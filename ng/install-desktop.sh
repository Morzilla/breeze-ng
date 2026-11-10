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
#   --only LIST            parts to install, comma separated: dolphin,breeze,icons
#   --apply [soft|glass]   switch the desktop to Breeze NG (default soft) and to the NeoLine icons
#   --icons NAME           icon theme used when applying: NeoLine, NeoLine-White (default), NeoLine-Yellow
#   --no-deps              do not install build dependencies (no sudo)
#   --src DIR              keep the sources in DIR instead of ~/.local/src
#   --jobs N               parallel compile jobs (default: half of the processors)
#   --licence              print the licence and the notices, then exit
#
# This script is MIT licensed (text below, in notices()). The software it builds has its own licences.
set -euo pipefail

SRC="$HOME/.local/src" DEPS=1 APPLY="" APPLY_SET=0 ICONS="NeoLine-White" ONLY="" YES=0
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

  What this script does to your system: it installs build dependencies with your package manager (that step
  asks for sudo and is the only one that runs as root), downloads sources into ~/.local/src and installs into
  ~/.local. With "apply" it changes the widget style, window decoration, colours and icons of your session.

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
        --only) ONLY="$2"; shift ;;
        --src) SRC="$2"; shift ;;
        --jobs) JOBS="$2"; shift ;;
        --licence|--license) notices; exit 0 ;;
        -h|--help) sed -n '5,22p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) die "unknown option: $1 (see --help)" ;;
    esac
    shift
done
export JOBS

# ---- the parts
KEYS=(dolphin breeze icons)
NAMES=("Dolphin NG" "Breeze NG" "NeoLine icons")
NOTES=("the file manager: cards view, two-line list, glass sidebar, previews" "widget style and window decoration, Soft and Glass" "outline icon theme: colour, white, yellow")
PICK=(1 1 1)
if [ -n "$ONLY" ]; then
    PICK=(0 0 0)
    IFS=',' read -ra wanted <<<"$ONLY"
    for w in "${wanted[@]}"; do
        found=0
        for i in "${!KEYS[@]}"; do [ "${KEYS[$i]}" = "$w" ] && PICK[$i]=1 && found=1; done
        [ "$found" = 1 ] || die "unknown part: $w (dolphin, breeze, icons)"
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
            a) PICK=(1 1 1) ;;
            n) PICK=(0 0 0) ;;
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
    [ "${PICK[0]}${PICK[1]}${PICK[2]}" = 000 ] && { echo; echo "Nothing selected: nothing to do."; exit 0; }
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
want() { local i; for i in "${!KEYS[@]}"; do [ "${KEYS[$i]}" = "$1" ] && [ "${PICK[$i]}" = 1 ] && return 0; done; return 1; }

# ---- 1. dependencies
if [ "$DEPS" = 1 ]; then
    say "Build dependencies (sudo)"
    if command -v dnf >/dev/null; then
        sudo dnf install -y git cmake gcc-c++ extra-cmake-modules 'dnf-command(builddep)' python3 \
            poppler-utils qt6-qtmultimedia-devel qt6-qtsvg-devel kf6-frameworkintegration-devel ImageMagick ffmpeg-free
        sudo dnf builddep -y dolphin breeze || sudo dnf builddep -y dolphin plasma-breeze
    elif command -v apt-get >/dev/null; then
        sudo apt-get update
        sudo apt-get install -y git cmake g++ extra-cmake-modules python3 poppler-utils qt6-multimedia-dev qt6-svg-dev imagemagick ffmpeg
        sudo apt-get build-dep -y dolphin breeze || die "apt could not install the build dependencies: enable the source repositories (deb-src) and run this again."
    elif command -v pacman >/dev/null; then
        sudo pacman -S --needed --noconfirm git cmake base-devel extra-cmake-modules python poppler qt6-multimedia qt6-svg imagemagick ffmpeg \
            kio kxmlgui kparts knewstuff kcmutils baloo baloo-widgets kfilemetadata kuserfeedback phonon-qt6 \
            kdecoration kwindowsystem kconfigwidgets kguiaddons ki18n kiconthemes kcoreaddons kcolorscheme frameworkintegration kirigami
    elif command -v zypper >/dev/null; then
        sudo zypper install -y git cmake gcc-c++ extra-cmake-modules python3 poppler-tools qt6-multimedia-devel qt6-svg-devel ImageMagick ffmpeg
        sudo zypper source-install --build-deps-only dolphin breeze6-style || true
    else
        die "No known package manager (dnf, apt, pacman, zypper): install the build dependencies of Dolphin and Breeze by hand, then use --no-deps."
    fi
    ok "dependencies installed"
fi
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

DONE=()
if want dolphin; then
    say "Dolphin NG"
    fetch dolphin-ng ng
    bash "$SRC/dolphin-ng/ng/install.sh" --no-deps --no-theme
    DONE+=("Dolphin NG   start it from the menu or with: dolphin-ng   (cards view: Ctrl+4)")
fi
if want breeze; then
    say "Breeze NG"
    [ "$BREEZE" = "$SRC/breeze-ng" ] && fetch breeze-ng ng
    "$BREEZE/ng/breeze-ng" build
    "$BREEZE/ng/breeze-ng" env
    DONE+=("Breeze NG    log out and in once, then System Settings > Global Theme > Breeze NG Soft / Glass")
    DONE+=("             or: $BREEZE/ng/breeze-ng use soft|glass   (without logging out: ... now)")
fi
if want icons; then
    say "NeoLine icons"
    fetch neoline-icons master
    python3 "$SRC/neoline-icons/make-icons.py"
    DONE+=("NeoLine      System Settings > Icons > NeoLine, NeoLine White or NeoLine Yellow")
fi

if [ -n "$APPLY" ]; then
    say "Switching the desktop"
    if want breeze; then "$BREEZE/ng/breeze-ng" use "$APPLY" && ok "Breeze NG $APPLY"; fi
    if want icons; then
        if [ -x /usr/libexec/plasma-changeicons ]; then
            /usr/libexec/plasma-changeicons "$ICONS" >/dev/null 2>&1 || true
        else
            kwriteconfig6 --file kdeglobals --group Icons --key Theme "$ICONS"
        fi
        ok "icons: $ICONS"
    fi
    want breeze && warn "the window decoration and running applications pick the style up after you log out and in"
fi

printf '\n%s%sDone.%s\n' "$GR" "$B" "$R"
for line in "${DONE[@]}"; do printf '  %s\n' "$line"; done
printf '\n  %sRun this again at any time to update. Licence and credits: --licence%s\n' "$D" "$R"
printf '  %sMade by Morzilla with Claude (Anthropic), on the shoulders of the KDE community.%s\n\n' "$D" "$R"
