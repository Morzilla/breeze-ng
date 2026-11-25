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
    //* Breeze NG Aurora: the tint of an inline message (KMessageWidget), by its kind
    QColor messageTint(const QWidget *message) const;

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
    //* the thin rims and lit top edges of Glass. Aurora has none: its surfaces are soft, as in Soft, only see-through
    bool hasRims() const
    {
        return _glass && !_aurora;
    }
    //* how opaque the sheet of a Glass window is (0-1): the setting, capped lower in Aurora
    qreal windowSheetAlpha() const;
    //* Aurora: the lights behind a window. windowRect is the whole window in the painter's coordinates, so that
    //* every widget that lays its own piece of sheet continues the same lights.
    //* (`active`: the window has the focus; a window in the background dims its lights, as its title bar does)
    void renderAuroraLights(QPainter *painter, const QRectF &windowRect, const QPalette &palette, bool active = true) const;
    //* Breeze NG: white on a dark palette, black on a light one, for translucent sheets and edges
    static QColor ink(const QPalette &palette);
    static bool isDark(const QPalette &palette);
    //* Breeze NG: the sheet of a tab panel and of its current tab, a tone lifted from the window
    QColor tabPanelColor(const QPalette &palette) const
    {
        if (_aurora) {
            // Aurora: a veil, not a solid panel: the lights behind the window show through the tab pages too (a card
            // as every other: on a dark scheme darker than the window)
            return isDark(palette) ? auroraCardColor(palette) : auroraVeilColor(palette, false);
        }
        return KColorUtils::mix(palette.color(QPalette::Window), ink(palette), isDark(palette) ? 0.08 : 0.05);
    }
    //* Breeze NG Aurora: how much ink a surface holds at rest: one intensity for panels, sidebars and cards, one for
    //* buttons and fields (the same unless the user sets them apart)
    qreal auroraVeil(const QPalette &palette, bool button) const;
    //* Breeze NG Aurora: the veil as a colour. Dark schemes: white ink at auroraVeil(). Light schemes: frost, white
    //* too and much denser (black ink on light glass read as grey, a dirty panel): surfaces are lighter than the glass
    QColor auroraVeilColor(const QPalette &palette, bool button, qreal factor = 1.0, qreal more = 0.0) const;
    //* the same for an amount of ink worked out elsewhere (a button's fill with its hover and press)
    QColor auroraSheet(const QPalette &palette, qreal inkAlpha) const;
    //* Breeze NG Aurora: the ground of what is selected or switched on: a light sheet of the accent, or the full
    //* accent (setting AuroraFullSelection); \a more adds to it (hover, press)
    QColor auroraSelection(const QPalette &palette, qreal more = 0.0) const;
    //* the colour of text and marks on auroraSelection()
    QColor auroraSelectionText(const QPalette &palette) const;

    // Breeze NG AccentText: 0 off, 1 the titles of pages in the accent, 2 in a gradient, 3 the gradient on the secondary
    // titles too (the sections' titles) (0 with a grey accent)
    int accentText(const QPalette &palette) const;
    // the colour of a title: a deep accent (AccentText 2: the gradient's first stop)
    QColor accentTitleColor(const QPalette &palette) const;
    // a title's brush: the deep accent, or (AccentText 2) a gradient across the text from the accent to the second light
    QBrush accentTitleBrush(const QPalette &palette) const;
    //* Breeze NG Aurora: the accent of what is on (a slider's track, a progress bar, the primary action): the application's
    //* accent, at lightness 0.52 on a light scheme and 0.60 on a dark one
    QColor auroraFill(const QPalette &palette) const;
    //* Breeze NG Aurora: a card on the glass (a group box, a sidebar's card): the panels' veil (the setting "Sidebar,
    //* cards, lists"), a little denser, and its light hairline
    //* (on a dark scheme a card is darker than the window, set into the glass: a light veil there was a milky grey under
    //* the light keys; the density follows the same setting, 7 % giving about the 24 % of the 2026-11-24 mockup)
    QColor auroraCardColor(const QPalette &palette) const
    {
        if (isDark(palette)) {
            return QColor::fromRgbF(6 / 255.0, 8 / 255.0, 18 / 255.0, qBound(0.0, auroraVeil(palette, false) * 3.4, 0.6));
        }
        return auroraVeilColor(palette, false, 1.45);
    }
    QColor auroraCardEdge(const QPalette &palette) const { return isDark(palette) ? QColor(255, 255, 255, 18) : QColor(255, 255, 255, 190); }
    //* the card at a height \a t (0 top, 1 bottom): variant B of 2026-11-24, a vertical gradient, more light at the top
    QColor auroraCardColorAt(const QPalette &palette, qreal t) const;
    //* paint a card of the glass in \a rect (radius \a radius): its gradient and its edge, lighter along the top
    void renderAuroraCard(QPainter *painter, const QRectF &rect, const QPalette &palette, qreal radius) const;
    bool auroraFullSelection() const;

    //* Breeze NG Aurora: the card of a sidebar on the glass, as the sidebar of Dolphin NG (6 px in from \a rect, round
    //* corners of 12 px, the veil, and on light schemes the bright hairline edge of frost)
    void renderAuroraSideCard(QPainter *painter, const QRectF &rect, const QPalette &palette) const;

    //* Breeze NG Aurora: a tab of a page as a segment of a segmented control (\a rect: the tab's own rectangle)
    void renderSegmentTab(QPainter *painter, const QRectF &rect, const QPalette &palette, bool selected, bool first, bool last) const;
    void renderSegmentTrack(QPainter *painter, const QRectF &rect, const QPalette &palette) const;
    //* Breeze NG Aurora: the accent as a light (its hue, always luminous), with the given alpha
    QColor auroraAccent(const QPalette &palette, qreal alpha) const;
    //* Breeze NG Aurora: is the sheet of a menu joined to its button see-through (then the button is laid as joinedFill
    //* does: from nothing, so that it shows what the menu shows)
    bool joinedGlass() const
    {
        return _aurora;
    }
    //* Breeze NG: fill the shape of a button (or menu bar title) joined to its open menu with the menu's sheet (see-through
    //* in Aurora, laid over the window's glass).
    void joinedFill(QPainter *painter, const QPainterPath &path, const QColor &sheet, const QPen &pen = QPen(Qt::NoPen)) const;
    //* Breeze NG: a window without a frame that rounds its own corners (property "_breeze_ng_radius" on the window,
    //* as Dolphin NG sets with its tabs in the title bar): take away what was painted outside the rounded shape.
    //* `widget` is the widget being painted on (the window itself or a child).
    void cutCorners(QPainter *painter, const QWidget *widget) const;
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

    //* Breeze NG Glass: true if the compositor blurs behind windows (its blur effect is on). Without it the glass
    //* would be bare: what lies behind a window shown sharp through it. (Known only once the application runs: KWin
    //* announces its blur when the event loop has turned, so asked while the first window is polished it said no)
    bool blurAvailable() const;

    //* Breeze NG: the soft shadow around text on glass (TextShadow), for text drawn on the given device; its colour,
    //* the opposite tone of the text's. False: none (a solid window, the setting off)
    bool textShadow(const QPaintDevice *device, const QColor &text, QColor *shadow) const;

    //* no blur behind the windows (checked a moment after the first glass window): their sheet is painted solid
    void setBareGlass(bool bare)
    {
        _bareGlass = bare;
    }
    bool bareGlass() const
    {
        return _bareGlass;
    }

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
    bool _bareGlass = false; // Breeze NG: no blur behind the windows, see setBareGlass()
    bool _auroraLight = false; // Breeze NG: the lighter glass of "Breeze NG Aurora Light"
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
    mutable bool _ngInJoinFade = false; // Breeze NG: inside the two paints of a fading join (no third)
    mutable bool _ngIgnoreJoined = false; // Breeze NG: paint a button as not joined to its menu (the fade of a closing menu)

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
