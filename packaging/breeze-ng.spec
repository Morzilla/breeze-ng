# SPDX-License-Identifier: GPL-2.0-or-later
%global commit      5d5757bc30541beeeed1ee0cf17b8a1749d22fdd
%global shortcommit %(c=%{commit}; echo ${c:0:7})
%global snapdate    202611141143
# snapdate: date and time of the commit (YYYYMMDDHHMM), so that two snapshots of one day sort in order
# installed apart from the system, next to Fedora's own Breeze
%global ngprefix    /opt/ng/breeze-ng
# its plugins have names of their own (breezeng6, org.kde.breezeng...), so links to them go in Qt's plugin folder,
# where every application and KWin find them at once, with no change to the session
%global __brp_check_rpaths %{nil}

Name:           breeze-ng
Version:        6.7.5^%{snapdate}git%{shortcommit}
Release:        1%{?dist}
Summary:        Breeze widget style and window decoration with two looks, Soft and Glass
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

%install
DESTDIR=%{buildroot} cmake --install build
# the Plasma styles (panel popups, launcher, their buttons and lists), from Fedora's default style
for v in soft glass; do
    python3 ng/plasma-style/make-plasma-style.py $v %{buildroot}%{_datadir}/plasma/desktoptheme/breeze-ng-$v
done
install -Dm644 -t %{buildroot}%{_datadir}/color-schemes ng/color-schemes/BreezeNG.colors ng/color-schemes/BreezeNGLight.colors
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
%{_datadir}/color-schemes/BreezeNG.colors
%{_datadir}/color-schemes/BreezeNGLight.colors
%{_datadir}/kstyle/themes/breezeng.themerc
%{_datadir}/plasma/look-and-feel/org.morzilla.breezeng.soft
%{_datadir}/plasma/look-and-feel/org.morzilla.breezeng.glass
%{_datadir}/plasma/look-and-feel/org.morzilla.breezeng.soft.light
%{_datadir}/plasma/look-and-feel/org.morzilla.breezeng.glass.light
%{_datadir}/plasma/desktoptheme/breeze-ng-soft
%{_datadir}/plasma/desktoptheme/breeze-ng-glass
%{_qt6_plugindir}/styles/breezeng6.so
%{_qt6_plugindir}/kstyle_config/breezengstyleconfig.so
%{_qt6_plugindir}/org.kde.kdecoration3/org.kde.breezeng.so
%{_qt6_plugindir}/org.kde.kdecoration3.kcm/kcm_breezengdecoration.so

%changelog
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
