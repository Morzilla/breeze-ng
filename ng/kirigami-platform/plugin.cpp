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
#include <QGuiApplication>
#include <QPalette>
#include <QPluginLoader>
#include <QPointer>
#include <QTimer>
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
    qreal lights = 1.0; // strength of the lights behind the window
    int grain = 30; // the grain over them
};

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
    const int body = qBound(10, common.readEntry("AuroraBodyOpacity", 52), 95);
    const int panel = qBound(0, common.readEntry("AuroraPanelVeil", 7), 40);
    const QColor window = QGuiApplication::palette().color(QPalette::Window);
    const bool dark = window.lightnessF() < 0.5;
    look.dark = dark;
    look.lights = qBound(0, common.readEntry("AuroraLights", 100), 180) / 100.0;
    look.grain = qBound(0, common.readEntry("AuroraGrain", 30), 100);
    look.sheet = window;
    look.sheet.setAlphaF(qMin(body + (dark ? (lightGlass ? 0 : 22) : 18), 95) / 100.0); // as Helper::windowSheetAlpha in the widget style
    // Some components take a background colour and make it solid, or mix it with the text colour: a clear black
    // turned their cards black, a white veil turned them white. So "nothing" is the window's colour with no alpha,
    // and the veil is a tone lifted from it at an alpha that weighs as the veil of the widget style does.
    look.ground = window;
    look.ground.setAlpha(0);
    // (the veil is ink, as in the widget style: a grey tone lifted from the window took the colour out of what lay
    // under it, an olive cast on the lights. The dark slab under Kirigami's cards, which made a white veil a white
    // card, is faded out: see cardShadow)
    look.veil = dark ? QColor(255, 255, 255) : QColor(0, 0, 0);
    look.veil.setAlphaF(panel / 100.0 * (dark ? 1.0 : 0.75));
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
        painter->fillRect(rect, m_look.sheet); // the window's glass
        const qreal strength = (m_look.dark ? 1.0 : 0.70) * (m_window->isActive() ? 1.0 : 0.6) * m_look.lights;
        if (strength > 0.0) {
            const QColor highlight = QGuiApplication::palette().color(QPalette::Active, QPalette::Highlight);
            const qreal hue = highlight.hslHueF() < 0 ? 0.58 : highlight.hslHueF();
            const qreal saturation = qBound(0.60, highlight.hslSaturationF() * 1.4, 0.95);
            const QColor accent = QColor::fromHslF(hue, saturation, 0.62);
            const QColor second = QColor::fromHslF(std::fmod(hue + 0.17, 1.0), saturation, 0.64);
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
            light(rect.topLeft(), radius, accent, 0.52);
            light(rect.topRight(), radius * 0.8, second, 0.36);
            light(rect.bottomRight(), radius * 0.9, accent, 0.32);
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

// is this object in a Qt Quick view that the widget style treats as a sidebar (it marks the view's offscreen window)?
bool sideView(const QObject *owner)
{
    for (const QObject *object = owner; object; object = object->parent()) {
        if (auto item = qobject_cast<const QQuickItem *>(object)) {
            return item->window() && item->window()->property("_breeze_ng_side").toBool();
        }
    }
    return false;
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
        const Look look = m_look;
        auto apply = [guard, owner, look, this] {
            if (!guard) {
                return;
            }
            const bool window = owner && owner->inherits("QQuickWindow") && !owner->inherits("QQuickWidgetOffscreenWindow");
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
                const bool veil = above == 1 && !sideView(owner);
                guard->setCustomBackgroundColor(veil ? look.veil : look.ground);
                guard->setCustomAlternateBackgroundColor(veil ? look.veil : look.ground);
                guard->setProperty("_breeze_ng_glass", veil ? 5 : 6);
                cardShadow(owner);
            messageGlass(owner);
                messageGlass(owner);
                return;
            }
            const bool view = guard->colorSet() == PlatformTheme::View && !window;
            // (a sidebar view inside a window of widgets has its card painted under it by the widget style, which
            // marks the view's window: no veil of its own on top of that)
            const bool veiled = view && !onVeil(owner) && !sideView(owner);
            // (the window's own theme is nothing too: the sheet is painted once, by the backdrop; as the window's
            // colour it was laid again by every item that took its colour from the window)
            const QColor background = veiled ? look.veil : look.ground;
            guard->setCustomBackgroundColor(background);
            guard->setCustomAlternateBackgroundColor(veiled ? look.veil : look.ground);
            guard->setProperty("_breeze_ng_glass", window ? 4 : veiled ? 1 : view ? 3 : 2); // (the widget style's own pass leaves these alone)
            cardShadow(owner);
            messageGlass(owner);
            if (window && !owner->property("_breeze_ng_blur").toBool()) {
                owner->setProperty("_breeze_ng_blur", true);
                owner->installEventFilter(new BlurBehind(owner));
                if (auto quickWindow = qobject_cast<QQuickWindow *>(owner.data())) {
                    new AuroraBackdrop(quickWindow, look);
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
