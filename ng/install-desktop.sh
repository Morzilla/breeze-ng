#!/bin/bash
# The whole NG desktop in one go: Dolphin NG, the Breeze NG style and window decoration, the NeoLine icons.
#
#   ng/install-desktop.sh                 from a clone of breeze-ng
#   curl -fsSL https://raw.githubusercontent.com/Morzilla/breeze-ng/ng/ng/install-desktop.sh | bash
#
# What it does, in order:
#   1. installs the build dependencies of the three parts with your package manager (asks for sudo)
#   2. downloads or updates the sources in ~/.local/src (dolphin-ng, breeze-ng, neoline-icons)
#   3. Dolphin NG: builds it into ~/.local/dolphin-ng, with its menu entry and the dolphin-ng command
#   4. Breeze NG: builds it into ~/.local/breeze-ng, installs the two global themes and the colour scheme,
#      and writes the session file that makes the plugins visible (log out and in once)
#   5. NeoLine: generates the three icon themes into ~/.local/share/icons
# Nothing of your distribution is replaced: its Dolphin and its Breeze stay as they are.
#
# Options:
#   --apply [soft|glass]   also switch the desktop to Breeze NG (default soft) and to the NeoLine White icons
#   --icons NAME           icon theme used by --apply: NeoLine, NeoLine-White (default) or NeoLine-Yellow
#   --no-deps              skip step 1
#   --only PART            only one part: dolphin, breeze or icons
#   --src DIR              keep the sources in DIR instead of ~/.local/src
#   --jobs N               parallel compile jobs (default 4)
set -euo pipefail

SRC="$HOME/.local/src" DEPS=1 APPLY="" ICONS="NeoLine-White" ONLY="" JOBS="${JOBS:-4}"
while [ $# -gt 0 ]; do
    case "$1" in
        --apply) APPLY=soft; case "${2:-}" in soft|glass) APPLY="$2"; shift ;; esac ;;
        --icons) ICONS="$2"; shift ;;
        --no-deps) DEPS=0 ;;
        --only) ONLY="$2"; shift ;;
        --src) SRC="$2"; shift ;;
        --jobs) JOBS="$2"; shift ;;
        -h|--help) sed -n '2,23p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done
export JOBS
say() { printf '\n==> %s\n' "$*"; }
want() { [ -z "$ONLY" ] || [ "$ONLY" = "$1" ]; }

# ---- 1. dependencies
if [ "$DEPS" = 1 ]; then
    say "installing the build dependencies (sudo)"
    if command -v dnf >/dev/null; then
        sudo dnf install -y git cmake gcc-c++ extra-cmake-modules 'dnf-command(builddep)' python3 \
            poppler-utils qt6-qtmultimedia-devel qt6-qtsvg-devel kf6-frameworkintegration-devel ImageMagick ffmpeg-free
        sudo dnf builddep -y dolphin breeze || sudo dnf builddep -y dolphin plasma-breeze
    elif command -v apt-get >/dev/null; then
        sudo apt-get update
        sudo apt-get install -y git cmake g++ extra-cmake-modules python3 poppler-utils qt6-multimedia-dev qt6-svg-dev imagemagick ffmpeg
        sudo apt-get build-dep -y dolphin breeze || {
            echo "apt could not install the build dependencies: enable the source repositories (deb-src) and run this again." >&2
            exit 1
        }
    elif command -v pacman >/dev/null; then
        sudo pacman -S --needed --noconfirm git cmake base-devel extra-cmake-modules python poppler qt6-multimedia qt6-svg imagemagick ffmpeg \
            kio kxmlgui kparts knewstuff kcmutils baloo baloo-widgets kfilemetadata kuserfeedback phonon-qt6 \
            kdecoration kwindowsystem kconfigwidgets kguiaddons ki18n kiconthemes kcoreaddons kcolorscheme frameworkintegration kirigami
    elif command -v zypper >/dev/null; then
        sudo zypper install -y git cmake gcc-c++ extra-cmake-modules python3 poppler-tools qt6-multimedia-devel qt6-svg-devel ImageMagick ffmpeg
        sudo zypper source-install --build-deps-only dolphin breeze6-style || true
    else
        echo "No known package manager (dnf, apt, pacman, zypper): install the build dependencies of Dolphin and Breeze by hand, then use --no-deps." >&2
        exit 1
    fi
fi

# ---- 2. sources
fetch() { # name branch
    local dir="$SRC/$1"
    if [ -d "$dir/.git" ]; then
        say "updating $1"
        git -C "$dir" pull --ff-only
    else
        say "downloading $1"
        mkdir -p "$SRC"
        git clone --depth 1 --branch "$2" "https://github.com/Morzilla/$1.git" "$dir"
    fi
}
# this script may be running from a clone of breeze-ng: use it instead of downloading another
HERE="$(cd "$(dirname "${BASH_SOURCE[0]:-/nonexistent}")" 2>/dev/null && pwd || true)"
if [ -n "$HERE" ] && [ -f "$HERE/breeze-ng" ]; then
    BREEZE="$(cd "$HERE/.." && pwd)"
else
    want breeze && fetch breeze-ng ng
    BREEZE="$SRC/breeze-ng"
fi

# ---- 3. Dolphin NG
if want dolphin; then
    fetch dolphin-ng ng
    say "building Dolphin NG"
    bash "$SRC/dolphin-ng/ng/install.sh" --no-deps --no-theme
fi

# ---- 4. Breeze NG
if want breeze; then
    say "building Breeze NG"
    "$BREEZE/ng/breeze-ng" build
    "$BREEZE/ng/breeze-ng" env
fi

# ---- 5. NeoLine
if want icons; then
    fetch neoline-icons master
    say "generating the NeoLine icons"
    python3 "$SRC/neoline-icons/make-icons.py"
fi

# ---- apply
if [ -n "$APPLY" ]; then
    say "switching the desktop to Breeze NG $APPLY and the $ICONS icons"
    want breeze && "$BREEZE/ng/breeze-ng" use "$APPLY"
    if want icons; then
        if [ -x /usr/libexec/plasma-changeicons ]; then
            /usr/libexec/plasma-changeicons "$ICONS" || true
        else
            kwriteconfig6 --file kdeglobals --group Icons --key Theme "$ICONS"
        fi
    fi
fi

cat <<END

Done.
  Dolphin NG   start it from the menu or with: dolphin-ng     (cards view: Ctrl+4)
  Breeze NG    log out and in once, then: $BREEZE/ng/breeze-ng use soft|glass
               (or System Settings > Global Theme > Breeze NG Soft / Glass); without logging out: breeze-ng now
  NeoLine      System Settings > Icons > NeoLine, NeoLine White or NeoLine Yellow
Run this script again at any time to update the three parts.
END
