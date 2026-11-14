/*
 * SPDX-FileCopyrightText: 2014 Hugo Pereira Da Costa <hugo.pereira@free.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "breeze.h"
#include "breezeanimationdata.h"
#include "breezemetrics.h"

#include <KConfigWatcher>
#include <KColorUtils>
#include <KSharedConfig>
#include <KStatefulBrush>

#include <QIcon>
#include <QPainterPath>
#include <QStyleOptionViewItem>
#include <QToolBar>
#include <QWidget>
#include <qpainter.h>

class QSlider;
class QStyleOptionSlider;

namespace Breeze
{
class PaletteChangedEventFilter;
//* breeze style helper class.
/** contains utility functions used at multiple places in both breeze style and breeze window decoration */
class Helper : public QObject
{
    Q_OBJECT

public:
    //* constructor
    explicit Helper(KSharedConfig::Ptr);

    //* destructor
    virtual ~Helper()
    {
    }

    //* load configuration
    virtual void loadConfig();

    //* pointer to shared config
    KSharedConfig::Ptr config() const;

    //* install event filter for palette change event
    void installEventFilter(QApplication *app) const;

    //* uninstall event filter
    void removeEventFilter(QApplication *app) const;

    //*@name color utilities
    //@{

    //* add alpha channel multiplier to color
    QColor alphaColor(QColor color, qreal alpha) const;

    //* mouse over color
    QColor hoverColor(const QPalette &palette) const
    {
        return _viewHoverBrush.brush(palette).color();
    }

    //* focus color
    QColor focusColor(const QPalette &palette) const
    {
        return _viewFocusBrush.brush(palette).color();
    }

    //* negative text color (used for close button)
    QColor negativeText(const QPalette &palette) const
    {
        return _viewNegativeTextBrush.brush(palette).color();
    }

    //* neutral text color
    QColor neutralText(const QPalette &palette) const
    {
        return _viewNeutralTextBrush.brush(palette).color();
    }

    //* shadow
    QColor shadowColor([[maybe_unused]] const QPalette &palette, qreal opacity = 0.125) const
    {
        return QColor::fromRgbF(0, 0, 0, opacity);
    }

    //* titlebar color
    const QColor &titleBarColor(bool active) const
    {
        return active ? _activeTitleBarColor : _inactiveTitleBarColor;
    }

    //* titlebar text color
    const QColor &titleBarTextColor(bool active) const
    {
        return active ? _activeTitleBarTextColor : _inactiveTitleBarTextColor;
    }

    qreal frameIntensityBias() const
    {
        return _contrastBias;
    }

    //* Breeze NG: the variant in use. The window decoration theme chosen in KWin ("Breeze NG Glass" / "Breeze NG
    //* Soft", what a global theme sets) decides; breezengrc [Common] Variant is the fallback.
    bool isGlass() const
    {
        return _glass;
    }
    bool isSoft() const
    {
        return !isGlass();
    }
    //* Breeze NG Aurora: Glass pushed further (more see-through, lights of the accent colour behind the window, a light
    //* that follows the pointer inside buttons). Aurora is a kind of Glass: isGlass() is true as well.
    bool isAurora() const
    {
        return _aurora;
    }
    //* how opaque the sheet of a Glass window is (0-1): the setting, capped lower in Aurora
    qreal windowSheetAlpha() const;
    //* Aurora: the lights behind a window. windowRect is the whole window in the painter's coordinates, so that
    //* every widget that lays its own piece of sheet continues the same lights.
    void renderAuroraLights(QPainter *painter, const QRectF &windowRect, const QPalette &palette) const;
    //* Breeze NG: white on a dark palette, black on a light one, for translucent sheets and edges
    static QColor ink(const QPalette &palette);
    static bool isDark(const QPalette &palette);
    //* Breeze NG: the sheet of a tab panel and of its current tab, a tone lifted from the window
    QColor tabPanelColor(const QPalette &palette) const
    {
        return KColorUtils::mix(palette.color(QPalette::Window), ink(palette), isDark(palette) ? 0.08 : 0.05);
    }
    //* Breeze NG: background of text fields
    QColor inputBackground(const QPalette &palette) const;
    //* Breeze NG: background and outline of a menu panel (`alpha`: the menu window has an alpha channel)
    QColor menuSheet(const QPalette &palette, bool alpha, QColor *outline, bool joined = false) const;
    //* Breeze NG: a neon glow just inside the edge of `rect`, `strength` 0-1 (the animation engine fades it)
    void renderNgGlow(QPainter *painter, const QRectF &rect, const QColor &color, qreal strength, qreal radius = -1) const;
    //* Breeze NG Glass: the sheet colour of a translucent window (invalid if the painter's widget is not in one)
    QColor glassSheet(const QPainter *painter) const;
    //* Breeze NG Glass: lay the sheet under `rect` before a translucent veil is painted on a widget that does not
    //* fill its own background, so that the veils of successive repaints cannot pile up
    void clearUnder(QPainter *painter, const QRectF &rect) const;
    //* Breeze NG: the frame of buttons and tool buttons of both variants
    void renderNgButtonFrame(QPainter *painter, const QRectF &rect, const QPalette &palette, const QHash<QByteArray, bool> &stateProperties, qreal bgAnimation, qreal penAnimation) const;

    //* frame outline color, using animations
    QColor frameOutlineColor(const QPalette &,
                             bool mouseOver = false,
                             bool hasFocus = false,
                             qreal opacity = AnimationData::OpacityInvalid,
                             AnimationMode = AnimationNone) const;

    //* focus outline color, using animations
    QColor focusOutlineColor(const QPalette &) const;

    //* hover outline color, using animations
    QColor hoverOutlineColor(const QPalette &) const;

    //* side panel outline color, using animations
    QColor sidePanelOutlineColor(const QPalette &, bool hasFocus = false, qreal opacity = AnimationData::OpacityInvalid, AnimationMode = AnimationNone) const;

    //* frame background color
    QColor frameBackgroundColor(const QPalette &palette) const
    {
        return frameBackgroundColor(palette, palette.currentColorGroup());
    }

    //* frame background color
    QColor frameBackgroundColor(const QPalette &, QPalette::ColorGroup) const;

    //* arrow outline color
    QColor arrowColor(const QPalette &, QPalette::ColorGroup, QPalette::ColorRole) const;

    //* arrow outline color
    QColor arrowColor(const QPalette &palette, QPalette::ColorRole role) const
    {
        return arrowColor(palette, palette.currentColorGroup(), role);
    }

    //* arrow outline color, using animations
    QColor arrowColor(const QPalette &, bool mouseOver, bool hasFocus, qreal opacity = AnimationData::OpacityInvalid, AnimationMode = AnimationNone) const;

    //* slider outline color, using animations
    QColor
    sliderOutlineColor(const QPalette &, bool mouseOver, bool hasFocus, qreal opacity = AnimationData::OpacityInvalid, AnimationMode = AnimationNone) const;

    //* scrollbar handle color, using animations
    QColor
    scrollBarHandleColor(const QPalette &, bool mouseOver, bool hasFocus, qreal opacity = AnimationData::OpacityInvalid, AnimationMode = AnimationNone) const;

    //* checkbox indicator, using animations
    QColor
    checkBoxIndicatorColor(const QPalette &, bool mouseOver, bool active, qreal opacity = AnimationData::OpacityInvalid, AnimationMode = AnimationNone) const;

    //* separator color
    QColor separatorColor(const QPalette &) const;

    //* merge active and inactive palettes based on ratio, for smooth enable state change transition
    QPalette disabledPalette(const QPalette &, qreal ratio) const;

    //@}

    //*@name rendering utilities
    //@{

    //* debug frame
    void renderDebugFrame(QPainter *, const QRectF &) const;

    //* focus rect
    void renderFocusRect(QPainter *, const QRectF &, const QBrush &, const QColor &outline = QColor(), Sides = {}) const;

    //* focus line
    void renderFocusLine(QPainter *, const QRectF &, const QColor &) const;

    //* generic frame
    void renderFrame(QPainter *, const QRectF &, const QColor &color, const QColor &outline = QColor()) const;

    //* generic frame, with separators only on the side
    void renderFrameWithSides(QPainter *, const QRectF &, const QColor &color, Qt::Edges edges, const QColor &outline = QColor()) const;

    //* side panel frame
    void renderSidePanelFrame(QPainter *, const QRectF &, const QColor &outline, Side) const;

    //* menu frame
    void renderMenuFrame(QPainter *,
                         const QRectF &,
                         const QColor &color,
                         const QColor &outline,
                         bool roundCorners = true,
                         Qt::Edges seamlessEdges = Qt::Edges()) const;

    QRegion menuFrameRegion(const QMenu *widget);

    //* button frame
    void renderButtonFrame(QPainter *painter,
                           const QRectF &rect,
                           const QPalette &palette,
                           const QHash<QByteArray, bool> &stateProperties,
                           qreal bgAnimation = AnimationData::OpacityInvalid,
                           qreal penAnimation = AnimationData::OpacityInvalid) const;

    //* toolbutton frame
    void renderToolBoxFrame(QPainter *, const QRectF &, int tabWidth, const QColor &color) const;

    //* tab widget frame
    void renderTabWidgetFrame(QPainter *, const QRectF &, const QColor &color, const QColor &outline, Corners) const;

    //* separator
    void renderSeparator(QPainter *, const QRectF &, const QColor &, bool vertical = false) const;

    //* checkbox
    void renderCheckBoxBackground(QPainter *,
                                  const QRectF &,
                                  const QPalette &palette,
                                  CheckBoxState state,
                                  bool neutalHighlight,
                                  bool sunken,
                                  qreal animation = AnimationData::OpacityInvalid) const;

    //* checkbox
    void renderCheckBox(QPainter *,
                        const QRectF &,
                        const QPalette &palette,
                        bool mouseOver,
                        CheckBoxState state,
                        CheckBoxState target,
                        bool neutalHighlight,
                        bool sunken,
                        qreal animation = AnimationData::OpacityInvalid,
                        qreal hoverAnimation = AnimationData::OpacityInvalid) const;

    //* radio button
    void renderRadioButtonBackground(QPainter *,
                                     const QRectF &,
                                     const QPalette &palette,
                                     RadioButtonState state,
                                     bool neutalHighlight,
                                     bool sunken,
                                     qreal animation = AnimationData::OpacityInvalid) const;

    //* radio button
    void renderRadioButton(QPainter *,
                           const QRectF &,
                           const QPalette &palette,
                           bool mouseOver,
                           RadioButtonState state,
                           bool neutalHighlight,
                           bool sunken,
                           qreal animation = AnimationData::OpacityInvalid,
                           qreal hoverAnimation = AnimationData::OpacityInvalid) const;

    //* slider groove
    void renderSliderGroove(QPainter *, const QRectF &, const QColor &fg, const QColor &bg) const;

    //* reimplementation of protected method
    void initSliderStyleOption(const QSlider *, QStyleOptionSlider *) const;

    //* slider focus frame
    QRectF pathForSliderHandleFocusFrame(QPainterPath &, const QRectF &, int hmargin, int vmargin) const;

    //* slider handle
    void renderSliderHandle(QPainter *, const QRectF &, const QColor &, const QColor &outline, const QColor &shadow, bool sunken) const;

    //* dial groove
    void renderDialGroove(QPainter *, const QRectF &, const QColor &fg, const QColor &bg, qreal first, qreal last) const;

    //* progress bar groove
    void renderProgressBarGroove(QPainter *, const QRectF &, const QColor &fg, const QColor &bg) const;

    //* progress bar contents (animated)
    void renderProgressBarBusyContents(QPainter *painter,
                                       const QRectF &rect,
                                       const QColor &first,
                                       const QColor &second,
                                       bool horizontal,
                                       bool reverse,
                                       int progress) const;

    //* scrollbar handle
    void renderScrollBarHandle(QPainter *, const QRectF &, const QColor &fg, const QColor &bg) const;

    //* separator between scrollbar and contents
    void renderScrollBarBorder(QPainter *, const QRectF &, const QColor &) const;

    //* tabbar tab
    void renderTabBarTab(QPainter *, const QRectF &, const QPalette &palette, const QHash<QByteArray, bool> &stateProperties, Corners corners, qreal animation)
        const;
    void renderStaticTabBarTab(QPainter *,
                               const QRectF &,
                               const QPalette &palette,
                               const QHash<QByteArray, bool> &stateProperties,
                               Corners corners,
                               qreal animation) const;
    // TODO(janet): document should be set based on whether or not we consider the
    // tab user-editable, but Qt apps often misuse or don't use documentMode property
    // so we're currently just always setting it to true for now
    qreal devicePixelRatio(QPainter *) const;

    //* generic arrow
    void renderArrow(QPainter *, const QRectF &, const QColor &, ArrowOrientation) const;

    //* generic button (for mdi decorations, tabs and dock widgets)
    void renderDecorationButton(QPainter *, const QRectF &, const QColor &, ButtonType, bool inverted) const;

    //* generic shadow for rounded rectangles
    void renderRoundedRectShadow(QPainter *, const QRectF &, const QColor &, qreal radius) const;

    //* generic shadow for ellipses
    void renderEllipseShadow(QPainter *, const QRectF &, const QColor &) const;

    void renderViewItemPosition(QPainter *painter,
                                const QStyleOptionViewItem::ViewItemPosition &pos,
                                const Qt::LayoutDirection direction,
                                const QRectF &rect,
                                const QBrush &bg,
                                const QColor &outline) const;

    //@}

    //*@name compositing utilities
    //@{

    //* true if style was compiled for and is running on X11
    static bool isX11();

    //* true if running on platform Wayland
    static bool isWayland();

    //* returns true if compositing is active
    bool compositingActive() const;

    //* returns true if a given widget supports alpha channel
    bool hasAlphaChannel(const QWidget *) const;

    //* returns true if the tools area should be drawn
    bool shouldDrawToolsArea(const QWidget *) const;

    //@}

    //* frame radius
    constexpr qreal frameRadius(const int penWidth = PenWidth::NoPen, const qreal bias = 0) const
    {
        return qMax(Metrics::Frame_FrameRadius - (0.5 * penWidth) + bias, 0.0);
    }

    //* frame radius with new pen width
    constexpr qreal frameRadiusForNewPenWidth(const qreal oldRadius, const int penWidth) const
    {
        return qMax(oldRadius - (0.5 * penWidth), 0.0);
    }

    //* return a QRectF with the appropriate size for a rectangle with a pen stroke
    QRectF strokedRect(const QRectF &rect, const qreal penWidth = PenWidth::Frame) const;

    //* return a QRectF with the appropriate size for a rectangle with a shadow around it
    QRectF shadowedRect(const QRectF &rect, const qreal shadowSize = PenWidth::Shadow) const
    {
        return rect.adjusted(shadowSize, shadowSize, -shadowSize, -shadowSize);
    }

    QMargins itemViewItemMargins(const QStyleOptionViewItem *option) const;

    QPixmap coloredIcon(const QIcon &icon,
                        const QPalette &palette,
                        const QSize &size,
                        qreal devicePixelRatio,
                        QIcon::Mode mode = QIcon::Normal,
                        QIcon::State state = QIcon::Off);

    static Qt::Edges menuSeamlessEdges(const QWidget *);

protected:
    //* return rounded path in a given rect, with only selected corners rounded, and for a given radius
    QPainterPath roundedPath(const QRectF &, Corners, qreal) const;

private:
    //* configuration
    KSharedConfig::Ptr _config;

    //* KWin configuration
    KSharedConfig::Ptr _kwinConfig;
    bool _glass = false; // Breeze NG
    bool _aurora = false; // Breeze NG
    bool glassLayers(const QPainter *painter, QColor *base, QList<QColor> *panels, const QWidget **widget, const QWidget **window) const;

    //* event filter
    PaletteChangedEventFilter *_eventFilter;

    //*@name brushes
    //@{
    KStatefulBrush _viewFocusBrush;
    KStatefulBrush _viewHoverBrush;
    KStatefulBrush _viewNegativeTextBrush;
    KStatefulBrush _viewNeutralTextBrush;
    //@}

    //*@name windeco colors
    //@{
    QColor _activeTitleBarColor;
    QColor _activeTitleBarTextColor;
    QColor _inactiveTitleBarColor;
    QColor _inactiveTitleBarTextColor;
    //@}

    mutable bool _cachedAutoValid = false;

    qreal _contrastBias;

    friend class ToolsAreaManager;
    friend class PaletteChangedEventFilter;
};

class PaletteChangedEventFilter : public QObject
{
    Q_OBJECT

public:
    explicit PaletteChangedEventFilter(Helper *);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // lifetime: helper IS the parent QObject. When it is destructed, so is the filter.
    Helper *_helper = nullptr;
};
}
