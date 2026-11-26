# SPDX-License-Identifier: GPL-2.0-or-later
%global commit      a4ec7314c0df42b69b824bac22f1e94572c4c3f3
%global shortcommit %(c=%{commit}; echo ${c:0:7})
%global snapdate    202611261402
# snapdate: date and time of the commit (YYYYMMDDHHMM), so that two snapshots of one day sort in order
# installed apart from the system, next to Fedora's own Breeze
%global ngprefix    /opt/ng/breeze-ng
# its plugins have names of their own (breezeng6, org.kde.breezeng...), so links to them go in Qt's plugin folder,
# where every application and KWin find them at once, with no change to the session
%global __brp_check_rpaths %{nil}

Name:           breeze-ng
Version:        6.7.5^%{snapdate}git%{shortcommit}
Release:        1%{?dist}
Summary:        Breeze widget style and window decoration with three looks: Soft, Glass and Aurora Glass
License:        GPL-2.0-or-later AND MIT AND LGPL-2.0-or-later
URL:            https://github.com/Morzilla/breeze-ng
Source0:        %{url}/archive/%{commit}/breeze-ng-%{shortcommit}.tar.gz

BuildRequires:  cmake
BuildRequires:  qt6-rpm-macros
# the Plasma styles are drawn from Plasma's default style
BuildRequires:  libplasma
BuildRequires:  python3
BuildRequires:  extra-cmake-modules
BuildRequires:  gcc-c++
BuildRequires:  gettext
BuildRequires:  cmake(Qt6Core)
BuildRequires:  cmake(Qt6DBus)
BuildRequires:  cmake(Qt6Gui)
BuildRequires:  cmake(Qt6Quick)
BuildRequires:  cmake(Qt6Svg)
BuildRequires:  cmake(Qt6Widgets)
BuildRequires:  cmake(KF6ColorScheme)
BuildRequires:  cmake(KF6Config)
BuildRequires:  cmake(KF6CoreAddons)
BuildRequires:  cmake(KF6FrameworkIntegration)
BuildRequires:  cmake(KF6GuiAddons)
BuildRequires:  cmake(KF6I18n)
BuildRequires:  cmake(KF6IconThemes)
BuildRequires:  cmake(KF6KCMUtils)
BuildRequires:  cmake(KF6WindowSystem)
BuildRequires:  cmake(KDecoration3)
BuildRequires:  cmake(Qt6Qml)
BuildRequires:  cmake(KF6KirigamiPlatform)
# (the Aurora gradient of page titles in Kirigami applications and Plasma: without it they keep a plain colour)
Recommends:     qt6-qt5compat

%description
Breeze NG is a fork of KDE's Breeze widget style and window decoration with
two looks: Soft (surfaces told apart by tone, no separator lines) and Glass
(translucent windows and menus blurred by the compositor). It installs under
%{ngprefix}, next to the stock Breeze, which stays untouched, with links
to its plugins in Qt's plugin folder.

It also brings two Plasma styles in the same looks, for what Plasma draws
itself: the panel's popups, the launcher, their buttons, lists and fields.

After installing, pick "Breeze NG Soft" or "Breeze NG Glass" in System
Settings > Global Theme: it sets the widget style, the window decoration,
the colours and the Plasma style together. Applications already open keep their old style
until restarted.

%prep
%autosetup -n breeze-ng-%{commit}

%build
%set_build_flags
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=%{ngprefix} \
      -DBUILD_QT5=OFF -DBUILD_QT6=ON -DWITH_WALLPAPERS=OFF -DBUILD_CURSOR=OFF -DBUILD_TESTING=OFF
cmake --build build -j%{?_smp_build_ncpus}
# Aurora for Kirigami applications: a platform plugin in a directory of its own (ng/kirigami-platform/README.md)
cmake -S ng/kirigami-platform -B build-kirigami -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=%{ngprefix} \
      -DCMAKE_INSTALL_LIBDIR=%{_lib}
cmake --build build-kirigami -j%{?_smp_build_ncpus}

%install
DESTDIR=%{buildroot} cmake --install build
DESTDIR=%{buildroot} cmake --install build-kirigami
# the Kirigami plugin has to come before the desktop style's own in the plugin path: for the whole session, from the
# next login (the widget style and the decoration need neither this nor a login)
mkdir -p %{buildroot}%{_sysconfdir}/xdg/plasma-workspace/env
cat > %{buildroot}%{_sysconfdir}/xdg/plasma-workspace/env/breeze-ng-kirigami.sh <<'EOT'
# Breeze NG: Aurora for Kirigami applications (see %{ngprefix}/share/doc or ng/kirigami-platform/README.md)
export QT_PLUGIN_PATH="%{ngprefix}/%{_lib}/breeze-ng-kirigami${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
EOT
# the Plasma styles (panel popups, launcher, their buttons and lists), from Fedora's default style
for v in soft glass aurora; do
    python3 ng/plasma-style/make-plasma-style.py $v %{buildroot}%{_datadir}/plasma/desktoptheme/breeze-ng-$v
done
install -Dm644 -t %{buildroot}%{_datadir}/color-schemes ng/color-schemes/BreezeNG.colors ng/color-schemes/BreezeNGLight.colors ng/color-schemes/BreezeNGCode.colors ng/color-schemes/BreezeNGGrey.colors
install -Dm644 kstyle/breezeng.themerc %{buildroot}%{_datadir}/kstyle/themes/breezeng.themerc
mkdir -p %{buildroot}%{_datadir}/plasma/look-and-feel
cp -a ng/look-and-feel/org.morzilla.breezeng.* %{buildroot}%{_datadir}/plasma/look-and-feel/
# links to the plugins in Qt's plugin folder (no QT_PLUGIN_PATH, no new login)
for f in styles/breezeng6.so kstyle_config/breezengstyleconfig.so org.kde.kdecoration3/org.kde.breezeng.so \
         org.kde.kdecoration3.kcm/kcm_breezengdecoration.so; do
    test -e %{buildroot}%{ngprefix}/%{_lib}/plugins/$f
    mkdir -p "$(dirname %{buildroot}%{_qt6_plugindir}/$f)"
    ln -s %{ngprefix}/%{_lib}/plugins/$f %{buildroot}%{_qt6_plugindir}/$f
done

%files
%license LICENSES/*
%doc NG.md README.md
%{ngprefix}
%dir /opt/ng
%{_sysconfdir}/xdg/plasma-workspace/env/breeze-ng-kirigami.sh
%{_datadir}/color-schemes/BreezeNG.colors
%{_datadir}/color-schemes/BreezeNGLight.colors
%{_datadir}/color-schemes/BreezeNGCode.colors
%{_datadir}/color-schemes/BreezeNGGrey.colors
%{_datadir}/kstyle/themes/breezeng.themerc
%{_datadir}/plasma/look-and-feel/org.morzilla.breezeng.soft
%{_datadir}/plasma/look-and-feel/org.morzilla.breezeng.glass
%{_datadir}/plasma/look-and-feel/org.morzilla.breezeng.soft.light
%{_datadir}/plasma/look-and-feel/org.morzilla.breezeng.glass.light
%{_datadir}/plasma/look-and-feel/org.morzilla.breezeng.aurora
%{_datadir}/plasma/look-and-feel/org.morzilla.breezeng.aurora.light
%{_datadir}/plasma/look-and-feel/org.morzilla.breezeng.aurora.code
%{_datadir}/plasma/look-and-feel/org.morzilla.breezeng.grey
%{_datadir}/plasma/look-and-feel/org.morzilla.breezeng.aurora.airy
%{_datadir}/plasma/desktoptheme/breeze-ng-soft
%{_datadir}/plasma/desktoptheme/breeze-ng-glass
%{_datadir}/plasma/desktoptheme/breeze-ng-aurora
%{_qt6_plugindir}/styles/breezeng6.so
%{_qt6_plugindir}/kstyle_config/breezengstyleconfig.so
%{_qt6_plugindir}/org.kde.kdecoration3/org.kde.breezeng.so
%{_qt6_plugindir}/org.kde.kdecoration3.kcm/kcm_breezengdecoration.so

%changelog
* Thu Nov 26 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^202611261402gita4ec731-1
- Aurora: modern controls (pills, cards, segmented tabs), lighter window shadows in the accent
- Plasma in Aurora: panel and popups of the same glass, a floating panel by default (options)
- Rings for dials and System Monitor's gauges: gradient, glow or ticks (option)
- Titles in the accent or in a gradient (option); Kirigami inline messages as glass cards
- A menu joined to its button follows the menu opacity; a window stays active under its own menu

* Tue Nov 24 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^202611241417git0cc888b-1
- Sidebars of every application as Dolphin NG's: a card, compact rows (option), text tinged by the accent
- System Settings and other Qt Quick windows see-through again; readable list selections there
- Aurora: the title bar's lights as the window's

* Mon Nov 23 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^202611232009gite394658-1
- Kirigami plugin: plasmashell no longer crashes at start with it (Kubuntu 26.04)

* Mon Nov 23 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^202611231837git97cfb5f-1
- Text and light surfaces tinged by the accent (option)
- Aurora: coloured corner lights on light schemes too (option)
- Joined menus: the side shadow fades in from the button
- Buttons that are on: no glass shadow under their text

* Sun Nov 22 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^202611201755git58b0f6f-1
- Scroll bar handle on glass: a thin ring with the text shadow on

* Fri Nov 20 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^202611201715git71437e8-1
- Text shadow in Qt Quick applications too
- System Settings and Info Centre no longer come up empty with Aurora (OpenGL windows)
- Installer: system update instead of stopping

* Fri Nov 20 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^202611191344git0460bff-1
- Aurora: no lit line across the top of the title bar
- Glass: no black square between two scroll bars

* Thu Nov 19 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^202611191157gitde1a701-1
- Text shadow on glass (never, when the contrast may be low, always), icons too; not under dimmed text
- Location bar: the hover of a path button inside the field, no dark corners
- Installers: always build the branches' last commits

* Thu Nov 19 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^202611190929git1cbf7dd-1
- Glass again from the first window; a solid sheet only when KWin has no blur

* Wed Nov 18 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^202611181829git636105f-1
- Glass only where the compositor blurs: without KWin's blur effect a window stays solid

* Wed Nov 18 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^202611181157git87cd7ba-1
- Aurora: the buttons of a message bar without a dark outline
- Settings: Defaults are saved; an Apply button in System Settings' dialog; blur back to its default
- Buttons of message bars (errors, warnings) readable on their coloured band
- Aurora: a menu joined to its button is opaque, the two one colour

* Tue Nov 17 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^202611172030git160f83c-1
- Breeze NG Grey: an opaque light theme with its own colour scheme
- Menus: a light sheet of the accent under the pointer; Soft's primary button with readable text

* Tue Nov 17 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^202611171230git17fe3e7-1
- The title bar follows a new accent at once; no unblurred line under it at 125%
- Joined menus: no thin line under the button at a fractional scale

* Tue Nov 17 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^202611171142gitd478205-1
- Aurora: the accent's lights on the right, mirrored (on with the VS Code button)
- Glass windows: no solid header band on KDE settings pages

* Tue Nov 17 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^202611170937git4e786c6-1
- No crash with the platform's own dialogs (Spectacle's colour picker)

* Tue Nov 17 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^202611170916git9501354-1
- Aurora Light on the light scheme, Aurora Airy, Aurora Code, VS Code accents, accented checks, switches in Qt Quick

* Mon Nov 16 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^202611161949git06c167d-1
- Menus keep their glass with the grain

* Mon Nov 16 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^202611161933git974d94c-1
- Qt Quick: no crash in System Settings' Global Theme, no white sidebar, no stutter

* Mon Nov 16 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^202611161922git3682ce8-1
- Breeze NG Code colour scheme

* Mon Nov 16 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^202611161901gitab0183c-1
- Aurora: real window opacity, sidebar opacity, grey accent, menu grain, blurred combo lists, live scheme changes

* Mon Nov 16 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^202611161358git06e3620-1
- Aurora light (white frost, A/B selection), Kirigami plugin for Qt Quick apps, joined menus, relogin notice

* Sat Nov 14 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^202611141324git7deaab6-1
- Aurora: a third look, glass without rims, with lights behind the windows and in buttons

* Sat Nov 14 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^202611141244gitdbe1f58-1
- Aurora, a third look (first step): lights behind the windows, a light following the pointer in buttons

* Sat Nov 14 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^202611141235git9075f9a-1
- Light colour scheme and light global themes; previews for the global themes

* Sat Nov 14 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^202611141143git5d5757b-1
- Plasma styles Breeze NG Soft and Glass, chosen by the global themes

* Sat Nov 14 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^202611141043gitf1da5a9-1
- Radio button dot antialiased and centred

* Sat Nov 14 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^202611141032gitf8df1b2-1
- Joined menus on Wayland are anchored to the button's rectangle: exactly lined up

* Sat Nov 14 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^202611140926gitc65b3a9-1
- A joined menu lines up with its button on Wayland (one pixel off before)
- Snapshot versions carry the time of the commit, so builds of the same day sort right

* Sat Nov 14 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^20261114git4fd88f1-1
- A menu joined to its button follows it where the compositor slides it

* Sat Nov 14 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^20261113git399ea7a-2
- Links to the plugins in Qt's plugin folder: the style and the decoration are there
  at once, with no new login (the file in /etc/xdg/plasma-workspace/env is gone)

* Sat Nov 14 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^20261113git399ea7a-1
- First package
