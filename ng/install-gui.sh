#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Morzilla
#
# Starts the graphical installer of the NG desktop (Dolphin NG, Breeze NG, NeoLine, Dialog NG).
#
#   ng/install-gui.sh              from a clone of breeze-ng
#   bash <(curl -fsSL https://raw.githubusercontent.com/Morzilla/breeze-ng/ng/ng/install-gui.sh)
#
# It needs Python 3 with PyQt6 or PySide6. If they are missing it offers to install them, and if that is not
# possible it falls back to the text installer (install-desktop.sh), which needs nothing but a terminal.
set -euo pipefail
RAW="https://raw.githubusercontent.com/Morzilla/breeze-ng/ng/ng"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]:-/nonexistent}")" 2>/dev/null && pwd || true)"
if [ -n "$HERE" ] && [ -f "$HERE/install-gui.py" ]; then
    DIR="$HERE"
else
    # started from the web: fetch the installer into the cache, fresh every time
    DIR="${XDG_CACHE_HOME:-$HOME/.cache}/ng-installer"
    mkdir -p "$DIR/screenshots"
    for f in install-gui.py install-desktop.sh logo.png screenshots/01-desktop.png screenshots/icons-NeoLine.png screenshots/icons-NeoLine-White.png screenshots/icons-NeoLine-Yellow.png; do
        curl -fsSL "$RAW/$f" -o "$DIR/$f" || { [ "$f" = install-gui.py ] || [ "$f" = install-desktop.sh ]; } && [ -s "$DIR/$f" ] || true
    done
    [ -s "$DIR/install-gui.py" ] && [ -s "$DIR/install-desktop.sh" ] || { echo "Could not download the installer (no network?)." >&2; exit 1; }
fi

has_qt() { python3 -c 'import PyQt6.QtWidgets' 2>/dev/null || python3 -c 'import PySide6.QtWidgets' 2>/dev/null; }
text_installer() { echo "Starting the text installer instead."; exec bash "$DIR/install-desktop.sh" "$@"; }

command -v python3 >/dev/null || text_installer "$@"
if ! has_qt; then
    if command -v dnf >/dev/null; then PKG="python3-pyqt6"; INSTALL="dnf install -y $PKG"
    elif command -v apt-get >/dev/null; then PKG="python3-pyqt6"; INSTALL="apt-get install -y $PKG"
    elif command -v pacman >/dev/null; then PKG="python-pyqt6"; INSTALL="pacman -S --needed --noconfirm $PKG"
    elif command -v zypper >/dev/null; then PKG="python3-PyQt6"; INSTALL="zypper install -y $PKG"
    else PKG=""; fi
    QUESTION="The graphical installer needs the package $PKG (Qt for Python). Install it now?"
    if [ -n "$PKG" ] && command -v kdialog >/dev/null && command -v pkexec >/dev/null && [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
        kdialog --title "NG desktop installer" --yesno "$QUESTION" && pkexec sh -c "$INSTALL" || true
    elif [ -n "$PKG" ] && [ -t 0 ]; then
        printf '%s [Y/n] ' "$QUESTION"; read -r answer
        case "${answer:-y}" in y*|Y*|s*|S*) sudo sh -c "$INSTALL" || true ;; esac
    fi
    has_qt || text_installer "$@"
fi
exec python3 "$DIR/install-gui.py" "$@"
