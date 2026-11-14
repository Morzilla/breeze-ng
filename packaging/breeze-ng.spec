# SPDX-License-Identifier: GPL-2.0-or-later
%global commit      399ea7a8bc24e319de3e6852c8aba548880f6d48
%global shortcommit %(c=%{commit}; echo ${c:0:7})
%global snapdate    20261113
# installed apart from the system, next to Fedora's own Breeze
%global ngprefix    /opt/ng/breeze-ng
# its programs find its libraries through an RPATH to /opt/ng/breeze-ng: on purpose, since they have the same names as
# Fedora's Breeze libraries and must not be put on the system's library path
%global __brp_check_rpaths %{nil}

Name:           breeze-ng
Version:        6.7.5^%{snapdate}git%{shortcommit}
Release:        1%{?dist}
Summary:        Breeze widget style and window decoration with two looks, Soft and Glass
License:        GPL-2.0-or-later AND MIT
URL:            https://github.com/Morzilla/breeze-ng
Source0:        %{url}/archive/%{commit}/breeze-ng-%{shortcommit}.tar.gz

BuildRequires:  cmake
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
%{ngprefix}, next to the stock Breeze, which stays untouched; every Plasma
session finds it through a file in /etc/xdg/plasma-workspace/env.

After installing, log out and in once, then pick "Breeze NG Soft" or
"Breeze NG Glass" in System Settings > Global Theme.

%prep
%autosetup -n breeze-ng-%{commit}

%build
%set_build_flags
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=%{ngprefix} \
      -DBUILD_QT5=OFF -DBUILD_QT6=ON -DWITH_WALLPAPERS=OFF -DBUILD_CURSOR=OFF -DBUILD_TESTING=OFF
cmake --build build -j%{?_smp_build_ncpus}

%install
DESTDIR=%{buildroot} cmake --install build
install -Dm644 ng/color-schemes/BreezeNG.colors %{buildroot}%{_datadir}/color-schemes/BreezeNG.colors
install -Dm644 kstyle/breezeng.themerc %{buildroot}%{_datadir}/kstyle/themes/breezeng.themerc
mkdir -p %{buildroot}%{_datadir}/plasma/look-and-feel
cp -a ng/look-and-feel/org.morzilla.breezeng.soft ng/look-and-feel/org.morzilla.breezeng.glass %{buildroot}%{_datadir}/plasma/look-and-feel/
# the plugin folder: lib64 on Fedora
plugins=$(dirname "$(dirname "$(find %{buildroot}%{ngprefix} -name breezeng6.so -path '*/styles/*' | head -n 1)")")
plugins=${plugins#%{buildroot}}
mkdir -p %{buildroot}%{_sysconfdir}/xdg/plasma-workspace/env
cat > %{buildroot}%{_sysconfdir}/xdg/plasma-workspace/env/breeze-ng.sh <<EOT
# Breeze NG: the widget style and window decoration live under %{ngprefix}
export QT_PLUGIN_PATH="$plugins\${QT_PLUGIN_PATH:+:\$QT_PLUGIN_PATH}"
EOT

%files
%license LICENSES/*
%doc NG.md README.md
%{ngprefix}
%dir /opt/ng
%{_datadir}/color-schemes/BreezeNG.colors
%{_datadir}/kstyle/themes/breezeng.themerc
%{_datadir}/plasma/look-and-feel/org.morzilla.breezeng.soft
%{_datadir}/plasma/look-and-feel/org.morzilla.breezeng.glass
%config(noreplace) %{_sysconfdir}/xdg/plasma-workspace/env/breeze-ng.sh

%changelog
* Sat Nov 14 2026 Morzilla <3956663+Morzilla@users.noreply.github.com> - 6.7.5^20261113git399ea7a-1
- First package
