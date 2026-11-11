#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Morzilla
#
# The NG desktop installer: Dolphin NG, the Breeze NG style and window decoration, the NeoLine icons.
#
#   ng/install-desktop.sh                 from a clone of breeze-ng
#   bash <(curl -fsSL https://raw.githubusercontent.com/Morzilla/breeze-ng/ng/ng/install-desktop.sh)
#
# Run without options it asks what to install (everything is preselected) and whether to switch the desktop
# to it. Nothing of your distribution is replaced: its Dolphin and its Breeze stay as they are.
#
# Options (any of them skips the matching question):
#   --yes                  no questions: install every part, do not switch the desktop
#   --only LIST            parts to install, comma separated: dolphin,breeze,icons,effects
#                          (effects = Better Blur and KDE Rounded Corners, two external projects: see --licence)
#   --apply [soft|glass]   switch the desktop to Breeze NG (default soft) and to the NeoLine icons
#   --icons NAME           icon theme used when applying: NeoLine, NeoLine-White (default), NeoLine-Yellow
#   --no-deps              do not install build dependencies (no sudo)
#   --src DIR              keep the sources in DIR instead of ~/.local/src
#   --jobs N               parallel compile jobs (default: half of the processors)
#   --preset NAME          Dolphin NG settings preset: classic, soft or glass
#   --deps-only            only install the build dependencies (this is what the graphical installer runs as root)
#   --licence              print the licence and the notices, then exit
#
# This script is MIT licensed (text below, in notices()). The software it builds has its own licences.
set -euo pipefail

SRC="$HOME/.local/src" DEPS=1 APPLY="" APPLY_SET=0 ICONS="NeoLine-White" ONLY="" YES=0 PRESET="" DEPS_ONLY=0
# as root (pkexec from the graphical installer) there is nothing to elevate
SUDO="sudo"; [ "$(id -u)" = 0 ] && SUDO=""
JOBS="${JOBS:-$(( ($(nproc 2>/dev/null || echo 4) + 1) / 2 ))}"

# ---- colours, only on a terminal
if [ -t 1 ]; then
    B=$'\e[1m' D=$'\e[2m' R=$'\e[0m' CY=$'\e[36m' GR=$'\e[32m' YE=$'\e[33m' RE=$'\e[31m' MA=$'\e[35m'
else
    B="" D="" R="" CY="" GR="" YE="" RE="" MA=""
fi
say() { printf '\n%s==>%s %s%s%s\n' "$CY" "$R" "$B" "$*" "$R"; }
ok() { printf '  %s✓%s %s\n' "$GR" "$R" "$*"; }
warn() { printf '  %s!%s %s\n' "$YE" "$R" "$*"; }
die() { printf '%s✗ %s%s\n' "$RE" "$*" "$R" >&2; exit 1; }

notices() {
    cat <<'END'
NOTICES

  This installer                MIT License, Copyright (c) 2026 Morzilla
  NeoLine icons                 MIT License, Copyright (c) 2026 Morzilla
  Dolphin NG                    GPL-2.0-or-later: a fork of Dolphin, Copyright (c) the KDE community
  Breeze NG                     GPL-2.0-or-later: a fork of Breeze, Copyright (c) the KDE community

  Dolphin NG and Breeze NG are derivative works of KDE software. They stay under the GNU General Public
  License and cannot be relicensed: their sources, with the full licence texts, are in the repositories this
  script downloads. They are independent forks, not affiliated with or endorsed by KDE e.V.; "KDE", "Plasma",
  "Dolphin" and "Breeze" are names and trademarks of their respective owners.

  The quick actions of Dolphin NG call programs that must be installed separately and have their own licences
  (ImageMagick, FFmpeg, Poppler, Ark, bsdtar). Package repositories of some distributions ship FFmpeg builds
  without patented codecs.

  EXTERNAL PROJECTS (the optional part "effects")

  Better Blur                   https://github.com/taj-ny/kwin-effects-forceblur         GPL-3.0
  KDE Rounded Corners           https://github.com/matinlotfali/KDE-Rounded-Corners      GPL-3.0

  These two KWin effects give the look of the screenshots (blur behind translucent windows, rounded window
  corners). They are NOT part of this project and are not written or maintained by its authors: this script
  only downloads them from their own repositories and builds them the way their READMEs describe. They are
  KWin plugins, so unlike everything else here they are installed system-wide (under /usr, as root), they
  must match your KWin version and they need rebuilding after a KWin update. For anything about them - a
  build that fails, a bug, a newer Plasma - refer to their repositories, not to this one.

  What this script does to your system: it installs build dependencies with your package manager (that step
  asks for sudo and is the only one that runs as root), downloads sources into ~/.local/src and installs into
  ~/.local. With "apply" it changes the widget style, window decoration, colours and icons of your session.
  Only the optional "effects" part installs outside your home folder (see above).

MIT License

  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
  associated documentation files (the "Software"), to deal in the Software without restriction, including
  without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the
  following conditions:

  The above copyright notice and this permission notice shall be included in all copies or substantial
  portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
  LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
  EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
  THE USE OR OTHER DEALINGS IN THE SOFTWARE.

CREDITS

  Morzilla                      idea, direction, design decisions and every "no, not like that"
  Claude (Anthropic)            code, drawings and this installer, written with Claude Code
  The KDE community             Dolphin, Breeze, KWin, Plasma and the frameworks all of this stands on
END
}

while [ $# -gt 0 ]; do
    case "$1" in
        --yes|-y) YES=1 ;;
        --apply) APPLY=soft; APPLY_SET=1; case "${2:-}" in soft|glass) APPLY="$2"; shift ;; esac ;;
        --icons) ICONS="$2"; shift ;;
        --no-deps) DEPS=0 ;;
        --deps-only) DEPS_ONLY=1; YES=1 ;;
        --preset) PRESET="$2"; shift ;;
        --only) ONLY="$2"; shift ;;
        --src) SRC="$2"; shift ;;
        --jobs) JOBS="$2"; shift ;;
        --licence|--license) notices; exit 0 ;;
        -h|--help) sed -n '5,24p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) die "unknown option: $1 (see --help)" ;;
    esac
    shift
done
export JOBS

# ---- the parts
KEYS=(dolphin breeze icons effects)
NAMES=("Dolphin NG" "Breeze NG" "NeoLine icons" "Blur + corners")
NOTES=("the file manager: cards view, two-line list, glass sidebar, previews" "widget style and window decoration, Soft and Glass" "outline icon theme: colour, white, yellow"
       "Better Blur and KDE Rounded Corners: EXTERNAL projects, installed system-wide (sudo)")
PICK=(1 1 1 0) # the external effects are opt-in
if [ -n "$ONLY" ]; then
    PICK=(0 0 0 0)
    IFS=',' read -ra wanted <<<"$ONLY"
    for w in "${wanted[@]}"; do
        found=0
        for i in "${!KEYS[@]}"; do [ "${KEYS[$i]}" = "$w" ] && PICK[$i]=1 && found=1; done
        [ "$found" = 1 ] || die "unknown part: $w (dolphin, breeze, icons, effects)"
    done
fi

# questions are read from the terminal, also when the script itself comes from a pipe
TTY=""
if [ "$YES" = 0 ] && { exec 3</dev/tty; } 2>/dev/null; then TTY=1; fi

banner() {
    printf '\n%s' "$MA"
    cat <<'END'
   _  _  ___     _        _   _
  | \| |/ __|   __| |___ __| |_| |_ ___ _ __
  | .` | (_ |  / _` / -_|_-< / /  _/ _ \ '_ \
  |_|\_|\___|  \__,_\___/__/_\_\\__\___/ .__/
                                       |_|
END
    printf '%s  %sDolphin NG · Breeze NG · NeoLine%s\n' "$R" "$B" "$R"
    printf '  %sby Morzilla, built with Claude (Anthropic) on top of KDE software%s\n' "$D" "$R"
}

# a checklist: arrows or j/k move, space toggles, a = all, n = none, enter confirms
checklist() {
    local cur=0 key rest n=${#KEYS[@]} i
    printf '\n%sWhat do you want to install?%s  %s↑/↓ move · space toggle · a all · n none · enter confirm%s\n' "$B" "$R" "$D" "$R"
    while :; do
        for i in "${!KEYS[@]}"; do
            local mark="[ ]" pointer="  "
            [ "${PICK[$i]}" = 1 ] && mark="${GR}[x]${R}"
            [ "$i" = "$cur" ] && pointer="${CY}> ${R}"
            printf '\r\e[K%s%s %s%-14s%s %s%s%s\n' "$pointer" "$mark" "$B" "${NAMES[$i]}" "$R" "$D" "${NOTES[$i]}" "$R"
        done
        IFS= read -rsn1 -u 3 key || key=""
        case "$key" in
            $'\e') read -rsn2 -t 0.05 -u 3 rest || rest=""
                   case "$rest" in '[A') cur=$(( (cur + n - 1) % n )) ;; '[B') cur=$(( (cur + 1) % n )) ;; esac ;;
            k) cur=$(( (cur + n - 1) % n )) ;;
            j) cur=$(( (cur + 1) % n )) ;;
            ' ') PICK[$cur]=$(( 1 - PICK[$cur] )) ;;
            a) PICK=(1 1 1 1) ;;
            n) PICK=(0 0 0 0) ;;
            '') break ;;
        esac
        printf '\e[%dA' "$n"
    done
}

ask() { # question default(y/n)
    local answer hint="[Y/n]"
    [ "$2" = n ] && hint="[y/N]"
    printf '%s%s%s %s ' "$B" "$1" "$R" "$hint"
    IFS= read -r -u 3 answer || answer=""
    answer="${answer:-$2}"
    case "$answer" in y*|Y*|s*|S*) return 0 ;; *) return 1 ;; esac
}

choose() { # prompt option...  -> sets CHOICE
    local prompt="$1" answer i
    shift
    printf '%s%s%s\n' "$B" "$prompt" "$R"
    i=1
    for option in "$@"; do printf '  %s%d%s  %s\n' "$CY" "$i" "$R" "$option"; i=$((i + 1)); done
    printf '  choice [1]: '
    IFS= read -r -u 3 answer || answer=""
    case "$answer" in ''|*[!0-9]*) answer=1 ;; esac
    [ "$answer" -ge 1 ] && [ "$answer" -le $# ] || answer=1
    CHOICE=$answer
}

banner
if [ -n "$TTY" ]; then
    printf '\n%sBefore you start%s\n' "$B" "$R"
    printf '  · Everything goes into %s~/.local%s; your distribution'"'"'s Dolphin and Breeze are not touched.\n' "$B" "$R"
    printf '  · Build dependencies are installed with your package manager: that step asks for %ssudo%s.\n' "$B" "$R"
    printf '  · This installer and NeoLine are MIT; Dolphin NG and Breeze NG are GPL forks of KDE software,\n'
    printf '    not affiliated with KDE. No warranty of any kind. Full notices: %s--licence%s\n' "$B" "$R"
    [ -z "$ONLY" ] && checklist
    [ "${PICK[0]}${PICK[1]}${PICK[2]}${PICK[3]}" = 0000 ] && { echo; echo "Nothing selected: nothing to do."; exit 0; }
    if [ "${PICK[3]}" = 1 ]; then
        printf '\n%sBlur + corners%s are two external projects, not part of this one: Better Blur (taj-ny) and\n' "$B" "$R"
        printf 'KDE Rounded Corners (matinlotfali). They are built from their own repositories and installed\n'
        printf 'system-wide with sudo; problems with them belong to their repositories. Details: %s--licence%s\n' "$B" "$R"
    fi
    if [ "$DEPS" = 1 ] && ! ask $'\nInstall the build dependencies now (sudo)?' y; then DEPS=0; fi
    if [ "$APPLY_SET" = 0 ] && { [ "${PICK[1]}" = 1 ] || [ "${PICK[2]}" = 1 ]; }; then
        if ask "Switch the desktop to it when done (style, decoration, colours, icons)?" n; then
            APPLY=soft
            if [ "${PICK[1]}" = 1 ]; then
                choose "Which look of Breeze NG?" "Soft: opaque sheets told apart by tone" "Glass: translucent windows blurred by the compositor"
                [ "$CHOICE" = 2 ] && APPLY=glass
            fi
            if [ "${PICK[2]}" = 1 ]; then
                choose "Which icons?" "NeoLine White: white places, file types in colour" "NeoLine: everything in the colour of its kind" "NeoLine Yellow: yellow places, file types in colour"
                case "$CHOICE" in 2) ICONS=NeoLine ;; 3) ICONS=NeoLine-Yellow ;; *) ICONS=NeoLine-White ;; esac
            fi
        fi
    fi
    printf '\n%sSummary%s\n' "$B" "$R"
    for i in "${!KEYS[@]}"; do [ "${PICK[$i]}" = 1 ] && printf '  %s•%s %s\n' "$GR" "$R" "${NAMES[$i]}"; done
    printf '  dependencies: %s   switch the desktop: %s   compile jobs: %s\n' "$([ "$DEPS" = 1 ] && echo yes || echo no)" "${APPLY:-no}${APPLY:+ + $ICONS}" "$JOBS"
    ask $'\nGo?' y || { echo "Nothing was changed."; exit 0; }
fi
SESSION="${XDG_SESSION_TYPE:-}"; [ -n "$SESSION" ] || { [ -n "${WAYLAND_DISPLAY:-}" ] && SESSION=wayland || SESSION=x11; }
# a command as root: sudo on a terminal, the graphical prompt when there is none (the graphical installer)
as_root() {
    if [ -z "$SUDO" ]; then "$@"
    elif [ ! -t 0 ] && command -v pkexec >/dev/null; then pkexec "$@"
    else $SUDO "$@"; fi
}
want() { local i; for i in "${!KEYS[@]}"; do [ "${KEYS[$i]}" = "$1" ] && [ "${PICK[$i]}" = 1 ] && return 0; done; return 1; }

# ---- 1. dependencies
if [ "$DEPS" = 1 ]; then
    say "Build dependencies (sudo)"
    if command -v dnf >/dev/null; then
        $SUDO dnf install -y git cmake gcc-c++ extra-cmake-modules 'dnf-command(builddep)' python3 \
            poppler-utils qt6-qtmultimedia-devel qt6-qtsvg-devel kf6-frameworkintegration-devel ImageMagick ffmpeg-free
        $SUDO dnf builddep -y dolphin breeze || $SUDO dnf builddep -y dolphin plasma-breeze
        if want effects; then
            # what the two READMEs list for Fedora; the X11 session needs the X11 flavour of the KWin headers
            $SUDO dnf install -y kwin-devel libepoxy-devel wayland-devel libdrm-devel plasma-workspace-devel libplasma-devel kdecoration-devel \
                qt6-qtbase-private-devel kf6-kwindowsystem-devel kf6-kconfigwidgets-devel kf6-kcmutils-devel kf6-ki18n-devel kf6-kio-devel \
                kf6-kcrash-devel kf6-kguiaddons-devel kf6-kglobalaccel-devel kf6-knotifications-devel kf6-kdeclarative-devel || true
            [ "$SESSION" = x11 ] && { $SUDO dnf install -y kwin-x11-devel || true; }
        fi
    elif command -v apt-get >/dev/null; then
        $SUDO apt-get update
        $SUDO apt-get install -y git cmake g++ extra-cmake-modules python3 poppler-utils qt6-multimedia-dev qt6-svg-dev imagemagick ffmpeg
        if ! $SUDO apt-get build-dep -y dolphin breeze; then
            # "apt build-dep" reads the build dependencies from the source packages, and the list of sources is
            # off on a fresh Ubuntu or Debian: switch it on (it only adds the source index) and try again
            warn "the source repositories are off: enabling them (deb-src) to read the build dependencies"
            for list in /etc/apt/sources.list.d/*.sources; do
                [ -f "$list" ] && $SUDO sed -i 's/^Types: deb$/Types: deb deb-src/' "$list"
            done
            if [ -f /etc/apt/sources.list ] && ! grep -q '^deb-src ' /etc/apt/sources.list; then
                $SUDO sh -c "sed -n 's/^deb /deb-src /p' /etc/apt/sources.list >> /etc/apt/sources.list"
            fi
            $SUDO apt-get update
            $SUDO apt-get build-dep -y dolphin breeze || die "apt could not install the build dependencies of dolphin and breeze (apt-get build-dep dolphin breeze)."
        fi
        if want effects; then
            $SUDO apt-get install -y kwin-dev qt6-base-private-dev qt6-base-dev-tools qt6-tools-dev libkf6kcmutils-dev libkf6configwidgets-dev libdrm-dev \
                gettext libkf6crash-dev libkf6globalaccel-dev libkf6kio-dev libkf6service-dev libkf6notifications-dev || true
            [ "$SESSION" = x11 ] && { $SUDO apt-get install -y kwin-x11-dev || true; }
        fi
    elif command -v pacman >/dev/null; then
        # Arch has no "build-dep", but its packages carry their headers: installing Dolphin and Breeze themselves
        # brings every library they are built on. The rest are the tools and the few build-only libraries.
        $SUDO pacman -Sy --needed --noconfirm git cmake base-devel extra-cmake-modules kdoctools qt6-tools python poppler qt6-multimedia qt6-svg imagemagick ffmpeg \
            dolphin breeze \
            kio kxmlgui kparts knewstuff kcmutils baloo baloo-widgets kfilemetadata kuserfeedback phonon-qt6 ktextwidgets kcompletion kcodecs kcrash kdbusaddons \
            kbookmarks knotifications solid kwidgetsaddons kitemviews kjobwidgets kservice ktexteditor kwallet plasma-activities packagekit-qt6 \
            kdecoration kwindowsystem kconfigwidgets kguiaddons ki18n kiconthemes kcoreaddons kcolorscheme frameworkintegration kirigami
        if want effects; then
            $SUDO pacman -S --needed --noconfirm qt6-tools vulkan-headers kwin || true
            [ "$SESSION" = x11 ] && { $SUDO pacman -S --needed --noconfirm kwin-x11 || true; }
        fi
    elif command -v zypper >/dev/null; then
        $SUDO zypper install -y git cmake gcc-c++ extra-cmake-modules python3 poppler-tools qt6-multimedia-devel qt6-svg-devel ImageMagick ffmpeg
        $SUDO zypper source-install --build-deps-only dolphin breeze6-style || true
        want effects && warn "effects: install the KWin development packages listed in the two projects' READMEs if the build fails"
    else
        die "No known package manager (dnf, apt, pacman, zypper): install the build dependencies of Dolphin and Breeze by hand, then use --no-deps."
    fi
    ok "dependencies installed"
fi
[ "$DEPS_ONLY" = 1 ] && exit 0
command -v git >/dev/null || die "git is needed to download the sources."

# ---- 2. sources
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
[ -n "$HERE" ] && [ -f "$HERE/breeze-ng" ] && BREEZE="$(cd "$HERE/.." && pwd)"

# Each part is built on its own: one that fails (a library too old on this distribution, say) is reported at the
# end with its log, and the others are installed all the same.
DONE=()
FAILED=()
LOGS="$SRC/logs"
mkdir -p "$LOGS"
part() { # title, log name, command...: runs it, keeps the full output in a log, shows the end of it on failure
    local title="$1" log="$LOGS/$2.log"
    shift 2
    # in the background, with a sign of life every half minute: a build says nothing for a long while
    "$@" >"$log" 2>&1 &
    local pid=$! waited=0
    while kill -0 "$pid" 2>/dev/null; do
        sleep 1
        waited=$((waited + 1))
        [ $((waited % 30)) -eq 0 ] && printf '  %s… still working on %s (%d min %02d s)%s\n' "$D" "$title" $((waited / 60)) $((waited % 60)) "$R"
    done
    if wait "$pid"; then
        return 0
    fi
    printf '%s' "$RE"; tail -n 12 "$log" | sed 's/^/    /'; printf '%s' "$R"
    warn "$title did not install; the rest goes on. Full log: $log"
    FAILED+=("$title   log: $log")
    return 1
}
if want dolphin; then
    say "Dolphin NG (the longest part: several minutes)"
    if part "Dolphin NG" dolphin-ng bash -c "$(declare -f fetch ok); SRC='$SRC'; GR='' R=''; fetch dolphin-ng ng && bash '$SRC/dolphin-ng/ng/install.sh' --no-deps --no-theme"; then
        ok "Dolphin NG installed"
        DONE+=("Dolphin NG   start it from the menu or with: dolphin-ng   (cards view: Ctrl+4)")
    fi
fi
if want breeze; then
    say "Breeze NG"
    [ "$BREEZE" = "$SRC/breeze-ng" ] && fetch breeze-ng ng
    if part "Breeze NG" breeze-ng bash -c "'$BREEZE/ng/breeze-ng' build && '$BREEZE/ng/breeze-ng' env"; then
        ok "Breeze NG installed"
        DONE+=("Breeze NG    log out and in once, then System Settings > Global Theme > Breeze NG Soft / Glass")
        DONE+=("             or: $BREEZE/ng/breeze-ng use soft|glass   (without logging out: ... now)")
    fi
fi
if want icons; then
    say "NeoLine icons"
    if part "NeoLine icons" neoline bash -c "$(declare -f fetch ok); SRC='$SRC'; GR='' R=''; fetch neoline-icons master && python3 '$SRC/neoline-icons/make-icons.py'"; then
        ok "NeoLine installed"
        DONE+=("NeoLine      System Settings > Icons > NeoLine, NeoLine White or NeoLine Yellow")
    fi
fi

# ---- the two external KWin effects: built the way their own READMEs say, installed system-wide
effect_present() { # file name pattern of the plugin
    # (not a pipeline: find fails on the folders that do not exist, and with pipefail that read as "not installed")
    local found
    found=$(find /usr/lib /usr/lib64 /usr/local/lib /usr/local/lib64 -path '*kwin*/effects/plugins/*' -name "$1" -print -quit 2>/dev/null || true)
    [ -n "$found" ]
}
build_effect() { # title, repository, plugin pattern, kwinrc key, cmake options...
    local title="$1" repo="$2" pattern="$3" key="$4" dir="$SRC/external/$(basename "$2")"
    shift 4
    if effect_present "$pattern"; then
        ok "$title is already installed: left as it is"
    else
        mkdir -p "$SRC/external"
        if [ -d "$dir/.git" ]; then git -C "$dir" pull --ff-only --quiet || true; else git clone --quiet --depth 1 "$repo" "$dir" || { warn "$title: could not download $repo"; return 0; }; fi
        if ! { cmake -S "$dir" -B "$dir/build" "$@" >"$dir/build.log" 2>&1 && cmake --build "$dir/build" -j"$JOBS" >>"$dir/build.log" 2>&1; }; then
            warn "$title did not build (log: $dir/build.log). It is an external project: see $repo"
            return 0
        fi
        say "$title: installing system-wide (asks for your password)"
        as_root cmake --install "$dir/build" || { warn "$title was built but not installed"; return 0; }
        ok "$title installed from $repo"
    fi
    kwriteconfig6 --file kwinrc --group Plugins --key "${key}Enabled" true 2>/dev/null || true
}
if want effects; then
    say "Blur and rounded corners (external projects)"
    printf '  %sBetter Blur and KDE Rounded Corners are not part of this project. Problems with them: their repositories.%s\n' "$D" "$R"
    if [ "$SESSION" = x11 ]; then
        build_effect "Better Blur" https://github.com/taj-ny/kwin-effects-forceblur 'forceblur*.so' forceblur_x11 -DCMAKE_INSTALL_PREFIX=/usr
        build_effect "KDE Rounded Corners" https://github.com/matinlotfali/KDE-Rounded-Corners 'kwin4_effect_shapecorners.so' kwin4_effect_shapecorners -DCMAKE_INSTALL_PREFIX=/usr -DKWIN_X11=ON
    else
        build_effect "Better Blur" https://github.com/taj-ny/kwin-effects-forceblur 'forceblur*.so' forceblur -DCMAKE_INSTALL_PREFIX=/usr
        build_effect "KDE Rounded Corners" https://github.com/matinlotfali/KDE-Rounded-Corners 'kwin4_effect_shapecorners.so' kwin4_effect_shapecorners -DCMAKE_INSTALL_PREFIX=/usr
    fi
    kwriteconfig6 --file kwinrc --group Plugins --key blurEnabled false 2>/dev/null || true # KWin's own blur and Better Blur do not go together
    DONE+=("Effects      log out and in (or restart KWin); settings in System Settings > Window Management > Desktop Effects")
    DONE+=("             they need rebuilding after a KWin update: run this installer again with --only effects")
fi

# ---- a preset for the settings of Dolphin NG (the same three as its settings page)
failed() { local f; for f in "${FAILED[@]:-}"; do case "$f" in "$1"*) return 0 ;; esac; done; return 1; }
if [ -n "$PRESET" ] && want dolphin && ! failed "Dolphin NG"; then
    say "Dolphin NG preset: $PRESET"
    key() { if [ -n "$2" ]; then kwriteconfig6 --file dolphinrc --group DolphinNG --key "$1" "$2"; else kwriteconfig6 --file dolphinrc --group DolphinNG --key "$1" --delete; fi; }
    for k in SIDEBAR DEVICES SIDEBAR_FILTERS LIST ICONS TILES GLASS_RIM CARDS_MARK RowStyle; do key "$k" ""; done
    case "$PRESET" in
        classic) key SIDEBAR plain; key DEVICES show; key SIDEBAR_FILTERS hide; key LIST columns; key ICONS show; key TILES off; key RowStyle stripe ;;
        soft) key SIDEBAR plain; key DEVICES show ;;
        glass) key GLASS_RIM diagonal; key CARDS_MARK type ;;
        *) warn "unknown preset: $PRESET (classic, soft, glass)" ;;
    esac
    ok "settings written (change them later in Dolphin NG > Configure > NG)"
fi

if [ -n "$APPLY" ]; then
    say "Switching the desktop"
    if want breeze && ! failed "Breeze NG"; then "$BREEZE/ng/breeze-ng" use "$APPLY" && ok "Breeze NG $APPLY"; fi
    if want icons && ! failed "NeoLine"; then
        if [ -x /usr/libexec/plasma-changeicons ]; then
            /usr/libexec/plasma-changeicons "$ICONS" >/dev/null 2>&1 || true
        else
            kwriteconfig6 --file kdeglobals --group Icons --key Theme "$ICONS"
        fi
        ok "icons: $ICONS"
    fi
    want breeze && warn "the window decoration and running applications pick the style up after you log out and in"
fi

if [ "${#FAILED[@]}" -gt 0 ]; then
    printf '\n%s%sDone, but not everything installed.%s\n' "$YE" "$B" "$R"
else
    printf '\n%s%sDone.%s\n' "$GR" "$B" "$R"
fi
for line in "${DONE[@]:-}"; do [ -n "$line" ] && printf '  %s\n' "$line"; done
if [ "${#FAILED[@]}" -gt 0 ]; then
    printf '\n  %sNot installed:%s\n' "$B" "$R"
    for line in "${FAILED[@]}"; do printf '  %s✗%s %s\n' "$RE" "$R" "$line"; done
    printf '  %sPlease report it with the log: https://github.com/Morzilla/breeze-ng/issues%s\n' "$D" "$R"
fi
printf '\n  %sRun this again at any time to update. Licence and credits: --licence%s\n' "$D" "$R"
printf '  %sMade by Morzilla with Claude (Anthropic), on the shoulders of the KDE community.%s\n\n' "$D" "$R"
[ "${#FAILED[@]}" -eq 0 ]
