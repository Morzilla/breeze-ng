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

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QLibraryInfo>
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
#include <QPixmap>
#include <QQuickItem>
#include <QQuickPaintedItem>
#include <QQuickWindow>
#include <QRadialGradient>
#include <cmath>

#include <dlfcn.h>

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
    qreal lights = 1.0; // strength of the lights behind the window
    int grain = 30; // the grain over them
    bool dim = true; // windows in the background dim their lights
    QColor second; // the second light, if the user chose its colour
    bool accentRight = false; // the accent's light from the top right, the second light from the top left
    bool fullSelection = false; // selections in the full accent with white text
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
    const QString secondAccent = common.readEntry("AuroraSecondAccent", QString());
    look.second = secondAccent.isEmpty() ? QColor() : QColor(secondAccent);
    look.accentRight = common.readEntry("AuroraAccentRight", false);
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

// The lights and the grain of Aurora behind the content of a Qt Quick window, as the widget style paints them in its
// windows (Helper::renderAuroraLights) and the window decoration in the title bar: the same centres, so that the
// title bar and the window are one sheet.
class AuroraBackdrop : public QQuickPaintedItem
{
public:
    AuroraBackdrop(QQuickWindow *window, const Look &look)
        : QQuickPaintedItem(window->contentItem())
        , m_window(window)
        , m_look(look)
    {
        setZ(-100000);
        setAntialiasing(false);
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
        const QRectF rect(0, 0, width(), height());
        colours(m_look); // (with the palette of now)
        painter->fillRect(rect, m_look.sheet); // the window's glass
        const qreal strength = (m_look.dark ? 1.0 : 0.95) * ((m_window->isActive() || !m_look.dim) ? 1.0 : 0.6) * m_look.lights;
        if (strength > 0.0) {
            const QColor highlight = QGuiApplication::palette().color(QPalette::Active, QPalette::Highlight);
            const qreal hue = highlight.hslHueF() < 0 ? 0.58 : highlight.hslHueF();
            const qreal saturation = (highlight.hslSaturationF() < 0.15 ? highlight.hslSaturationF() : qBound(0.60, highlight.hslSaturationF() * 1.4, 0.95));
            const QColor accent = QColor::fromHslF(hue, saturation, (m_look.dark) ? 0.62 : 0.90);
            QColor second = QColor::fromHslF(std::fmod(hue + 0.17, 1.0), saturation, (m_look.dark) ? 0.64 : 0.91);
            if (m_look.second.isValid()) {
                second = QColor::fromHslF(m_look.second.hslHueF() < 0 ? hue : m_look.second.hslHueF(), (m_look.second.hslSaturationF() < 0.15 ? m_look.second.hslSaturationF() : qBound(0.60, m_look.second.hslSaturationF() * 1.4, 0.95)), (m_look.dark) ? 0.64 : 0.91);
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
                wash.setAlphaF(qMin(1.0, 0.20 * strength));
                painter->fillRect(rect, wash);
            }
            light(m_look.accentRight ? rect.topRight() : rect.topLeft(), radius, accent, 0.52); // (as the widget style)
            light(m_look.accentRight ? rect.topLeft() : rect.topRight(), radius * 0.8, second, 0.36);
            light(m_look.accentRight ? rect.bottomLeft() : rect.bottomRight(), radius * 0.9, accent, 0.32);
        }
        if (m_look.grain > 0) {
            painter->drawTiledPixmap(rect, grainTile(m_look.grain));
        }
    }

private:
    static const QPixmap &grainTile(int grain)
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

    QQuickWindow *m_window;
    Look m_look;
};

// debugging: BREEZE_NG_DEBUG_QUICK=1 lists the large coloured items of each window a few seconds after it opens
void dumpItems(QQuickItem *item, int depth)
{
    if (!item || depth > 80) {
        return;
    }
    const QVariant colour = item->property("color");
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
    for (const char *name : {"leftPadding", "rightPadding"}) {
        owner->setProperty(name, owner->property(name).toReal() + inset);
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
            if (node->inherits("QQuickItemDelegate") && !node->property("_breeze_ng_air").toBool()) {
                node->setProperty("_breeze_ng_air", true);
                giveAir(node);
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
    QObject::connect(window, &QQuickWindow::afterAnimating, window, [walk, last] {
        if (!last->isValid() || last->elapsed() > 250) {
            last->start();
            walk();
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
                const bool skip = bare || !node->isEnabled() || node->opacity() < 0.75 || colour.alphaF() < 0.75
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
                    const QColor shadow = colour.lightnessF() > 0.5 ? QColor(0, 0, 0, 110) : QColor(255, 255, 255, 120);
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

class BreezeNgKirigamiFactory : public PlatformPluginFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PlatformPluginFactory_iid)
    Q_INTERFACES(Kirigami::Platform::PlatformPluginFactory)

public:
    explicit BreezeNgKirigamiFactory(QObject *parent = nullptr)
        : PlatformPluginFactory(parent)
        , m_look(readLook())
    {
    }

    PlatformTheme *createPlatformTheme(QObject *parent) override
    {
        PlatformPluginFactory *factory = real();
        PlatformTheme *theme = factory ? factory->createPlatformTheme(parent) : nullptr;
        if (theme && m_look.aurora) {
            glass(theme, parent);
        }
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
            const QString file = QFileInfo(path + QStringLiteral("/kf6/kirigami/platform/org.kde.desktop.so")).canonicalFilePath();
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
                    new AuroraBackdrop(quickWindow, look);
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
                background->setOpacity(0.34);
                guard->setProperty("_breeze_ng_glass", true);
            }
        };
        thin();
        QTimer::singleShot(0, message, thin);
        QTimer::singleShot(300, message, thin);
    }

    Look m_look;
    PlatformPluginFactory *m_real = nullptr;
    bool m_searched = false;
};

#include "plugin.moc"
