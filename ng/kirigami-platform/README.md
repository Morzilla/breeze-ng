# Breeze NG Aurora for Kirigami

Qt Quick applications built with Kirigami (System Settings' modules, Info Centre, System Monitor, Discover) ask a
*platform plugin* for their colours. This one stands in front of the desktop style's own plugin, lets it do its
work and, when the Aurora look is on, hands out the themes with see-through backgrounds: the applications are glass
from their first frame, like the windows the widget style paints.

It has the same file name as the plugin it stands in for (`org.kde.desktop.so`), in a directory of its own that
must come first in the plugin path:

    QT_PLUGIN_PATH=<prefix>/lib64/breeze-ng-kirigami:$QT_PLUGIN_PATH

The package sets that for the whole session with a file in `/etc/xdg/plasma-workspace/env/` (it takes effect at the
next login). With another look, or another widget style, the plugin only passes everything through.
`BREEZE_NG_NO_QUICK=1` switches it off for one application.

Build: `cmake -S ng/kirigami-platform -B build-kirigami -DCMAKE_INSTALL_PREFIX=<prefix> && cmake --build build-kirigami && cmake --install build-kirigami`
(needs the development files of Kirigami, KConfig and KWindowSystem).
