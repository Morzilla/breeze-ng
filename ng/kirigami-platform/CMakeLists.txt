# SPDX-License-Identifier: LGPL-2.0-or-later
# Breeze NG Aurora for Kirigami: a platform plugin that stands in front of the desktop style's own and hands out
# its themes with see-through backgrounds, so that Qt Quick applications are glass from their first frame.
cmake_minimum_required(VERSION 3.16)
project(breeze-ng-kirigami-platform LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
include(GNUInstallDirs)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick)
find_package(KF6KirigamiPlatform REQUIRED)
find_package(KF6Config REQUIRED)
find_package(KF6WindowSystem REQUIRED)
# the same file name as the plugin it stands in for: Kirigami looks for the name of the Qt Quick Controls style
add_library(org.kde.desktop MODULE plugin.cpp)
set_target_properties(org.kde.desktop PROPERTIES PREFIX "")
target_link_libraries(org.kde.desktop PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick KF6::KirigamiPlatform KF6::ConfigCore KF6::WindowSystem ${CMAKE_DL_LIBS})
# its own directory, put in front of the others with QT_PLUGIN_PATH (see ng/kirigami-platform/README)
install(TARGETS org.kde.desktop DESTINATION ${CMAKE_INSTALL_LIBDIR}/breeze-ng-kirigami/kf6/kirigami/platform)
