// SPDX-License-Identifier: LGPL-2.0-or-later
// Breeze NG Aurora for Kirigami.
//
// Kirigami asks a platform plugin for the theme object of every item that wants colours, at the moment the item is
// made. This plugin carries the name of the desktop style's plugin and stands before it in the plugin path: it loads
// the real one, lets it make the theme, and (with the Aurora look on) gives the theme see-through backgrounds before
// handing it out. The pages are glass from the first frame; a window made with Qt Quick becomes see-through by its
// own means, because Kirigami's application window takes its colour from its theme.
//
//   pages, headers, every colour set but View   nothing (the window's glass shows)
//   the View set (lists, fields)                the veil of every other surface in Aurora
//   the window itself (Qt Quick windows)        the window's sheet, as the widget style paints it
//   what floats (popups, menus, dialogs, ...)   untouched: solid, as the colour scheme says
//
// BREEZE_NG_NO_QUICK=1 switches it off for an application.

#include <Kirigami/Platform/PlatformPluginFactory>
#include <Kirigami/Platform/PlatformTheme>
#include <Kirigami/Platform/Units>

#include <KConfigGroup>
#include <KSharedConfig>
#include <KWindowEffects>

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHash>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlListReference>
#include <QQmlProperty>
#include <QSet>
#include <QLibraryInfo>
#include <QMetaMethod>
#include <QFile>
#include <QGuiApplication>
#include <QPalette>
#include <QPluginLoader>
#include <QPointer>
#include <QQuickRenderControl>
#include <QTimer>
#include <functional>
#include <memory>
#include <QElapsedTimer>
#include <QWindow>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QQuickItem>
#include <QQuickPaintedItem>
#include <QQuickWindow>
#include <QRadialGradient>
#include <cmath>

#include <dlfcn.h>

#include "../auroraring.h"

using namespace Kirigami::Platform;

namespace
{
struct Look {
    bool aurora = false;
    QColor sheet; // the window's glass
    QColor veil; // what lists stand on
    QColor ground; // everything else: nothing, in the window's own colour
    bool dark = true;
    int body = 52; // settings of Breeze NG (breezengrc)
    int panel = 7;
    bool lightGlass = false; // "Breeze NG Aurora Light"
    bool cornerLights = true; // AuroraCornerLights
    qreal lights = 1.0; // strength of the lights behind the window
    int grain = 30; // the grain over them
    bool dim = true; // windows in the background dim their lights
    QColor second; // the second light, if the user chose its colour
    bool accentRight = false; // the accent's light from the top right, the second light from the top left
    bool fullSelection = false; // selections in the full accent with white text
    bool accentTint = true; // the text tinged by the accent, as the widget style's palette (Breeze NG's AccentTint)
    int plasmaPanel = 45; // the density of the mask of Plasma's panel (AuroraPlasmaPanel)
    int ring = 1; // how rings are drawn (AuroraRing: 0 plain, 1 gradient, 2 glow, 3 ticks)
    int accentText = 0; // titles in the accent (Breeze NG's AccentText: 1 accent, 2 gradient titles, 3 secondary titles too)
    QColor onSelection; // text on a selection, where it is not the scheme's own
    QColor listSheet; // a selected row of a list: the light sheet of the accent (dark schemes: the accent, see-through)
};

// Does qqc2-desktop-style draw a selected row of a list through the widget style (then Breeze NG's light sheet of the
// accent, KDE Frameworks 6.31 here), or as a rectangle of the full highlight colour (6.24, Kubuntu 26.04)? Read once
// from its QML. With the rectangle, the deep accent as the text of a selection was unreadable on the full accent
// (System Settings' sidebar): the rectangle takes the light sheet instead, as in Dolphin NG's sidebar.
bool listRowsStyled()
{
    static const bool styled = [] {
        const QStringList bases = QStringList{QLibraryInfo::path(QLibraryInfo::QmlImportsPath)} + qEnvironmentVariable("QML2_IMPORT_PATH").split(QLatin1Char(':'), Qt::SkipEmptyParts);
        for (const QString &base : bases) {
            QFile file(base + QStringLiteral("/org/kde/desktop/private/DefaultListItemBackground.qml"));
            if (file.open(QIODevice::ReadOnly)) {
                return file.readAll().contains("elementType: \"item\"");
            }
        }
        return true; // (not found: as on the newer Frameworks)
    }();
    return styled;
}

// The text of a theme tinged by the accent, as Style::polish(QPalette) tinges the widgets' text: near black leaning to
// the accent's hue (a light one only a hint). Kirigami takes its text from the colour scheme, not from the style's
// palette: in System Settings' sidebar the text was the scheme's grey beside the tinged text of Dolphin NG's.
// BREEZE_NG_TINTED_TEXT=0 leaves it, as in the widget style.
// AccentText 2 ("in an Aurora gradient") for a Kirigami title: a Text item takes no gradient as its colour, so the
// heading is drawn through a layer whose effect is a LinearGradient of Qt5Compat.GraphicalEffects, masked by the text
// itself, across the width of the text, from the accent to Aurora's second light, as the widget style's titles. (A layer:
// an item laid over the heading went into its ColumnLayout as one more row; the effect of a layer is left out of
// layouts.) Without Qt5Compat the heading keeps its plain colour.
void titleGradient(QQuickItem *heading, const QColor &first, const QColor &second)
{
    QPointer<QQuickItem> guard(heading);
    QTimer::singleShot(0, heading, [guard, first, second] {
        if (!guard) {
            return;
        }
        const bool had = guard->property("_breeze_ng_title_gradient").toBool();
        if (!first.isValid()) {
            if (had) {
                QQmlProperty::write(guard, QStringLiteral("layer.enabled"), false);
                guard->setProperty("_breeze_ng_title_gradient", QVariant());
            }
            return;
        }
        QQmlEngine *engine = qmlEngine(guard);
        if (!engine) {
            return;
        }
        // one holder per engine and pair of colours: a QML object whose property is the effect's component (a
        // component made from C++ has no creation context, and the layer, creating the effect in it, crashed)
        static QHash<QString, QObject *> holders;
        const QString key = QString::number(quintptr(engine)) + first.name() + second.name();
        QObject *holder = holders.value(key);
        if (!holder) {
            QQmlComponent maker(engine);
            maker.setData(QStringLiteral(R"(
import QtQuick
import Qt5Compat.GraphicalEffects
QtObject {
    property Component effect: Component {
        LinearGradient {
            start: Qt.point(0, 0)
            end: Qt.point(Math.max(1, Math.min(width, source && source.sourceItem && source.sourceItem.contentWidth > 0 ? source.sourceItem.contentWidth : width)), 0)
            gradient: Gradient {
                GradientStop { position: 0.0; color: "%1" }
                GradientStop { position: 1.0; color: "%2" }
            }
        }
    }
}
)").arg(first.name(), second.name()).toUtf8(), QUrl(QStringLiteral("breeze-ng:/TitleGradient.qml")));
            if (!maker.isReady()) {
                return; // (no Qt5Compat.GraphicalEffects: the plain colour stays)
            }
            holder = maker.create(engine->rootContext());
            if (!holder) {
                return;
            }
            holder->setParent(engine);
            holders.insert(key, holder);
            QObject::connect(holder, &QObject::destroyed, [key] {
                holders.remove(key);
            });
        }
        auto component = holder->property("effect").value<QQmlComponent *>();
        if (!component) {
            return;
        }
        if (guard->property("_breeze_ng_shadow").toBool()) { // (the halo off: it would be masked into the gradient)
            guard->setProperty("style", 0);
            guard->setProperty("_breeze_ng_shadow", false);
        }
        QQmlProperty::write(guard, QStringLiteral("layer.effect"), QVariant::fromValue(component), qmlContext(guard));
        QQmlProperty::write(guard, QStringLiteral("layer.enabled"), true, qmlContext(guard));
        guard->setProperty("_breeze_ng_title_gradient", true);
    });
}

// a Kirigami.Heading, or a type made from one (Plasma's notifications draw their title with Summary, a Heading)
bool isHeading(const QObject *object)
{
    for (const QMetaObject *meta = object ? object->metaObject() : nullptr; meta; meta = meta->superClass()) {
        if (QByteArray(meta->className()).startsWith("Heading")) {
            return true;
        }
    }
    return false;
}

// AccentText for a title: only a Kirigami.Heading of level 1, the title of a page, takes the accent (as the widget
// style, Helper::accentTitleColor), or Aurora's gradient through titleGradient. Every other heading is text:
// FormLayout's labels ("General:") are headings of level 5. The surfaces of the window's own colours only: not the
// dark ones of a light scheme (Complementary), not the text of a selection or a tooltip. True when the theme is a title's.
bool titleText(PlatformTheme *theme, int accentText, const QColor &secondLight, bool dark)
{
    const QObject *owner = theme->parent();
    const int headingLevel = (owner && owner->property("level").isValid()) ? owner->property("level").toInt() : 1;
    // (AccentText 3: the secondary titles too, headings down to level 4: a section's title, a list's header, the name
    // in the launcher, a notification's title. Level 5 stays text: FormLayout's labels)
    if (accentText <= 0 || !owner || headingLevel > (accentText == 3 ? 4 : 1) || !isHeading(owner)
        || !(theme->colorSet() == PlatformTheme::Window || theme->colorSet() == PlatformTheme::View || theme->colorSet() == PlatformTheme::Header)) {
        return false;
    }
    const QColor accent = QGuiApplication::palette().color(QPalette::Active, QPalette::Highlight);
    const qreal hue = accent.hslHueF() < 0 ? 0.58 : accent.hslHueF();
    const QColor colour = accentText >= 2 ? (dark ? QColor::fromHslF(hue, 0.90, 0.78) : QColor::fromHslF(hue, 0.70, 0.38))
                                          : (dark ? QColor::fromHslF(hue, 0.80, 0.90) : QColor::fromHslF(hue, 0.62, 0.31));
    if (auto heading = qobject_cast<QQuickItem *>(const_cast<QObject *>(owner))) {
        if (accentText >= 2) {
            // (the second light: the user's colour, or the hue next to the accent's, as Helper::accentTitleBrush)
            const qreal hue2 = (secondLight.isValid() && secondLight.hslHueF() >= 0) ? secondLight.hslHueF() : std::fmod(hue + 0.17, 1.0);
            titleGradient(heading, colour, dark ? QColor::fromHslF(hue2, 0.85, 0.80) : QColor::fromHslF(hue2, 0.62, 0.45));
        } else {
            titleGradient(heading, QColor(), QColor());
        }
    }
    if (theme->textColor() != colour) {
        theme->setCustomTextColor(colour);
    }
    theme->setProperty("_breeze_ng_tinted", true);
    return true;
}

// the gradient of a heading that is no longer a title (its level changed, the option is off, a grey accent)
void dropGradient(PlatformTheme *theme)
{
    if (auto heading = qobject_cast<QQuickItem *>(theme->parent()); heading && heading->property("_breeze_ng_title_gradient").toBool()) {
        titleGradient(heading, QColor(), QColor());
    }
}

// a theme whose text is left as it is (AccentTint off): only the titles take AccentText
void titleOnly(PlatformTheme *theme, int accentText, const QColor &secondLight)
{
    const QPalette palette = QGuiApplication::palette();
    const QColor accent = palette.color(QPalette::Active, QPalette::Highlight);
    const bool dark = palette.color(QPalette::Window).lightnessF() < 0.5;
    if (accent.hslSaturationF() >= 0.15 && qEnvironmentVariable("BREEZE_NG_TINTED_TEXT") != QLatin1String("0") && titleText(theme, accentText, secondLight, dark)) {
        return;
    }
    dropGradient(theme);
    if (theme->property("_breeze_ng_tinted").toBool()) {
        theme->setCustomTextColor();
        theme->setProperty("_breeze_ng_tinted", QVariant());
    }
}

void tintText(PlatformTheme *theme, int accentText, const QColor &secondLight)
{
    // (an icon of a button that is on has its text colour set by checkedIcon: left to it)
    if (theme->property("_breeze_ng_checked_icon").toBool()) {
        return;
    }
    // the tinge set here before is dropped whenever the theme no longer takes it (its colour set changed to one that
    // is not tinged, as Complementary: the dark text of a light window stayed on its dark surface)
    auto release = [theme] {
        dropGradient(theme);
        if (theme->property("_breeze_ng_tinted").toBool()) {
            theme->setCustomTextColor();
            theme->setProperty("_breeze_ng_tinted", QVariant());
        }
    };
    const QPalette palette = QGuiApplication::palette();
    const QColor accent = palette.color(QPalette::Active, QPalette::Highlight);
    if (accent.hslSaturationF() < 0.15 || qEnvironmentVariable("BREEZE_NG_TINTED_TEXT") == QLatin1String("0")) {
        release();
        return;
    }
    const bool dark = palette.color(QPalette::Window).lightnessF() < 0.5;
    if (titleText(theme, accentText, secondLight, dark)) {
        return;
    }
    dropGradient(theme);
    // (titles in Aurora's gradient: the rest of the text plain, as the widget style leaves its palette)
    if (accentText >= 2) {
        release();
        return;
    }
    QPalette::ColorRole role;
    switch (theme->colorSet()) {
    case PlatformTheme::View:
        role = QPalette::Text;
        break;
    case PlatformTheme::Button:
        role = QPalette::ButtonText;
        break;
    case PlatformTheme::Window:
    case PlatformTheme::Header:
        role = QPalette::WindowText;
        break;
    default:
        release();
        return;
    }
    // (from the palette, not from the theme's own colour: read back right after a change it was not there yet, and the
    // text came out empty. In an application of widgets the palette is the style's, already tinged: tinging is the same)
    const QColor text = palette.color(QPalette::Active, role);
    if (!text.isValid() || text.alpha() < 128 || text.hslSaturationF() > 0.35) {
        release(); // (coloured text is left as it is)
        return;
    }
    if (text.lightnessF() >= 0.5) {
        release(); // (the light text of a dark scheme stays white, as the widget style leaves it)
        return;
    }
    QColor tinted = QColor::fromHslF(accent.hslHueF(), 0.24, text.lightnessF());
    tinted.setAlphaF(text.alphaF());
    if (theme->textColor() != tinted) {
        theme->setCustomTextColor(tinted);
    }
    theme->setProperty("_breeze_ng_tinted", true);
}

// The colours that depend on the palette, worked out when they are used: the plugin is made very early (at login,
// the first application was seen to get a light grey sheet: the palette of the colour scheme was not there yet).
void colours(Look &look)
{
    const QColor window = QGuiApplication::palette().color(QPalette::Window);
    const bool dark = window.lightnessF() < 0.5;
    look.dark = dark;
    look.sheet = window;
    look.sheet.setAlphaF(qBound(10, look.body - ((dark && look.lightGlass) ? 22 : 0), 100) / 100.0); // as Helper::windowSheetAlpha in the widget style
    // Some components take a background colour and make it solid, or mix it with the text colour: "nothing" is the
    // window's colour with no alpha. The veil is ink, as in the widget style.
    look.ground = window;
    look.ground.setAlpha(0);
    // (light schemes: frost, white and denser, as Helper::auroraVeilColor: black ink on light glass read as grey)
    look.veil = QColor(255, 255, 255);
    look.veil.setAlphaF(dark ? look.panel / 100.0 : qMin(look.panel / 100.0 * 0.75 * 8.8, 0.92));
    // a selected row is a light sheet of the accent: on a light scheme its text is the deep accent, not white
    look.onSelection = QColor();
    if (!dark && !look.fullSelection) {
        const QColor highlight = QGuiApplication::palette().color(QPalette::Active, QPalette::Highlight);
        look.onSelection = QColor::fromHslF(highlight.hslHueF() < 0 ? 0.58 : highlight.hslHueF(), (highlight.hslSaturationF() < 0.15 ? highlight.hslSaturationF() : qBound(0.55, highlight.hslSaturationF() * 1.3, 0.90)), 0.38);
    }
    // (as Helper::auroraSelection in the widget style: white at 0.55 with the accent at 0.22 over it, as one colour)
    {
        const QColor highlight = QGuiApplication::palette().color(QPalette::Active, QPalette::Highlight);
        const QColor accent = QColor::fromHslF(highlight.hslHueF() < 0 ? 0.58 : highlight.hslHueF(),
                                               (highlight.hslSaturationF() < 0.15 ? highlight.hslSaturationF() : qBound(0.55, highlight.hslSaturationF() * 1.3, 0.90)),
                                               dark ? 0.66 : 0.52);
        if (dark) {
            look.listSheet = accent;
            look.listSheet.setAlphaF(0.34);
        } else {
            const qreal a = 0.22, w = 0.55 * (1.0 - a), alpha = w + a;
            look.listSheet = QColor::fromRgbF((w + accent.redF() * a) / alpha, (w + accent.greenF() * a) / alpha, (w + accent.blueF() * a) / alpha, alpha);
        }
    }
}

Look readLook()
{
    Look look;
    if (qEnvironmentVariableIsSet("BREEZE_NG_NO_QUICK")) {
        return look;
    }
    // the look is the window decoration's theme, as the widget style reads it; and the widget style must be Breeze NG
    const QString theme = KSharedConfig::openConfig(QStringLiteral("kwinrc"))->group(QStringLiteral("org.kde.kdecoration2")).readEntry("theme", QString());
    const QString style = KSharedConfig::openConfig()->group(QStringLiteral("KDE")).readEntry("widgetStyle", QString());
    look.aurora = theme.startsWith(QLatin1String("Breeze NG Aurora"), Qt::CaseInsensitive) && style.compare(QLatin1String("breezeng"), Qt::CaseInsensitive) == 0;
    const bool lightGlass = theme.endsWith(QLatin1String("Light"), Qt::CaseInsensitive);
    if (!look.aurora) {
        return look;
    }
    const KConfigGroup common = KSharedConfig::openConfig(QStringLiteral("breezengrc"))->group(QStringLiteral("Common"));
    look.body = qBound(10, common.readEntry("AuroraBodyOpacity", 60), 100);
    look.panel = qBound(0, common.readEntry("AuroraPanelVeil", 7), 40);
    look.lightGlass = lightGlass;
    look.lights = qBound(0, common.readEntry("AuroraLights", 100), 180) / 100.0;
    look.grain = qBound(0, common.readEntry("AuroraGrain", 30), 100);
    look.dim = common.readEntry("AuroraDimInactive", true);
    look.fullSelection = common.readEntry("AuroraFullSelection", false);
    look.accentTint = common.readEntry("AccentTint", true);
    look.accentText = qBound(0, common.readEntry("AccentText", 0), 3);
    look.plasmaPanel = qBound(0, common.readEntry("AuroraPlasmaPanel", 45), 100);
    look.ring = qBound(0, common.readEntry("AuroraRing", 1), 3);
    const QString secondAccent = common.readEntry("AuroraSecondAccent", QString());
    look.second = secondAccent.isEmpty() ? QColor() : QColor(secondAccent);
    look.accentRight = common.readEntry("AuroraAccentRight", false);
    look.cornerLights = common.readEntry("AuroraCornerLights", true);
    colours(look);
    return look;
}

// what floats over the page keeps the solid colours of the scheme; a drawer that stays open beside the page (not
// modal) is a sidebar: glass, like the page (its items sit inside popup items too: the drawer decides)
bool floats(const QObject *object)
{
    bool floating = false;
    for (int depth = 0; object && depth < 64; object = object->parent(), ++depth) {
        const QByteArray name = object->metaObject()->className();
        if (name.contains("Drawer")) {
            const QVariant modal = object->property("modal");
            if (modal.isValid()) {
                return modal.toBool();
            }
            floating = true;
        } else if (name.contains("Popup") || name.contains("Dialog") || name.contains("Menu") || name.contains("ToolTip") || name.contains("Overlay") || name.contains("Sheet")) {
            floating = true;
        }
    }
    return floating;
}

// an item of the View set inside another one already stands on its veil: a second veil would make it lighter
bool onVeil(const QObject *owner)
{
    for (const QObject *object = owner ? owner->parent() : nullptr; object; object = object->parent()) {
        const auto children = object->children();
        for (const QObject *child : children) {
            const int kind = child->inherits("Kirigami::Platform::PlatformTheme") ? child->property("_breeze_ng_glass").toInt() : 0;
            if (kind == 1 || kind == 5) {
                return true;
            }
        }
    }
    return false;
}

// the grain of Aurora: a tile of faint light and dark specks
const QPixmap &auroraGrainTile(int grain)
{
    static QPixmap tile;
    static int tileGrain = -1;
    if (tile.isNull() || tileGrain != grain) {
        tileGrain = grain;
        const int reach = 2 + grain / 10;
        QImage image(128, 128, QImage::Format_ARGB32_Premultiplied);
        quint32 seed = 0x9e3779b9u;
        for (int y = 0; y < image.height(); ++y) {
            QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                seed ^= seed << 13;
                seed ^= seed >> 17;
                seed ^= seed << 5;
                const int value = int(seed % quint32(2 * reach + 1)) - reach;
                line[x] = value >= 0 ? qPremultiply(qRgba(255, 255, 255, value)) : qPremultiply(qRgba(0, 0, 0, -value));
            }
        }
        tile = QPixmap::fromImage(image);
    }
    return tile;
}

// The lights of Aurora (and the window's sheet, when asked) over a rectangle: AuroraBackdrop behind a Qt Quick
// window, PlasmaLights over the sheet of a Plasma popup
void paintAuroraLights(QPainter *painter, const QRectF &rect, Look &m_look, bool active, bool sheet)
{
    colours(m_look); // (with the palette of now)
    if (sheet) {
        painter->fillRect(rect, m_look.sheet); // the window's glass
    }
    const qreal strength = (m_look.dark ? 1.0 : 0.95) * ((active || !m_look.dim) ? 1.0 : 0.6) * m_look.lights;
    if (strength > 0.0) {
        const QColor highlight = QGuiApplication::palette().color(QPalette::Active, QPalette::Highlight);
        const qreal hue = highlight.hslHueF() < 0 ? 0.58 : highlight.hslHueF();
        const qreal saturation = (highlight.hslSaturationF() < 0.15 ? highlight.hslSaturationF() : qBound(0.60, highlight.hslSaturationF() * 1.4, 0.95));
        // (as Helper::renderAuroraLights: 0.76 / 0.78 on light schemes. This copy had stayed at 0.90, nearly white
        // lights, and a Qt Quick window came out lighter and plainer than its own title bar)
        const QColor accent = QColor::fromHslF(hue, saturation, (m_look.dark) ? 0.62 : 0.76);
        QColor second = QColor::fromHslF(std::fmod(hue + 0.17, 1.0), saturation, (m_look.dark) ? 0.64 : 0.78);
        if (m_look.second.isValid()) {
            second = QColor::fromHslF(m_look.second.hslHueF() < 0 ? hue : m_look.second.hslHueF(), (m_look.second.hslSaturationF() < 0.15 ? m_look.second.hslSaturationF() : qBound(0.60, m_look.second.hslSaturationF() * 1.4, 0.95)), (m_look.dark) ? 0.64 : 0.78);
        }
        const qreal radius = qBound(260.0, rect.width() * 0.55, 620.0);
        auto light = [&](const QPointF &centre, qreal r, QColor colour, qreal alpha) {
            QRadialGradient gradient(centre, r);
            colour.setAlphaF(qMin(1.0, alpha * strength));
            gradient.setColorAt(0.0, colour);
            colour.setAlphaF(qMin(1.0, alpha * strength * 0.46));
            gradient.setColorAt(0.45, colour);
            colour.setAlphaF(0.0);
            gradient.setColorAt(1.0, colour);
            painter->fillRect(rect, gradient);
        };
        if (!m_look.dark) { // (as Helper::renderAuroraLights: the wash of the accent on light schemes)
            painter->fillRect(rect, QColor(255, 255, 255, 120));
            QColor wash = accent;
            wash.setAlphaF(qMin(1.0, 0.10 * strength));
            painter->fillRect(rect, wash);
        }
        if (m_look.cornerLights) { // (the corners' lights off: the sheet, the wash and the grain only)
            light(m_look.accentRight ? rect.topRight() : rect.topLeft(), radius, accent, 0.52); // (as the widget style)
            light(m_look.accentRight ? rect.topLeft() : rect.topRight(), radius * 0.8, second, 0.36);
            light(m_look.accentRight ? rect.bottomLeft() : rect.bottomRight(), radius * 0.9, accent, 0.32);
        }
    }
    if (m_look.grain > 0) {
        painter->drawTiledPixmap(rect, auroraGrainTile(m_look.grain));
    }
}

// The lights and the grain of Aurora behind the content of a Qt Quick window, as the widget style paints them in its
// windows (Helper::renderAuroraLights) and the window decoration in the title bar: the same centres, so that the
// title bar and the window are one sheet.
class AuroraBackdrop : public QQuickPaintedItem
{
public:
    AuroraBackdrop(QQuickWindow *window, Look *look, QObject *notifier)
        : QQuickPaintedItem(window->contentItem())
        , m_window(window)
        , m_look(look)
    {
        setZ(-100000);
        setAntialiasing(false);
        QObject::connect(notifier, SIGNAL(changed()), this, SLOT(update()));
        auto fit = [this] {
            setSize(QSizeF(m_window->width(), m_window->height()));
            update();
        };
        connect(window, &QWindow::widthChanged, this, fit);
        connect(window, &QWindow::heightChanged, this, fit);
        connect(window, &QWindow::activeChanged, this, [this] {
            update();
        });
        fit();
    }

    void paint(QPainter *painter) override
    {
        paintAuroraLights(painter, QRectF(0, 0, width(), height()), *m_look, m_window->isActive(), true);
    }

private:
    QQuickWindow *m_window;
    Look *m_look; // (the plugin's own: a change of the settings reaches what is already shown)
};

// What the plugin lays into other people's scenes (lights, cards, sheets): taken out again before the application
// tears its windows down. Left in, plasmashell crashed now and then when it stopped (in the destruction of a system
// tray applet, an item taking leave of a window that was gone)
QList<QPointer<QQuickItem>> &injected()
{
    static QList<QPointer<QQuickItem>> items;
    return items;
}
void inject(QQuickItem *item)
{
    static bool hooked = false;
    if (!hooked) {
        hooked = true;
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, [] {
            const auto items = injected();
            injected().clear();
            for (const QPointer<QQuickItem> &one : items) {
                if (one) {
                    one->setParentItem(nullptr);
                    delete one.data();
                }
            }
        }, Qt::DirectConnection);
    }
    injected().removeAll(QPointer<QQuickItem>());
    injected().append(item);
}

// The lights of Aurora in a popup of Plasma (the launcher, the system tray, the calendar, a notification), as in the
// windows: laid on the popup's own background (the Plasma style's sheet), under its content, inside its rounded
// corners. The sheet itself stays the Plasma style's.
class PlasmaLights : public QQuickPaintedItem
{
public:
    PlasmaLights(QQuickItem *background, Look *look, QObject *notifier, qreal radius, bool sheet = true)
        : QQuickPaintedItem(background)
        , m_look(look)
        , m_radius(radius)
        , m_sheet(sheet)
    {
        setAntialiasing(true);
        setObjectName(QStringLiteral("breeze-ng-plasma-lights"));
        QObject::connect(notifier, SIGNAL(changed()), this, SLOT(update()));
        auto fit = [this, background] {
            setSize(QSizeF(background->width(), background->height()));
            update();
        };
        if (background->metaObject()->indexOfSignal("enabledBordersChanged()") >= 0) {
            connect(background, SIGNAL(enabledBordersChanged()), this, SLOT(update()));
        }
        connect(background, &QQuickItem::widthChanged, this, fit);
        connect(background, &QQuickItem::heightChanged, this, fit);
        fit();
    }


    void paint(QPainter *painter) override
    {
        const QRectF rect(0, 0, width(), height());
        QPainterPath shape;
        if (!m_sheet) {
            // (a floating panel has all four borders and round corners: the overlay keeps inside them)
            const QQuickItem *frame = parentItem();
            const QVariant borders = frame ? frame->property("enabledBorders") : QVariant();
            if (borders.isValid() && borders.toInt() == 15) {
                QPainterPath round;
                round.addRoundedRect(rect.adjusted(1, 1, -1, -1), 13, 13);
                painter->setRenderHint(QPainter::Antialiasing, true);
                painter->setClipPath(round);
            }
            // a panel: its mask (the window's colour lifted towards white: frost on a light scheme, a lighter glass on
            // a dark one) at the density of the settings, then the lights and the grain
            colours(*m_look);
            const QColor window = QGuiApplication::palette().color(QPalette::Window);
            const qreal lift = m_look->dark ? 0.22 : 0.55;
            QColor mask = QColor::fromRgbF(window.redF() + (1.0 - window.redF()) * lift, window.greenF() + (1.0 - window.greenF()) * lift,
                                           window.blueF() + (1.0 - window.blueF()) * lift);
            // (the Plasma style's own panel sheet is the floor, about 41 %, readable without this plugin: the mask
            // adds what is missing to the density of the settings)
            const qreal floor = 0.41;
            mask.setAlphaF(qBound(0.0, (m_look->plasmaPanel / 100.0 - floor) / (1.0 - floor), 1.0));
            painter->fillRect(rect, mask);
            paintAuroraLights(painter, rect, *m_look, true, false);
            return;
        }
        shape.addRoundedRect(rect.adjusted(1, 1, -1, -1), m_radius - 1, m_radius - 1);
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setClipPath(shape);
        // the window's glass, as AuroraBackdrop lays it: over the Plasma style's faint sheet (make-plasma-style.py
        // AURORA_POPUP_SVG), together as dense as a window's body
        colours(*m_look);
        QColor sheet = m_look->sheet;
        const qreal svg = 0.20;
        sheet.setAlphaF(qBound(0.0, 1.0 - (1.0 - m_look->sheet.alphaF()) / (1.0 - svg), 1.0));
        painter->fillRect(rect, sheet);
        paintAuroraLights(painter, rect, *m_look, true, false);
    }

private:
    Look *m_look; // (the plugin's own: a change of the settings reaches what is already shown)
    qreal m_radius;
    bool m_sheet;
};

// A card of glass under a list of a Plasma popup (the launcher's sidebar, a system tray page's list), as the widget
// style lays under a sidebar (Helper::renderAuroraCard): lighter and denser at the top, a hairline all round. Under
// the list's content (a child with a negative z), a little larger than the list
class PlasmaCard : public QQuickPaintedItem
{
public:
    PlasmaCard(QQuickItem *list, Look *look, QObject *notifier, qreal reach)
        : QQuickPaintedItem(list)
        , m_look(look)
    {
        setZ(-1);
        setAntialiasing(true);
        setObjectName(QStringLiteral("breeze-ng-plasma-card"));
        QObject::connect(notifier, SIGNAL(changed()), this, SLOT(update()));
        auto fit = [this, list, reach] {
            setPosition(QPointF(-reach, -reach));
            setSize(QSizeF(list->width() + 2 * reach, list->height() + 2 * reach));
            update();
        };
        connect(list, &QQuickItem::widthChanged, this, fit);
        connect(list, &QQuickItem::heightChanged, this, fit);
        fit();
    }

    void paint(QPainter *painter) override
    {
        colours(*m_look);
        const QRectF rect = QRectF(0, 0, width(), height()).adjusted(0.5, 0.5, -0.5, -0.5);
        QLinearGradient fill(rect.topLeft(), rect.bottomLeft());
        QColor edge;
        if (m_look->dark) {
            // (from the scheme's window colour, darker towards the bottom: a neutral or warm scheme keeps its tone)
            QColor top = QGuiApplication::palette().color(QPalette::Active, QPalette::Window).darker(130);
            QColor bottom = top.darker(600);
            top.setAlpha(140);
            bottom.setAlpha(107);
            fill.setColorAt(0.0, top);
            fill.setColorAt(1.0, bottom);
            edge = QColor(255, 255, 255, 22);
        } else {
            // (a veil over the popup's lights, not a sheet: denser, it turned a list that fills its popup white and
            // the lights showed only round the title)
            fill.setColorAt(0.0, QColor(255, 255, 255, 84));
            fill.setColorAt(1.0, QColor(255, 255, 255, 26));
            edge = QColor(255, 255, 255, 150);
        }
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setBrush(fill);
        painter->setPen(QPen(edge, 1));
        painter->drawRoundedRect(rect, 10, 10);
    }

private:
    Look *m_look; // (the plugin's own: a change of the settings reaches what is already shown)
};

// debugging: BREEZE_NG_DEBUG_QUICK=1 lists the large coloured items of each window a few seconds after it opens
// debugging (BREEZE_NG_DEBUG_QUICK=3): the tree of a Plasma popup, with the text items' level, weight and colour
void dumpTree(QQuickItem *item, int depth)
{
    if (!item || depth > 40) {
        return;
    }
    const QVariant text = item->property("text");
    const QVariant level = item->property("level");
    const QFont font = item->property("font").value<QFont>();
    const QVariant colour = item->property("color");
    const QVariant image = item->property("imagePath");
    const QVariant prefix = item->property("prefix");
    qWarning("breeze-ng tree: %*s%s %dx%d op %.2f%s%s%s%s%s", depth * 2, "", item->metaObject()->className(), int(item->width()), int(item->height()), item->opacity(),
             text.isValid() ? qPrintable(QStringLiteral(" text=\"%1\"").arg(text.toString().left(40))) : "",
             level.isValid() ? qPrintable(QStringLiteral(" level=%1").arg(level.toInt())) : "",
             text.isValid() ? qPrintable(QStringLiteral(" weight=%1").arg(font.weight())) : "",
             (colour.isValid() && !text.isValid()) ? qPrintable(QStringLiteral(" color=%1").arg(colour.value<QColor>().name(QColor::HexArgb))) : "",
             image.isValid() ? qPrintable(QStringLiteral(" svg=%1:%2").arg(image.toString(), prefix.toString())) : "");
    const auto children = item->childItems();
    for (QQuickItem *child : children) {
        dumpTree(child, depth + 1);
    }
}

void dumpItems(QQuickItem *item, int depth)
{
    if (!item || depth > 80) {
        return;
    }
    const QVariant colour = item->property("color");
    static const bool thin = qgetenv("BREEZE_NG_DEBUG_QUICK") == "4"; // thin coloured items (lines) with their parents
    if (thin && item->isVisible() && item->width() > 100 && item->height() <= 3 && item->height() > 0 && colour.isValid()) {
        QByteArray chain;
        for (const QQuickItem *o = item; o && chain.size() < 400; o = o->parentItem()) {
            chain += QByteArray(o->metaObject()->className()) + " < ";
        }
        qWarning("breeze-ng kirigami line: %dx%d at y %d color %s opacity %.2f in %s", int(item->width()), int(item->height()), int(item->mapToScene(QPointF(0, 0)).y()),
                 qPrintable(colour.value<QColor>().name(QColor::HexArgb)), item->opacity(), chain.constData());
    }
    static const bool all = qgetenv("BREEZE_NG_DEBUG_QUICK") == "2"; // every large item, coloured or not
    if (all && item->isVisible() && item->width() > 900 && item->height() > 300) {
        qWarning("breeze-ng kirigami big: %s %dx%d flags %d opacity %.2f clip %d color %s", item->metaObject()->className(), int(item->width()), int(item->height()),
                 int(item->flags()), item->opacity(), item->clip(), colour.isValid() ? qPrintable(colour.value<QColor>().name(QColor::HexArgb)) : "-");
    }
    if (item->isVisible() && item->width() > 100 && item->height() > 60 && colour.isValid() && colour.value<QColor>().alpha() > 0) {
        QByteArray chain;
        for (const QObject *o = item; o && chain.size() < 900; o = o->parent()) {
            chain += o->metaObject()->className();
            for (const QObject *c : o->children()) {
                if (c->inherits("Kirigami::Platform::PlatformTheme")) {
                    chain += "[theme set " + QByteArray::number(c->property("colorSet").toInt()) + " kind " + QByteArray::number(c->property("_breeze_ng_glass").toInt()) + " inherit "
                        + QByteArray::number(c->property("inherit").toBool()) + " bg " + c->property("backgroundColor").value<QColor>().name(QColor::HexArgb).toLatin1() + "]";
                }
            }
            chain += " < ";
        }
        qWarning("breeze-ng kirigami item: %s %dx%d color %s opacity %.2f in %s", item->metaObject()->className(), int(item->width()), int(item->height()),
                 qPrintable(colour.value<QColor>().name(QColor::HexArgb)), item->opacity(), chain.constData());
    }
    const auto children = item->childItems();
    for (QQuickItem *child : children) {
        dumpItems(child, depth + 1);
    }
}

// the hooks already set on a window (bits: 1 air, 2 cover, 4 shadow): kept here, not as dynamic properties of the
// window. Setting one sends the window a DynamicPropertyChange event, and Plasma's PlasmaQuick::Dialog, met before it
// had its main item, crashed on it: plasmashell would not start on Kubuntu 26.04 with this plugin in QT_PLUGIN_PATH.
QHash<const QObject *, int> &windowHooks()
{
    static QHash<const QObject *, int> hooks;
    return hooks;
}

bool hasHook(const QQuickWindow *window, int bit)
{
    return windowHooks().value(window) & bit;
}

void setHook(QQuickWindow *window, int bit)
{
    auto &hooks = windowHooks();
    if (!hooks.contains(window)) {
        QObject::connect(window, &QObject::destroyed, qApp, [window] {
            windowHooks().remove(window);
        });
    }
    hooks[window] |= bit;
}

// is this object in a Qt Quick view that the widget style treats as a sidebar (it marks the view's offscreen window)?
bool sideView(const QObject *owner)
{
    for (const QObject *object = owner; object; object = object->parent()) {
        if (auto item = qobject_cast<const QQuickItem *>(object)) {
            QQuickWindow *window = item->window();
            if (!window) {
                return false;
            }
            if (window->property("_breeze_ng_side").toBool()) {
                return true; // (the widget style says so, when it has looked: it may be later than this)
            }
            // the same rule as the widget style's, from here: a narrow, tall view at the side of the window it is shown in
            const QWindow *host = QQuickRenderControl::renderWindowFor(window);
            return host && host != window && window->width() * 5 <= host->width() * 2 && window->height() * 2 > host->height();
        }
    }
    return false;
}

// Is this the theme of an item that fills (nearly) the window it is in? That is a page, not a card on it: on a light
// scheme, where the veil is dense frost, a veil over the whole page (and another over the list inside it) made the
// window a white slab under a tinted title bar.
bool fillsWindow(const QObject *owner)
{
    for (const QObject *object = owner; object; object = object->parent()) {
        if (auto item = qobject_cast<const QQuickItem *>(object)) {
            const QQuickWindow *window = item->window();
            return window && item->width() >= window->width() * 0.8 && item->height() >= window->height() * 0.6;
        }
    }
    return false;
}

// A row of a sidebar (an item delegate in a side view): air around its selection, as the rows of Dolphin NG's sidebar,
// which stand 8 px in from the card's edges with their text further in. The delegates fill the list's width and paint
// their background edge to edge: insets move the background in, padding moves the content with it.
// Breeze NG's SidebarCompact: the rows of a sidebar compact, as Dolphin NG's (one line, a small icon)
bool compactSide()
{
    static const bool on = KSharedConfig::openConfig(QStringLiteral("breezengrc"))->group(QStringLiteral("Common")).readEntry("SidebarCompact", true);
    return on;
}

void giveAir(QObject *owner)
{
    owner->setProperty("_breeze_ng_air", true);
    const qreal inset = 8;
    for (const char *name : {"leftInset", "rightInset"}) {
        owner->setProperty(name, inset);
    }
    for (const char *name : {"topInset", "bottomInset"}) {
        owner->setProperty(name, 1.0);
    }
    // (once: a row given its air again after its insets were reset may have kept its padding, which already holds
    // the inset: the padding we set is remembered, and added to only when the row has another one now)
    for (const char *name : {"leftPadding", "rightPadding"}) {
        const QByteArray mark = QByteArray("_breeze_ng_padded_") + name;
        const qreal now = owner->property(name).toReal();
        if (!owner->property(mark).isValid() || owner->property(mark).toReal() != now) {
            owner->setProperty(name, now + inset);
            owner->setProperty(mark, now + inset);
        }
    }
    if (compactSide()) {
        for (const char *name : {"topPadding", "bottomPadding"}) {
            owner->setProperty(name, 7.0); // (rows of 32 px, as Dolphin NG's)
        }
        for (const char *name : {"icon.width", "icon.height"}) {
            QQmlProperty(owner, QString::fromLatin1(name)).write(16);
        }
    }
}

// The delegates do not ask for a theme of their own, and a list makes them as it scrolls: once a sidebar's window is
// known, its rows are looked at after its animations, now and then
void sideRowAir(QObject *owner)
{
    if (!owner || !sideView(owner)) {
        return;
    }
    const auto item = qobject_cast<QQuickItem *>(owner);
    QQuickWindow *window = item ? item->window() : nullptr;
    if (!window || hasHook(window, 1)) {
        return;
    }
    setHook(window, 1);
    auto walk = [window] {
        std::function<void(QQuickItem *, int)> visit = [&](QQuickItem *node, int depth) {
            // (given again when its insets are not ours any more: a list reuses its rows as it scrolls or reloads,
            // and a reused row got its own insets back while keeping our mark: its icon stood further left)
            if (node->inherits("QQuickItemDelegate") && (!node->property("_breeze_ng_air").toBool() || node->property("leftInset").toReal() != node->property("_breeze_ng_air_at").toReal())) {
                node->setProperty("_breeze_ng_air", true);
                giveAir(node);
                node->setProperty("_breeze_ng_air_at", node->property("leftInset").toReal());
            }
            if (depth < 40) {
                const auto children = node->childItems();
                for (QQuickItem *child : children) {
                    visit(child, depth + 1);
                }
            }
        };
        visit(window->contentItem(), 0);
    };
    auto last = std::make_shared<QElapsedTimer>();
    auto pending = std::make_shared<bool>(false);
    QPointer<QQuickWindow> guard(window);
    QObject::connect(window, &QQuickWindow::afterAnimating, window, [walk, last, pending, guard] {
        if (!last->isValid() || last->elapsed() > 250) {
            last->start();
            walk();
        } else if (!*pending && guard) {
            // (a frame inside the pause: looked at once more when it ends. A row made by the last frames of a
            // page's loading was otherwise never seen, and stood without its air, its icon further left)
            *pending = true;
            QTimer::singleShot(300, guard, [walk, last, pending] {
                *pending = false;
                last->start();
                walk();
            });
        }
    });
    walk();
}

// Is this item the background of the control above it (a card's DefaultCardBackground, a form card's rounded sheet)?
// Under a veiled control only its background takes the veil: content inside it (a table in a card) stands on that
// veil already, and a second one made it a lighter block.
bool isBackground(const QObject *owner)
{
    const auto item = qobject_cast<const QQuickItem *>(owner);
    if (!item) {
        return false;
    }
    const QByteArray name = item->metaObject()->className();
    if (name.contains("Background") || name.startsWith("ShadowedRectangle")) {
        return true;
    }
    const QQuickItem *parent = item->parentItem();
    return parent && parent->property("background").value<QQuickItem *>() == item;
}

// Calls a function when a signal found by name is sent (a signal of a QML type, unknown at compile time)
// The sheet of a Kirigami inline message: a gradient of the message's colour, more of it at the icon's side, fading
// to nearly white (a dark scheme: to nearly nothing), in place of the block of colour Kirigami draws
class MessageSheet : public QQuickPaintedItem
{
public:
    explicit MessageSheet(QQuickItem *background)
        : QQuickPaintedItem(background)
    {
        setAntialiasing(true);
        auto fit = [this, background] {
            setSize(QSizeF(background->width(), background->height()));
            update();
        };
        connect(background, &QQuickItem::widthChanged, this, fit);
        connect(background, &QQuickItem::heightChanged, this, fit);
        fit();
    }
    // the message's own icon (an emblem of the icon theme, a flat square or triangle) gives way to a badge drawn
    // here, where it stood: a disc of the message's colour with a white sign
    void setBadge(QQuickItem *icon, int type)
    {
        m_type = type;
        if (m_icon == icon) {
            update();
            return;
        }
        m_icon = icon;
        if (icon) {
            icon->setOpacity(0.0);
            connect(icon, &QQuickItem::xChanged, this, [this] {
                update();
            });
            connect(icon, &QQuickItem::yChanged, this, [this] {
                update();
            });
        }
        update();
    }
    void setInset(qreal x, qreal y)
    {
        m_insetX = x;
        m_insetY = y;
        update();
    }
    bool hasKind() const
    {
        return m_kind.isValid();
    }
    void setKind(const QColor &kind, qreal radius)
    {
        m_kind = kind;
        m_radius = radius;
        update();
    }
    void paint(QPainter *painter) override
    {
        if (!m_kind.isValid()) {
            return;
        }
        const bool dark = QGuiApplication::palette().color(QPalette::Window).lightnessF() < 0.5;
        const bool reversed = QGuiApplication::layoutDirection() == Qt::RightToLeft;
        const QRectF rect = QRectF(0, 0, width(), height()).adjusted(m_insetX, m_insetY, -m_insetX, -m_insetY);
        const qreal m_radius = (m_insetX > 0 && this->m_radius < 1) ? 8 : this->m_radius;
        // a card of glass, as the panels (lighter and denser at the top), and the message's colour over it,
        // see-through: more at the icon's side, a breath at the other; a hairline of the colour round it
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        QLinearGradient card(rect.topLeft(), rect.bottomLeft());
        card.setColorAt(0.0, dark ? QColor(30, 34, 52, 110) : QColor(255, 255, 255, 110));
        card.setColorAt(1.0, dark ? QColor(4, 5, 12, 80) : QColor(255, 255, 255, 50));
        painter->setBrush(card);
        painter->drawRoundedRect(rect, m_radius, m_radius);
        auto kind = [&](qreal alpha) {
            QColor c = m_kind;
            c.setAlphaF(alpha);
            return c;
        };
        QLinearGradient fill(reversed ? rect.topRight() : rect.topLeft(), reversed ? rect.topLeft() : rect.topRight());
        fill.setColorAt(0.0, kind(dark ? 0.42 : 0.34));
        fill.setColorAt(1.0, kind(dark ? 0.12 : 0.10));
        painter->setBrush(fill);
        painter->setPen(QPen(kind(dark ? 0.45 : 0.30), 1));
        painter->drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), m_radius, m_radius);
        if (m_icon && m_icon->width() > 4) {
            const QRectF disc(m_icon->mapToItem(this, QPointF(0, 0)), QSizeF(m_icon->width(), m_icon->height()));
            QLinearGradient body(disc.topLeft(), disc.bottomLeft());
            body.setColorAt(0.0, m_kind.lighter(118));
            body.setColorAt(1.0, m_kind);
            painter->setPen(Qt::NoPen);
            painter->setBrush(body);
            painter->drawEllipse(disc);
            const QPointF c = disc.center();
            const qreal u = disc.width() / 22.0;
            painter->setPen(QPen(QColor(255, 255, 255), 2.0 * u, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->setBrush(Qt::NoBrush);
            switch (m_type) {
            case 1: // positive: a tick
                painter->drawPolyline(QPolygonF({c + QPointF(-4.5, 0.5) * u, c + QPointF(-1.5, 3.5) * u, c + QPointF(4.5, -3.5) * u}));
                break;
            case 2: // warning: an exclamation mark
                painter->drawLine(c + QPointF(0, -5) * u, c + QPointF(0, 1) * u);
                painter->drawPoint(c + QPointF(0, 4.6) * u);
                break;
            case 3: // error: a cross
                painter->drawLine(c + QPointF(-3.5, -3.5) * u, c + QPointF(3.5, 3.5) * u);
                painter->drawLine(c + QPointF(3.5, -3.5) * u, c + QPointF(-3.5, 3.5) * u);
                break;
            default: // information: an i
                painter->drawPoint(c + QPointF(0, -4.6) * u);
                painter->drawLine(c + QPointF(0, -1) * u, c + QPointF(0, 5) * u);
                break;
            }
        }
    }

private:
    QColor m_kind;
    qreal m_radius = 0;
    qreal m_insetX = 0, m_insetY = 0;
    QPointer<QQuickItem> m_icon;
    int m_type = 0;
};

class Relay : public QObject
{
    Q_OBJECT
public:
    Relay(QObject *parent, std::function<void()> call)
        : QObject(parent)
        , m_call(std::move(call))
    {
    }
public Q_SLOTS:
    void fire()
    {
        m_call();
    }

private:
    std::function<void()> m_call;
};

// A ring chart of KQuickCharts with one value (System Monitor's gauges: CPU, memory) drawn as the rings of the widget
// style are (BreezeNg::paintAuroraRing, the setting AuroraRing): the chart itself only takes a colour and a thickness. The
// gauge lies over the chart, which is hidden while the gauge draws; a chart of several values, or the plain ring of the
// settings, is left to the chart
class RingGauge : public QQuickPaintedItem
{
public:
    RingGauge(QQuickItem *chart, Look *look, QObject *notifier)
        : QQuickPaintedItem(chart->parentItem())
        , m_chart(chart)
        , m_look(look)
    {
        setAntialiasing(true);
        setZ(chart->z() + 0.5);
        auto fit = [this] {
            if (m_chart) {
                setPosition(m_chart->position());
                setSize(m_chart->size());
                update();
            }
        };
        for (auto signal : {&QQuickItem::xChanged, &QQuickItem::yChanged, &QQuickItem::widthChanged, &QQuickItem::heightChanged}) {
            connect(chart, signal, this, fit);
        }
        connect(chart, &QQuickItem::visibleChanged, this, [this] {
            setVisible(m_chart && m_chart->isVisible());
        });
        connect(chart, &QObject::destroyed, this, &QObject::deleteLater);
        QObject::connect(notifier, SIGNAL(changed()), this, SLOT(update()));
        for (const char *signal : {"valueSourcesChanged()", "rangeChanged()"}) {
            if (chart->metaObject()->indexOfSignal(signal) >= 0) {
                QObject::connect(chart, QByteArray("2" + QByteArray(signal)).constData(), this, SLOT(update()));
            }
        }
        if (QObject *range = chart->property("range").value<QObject *>(); range && range->metaObject()->indexOfSignal("rangeChanged()") >= 0) {
            QObject::connect(range, SIGNAL(rangeChanged()), this, SLOT(update()));
        }
        connect(chart, &QQuickItem::opacityChanged, this, [this] {
            if (m_hidden && m_chart && m_chart->opacity() != 0.0) {
                update(); // (paint() takes the new value over)
            }
        });
        watch();
        fit();
    }

    void paint(QPainter *painter) override
    {
        if (!m_chart) {
            return;
        }
        // (the chart may be given another source, or the source another model, after the gauge was made)
        watch();
        qreal fraction = 0.0;
        const qreal side = qMin(width(), height());
        const QRectF rect((width() - side) / 2, (height() - side) / 2, side, side);
        const qreal thickness = qMax(5.0, m_chart->property("thickness").toReal());
        // (a chart too small for the ring of the settings is left to the chart)
        const bool ours = m_look->ring != BreezeNg::RingPlain && BreezeNg::auroraRingFits(rect, thickness, m_look->ring) && reading(&fraction);
        if (qEnvironmentVariableIsSet("BREEZE_NG_DEBUG_QUICK") && !m_told) {
            m_told = true;
            QQmlListReference sources(m_chart, "valueSources");
            qWarning("breeze-ng ring gauge: sources valid %d count %d, source %s, ours %d, ring %d", int(sources.isValid()), int(sources.isValid() ? sources.count() : -1),
                     m_source ? m_source->metaObject()->className() : "-", int(ours), m_look->ring);
        }
        // (the chart is hidden only while the gauge stands in for it, and gets back the opacity it had: a chart the
        // gauge does not draw is the application's own)
        if (ours && !m_hidden) {
            m_hidden = true;
            m_opacity = m_chart->opacity();
            m_chart->setOpacity(0.0);
            setOpacity(m_opacity);
        } else if (!ours && m_hidden) {
            m_hidden = false;
            m_chart->setOpacity(m_opacity);
        } else if (ours && m_chart->opacity() != 0.0) {
            // (the application gave the chart another opacity meanwhile: that is the one to keep, and to give back)
            m_opacity = m_chart->opacity();
            m_chart->setOpacity(0.0);
            setOpacity(m_opacity);
        }
        if (!ours) {
            return;
        }
        const qreal fromAngle = m_chart->property("fromAngle").isValid() ? m_chart->property("fromAngle").toReal() : 0.0;
        const qreal toAngle = m_chart->property("toAngle").isValid() ? m_chart->property("toAngle").toReal() : 360.0;
        const QPalette palette = QGuiApplication::palette();
        const bool dark = palette.color(QPalette::Window).lightnessF() < 0.5;
        const QColor accent = palette.color(QPalette::Active, QPalette::Highlight);
        const bool grey = accent.hslSaturationF() < 0.15;
        const qreal hue = accent.hslHueF() < 0 ? 0.58 : accent.hslHueF();
        // (a second accent with next to no colour stays as it is, like the first: only one not set gets the hue next
        // to the accent's)
        const bool second = m_look->second.isValid();
        const bool grey2 = second && (m_look->second.hslHueF() < 0 || m_look->second.hslSaturationF() < 0.15);
        const qreal hue2 = second && !grey2 ? m_look->second.hslHueF() : std::fmod(hue + 0.17, 1.0);
        QColor track = palette.color(QPalette::WindowText);
        track.setAlphaF(dark ? 0.16 : 0.13);
        // (the chart's angles run clockwise from twelve o'clock)
        BreezeNg::paintAuroraRing(painter, rect, thickness, 90.0 - fromAngle, -(toAngle - fromAngle), fraction, m_look->ring,
                                  grey ? accent : QColor::fromHslF(hue, dark ? 0.90 : 0.80, dark ? 0.66 : 0.50),
                                  grey2 ? m_look->second : ((grey && !second) ? accent : QColor::fromHslF(hue2, dark ? 0.88 : 0.68, dark ? 0.72 : 0.56)), track);
    }

private:
    // The one source of a chart with one ring; none for a chart of several rings (one source each), left to the chart
    QObject *valueSource() const
    {
        QQmlListReference sources(m_chart, "valueSources");
        return (sources.isValid() && sources.count() == 1) ? sources.at(0) : nullptr;
    }

    // Follows the chart's source and the source's model, whichever they are now
    void watch()
    {
        QObject *source = m_chart ? valueSource() : nullptr;
        if (source != m_source) {
            if (m_source) {
                m_source->disconnect(this);
            }
            m_source = source;
            if (source) {
                for (const char *signal : {"dataChanged()", "modelChanged()", "columnChanged()", "roleNameChanged()", "indexColumnsChanged()"}) {
                    if (source->metaObject()->indexOfSignal(signal) >= 0) {
                        QObject::connect(source, QByteArray("2" + QByteArray(signal)).constData(), this, SLOT(update()));
                    }
                }
            }
        }
        QAbstractItemModel *model = source ? qobject_cast<QAbstractItemModel *>(source->property("model").value<QObject *>()) : nullptr;
        if (model != m_model) {
            if (m_model) {
                m_model->disconnect(this);
            }
            m_model = model;
            if (model) {
                auto again = [this] {
                    update();
                };
                connect(model, &QAbstractItemModel::dataChanged, this, again);
                connect(model, &QAbstractItemModel::modelReset, this, again);
                connect(model, &QAbstractItemModel::layoutChanged, this, again);
                connect(model, &QAbstractItemModel::columnsInserted, this, again);
                connect(model, &QAbstractItemModel::columnsRemoved, this, again);
                connect(model, &QAbstractItemModel::rowsInserted, this, again);
                connect(model, &QAbstractItemModel::rowsRemoved, this, again);
            }
        }
    }

    // The part of the ring to fill, worked out as the chart does (PieChart::onDataChanged, RangeGroup::calculateRange
    // and ModelSource::item of KQuickCharts; the source's count and values are C++ methods out of reach here, so they
    // are read from its model). False when this is not a chart of one value the gauge can draw as the chart would
    bool reading(qreal *fraction) const
    {
        if (!m_source || !m_model) {
            return false;
        }
        const bool byColumns = m_source->property("indexColumns").toBool();
        if ((byColumns ? m_model->columnCount() : m_model->rowCount()) != 1) {
            return false;
        }
        const int role = m_model->roleNames().key(m_source->property("roleName").toString().toLatin1(), -1);
        const int column = byColumns ? 0 : m_source->property("column").toInt();
        const QModelIndex index = m_model->index(0, column);
        const QObject *range = m_chart->property("range").value<QObject *>();
        if (role < 0 || !index.isValid() || !range) {
            return false;
        }
        const qreal value = m_model->data(index, role).toReal();
        const bool automatic = range->property("automatic").toBool();
        if (!automatic && !(range->property("to").toReal() > range->property("from").toReal())) {
            return false; // (RangeGroup::isValid: the chart draws nothing with such a range)
        }
        // (an automatic range starts at nought and ends at the value or at the limit its model carries, the sensors'
        // own, whichever is more)
        const QVariant least = m_model->property("minimum"), most = m_model->property("maximum");
        const qreal start = automatic ? 0.0 : range->property("from").toReal();
        // (ModelSource::maximum: a model's maximum that tells its changes is taken as it is, also when equal to the
        // minimum; one that does not only when the two differ)
        const int mostIndex = m_model->metaObject()->indexOfProperty("maximum");
        const bool told = mostIndex >= 0 && m_model->metaObject()->property(mostIndex).hasNotifySignal();
        qreal end = automatic ? qMax(value, (most.isValid() && (told || most != least)) ? most.toReal() : value) : range->property("to").toReal();
        end = qMax(end, range->property("minimum").toReal());
        if (const qreal step = range->property("increment").toReal(); step > 0.0) {
            end = step * std::ceil(end / step);
        }
        if (!(end - start > 0.0)) {
            return false;
        }
        *fraction = qBound(0.0, qMin(value - start, end) / (end - start), 1.0);
        return true;
    }

    QPointer<QQuickItem> m_chart;
    Look *m_look;
    QPointer<QObject> m_source;
    QPointer<QAbstractItemModel> m_model;
    qreal m_opacity = 1.0;
    bool m_hidden = false;
    bool m_told = false;
};

// The ring charts of KQuickCharts on glass: their track is worked out from the theme's background, see-through here, and
// came out a thick ring of solid white (or dark) with a dull arc. A thinner ring with round ends on a quiet track, and a
// RingGauge over each. Charts come and go with the pages: looked for now and then while the window shows
void ringCharts(QQuickWindow *window, Look *look, QObject *notifier)
{
    auto timer = new QTimer(window);
    timer->setInterval(1500);
    QPointer<QQuickWindow> guard(window);
    QObject::connect(timer, &QTimer::timeout, window, [guard, look, notifier] {
        if (!guard || !guard->isVisible()) {
            return;
        }
        const bool dark = QGuiApplication::palette().color(QPalette::Window).lightnessF() < 0.5;
        std::function<void(QQuickItem *, int)> visit = [&](QQuickItem *node, int depth) {
            if (node->inherits("PieChart") && !node->property("_breeze_ng_ring").toBool() && node->property("thickness").isValid() && node->parentItem()) {
                node->setProperty("_breeze_ng_ring", true);
                // (through QQmlProperty: the face binds these, and a bound value would come back. A solid tone for the
                // track: the chart adds a see-through colour to what is under it, and a faint ink came out lighter than
                // the card, a white ring again)
                const QPalette palette = QGuiApplication::palette();
                const QColor window = palette.color(QPalette::Window), text = palette.color(QPalette::WindowText);
                const qreal t = dark ? 0.20 : 0.16;
                QQmlProperty(node, QStringLiteral("backgroundColor"))
                    .write(QColor::fromRgbF(window.redF() + (text.redF() - window.redF()) * t, window.greenF() + (text.greenF() - window.greenF()) * t,
                                            window.blueF() + (text.blueF() - window.blueF()) * t));
                QQmlProperty(node, QStringLiteral("thickness")).write(qMax(5.0, node->property("thickness").toReal() * 0.62));
                QQmlProperty(node, QStringLiteral("smoothEnds")).write(true);
                inject(new RingGauge(node, look, notifier));
            }
            if (depth < 60) {
                const auto children = node->childItems();
                for (QQuickItem *child : children) {
                    visit(child, depth + 1);
                }
            }
        };
        visit(guard->contentItem(), 0);
    });
    timer->start();
}

// A Kirigami.Separator laid across the top of a window, right under the title bar (the authentication dialog has one):
// its colour is worked out from a background that is see-through here and came out a solid dark line between the title
// bar and the glass. Aurora has no line there: the two are one sheet. Looked for when the window shows
void topLine(QQuickWindow *window)
{
    QPointer<QQuickWindow> guard(window);
    auto hide = [guard] {
        if (!guard) {
            return;
        }
        std::function<void(QQuickItem *, int)> visit = [&](QQuickItem *node, int depth) {
            if (QByteArray(node->metaObject()->className()).startsWith("Separator") && node->height() <= 2 && node->width() >= guard->width() - 2
                && qAbs(node->mapToScene(QPointF(0, 0)).y()) <= 1) {
                node->setOpacity(0.0);
            }
            if (depth < 5) {
                const auto children = node->childItems();
                for (QQuickItem *child : children) {
                    visit(child, depth + 1);
                }
            }
        };
        visit(guard->contentItem(), 0);
    };
    QObject::connect(window, &QWindow::visibleChanged, window, [guard, hide](bool visible) {
        if (visible) {
            QTimer::singleShot(0, guard, hide);
            QTimer::singleShot(300, guard, hide);
        }
    });
    QTimer::singleShot(0, window, hide);
    QTimer::singleShot(600, window, hide);
}

// A plain Rectangle that covers (nearly) the whole window in the scheme's solid base or window colour is the page's
// background painted by the application itself (Elisa's main view is one: palette.base, not Kirigami's theme): in
// Aurora it hid the glass, the body was a solid block under a glass title bar. It is cleared to show the glass.
void unfillCovers(QQuickWindow *window)
{
    // (only a background as large as the window, near the top of the tree, and only while the window fills: walking
    // the items at every frame made System Settings stutter, and an object of ours among a rectangle's children
    // crashed Kirigami's theme updates)
    if (!window || hasHook(window, 2)) {
        return;
    }
    setHook(window, 2);
    QPointer<QQuickWindow> guard(window);
    auto walk = [guard] {
        if (!guard) {
            return;
        }
        const QPalette palette = QGuiApplication::palette();
        const QColor base = palette.color(QPalette::Base), ground = palette.color(QPalette::Window);
        const QSizeF size = guard->size();
        auto close = [](const QColor &a, const QColor &b) {
            return qAbs(a.red() - b.red()) + qAbs(a.green() - b.green()) + qAbs(a.blue() - b.blue()) <= 6;
        };
        std::function<void(QQuickItem *, int)> visit = [&](QQuickItem *node, int depth) {
            if (node->inherits("QQuickRectangle") && node->width() >= size.width() * 0.95 && node->height() >= size.height() * 0.6) {
                const QColor colour = node->property("color").value<QColor>();
                const bool neutral = colour.hslSaturationF() < 0.12 && qAbs(colour.lightnessF() - ground.lightnessF()) < 0.12;
                if (colour.alpha() == 255 && (close(colour, base) || close(colour, ground) || neutral)) {
                    node->setProperty("color", QColor(Qt::transparent));
                }
            }
            if (depth < 3) {
                const auto children = node->childItems();
                for (QQuickItem *child : children) {
                    visit(child, depth + 1);
                }
            }
        };
        visit(guard->contentItem(), 0);
    };
    for (int delay : {0, 300, 900, 2000, 4000}) {
        QTimer::singleShot(delay, window, walk);
    }
}


// Text on the glass gets a shadow of the opposite tone, as in the widget applications (Breeze NG's "Text shadow":
// Never, When the contrast may be low, Always): QtQuick's own Text.Raised, a copy of the text one pixel below in a
// colour of its own, cheap to draw. "When the contrast may be low" reckons the worst case, as Helper::textShadow
// does: a white window behind light text (a black one behind dark text) seen through the glass, against 4.5:1.
// Not under text on a selection (its own solid fill), nor under dimmed or disabled text (the shadow smudged it), and
// never over a style the application chose. Texts come and go as pages change and lists scroll: looked at after the
// window's animations, now and then.
bool shadowNeeded(const Look &look)
{
    const QString mode = KSharedConfig::openConfig(QStringLiteral("breezengrc"))->group(QStringLiteral("Common")).readEntry("TextShadow", QStringLiteral("LowContrast"));
    if (mode == QLatin1String("Never")) {
        return false;
    }
    if (mode == QLatin1String("Always")) {
        return true;
    }
    const QPalette palette = QGuiApplication::palette();
    const QColor text = palette.color(QPalette::WindowText), sheet = palette.color(QPalette::Window);
    auto luminance = [](const QColor &c) {
        auto linear = [](qreal v) {
            return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * linear(c.redF()) + 0.7152 * linear(c.greenF()) + 0.0722 * linear(c.blueF());
    };
    const qreal alpha = look.sheet.alphaF();
    const qreal behind = text.lightnessF() > 0.5 ? 1.0 : 0.0;
    const QColor ground = QColor::fromRgbF(alpha * sheet.redF() + (1 - alpha) * behind, alpha * sheet.greenF() + (1 - alpha) * behind,
                                           alpha * sheet.blueF() + (1 - alpha) * behind);
    const qreal a = luminance(text), b = luminance(ground);
    return (qMax(a, b) + 0.05) / (qMin(a, b) + 0.05) < 4.5;
}

void textShadows(QQuickWindow *window, const Look &look)
{
    if (qEnvironmentVariableIsSet("BREEZE_NG_DEBUG_QUICK")) {
        qWarning("breeze-ng kirigami shadow: window %p needed %d", static_cast<void *>(window), int(shadowNeeded(look)));
    }
    if (!window || hasHook(window, 4) || qEnvironmentVariableIsSet("BREEZE_NG_QUICK_NO_SHADOW") || !shadowNeeded(look)) {
        return;
    }
    setHook(window, 4);
    QPointer<QQuickWindow> guard(window);
    const QColor onSelection = look.onSelection;
    auto walk = [guard, onSelection] {
        if (!guard) {
            return;
        }
        const QPalette palette = QGuiApplication::palette();
        const QColor windowText = palette.color(QPalette::Active, QPalette::WindowText);
        const QColor highlighted = palette.color(QPalette::Active, QPalette::HighlightedText);
        const QColor disabled = palette.color(QPalette::Disabled, QPalette::WindowText);
        // (no blur behind the window, as the widget style found: the window is solid, no shadow wanted)
        const bool bare = qApp->property("_breeze_ng_bare_glass").toBool();
        auto close = [](const QColor &a, const QColor &b) {
            return qAbs(a.red() - b.red()) + qAbs(a.green() - b.green()) + qAbs(a.blue() - b.blue()) <= 12;
        };
        std::function<void(QQuickItem *, int)> visit = [&](QQuickItem *node, int depth) {
            if (node->inherits("QQuickText")) {
                const QColor colour = node->property("color").value<QColor>();
                const bool ours = node->property("_breeze_ng_shadow").toBool();
                // (a title drawn through the Aurora gradient: its halo would take the gradient too, a bold rim)
                const bool skip = bare || node->property("_breeze_ng_title_gradient").toBool() || !node->isEnabled() || node->opacity() < 0.75 || colour.alphaF() < 0.75
                    || (close(colour, highlighted) && !close(colour, windowText)) || (onSelection.isValid() && close(colour, onSelection))
                    || (close(colour, disabled) && !close(colour, windowText));
                if (skip) {
                    if (ours) {
                        node->setProperty("style", 0); // Text.Normal
                        node->setProperty("_breeze_ng_shadow", false);
                    }
                } else if (ours || node->property("style").toInt() == 0) {
                    // a halo all round, as the text of Dolphin NG's sidebar (a blurred drop shadow there, which Qt Quick
                    // has not for text): a faint outline. (Text.Raised, one copy a pixel below, hardly showed)
                    // (faint: at 120 the outline was a rim round every letter and the text looked bold)
                    const QColor shadow = colour.lightnessF() > 0.5 ? QColor(0, 0, 0, 60) : QColor(255, 255, 255, 55);
                    if (!ours || node->property("styleColor").value<QColor>() != shadow || node->property("style").toInt() != 1) {
                        node->setProperty("styleColor", shadow);
                        node->setProperty("style", 1); // Text.Outline
                        node->setProperty("_breeze_ng_shadow", true);
                    }
                }
            }
            if (depth < 60) {
                const auto children = node->childItems();
                for (QQuickItem *child : children) {
                    visit(child, depth + 1);
                }
            }
        };
        visit(guard->contentItem(), 0);
        if (qEnvironmentVariableIsSet("BREEZE_NG_DEBUG_QUICK")) {
            int texts = 0, shaded = 0;
            std::function<void(QQuickItem *)> count = [&](QQuickItem *node) {
                if (node->inherits("QQuickText")) {
                    ++texts;
                    shaded += node->property("_breeze_ng_shadow").toBool();
                }
                for (QQuickItem *child : node->childItems()) {
                    count(child);
                }
            };
            count(guard->contentItem());
            qWarning("breeze-ng kirigami shadow: %d texts, %d shaded", texts, shaded);
        }
    };
    auto last = std::make_shared<QElapsedTimer>();
    QObject::connect(window, &QQuickWindow::afterAnimating, window, [walk, last] {
        if (!last->isValid() || last->elapsed() > 400) {
            last->start();
            walk();
        }
    });
    for (int delay : {0, 500, 1500, 4000}) {
        QTimer::singleShot(delay, window, walk);
    }
}

// An icon inside a button that is on (an orientation in the display settings, a view switch): it takes the colour of
// text on a selection, as the button's sheet is one (the deep accent on its light sheet, white on the full accent).
// The icon asks for a theme of its own; the button above it is watched for being switched on and off.
void checkedIcon(PlatformTheme *theme, QObject *owner, const Look &look)
{
    if (!theme || !owner || theme->property("_breeze_ng_checked_icon").toBool() || !QByteArray(owner->metaObject()->className()).contains("Icon")) {
        return;
    }
    QObject *button = nullptr;
    int depth = 0;
    for (QObject *object = owner->parent(); object && depth < 4; object = object->parent(), ++depth) {
        if (object->inherits("QQuickAbstractButton")) {
            button = object;
            break;
        }
    }
    const QColor onSelection = look.fullSelection ? QColor(255, 255, 255) : look.onSelection;
    if (!button || !onSelection.isValid()) {
        return;
    }
    theme->setProperty("_breeze_ng_checked_icon", true);
    QPointer<PlatformTheme> guard(theme);
    QPointer<QObject> watched(button);
    auto follow = [guard, watched, onSelection] {
        if (!guard || !watched) {
            return;
        }
        if (watched->property("checked").toBool()) {
            guard->setCustomTextColor(onSelection);
        } else {
            guard->setCustomTextColor();
        }
    };
    if (qEnvironmentVariableIsSet("BREEZE_NG_QUICK_NO_CHECKED")) {
        return;
    }
    const int signal = button->metaObject()->indexOfSignal("checkedChanged()");
    if (signal >= 0) {
        auto *relay = new Relay(theme, follow);
        QObject::connect(button, button->metaObject()->method(signal), relay, relay->metaObject()->method(relay->metaObject()->indexOfSlot("fire()")));
    }
    follow();
}

// The kind of the nearest theme above this object that has our colours: 1 a veil, 2 nothing (0: none; the window's
// own does not count). A theme inherits along the items: one attached to a popup or a drawer, which are not items,
// does not reach the items inside it.
int oursAbove(const QObject *owner)
{
    for (const QObject *object = owner ? owner->parent() : nullptr; object; object = object->parent()) {
        if (!object->inherits("QQuickItem")) {
            return 0;
        }
        const auto children = object->children();
        for (const QObject *child : children) {
            const int kind = child->inherits("Kirigami::Platform::PlatformTheme") ? child->property("_breeze_ng_glass").toInt() : 0;
            if (kind == 1 || kind == 5) {
                return 1;
            }
            if (kind == 2 || kind == 3 || kind == 6) {
                return 2;
            }
        }
    }
    return 0;
}

// asks the compositor for the blur behind a Qt Quick window, each time it gets a native window
class BlurBehind : public QObject
{
public:
    using QObject::QObject;
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Expose || event->type() == QEvent::PlatformSurface) {
            if (auto window = qobject_cast<QWindow *>(watched); window && window->handle()) {
                KWindowEffects::enableBlurBehind(window, true);
            }
        }
        return false;
    }
};
}

// told when Breeze NG's settings change (the titles' option): the themes already handed out lay their colours again
class LookNotifier : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
Q_SIGNALS:
    void changed();
};

class BreezeNgKirigamiFactory : public PlatformPluginFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PlatformPluginFactory_iid)
    Q_INTERFACES(Kirigami::Platform::PlatformPluginFactory)

public:
    explicit BreezeNgKirigamiFactory(QObject *parent = nullptr)
        : PlatformPluginFactory(parent)
        , m_look(readLook())
        , m_notifier(new LookNotifier(this))
    {
        // the titles' option (AccentText, the second light, the text's tint) followed while the application runs:
        // the settings' Apply reaches the windows already open, and Plasma. (The signal the widget style listens to;
        // the settings are not written with KConfig's notification)
        QDBusConnection::sessionBus().connect(QString(), QStringLiteral("/BreezeStyle"), QStringLiteral("org.kde.Breeze.Style"),
                                              QStringLiteral("reparseConfiguration"), this, SLOT(reloadLook()));
        // (the shell's scripting answers once the shell is up)
        QTimer::singleShot(6000, this, [this] {
            floatingPanels();
        });
    }

public Q_SLOTS:
    // Aurora's preset for Plasma's panels (AuroraFloatingPanel): floating or not, told to the shell through its own
    // scripting, once each time the setting changes (the value last applied is kept in breezengrc): a panel the user
    // then sets otherwise in Plasma is left alone. Only inside plasmashell
    void floatingPanels()
    {
#ifdef BREEZE_NG_PLASMA_STYLE
        if (!m_look.aurora || QCoreApplication::applicationName() != QLatin1String("plasmashell")) {
            return;
        }
        KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("breezengrc"));
        const bool wanted = config->group(QStringLiteral("Common")).readEntry("AuroraFloatingPanel", true);
        KConfigGroup applied = config->group(QStringLiteral("Plasma"));
        const QString mark = wanted ? QStringLiteral("true") : QStringLiteral("false");
        if (applied.readEntry("FloatingPanelApplied", QString()) == mark) {
            return;
        }
        if (m_floatingPending) {
            return;
        }
        m_floatingPending = true;
        QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.kde.plasmashell"), QStringLiteral("/PlasmaShell"), QStringLiteral("org.kde.PlasmaShell"),
                                                           QStringLiteral("evaluateScript"));
        call << QStringLiteral("for (const p of panels()) { p.floating = %1; }").arg(mark);
        // (noted as applied only when the shell has run the script: a call that fails, the shell not ready yet or its
        // scripting locked, is tried again at the next Apply or start)
        auto watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, mark](QDBusPendingCallWatcher *done) {
            done->deleteLater();
            m_floatingPending = false;
            if (done->isError()) {
                qWarning("breeze-ng: the floating panel preset was not applied: %s", qPrintable(done->error().message()));
                return;
            }
            KConfigGroup applied = KSharedConfig::openConfig(QStringLiteral("breezengrc"))->group(QStringLiteral("Plasma"));
            applied.writeEntry("FloatingPanelApplied", mark);
            applied.sync();
        });
#endif
    }

    void reloadLook()
    {
        // (the whole look again: titles, lights, grain, the second light, the glass' density; a change of the look
        // itself, Aurora on or off, needs the application started again, as the glass is set up when it starts)
        KSharedConfig::openConfig(QStringLiteral("breezengrc"))->reparseConfiguration();
        const bool aurora = m_look.aurora;
        m_look = readLook();
        m_look.aurora = aurora;
        Q_EMIT m_notifier->changed();
        floatingPanels();
    }

public:

    PlatformTheme *createPlatformTheme(QObject *parent) override
    {
        PlatformPluginFactory *factory = real();
        PlatformTheme *theme = factory ? factory->createPlatformTheme(parent) : nullptr;
#ifdef BREEZE_NG_PLASMA_STYLE
        // in Plasma (the shell's panels and popups) only the titles, as the windows have them: the glass and the
        // rest of the colours are the Plasma style's own
        if (theme && m_look.aurora && parent && isHeading(parent)) {
            plasmaTitle(theme); // (with the option off too: it can be turned on while Plasma runs)
        }
        if (theme && m_look.aurora && !qEnvironmentVariableIsSet("BREEZE_NG_PLASMA_NO_LIGHTS")) {
            if (auto item = qobject_cast<QQuickItem *>(parent)) {
                // (the item has no window yet when its theme is asked for)
                QPointer<QQuickItem> guard(item);
                QTimer::singleShot(0, item, [this, guard] {
                    popupLights(guard);
                    popupCard(guard);
                });
                // (a popup's content is made when Plasma starts, its window when it first opens)
                QObject::connect(item, &QQuickItem::windowChanged, item, [this, guard](QQuickWindow *window) {
                    if (window) {
                        popupLights(guard);
                        popupCard(guard);
                    }
                });
            }
        }
#else
        if (theme && m_look.aurora) {
            glass(theme, parent);
        }
#endif
        return theme;
    }

    Units *createUnits(QObject *parent) override
    {
        PlatformPluginFactory *factory = real();
        return factory ? factory->createUnits(parent) : nullptr;
    }

private:
    // the desktop style's own plugin: the file of this name in a later directory of the plugin path
    PlatformPluginFactory *real()
    {
        if (m_searched) {
            return m_real;
        }
        m_searched = true;
        Dl_info info;
        QString own;
        if (dladdr(reinterpret_cast<void *>(&readLook), &info) && info.dli_fname) {
            own = QFileInfo(QString::fromLocal8Bit(info.dli_fname)).canonicalFilePath();
        }
        const auto paths = QCoreApplication::libraryPaths();
        for (const QString &path : paths) {
#ifdef BREEZE_NG_PLASMA_STYLE
            const QString file = QFileInfo(path + QStringLiteral("/kf6/kirigami/platform/KirigamiPlasmaStyle.so")).canonicalFilePath();
#else
            const QString file = QFileInfo(path + QStringLiteral("/kf6/kirigami/platform/org.kde.desktop.so")).canonicalFilePath();
#endif
            if (file.isEmpty() || file == own) {
                continue;
            }
            QPluginLoader loader(file);
            if (auto factory = qobject_cast<PlatformPluginFactory *>(loader.instance())) {
                m_real = factory;
                break;
            }
        }
        if (!m_real) {
            qWarning("breeze-ng: the desktop style's Kirigami plugin was not found: Kirigami falls back to its own colours");
        }
        return m_real;
    }

    // the lights of Aurora in the Plasma popup this item is shown in (PlasmaLights), once per popup: on the popup's
    // background, the Plasma style's frame, which every popup of Plasma 6 lays first in its window. Not in tooltips
    void popupLights(QQuickItem *item)
    {
        QQuickWindow *window = item ? item->window() : nullptr;
        if (!window || hasHook(window, 8)) {
            return;
        }
        const bool popup = (window->inherits("PlasmaQuick::PlasmaWindow") || window->inherits("PlasmaQuick::Dialog")) && !window->inherits("ToolTipDialog")
            && !QByteArray(window->metaObject()->className()).contains("ToolTip");
        if (qgetenv("BREEZE_NG_DEBUG_QUICK") == "3" && !window->property("_breeze_ng_dumped").toBool()) {
            window->setProperty("_breeze_ng_dumped", true);
            QByteArray chain;
            for (const QMetaObject *meta = window->metaObject(); meta; meta = meta->superClass()) {
                chain += QByteArray(meta->className()) + " < ";
            }
            qWarning("breeze-ng window: %s popup %d", chain.constData(), int(popup));
            QPointer<QQuickWindow> seen(window);
            QObject::connect(window, &QWindow::visibleChanged, window, [seen](bool visible) {
                if (visible) {
                    QTimer::singleShot(1500, seen, [seen] {
                        if (seen && seen->isVisible()) {
                            dumpTree(seen->contentItem(), 0);
                        }
                    });
                }
            });
        }
        if (!popup) {
            setHook(window, 8);
            if (window->inherits("PanelView")) {
                if (!window->property("_breeze_ng_highlights").toBool()) {
                    window->setProperty("_breeze_ng_highlights", true);
                    panelHighlights(window);
                }
                // Aurora's lights in the panel too, over its background (the largest frame of the Plasma style's
                // panel-background), under its applets
                // (each of the panel's background frames, shown or not: Plasma keeps one for a floating panel and one
                // for a panel against the edge and shows one of them; the overlay of each follows its frame)
                int laid = 0;
                std::function<void(QQuickItem *, int)> look = [&](QQuickItem *node, int depth) {
                    const QString path = node->property("imagePath").toString();
                    if (path.endsWith(QLatin1String("widgets/panel-background")) && !path.contains(QLatin1String("solid/"))
                        && qMax(node->width(), node->height()) > 100 && qMin(node->width(), node->height()) > 8) {
                        ++laid;
                        if (!node->property("_breeze_ng_lights").toBool()) {
                            node->setProperty("_breeze_ng_lights", true);
                            inject(new PlasmaLights(node, &m_look, m_notifier, 0, false));
                        }
                    }
                    if (depth < 3) {
                        const auto children = node->childItems();
                        for (QQuickItem *child : children) {
                            look(child, depth + 1);
                        }
                    }
                };
                look(window->contentItem(), 0);
                if (qEnvironmentVariableIsSet("BREEZE_NG_DEBUG_QUICK")) {
                    qWarning("breeze-ng plasma: panel frames with lights %d", laid);
                }
                if (laid == 0) {
                    windowHooks()[window] &= ~8; // (not laid out yet: the next item asks again)
                }
            }
            return;
        }
        QQuickItem *background = nullptr;
        qreal area = 0;
        std::function<void(QQuickItem *, int)> find = [&](QQuickItem *node, int depth) {
            const QByteArray name = node->metaObject()->className();
            if ((name.contains("FrameSvgItem") || name.contains("DialogBackground")) && node->width() * node->height() > area) {
                background = node;
                area = node->width() * node->height();
            }
            if (depth < 2) {
                const auto children = node->childItems();
                for (QQuickItem *child : children) {
                    find(child, depth + 1);
                }
            }
        };
        find(window->contentItem(), 0);
        if (qEnvironmentVariableIsSet("BREEZE_NG_DEBUG_QUICK")) {
            qWarning("breeze-ng plasma: popup %s, background %s %.0fx%.0f", window->metaObject()->className(),
                     background ? background->metaObject()->className() : "-", background ? background->width() : 0.0, background ? background->height() : 0.0);
        }
        if (!background || area < 400) {
            // (not laid out yet: asked again a little later, a few times)
            const int tries = window->property("_breeze_ng_light_tries").toInt();
            if (tries < 20) {
                window->setProperty("_breeze_ng_light_tries", tries + 1);
                QPointer<QQuickItem> guard(item);
                QTimer::singleShot(250, item, [this, guard] {
                    popupLights(guard);
                });
            }
            return;
        }
        setHook(window, 8);
        // the Plasma style's own sheet (readable by itself, 80 %) is thinned to a fifth under the glass laid here
        bool thinned = false;
        const auto parts = background->childItems();
        for (QQuickItem *part : parts) {
            if (part->property("imagePath").toString().endsWith(QLatin1String("dialogs/background"))) {
                part->setOpacity(0.25);
                thinned = true;
            }
        }
        if (!thinned) {
            setHook(window, 8);
            return; // (a popup built another way: left as the Plasma style draws it)
        }
        inject(new PlasmaLights(background, &m_look, m_notifier, 14)); // (the radius of the Plasma style's popups, make-plasma-style.py R_POPUP)
    }

    // the card under a list of a Plasma popup (PlasmaCard), once per list: the launcher's sidebar (its categories,
    // a KickoffListView in the TriangleMouseFilter of its applications page) and the main list of a system tray
    // page (a ScrollView that fills most of its popup). Other popups and lists are left as they are
    void popupCard(QQuickItem *item)
    {
        QQuickWindow *window = item ? item->window() : nullptr;
        if (!window || !window->inherits("PlasmaQuick::AppletPopup") || qEnvironmentVariableIsSet("BREEZE_NG_PLASMA_NO_CARDS")) {
            return;
        }
        // (the themes are asked for by the rows and the labels, not by the lists: from the item up to its list)
        QQuickItem *target = nullptr;
        qreal reach = 0;
        bool inLauncher = false;
        int depth = 0;
        for (QQuickItem *node = item; node && depth < 16 && !target; node = node->parentItem(), ++depth) {
            if (node->property("_breeze_ng_card").toBool()) {
                return; // (its list has its card already)
            }
            const QByteArray name = node->metaObject()->className();
            if (name.startsWith("KickoffListView") && node->parentItem() && node->parentItem()->parentItem()
                && QByteArray(node->parentItem()->parentItem()->metaObject()->className()).startsWith("TriangleMouseFilter")) {
                target = node->parentItem()->parentItem(); // the launcher's sidebar
                reach = -4; // (inside the sidebar's own room: its rows' highlight reaches its edges)
            } else if (name.startsWith("NormalPage") || name.startsWith("ApplicationsPage") || name.startsWith("KickoffListView") || name.startsWith("DropAreaGridView")) {
                inLauncher = true;
            } else if (!inLauncher && name.startsWith("ScrollView") && node->width() > window->width() * 0.6 && node->height() > window->height() * 0.3) {
                target = node; // the main list of a system tray page
                // (inside the popup's edges, with room round it: reaching them, the card read as the popup cut in two
                // under its header. The list's own padding grows with it, so that its rows stay inside the card)
                reach = -12;
                for (const char *side : {"leftPadding", "rightPadding"}) {
                    node->setProperty(side, node->property(side).toReal() + 10);
                }
                for (const char *side : {"topPadding", "bottomPadding"}) {
                    node->setProperty(side, node->property(side).toReal() + 6);
                }
            }
        }
        if (!target || target->property("_breeze_ng_card").toBool()) {
            return;
        }
        target->setProperty("_breeze_ng_card", true);
        inject(new PlasmaCard(target, &m_look, m_notifier, reach));
    }

    // The highlight of an open applet in a panel (the system tray's icon, the launcher's button) is the Plasma
    // style's active tab, which the panel lays a little larger than the applet's cell: in the system tray it reached
    // into the icons beside. In a panel it is drawn smaller, round its centre (the same tab is the launcher's and the
    // calendar's segment, at its full size). Looked for when the panel shows and again a little later (the tray makes
    // its highlight when it is first needed)
    static void panelHighlights(QQuickWindow *window)
    {
        QPointer<QQuickWindow> guard(window);
        auto shrink = [guard] {
            if (!guard) {
                return;
            }
            std::function<void(QQuickItem *, int)> visit = [&](QQuickItem *node, int depth) {
                // (inside an applet only: the same asset, laid by the panel itself near the root, is its keyboard focus
                // indicator, which keeps its size)
                if (depth > 3 && node->property("imagePath").toString().endsWith(QLatin1String("widgets/tabbar")) && !node->property("_breeze_ng_shrunk").toBool()) {
                    node->setProperty("_breeze_ng_shrunk", true);
                    node->setTransformOrigin(QQuickItem::Center);
                    node->setScale(0.78);
                }
                if (depth < 30) {
                    const auto children = node->childItems();
                    for (QQuickItem *child : children) {
                        visit(child, depth + 1);
                    }
                }
            };
            visit(guard->contentItem(), 0);
        };
        QTimer::singleShot(1000, window, shrink);
        QTimer::singleShot(5000, window, shrink);
        QObject::connect(window, &QWindow::activeChanged, window, shrink); // (an applet opened from the panel)
    }

    // a heading of Plasma: the title's colour (or gradient) once it has its level and colour set. On Plasma's own
    // surfaces: dark or light from the theme's background, not from the application's palette (a Plasma style can
    // have colours of its own)
    void plasmaTitle(PlatformTheme *theme)
    {
        QPointer<PlatformTheme> guard(theme);
        auto apply = [guard, this] {
            const int accentText = m_look.accentText;
            const QColor second = m_look.second;
            if (!guard || !guard->parent() || !isHeading(guard->parent())) {
                return;
            }
            const bool tinted = guard->property("_breeze_ng_tinted").toBool();
            const QColor ground = guard->property("_breeze_ng_ground").value<QColor>();
            // (the background read before the title's colour was set: the theme's own)
            const QColor background = (tinted && ground.isValid()) ? ground : guard->backgroundColor();
            guard->setProperty("_breeze_ng_ground", background);
            // (as tintText and titleOnly: a grey accent, or the tint switched off for the application, leaves titles plain)
            const QColor accent = QGuiApplication::palette().color(QPalette::Active, QPalette::Highlight);
            const bool allowed = accent.hslSaturationF() >= 0.15 && qEnvironmentVariable("BREEZE_NG_TINTED_TEXT") != QLatin1String("0");
            if (!allowed || !titleText(guard, accentText, second, background.lightnessF() < 0.5)) {
                dropGradient(guard);
                if (tinted) {
                    guard->setCustomTextColor();
                    guard->setProperty("_breeze_ng_tinted", QVariant());
                }
            }
        };
        QObject::connect(theme, &PlatformTheme::colorSetChanged, theme, apply);
        QObject::connect(theme, &PlatformTheme::inheritChanged, theme, apply);
        QObject::connect(m_notifier, &LookNotifier::changed, theme, apply);
        QTimer::singleShot(0, theme, apply);
        QTimer::singleShot(500, theme, apply);
    }

    void glass(PlatformTheme *theme, QObject *parent)
    {
        QPointer<PlatformTheme> guard(theme);
        QPointer<QObject> owner(parent);
        auto apply = [guard, owner, this] {
            if (!guard) {
                return;
            }
            colours(m_look); // (with the palette of now)
            const Look look = m_look;
            // the background of a list row of the older qqc2-desktop-style: its theme's highlight is the light sheet
            if (!look.fullSelection && owner && !listRowsStyled() && QByteArray(owner->metaObject()->className()).startsWith("DefaultListItemBackground")) {
                guard->setCustomHighlightColor(look.listSheet);
                return;
            }
            const bool window = owner && owner->inherits("QQuickWindow") && !owner->inherits("QQuickWidgetOffscreenWindow");
            // a Qt Quick view inside a window of widgets (System Settings' sidebar and pages) is drawn offscreen and
            // cleared with its own colour, white by default: it showed white for a moment before the widget style
            // made it glass. Cleared to nothing from the first frame
            for (QObject *up = owner; up; up = up->parent()) {
                if (auto item = qobject_cast<QQuickItem *>(up)) {
                    QQuickWindow *itemWindow = item->window();
                    if (itemWindow && itemWindow->inherits("QQuickWidgetOffscreenWindow") && !itemWindow->property("_breeze_ng_clear").toBool()) {
                        itemWindow->setProperty("_breeze_ng_clear", true);
                        itemWindow->setColor(Qt::transparent);
                    }
                    // the text shadow, in any window the item is shown in: a Qt Quick window, or the one a view inside
                    // a window of widgets (System Settings) is drawn in offscreen
                    if (itemWindow) {
                        textShadows(itemWindow, look);
                    }
                    // (debugging: the coloured items of a view inside a window of widgets too, once per view)
                    if (itemWindow && itemWindow->inherits("QQuickWidgetOffscreenWindow") && qEnvironmentVariableIsSet("BREEZE_NG_DEBUG_QUICK")) {
                        static QSet<const QObject *> dumped;
                        if (!dumped.contains(itemWindow)) {
                            dumped.insert(itemWindow);
                            QPointer<QQuickWindow> seen(itemWindow);
                            QTimer::singleShot(4000, itemWindow, [seen] {
                                if (seen) {
                                    qWarning("breeze-ng kirigami: offscreen view %dx%d", seen->width(), seen->height());
                                    dumpItems(seen->contentItem(), 0);
                                }
                            });
                        }
                    }
                    break;
                }
            }
            sideRowAir(owner);
            // (before checkedIcon: a button that is on has its own text colour. The titles take AccentText with
            // the text's tint off too)
            if (look.accentTint) {
                tintText(guard, look.accentText, look.second);
            } else {
                titleOnly(guard, look.accentText, look.second);
            }
            {
                Look forIcon = m_look;
                colours(forIcon);
                checkedIcon(guard, owner, forIcon);
            }
            // the indicators of switches and check boxes draw their track and handle in the theme's background
            // colour: made see-through, the handle vanished and the track was a dark smudge. They keep the scheme's
            // solid colours (as in the widget style)
            if (!window && owner) {
                bool indicator = false;
                for (QObject *up = owner; up && !indicator; up = up->parent()) {
                    const QByteArray name = up->metaObject()->className();
                    indicator = name.startsWith("SwitchIndicator") || name.startsWith("CheckIndicator");
                }
                if (indicator) {
                    if (guard->property("_breeze_ng_glass").toInt() != 0) {
                        guard->setCustomBackgroundColor();
                        guard->setCustomAlternateBackgroundColor();
                        guard->setProperty("_breeze_ng_glass", 0);
                    }
                    return;
                }
            }
            if (!window && floats(owner)) {
                if (guard->property("_breeze_ng_glass").toInt() != 0) {
                    guard->setCustomBackgroundColor();
                    guard->setCustomAlternateBackgroundColor();
                    guard->setProperty("_breeze_ng_glass", 0);
                }
                return;
            }
            if (const int above = (!window && guard->inherit()) ? oursAbove(owner) : 0) {
                // It would take its colours from the theme above it, which has ours: but custom colours were seen
                // not to reach it every time (a solid block in the middle of a glass page). Give it the same ones.
                const bool veil = above == 1 && !sideView(owner) && (look.dark || !fillsWindow(owner)) && isBackground(owner);
                guard->setCustomBackgroundColor(veil ? look.veil : look.ground);
                guard->setCustomAlternateBackgroundColor(veil ? look.veil : look.ground);
                if (look.onSelection.isValid()) {
                    guard->setCustomHighlightedTextColor(look.onSelection);
                }
                guard->setProperty("_breeze_ng_glass", veil ? 5 : 6);
                cardShadow(owner);
                messageGlass(owner);
                return;
            }
            const bool view = guard->colorSet() == PlatformTheme::View && !window;
            // (a sidebar view inside a window of widgets has its card painted under it by the widget style, which
            // marks the view's window: no veil of its own on top of that)
            const bool veiled = view && !onVeil(owner) && !sideView(owner) && (look.dark || !fillsWindow(owner));
            // (the window's own theme is nothing too: the sheet is painted once, by the backdrop; as the window's
            // colour it was laid again by every item that took its colour from the window)
            const QColor background = veiled ? look.veil : look.ground;
            guard->setCustomBackgroundColor(background);
            guard->setCustomAlternateBackgroundColor(veiled ? look.veil : look.ground);
            if (look.onSelection.isValid()) {
                guard->setCustomHighlightedTextColor(look.onSelection);
            }
            guard->setProperty("_breeze_ng_glass", window ? 4 : veiled ? 1 : view ? 3 : 2); // (the widget style's own pass leaves these alone)
            cardShadow(owner);
            messageGlass(owner);
            // the window this item is shown in: its glass is set up here too, when the window's own theme came
            // first in a way that missed it (the Welcome Center was seen with a plain white window: QQuickWindow's
            // own default colour, no sheet and no lights, under a title bar that had them)
            QObject *glassOwner = window ? owner.data() : nullptr;
            if (!glassOwner && owner) {
                for (QObject *object = owner; object; object = object->parent()) {
                    if (auto item = qobject_cast<QQuickItem *>(object)) {
                        QQuickWindow *itemWindow = item->window();
                        if (itemWindow && !itemWindow->inherits("QQuickWidgetOffscreenWindow") && itemWindow->format().hasAlpha()) {
                            glassOwner = itemWindow;
                        }
                        break;
                    }
                }
            }
            if (glassOwner && !glassOwner->property("_breeze_ng_blur").toBool()) {
                glassOwner->setProperty("_breeze_ng_blur", true);
                glassOwner->installEventFilter(new BlurBehind(glassOwner));
                if (auto shown = qobject_cast<QWindow *>(glassOwner); shown && shown->handle()) {
                    KWindowEffects::enableBlurBehind(shown, true); // (already on screen: no expose to wait for)
                }
                if (qEnvironmentVariableIsSet("BREEZE_NG_DEBUG_QUICK")) {
                    qWarning("breeze-ng kirigami: glass for %s (from %s)", glassOwner->metaObject()->className(), owner ? owner->metaObject()->className() : "-");
                }
                if (auto quickWindow = qobject_cast<QQuickWindow *>(glassOwner)) {
                    // The window is cleared with its colour as it is, not premultiplied: "nothing in the window's
                    // colour" (the ground, alpha 0) was added by the compositor to what lies behind the window, which
                    // made the body brighter than the title bar (on a light scheme, plain white). The window itself
                    // is cleared to true nothing, whatever Kirigami binds its colour to.
                    auto clear = [quickWindow] {
                        if (quickWindow->color().alpha() == 0 && quickWindow->color() != QColor(Qt::transparent)) {
                            quickWindow->setColor(Qt::transparent);
                        }
                    };
                    QObject::connect(quickWindow, &QQuickWindow::colorChanged, quickWindow, clear, Qt::QueuedConnection);
                    quickWindow->setColor(Qt::transparent);
                    new AuroraBackdrop(quickWindow, &m_look, m_notifier);
                    topLine(quickWindow);
                    ringCharts(quickWindow, &m_look, m_notifier);
                    if (!qEnvironmentVariableIsSet("BREEZE_NG_QUICK_NO_COVERS")) {
                        unfillCovers(quickWindow);
                    }
                    textShadows(quickWindow, look);
                    if (qEnvironmentVariableIsSet("BREEZE_NG_DEBUG_QUICK")) {
                        QTimer::singleShot(4000, quickWindow, [quickWindow] {
                            dumpItems(quickWindow->contentItem(), 0);
                        });
                    }
                }
            }
        };
        apply();
        QObject::connect(theme, &PlatformTheme::colorSetChanged, theme, apply);
        QObject::connect(theme, &PlatformTheme::inheritChanged, theme, apply);
        QObject::connect(m_notifier, &LookNotifier::changed, theme, apply);
        // the item is given its parent, and a popup its content, a moment after the theme is asked for; the widget
        // style finds out which view is a sidebar a little later still
        QTimer::singleShot(0, theme, apply);
        QTimer::singleShot(500, theme, apply);
        QTimer::singleShot(1500, theme, apply);
    }

    // Kirigami's card background lays a dark slab under itself as a drop shadow (60 % of the background colour,
    // darkened): hidden by a solid card, it shows through a glass one and turns it dark. Fade it out.
    static void cardShadow(QObject *owner)
    {
        auto card = qobject_cast<QQuickItem *>(owner);
        if (!card || !QByteArray(card->metaObject()->className()).startsWith("DefaultCardBackground")) {
            return;
        }
        QPointer<QQuickItem> guard(card);
        auto fade = [guard] {
            if (!guard) {
                return;
            }
            const auto children = guard->childItems();
            for (QQuickItem *child : children) {
                if (child->z() < 0 && child->inherits("QQuickRectangle")) {
                    child->setOpacity(0.0);
                }
            }
        };
        fade();
        QTimer::singleShot(0, card, fade);
        QTimer::singleShot(300, card, fade);
    }

    // Kirigami's inline message (the coloured strip: "codec not found", "restart needed") draws a block of its
    // kind's colour and, over it, the page's background to leave only a border: with a see-through background the
    // whole block shows, solid. Thin the block out instead: a sheet of glass tinted with the colour of its kind.
    static void messageGlass(QObject *owner)
    {
        QObject *message = owner;
        for (int depth = 0; message && depth < 4 && !QByteArray(message->metaObject()->className()).startsWith("InlineMessage"); ++depth) {
            message = message->parent();
        }
        if (!message || !QByteArray(message->metaObject()->className()).startsWith("InlineMessage") || message->property("_breeze_ng_glass").toBool()) {
            return;
        }
        QPointer<QObject> guard(message);
        auto thin = [guard] {
            if (!guard) {
                return;
            }
            if (auto background = qobject_cast<QQuickItem *>(guard->property("background").value<QObject *>())) {
                // a pale sheet with a tinge of the message's colour, not the colour itself (orange over the glass was a
                // muddy brown block under dark text). Kirigami lays the colour in the background and its children and
                // binds it again: each large coloured item is given the sheet, and given it again when Kirigami
                // writes its own
                MessageSheet *sheet = nullptr;
                const auto laid = background->childItems();
                for (QQuickItem *child : laid) {
                    if (child->objectName() == QLatin1String("breeze-ng-message-sheet")) {
                        sheet = dynamic_cast<MessageSheet *>(child);
                    }
                }
                if (!sheet) {
                    sheet = new MessageSheet(background);
                    sheet->setObjectName(QStringLiteral("breeze-ng-message-sheet"));
                }
                QPointer<MessageSheet> sheetGuard(sheet);
                std::function<void(QQuickItem *, int)> pale = [&](QQuickItem *node, int depth) {
                    const int index = node->metaObject()->indexOfProperty("color");
                    if (node != sheet && index >= 0 && node->width() >= background->width() * 0.5) {
                        QPointer<QQuickItem> item(node);
                        // (the item's own colour goes to the sheet, and the item is emptied; again whenever Kirigami
                        // writes its colour)
                        auto apply = [item, sheetGuard] {
                            if (!item || !sheetGuard) {
                                return;
                            }
                            const QColor now = item->property("color").value<QColor>();
                            if (!now.isValid() || now.alpha() == 0) {
                                return;
                            }
                            // (the message's colour is the background's own; its children, the fill in the theme's
                            // colour and the veil, are only emptied)
                            if (item->parentItem() && sheetGuard->parentItem() == item) {
                                QColor kind = now;
                                kind.setAlpha(255);
                                sheetGuard->setKind(kind, item->property("radius").toReal());
                            } else if (!sheetGuard->hasKind()) {
                                // (a background that is not a coloured item gives the sheet no colour, and the sheet
                                // draws nothing: the message keeps its own fill)
                                return;
                            }
                            QColor none = now;
                            none.setAlpha(0);
                            item->setProperty("color", none);
                        };
                        apply();
                        const QMetaMethod notify = node->metaObject()->property(index).notifySignal();
                        if (notify.isValid() && !node->property("_breeze_ng_pale").toBool()) {
                            node->setProperty("_breeze_ng_pale", true);
                            auto relay = new Relay(node, apply);
                            QObject::connect(node, notify, relay, relay->metaObject()->method(relay->metaObject()->indexOfSlot("fire()")));
                        }
                    }
                    if (depth < 3) {
                        const auto children = node->childItems();
                        for (QQuickItem *child : children) {
                            if (child != sheet) {
                                pale(child, depth + 1);
                            }
                        }
                    }
                };
                pale(background, 0);
                // More room round the content, a message across the top of a page a card a little inside the page's
                // edges, and the badge in place of the icon. A moment later: the message is not complete yet here (its
                // position and paddings unread). Kirigami binds the paddings (controls/InlineMessage.qml: the border's
                // margin plus smallSpacing): written through QQmlProperty, which takes the binding away
                if (!guard->property("_breeze_ng_roomy").toBool()) {
                    guard->setProperty("_breeze_ng_roomy", true);
                    QPointer<QObject> message(guard);
                    QTimer::singleShot(350, guard, [message, sheetGuard] {
                        if (!message || !sheetGuard) {
                            return;
                        }
                        const bool band = message->property("position").toInt() != 0;
                        sheetGuard->setInset(band ? 6 : 0, band ? 4 : 0);
                        for (const char *side : {"leftPadding", "rightPadding"}) {
                            QQmlProperty padding(message, QString::fromLatin1(side));
                            padding.write(padding.read().toReal() + (band ? 12 : 6));
                        }
                        for (const char *side : {"topPadding", "bottomPadding"}) {
                            QQmlProperty padding(message, QString::fromLatin1(side));
                            padding.write(padding.read().toReal() + (band ? 9 : 4));
                        }
                        // (the first Kirigami icon of the content: the message's own)
                        QQuickItem *icon = nullptr;
                        std::function<void(QQuickItem *, int)> find = [&](QQuickItem *node, int depth) {
                            if (!icon && node->inherits("Icon") && node != sheetGuard) {
                                icon = node;
                            }
                            if (!icon && depth < 4) {
                                const auto children = node->childItems();
                                for (QQuickItem *child : children) {
                                    find(child, depth + 1);
                                }
                            }
                        };
                        if (auto content = qobject_cast<QQuickItem *>(message->property("contentItem").value<QObject *>())) {
                            find(content, 0);
                        }
                        // (an icon the application chose stays: only Kirigami's default emblem gives way to the badge)
                        const QObject *chosen = message->property("icon").value<QObject *>();
                        if (chosen && (!chosen->property("name").toString().isEmpty() || chosen->property("source").isValid() && !chosen->property("source").toString().isEmpty())) {
                            return;
                        }
                        sheetGuard->setBadge(icon, message->property("type").toInt());
                        // (the badge follows the message's type: a message used again for another kind)
                        const int typeIndex = message->metaObject()->indexOfProperty("type");
                        const QMetaMethod typeChanged = typeIndex >= 0 ? message->metaObject()->property(typeIndex).notifySignal() : QMetaMethod();
                        if (typeChanged.isValid()) {
                            QPointer<QQuickItem> iconGuard(icon);
                            auto relay = new Relay(message, [message, sheetGuard, iconGuard] {
                                if (message && sheetGuard) {
                                    sheetGuard->setBadge(iconGuard, message->property("type").toInt());
                                }
                            });
                            QObject::connect(message, typeChanged, relay, relay->metaObject()->method(relay->metaObject()->indexOfSlot("fire()")));
                        }
                    });
                }
                background->setOpacity(1.0);
                guard->setProperty("_breeze_ng_glass", true);
            }
        };
        thin();
        QTimer::singleShot(0, message, thin);
        QTimer::singleShot(300, message, thin);
    }

    Look m_look;
    LookNotifier *m_notifier = nullptr;
    PlatformPluginFactory *m_real = nullptr;
    bool m_searched = false;
    bool m_floatingPending = false;
};

#include "plugin.moc"
