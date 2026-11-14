/*
 * SPDX-FileCopyrightText: 2014 Hugo Pereira Da Costa <hugo.pereira@free.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "breezehelper.h"
#include "breezestyleconfigdata.h"

#include "breeze.h"
#include "breezepropertynames.h"

#include <KColorScheme>
#include <KColorUtils>
#include <KIconLoader>
#include <KWindowSystem>
#include <qobject.h>
#if __has_include(<KX11Extras>)
#include <KX11Extras>
#endif

#include <QApplication>
#include <QCursor>
#include <cmath>
#include <QDockWidget>
#include <QFileInfo>
#include <QMainWindow>
#include <QMdiArea>
#include <QMenuBar>
#include <QPainter>
#include <QStyleOption>
#include <QTreeView>
#include <QWindow>

#include <QDialog>
#include <algorithm>

namespace Breeze
{
//* contrast for arrow and treeline rendering
static const qreal arrowShade = 0.15;

static const qreal highlightBackgroundAlpha = Metrics::Blend_Value;

static const auto radioCheckSunkenDarkeningFactor = 110;

PaletteChangedEventFilter::PaletteChangedEventFilter(Helper *helper)
    : QObject(helper)
    , _helper(helper)
{
}

bool PaletteChangedEventFilter::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::ApplicationPaletteChange || watched != qApp) {
        return QObject::eventFilter(watched, event);
    }
    if (!qApp->property("KDE_COLOR_SCHEME_PATH").isValid()) {
        return QObject::eventFilter(watched, event);
    }
    const auto path = qApp->property("KDE_COLOR_SCHEME_PATH").toString();
    if (!path.isEmpty()) {
        KConfig config(path, KConfig::SimpleConfig);
        KConfigGroup group(config.group(QStringLiteral("WM")));
        const QPalette palette(QApplication::palette());
        _helper->_activeTitleBarColor = group.readEntry("activeBackground", palette.color(QPalette::Active, QPalette::Highlight));
        _helper->_activeTitleBarTextColor = group.readEntry("activeForeground", palette.color(QPalette::Active, QPalette::HighlightedText));
        _helper->_inactiveTitleBarColor = group.readEntry("inactiveBackground", palette.color(QPalette::Disabled, QPalette::Highlight));
        _helper->_inactiveTitleBarTextColor = group.readEntry("inactiveForeground", palette.color(QPalette::Disabled, QPalette::HighlightedText));
    }
    return QObject::eventFilter(watched, event);
}

//____________________________________________________________________
Helper::Helper(KSharedConfig::Ptr config)
    : QObject()
    , _config(std::move(config))
    , _kwinConfig(KSharedConfig::openConfig("kwinrc"))
    , _eventFilter(new PaletteChangedEventFilter(this))
{
}

//____________________________________________________________________
KSharedConfig::Ptr Helper::config() const
{
    return _config;
}

//____________________________________________________________________
void Helper::loadConfig()
{
    _viewFocusBrush = KStatefulBrush(KColorScheme::View, KColorScheme::FocusColor);
    _viewHoverBrush = KStatefulBrush(KColorScheme::View, KColorScheme::HoverColor);
    _viewNegativeTextBrush = KStatefulBrush(KColorScheme::View, KColorScheme::NegativeText);
    _viewNeutralTextBrush = KStatefulBrush(KColorScheme::View, KColorScheme::NeutralText);

    const QPalette palette(QApplication::palette());
    _config->reparseConfiguration();
    _kwinConfig->reparseConfiguration();
    _cachedAutoValid = false;

    // Breeze NG: which look
    {
        const QString theme = _kwinConfig->group(QStringLiteral("org.kde.kdecoration2")).readEntry("theme", QString());
        _aurora = false;
        _auroraLight = false;
        if (theme.startsWith(QLatin1String("Breeze NG Aurora"), Qt::CaseInsensitive)) {
            // "Breeze NG Aurora": dark tones; "Breeze NG Aurora Light": the same effects on a lighter, airier glass
            _glass = true;
            _aurora = true;
            _auroraLight = theme.endsWith(QLatin1String("Light"), Qt::CaseInsensitive);
        } else if (theme.compare(QLatin1String("Breeze NG Glass"), Qt::CaseInsensitive) == 0) {
            _glass = true;
        } else if (theme.compare(QLatin1String("Breeze NG Soft"), Qt::CaseInsensitive) == 0) {
            _glass = false;
        } else {
            StyleConfigData::self()->load();
            _aurora = StyleConfigData::variant() == StyleConfigData::Aurora;
            _glass = _aurora || StyleConfigData::variant() == StyleConfigData::Glass;
        }
    }

    KConfigGroup globalGroup(_config->group(QStringLiteral("WM")));
    _activeTitleBarColor = globalGroup.readEntry("activeBackground", palette.color(QPalette::Active, QPalette::Highlight));
    _activeTitleBarTextColor = globalGroup.readEntry("activeForeground", palette.color(QPalette::Active, QPalette::HighlightedText));
    _inactiveTitleBarColor = globalGroup.readEntry("inactiveBackground", palette.color(QPalette::Disabled, QPalette::Highlight));
    _inactiveTitleBarTextColor = globalGroup.readEntry("inactiveForeground", palette.color(QPalette::Disabled, QPalette::HighlightedText));
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    _contrastBias = KColorScheme::frameContrast();
#else
    _contrastBias = 0.2;
#endif

    if (const QString colorSchemePath = qApp->property("KDE_COLOR_SCHEME_PATH").toString(); !colorSchemePath.isEmpty()) {
        KConfig config(colorSchemePath, KConfig::SimpleConfig);
        KConfigGroup appGroup(config.group(QStringLiteral("WM")));
        _activeTitleBarColor = appGroup.readEntry("activeBackground", _activeTitleBarColor);
        _activeTitleBarTextColor = appGroup.readEntry("activeForeground", _activeTitleBarTextColor);
        _inactiveTitleBarColor = appGroup.readEntry("inactiveBackground", _inactiveTitleBarColor);
        _inactiveTitleBarTextColor = appGroup.readEntry("inactiveForeground", _inactiveTitleBarTextColor);
    }
}

void Helper::installEventFilter(QApplication *app) const
{
    if (app) {
        app->installEventFilter(_eventFilter);
    }
}

void Helper::removeEventFilter(QApplication *app) const
{
    if (app) {
        app->removeEventFilter(_eventFilter);
    }
}

QColor transparentize(const QColor &color, qreal amount)
{
    auto clone = color;
    clone.setAlphaF(amount);
    return clone;
}

//____________________________________________________________________

bool Helper::isDark(const QPalette &palette)
{
    return palette.color(QPalette::Window).lightnessF() < 0.5;
}

QColor Helper::ink(const QPalette &palette)
{
    return isDark(palette) ? QColor(255, 255, 255) : QColor(0, 0, 0);
}

QColor Helper::menuSheet(const QPalette &palette, bool alpha, QColor *outline, bool joined) const
{
    QColor background = frameBackgroundColor(palette);
    if (_aurora) {
        // Aurora: a menu is glass like the window it opens on, a tone lifted from the window's colour (the darker
        // frame colour at 78 % was a dark block on the glass)
        background = KColorUtils::mix(palette.color(QPalette::Window), ink(palette), 0.10);
    }
    // a menu joined to a button is opaque: only then can the button be painted in exactly its colour (Aurora: the
    // menu stays see-through and blurred, and the button is laid from nothing in the same sheet: joinedFill)
    // (38 %: behind a menu lies its own window, already glass: a thin sheet over its blur reads as the same glass)
    const int glassOpacity = _aurora ? 38 : 78;
    const int menuOpacity = (joined && !_aurora) ? 100 : ((isGlass() && StyleConfigData::menuOpacity() >= 100) ? glassOpacity : StyleConfigData::menuOpacity());
    QColor edge = isGlass() ? alphaColor(ink(palette), 0.16) : frameOutlineColor(palette);
    if (menuOpacity < 100 && alpha) {
        background.setAlphaF(menuOpacity / 100.0);
        edge = alphaColor(ink(palette), isGlass() ? 0.16 : 0.12);
    } else if (isSoft()) {
        edge = alphaColor(ink(palette), 0.10);
    }
    if (outline) {
        *outline = edge;
    }
    return background;
}

void Helper::renderNgGlow(QPainter *painter, const QRectF &rect, const QColor &color, qreal strength, qreal radius) const
{
    if (strength <= 0.0 || !color.isValid()) {
        return;
    }
    if (radius < 0) {
        radius = frameRadius(PenWidth::Frame);
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(Qt::NoBrush);
    if (!StyleConfigData::focusGlow()) {
        // the glow switched off in the settings: a thin outline in the same colour says it instead
        QColor ring = color;
        ring.setAlphaF(qBound(0.0, 0.9 * strength, 1.0));
        painter->setPen(QPen(ring, 1.5));
        painter->drawRoundedRect(rect.adjusted(0.75, 0.75, -0.75, -0.75), qMax(0.0, radius - 0.5), qMax(0.0, radius - 0.5));
        painter->restore();
        return;
    }
    // four rings, from the edge inwards, each fainter: the light bleeds into the field
    const qreal alphas[4] = {0.95, 0.45, 0.22, 0.10};
    for (int i = 0; i < 4; ++i) {
        QColor ring = color;
        ring.setAlphaF(qBound(0.0, alphas[i] * strength, 1.0));
        painter->setPen(QPen(ring, 1));
        const qreal inset = 0.5 + i;
        painter->drawRoundedRect(rect.adjusted(inset, inset, -inset, -inset), qMax(0.0, radius - i), qMax(0.0, radius - i));
    }
    painter->restore();
}

qreal Helper::windowSheetAlpha() const
{
    const int opacity = qBound(20, StyleConfigData::windowOpacity(), 100);
    // Aurora: more see-through; less so under a light scheme, where a dark desktop behind would turn the window grey
    if (_aurora) {
        const int body = qBound(10, StyleConfigData::auroraBodyOpacity(), 95);
        // the setting is the glass of Aurora Light; Aurora (dark tones) holds more of the window's colour; under a
        // light colour scheme a dark desktop behind would turn the window grey: less see-through there too
        return qMin(body + (isDark(QApplication::palette()) ? (_auroraLight ? 0 : 22) : 18), 95) / 100.0;
    }
    return opacity / 100.0;
}

qreal Helper::auroraVeil(const QPalette &palette, bool button) const
{
    const int percent = qBound(0, button ? StyleConfigData::auroraButtonVeil() : StyleConfigData::auroraPanelVeil(), 40);
    return percent / 100.0 * (isDark(palette) ? 1.0 : 0.75); // black ink weighs more than white
}

QColor Helper::auroraAccent(const QPalette &palette, qreal alpha) const
{
    const QColor highlight = palette.color(QPalette::Active, QPalette::Highlight);
    const qreal hue = highlight.hslHueF() < 0 ? 0.58 : highlight.hslHueF();
    const qreal saturation = qBound(0.55, highlight.hslSaturationF() * 1.3, 0.90);
    QColor accent = QColor::fromHslF(hue, saturation, isDark(palette) ? 0.66 : 0.52);
    accent.setAlphaF(alpha);
    return accent;
}

// Breeze NG Aurora: a fine grain for the lights. Their gradients are faint (a few levels of alpha over hundreds of
// pixels) and showed bands, as if the screen had 16 bits; a noise of two or three levels breaks the bands up.
static const QPixmap &ngGrainTile(int grain)
{
    // grain 0-100: the noise spans from 2 to 12 levels of alpha either way (a 6 bit panel needs about 4 levels of the
    // picture to hide its steps)
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
                const int value = int(seed % quint32(2 * reach + 1)) - reach; // white above zero, black below, as alpha
                line[x] = value >= 0 ? qPremultiply(qRgba(255, 255, 255, value)) : qPremultiply(qRgba(0, 0, 0, -value));
            }
        }
        tile = QPixmap::fromImage(image);
    }
    return tile;
}

void Helper::renderAuroraLights(QPainter *painter, const QRectF &windowRect, const QPalette &palette, bool active) const
{
    // three soft lights: the accent from the top left corner, a neighbouring hue from the top right, the accent again
    // from the bottom right. The window decoration draws the same two top ones in the title bar, from the same
    // centres (the top corners of the window's contents), so they run on without a seam.
    // lights, not paint: the hue of the accent, but always luminous (a scheme's highlight may be a dark blue, which
    // would dim the window instead of lighting it)
    const QColor highlight = palette.color(QPalette::Active, QPalette::Highlight);
    const qreal hue = highlight.hslHueF() < 0 ? 0.58 : highlight.hslHueF();
    const qreal saturation = qBound(0.60, highlight.hslSaturationF() * 1.4, 0.95);
    const QColor accent = QColor::fromHslF(hue, saturation, 0.62);
    // the second light: the user's own colour (its hue, made luminous like the accent's), or a hue next to the accent's
    QColor second = QColor::fromHslF(std::fmod(hue + 0.17, 1.0), saturation, 0.64);
    if (const QColor chosen(StyleConfigData::auroraSecondAccent()); chosen.isValid() && !StyleConfigData::auroraSecondAccent().isEmpty()) {
        second = QColor::fromHslF(chosen.hslHueF() < 0 ? hue : chosen.hslHueF(), qBound(0.60, chosen.hslSaturationF() * 1.4, 0.95), 0.64);
    }
    const qreal strength = (isDark(palette) ? 1.0 : 0.70) * ((active || !StyleConfigData::auroraDimInactive()) ? 1.0 : 0.6) * qBound(0, StyleConfigData::auroraLights(), 180) / 100.0;
    if (strength <= 0.0) {
        return;
    }
    const qreal radius = qBound(260.0, windowRect.width() * 0.55, 620.0);
    auto light = [&](const QPointF &centre, qreal r, QColor colour, qreal alpha) {
        QRadialGradient gradient(centre, r);
        colour.setAlphaF(qMin(1.0, alpha * strength));
        gradient.setColorAt(0.0, colour);
        colour.setAlphaF(qMin(1.0, alpha * strength * 0.46));
        gradient.setColorAt(0.45, colour);
        colour.setAlphaF(0.0);
        gradient.setColorAt(1.0, colour);
        painter->fillRect(windowRect, gradient);
    };
    light(windowRect.topLeft(), radius, accent, 0.52);
    light(windowRect.topRight(), radius * 0.8, second, 0.36);
    light(windowRect.bottomRight(), radius * 0.9, accent, 0.32);
    if (const int grain = qBound(0, StyleConfigData::auroraGrain(), 100); grain > 0) {
        painter->drawTiledPixmap(windowRect, ngGrainTile(grain), windowRect.topLeft());
    } // (the same grain at the same place at every repaint)
}

bool Helper::glassLayers(const QPainter *painter, QColor *base, QList<QColor> *panels, const QWidget **widgetOut, const QWidget **windowOut) const
{
    if (!painter || !painter->device() || painter->device()->devType() != QInternal::Widget) {
        return false;
    }
    const QWidget *widget = static_cast<const QWidget *>(painter->device());
    const QWidget *window = widget->window();
    if (!window || !window->property("_breeze_ng_glass").toBool() || widget->autoFillBackground()) {
        return false;
    }
    *base = alphaColor(window->palette().color(QPalette::Window), windowSheetAlpha());
    // a control inside a tab page or a group box sits on that panel, not on the bare window: what lies under it is the
    // window sheet with the panel's fill over it (clearing to the bare sheet would punch a hole in the panel)
    const QWidget *child = widget;
    for (const QWidget *parent = widget->parentWidget(); parent && parent != window; child = parent, parent = parent->parentWidget()) {
        if (parent->autoFillBackground() || parent->inherits("QAbstractScrollArea")) {
            return false; // an opaque surface of its own
        }
        if (parent->inherits("QTabWidget") && child->inherits("QTabBar")) {
            continue; // the tab bar is beside the panel of its tab widget, not on it
        }
        if (parent->inherits("QTabWidget")) {
            panels->prepend(tabPanelColor(parent->palette()));
        } else if (parent->inherits("QGroupBox")) {
            panels->prepend(frameBackgroundColor(parent->palette()));
        }
    }
    if (widgetOut) {
        *widgetOut = widget;
    }
    if (windowOut) {
        *windowOut = window;
    }
    return true;
}

QColor Helper::glassSheet(const QPainter *painter) const
{
    QColor sheet;
    QList<QColor> panels;
    if (!glassLayers(painter, &sheet, &panels, nullptr, nullptr)) {
        return QColor();
    }
    auto over = [](const QColor &top, const QColor &bottom) {
        const qreal ta = top.alphaF(), ba = bottom.alphaF();
        const qreal a = ta + ba * (1.0 - ta);
        if (a <= 0.0) {
            return QColor(0, 0, 0, 0);
        }
        QColor result;
        result.setRgbF((top.redF() * ta + bottom.redF() * ba * (1.0 - ta)) / a,
                       (top.greenF() * ta + bottom.greenF() * ba * (1.0 - ta)) / a,
                       (top.blueF() * ta + bottom.blueF() * ba * (1.0 - ta)) / a,
                       a);
        return result;
    };
    for (const QColor &panel : std::as_const(panels)) {
        sheet = over(panel, sheet);
    }
    return sheet;
}

void Helper::clearUnder(QPainter *painter, const QRectF &rect) const
{
    if (_aurora) {
        // Aurora: under a control lie the window's sheet, the lights and then the panels it sits on: lay them again in
        // that order, or the control would stand on a flat patch without the lights around it
        QColor base;
        QList<QColor> panels;
        const QWidget *widget = nullptr;
        const QWidget *window = nullptr;
        if (!glassLayers(painter, &base, &panels, &widget, &window)) {
            return;
        }
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setClipRect(rect, Qt::IntersectClip);
        painter->setCompositionMode(QPainter::CompositionMode_Source);
        painter->fillRect(rect, base);
        painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
        renderAuroraLights(painter, QRectF(widget->mapFrom(window, QPoint(0, 0)), window->size()), window->palette(), window->isActiveWindow());
        for (const QColor &panel : std::as_const(panels)) {
            painter->fillRect(rect, panel);
        }
        painter->restore();
        cutCorners(painter, widget);
        return;
    }
    const QColor sheet = glassSheet(painter);
    if (!sheet.isValid()) {
        return;
    }
    painter->save();
    painter->setCompositionMode(QPainter::CompositionMode_Source);
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->fillRect(rect, sheet);
    painter->restore();
}

void Helper::cutCorners(QPainter *painter, const QWidget *widget) const
{
    const QWidget *window = widget ? widget->window() : nullptr;
    const int radius = window ? window->property("_breeze_ng_radius").toInt() : 0;
    if (radius <= 0) {
        return;
    }
    const QRectF frame(widget->mapFrom(window, QPoint(0, 0)), QSizeF(window->size()));
    QPainterPath outside;
    outside.addRect(frame);
    QPainterPath shape;
    shape.addRoundedRect(frame, radius, radius);
    outside = outside.subtracted(shape);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setCompositionMode(QPainter::CompositionMode_DestinationOut);
    painter->setPen(Qt::NoPen);
    painter->setBrush(Qt::black);
    painter->drawPath(outside);
    painter->restore();
}

void Helper::joinedFill(QPainter *painter, const QPainterPath &path, const QColor &sheet, const QPen &pen) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    // (the sheet goes over the window's glass, not in its place: the menu shows its sheet over the blurred window,
    // the button its sheet over the window itself. Taking the glass away under the button left it lighter.)
    painter->setPen(pen);
    painter->setBrush(sheet);
    painter->drawPath(path);
    painter->restore();
}

QColor Helper::inputBackground(const QPalette &palette) const
{
    const QColor base = palette.color(QPalette::Base);
    // Aurora: the same veil as the buttons, so that every surface has one intensity
    return _aurora ? alphaColor(ink(palette), auroraVeil(palette, true)) : isGlass() ? alphaColor(base, 0.62) : base;
}

QColor Helper::frameOutlineColor(const QPalette &palette, bool mouseOver, bool hasFocus, qreal opacity, AnimationMode mode) const
{
    // Breeze NG: Soft has no outline at rest; Glass has a faint edge of the ink colour
    QColor outline = hasRims() ? alphaColor(ink(palette), 0.13) : QColor(0, 0, 0, 0);

    // focus takes precedence over hover
    if (mode == AnimationFocus) {
        const QColor focus(focusColor(palette));
        const QColor hover(hoverColor(palette));

        if (mouseOver) {
            outline = KColorUtils::mix(hover, focus, opacity);
        } else {
            outline = KColorUtils::mix(outline, focus, opacity);
        }

    } else if (hasFocus) {
        outline = focusColor(palette);

    } else if (mode == AnimationHover) {
        const QColor hover(hoverColor(palette));
        outline = KColorUtils::mix(outline, hover, opacity);

    } else if (mouseOver) {
        outline = hoverColor(palette);
    }

    return outline;
}

//____________________________________________________________________
QColor Helper::focusOutlineColor(const QPalette &palette) const
{
    return KColorUtils::mix(focusColor(palette), palette.color(QPalette::WindowText), 0.15);
}

//____________________________________________________________________
QColor Helper::hoverOutlineColor(const QPalette &palette) const
{
    return KColorUtils::mix(hoverColor(palette), palette.color(QPalette::WindowText), 0.15);
}

//____________________________________________________________________
QColor Helper::sidePanelOutlineColor(const QPalette &palette, bool hasFocus, qreal opacity, AnimationMode mode) const
{
    // Breeze NG: side panels have no outline
    Q_UNUSED(hasFocus)
    Q_UNUSED(opacity)
    Q_UNUSED(mode)
    Q_UNUSED(palette)
    return QColor(0, 0, 0, 0);

    QColor outline(palette.color(QPalette::Inactive, QPalette::Highlight));
    const QColor &focus = palette.color(QPalette::Active, QPalette::Highlight);

    if (mode == AnimationFocus) {
        outline = KColorUtils::mix(outline, focus, opacity);

    } else if (hasFocus) {
        outline = focus;
    }

    return outline;
}

//____________________________________________________________________
QColor Helper::frameBackgroundColor(const QPalette &palette, QPalette::ColorGroup group) const
{
    return KColorUtils::mix(palette.color(group, QPalette::Window), palette.color(group, QPalette::Base), Metrics::Blend_Value);
}

//____________________________________________________________________
QColor Helper::arrowColor(const QPalette &palette, QPalette::ColorGroup group, QPalette::ColorRole role) const
{
    switch (role) {
    case QPalette::Text:
        return KColorUtils::mix(palette.color(group, QPalette::Text), palette.color(group, QPalette::Base), arrowShade);
    case QPalette::WindowText:
        return KColorUtils::mix(palette.color(group, QPalette::WindowText), palette.color(group, QPalette::Window), arrowShade);
    case QPalette::ButtonText:
        return KColorUtils::mix(palette.color(group, QPalette::ButtonText), palette.color(group, QPalette::Button), arrowShade);
    default:
        return palette.color(group, role);
    }
}

//____________________________________________________________________
QColor Helper::arrowColor(const QPalette &palette, bool mouseOver, bool hasFocus, qreal opacity, AnimationMode mode) const
{
    QColor outline(arrowColor(palette, QPalette::WindowText));
    if (mode == AnimationHover) {
        const QColor focus(focusColor(palette));
        const QColor hover(hoverColor(palette));
        if (hasFocus) {
            outline = KColorUtils::mix(focus, hover, opacity);
        } else {
            outline = KColorUtils::mix(outline, hover, opacity);
        }

    } else if (mouseOver) {
        outline = hoverColor(palette);

    } else if (mode == AnimationFocus) {
        const QColor focus(focusColor(palette));
        outline = KColorUtils::mix(outline, focus, opacity);

    } else if (hasFocus) {
        outline = focusColor(palette);
    }

    return outline;
}

//____________________________________________________________________
QColor Helper::sliderOutlineColor(const QPalette &palette, bool mouseOver, bool hasFocus, qreal opacity, AnimationMode mode) const
{
    QColor outline(KColorUtils::mix(palette.color(QPalette::Button), palette.color(QPalette::ButtonText), frameIntensityBias()));

    // hover takes precedence over focus
    if (mode == AnimationHover) {
        const QColor hover(hoverColor(palette));
        const QColor focus(focusColor(palette));
        if (hasFocus) {
            outline = KColorUtils::mix(focus, hover, opacity);
        } else {
            outline = KColorUtils::mix(outline, hover, opacity);
        }

    } else if (mouseOver) {
        outline = hoverColor(palette);

    } else if (mode == AnimationFocus) {
        const QColor focus(focusColor(palette));
        outline = KColorUtils::mix(outline, focus, opacity);

    } else if (hasFocus) {
        outline = focusColor(palette);
    }

    return outline;
}

//____________________________________________________________________
QColor Helper::scrollBarHandleColor(const QPalette &palette, bool mouseOver, bool hasFocus, qreal opacity, AnimationMode mode) const
{
    QColor color(alphaColor(palette.color(QPalette::WindowText), 0.5));

    // hover takes precedence over focus
    if (mode == AnimationHover) {
        const QColor hover(hoverColor(palette));
        const QColor focus(focusColor(palette));
        if (hasFocus) {
            color = KColorUtils::mix(focus, hover, opacity);
        } else {
            color = KColorUtils::mix(color, hover, opacity);
        }

    } else if (mouseOver) {
        color = hoverColor(palette);

    } else if (mode == AnimationFocus) {
        const QColor focus(focusColor(palette));
        color = KColorUtils::mix(color, focus, opacity);

    } else if (hasFocus) {
        color = focusColor(palette);
    }

    return color;
}

//______________________________________________________________________________
QColor Helper::checkBoxIndicatorColor(const QPalette &palette, bool mouseOver, bool active, qreal opacity, AnimationMode mode) const
{
    QColor color(KColorUtils::mix(palette.color(QPalette::Window), palette.color(QPalette::WindowText), 0.6));
    if (mode == AnimationHover) {
        const QColor focus(focusColor(palette));
        const QColor hover(hoverColor(palette));
        if (active) {
            color = KColorUtils::mix(focus, hover, opacity);
        } else {
            color = KColorUtils::mix(color, hover, opacity);
        }

    } else if (mouseOver) {
        color = hoverColor(palette);

    } else if (active) {
        color = focusColor(palette);
    }

    return color;
}

//______________________________________________________________________________
QColor Helper::separatorColor(const QPalette &palette) const
{
    // Breeze NG: no separator lines at all; surfaces are told apart by tone
    Q_UNUSED(palette)
    return QColor(0, 0, 0, 0);

    return KColorUtils::mix(palette.color(QPalette::Window), palette.color(QPalette::WindowText), frameIntensityBias());
}

//______________________________________________________________________________
QPalette Helper::disabledPalette(const QPalette &source, qreal ratio) const
{
    QPalette copy(source);

    const QList<QPalette::ColorRole> roles =
        {QPalette::Window, QPalette::Highlight, QPalette::WindowText, QPalette::ButtonText, QPalette::Text, QPalette::Button};
    for (const QPalette::ColorRole &role : roles) {
        copy.setColor(role, KColorUtils::mix(source.color(QPalette::Active, role), source.color(QPalette::Disabled, role), 1.0 - ratio));
    }

    return copy;
}

//____________________________________________________________________
QColor Helper::alphaColor(QColor color, qreal alpha) const
{
    if (alpha >= 0 && alpha < 1.0) {
        color.setAlphaF(alpha * color.alphaF());
    }
    return color;
}

//______________________________________________________________________________
void Helper::renderDebugFrame(QPainter *painter, const QRectF &rect) const
{
    painter->save();
    painter->setRenderHints(QPainter::Antialiasing);
    painter->setBrush(Qt::NoBrush);
    painter->setPen(Qt::red);
    painter->drawRect(strokedRect(rect));
    painter->restore();
}

//______________________________________________________________________________
void Helper::renderFocusRect(QPainter *painter, const QRectF &rect, const QBrush &brush, const QColor &outline, Sides sides) const
{
    painter->save();
    painter->setRenderHints(QPainter::Antialiasing);
    painter->setBrush(brush);

    if (!(outline.isValid() && sides)) {
        painter->setPen(Qt::NoPen);
        painter->drawRect(rect);

    } else {
        painter->setClipRect(rect);

        QRectF copy(strokedRect(rect));

        const qreal radius(frameRadius(PenWidth::Frame));
        if (!(sides & SideTop)) {
            copy.adjust(0, -radius, 0, 0);
        }
        if (!(sides & SideBottom)) {
            copy.adjust(0, 0, 0, radius);
        }
        if (!(sides & SideLeft)) {
            copy.adjust(-radius, 0, 0, 0);
        }
        if (!(sides & SideRight)) {
            copy.adjust(0, 0, radius, 0);
        }

        painter->setPen(outline);
        // painter->setBrush( Qt::NoBrush );
        painter->drawRoundedRect(copy, radius, radius);
    }

    painter->restore();
}

//______________________________________________________________________________
void Helper::renderFocusLine(QPainter *painter, const QRectF &rect, const QColor &color) const
{
    if (!color.isValid()) {
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setBrush(Qt::NoBrush);
    painter->setPen(color);

    painter->translate(0, 2);
    painter->drawLine(rect.bottomLeft(), rect.bottomRight());
    painter->restore();
}

//______________________________________________________________________________
void Helper::renderFrameWithSides(QPainter *painter, const QRectF &rect, const QColor &color, Qt::Edges edges, const QColor &outline) const
{
    painter->save();

    painter->setRenderHint(QPainter::Antialiasing);

    QRectF frameRect(rect);

    // set brush
    painter->setBrush(color);
    painter->setPen(Qt::NoPen);

    // render
    painter->drawRect(frameRect);

    // set brush again
    painter->setBrush(Qt::NoBrush);
    painter->setPen(outline);

    // manually apply the effects of StrokedRect here but only to the edges with a frame
    if (edges & Qt::LeftEdge) {
        frameRect.adjust(0.5, 0.0, 0.0, 0.0);
    }
    if (edges & Qt::RightEdge) {
        frameRect.adjust(0.0, 0, -0.5, 0.0);
    }
    if (edges & Qt::TopEdge) {
        frameRect.adjust(0.0, 0.5, 0.0, 0.0);
    }
    if (edges & Qt::BottomEdge) {
        frameRect.adjust(0.0, 0.0, 0.0, -0.5);
    }

    // draw lines
    if (edges & Qt::LeftEdge) {
        painter->drawLine(QLineF(frameRect.topLeft(), frameRect.bottomLeft()));
    }
    if (edges & Qt::RightEdge) {
        painter->drawLine(QLineF(frameRect.topRight(), frameRect.bottomRight()));
    }
    if (edges & Qt::TopEdge) {
        painter->drawLine(QLineF(frameRect.topLeft(), frameRect.topRight()));
    }
    if (edges & Qt::BottomEdge) {
        painter->drawLine(QLineF(frameRect.bottomLeft(), frameRect.bottomRight()));
    }

    painter->restore();
}

//______________________________________________________________________________
void Helper::renderFrame(QPainter *painter, const QRectF &rect, const QColor &color, const QColor &outline) const
{
    painter->setRenderHint(QPainter::Antialiasing);

    QRectF frameRect(rect);
    qreal radius(frameRadius(PenWidth::NoPen));

    // set pen
    if (outline.isValid()) {
        painter->setPen(outline);
        frameRect = strokedRect(frameRect);
        radius = frameRadiusForNewPenWidth(radius, PenWidth::Frame);

    } else {
        painter->setPen(Qt::NoPen);
    }

    // set brush
    if (color.isValid()) {
        painter->setBrush(color);
    } else {
        painter->setBrush(Qt::NoBrush);
    }

    // render
    painter->drawRoundedRect(frameRect, radius, radius);
}

//______________________________________________________________________________
void Helper::renderSidePanelFrame(QPainter *painter, const QRectF &rect, const QColor &outline, Side side) const
{
    // check color
    if (!outline.isValid()) {
        return;
    }

    // adjust rect
    QRectF frameRect(strokedRect(rect));

    // setup painter
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(outline);

    // render
    switch (side) {
    default:
    case SideLeft:
        painter->drawLine(frameRect.topRight(), frameRect.bottomRight());
        break;

    case SideTop:
        painter->drawLine(frameRect.topLeft(), frameRect.topRight());
        break;

    case SideRight:
        painter->drawLine(frameRect.topLeft(), frameRect.bottomLeft());
        break;

    case SideBottom:
        painter->drawLine(frameRect.bottomLeft(), frameRect.bottomRight());
        break;

    case AllSides: {
        const qreal radius(frameRadius(PenWidth::Frame));
        painter->drawRoundedRect(frameRect, radius, radius);
        break;
    }
    }
}

//______________________________________________________________________________
void Helper::renderMenuFrame(QPainter *painter, const QRectF &rect, const QColor &color, const QColor &outline, bool roundCorners, Qt::Edges seamlessEdges)
    const
{
    painter->save();

    // set brush
    if (color.isValid()) {
        painter->setBrush(color);
    } else {
        painter->setBrush(Qt::NoBrush);
    }

    // We simulate being able to independently adjust corner radii by
    // setting a clip region and then extending the rectangle beyond it.
    if (seamlessEdges != Qt::Edges()) {
        painter->setClipRect(rect);
    }

    if (roundCorners) {
        painter->setRenderHint(QPainter::Antialiasing);
        QRectF frameRect(rect);

        qreal radius(Metrics::Menu_Radius); // Breeze NG

        frameRect.adjust( //
            seamlessEdges.testFlag(Qt::LeftEdge) ? -radius : 0,
            seamlessEdges.testFlag(Qt::TopEdge) ? -radius : 0,
            seamlessEdges.testFlag(Qt::RightEdge) ? radius : 0,
            seamlessEdges.testFlag(Qt::BottomEdge) ? radius : 0);

        // set pen
        if (outline.isValid()) {
            painter->setPen(outline);
            frameRect = strokedRect(frameRect);
            radius = frameRadiusForNewPenWidth(radius, PenWidth::Frame);

        } else {
            painter->setPen(Qt::NoPen);
        }

        // render
        painter->drawRoundedRect(frameRect, radius, radius);

    } else {
        painter->setRenderHint(QPainter::Antialiasing, false);
        QRectF frameRect(rect);

        frameRect.adjust( //
            seamlessEdges.testFlag(Qt::LeftEdge) ? 1 : 0,
            seamlessEdges.testFlag(Qt::TopEdge) ? 1 : 0,
            seamlessEdges.testFlag(Qt::RightEdge) ? -1 : 0,
            seamlessEdges.testFlag(Qt::BottomEdge) ? -1 : 0);

        if (outline.isValid()) {
            painter->setPen(outline);
            frameRect.adjust(0, 0, -1, -1);

        } else {
            painter->setPen(Qt::NoPen);
        }

        painter->drawRect(frameRect);
    }

    painter->restore();
}

QRegion Helper::menuFrameRegion(const QMenu *widget)
{
    if (!widget) {
        return {};
    }

    const bool hasAlpha(hasAlphaChannel(widget));
    const auto seamlessEdges = menuSeamlessEdges(widget);
    const auto roundCorners = hasAlpha;

    if (roundCorners) {
        QRectF frameRect(widget->rect());

        qreal radius(Metrics::Menu_Radius); // Breeze NG

        frameRect.adjust( //
            seamlessEdges.testFlag(Qt::LeftEdge) ? -radius : 0,
            seamlessEdges.testFlag(Qt::TopEdge) ? -radius : 0,
            seamlessEdges.testFlag(Qt::RightEdge) ? radius : 0,
            seamlessEdges.testFlag(Qt::BottomEdge) ? radius : 0);

        // outline is always valid/drawn
        frameRect = strokedRect(frameRect);
        radius = frameRadiusForNewPenWidth(radius, PenWidth::Frame);

        QPainterPath path;
        path.addRoundedRect(frameRect, radius, radius);
        return QRegion(path.toFillPolygon().toPolygon()).intersected(widget->rect());
    }

    return QRegion(widget->rect());
}

//______________________________________________________________________________
void Helper::renderButtonFrame(QPainter *painter,
                               const QRectF &rect,
                               const QPalette &palette,
                               const QHash<QByteArray, bool> &stateProperties,
                               qreal bgAnimation,
                               qreal penAnimation) const
{
    bool enabled = stateProperties.value("enabled", true);
    bool visualFocus = stateProperties.value("visualFocus");
    bool hovered = stateProperties.value("hovered");
    bool down = stateProperties.value("down");
    bool checked = stateProperties.value("checked");
    bool flat = stateProperties.value("flat");
    bool defaultButton = stateProperties.value("defaultButton");
    bool hasNeutralHighlight = stateProperties.value("hasNeutralHighlight");
    bool isActiveWindow = stateProperties.value("isActiveWindow");
    const bool roundButton = stateProperties.value("roundButton");

    // don't render background if flat and not hovered, down, checked, or given visual focus
    if (flat && !(hovered || down || checked || visualFocus) && bgAnimation == AnimationData::OpacityInvalid && penAnimation == AnimationData::OpacityInvalid) {
        clearUnder(painter, rect); // Breeze NG Glass: wipe the veil of the last hover
        return;
    }

    renderNgButtonFrame(painter, rect, palette, stateProperties, bgAnimation, penAnimation); // Breeze NG
    if (true) {
        return;
    }

    QRectF shadowedRect = this->shadowedRect(rect);
    QRectF frameRect = strokedRect(shadowedRect);
    // setting color group to work around KColorScheme feature
    const QColor &highlightColor = palette.color(!enabled ? QPalette::Disabled : QPalette::Active, QPalette::Highlight);
    QBrush bgBrush;
    QBrush penBrush;

    // Colors
    if (flat) {
        if (down && enabled) {
            bgBrush = alphaColor(highlightColor, highlightBackgroundAlpha);
        } else if (checked) {
            bgBrush = hasNeutralHighlight ? alphaColor(neutralText(palette), highlightBackgroundAlpha) : alphaColor(palette.buttonText().color(), 0.125);
            penBrush =
                hasNeutralHighlight ? neutralText(palette) : KColorUtils::mix(palette.button().color(), palette.buttonText().color(), frameIntensityBias());
        } else if (isActiveWindow && defaultButton) {
            bgBrush = alphaColor(highlightColor, 0.125);
            penBrush = KColorUtils::mix(highlightColor, KColorUtils::mix(palette.button().color(), palette.buttonText().color(), frameIntensityBias()), 0.5);
        } else {
            bgBrush = alphaColor(highlightColor, 0);
            penBrush = hasNeutralHighlight ? neutralText(palette) : bgBrush;
        }
    } else {
        if (down && enabled) {
            bgBrush = KColorUtils::mix(palette.button().color(), highlightColor, Metrics::Blend_Value);
        } else if (checked) {
            bgBrush = hasNeutralHighlight ? KColorUtils::mix(palette.button().color(), neutralText(palette), Metrics::Blend_Value)
                                          : KColorUtils::mix(palette.button().color(), palette.buttonText().color(), 0.125);
            penBrush =
                hasNeutralHighlight ? neutralText(palette) : KColorUtils::mix(palette.button().color(), palette.buttonText().color(), frameIntensityBias());
        } else if (isActiveWindow && defaultButton) {
            bgBrush = KColorUtils::mix(palette.button().color(), highlightColor, 0.2);
            penBrush = KColorUtils::mix(highlightColor, KColorUtils::mix(palette.button().color(), palette.buttonText().color(), frameIntensityBias()), 0.5);
        } else {
            bgBrush = palette.button().color();
            penBrush =
                hasNeutralHighlight ? neutralText(palette) : KColorUtils::mix(palette.button().color(), palette.buttonText().color(), frameIntensityBias());
        }
    }

    if ((hovered || visualFocus || down) && enabled) {
        penBrush = highlightColor;
    }

    // Animations
    if (bgAnimation != AnimationData::OpacityInvalid && enabled) {
        QColor color1 = bgBrush.color();
        QColor color2 =
            flat ? alphaColor(highlightColor, highlightBackgroundAlpha) : KColorUtils::mix(palette.button().color(), highlightColor, Metrics::Blend_Value);
        bgBrush = KColorUtils::mix(color1, color2, bgAnimation);
    }
    if (penAnimation != AnimationData::OpacityInvalid && enabled) {
        QColor color1 = penBrush.color();
        QColor color2 = highlightColor;
        penBrush = KColorUtils::mix(color1, color2, penAnimation);
    }

    const qreal roundRadius = std::max(rect.width(), rect.height()) / 2;
    // Shadow
    if (isActiveWindow && !(flat || down || checked) && enabled) {
        const qreal shadowRadius = roundButton ? roundRadius : Metrics::Frame_FrameRadius - PenWidth::Shadow / 2;
        renderRoundedRectShadow(painter, shadowedRect, shadowColor(palette), shadowRadius);
    }

    // Render button
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(bgBrush);
    painter->setPen(QPen(penBrush, PenWidth::Frame));
    const qreal radius = roundButton ? roundRadius : frameRadius(PenWidth::Frame);
    painter->drawRoundedRect(frameRect, radius, radius);
}

//______________________________________________________________________________
void Helper::renderNgButtonFrame(QPainter *painter,
                                 const QRectF &rect,
                                 const QPalette &palette,
                                 const QHash<QByteArray, bool> &stateProperties,
                                 qreal bgAnimation,
                                 qreal penAnimation) const
{
    const bool enabled = stateProperties.value("enabled", true);
    const bool visualFocus = stateProperties.value("visualFocus");
    const bool hovered = stateProperties.value("hovered") && enabled;
    const bool down = stateProperties.value("down") && enabled;
    const bool checked = stateProperties.value("checked");
    const bool flat = stateProperties.value("flat");
    const bool defaultButton = stateProperties.value("defaultButton") && stateProperties.value("isActiveWindow");
    const bool hasNeutralHighlight = stateProperties.value("hasNeutralHighlight");
    const bool roundButton = stateProperties.value("roundButton");
    const bool glass = isGlass();
    const bool dark = isDark(palette);
    const QColor inkColor = ink(palette);
    const QColor highlight = palette.color(enabled ? QPalette::Active : QPalette::Disabled, QPalette::Highlight);

    // how much ink the sheet holds
    qreal fill = flat ? 0.0 : _aurora ? auroraVeil(palette, true) : (glass ? (dark ? 0.09 : 0.06) : (dark ? 0.08 : 0.07));
    if (checked) {
        fill = glass ? 0.18 : 0.16;
    }
    // hover and press fade in and out with the animation engine
    if (penAnimation != AnimationData::OpacityInvalid) {
        fill += 0.06 * penAnimation;
    } else if (hovered) {
        fill += 0.06;
    }
    if (bgAnimation != AnimationData::OpacityInvalid) {
        fill += 0.08 * bgAnimation;
    } else if (down) {
        fill += 0.08;
    }
    QColor bg = alphaColor(inkColor, qMin(fill, 0.4));
    if (_aurora && checked) {
        // Aurora: what is on wears the accent
        bg = auroraAccent(palette, (dark ? 0.34 : 0.30) + (fill - 0.18));
    }
    if (_aurora && defaultButton && !checked) {
        // Aurora: the primary action is a sheet of the accent
        bg = auroraAccent(palette, qMin(1.0, (dark ? 0.62 : 0.50) + (fill - auroraVeil(palette, true)) * 1.5));
    } else if (defaultButton && !checked) {
        // the primary action: tinted with the highlight, still readable with the normal button text
        bg = glass ? KColorUtils::mix(alphaColor(inkColor, 0.16), highlight, 0.25) : (hovered ? highlight.lighter(112) : highlight);
    } else if (hasNeutralHighlight) {
        bg = alphaColor(neutralText(palette), 0.25);
    }

    QColor edge(0, 0, 0, 0);
    const bool rims = hasRims(); // Aurora: the fills of Glass, without its rims
    if (rims && (!flat || checked || hovered || down)) {
        edge = alphaColor(inkColor, checked ? 0.26 : (hovered ? 0.22 : 0.14));
    }
    if (visualFocus) {
        // the glow below says it
    } else if (penAnimation != AnimationData::OpacityInvalid && rims) {
        edge = KColorUtils::mix(edge, alphaColor(inkColor, 0.22), penAnimation);
    }

    clearUnder(painter, rect); // Breeze NG Glass: a clean sheet first (even when nothing follows: the old veil goes)
    if (bg.alpha() == 0 && edge.alpha() == 0) {
        if (visualFocus) {
            renderNgGlow(painter, rect, focusColor(palette), 1.0, roundButton ? std::max(rect.width(), rect.height()) / 2 : frameRadius(PenWidth::Frame));
        }
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    const qreal radius = roundButton ? std::max(rect.width(), rect.height()) / 2 : frameRadius(PenWidth::Frame);
    QRectF frameRect = strokedRect(rect);
    QPainterPath path;
    // joined to an open menu (see Style::eventFilterNgMenu): one shape with it
    QString joined;
    QColor joinedBg;
    QColor joinedEdge;
    QRectF widgetRect;
    if (painter->device() && painter->device()->devType() == QInternal::Widget) {
        const QWidget *device = static_cast<const QWidget *>(painter->device());
        joined = device->property("_breeze_ng_joined").toString();
        joinedBg = device->property("_breeze_ng_joined_bg").value<QColor>();
        joinedEdge = device->property("_breeze_ng_joined_edge").value<QColor>();
        widgetRect = device->rect();
    }
    if (joined == QLatin1String("bottom") || joined == QLatin1String("top")) {
        // the menu's own sheet and outline. The shape covers the whole widget, whatever sub-rect the style was
        // handed, because the menu sits against the widget's edge; it runs past the joined side, which the widget clips.
        frameRect = strokedRect(widgetRect.isValid() ? widgetRect : QRectF(rect));
        if (joined == QLatin1String("bottom")) {
            frameRect.adjust(0, 0, 0, radius + 3);
            path = roundedPath(frameRect, CornersTop, radius);
        } else {
            frameRect.adjust(0, -radius - 3, 0, 0);
            path = roundedPath(frameRect, CornersBottom, radius);
        }
        bg = joinedBg.isValid() ? joinedBg : frameBackgroundColor(palette);
        if (!_aurora) {
            bg.setAlpha(255);
        }
        // the outline is the menu's translucent one, composited over the fill (never Source: it would turn black)
        edge = joinedEdge.isValid() ? KColorUtils::overlayColors(bg, joinedEdge) : QColor(0, 0, 0, 0);
    } else {
        path.addRoundedRect(frameRect, radius, radius);
    }
    if (rims && !flat) {
        // a sheet of glass: lighter towards the top
        QLinearGradient sheet(frameRect.topLeft(), frameRect.bottomLeft());
        sheet.setColorAt(0.0, alphaColor(inkColor, qMin(bg.alphaF() + 0.06, 0.5)));
        sheet.setColorAt(1.0, bg);
        painter->setBrush(sheet);
    } else {
        painter->setBrush(bg);
    }
    painter->setPen(edge.alpha() > 0 ? QPen(edge, PenWidth::Frame) : QPen(Qt::NoPen));
    if (_aurora && (joined == QLatin1String("bottom") || joined == QLatin1String("top"))) {
        joinedFill(painter, path, bg, painter->pen()); // the menu's see-through sheet, from nothing
    } else {
        painter->drawPath(path);
    }
    if (rims && !flat && !down) {
        // the bright hairline on the top edge, as on the panels
        painter->setClipPath(path);
        painter->setPen(QPen(alphaColor(inkColor, dark ? 0.16 : 0.5), 1));
        painter->drawLine(QPointF(frameRect.left() + radius, frameRect.top() + 1.0), QPointF(frameRect.right() - radius, frameRect.top() + 1.0));
    }
    if (_aurora && hovered && painter->device() && painter->device()->devType() == QInternal::Widget) {
        // Aurora: a light that follows the pointer inside the button (the style repaints the button as the pointer moves)
        const QWidget *device = static_cast<const QWidget *>(painter->device());
        const QPointF pointer = device->mapFromGlobal(QCursor::pos());
        if (rect.adjusted(-30, -30, 30, 30).contains(pointer)) {
            painter->setClipPath(path);
            painter->setPen(Qt::NoPen);
            QRadialGradient spot(pointer, qMax<qreal>(56.0, rect.height() * 2.0));
            spot.setColorAt(0.0, alphaColor(inkColor, dark ? 0.24 : 0.16));
            spot.setColorAt(0.5, alphaColor(inkColor, dark ? 0.08 : 0.05));
            spot.setColorAt(1.0, alphaColor(inkColor, 0.0));
            painter->fillRect(rect, spot);
        }
    }
    painter->restore();
    if (visualFocus) {
        renderNgGlow(painter, rect, focusColor(palette), 1.0, radius);
    }
}

//______________________________________________________________________________
void Helper::renderToolBoxFrame(QPainter *painter, const QRectF &rect, int tabWidth, const QColor &outline) const
{
    if (!outline.isValid()) {
        return;
    }

    // round radius
    const qreal radius(frameRadius(PenWidth::Frame));
    const QSizeF cornerSize(2 * radius, 2 * radius);

    // if rect - tabwidth is even, need to increase tabWidth by 1 unit
    // for anti aliasing
    if (!((rect.toRect().width() - tabWidth) % 2)) {
        ++tabWidth;
    }

    // adjust rect for antialiasing
    QRectF baseRect(strokedRect(rect));

    // create path
    QPainterPath path;
    path.moveTo(0, baseRect.height() - 1);
    path.lineTo((baseRect.width() - tabWidth) / 2 - radius, baseRect.height() - 1);
    path.arcTo(QRectF(QPointF((baseRect.width() - tabWidth) / 2 - 2 * radius, baseRect.height() - 1 - 2 * radius), cornerSize), 270, 90);
    path.lineTo((baseRect.width() - tabWidth) / 2, radius);
    path.arcTo(QRectF(QPointF((baseRect.width() - tabWidth) / 2, 0), cornerSize), 180, -90);
    path.lineTo((baseRect.width() + tabWidth) / 2 - 1 - radius, 0);
    path.arcTo(QRectF(QPointF((baseRect.width() + tabWidth) / 2 - 1 - 2 * radius, 0), cornerSize), 90, -90);
    path.lineTo((baseRect.width() + tabWidth) / 2 - 1, baseRect.height() - 1 - radius);
    path.arcTo(QRectF(QPointF((baseRect.width() + tabWidth) / 2 - 1, baseRect.height() - 1 - 2 * radius), cornerSize), 180, 90);
    path.lineTo(baseRect.width() - 1, baseRect.height() - 1);

    // render
    painter->setRenderHints(QPainter::Antialiasing);
    painter->setBrush(Qt::NoBrush);
    painter->setPen(outline);
    painter->translate(baseRect.topLeft());
    painter->drawPath(path);
}

//______________________________________________________________________________
void Helper::renderTabWidgetFrame(QPainter *painter, const QRectF &rect, const QColor &color, const QColor &outline, Corners corners) const
{
    painter->setRenderHint(QPainter::Antialiasing);

    QRectF frameRect(rect.adjusted(1, 1, -1, -1));
    qreal radius(frameRadius(PenWidth::NoPen));

    // set pen
    if (outline.isValid()) {
        painter->setPen(outline);
        frameRect = strokedRect(frameRect);
        radius = frameRadiusForNewPenWidth(radius, PenWidth::Frame);

    } else {
        painter->setPen(Qt::NoPen);
    }

    // set brush
    if (color.isValid()) {
        painter->setBrush(color);
    } else {
        painter->setBrush(Qt::NoBrush);
    }

    // render
    QPainterPath path(roundedPath(frameRect, corners, radius));
    painter->drawPath(path);
}

//______________________________________________________________________________
void Helper::renderSeparator(QPainter *painter, const QRectF &rect, const QColor &color, bool vertical) const
{
    // Breeze NG: no separator lines, anywhere
    Q_UNUSED(painter)
    Q_UNUSED(rect)
    Q_UNUSED(color)
    Q_UNUSED(vertical)
    return;

    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setBrush(Qt::NoBrush);
    painter->setPen(color);

    if (vertical) {
        painter->translate(rect.width() / 2, 0);
        painter->drawLine(rect.topLeft(), rect.bottomLeft());

    } else {
        painter->translate(0, rect.height() / 2);
        painter->drawLine(rect.topLeft(), rect.topRight());
    }
}

//______________________________________________________________________________
void Helper::renderCheckBoxBackground(QPainter *painter,
                                      const QRectF &rect,
                                      const QPalette &palette,
                                      CheckBoxState state,
                                      bool neutalHighlight,
                                      bool sunken,
                                      qreal animation) const
{
    // Breeze NG: Soft fills the box with a tone (the accent when checked); Glass with a sheet of ink and an edge
    {
        const bool glass = hasRims(); // Aurora: as Soft
        const QColor inkColor = ink(palette);
        const qreal on = (state == CheckOn || state == CheckPartial) ? 1.0 : (state == CheckAnimated ? animation : 0.0);
        const QColor off = alphaColor(inkColor, glass ? 0.10 : 0.18); // an empty box must still read as a box
        const QColor onColor = glass ? alphaColor(inkColor, 0.28) : palette.color(QPalette::Highlight);
        QColor fill = KColorUtils::mix(off, onColor, on);
        QColor edge = glass ? alphaColor(inkColor, 0.22 + 0.18 * on) : QColor(0, 0, 0, 0);
        if (neutalHighlight) {
            edge = neutralText(palette);
        }
        if (sunken) {
            fill = fill.darker(110);
        }
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        QRectF frameRect(rect.adjusted(2, 2, -2, -2));
        painter->setBrush(fill);
        if (edge.alpha() > 0) {
            painter->setPen(QPen(edge, PenWidth::Frame));
            frameRect = strokedRect(frameRect);
        } else {
            painter->setPen(Qt::NoPen);
        }
        painter->drawRoundedRect(frameRect, Metrics::CheckBox_Radius, Metrics::CheckBox_Radius);
        painter->restore();
        return;
    }

    // setup painter
    painter->setRenderHint(QPainter::Antialiasing, true);

    // copy rect
    QRectF frameRect(rect);
    frameRect.adjust(2, 2, -2, -2);
    frameRect = strokedRect(frameRect);

    auto transparent = neutalHighlight ? neutralText(palette) : palette.highlight().color();
    transparent.setAlphaF(highlightBackgroundAlpha);

    QBrush penBrush;
    if (neutalHighlight) {
        penBrush = neutralText(palette);
    } else if (state == CheckOn || state == CheckPartial) {
        penBrush = palette.highlight().color();
    } else {
        penBrush = separatorColor(palette);
    }
    painter->setPen(QPen(penBrush, PenWidth::Frame));

    const auto radius = Metrics::CheckBox_Radius;

    painter->setBrush(palette.button().color().darker(sunken ? radioCheckSunkenDarkeningFactor : 100));
    painter->drawRoundedRect(frameRect, radius, radius);

    if (state == CheckOff) {
        return;
    }

    painter->setBrush(transparent);
    if (state == CheckAnimated) {
        painter->setOpacity(animation);
    }
    painter->drawRoundedRect(frameRect, radius, radius);
}

//______________________________________________________________________________
void Helper::renderCheckBox(QPainter *painter,
                            const QRectF &rect,
                            const QPalette &palette,
                            bool mouseOver,
                            CheckBoxState state,
                            CheckBoxState target,
                            bool neutalHighlight,
                            bool sunken,
                            qreal animation,
                            qreal hoverAnimation) const
{
    Q_UNUSED(sunken)

    // setup painter
    painter->setRenderHint(QPainter::Antialiasing, true);

    // copy rect and radius
    QRectF frameRect(rect);
    frameRect.adjust(2, 2, -2, -2);

    if (mouseOver) {
        painter->save();

        if (hoverAnimation != AnimationData::OpacityInvalid) {
            painter->setOpacity(hoverAnimation);
        }

        painter->setPen(QPen(neutalHighlight ? neutralText(palette).lighter() : focusColor(palette), PenWidth::Frame));
        painter->setBrush(Qt::NoBrush);

        painter->drawRoundedRect(frameRect.adjusted(0.5, 0.5, -0.5, -0.5), Metrics::CheckBox_Radius, Metrics::CheckBox_Radius);

        painter->restore();
    }

    // check
    auto leftPoint = frameRect.center();
    leftPoint.setX(frameRect.left() + 4);

    auto bottomPoint = frameRect.center();
    bottomPoint.setX(bottomPoint.x() - 1);
    bottomPoint.setY(frameRect.bottom() - 5);

    auto rightPoint = frameRect.center();
    rightPoint.setX(rightPoint.x() + 4.5);
    rightPoint.setY(frameRect.top() + 5.5);

    QPainterPath path;
    path.moveTo(leftPoint);
    path.lineTo(bottomPoint);
    path.lineTo(rightPoint);

    // dots
    auto centerDot = QRectF(frameRect.center(), QSize(2, 2));
    centerDot.adjust(-1, -1, -1, -1);
    auto leftDot = centerDot.adjusted(-4, 0, -4, 0);
    auto rightDot = centerDot.adjusted(4, 0, 4, 0);

    painter->setPen(Qt::transparent);
    painter->setBrush(Qt::transparent);

    auto checkPen = QPen(palette.text(), PenWidth::Frame * 2);
    checkPen.setJoinStyle(Qt::MiterJoin);

    switch (state) {
    case CheckOff:
        break;
    case CheckOn:
        painter->setPen(checkPen);
        painter->drawPath(path);
        break;
    case CheckPartial:
        painter->setBrush(palette.text());
        painter->drawRect(leftDot);
        painter->drawRect(centerDot);
        painter->drawRect(rightDot);
        break;
    case CheckAnimated:
        checkPen.setDashPattern({path.length() * animation, path.length()});

        switch (target) {
        case CheckOff:
            break;
        case CheckOn:
            painter->setPen(checkPen);
            painter->drawPath(path);
            break;
        case CheckPartial:
            if (animation >= 3.0 / 3.0) {
                painter->drawRect(rightDot);
            }
            if (animation >= 2.0 / 3.0) {
                painter->drawRect(centerDot);
            }
            if (animation >= 1.0 / 3.0) {
                painter->drawRect(leftDot);
            }
            break;
        case CheckAnimated:
            break;
        }
        break;
    }
}

//______________________________________________________________________________
void Helper::renderRadioButtonBackground(QPainter *painter,
                                         const QRectF &rect,
                                         const QPalette &palette,
                                         RadioButtonState state,
                                         bool neutalHighlight,
                                         bool sunken,
                                         qreal animation) const
{
    // Breeze NG: as the check box, round
    {
        const bool glass = hasRims(); // Aurora: as Soft
        const QColor inkColor = ink(palette);
        const qreal on = state == RadioOn ? 1.0 : (state == RadioAnimated ? animation : 0.0);
        const QColor off = alphaColor(inkColor, glass ? 0.10 : 0.18); // an empty box must still read as a box
        const QColor onColor = glass ? alphaColor(inkColor, 0.28) : palette.color(QPalette::Highlight);
        QColor fill = KColorUtils::mix(off, onColor, on);
        QColor edge = glass ? alphaColor(inkColor, 0.22 + 0.18 * on) : QColor(0, 0, 0, 0);
        if (neutalHighlight) {
            edge = neutralText(palette);
        }
        if (sunken) {
            fill = fill.darker(110);
        }
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        QRectF frameRect(rect.adjusted(2, 2, -2, -2));
        painter->setBrush(fill);
        if (edge.alpha() > 0) {
            painter->setPen(QPen(edge, PenWidth::Frame));
            frameRect = strokedRect(frameRect);
        } else {
            painter->setPen(Qt::NoPen);
        }
        painter->drawEllipse(frameRect);
        painter->restore();
        return;
    }

    // setup painter
    painter->setRenderHint(QPainter::Antialiasing, true);

    // copy rect
    QRectF frameRect(rect);
    frameRect.adjust(2, 2, -2, -2);
    frameRect.adjust(0.5, 0.5, -0.5, -0.5);

    auto transparent = neutalHighlight ? neutralText(palette) : palette.highlight().color();
    transparent.setAlphaF(highlightBackgroundAlpha);

    QBrush penBrush;
    if (neutalHighlight) {
        penBrush = neutralText(palette);
    } else if (state == RadioOn) {
        penBrush = palette.highlight().color();
    } else {
        penBrush = separatorColor(palette);
    }
    painter->setPen(QPen(penBrush, PenWidth::Frame));

    painter->setBrush(palette.button().color().darker(sunken ? radioCheckSunkenDarkeningFactor : 100));
    painter->drawEllipse(frameRect);

    if (state == RadioOff) {
        return;
    }

    painter->setBrush(transparent);
    if (state == RadioAnimated) {
        painter->setOpacity(animation);
    }
    painter->drawEllipse(frameRect);
}

//______________________________________________________________________________
void Helper::renderRadioButton(QPainter *painter,
                               const QRectF &rect,
                               const QPalette &palette,
                               bool mouseOver,
                               RadioButtonState state,
                               bool neutralHighlight,
                               bool sunken,
                               qreal animation,
                               qreal animationHover) const
{
    Q_UNUSED(sunken)

    // copy rect
    QRectF frameRect(rect);
    frameRect.adjust(1, 1, -1, -1);

    if (mouseOver) {
        painter->save();

        if (animationHover != AnimationData::OpacityInvalid) {
            painter->setOpacity(animationHover);
        }

        painter->setPen(QPen(neutralHighlight ? neutralText(palette).lighter() : focusColor(palette), PenWidth::Frame));
        painter->setBrush(Qt::NoBrush);

        const QRectF contentRect(frameRect.adjusted(1, 1, -1, -1).adjusted(0.5, 0.5, -0.5, -0.5));
        painter->drawEllipse(contentRect);

        painter->restore();
    }

    painter->save();
    painter->setBrush(palette.text());
    painter->setPen(Qt::NoPen);
    // Breeze NG: antialiased like the ring around it. Without it Qt rounded the small disc unevenly: one pixel
    // narrower in height than in width and half a pixel below the ring's centre.
    painter->setRenderHint(QPainter::Antialiasing, true);

    const int radius = (std::min(frameRect.width(), frameRect.height()) - 12) / 2;
    const QPointF center = frameRect.center();

    // mark
    switch (state) {
    case RadioOn:
        painter->drawEllipse(center, radius, radius);
        break;
    case RadioAnimated: {
        const qreal animationRadius = radius * animation;
        painter->drawEllipse(center, animationRadius, animationRadius);
        break;
    }
    default:
        break;
    }
    painter->restore();
}

//______________________________________________________________________________
void Helper::renderSliderGroove(QPainter *painter, const QRectF &rect, const QColor &fg, const QColor &bg) const
{
    // setup painter
    painter->setRenderHint(QPainter::Antialiasing, true);

    QRectF baseRect(rect);
    baseRect.adjust(0.5, 0.5, -0.5, -0.5);
    const qreal radius(0.5 * Metrics::Slider_GrooveThickness);

    // content
    // content
    if (fg.isValid()) {
        painter->setPen(QPen(transparentize(fg, frameIntensityBias()), PenWidth::Frame));
        painter->setBrush(KColorUtils::overlayColors(bg, alphaColor(fg, 0.7)));
        painter->drawRoundedRect(baseRect, radius, radius);
    }
}

//______________________________________________________________________________
void Helper::renderDialGroove(QPainter *painter, const QRectF &rect, const QColor &fg, const QColor &bg, qreal first, qreal last) const
{
    // setup painter
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRectF baseRect(rect);

    // content
    if (fg.isValid()) {
        const qreal penWidth(Metrics::Slider_GrooveThickness);
        const QRectF grooveRect(rect.adjusted(penWidth / 2, penWidth / 2, -penWidth / 2, -penWidth / 2));

        // setup angles
        const int angleStart(first * 180 * 16 / M_PI);
        const int angleSpan((last - first) * 180 * 16 / M_PI);
        const QPen bgPen(fg, penWidth, Qt::SolidLine, Qt::RoundCap);
        const QPen fgPen(transparentize(KColorUtils::overlayColors(bg, alphaColor(fg, 0.5)), frameIntensityBias()), penWidth - 2, Qt::SolidLine, Qt::RoundCap);

        // setup pen
        if (angleSpan != 0) {
            painter->setPen(bgPen);
            painter->setBrush(Qt::NoBrush);
            painter->drawArc(grooveRect, angleStart, angleSpan);
            painter->setPen(fgPen);
            painter->drawArc(grooveRect, angleStart, angleSpan);
        }
    }
}

//______________________________________________________________________________
void Helper::initSliderStyleOption(const QSlider *slider, QStyleOptionSlider *option) const
{
    option->initFrom(slider);
    option->subControls = QStyle::SC_None;
    option->activeSubControls = QStyle::SC_None;
    option->orientation = slider->orientation();
    option->maximum = slider->maximum();
    option->minimum = slider->minimum();
    option->tickPosition = slider->tickPosition();
    option->tickInterval = slider->tickInterval();
    option->upsideDown = (slider->orientation() == Qt::Horizontal) //
        ? (slider->invertedAppearance() != (option->direction == Qt::RightToLeft))
        : (!slider->invertedAppearance());
    option->direction = Qt::LeftToRight; // we use the upsideDown option instead
    option->sliderPosition = slider->sliderPosition();
    option->sliderValue = slider->value();
    option->singleStep = slider->singleStep();
    option->pageStep = slider->pageStep();
    if (slider->orientation() == Qt::Horizontal) {
        option->state |= QStyle::State_Horizontal;
    }
    // Can't fetch activeSubControls, because it's private API
}

//______________________________________________________________________________
QRectF Helper::pathForSliderHandleFocusFrame(QPainterPath &focusFramePath, const QRectF &rect, int hmargin, int vmargin) const
{
    // Mimics path and adjustments of renderSliderHandle
    QRectF frameRect(rect);
    frameRect.translate(hmargin, vmargin);
    frameRect.adjust(1, 1, -1, -1);
    frameRect = strokedRect(frameRect);
    focusFramePath.addEllipse(frameRect);
    frameRect.adjust(-hmargin, -vmargin, hmargin, vmargin);
    focusFramePath.addEllipse(frameRect);
    return frameRect;
}

//______________________________________________________________________________
void Helper::renderSliderHandle(QPainter *painter, const QRectF &rect, const QColor &color, const QColor &outline, const QColor &shadow, bool sunken) const
{
    // Breeze NG: a plain disc that contrasts with the surface, no outline
    {
        Q_UNUSED(outline)
        Q_UNUSED(shadow)
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const bool darkSurface = color.lightnessF() < 0.5;
        QColor disc = darkSurface ? KColorUtils::mix(color, QColor(255, 255, 255), 0.82) : KColorUtils::mix(color, QColor(0, 0, 0), 0.55);
        if (sunken) {
            disc = darkSurface ? disc.darker(115) : disc.lighter(115);
        }
        painter->setPen(Qt::NoPen);
        painter->setBrush(disc);
        painter->drawEllipse(rect.adjusted(2, 2, -2, -2));
        painter->restore();
        return;
    }
    // setup painter
    painter->setRenderHint(QPainter::Antialiasing, true);

    // copy rect
    QRectF frameRect(rect);
    frameRect.adjust(1, 1, -1, -1);

    // shadow
    if (!sunken) {
        renderEllipseShadow(painter, frameRect, shadow);
    }

    // set pen
    if (outline.isValid()) {
        painter->setPen(QPen(outline, PenWidth::Frame));
        frameRect = strokedRect(frameRect);

    } else {
        painter->setPen(Qt::NoPen);
    }

    // set brush
    if (color.isValid()) {
        painter->setBrush(color);
    } else {
        painter->setBrush(Qt::NoBrush);
    }

    // render
    painter->drawEllipse(frameRect);
}

//______________________________________________________________________________
void Helper::renderProgressBarGroove(QPainter *painter, const QRectF &rect, const QColor &fg, const QColor &bg) const
{
    // setup painter
    painter->setRenderHint(QPainter::Antialiasing, true);

    QRectF baseRect(rect);
    baseRect.adjust(0.5, 0.5, -0.5, -0.5);
    const qreal radius(0.5 * Metrics::ProgressBar_Thickness);

    // content
    if (fg.isValid()) {
        painter->setPen(QPen(transparentize(fg, frameIntensityBias()), PenWidth::Frame));
        painter->setBrush(KColorUtils::overlayColors(bg, alphaColor(fg, 0.7)));
        painter->drawRoundedRect(baseRect, radius, radius);
    }
}

//______________________________________________________________________________
void Helper::renderProgressBarBusyContents(QPainter *painter,
                                           const QRectF &rect,
                                           const QColor &first,
                                           const QColor &second,
                                           bool horizontal,
                                           bool reverse,
                                           int progress) const
{
    // setup painter
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRectF baseRect(rect);
    const qreal radius(0.5 * Metrics::ProgressBar_Thickness);

    // setup brush
    QPixmap pixmap(horizontal ? 2 * Metrics::ProgressBar_BusyIndicatorSize : 1, horizontal ? 1 : 2 * Metrics::ProgressBar_BusyIndicatorSize);
    pixmap.fill(second);
    if (horizontal) {
        QPainter painter(&pixmap);
        painter.setBrush(first);
        painter.setPen(Qt::NoPen);

        progress %= 2 * Metrics::ProgressBar_BusyIndicatorSize;
        if (reverse) {
            progress = 2 * Metrics::ProgressBar_BusyIndicatorSize - progress - 1;
        }
        painter.drawRect(QRect(0, 0, Metrics::ProgressBar_BusyIndicatorSize, 1).translated(progress, 0));

        if (progress > Metrics::ProgressBar_BusyIndicatorSize) {
            painter.drawRect(QRect(0, 0, Metrics::ProgressBar_BusyIndicatorSize, 1).translated(progress - 2 * Metrics::ProgressBar_BusyIndicatorSize, 0));
        }

    } else {
        QPainter painter(&pixmap);
        painter.setBrush(first);
        painter.setPen(Qt::NoPen);

        progress %= 2 * Metrics::ProgressBar_BusyIndicatorSize;
        progress = 2 * Metrics::ProgressBar_BusyIndicatorSize - progress - 1;
        painter.drawRect(QRect(0, 0, 1, Metrics::ProgressBar_BusyIndicatorSize).translated(0, progress));

        if (progress > Metrics::ProgressBar_BusyIndicatorSize) {
            painter.drawRect(QRect(0, 0, 1, Metrics::ProgressBar_BusyIndicatorSize).translated(0, progress - 2 * Metrics::ProgressBar_BusyIndicatorSize));
        }
    }

    painter->setPen(Qt::NoPen);
    painter->setBrush(pixmap);
    painter->drawRoundedRect(baseRect, radius, radius);
}

//______________________________________________________________________________
void Helper::renderScrollBarHandle(QPainter *painter, const QRectF &rect, const QColor &fg, const QColor &bg) const
{
    // setup painter
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRectF baseRect(rect);
    const qreal radius(0.5 * std::min({baseRect.width(), baseRect.height(), (qreal)Metrics::ScrollBar_SliderWidth}));

    Q_UNUSED(bg)
    painter->setPen(Qt::NoPen); // Breeze NG: no outline on the handle
    painter->setBrush(alphaColor(fg, 0.45));
    painter->drawRoundedRect(baseRect, radius, radius);
}

//______________________________________________________________________________
void Helper::renderScrollBarBorder(QPainter *painter, const QRectF &rect, const QColor &color) const
{
    // Breeze NG: no line between a view and its scroll bar
    Q_UNUSED(painter)
    Q_UNUSED(rect)
    Q_UNUSED(color)
    return;

    // content
    if (color.isValid()) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
        painter->drawRect(rect);
    }
}

void Helper::renderStaticTabBarTab(QPainter *painter,
                                   const QRectF &rect,
                                   const QPalette &palette,
                                   const QHash<QByteArray, bool> &stateProperties,
                                   Corners corners,
                                   qreal animation) const
{
    bool enabled = stateProperties.value("enabled", true);
    bool hovered = stateProperties.value("hovered");
    bool selected = stateProperties.value("selected");
    bool documentMode = stateProperties.value("documentMode");
    bool north = stateProperties.value("north");
    bool south = stateProperties.value("south");
    bool west = stateProperties.value("west");
    bool east = stateProperties.value("east");
    bool isFirst = stateProperties.value("isFirst");
    bool isLast = stateProperties.value("isLast");
    bool isRightOfSelected = stateProperties.value("isRightOfSelected");
    bool animated = animation != AnimationData::OpacityInvalid;
    bool isQtQuickControl = stateProperties.value("isQtQuickControl");
    bool hasAlteredBackground = stateProperties.value("hasAlteredBackground");
    const auto baseColor = palette.color(QPalette::Base).darker(102);
    const auto windowColor = palette.color(QPalette::Window);

    // setup painter
    painter->setRenderHint(QPainter::Antialiasing, true);
    QRectF frameRect = rect;
    QColor bgBrush;

    if (isGlass()) {
        // Breeze NG Glass: every tab is a pill; the current one is a lit sheet of glass, a hovered one a faint veil
        Q_UNUSED(documentMode)
        Q_UNUSED(isQtQuickControl)
        Q_UNUSED(hasAlteredBackground)
        const QColor inkColor = ink(palette);
        qreal alpha = selected ? 0.16 : 0.0;
        if (animated) {
            alpha = std::max(alpha, 0.08 * animation);
        } else if (enabled && hovered && !selected) {
            alpha = 0.08;
        }
        clearUnder(painter, frameRect);
        if (alpha <= 0.0) {
            return;
        }
        const QRectF pill = frameRect.adjusted(2, 3, -2, -3);
        const qreal radius = pill.height() / 2.0;
        painter->setBrush(alphaColor(inkColor, alpha));
        painter->setPen(selected && hasRims() ? QPen(alphaColor(inkColor, 0.22), PenWidth::Frame) : QPen(Qt::NoPen));
        painter->drawRoundedRect(pill, radius, radius);
        if (selected && hasRims()) {
            painter->setPen(QPen(alphaColor(inkColor, isDark(palette) ? 0.18 : 0.5), 1));
            painter->drawLine(QPointF(pill.left() + radius, pill.top() + 1.5), QPointF(pill.right() - radius, pill.top() + 1.5));
        }
        return;
    }

    // Breeze NG Soft: the current tab is a tongue of the page lifted from the bar, the others are labels; no lines
    Q_UNUSED(baseColor)
    Q_UNUSED(isFirst)
    Q_UNUSED(isLast)
    Q_UNUSED(isRightOfSelected)
    Q_UNUSED(documentMode)
    Q_UNUSED(isQtQuickControl)
    Q_UNUSED(hasAlteredBackground)
    const Corners tongueCorners = north ? Corners(CornersTop) : south ? Corners(CornersBottom) : Corners(CornersTop | CornersBottom);
    QRectF tongue = frameRect;
    if (north) {
        tongue.adjust(3, 3, -3, 2);
    } else if (south) {
        tongue.adjust(3, -2, -3, -3);
    } else {
        tongue.adjust(3, 3, -3, -3);
    }
    painter->setPen(Qt::NoPen);
    if (selected) {
        painter->setBrush(tabPanelColor(palette));
        painter->drawPath(roundedPath(tongue, tongueCorners, frameRadius()));
    } else {
        qreal veil = 0.0;
        if (animated) {
            veil = 0.07 * animation;
        } else if (enabled && hovered) {
            veil = 0.07;
        }
        if (veil > 0.0) {
            painter->setBrush(alphaColor(ink(palette), veil));
            painter->drawPath(roundedPath(tongue, tongueCorners, frameRadius()));
        }
    }
}

//______________________________________________________________________________
void Helper::renderTabBarTab(QPainter *painter,
                             const QRectF &rect,
                             const QPalette &palette,
                             const QHash<QByteArray, bool> &stateProperties,
                             Corners corners,
                             qreal animation) const
{
    bool enabled = stateProperties.value("enabled", true);
    bool hovered = stateProperties.value("hovered");
    bool selected = stateProperties.value("selected");
    bool documentMode = stateProperties.value("documentMode");
    bool north = stateProperties.value("north");
    bool south = stateProperties.value("south");
    bool west = stateProperties.value("west");
    bool east = stateProperties.value("east");
    bool animated = animation != AnimationData::OpacityInvalid;
    bool isQtQuickControl = stateProperties.value("isQtQuickControl");
    bool hasAlteredBackground = stateProperties.value("hasAlteredBackground");
    const bool pillBar = stateProperties.value("pillBar");

    // setup painter
    painter->setRenderHint(QPainter::Antialiasing, true);
    QRectF frameRect = rect;
    QColor bgBrush;

    if (pillBar && (!selected || _aurora)) {
        // Breeze NG: a tab at rest is a faint pill, a little brighter under the pointer
        // (Aurora: the current one too, brighter: no tongue in the opaque colour of the page, the page is glass)
        qreal veil = selected ? 0.16 : _aurora ? auroraVeil(palette, true) : 0.06;
        if (animated) {
            veil += 0.06 * animation;
        } else if (enabled && hovered) {
            veil += 0.06;
        }
        QRectF pill = frameRect;
        if (north || south) {
            pill.adjust(2, 4, -2, -4);
        } else {
            pill.adjust(4, 2, -4, -2);
        }
        const qreal radius = (north || south) ? pill.height() / 2.0 : pill.width() / 2.0;
        // (no clean sheet here: the whole tab bar gets one before it paints, see Style::eventFilter. One per tab cut
        // the round ends off the neighbours of the current tab, whose rectangle reaches over them.)
        painter->setPen(Qt::NoPen);
        // Aurora: the current tab wears the accent
        painter->setBrush(selected && _aurora ? auroraAccent(palette, veil + 0.16) : alphaColor(ink(palette), veil));
        painter->drawRoundedRect(pill, radius, radius);
        return;
    }

    if (selected) {
        // overlap border
        // This covers just enough of the border, so that both the border and its
        // antialiasing effect is covered. On 100% scale it does nothing
        const qreal overlap = devicePixelRatio(painter) * devicePixelRatio(painter);
        frameRect.adjust(east ? -overlap : 0, south ? -overlap : 0, west ? overlap : 0, north ? overlap : 0);

        // Breeze NG Soft: the current tab is a tongue lifted a little from the bar, no outline
        Q_UNUSED(documentMode)
        Q_UNUSED(isQtQuickControl)
        Q_UNUSED(hasAlteredBackground)
        // in a document bar the tongue takes the colour of the page under it, so that the two are one surface,
        // and runs past the edge of the bar (the bar clips it) so that no row of the bar shows between them
        bgBrush = pillBar ? palette.color(QPalette::Base) : tabPanelColor(palette);
        if (pillBar) {
            frameRect.adjust(east ? -4 : 0, south ? -4 : 0, west ? 4 : 0, north ? 4 : 0);
        }
        painter->setBrush(bgBrush);
        painter->setPen(Qt::NoPen);
        QRectF highlightRect = frameRect;
        if (north || south) {
            highlightRect.setHeight(Metrics::TabBar_ActiveEffectSize);
        } else if (west || east) {
            highlightRect.setWidth(Metrics::TabBar_ActiveEffectSize);
        }
        if (south) {
            highlightRect.moveBottom(frameRect.bottom());
        } else if (east) {
            highlightRect.moveRight(frameRect.right());
        }
        QPainterPath path = roundedPath(strokedRect(frameRect), corners, frameRadius(PenWidth::Frame));
        painter->drawPath(path);
        Q_UNUSED(highlightRect) // Breeze NG Soft: no accent stripe on the current tab
    } else {
        // don't overlap border
        // Since we don't set the rectangle as strokedRect here, modify only one side of it
        // the same amount strokedRect method would, to make it snap next to the border
        const qreal overlap = PenWidth::Frame;
        frameRect.adjust(east ? overlap : 0, south ? overlap : 0, west ? -overlap : 0, north ? -overlap : 0);

        // Breeze NG Soft: a tab at rest is only its label; under the pointer a faint veil fades in
        qreal veil = 0.0;
        if (animated) {
            veil = 0.07 * animation;
        } else if (enabled && hovered && !selected) {
            veil = 0.07;
        }
        if (veil <= 0.0) {
            return;
        }
        bgBrush = alphaColor(ink(palette), veil);
        painter->setBrush(bgBrush);
        painter->setPen(Qt::NoPen);
        QPainterPath path = roundedPath(frameRect, corners, Metrics::Frame_FrameRadius);
        painter->drawPath(path);
    }
}

//______________________________________________________________________________
void Helper::renderArrow(QPainter *painter, const QRectF &rect, const QColor &color, ArrowOrientation orientation) const
{
    int size = std::min({rect.toRect().width(), rect.toRect().height(), Metrics::ArrowSize});
    // No point in trying to draw if it's too small
    if (size <= 0) {
        return;
    }

    qreal penOffset = PenWidth::Symbol / 2.0;
    qreal center = size / 2.0;
    qreal maxExtent = size * 0.75;
    qreal minExtent = size / 4.0;
    qreal sizeOffset = 0;
    int remainder = size % 4;
    if (remainder == 2) {
        sizeOffset = 0.5;
    } else if (remainder == 1) {
        sizeOffset = -0.25;
    } else if (remainder == 3) {
        sizeOffset = 0.25;
    }

    QPolygonF arrow;
    switch (orientation) {
    case ArrowUp:
        arrow = QVector<QPointF>{
            {penOffset, maxExtent - penOffset - sizeOffset}, // left
            {center, minExtent - sizeOffset}, // mid
            {size - penOffset, maxExtent - penOffset - sizeOffset} // right
        };
        break;
    case ArrowDown:
        arrow = QVector<QPointF>{
            {penOffset, minExtent + penOffset + sizeOffset}, // left
            {center, maxExtent + sizeOffset}, // mid
            {size - penOffset, minExtent + penOffset + sizeOffset} // right
        };
        break;
    case ArrowLeft:
        arrow = QVector<QPointF>{
            {maxExtent - penOffset - sizeOffset, penOffset}, // top
            {minExtent - sizeOffset, center}, // mid
            {maxExtent - penOffset - sizeOffset, size - penOffset}, // bottom
        };
        break;
    case ArrowRight:
        arrow = QVector<QPointF>{
            {minExtent + penOffset + sizeOffset, penOffset}, // top
            {maxExtent + sizeOffset, center}, // mid
            {minExtent + penOffset + sizeOffset, size - penOffset}, // bottom
        };
        break;
    default:
        break;
    }

    arrow.translate(rect.x() + (rect.width() - size) / 2.0, rect.y() + (rect.height() - size) / 2.0);

    painter->save();
    painter->setRenderHints(QPainter::Antialiasing);
    painter->setBrush(Qt::NoBrush);
    QPen pen(color, PenWidth::Symbol);
    pen.setCapStyle(Qt::SquareCap);
    pen.setJoinStyle(Qt::MiterJoin);
    painter->setPen(pen);
    painter->drawPolyline(arrow);
    painter->restore();
}

//______________________________________________________________________________
void Helper::renderDecorationButton(QPainter *painter, const QRectF &rect, const QColor &color, ButtonType buttonType, bool inverted) const
{
    painter->save();
    painter->setViewport(rect.toRect());
    painter->setWindow(0, 0, 18, 18);
    painter->setRenderHints(QPainter::Antialiasing);

    // initialize pen
    QPen pen;
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::MiterJoin);

    if (inverted) {
        // render circle
        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
        painter->drawEllipse(QRectF(0, 0, 18, 18));

        // take out the inner part
        painter->setCompositionMode(QPainter::CompositionMode_DestinationOut);
        painter->setBrush(Qt::NoBrush);
        pen.setColor(Qt::black);

    } else {
        painter->setBrush(Qt::NoBrush);
        pen.setColor(color);
    }

    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::MiterJoin);
    pen.setWidthF(PenWidth::Symbol * qMax(1.0, 18.0 / rect.width()));
    painter->setPen(pen);

    switch (buttonType) {
    case ButtonClose: {
        painter->drawLine(QPointF(5, 5), QPointF(13, 13));
        painter->drawLine(13, 5, 5, 13);
        break;
    }

    case ButtonMaximize: {
        painter->drawPolyline(QVector<QPointF>{QPointF(4, 11), QPointF(9, 6), QPointF(14, 11)});
        break;
    }

    case ButtonMinimize: {
        painter->drawPolyline(QVector<QPointF>{QPointF(4, 7), QPointF(9, 12), QPointF(14, 7)});
        break;
    }

    case ButtonRestore: {
        pen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(pen);
        painter->drawPolygon(QVector<QPointF>{QPointF(4.5, 9), QPointF(9, 4.5), QPointF(13.5, 9), QPointF(9, 13.5)});
        break;
    }

    default:
        break;
    }

    painter->restore();
}

//______________________________________________________________________________
void Helper::renderRoundedRectShadow(QPainter *painter, const QRectF &rect, const QColor &color, qreal radius) const
{
    if (!color.isValid()) {
        return;
    }

    painter->setRenderHint(QPainter::Antialiasing, true);

    qreal adjustment = 0.5 * PenWidth::Shadow; // Translate for the pen
    QRectF shadowRect = rect.adjusted(adjustment, adjustment, -adjustment, adjustment);

    painter->setPen(QPen(color, PenWidth::Shadow));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(shadowRect, radius, radius);
}

//______________________________________________________________________________
void Helper::renderEllipseShadow(QPainter *painter, const QRectF &rect, const QColor &color) const
{
    if (!color.isValid()) {
        return;
    }

    painter->save();

    // Clipping does not improve performance here

    qreal adjustment = 0.5 * PenWidth::Shadow; // Adjust for the pen

    qreal radius = rect.width() / 2 - adjustment;

    /* The right side is offset by +0.5 for the visible part of the shadow.
     * The other sides are offset by +0.5 or -0.5 because of the pen.
     */
    QRectF shadowRect = rect.adjusted(adjustment, adjustment, adjustment, -adjustment);

    painter->translate(rect.center());
    painter->rotate(45);
    painter->translate(-rect.center());
    painter->setPen(color);
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(shadowRect, radius, radius);

    painter->restore();
}

void Helper::renderViewItemPosition(QPainter *painter,
                                    const QStyleOptionViewItem::ViewItemPosition &pos,
                                    const Qt::LayoutDirection direction,
                                    const QRectF &rect,
                                    const QBrush &bg,
                                    const QColor &outline) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    QFlags<Breeze::Side> sides;
    const bool reverse = direction == Qt::RightToLeft;

    switch (pos) {
    case QStyleOptionViewItem::Invalid:
        painter->setBrush(bg);
        painter->setPen(outline);
        painter->drawRect(rect);
        painter->restore();
        return;
    case QStyleOptionViewItem::Beginning:
        painter->setClipping(true);
        painter->setClipRect(rect);
        sides = SideTop | SideBottom | (reverse ? SideRight : SideLeft);
        break;
    case QStyleOptionViewItem::Middle:;
        sides = SideTop | SideBottom;
        break;
    case QStyleOptionViewItem::End:
        painter->setClipping(true);
        painter->setClipRect(rect);
        sides = SideTop | SideBottom | (reverse ? SideLeft : SideRight);
        break;
    case QStyleOptionViewItem::OnlyOne:
        sides = SideTop | SideBottom | SideLeft | SideRight;
        break;
    }
    renderFocusRect(painter, rect, bg, outline, sides);
    painter->restore();
}

//______________________________________________________________________________
bool Helper::isX11()
{
    static const bool s_isX11 = KWindowSystem::isPlatformX11();
    return s_isX11;
}

//______________________________________________________________________________
bool Helper::isWayland()
{
    static const bool s_isWayland = KWindowSystem::isPlatformWayland();
    return s_isWayland;
}

//______________________________________________________________________________
QRectF Helper::strokedRect(const QRectF &rect, const qreal penWidth) const
{
    /* With a pen stroke width of 1, the rectangle should have each of its
     * sides moved inwards by half a pixel. This allows the stroke to be
     * pixel perfect instead of blurry from sitting between pixels and
     * prevents the rectangle with a stroke from becoming larger than the
     * original size of the rectangle.
     */
    qreal adjustment = 0.5 * penWidth;
    return rect.adjusted(adjustment, adjustment, -adjustment, -adjustment);
}

QMargins Helper::itemViewItemMargins(const QStyleOptionViewItem *option) const
{
    QMargins margins(Metrics::ItemView_ItemMarginLeft, Metrics::ItemView_ItemMarginTop, Metrics::ItemView_ItemMarginRight, Metrics::ItemView_ItemMarginBottom);
    if (!option) {
        return margins;
    }

    const QFrame *frame = qobject_cast<const QFrame *>(option->widget);
    const QAbstractItemView *abstractItemView = qobject_cast<const QAbstractItemView *>(option->widget);

    const bool isFirst = option->index.row() == 0;
    const bool hasFrame = frame && frame->frameShape() == QFrame::StyledPanel;
    const bool reverse = option->direction == Qt::RightToLeft;

    if (isFirst) {
        margins.setTop(Metrics::ItemView_FirstItemTopMarginHeight);
    }

    // Breeze frame has one extra white pixel
    if (hasFrame) {
        margins -= {1, isFirst ? 1 : 0, 1, 0};
    }

    // If a treeview wants to show the highlight separate for each column, always draw rounded borders
    // if (tree && !tree->allColumnsShowFocus()) {
    if (abstractItemView && abstractItemView->selectionBehavior() != QAbstractItemView::SelectRows) {
        return margins;
    }

    if ((reverse && option->viewItemPosition == QStyleOptionViewItem::End) || (!reverse && option->viewItemPosition == QStyleOptionViewItem::Beginning)
        || option->viewItemPosition == QStyleOptionViewItem::Middle) {
        margins.setRight(0);
    }
    if ((reverse && option->viewItemPosition == QStyleOptionViewItem::Beginning) || (!reverse && option->viewItemPosition == QStyleOptionViewItem::End)
        || option->viewItemPosition == QStyleOptionViewItem::Middle) {
        margins.setLeft(0);
    }

    return margins;
}

//______________________________________________________________________________
QPainterPath Helper::roundedPath(const QRectF &rect, Corners corners, qreal radius) const
{
    QPainterPath path;

    // simple cases
    if (corners == 0) {
        path.addRect(rect);
        return path;
    }

    if (corners == AllCorners) {
        path.addRoundedRect(rect, radius, radius);
        return path;
    }

    const QSizeF cornerSize(2 * radius, 2 * radius);

    // rotate counterclockwise
    // top left corner
    if (corners & CornerTopLeft) {
        path.moveTo(rect.topLeft() + QPointF(radius, 0));
        path.arcTo(QRectF(rect.topLeft(), cornerSize), 90, 90);

    } else {
        path.moveTo(rect.topLeft());
    }

    // bottom left corner
    if (corners & CornerBottomLeft) {
        path.lineTo(rect.bottomLeft() - QPointF(0, radius));
        path.arcTo(QRectF(rect.bottomLeft() - QPointF(0, 2 * radius), cornerSize), 180, 90);

    } else {
        path.lineTo(rect.bottomLeft());
    }

    // bottom right corner
    if (corners & CornerBottomRight) {
        path.lineTo(rect.bottomRight() - QPointF(radius, 0));
        path.arcTo(QRectF(rect.bottomRight() - QPointF(2 * radius, 2 * radius), cornerSize), 270, 90);

    } else {
        path.lineTo(rect.bottomRight());
    }

    // top right corner
    if (corners & CornerTopRight) {
        path.lineTo(rect.topRight() + QPointF(0, radius));
        path.arcTo(QRectF(rect.topRight() - QPointF(2 * radius, 0), cornerSize), 0, 90);

    } else {
        path.lineTo(rect.topRight());
    }

    path.closeSubpath();
    return path;
}

//________________________________________________________________________________________________________
bool Helper::compositingActive() const
{
    if (isX11()) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        return KWindowSystem::compositingActive();
#elif __has_include(<KX11Extras>)
        return KX11Extras::compositingActive();
#endif
    }

    return true;
}

//____________________________________________________________________
bool Helper::hasAlphaChannel(const QWidget *widget) const
{
    return compositingActive() && widget && widget->testAttribute(Qt::WA_TranslucentBackground);
}

//______________________________________________________________________________________

QPixmap Helper::coloredIcon(const QIcon &icon, const QPalette &palette, const QSize &size, qreal devicePixelRatio, QIcon::Mode mode, QIcon::State state)
{
    const QPalette activePalette = KIconLoader::global()->customPalette();
    const bool changePalette = activePalette != palette;
    if (changePalette) {
        KIconLoader::global()->setCustomPalette(palette);
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPixmap pixmap = icon.pixmap(size, devicePixelRatio, mode, state);
#else
    Q_UNUSED(devicePixelRatio);
    const QPixmap pixmap = icon.pixmap(size, mode, state);
#endif
    if (changePalette) {
        if (activePalette == QPalette()) {
            KIconLoader::global()->resetPalette();
        } else {
            KIconLoader::global()->setCustomPalette(activePalette);
        }
    }
    return pixmap;
}

bool Helper::shouldDrawToolsArea(const QWidget *widget) const
{
    if (!widget) {
        return false;
    }
    static bool isAuto = false;
    static QString borderSize;
    if (!_cachedAutoValid) {
        KConfigGroup kdecorationGroup(_kwinConfig->group(QStringLiteral("org.kde.kdecoration2")));
        isAuto = kdecorationGroup.readEntry("BorderSizeAuto", true);
        borderSize = kdecorationGroup.readEntry("BorderSize", "Normal");
        _cachedAutoValid = true;
    }
    if (isAuto) {
        auto window = widget->window();
        if (qobject_cast<const QDialog *>(widget)) {
            return true;
        }
        if (window) {
            auto handle = window->windowHandle();
            if (handle) {
                auto toolbar = qobject_cast<const QToolBar *>(widget);
                if (toolbar) {
                    if (toolbar->isFloating()) {
                        return false;
                    }
                }
                return true;
            }
        } else {
            return false;
        }
    }
    if (borderSize != "None" && borderSize != "NoSides") {
        return false;
    }
    return true;
}

Qt::Edges Helper::menuSeamlessEdges(const QWidget *widget)
{
    if (widget) {
        auto edges = widget->property(PropertyNames::menuSeamlessEdges).value<Qt::Edges>();
        // Fallback to older property
        if (edges == Qt::Edges() && widget->property(PropertyNames::isTopMenu).toBool()) {
            edges = Qt::TopEdge;
        }
        return edges;
    }
    return Qt::Edges();
}

qreal Helper::devicePixelRatio(QPainter *painter) const
{
    return painter->device() ? painter->device()->devicePixelRatioF() : qApp->devicePixelRatio();
}
}
