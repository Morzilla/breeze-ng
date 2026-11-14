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

#include <dlfcn.h>

using namespace Kirigami::Platform;

namespace
{
struct Look {
    bool aurora = false;
    QColor sheet; // the window's glass
    QColor veil; // what lists stand on
    QColor ground; // everything else: nothing, in the window's own colour
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
    look.aurora = theme.compare(QLatin1String("Breeze NG Aurora"), Qt::CaseInsensitive) == 0 && style.compare(QLatin1String("breezeng"), Qt::CaseInsensitive) == 0;
    if (!look.aurora) {
        return look;
    }
    const KConfigGroup common = KSharedConfig::openConfig(QStringLiteral("breezengrc"))->group(QStringLiteral("Common"));
    const int body = qBound(10, common.readEntry("AuroraBodyOpacity", 52), 95);
    const int panel = qBound(0, common.readEntry("AuroraPanelVeil", 7), 40);
    const QColor window = QGuiApplication::palette().color(QPalette::Window);
    const bool dark = window.lightnessF() < 0.5;
    look.sheet = window;
    look.sheet.setAlphaF((dark ? body : qMin(body + 18, 95)) / 100.0);
    // Some components take a background colour and make it solid, or mix it with the text colour: a clear black
    // turned their cards black, a white veil turned them white. So "nothing" is the window's colour with no alpha,
    // and the veil is a tone lifted from it at an alpha that weighs as the veil of the widget style does.
    look.ground = window;
    look.ground.setAlpha(0);
    const QColor ink = dark ? QColor(255, 255, 255) : QColor(0, 0, 0);
    const qreal lift = 0.30;
    look.veil = QColor::fromRgbF(window.redF() + (ink.redF() - window.redF()) * lift, window.greenF() + (ink.greenF() - window.greenF()) * lift,
                                 window.blueF() + (ink.blueF() - window.blueF()) * lift);
    look.veil.setAlphaF(qMin(0.9, panel / 100.0 * (dark ? 1.0 : 0.75) / lift));
    return look;
}

// what floats over the page keeps the solid colours of the scheme
bool floats(const QObject *object)
{
    for (int depth = 0; object && depth < 48; object = object->parent(), ++depth) {
        const QByteArray name = object->metaObject()->className();
        if (name.contains("Drawer")) {
            // a drawer that stays open beside the page (not modal) is a sidebar: glass, like the page
            const QVariant modal = object->property("modal");
            if (modal.isValid() && !modal.toBool()) {
                continue;
            }
            return true;
        }
        if (name.contains("Popup") || name.contains("Dialog") || name.contains("Menu") || name.contains("ToolTip") || name.contains("Overlay") || name.contains("Sheet")) {
            return true;
        }
    }
    return false;
}

// an item of the View set inside another one already stands on its veil: a second veil would make it lighter
bool onVeil(const QObject *owner)
{
    for (const QObject *object = owner ? owner->parent() : nullptr; object; object = object->parent()) {
        const auto children = object->children();
        for (const QObject *child : children) {
            if (child->inherits("Kirigami::Platform::PlatformTheme") && child->property("_breeze_ng_glass").toInt() == 1) {
                return true;
            }
        }
    }
    return false;
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
            const bool view = guard->colorSet() == PlatformTheme::View && !window;
            const bool veiled = view && !onVeil(owner);
            const QColor background = window ? look.sheet : veiled ? look.veil : look.ground;
            guard->setCustomBackgroundColor(background);
            guard->setCustomAlternateBackgroundColor(veiled ? look.veil : look.ground);
            guard->setProperty("_breeze_ng_glass", veiled ? 1 : view ? 3 : 2); // (the widget style's own pass leaves these alone)
            if (window && !owner->property("_breeze_ng_blur").toBool()) {
                owner->setProperty("_breeze_ng_blur", true);
                owner->installEventFilter(new BlurBehind(owner));
            }
        };
        apply();
        QObject::connect(theme, &PlatformTheme::colorSetChanged, theme, apply);
        // the item is given its parent, and a popup its content, a moment after the theme is asked for
        QTimer::singleShot(0, theme, apply);
    }

    Look m_look;
    PlatformPluginFactory *m_real = nullptr;
    bool m_searched = false;
};

#include "plugin.moc"
