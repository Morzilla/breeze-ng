#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Morzilla
#
# The NG desktop for every user of this computer: Breeze NG, Dolphin NG and the NeoLine icons, system-wide.
#
#   ng/install-system.sh                  from a clone of breeze-ng
#   bash <(curl -fsSL https://raw.githubusercontent.com/Morzilla/breeze-ng/ng/ng/install-system.sh)
#
# Everything is built as you, from source; only the copying into the system runs as root (sudo).
# Nothing of your distribution is overwritten: its Dolphin and its Breeze stay as they are.
#
#   /opt/ng/breeze-ng                               the widget style and the window decoration
#   /opt/ng/dolphin-ng                              the file manager
#   /usr/local/bin/dolphin-ng                       its command
#   /usr/local/share/applications/dolphin-ng.desktop    its menu entry ("Dolphin NG")
#   /usr/local/share/icons/NeoLine*                 the three icon themes
#   /usr/local/share/color-schemes/BreezeNG.colors  the colour scheme
#   /usr/local/share/plasma/look-and-feel/org.morzilla.breezeng.*    the global themes "Breeze NG Soft / Glass"
#   /etc/xdg/plasma-workspace/env/ng-desktop.sh     tells every Plasma session where the style and decoration are
#
# The distribution's Dolphin stays the default file manager unless you pass --default (or pick "Dolphin NG" in
# System Settings > Default Applications > File manager).
#
# It does not switch anybody's desktop: each user picks "Breeze NG Soft" or "Glass" in System Settings > Global
# Theme (and NeoLine in Icons) after logging out and in once.
#
# Options:
#   --default        make Dolphin NG the file manager that opens folders: for you now, and for the users of this
#                    computer who have not chosen one (the distribution's Dolphin stays installed, as "Dolphin")
#   --only LIST      parts to install, comma separated: breeze,dolphin,icons (default: all three)
#   --no-deps        do not install the build dependencies
#   --src DIR        keep the sources in DIR instead of ~/.local/src
#   --jobs N         parallel compile jobs (default: half of the processors)
#   --yes            do not ask before starting
#   --stage DIR      install into DIR instead of / (no root: for packaging, or to see what would be installed)
#   --uninstall      remove everything listed above
set -euo pipefail

SRC="$HOME/.local/src" DEPS=1 ONLY="breeze,dolphin,icons" YES=0 STAGE="" UNINSTALL=0 DEFAULT=0
JOBS="${JOBS:-$(( ($(nproc 2>/dev/null || echo 4) + 1) / 2 ))}"
while [ $# -gt 0 ]; do
    case "$1" in
        --only) ONLY="$2"; shift ;;
        --no-deps) DEPS=0 ;;
        --default) DEFAULT=1 ;;
        --src) SRC="$2"; shift ;;
        --jobs) JOBS="$2"; shift ;;
        --yes|-y) YES=1 ;;
        --stage) STAGE="$(mkdir -p "$2" && cd "$2" && pwd)"; shift ;;
        --uninstall) UNINSTALL=1 ;;
        -h|--help) sed -n '5,37p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done

if [ -t 1 ]; then B=$'\e[1m' D=$'\e[2m' R=$'\e[0m' CY=$'\e[36m' GR=$'\e[32m' YE=$'\e[33m' RE=$'\e[31m'; else B="" D="" R="" CY="" GR="" YE="" RE=""; fi
say() { printf '\n%s==>%s %s%s%s\n' "$CY" "$R" "$B" "$*" "$R"; }
ok() { printf '  %s✓%s %s\n' "$GR" "$R" "$*"; }
warn() { printf '  %s!%s %s\n' "$YE" "$R" "$*"; }
die() { printf '%s✗ %s%s\n' "$RE" "$*" "$R" >&2; exit 1; }
want() { case ",$ONLY," in *",$1,"*) return 0 ;; *) return 1 ;; esac; }

# root for the copying; none when staging or when already root
SUDO="sudo"; { [ -n "$STAGE" ] || [ "$(id -u)" = 0 ]; } && SUDO=""
as_root() { if [ -n "$SUDO" ]; then sudo "$@"; else "$@"; fi; }
put() { as_root install -D -m "${3:-644}" "$1" "$STAGE$2"; }      # file -> absolute system path
put_tree() { as_root mkdir -p "$STAGE$2" && as_root cp -r "$1" "$STAGE$2/"; }

OPT=/opt/ng
LOCAL=/usr/local
ENVFILE=/etc/xdg/plasma-workspace/env/ng-desktop.sh
FILES=("$LOCAL/bin/dolphin-ng" "$LOCAL/share/applications/dolphin-ng.desktop" "$LOCAL/share/color-schemes/BreezeNG.colors" "$LOCAL/share/kstyle/themes/breezeng.themerc" "$ENVFILE")
TREES=("$OPT" "$LOCAL/share/icons/NeoLine" "$LOCAL/share/icons/NeoLine-White" "$LOCAL/share/icons/NeoLine-Yellow"
       "$LOCAL/share/plasma/look-and-feel/org.morzilla.breezeng.soft" "$LOCAL/share/plasma/look-and-feel/org.morzilla.breezeng.glass")

if [ "$UNINSTALL" = 1 ]; then
    say "Removing the NG desktop from ${STAGE:-the system}"
    for f in "${FILES[@]}"; do as_root rm -f "$STAGE$f"; done
    # the default file manager, if it was set to Dolphin NG system-wide: only that line is taken out
    if [ -f "$STAGE$LOCAL/share/applications/mimeapps.list" ]; then
        as_root sed -i '/^inode\/directory=dolphin-ng.desktop;$/d' "$STAGE$LOCAL/share/applications/mimeapps.list"
    fi
    for t in "${TREES[@]}"; do as_root rm -rf "$STAGE$t"; done
    ok "removed. Users who had switched to Breeze NG go back to their previous theme in System Settings."
    exit 0
fi

printf '\n%sNG desktop, system-wide%s  %sDolphin NG · Breeze NG · NeoLine%s\n' "$B" "$R" "$D" "$R"
printf '  parts: %s   into: %s   compile jobs: %s\n' "$ONLY" "${STAGE:-/ (asks for sudo when it copies)}" "$JOBS"
printf '  %sNothing of the distribution is replaced; --uninstall removes it all.%s\n' "$D" "$R"
if [ "$YES" = 0 ] && [ -t 0 ]; then
    printf '%sGo?%s [Y/n] ' "$B" "$R"; read -r answer
    case "${answer:-y}" in y*|Y*|s*|S*) ;; *) echo "Nothing was changed."; exit 0 ;; esac
fi
command -v git >/dev/null || die "git is needed to download the sources (install it, or run without --no-deps on a system with sudo)."

fetch() { # name branch
    local dir="$SRC/$1"
    if [ -d "$dir/.git" ]; then
        git -C "$dir" pull --ff-only --quiet && ok "$1 updated"
    else
        mkdir -p "$SRC"
        git clone --quiet --depth 1 --branch "$2" "https://github.com/Morzilla/$1.git" "$dir" && ok "$1 downloaded into $dir"
    fi
}
# this script may be running from a clone of breeze-ng: use it instead of downloading another
HERE="$(cd "$(dirname "${BASH_SOURCE[0]:-/nonexistent}")" 2>/dev/null && pwd || true)"
BREEZE="$SRC/breeze-ng"
if [ -n "$HERE" ] && [ -f "$HERE/breeze-ng" ]; then BREEZE="$(cd "$HERE/.." && pwd)"; else fetch breeze-ng ng; fi

# ---- 1. build dependencies: the same as the per-user installer
if [ "$DEPS" = 1 ] && [ -z "$STAGE" ]; then
    say "Build dependencies (sudo)"
    sudo env "XDG_SESSION_TYPE=${XDG_SESSION_TYPE:-}" bash "$BREEZE/ng/install-desktop.sh" --deps-only --only "${ONLY//effects/}" | sed -n '/==>/,$p'
fi

# extra CMake options, space separated, for unusual setups (development packages unpacked by hand, for one):
#   NG_CMAKE_ARGS="-DCMAKE_PREFIX_PATH=/that/usr -DQt6Svg_DIR=/that/usr/lib64/cmake/Qt6Svg"
EXTRA=()
read -ra EXTRA <<<"${NG_CMAKE_ARGS:-}"

DONE=()
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# ---- 2. Breeze NG
if want breeze; then
    say "Breeze NG"
    cmake -S "$BREEZE" -B "$BREEZE/build-system" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$OPT/breeze-ng" \
        -DBUILD_QT5=OFF -DBUILD_QT6=ON -DWITH_WALLPAPERS=OFF -DBUILD_CURSOR=OFF -DBUILD_TESTING=OFF "${EXTRA[@]}" >"$TMP/breeze.log" 2>&1 \
        || { tail -n 20 "$TMP/breeze.log"; die "Breeze NG did not configure (are the build dependencies installed?)"; }
    nice -n 10 cmake --build "$BREEZE/build-system" -j"$JOBS" >>"$TMP/breeze.log" 2>&1 || { tail -n 30 "$TMP/breeze.log"; die "Breeze NG did not build"; }
    as_root env DESTDIR="$STAGE" cmake --install "$BREEZE/build-system" >/dev/null
    put "$BREEZE/ng/color-schemes/BreezeNG.colors" "$LOCAL/share/color-schemes/BreezeNG.colors"
    put "$BREEZE/kstyle/breezeng.themerc" "$LOCAL/share/kstyle/themes/breezeng.themerc"   # the style's name and Configure button in System Settings
    for v in soft glass; do put_tree "$BREEZE/ng/look-and-feel/org.morzilla.breezeng.$v" "$LOCAL/share/plasma/look-and-feel"; done
    # the plugin folder differs between distributions (lib64, lib, lib/x86_64-linux-gnu): find it
    style="$(find "$STAGE$OPT/breeze-ng" -name 'breezeng6.so' -path '*/styles/*' | head -n 1)"
    [ -n "$style" ] || die "the style plugin was not installed where expected under $OPT/breeze-ng"
    plugins="$(dirname "$(dirname "$style")")"; plugins="${plugins#"$STAGE"}"
    cat > "$TMP/ng-desktop.sh" <<EOT
# NG desktop (system-wide): the Breeze NG widget style and window decoration live under $OPT/breeze-ng.
# Installed by install-system.sh; remove with: install-system.sh --uninstall
export QT_PLUGIN_PATH="$plugins\${QT_PLUGIN_PATH:+:\$QT_PLUGIN_PATH}"
EOT
    put "$TMP/ng-desktop.sh" "$ENVFILE"
    ok "installed in $OPT/breeze-ng; every Plasma session gets it from $ENVFILE"
    DONE+=("Breeze NG    each user: log out and in, then System Settings > Global Theme > Breeze NG Soft / Glass")
fi

# ---- 3. Dolphin NG
if want dolphin; then
    say "Dolphin NG (this takes a while)"
    fetch dolphin-ng ng
    DOLPHIN="$SRC/dolphin-ng"
    cmake -S "$DOLPHIN" -B "$DOLPHIN/build-system" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX="$OPT/dolphin-ng" \
        -DBUILD_TESTING=OFF -DKDE_INSTALL_USE_QT_SYS_PATHS=OFF "${EXTRA[@]}" >"$TMP/dolphin.log" 2>&1 \
        || { tail -n 20 "$TMP/dolphin.log"; die "Dolphin NG did not configure (are the build dependencies installed?)"; }
    nice -n 10 cmake --build "$DOLPHIN/build-system" -j"$JOBS" >>"$TMP/dolphin.log" 2>&1 || { tail -n 30 "$TMP/dolphin.log"; die "Dolphin NG did not build"; }
    as_root env DESTDIR="$STAGE" cmake --install "$DOLPHIN/build-system" >/dev/null
    # the launcher of the fork (it sets the paths of the private prefix), then the command and the menu entry
    put "$DOLPHIN/ng/dolphin-ng" "$OPT/dolphin-ng/bin/dolphin-ng-launch" 755
    printf '#!/bin/sh\nDOLPHIN_NG_PREFIX=%s exec %s/bin/dolphin-ng-launch "$@"\n' "$OPT/dolphin-ng" "$OPT/dolphin-ng" > "$TMP/dolphin-ng"
    put "$TMP/dolphin-ng" "$LOCAL/bin/dolphin-ng" 755
    cat > "$TMP/dolphin-ng.desktop" <<ENTRY
[Desktop Entry]
Type=Application
Name=Dolphin NG
GenericName=File Manager
Comment=A fork of Dolphin with a cards view, quick actions and a glass sidebar
Exec=$LOCAL/bin/dolphin-ng %u
Icon=system-file-manager
Terminal=false
Categories=Qt;KDE;System;FileTools;FileManager;
MimeType=inode/directory;
StartupWMClass=dolphin
ENTRY
    put "$TMP/dolphin-ng.desktop" "$LOCAL/share/applications/dolphin-ng.desktop"
    ok "installed in $OPT/dolphin-ng; command: dolphin-ng; menu entry: Dolphin NG"
    if [ "$DEFAULT" = 1 ]; then
        # for everybody who has not chosen a file manager: the system-wide list of default applications
        list="$LOCAL/share/applications/mimeapps.list"
        { [ -f "$STAGE$list" ] && grep -v '^inode/directory=' "$STAGE$list" || true; } > "$TMP/mimeapps.list"
        grep -q '^\[Default Applications\]' "$TMP/mimeapps.list" || printf '[Default Applications]\n' >> "$TMP/mimeapps.list"
        sed -i '/^\[Default Applications\]/a inode/directory=dolphin-ng.desktop;' "$TMP/mimeapps.list"
        put "$TMP/mimeapps.list" "$list"
        # and for you, whose own choice (if any) comes first
        if [ -z "$STAGE" ] && [ "$(id -u)" != 0 ] && command -v xdg-mime >/dev/null; then
            xdg-mime default dolphin-ng.desktop inode/directory || true
        fi
        ok "Dolphin NG opens folders now (undo: System Settings > Default Applications > File manager)"
        DONE+=("Dolphin NG   is the file manager now; the distribution's Dolphin is still in the menu, as Dolphin")
    else
        DONE+=("Dolphin NG   from the menu, or: dolphin-ng. To make it the one that opens folders: run again with --default,")
        DONE+=("             or System Settings > Default Applications > File manager")
    fi
fi

# ---- 4. NeoLine
if want icons; then
    say "NeoLine icons"
    fetch neoline-icons master
    # the generator also installs into the icons folder of the user who runs it: point it at a scratch home
    cp -r "$SRC/neoline-icons" "$TMP/neoline"
    (cd "$TMP/neoline" && HOME="$TMP/home" python3 make-icons.py >/dev/null)
    for theme in NeoLine NeoLine-White NeoLine-Yellow; do
        as_root rm -rf "$STAGE$LOCAL/share/icons/$theme"
        put_tree "$TMP/neoline/$theme" "$LOCAL/share/icons"
    done
    ok "installed in $LOCAL/share/icons"
    DONE+=("NeoLine      each user: System Settings > Icons > NeoLine, NeoLine White or NeoLine Yellow")
fi

printf '\n%s%sDone.%s\n' "$GR" "$B" "$R"
for line in "${DONE[@]}"; do printf '  %s\n' "$line"; done
[ -n "$STAGE" ] && printf '\n  %sStaged under %s: nothing was installed in the system.%s\n' "$D" "$STAGE" "$R"
printf '\n  %sTo remove it all: %s --uninstall%s\n\n' "$D" "$(basename "${BASH_SOURCE[0]:-install-system.sh}")" "$R"
