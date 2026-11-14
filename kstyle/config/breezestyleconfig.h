/*
 * SPDX-FileCopyrightText: 2014 Hugo Pereira Da Costa <hugo.pereira@free.fr>
 * SPDX-FileCopyrightText: 2026 Morzilla
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QSlider;
class QStackedWidget;

namespace Breeze
{
class NgLookTile;
class NgOpacityPreview;
class NgEffectCard;

//* Breeze NG: the settings of the style, in pages (look, transparency, controls, windows, KWin effects, about)
class StyleConfig : public QWidget
{
    Q_OBJECT

public:
    //* constructor
    explicit StyleConfig(QWidget *);

    //* destructor
    ~StyleConfig() override = default;

Q_SIGNALS:

    //* emitted whenever one option is changed.
    void changed(bool);

public Q_SLOTS:

    //* load setup from config data
    void load();

    //* save current state
    void save();

    //* restore all default values
    void defaults();

    //* reset to saved configuration
    void reset();

protected Q_SLOTS:

    //* update modified state when option is checked/unchecked
    void updateChanged();

private:
    QWidget *pageLook();
    QWidget *pageTransparency();
    QWidget *pageControls();
    QWidget *pageWindows();
    QWidget *pageEffects();
    QWidget *pageAbout();
    void refreshEffects();
    int animationIndex() const;

    QListWidget *m_nav = nullptr;
    QStackedWidget *m_pages = nullptr;

    // look
    NgLookTile *m_soft = nullptr;
    NgLookTile *m_glass = nullptr;
    NgLookTile *m_aurora = nullptr;
    QCheckBox *m_applyDecoration = nullptr;
    QCheckBox *m_applyColours = nullptr;

    // transparency
    NgOpacityPreview *m_preview = nullptr;
    QSlider *m_windowOpacity = nullptr;
    QSlider *m_menuOpacity = nullptr;
    QLabel *m_windowOpacityValue = nullptr;
    QLabel *m_menuOpacityValue = nullptr;

    // controls and animations
    QCheckBox *m_animations = nullptr;
    QSlider *m_animationSpeed = nullptr;
    QLabel *m_animationSpeedValue = nullptr;
    QCheckBox *m_transitions = nullptr;
    QComboBox *m_mnemonics = nullptr;
    QComboBox *m_scrollTop = nullptr;
    QComboBox *m_scrollBottom = nullptr;
    QCheckBox *m_centeredTabs = nullptr;
    QCheckBox *m_tickMarks = nullptr;
    QCheckBox *m_focusGlow = nullptr;
    QCheckBox *m_menuJoin = nullptr;
    QCheckBox *m_viewFocus = nullptr;
    QCheckBox *m_toolbarSeparators = nullptr;

    // windows
    QComboBox *m_drag = nullptr;
    QComboBox *m_shadowSize = nullptr;
    QSlider *m_shadowStrength = nullptr;
    QLabel *m_shadowStrengthValue = nullptr;
    QCheckBox *m_dockFrame = nullptr;
    QCheckBox *m_sideFrame = nullptr;
    QCheckBox *m_branchLines = nullptr;
    QCheckBox *m_strongFocus = nullptr;
    QCheckBox *m_splitterProxy = nullptr;

    // KWin effects
    NgEffectCard *m_blurCard = nullptr;
    NgEffectCard *m_cornersCard = nullptr;

    bool m_loading = false;
};

}
