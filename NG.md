# Breeze NG

<img src="ng/logo.png" width="96" align="right" alt="Breeze NG logo">

A fork of KDE's [Breeze](https://invent.kde.org/plasma/breeze) widget style and window decoration with two looks
that go further than the original: **Soft** and **Glass**. It installs next to the stock Breeze, under its own
names, and leaves it untouched.

**Try it with the graphical installer: [what it does, step by step](#everything-at-once).** Pick the parts of the NG
desktop (Breeze NG, [Dolphin NG](https://github.com/Morzilla/dolphin-ng), the NeoLine icons, Dialog NG), pick a look,
and watch it build. Everything goes into your home folder, next to the stock applications, and nothing is replaced:

```sh
bash <(curl -fsSL https://raw.githubusercontent.com/Morzilla/breeze-ng/ng/ng/install-gui.sh)
```

| | Soft | Glass |
|---|---|---|
| surfaces | told apart by tone, no outlines at rest | translucent sheets the compositor blurs behind |
| controls | 9 px radii, a tone that fades in on hover and press | the same, with a faint edge and a lit top hairline |
| selection | the accent colour in content views, a tone in side panels | a pale sheet of light |
| windows | opaque, 14 px corners, title on the left, disc buttons | 70 % opaque with blur, same frame |

Both looks share: no separator lines anywhere, menus with padded rounded panels and a solid accent block on the
current item, tabs that are one sheet with their panel, a neon glow inside fields and buttons that have the focus,
and a menu opened from a button that joins it into one shape.

![Dolphin NG and, on the right, a demo window showing the controls in Breeze NG Soft (not a settings window)](ng/screenshots/01-desktop.png)

![Dolphin NG in Breeze NG, its sidebar showing the compositor's blur of the desktop](ng/screenshots/05-dolphin-live-blur.png)

## Part of the NG desktop

- [Dolphin NG](https://github.com/Morzilla/dolphin-ng) – the file manager with cards and drawn rows, groups and filters one click away, quick actions and work in progress shown on the files ([KDE Store](https://store.kde.org/p/2381561/))
- [NeoLine](https://github.com/Morzilla/neoline-icons) – the outline icon theme in the screenshots ([KDE Store](https://store.kde.org/p/2381562/))
- [Dialog NG](https://github.com/Morzilla/dialog-ng) – the Open/Save window of every KDE app in Dolphin NG's look: drawn rows, a quick preview on hover, filters with their counts, the glass sidebar
- Breeze NG on the KDE Store: https://store.kde.org/p/2381557/

## Parts

- `kstyle/` – the widget style, plugin key `BreezeNG`
- `kdecoration/` – the KWin decoration `org.kde.breezeng`, themes `Breeze NG Soft` and `Breeze NG Glass`
- `ng/breeze-ng` – build, install and switch script
- `ng/look-and-feel/` – the two global themes (System Settings › Global Theme)
- `ng/color-schemes/BreezeNG.colors` – the colour scheme both looks were designed on

## Settings

System Settings › Appearance › Application Style › Breeze NG › Configure (or `./ng/breeze-ng settings`) opens the
settings of the style, in pages:

- **Look** – Soft or Glass; choosing one also applies the Breeze NG window decoration and the BreezeNG colour
  scheme (each can be left out).
- **Transparency** – how see-through windows (Glass) and menus are, with a live preview.
- **Controls and animations** – animation speed, keyboard shortcut underlines, scroll bar arrows, centred tabs, the
  glow inside the focused field or button (off: a thin outline), menus joining the button they open from.
- **Windows** – where windows can be dragged from, the shadow of menus and tooltips, frames and tree lines.
- **KWin effects** – whether the two external effects the screenshots rely on (Better Blur DX for the blur, KDE
  Rounded Corners for the window corners) are installed and on, with links to them.

Everything is stored in `breezengrc`. The variant follows the decoration theme chosen in KWin (`Breeze NG Soft` or
`Breeze NG Glass`), which is what the global themes and the Look page set; `[Common] Variant=Soft|Glass` is the
fallback.

## Everything at once

`ng/install-desktop.sh` installs the whole desktop this style belongs to: [Dolphin NG](https://github.com/Morzilla/dolphin-ng),
Breeze NG, the [NeoLine icons](https://github.com/Morzilla/neoline-icons) and the [NG file dialog](https://github.com/Morzilla/dialog-ng), dependencies included. It asks what to
install and whether to switch the desktop to it; `--yes` runs it without questions, `--help` lists the options,
`--licence` prints the notices (the installer is MIT; Breeze NG and Dolphin NG stay GPL).

```
bash <(curl -fsSL https://raw.githubusercontent.com/Morzilla/breeze-ng/ng/ng/install-gui.sh)      # graphical
bash <(curl -fsSL https://raw.githubusercontent.com/Morzilla/breeze-ng/ng/ng/install-desktop.sh)  # in the terminal
```

The graphical installer is a small wizard: pick the parts, pick a look (Classic, Soft or Glass), watch it build. It
needs Python with PyQt6 or PySide6 and offers to install them; without them it falls back to the terminal one.

For every user of the computer instead of only yourself, there is a third script. It builds as you and copies into
the system with sudo: the style and Dolphin NG under `/opt/ng`, icons, colour scheme and global themes under
`/usr/local/share`, and one file in `/etc/xdg/plasma-workspace/env` that tells every Plasma session where the style
is. Nothing of the distribution is overwritten, and `--uninstall` removes it all.

```sh
bash <(curl -fsSL https://raw.githubusercontent.com/Morzilla/breeze-ng/ng/ng/install-system.sh)
```

Both can also install the two KWin effects the screenshots rely on, [Better Blur DX](https://github.com/xarblu/kwin-effects-better-blur-dx)
and [KDE Rounded Corners](https://github.com/matinlotfali/KDE-Rounded-Corners). That part is off unless you tick it:
they are external projects, not part of this one. The installer only builds them from their own repositories, the way
their READMEs describe, and installs them system-wide (the one step outside your home folder). For anything about
them, refer to their repositories.

![The graphical installer: choosing the look and the icons](ng/screenshots/06-installer.png)

## Build and install

Build dependencies are those of Breeze itself (`sudo dnf builddep breeze`, `sudo apt build-dep breeze`,
`sudo pacman -S --needed base-devel extra-cmake-modules kdecoration kwindowsystem …`). Then:

```
./ng/breeze-ng build        # cmake + install into ~/.local/breeze-ng, plus the global themes
./ng/breeze-ng env          # QT_PLUGIN_PATH for the session: log out and in once
./ng/breeze-ng use glass    # or: use soft — style, decoration and colours, applied at once
./ng/breeze-ng off          # back to Breeze
```

`./ng/breeze-ng now` applies the plugin path to the running session without logging out (X11: it restarts KWin
and Plasma). Applications already open keep their style until restarted. Choosing a global theme from System
Settings also works: pick *Breeze NG Soft* or *Breeze NG Glass*.

## Limits

Only Qt Widgets applications take the full look (Dolphin, Kate, Konsole, Okular, Gwenview…). QML applications
such as System Settings or Discover draw their pages with Plasma's own QML theme and take only the controls from
a widget style. Applications that bundle their own Qt (Telegram) or are not Qt at all cannot use it.

`BREEZE_NG_DEBUG=1` prints a trace of the button-to-menu joining logic.

Breeze NG is GPL-2.0-or-later, like Breeze. The Soft and Glass work is by Morzilla; the mockups it follows are
in the repository history of the sibling project [dolphin-ng](https://github.com/Morzilla/dolphin-ng).
