/*
 * SPDX-FileCopyrightText: 2014 Hugo Pereira Da Costa <hugo.pereira@free.fr>
 * SPDX-FileCopyrightText: 2026 Morzilla
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "breezestyleconfig.h"

#include "../breeze.h"
#include "../config-breeze.h"
#include "breezestyleconfigdata.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLinearGradient>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QToolButton>
#include <QPainterPath>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#if HAVE_QTDBUS
#include <QDBusConnection>
#include <QDBusMessage>
#endif

extern "C" {
Q_DECL_EXPORT QWidget *allocate_kstyle_config(QWidget *parent)
{
    return new Breeze::StyleConfig(parent);
}
}

namespace Breeze
{
namespace
{
// the page is written in English and Italian (the language of the session picks)
QString ngText(const char *english, const char *italian)
{
    static const bool italianSession = QLocale::system().language() == QLocale::Italian || qEnvironmentVariable("LANGUAGE").startsWith(QLatin1String("it"));
    return QString::fromUtf8(italianSession ? italian : english);
}

// how long an animation lasts, from fast to slow: the speed slider picks one
const int animationDurations[] = {40, 70, 100, 160, 250};

QColor ngAccent(const QPalette &palette)
{
    return palette.color(QPalette::Highlight);
}

// the effects of KWin the look relies on, which are not part of this project
struct EffectInfo {
    const char *id;
    const char *name;
    const char *english;
    const char *italian;
    const char *site;
};
const EffectInfo blurEffect{"better_blur_dx",
                            "Better Blur DX",
                            "Blurs the desktop behind Glass windows and menus, and behind the sidebar of Dolphin NG.",
                            "La sfocatura del desktop dietro finestre e menu in Glass, e dietro la barra laterale di Dolphin NG.",
                            "https://github.com/xarblu/kwin-effects-better-blur-dx"};
const EffectInfo cornersEffect{"kwin4_effect_shapecorners",
                               "KDE Rounded Corners",
                               "Rounds the corners of every window, with a thin outline. Without it, windows have the corners of their decoration.",
                               "Gli angoli arrotondati di tutte le finestre, con un bordo sottile. Senza, le finestre hanno gli angoli della decorazione.",
                               "https://github.com/matinlotfali/KDE-Rounded-Corners"};

bool effectInstalled(const char *id)
{
    const QString file = QStringLiteral("/kwin/effects/plugins/%1.so").arg(QLatin1String(id));
    const QStringList paths = QCoreApplication::libraryPaths() + QStringList{QStringLiteral("/usr/lib64/qt6/plugins"), QStringLiteral("/usr/lib/qt6/plugins"),
                                                                             QStringLiteral("/usr/local/lib64/qt6/plugins"), QStringLiteral("/usr/local/lib/qt6/plugins"),
                                                                             QStringLiteral("/usr/lib/x86_64-linux-gnu/qt6/plugins")};
    for (const QString &path : paths) {
        if (QFile::exists(path + file)) {
            return true;
        }
    }
    return false;
}

bool effectEnabled(const char *id)
{
    return KSharedConfig::openConfig(QStringLiteral("kwinrc"), KConfig::NoGlobals)->group(QStringLiteral("Plugins")).readEntry(QStringLiteral("%1Enabled").arg(QLatin1String(id)), false);
}

// KWin's blur strength (1-15): one for the whole desktop, whichever blur effect is in use
const char *const blurGroups[] = {"Effect-better-blur-dx", "Effect-blurplus", "Effect-blur"};
int blurStrength()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("kwinrc"), KConfig::NoGlobals);
    for (const char *group : blurGroups) {
        if (config->hasGroup(QLatin1String(group)) && config->group(QLatin1String(group)).hasKey("BlurStrength")) {
            return qBound(1, config->group(QLatin1String(group)).readEntry("BlurStrength", 15), 15);
        }
    }
    return 15; // KWin's default
}

void setBlurStrength(int strength)
{
    auto config = KSharedConfig::openConfig(QStringLiteral("kwinrc"), KConfig::NoGlobals);
    for (const char *group : blurGroups) {
        if (config->hasGroup(QLatin1String(group)) || qstrcmp(group, "Effect-blur") == 0) {
            config->group(QLatin1String(group)).writeEntry("BlurStrength", strength);
        }
    }
    config->sync();
}

void reloadKWin()
{
#if HAVE_QTDBUS
    QDBusConnection::sessionBus().send(QDBusMessage::createSignal(QStringLiteral("/KWin"), QStringLiteral("org.kde.KWin"), QStringLiteral("reloadConfig")));
#endif
}

void notifyStyleChanged()
{
#if HAVE_QTDBUS
    // KGlobalSettings: 2 is "the widget style changed", for the applications that are open
    QDBusMessage message = QDBusMessage::createSignal(QStringLiteral("/KGlobalSettings"), QStringLiteral("org.kde.KGlobalSettings"), QStringLiteral("notifyChange"));
    message << 2 << 0;
    QDBusConnection::sessionBus().send(message);
#endif
}
}

//* one of the two looks, as a tile with a small window drawn in it
// The look in use: the style follows the window decoration's theme first (a global theme sets only that), then its
// own setting; the page has to show the same one.
static bool ngCurrentAuroraLight()
{
    return KSharedConfig::openConfig(QStringLiteral("kwinrc"))->group(QStringLiteral("org.kde.kdecoration2")).readEntry("theme", QString()).endsWith(QLatin1String("Aurora Light"), Qt::CaseInsensitive);
}

static int ngCurrentLook()
{
    const QString theme = KSharedConfig::openConfig(QStringLiteral("kwinrc"))->group(QStringLiteral("org.kde.kdecoration2")).readEntry("theme", QString());
    if (theme.startsWith(QLatin1String("Breeze NG Aurora"), Qt::CaseInsensitive)) {
        return Breeze::StyleConfigData::Aurora;
    } else if (theme.compare(QLatin1String("Breeze NG Glass"), Qt::CaseInsensitive) == 0) {
        return Breeze::StyleConfigData::Glass;
    } else if (theme.compare(QLatin1String("Breeze NG Soft"), Qt::CaseInsensitive) == 0) {
        return Breeze::StyleConfigData::Soft;
    }
    return Breeze::StyleConfigData::variant();
}

class NgLookTile : public QAbstractButton
{
public:
    NgLookTile(bool glass, const QString &title, const QString &description, QWidget *parent, bool aurora = false)
        : QAbstractButton(parent)
        , m_glass(glass)
        , m_aurora(aurora)
        , m_title(title)
        , m_description(description)
    {
        setCheckable(true);
        setAutoExclusive(true);
        setCursor(Qt::PointingHandCursor);
        setMinimumSize(220, 236);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    QSize sizeHint() const override
    {
        return {280, 236};
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPalette pal = palette();
        const QColor accent = ngAccent(pal);
        const bool dark = pal.color(QPalette::Window).lightness() < 128;
        QRectF tile = QRectF(rect()).adjusted(5, 5, -5, -5);
        // the tile, ringed in the accent when chosen
        if (isChecked()) {
            p.setPen(QPen(accent, 2.5));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(tile.adjusted(-2.5, -2.5, 2.5, 2.5), 15, 15);
        }
        p.setPen(QPen(dark ? QColor(255, 255, 255, 22) : QColor(0, 0, 0, 22), 1));
        p.setBrush(dark ? QColor(255, 255, 255, underMouse() ? 16 : 9) : QColor(0, 0, 0, underMouse() ? 12 : 6));
        p.drawRoundedRect(tile, 13, 13);
        // the picture: a desktop and a small window on it
        const QRectF picture = tile.adjusted(10, 10, -10, -80);
        QPainterPath clip;
        clip.addRoundedRect(picture, 10, 10);
        p.save();
        p.setClipPath(clip);
        QLinearGradient desk(picture.topLeft(), picture.bottomRight());
        desk.setColorAt(0, QColor(45, 74, 134));
        desk.setColorAt(1, QColor(106, 58, 124));
        p.fillRect(picture, desk);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 210, 120, 120));
        p.drawEllipse(QPointF(picture.right() - picture.width() * 0.2, picture.top() + picture.height() * 0.3), picture.height() * 0.22, picture.height() * 0.22);
        const QRectF window(picture.left() + picture.width() * 0.14, picture.top() + picture.height() * 0.16, picture.width() * 0.72, picture.height() * 0.74);
        p.setBrush(m_aurora ? QColor(30, 33, 39, 96) : m_glass ? QColor(30, 33, 39, 140) : QColor(35, 38, 44));
        p.setPen(m_glass && !m_aurora ? QPen(QColor(255, 255, 255, 50), 1) : QPen(Qt::NoPen));
        p.drawRoundedRect(window, 9, 9);
        if (m_aurora) {
            // Aurora: lights of the accent behind the glass, no rims
            QPainterPath inside;
            inside.addRoundedRect(window, 9, 9);
            p.save();
            p.setClipPath(inside, Qt::IntersectClip);
            const QColor glow = QColor::fromHslF(accent.hslHueF() < 0 ? 0.58 : accent.hslHueF(), 0.8, 0.62);
            for (const auto &[centre, alpha] : {std::pair<QPointF, qreal>{window.topLeft(), 0.55}, std::pair<QPointF, qreal>{window.bottomRight(), 0.38}}) {
                QRadialGradient light(centre, window.width() * 0.6);
                QColor c = glow;
                c.setAlphaF(alpha);
                light.setColorAt(0, c);
                c.setAlphaF(0);
                light.setColorAt(1, c);
                p.fillRect(window, light);
            }
            p.restore();
        }
        const QRectF side(window.left() + 6, window.top() + 6, window.width() * 0.28, window.height() - 12);
        p.setPen(Qt::NoPen);
        p.setBrush(m_glass ? QColor(255, 255, 255, 22) : QColor(43, 47, 54));
        p.drawRoundedRect(side, 7, 7);
        const qreal x = side.right() + 10, w = window.right() - 10 - x;
        p.setBrush(QColor(255, 255, 255, 58));
        p.drawRoundedRect(QRectF(x, window.top() + 12, w * 0.7, 6), 3, 3);
        p.drawRoundedRect(QRectF(x, window.top() + 24, w * 0.5, 6), 3, 3);
        p.setBrush(accent);
        p.drawRoundedRect(QRectF(x, window.top() + 38, w * 0.46, 14), 7, 7);
        p.setBrush(QColor(255, 255, 255, 58));
        p.drawRoundedRect(QRectF(x, window.top() + 60, w * 0.6, 6), 3, 3);
        p.restore();
        // the name and what it is
        QFont bold = font();
        bold.setWeight(QFont::DemiBold);
        p.setFont(bold);
        p.setPen(pal.color(QPalette::WindowText));
        p.drawText(QRectF(tile.left() + 12, picture.bottom() + 8, tile.width() - 24, 20), Qt::AlignLeft | Qt::AlignVCenter, m_title);
        QColor dim = pal.color(QPalette::WindowText);
        dim.setAlphaF(0.65);
        p.setFont(font());
        p.setPen(dim);
        p.drawText(QRectF(tile.left() + 12, picture.bottom() + 28, tile.width() - 24, tile.bottom() - picture.bottom() - 30), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                   m_description);
    }
    void enterEvent(QEnterEvent *) override
    {
        update();
    }
    void leaveEvent(QEvent *) override
    {
        update();
    }

private:
    bool m_glass;
    bool m_aurora = false;
    QString m_title;
    QString m_description;
};

//* a window and a menu over a colourful desktop, as see-through as the sliders say
class NgOpacityPreview : public QWidget
{
public:
    explicit NgOpacityPreview(QWidget *parent)
        : QWidget(parent)
    {
        setMinimumHeight(170);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    void setOpacities(int window, int menu, bool glass)
    {
        m_window = window;
        m_menu = menu;
        m_glass = glass;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF area = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath clip;
        clip.addRoundedRect(area, 12, 12);
        p.setClipPath(clip);
        QLinearGradient desk(area.topLeft(), area.topRight());
        desk.setColorAt(0, QColor(44, 111, 176));
        desk.setColorAt(0.55, QColor(143, 63, 143));
        desk.setColorAt(1, QColor(224, 138, 74));
        p.fillRect(area, desk);
        // a few shapes, so that the transparency shows
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 70));
        p.drawEllipse(QPointF(area.width() * 0.28, area.height() * 0.45), 34, 34);
        p.drawEllipse(QPointF(area.width() * 0.66, area.height() * 0.62), 22, 22);
        // the window (opaque in Soft)
        const qreal windowAlpha = m_glass ? m_window / 100.0 : 1.0;
        const QRectF window(area.left() + area.width() * 0.06, area.top() + area.height() * 0.14, area.width() * 0.54, area.height() * 0.72);
        p.setBrush(QColor(30, 33, 39, qRound(255 * windowAlpha)));
        p.setPen(QPen(QColor(255, 255, 255, 38), 1));
        p.drawRoundedRect(window, 10, 10);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 64));
        const qreal widths[] = {0.6, 0.8, 0.45, 0.7};
        for (int i = 0; i < 4; ++i) {
            p.drawRoundedRect(QRectF(window.left() + 14, window.top() + 16 + i * 18, (window.width() - 28) * widths[i], 7), 3.5, 3.5);
        }
        // the menu
        const QStringList items{ngText("Open", "Apri"), ngText("Copy", "Copia"), ngText("Rename", "Rinomina"), ngText("Properties", "Proprietà")};
        const QFontMetricsF fm(font());
        const qreal itemHeight = fm.height() + 8;
        const QRectF menu(area.left() + area.width() * 0.52, area.top() + area.height() * 0.24, area.width() * 0.34, itemHeight * items.count() + 10);
        p.setBrush(QColor(22, 24, 29, qRound(255 * m_menu / 100.0)));
        p.setPen(QPen(QColor(255, 255, 255, 30), 1));
        p.drawRoundedRect(menu, 10, 10);
        for (int i = 0; i < items.count(); ++i) {
            const QRectF item(menu.left() + 5, menu.top() + 5 + i * itemHeight, menu.width() - 10, itemHeight);
            if (i == 1) {
                p.setPen(Qt::NoPen);
                p.setBrush(ngAccent(palette()));
                p.drawRoundedRect(item, 6, 6);
            }
            p.setPen(i == 1 ? QColor(6, 18, 26) : QColor(235, 237, 240));
            p.drawText(item.adjusted(9, 0, -9, 0), Qt::AlignLeft | Qt::AlignVCenter, items.at(i));
        }
    }

private:
    int m_window = 70;
    int m_menu = 100;
    bool m_glass = true;
};

//* one effect of KWin: whether it is there and on, what it does, where to get it
class NgEffectCard : public QFrame
{
public:
    NgEffectCard(const EffectInfo &info, QWidget *parent)
        : QFrame(parent)
        , m_info(info)
    {
        setFrameShape(QFrame::StyledPanel);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(14, 12, 14, 12);
        layout->setSpacing(8);
        auto *head = new QHBoxLayout();
        auto *name = new QLabel(QStringLiteral("<b>%1</b>").arg(QLatin1String(info.name)), this);
        m_status = new QLabel(this);
        head->addWidget(name);
        head->addStretch();
        head->addWidget(m_status);
        layout->addLayout(head);
        auto *what = new QLabel(ngText(info.english, info.italian), this);
        what->setWordWrap(true);
        layout->addWidget(what);
        auto *buttons = new QHBoxLayout();
        m_effects = new QPushButton(QIcon::fromTheme(QStringLiteral("preferences-desktop-effects")), ngText("Desktop effects…", "Effetti del desktop…"), this);
        m_install = new QPushButton(QIcon::fromTheme(QStringLiteral("download")), ngText("How to install it", "Come installarlo"), this);
        auto *site = new QPushButton(QIcon::fromTheme(QStringLiteral("internet-services")), ngText("Project site", "Sito del progetto"), this);
        buttons->addWidget(m_effects);
        buttons->addWidget(m_install);
        buttons->addWidget(site);
        buttons->addStretch();
        layout->addLayout(buttons);
        connect(m_effects, &QPushButton::clicked, this, [] {
            QProcess::startDetached(QStringLiteral("systemsettings"), {QStringLiteral("kcm_kwin_effects")});
        });
        auto openSite = [this] {
            QDesktopServices::openUrl(QUrl(QString::fromLatin1(m_info.site)));
        };
        connect(m_install, &QPushButton::clicked, this, openSite);
        connect(site, &QPushButton::clicked, this, openSite);
        refresh();
    }
    void refresh()
    {
        const bool installed = effectInstalled(m_info.id);
        const bool enabled = installed && effectEnabled(m_info.id);
        const QString colour = enabled ? QStringLiteral("#4caf6a") : QStringLiteral("#e0a23d");
        const QString text = enabled ? ngText("On", "Attivo") : installed ? ngText("Installed, off", "Installato, spento") : ngText("Not installed", "Non installato");
        m_status->setText(QStringLiteral("<span style=\"color:%1;font-weight:600\">● %2</span>").arg(colour, text));
        m_install->setVisible(!installed);
        m_effects->setVisible(installed);
    }

private:
    EffectInfo m_info;
    QLabel *m_status = nullptr;
    QPushButton *m_effects = nullptr;
    QPushButton *m_install = nullptr;
};

namespace
{
QWidget *page(const QString &title, const QString &hint, QVBoxLayout **body)
{
    auto *widget = new QWidget();
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(18, 12, 18, 12);
    layout->setSpacing(12);
    auto *heading = new QLabel(title, widget);
    QFont font = heading->font();
    font.setPointSizeF(font.pointSizeF() * 1.25);
    font.setWeight(QFont::DemiBold);
    heading->setFont(font);
    layout->addWidget(heading);
    if (!hint.isEmpty()) {
        auto *label = new QLabel(hint, widget);
        label->setWordWrap(true);
        label->setEnabled(false);
        layout->addWidget(label);
    }
    *body = layout;
    return widget;
}

QLabel *groupTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    QFont font = label->font();
    font.setWeight(QFont::DemiBold);
    label->setFont(font);
    label->setContentsMargins(0, 8, 0, 0);
    return label;
}

QSlider *slider(int minimum, int maximum, QWidget *parent)
{
    auto *s = new QSlider(Qt::Horizontal, parent);
    s->setRange(minimum, maximum);
    return s;
}

QWidget *sliderRow(QSlider *s, QLabel *value, QWidget *parent)
{
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(s, 1);
    value->setMinimumWidth(80);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(value);
    return row;
}
}

//__________________________________________________________________
StyleConfig::StyleConfig(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_nav = new QListWidget(this);
    m_nav->setFrameShape(QFrame::NoFrame);
    m_nav->setIconSize(QSize(22, 22));
    m_nav->setSpacing(2);
    m_nav->setFixedWidth(220);
    m_nav->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_nav->setMinimumHeight(7 * 40 + 12); // all the pages, never a scroll bar
    setMinimumSize(900, 560); // (the dialog scrolls the whole module: the pages need no scroll areas of their own)
    m_pages = new QStackedWidget(this);
    layout->addWidget(m_nav);
    layout->addWidget(m_pages, 1);

    auto add = [this](const QString &icon, const QString &name, QWidget *content) {
        auto *item = new QListWidgetItem(QIcon::fromTheme(icon), name, m_nav);
        item->setSizeHint(QSize(0, 36));
        m_pages->addWidget(content);
    };
    add(QStringLiteral("preferences-desktop-theme-global"), ngText("Look", "Aspetto"), pageLook());
    add(QStringLiteral("preferences-desktop-effects"), ngText("Transparency", "Trasparenza"), pageTransparency());
    add(QStringLiteral("preferences-desktop-color"), QStringLiteral("Aurora"), pageAurora());
    add(QStringLiteral("preferences-desktop"), ngText("Controls and animations", "Controlli e animazioni"), pageControls());
    add(QStringLiteral("window"), ngText("Windows", "Finestre"), pageWindows());
    add(QStringLiteral("kwin"), ngText("KWin effects", "Effetti di KWin"), pageEffects());
    add(QStringLiteral("help-about"), ngText("About", "Informazioni"), pageAbout());
    connect(m_nav, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    connect(m_nav, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row == 4) {
            refreshEffects(); // an effect may have been installed or switched on meanwhile
        }
    });
    // BREEZE_NG_SETTINGS_PAGE=0..5 opens on that page (screenshots, links from elsewhere)
    m_nav->setCurrentRow(qBound(0, qEnvironmentVariableIntValue("BREEZE_NG_SETTINGS_PAGE"), m_pages->count() - 1));

    load();
    // the first page is laid out before the dialog has its size: lay the whole module out again once it is shown
    QTimer::singleShot(0, this, [this] {
        m_nav->updateGeometry();
        if (this->layout()) {
            this->layout()->invalidate();
        }
    });
}

QWidget *StyleConfig::pageLook()
{
    QVBoxLayout *body = nullptr;
    QWidget *w = page(ngText("Look", "Aspetto"),
                      ngText("The look goes with the window decoration and the colours: choosing it here applies all three, as the global theme does.",
                             "La variante va insieme alla decorazione delle finestre e ai colori: scegliendola qui si applicano tutti e tre, come col tema globale."),
                      &body);
    auto *tiles = new QHBoxLayout();
    tiles->setSpacing(14);
    m_soft = new NgLookTile(false, QStringLiteral("Soft"), ngText("Solid, soft surfaces, without separator lines.", "Superfici piene e morbide, senza linee di separazione."), w);
    m_glass = new NgLookTile(true,
                             QStringLiteral("Glass"),
                             ngText("See-through windows and menus, with the desktop blurred behind.", "Finestre e menu trasparenti con la sfocatura del desktop dietro."),
                             w);
    m_aurora = new NgLookTile(true,
                              QStringLiteral("Aurora"),
                              ngText("Glass without rims, with lights of the accent colour behind the windows.",
                                     "Vetro senza bordi, con le luci del colore d'accento dietro le finestre."),
                              w,
                              true);
    tiles->addWidget(m_soft);
    tiles->addWidget(m_glass);
    tiles->addWidget(m_aurora);
    body->addLayout(tiles);
    // the accent colour: KDE's own (System Settings > Colours), one click away. It tints the selection, the sliders
    // and, in Aurora, the lights behind the windows. Applied at once.
    body->addWidget(groupTitle(ngText("Accent colour", "Colore d'accento"), w));
    {
        auto *row = new QHBoxLayout();
        row->setSpacing(10);
        static const char *const colours[] = {"#6a9bcc", "#3eb4a0", "#62b86a", "#9676e0", "#e06ca0", "#e8843c", "#e2b93b", "#d9534f"};
        for (const char *hex : colours) {
            auto *swatch = new QToolButton(w);
            swatch->setFixedSize(28, 28);
            swatch->setCursor(Qt::PointingHandCursor);
            swatch->setAutoRaise(true);
            swatch->setToolTip(QString::fromLatin1(hex));
            QPixmap dot(44, 44);
            dot.fill(Qt::transparent);
            {
                QPainter dp(&dot);
                dp.setRenderHint(QPainter::Antialiasing);
                dp.setPen(Qt::NoPen);
                dp.setBrush(QColor(QString::fromLatin1(hex)));
                dp.drawEllipse(QRectF(4, 4, 36, 36));
            }
            swatch->setIcon(QIcon(dot));
            swatch->setIconSize(QSize(22, 22));
            const QString colour = QString::fromLatin1(hex);
            connect(swatch, &QToolButton::clicked, this, [colour] {
                const QString scheme = KSharedConfig::openConfig(QStringLiteral("kdeglobals"))->group(QStringLiteral("General")).readEntry("ColorScheme", QStringLiteral("BreezeNG"));
                QProcess::startDetached(QStringLiteral("plasma-apply-colorscheme"), {QStringLiteral("--accent-color"), colour, scheme});
            });
            row->addWidget(swatch);
        }
        auto *own = new QPushButton(ngText("The colour scheme's own", "Quello dello schema di colori"), w);
        connect(own, &QPushButton::clicked, this, [] {
            auto config = KSharedConfig::openConfig(QStringLiteral("kdeglobals"));
            auto general = config->group(QStringLiteral("General"));
            const QString scheme = general.readEntry("ColorScheme", QStringLiteral("BreezeNG"));
            general.deleteEntry("AccentColor");
            general.deleteEntry("LastUsedCustomAccentColor");
            config->sync();
            // applying another scheme and then this one again makes Plasma rebuild the colours without the accent
            QProcess::startDetached(QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral("plasma-apply-colorscheme BreezeDark >/dev/null 2>&1; plasma-apply-colorscheme \"$0\"")
                                                              , scheme});
        });
        row->addSpacing(8);
        row->addWidget(own);
        row->addStretch();
        body->addLayout(row);
    }
    body->addWidget(groupTitle(ngText("Together with the look", "Insieme alla variante"), w));
    m_applyDecoration = new QCheckBox(ngText("Also apply the Breeze NG window decoration", "Applica anche la decorazione delle finestre Breeze NG"), w);
    m_applyColours = new QCheckBox(ngText("Also apply the BreezeNG colour scheme", "Applica anche lo schema di colori BreezeNG"), w);
    body->addWidget(m_applyDecoration);
    body->addWidget(m_applyColours);
    body->addStretch();
    for (QAbstractButton *b : {static_cast<QAbstractButton *>(m_soft), static_cast<QAbstractButton *>(m_glass), static_cast<QAbstractButton *>(m_aurora), static_cast<QAbstractButton *>(m_applyDecoration),
                               static_cast<QAbstractButton *>(m_applyColours)}) {
        connect(b, &QAbstractButton::toggled, this, &StyleConfig::updateChanged);
    }
    return w;
}

QWidget *StyleConfig::pageTransparency()
{
    QVBoxLayout *body = nullptr;
    QWidget *w = page(ngText("Transparency", "Trasparenza"),
                      ngText("The windows are see-through in the Glass look only. Without a blur effect in KWin the see-through parts show the desktop sharp: see “KWin effects”.",
                             "Le finestre sono trasparenti solo nella variante Glass. Senza un effetto di sfocatura in KWin le parti trasparenti mostrano il desktop nitido: vedi «Effetti di KWin»."),
                      &body);
    m_preview = new NgOpacityPreview(w);
    body->addWidget(m_preview);
    auto *form = new QFormLayout();
    m_windowOpacity = slider(30, 100, w);
    m_menuOpacity = slider(30, 100, w);
    m_windowOpacityValue = new QLabel(w);
    m_menuOpacityValue = new QLabel(w);
    form->addRow(ngText("Window opacity", "Opacità delle finestre"), sliderRow(m_windowOpacity, m_windowOpacityValue, w));
    form->addRow(ngText("Menu opacity", "Opacità dei menu"), sliderRow(m_menuOpacity, m_menuOpacityValue, w));
    body->addLayout(form);
    auto *note = new QLabel(ngText("Menus also cover drop-down lists and tooltips. 100 % is solid.", "I menu comprendono anche gli elenchi a tendina e i suggerimenti. 100 % è pieno."), w);
    note->setEnabled(false);
    note->setWordWrap(true);
    body->addWidget(note);
    body->addStretch();
    for (QSlider *s : {m_windowOpacity, m_menuOpacity}) {
        connect(s, &QSlider::valueChanged, this, &StyleConfig::updateChanged);
    }
    return w;
}

QWidget *StyleConfig::pageAurora()
{
    QVBoxLayout *body = nullptr;
    QWidget *w = page(QStringLiteral("Aurora"),
                      ngText("Everything that shapes the Aurora look: how see-through the glass is, how much the surfaces on it stand out, the lights, the grain, the blur. "
                             "They apply when Aurora is the look chosen in “Look”.",
                             "Tutto ciò che dà forma ad Aurora: quanto è trasparente il vetro, quanto risaltano le superfici che ci stanno sopra, le luci, la grana, la sfocatura. "
                             "Valgono quando Aurora è la variante scelta in «Aspetto»."),
                      &body);
    // Aurora has its own: the glass of the window, the veil of what lies on it, the lights behind
    m_auroraGroup = new QWidget(w);
    {
        auto *group = new QVBoxLayout(m_auroraGroup);
        group->setContentsMargins(0, 0, 0, 0);
        group->addWidget(groupTitle(ngText("Glass", "Vetro"), m_auroraGroup));
        m_auroraLight = new QCheckBox(ngText("Aurora Light: a lighter, airier glass (off: dark tones)", "Aurora Light: un vetro più chiaro e arioso (spento: toni scuri)"), m_auroraGroup);
        group->addWidget(m_auroraLight);
        connect(m_auroraLight, &QCheckBox::toggled, this, &StyleConfig::updateChanged);
        auto *aurora = new QFormLayout();
        m_auroraBody = slider(15, 90, m_auroraGroup);
        m_auroraPanel = slider(0, 25, m_auroraGroup);
        m_auroraButton = slider(0, 25, m_auroraGroup);
        m_auroraLights = slider(0, 160, m_auroraGroup);
        m_blurStrength = slider(1, 15, m_auroraGroup);
        m_auroraGrain = slider(0, 100, m_auroraGroup);
        m_auroraGrainValue = new QLabel(m_auroraGroup);
        m_auroraBodyValue = new QLabel(m_auroraGroup);
        m_auroraPanelValue = new QLabel(m_auroraGroup);
        m_auroraButtonValue = new QLabel(m_auroraGroup);
        m_auroraLightsValue = new QLabel(m_auroraGroup);
        m_blurStrengthValue = new QLabel(m_auroraGroup);
        aurora->addRow(ngText("Window opacity", "Opacità della finestra"), sliderRow(m_auroraBody, m_auroraBodyValue, m_auroraGroup));
        m_auroraBody->setToolTip(ngText("The glass of Aurora Light; Aurora with dark tones adds 22 to it", "Il vetro di Aurora Light; Aurora a toni scuri ne aggiunge 22"));
        aurora->addRow(ngText("Sidebar, cards, lists", "Barra laterale, schede, liste"), sliderRow(m_auroraPanel, m_auroraPanelValue, m_auroraGroup));
        aurora->addRow(ngText("Buttons and fields", "Pulsanti e campi"), sliderRow(m_auroraButton, m_auroraButtonValue, m_auroraGroup));
        aurora->addRow(ngText("Lights", "Luci"), sliderRow(m_auroraLights, m_auroraLightsValue, m_auroraGroup));
        aurora->addRow(ngText("Grain", "Grana"), sliderRow(m_auroraGrain, m_auroraGrainValue, m_auroraGroup));
        aurora->addRow(ngText("Blur", "Sfocatura"), sliderRow(m_blurStrength, m_blurStrengthValue, m_auroraGroup));
        group->addLayout(aurora);
        auto *auroraNote = new QLabel(ngText("The two veils are equal by default, so that sidebar, cards, buttons and fields have one intensity. The grain hides the bands that faint gradients show on some screens. The blur is KWin's: "
                                             "one strength for the whole desktop, it cannot differ between the parts of a window.",
                                             "I due veli sono uguali in partenza, così barra laterale, schede, pulsanti e campi hanno la stessa intensità. La grana nasconde le bande che le sfumature tenui mostrano su certi schermi. La "
                                             "sfocatura è quella di KWin: una sola per tutto il desktop, non può cambiare tra le parti di una finestra."),
                                      m_auroraGroup);
        auroraNote->setEnabled(false);
        auroraNote->setWordWrap(true);
        group->addWidget(auroraNote);
    }
    body->addWidget(m_auroraGroup);
    auto *defaults = new QPushButton(ngText("Aurora's defaults", "Valori predefiniti di Aurora"), w);
    connect(defaults, &QPushButton::clicked, this, [this] {
        m_auroraBody->setValue(52);
        m_auroraPanel->setValue(7);
        m_auroraButton->setValue(7);
        m_auroraLights->setValue(100);
        m_auroraGrain->setValue(30);
        updateChanged();
    });
    auto *row = new QHBoxLayout();
    row->addWidget(defaults);
    row->addStretch();
    body->addLayout(row);
    body->addStretch();
    for (QSlider *s : {m_auroraBody, m_auroraPanel, m_auroraButton, m_auroraLights, m_auroraGrain, m_blurStrength}) {
        connect(s, &QSlider::valueChanged, this, &StyleConfig::updateChanged);
    }
    return w;
}

QWidget *StyleConfig::pageControls()
{
    QVBoxLayout *body = nullptr;
    QWidget *w = page(ngText("Controls and animations", "Controlli e animazioni"), QString(), &body);
    body->addWidget(groupTitle(ngText("Animations", "Animazioni"), w));
    m_animations = new QCheckBox(ngText("Animate the controls (buttons, switches, tabs)", "Anima i controlli (pulsanti, interruttori, schede)"), w);
    body->addWidget(m_animations);
    auto *speedForm = new QFormLayout();
    m_animationSpeed = slider(0, 4, w);
    m_animationSpeed->setPageStep(1);
    m_animationSpeedValue = new QLabel(w);
    speedForm->addRow(ngText("Speed", "Velocità"), sliderRow(m_animationSpeed, m_animationSpeedValue, w));
    body->addLayout(speedForm);
    m_transitions = new QCheckBox(ngText("Transitions between pages", "Transizioni tra le pagine"), w);
    body->addWidget(m_transitions);

    body->addWidget(groupTitle(ngText("Controls", "Controlli"), w));
    auto *form = new QFormLayout();
    m_mnemonics = new QComboBox(w);
    m_mnemonics->addItems({ngText("Never", "Mai"), ngText("While Alt is held", "Visibili tenendo premuto Alt"), ngText("Always", "Sempre visibili")});
    m_scrollTop = new QComboBox(w);
    m_scrollBottom = new QComboBox(w);
    for (QComboBox *c : {m_scrollTop, m_scrollBottom}) {
        c->addItems({ngText("None", "Nessuna"), ngText("One", "Una"), ngText("Two", "Due")});
    }
    form->addRow(ngText("Keyboard shortcuts (underlined letters)", "Scorciatoie da tastiera (lettere sottolineate)"), m_mnemonics);
    form->addRow(ngText("Scroll bar arrows at the top", "Frecce in cima alla barra di scorrimento"), m_scrollTop);
    form->addRow(ngText("Scroll bar arrows at the bottom", "Frecce in fondo alla barra di scorrimento"), m_scrollBottom);
    body->addLayout(form);
    m_centeredTabs = new QCheckBox(ngText("Centred tabs", "Schede centrate"), w);
    m_tickMarks = new QCheckBox(ngText("Tick marks on sliders", "Tacche sui cursori"), w);
    m_focusGlow = new QCheckBox(ngText("A glow inside the field or button that has the focus (otherwise a thin outline)",
                                       "Alone dentro il campo o il pulsante che ha il fuoco (altrimenti un contorno sottile)"),
                                w);
    m_menuJoin = new QCheckBox(ngText("A menu opened from a button joins it into one shape", "Il menu aperto da un pulsante si unisce al pulsante"), w);
    m_viewFocus = new QCheckBox(ngText("Focus indicator in lists", "Indicatore del fuoco nelle liste"), w);
    m_toolbarSeparators = new QCheckBox(ngText("Separators between toolbar items", "Separatori tra gli elementi delle barre degli strumenti"), w);
    for (QCheckBox *c : {m_centeredTabs, m_tickMarks, m_focusGlow, m_menuJoin, m_viewFocus, m_toolbarSeparators}) {
        body->addWidget(c);
    }
    body->addStretch();
    for (QCheckBox *c : {m_animations, m_transitions, m_centeredTabs, m_tickMarks, m_focusGlow, m_menuJoin, m_viewFocus, m_toolbarSeparators}) {
        connect(c, &QCheckBox::toggled, this, &StyleConfig::updateChanged);
    }
    connect(m_animationSpeed, &QSlider::valueChanged, this, &StyleConfig::updateChanged);
    for (QComboBox *c : {m_mnemonics, m_scrollTop, m_scrollBottom}) {
        connect(c, &QComboBox::currentIndexChanged, this, &StyleConfig::updateChanged);
    }
    return w;
}

QWidget *StyleConfig::pageWindows()
{
    QVBoxLayout *body = nullptr;
    QWidget *w = page(ngText("Windows", "Finestre"), QString(), &body);
    auto *form = new QFormLayout();
    m_drag = new QComboBox(w);
    m_drag->addItems({ngText("The title bar only", "Solo la barra del titolo"), ngText("Title bar, menu bar and toolbars", "Barra del titolo, barra dei menu e strumenti"),
                      ngText("Any empty area", "Qualsiasi area vuota")});
    m_shadowSize = new QComboBox(w);
    m_shadowSize->addItems({ngText("None", "Nessuna"), ngText("Small", "Piccola"), ngText("Medium", "Media"), ngText("Large", "Grande"), ngText("Very large", "Molto grande")});
    m_shadowStrength = slider(25, 255, w);
    m_shadowStrengthValue = new QLabel(w);
    form->addRow(ngText("Drag windows from", "Trascina la finestra da"), m_drag);
    form->addRow(ngText("Shadow of menus and tooltips", "Ombra di menu e suggerimenti"), m_shadowSize);
    form->addRow(ngText("Shadow strength", "Intensità dell'ombra"), sliderRow(m_shadowStrength, m_shadowStrengthValue, w));
    body->addLayout(form);
    body->addWidget(groupTitle(ngText("Panels and views", "Pannelli e viste"), w));
    m_dockFrame = new QCheckBox(ngText("Frame around dockable panels", "Cornice attorno ai pannelli agganciabili"), w);
    m_sideFrame = new QCheckBox(ngText("Frame around side panels", "Cornice attorno ai pannelli laterali"), w);
    m_branchLines = new QCheckBox(ngText("Branch lines in folder trees", "Rami negli alberi delle cartelle"), w);
    m_strongFocus = new QCheckBox(ngText("Strong highlight of the current menu item", "Evidenziazione netta della voce di menu corrente"), w);
    m_splitterProxy = new QCheckBox(ngText("Wider grab area for splitters", "Area di presa più larga per i divisori"), w);
    for (QCheckBox *c : {m_dockFrame, m_sideFrame, m_branchLines, m_strongFocus, m_splitterProxy}) {
        body->addWidget(c);
        connect(c, &QCheckBox::toggled, this, &StyleConfig::updateChanged);
    }
    body->addStretch();
    for (QComboBox *c : {m_drag, m_shadowSize}) {
        connect(c, &QComboBox::currentIndexChanged, this, &StyleConfig::updateChanged);
    }
    connect(m_shadowStrength, &QSlider::valueChanged, this, &StyleConfig::updateChanged);
    return w;
}

QWidget *StyleConfig::pageEffects()
{
    QVBoxLayout *body = nullptr;
    QWidget *w = page(ngText("KWin effects", "Effetti di KWin"),
                      ngText("The rounded corners of the windows and the blur behind the see-through parts are not part of Breeze NG: they come from two KWin effects "
                             "of other projects. Here you see whether they are there and on.",
                             "Gli angoli arrotondati delle finestre e la sfocatura dietro alle parti trasparenti non sono di Breeze NG: vengono da due effetti di KWin "
                             "di altri progetti. Qui vedi se ci sono e se sono attivi."),
                      &body);
    m_blurCard = new NgEffectCard(blurEffect, w);
    m_cornersCard = new NgEffectCard(cornersEffect, w);
    body->addWidget(m_blurCard);
    body->addWidget(m_cornersCard);
    auto *note = new QLabel(ngText("They are external projects: the Breeze NG installer builds them from their own repositories only if you ask. For problems with them, "
                                   "refer to their repositories.",
                                   "Sono progetti esterni: l'installer di Breeze NG li compila dai loro repository solo se lo chiedi. Per problemi con loro, vale il loro "
                                   "repository."),
                            w);
    note->setWordWrap(true);
    note->setEnabled(false);
    body->addWidget(note);
    body->addStretch();
    return w;
}

QWidget *StyleConfig::pageAbout()
{
    QVBoxLayout *body = nullptr;
    QWidget *w = page(ngText("About", "Informazioni"), QString(), &body);
    auto *head = new QHBoxLayout();
    auto *icon = new QLabel(w);
    icon->setPixmap(QIcon::fromTheme(QStringLiteral("preferences-desktop-theme-global")).pixmap(64, 64));
    auto *text = new QLabel(QStringLiteral("<b style=\"font-size:large\">Breeze NG</b><br>%1")
                                .arg(ngText("Application style and window decoration · GPL-2.0-or-later · based on Breeze",
                                            "Stile delle applicazioni e decorazione delle finestre · GPL-2.0-or-later · basato su Breeze")),
                            w);
    text->setWordWrap(true);
    head->addWidget(icon);
    head->addWidget(text, 1);
    body->addLayout(head);
    auto *links = new QLabel(QStringLiteral("<a href=\"https://github.com/Morzilla/breeze-ng\">github.com/Morzilla/breeze-ng</a><br>"
                                            "<a href=\"https://github.com/Morzilla/dolphin-ng\">Dolphin NG</a><br>"
                                            "<a href=\"https://store.kde.org/p/2381557/\">KDE Store</a>"),
                             w);
    links->setOpenExternalLinks(true);
    body->addWidget(links);
    body->addWidget(groupTitle(ngText("Maintenance", "Manutenzione"), w));
    auto *buttons = new QHBoxLayout();
    auto *back = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-undo")), ngText("Back to Breeze", "Torna a Breeze"), w);
    auto *reset = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-reset")), ngText("Restore the defaults", "Ripristina le impostazioni predefinite"), w);
    buttons->addWidget(back);
    buttons->addWidget(reset);
    buttons->addStretch();
    body->addLayout(buttons);
    body->addStretch();
    connect(reset, &QPushButton::clicked, this, &StyleConfig::defaults);
    connect(back, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this,
                                  ngText("Back to Breeze", "Torna a Breeze"),
                                  ngText("Switch the application style and the window decoration back to Breeze? Breeze NG stays installed.",
                                         "Rimettere Breeze come stile delle applicazioni e decorazione delle finestre? Breeze NG resta installato."))
            != QMessageBox::Yes) {
            return;
        }
        KSharedConfig::openConfig(QStringLiteral("kdeglobals"))->group(QStringLiteral("KDE")).writeEntry("widgetStyle", QStringLiteral("Breeze"), KConfig::Notify);
        auto kwin = KSharedConfig::openConfig(QStringLiteral("kwinrc"))->group(QStringLiteral("org.kde.kdecoration2"));
        kwin.writeEntry("library", QStringLiteral("org.kde.breeze"));
        kwin.writeEntry("theme", QStringLiteral("Breeze"));
        kwin.sync();
        KSharedConfig::openConfig(QStringLiteral("kdeglobals"))->sync();
        reloadKWin();
        notifyStyleChanged();
    });
    return w;
}

void StyleConfig::refreshEffects()
{
    if (m_blurCard) {
        m_blurCard->refresh();
    }
    if (m_cornersCard) {
        m_cornersCard->refresh();
    }
}

int StyleConfig::animationIndex() const
{
    // the speed nearest to the duration in the settings
    const int duration = StyleConfigData::animationsDuration();
    int best = 2;
    for (int i = 0; i < 5; ++i) {
        if (qAbs(animationDurations[i] - duration) < qAbs(animationDurations[best] - duration)) {
            best = i;
        }
    }
    return best;
}

//__________________________________________________________________
void StyleConfig::save()
{
    const int look = m_aurora->isChecked() ? StyleConfigData::Aurora : m_glass->isChecked() ? StyleConfigData::Glass : StyleConfigData::Soft;
    const bool variantChanged = ngCurrentLook() != look;
    StyleConfigData::setVariant(look);
    StyleConfigData::setWindowOpacity(m_windowOpacity->value());
    StyleConfigData::setMenuOpacity(m_menuOpacity->value());
    StyleConfigData::setAuroraBodyOpacity(m_auroraBody->value());
    StyleConfigData::setAuroraPanelVeil(m_auroraPanel->value());
    StyleConfigData::setAuroraButtonVeil(m_auroraButton->value());
    StyleConfigData::setAuroraLights(m_auroraLights->value());
    StyleConfigData::setAuroraGrain(m_auroraGrain->value());
    const bool blurChanged = m_blurStrength->value() != blurStrength();
    if (blurChanged) {
        setBlurStrength(m_blurStrength->value());
    }
    // the window decoration draws the title bar with the same glass and lights
    const bool decorationChanged = blurChanged || m_auroraBody->value() != m_savedAuroraBody || m_auroraLights->value() != m_savedAuroraLights || m_auroraGrain->value() != m_savedAuroraGrain;
    m_savedAuroraGrain = m_auroraGrain->value();
    m_savedAuroraBody = m_auroraBody->value();
    m_savedAuroraLights = m_auroraLights->value();
    StyleConfigData::setAnimationsEnabled(m_animations->isChecked());
    StyleConfigData::setAnimationsDuration(animationDurations[qBound(0, m_animationSpeed->value(), 4)]);
    StyleConfigData::setStackedWidgetTransitionsEnabled(m_transitions->isChecked());
    StyleConfigData::setMnemonicsMode(m_mnemonics->currentIndex());
    StyleConfigData::setScrollBarSubLineButtons(m_scrollTop->currentIndex());
    StyleConfigData::setScrollBarAddLineButtons(m_scrollBottom->currentIndex());
    StyleConfigData::setTabBarDrawCenteredTabs(m_centeredTabs->isChecked());
    StyleConfigData::setSliderDrawTickMarks(m_tickMarks->isChecked());
    StyleConfigData::setFocusGlow(m_focusGlow->isChecked());
    StyleConfigData::setMenuJoin(m_menuJoin->isChecked());
    StyleConfigData::setViewDrawFocusIndicator(m_viewFocus->isChecked());
    StyleConfigData::setToolBarDrawItemSeparator(m_toolbarSeparators->isChecked());
    StyleConfigData::setWindowDragMode(m_drag->currentIndex());
    StyleConfigData::setShadowSize(m_shadowSize->currentIndex());
    StyleConfigData::setShadowStrength(m_shadowStrength->value());
    StyleConfigData::setDockWidgetDrawFrame(m_dockFrame->isChecked());
    StyleConfigData::setSidePanelDrawFrame(m_sideFrame->isChecked());
    StyleConfigData::setViewDrawTreeBranchLines(m_branchLines->isChecked());
    StyleConfigData::setMenuItemDrawStrongFocus(m_strongFocus->isChecked());
    StyleConfigData::setSplitterProxyEnabled(m_splitterProxy->isChecked());
    StyleConfigData::self()->save();

    // the choices of what goes with the look, remembered for next time
    auto settings = KSharedConfig::openConfig(QStringLiteral("breezengrc"))->group(QStringLiteral("Settings"));
    settings.writeEntry("ApplyDecoration", m_applyDecoration->isChecked());
    settings.writeEntry("ApplyColours", m_applyColours->isChecked());
    settings.sync();

    const QString variant = look == StyleConfigData::Aurora ? (m_auroraLight->isChecked() ? QStringLiteral("Aurora Light") : QStringLiteral("Aurora"))
        : look == StyleConfigData::Glass ? QStringLiteral("Glass")
                                         : QStringLiteral("Soft");
    if (m_applyDecoration->isChecked()) {
        // the look follows the decoration theme KWin was given: set it, or the choice above would not hold
        auto kwin = KSharedConfig::openConfig(QStringLiteral("kwinrc"))->group(QStringLiteral("org.kde.kdecoration2"));
        if (kwin.readEntry("library") != QLatin1String("org.kde.breezeng") || kwin.readEntry("theme") != QStringLiteral("Breeze NG ") + variant) {
            kwin.writeEntry("library", QStringLiteral("org.kde.breezeng"));
            kwin.writeEntry("theme", QStringLiteral("Breeze NG ") + variant);
            kwin.sync();
            reloadKWin();
        } else if (decorationChanged) {
            reloadKWin();
        }
    } else if (decorationChanged) {
        reloadKWin();
    }
    if (m_applyColours->isChecked() && variantChanged) {
        QProcess::startDetached(QStringLiteral("plasma-apply-colorscheme"), {QStringLiteral("BreezeNG")});
    }

#if HAVE_QTDBUS
    // emit dbus signal
    QDBusMessage message(
        QDBusMessage::createSignal(QStringLiteral("/BreezeStyle"), QStringLiteral("org.kde.Breeze.Style"), QStringLiteral("reparseConfiguration")));
    QDBusConnection::sessionBus().send(message);
#endif
    if (variantChanged) {
        notifyStyleChanged();
    }
    updateChanged();
}

//__________________________________________________________________
void StyleConfig::defaults()
{
    StyleConfigData::self()->setDefaults();
    load();
    m_applyDecoration->setChecked(true);
    m_applyColours->setChecked(true);
    updateChanged();
}

//__________________________________________________________________
void StyleConfig::reset()
{
    // reparse configuration
    StyleConfigData::self()->load();

    load();
}

//__________________________________________________________________
void StyleConfig::updateChanged()
{
    // the labels and the preview follow the controls
    m_windowOpacityValue->setText(QStringLiteral("%1 %").arg(m_windowOpacity->value()));
    m_menuOpacityValue->setText(QStringLiteral("%1 %").arg(m_menuOpacity->value()));
    m_auroraBodyValue->setText(QStringLiteral("%1 %").arg(m_auroraBody->value()));
    m_auroraPanelValue->setText(QStringLiteral("%1 %").arg(m_auroraPanel->value()));
    m_auroraButtonValue->setText(QStringLiteral("%1 %").arg(m_auroraButton->value()));
    m_auroraLightsValue->setText(QStringLiteral("%1 %").arg(m_auroraLights->value()));
    m_auroraGrainValue->setText(QStringLiteral("%1 %").arg(m_auroraGrain->value()));
    m_blurStrengthValue->setText(QString::number(m_blurStrength->value()));
    m_auroraGroup->setEnabled(m_aurora->isChecked());
    static const char *const speeds[][2] = {{"Very fast", "Molto veloce"}, {"Fast", "Veloce"}, {"Normal", "Normale"}, {"Slow", "Lenta"}, {"Very slow", "Molto lenta"}};
    const int speed = qBound(0, m_animationSpeed->value(), 4);
    m_animationSpeedValue->setText(ngText(speeds[speed][0], speeds[speed][1]));
    m_shadowStrengthValue->setText(QStringLiteral("%1 %").arg(qRound(m_shadowStrength->value() * 100.0 / 255.0)));
    m_animationSpeed->setEnabled(m_animations->isChecked());
    m_windowOpacity->setEnabled(m_glass->isChecked() && !m_aurora->isChecked());
    m_preview->setOpacities(m_windowOpacity->value(), m_menuOpacity->value(), m_glass->isChecked() || m_aurora->isChecked());
    if (m_loading) {
        return;
    }

    const auto settings = KSharedConfig::openConfig(QStringLiteral("breezengrc"))->group(QStringLiteral("Settings"));
    const bool modified = (m_aurora->isChecked() ? StyleConfigData::Aurora : m_glass->isChecked() ? StyleConfigData::Glass : StyleConfigData::Soft) != ngCurrentLook()
        || m_applyDecoration->isChecked() != settings.readEntry("ApplyDecoration", true) || m_applyColours->isChecked() != settings.readEntry("ApplyColours", true)
        || m_windowOpacity->value() != StyleConfigData::windowOpacity() || m_menuOpacity->value() != StyleConfigData::menuOpacity()
        || m_auroraBody->value() != StyleConfigData::auroraBodyOpacity() || m_auroraPanel->value() != StyleConfigData::auroraPanelVeil()
        || m_auroraButton->value() != StyleConfigData::auroraButtonVeil() || m_auroraLights->value() != StyleConfigData::auroraLights()
        || m_auroraGrain->value() != StyleConfigData::auroraGrain() || (m_aurora->isChecked() && m_auroraLight->isChecked() != ngCurrentAuroraLight())
        || m_blurStrength->value() != blurStrength()
        || m_animations->isChecked() != StyleConfigData::animationsEnabled() || m_animationSpeed->value() != animationIndex()
        || m_transitions->isChecked() != StyleConfigData::stackedWidgetTransitionsEnabled() || m_mnemonics->currentIndex() != StyleConfigData::mnemonicsMode()
        || m_scrollTop->currentIndex() != StyleConfigData::scrollBarSubLineButtons() || m_scrollBottom->currentIndex() != StyleConfigData::scrollBarAddLineButtons()
        || m_centeredTabs->isChecked() != StyleConfigData::tabBarDrawCenteredTabs() || m_tickMarks->isChecked() != StyleConfigData::sliderDrawTickMarks()
        || m_focusGlow->isChecked() != StyleConfigData::focusGlow() || m_menuJoin->isChecked() != StyleConfigData::menuJoin()
        || m_viewFocus->isChecked() != StyleConfigData::viewDrawFocusIndicator() || m_toolbarSeparators->isChecked() != StyleConfigData::toolBarDrawItemSeparator()
        || m_drag->currentIndex() != StyleConfigData::windowDragMode() || m_shadowSize->currentIndex() != StyleConfigData::shadowSize()
        || m_shadowStrength->value() != StyleConfigData::shadowStrength() || m_dockFrame->isChecked() != StyleConfigData::dockWidgetDrawFrame()
        || m_sideFrame->isChecked() != StyleConfigData::sidePanelDrawFrame() || m_branchLines->isChecked() != StyleConfigData::viewDrawTreeBranchLines()
        || m_strongFocus->isChecked() != StyleConfigData::menuItemDrawStrongFocus() || m_splitterProxy->isChecked() != StyleConfigData::splitterProxyEnabled();
    Q_EMIT changed(modified);
}

//__________________________________________________________________
void StyleConfig::load()
{
    m_loading = true;
    const int look = ngCurrentLook();
    m_aurora->setChecked(look == StyleConfigData::Aurora);
    m_auroraLight->setChecked(ngCurrentAuroraLight());
    m_glass->setChecked(look == StyleConfigData::Glass);
    m_soft->setChecked(look == StyleConfigData::Soft);
    const auto settings = KSharedConfig::openConfig(QStringLiteral("breezengrc"))->group(QStringLiteral("Settings"));
    m_applyDecoration->setChecked(settings.readEntry("ApplyDecoration", true));
    m_applyColours->setChecked(settings.readEntry("ApplyColours", true));
    m_windowOpacity->setValue(StyleConfigData::windowOpacity());
    m_menuOpacity->setValue(StyleConfigData::menuOpacity());
    m_auroraBody->setValue(StyleConfigData::auroraBodyOpacity());
    m_auroraPanel->setValue(StyleConfigData::auroraPanelVeil());
    m_auroraButton->setValue(StyleConfigData::auroraButtonVeil());
    m_auroraLights->setValue(StyleConfigData::auroraLights());
    m_auroraGrain->setValue(StyleConfigData::auroraGrain());
    m_savedAuroraGrain = m_auroraGrain->value();
    m_blurStrength->setValue(blurStrength());
    m_savedAuroraBody = m_auroraBody->value();
    m_savedAuroraLights = m_auroraLights->value();
    m_animations->setChecked(StyleConfigData::animationsEnabled());
    m_animationSpeed->setValue(animationIndex());
    m_transitions->setChecked(StyleConfigData::stackedWidgetTransitionsEnabled());
    m_mnemonics->setCurrentIndex(StyleConfigData::mnemonicsMode());
    m_scrollTop->setCurrentIndex(StyleConfigData::scrollBarSubLineButtons());
    m_scrollBottom->setCurrentIndex(StyleConfigData::scrollBarAddLineButtons());
    m_centeredTabs->setChecked(StyleConfigData::tabBarDrawCenteredTabs());
    m_tickMarks->setChecked(StyleConfigData::sliderDrawTickMarks());
    m_focusGlow->setChecked(StyleConfigData::focusGlow());
    m_menuJoin->setChecked(StyleConfigData::menuJoin());
    m_viewFocus->setChecked(StyleConfigData::viewDrawFocusIndicator());
    m_toolbarSeparators->setChecked(StyleConfigData::toolBarDrawItemSeparator());
    m_drag->setCurrentIndex(StyleConfigData::windowDragMode());
    m_shadowSize->setCurrentIndex(StyleConfigData::shadowSize());
    m_shadowStrength->setValue(StyleConfigData::shadowStrength());
    m_dockFrame->setChecked(StyleConfigData::dockWidgetDrawFrame());
    m_sideFrame->setChecked(StyleConfigData::sidePanelDrawFrame());
    m_branchLines->setChecked(StyleConfigData::viewDrawTreeBranchLines());
    m_strongFocus->setChecked(StyleConfigData::menuItemDrawStrongFocus());
    m_splitterProxy->setChecked(StyleConfigData::splitterProxyEnabled());
    m_loading = false;
    updateChanged();
}

}
