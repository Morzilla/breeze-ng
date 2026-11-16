# AGENTS.md

Guidance for coding agents (and people) working on Breeze NG.

## What this is

Breeze NG is a fork of KDE's Breeze (the widget style and the KWin window decoration, Qt 6) with three looks:
**Soft**, **Glass** and **Aurora Glass**, each with a dark and a light colour scheme. It installs next to the stock
Breeze under its own names (style `BreezeNG`, decoration `org.kde.breezeng`) and leaves Breeze untouched. It is part
of the NG desktop with [Dolphin NG](https://github.com/Morzilla/dolphin-ng), Dialog NG and the NeoLine icons, and
this repository also holds the NG desktop's installers. What the user sees is described in `NG.md`.

The development branch is `ng`. Upstream is Breeze from invent.kde.org.

## Layout

```
kstyle/                     the widget style
├── breezestyle.cpp         drawing of every control, polish rules, event filters
├── breezehelper.cpp        colours and shapes: the looks, Aurora's veils, lights and selections, glass layers
└── config/                 the settings window (breezengrc; Look, Transparency, Aurora, Controls, Windows, KWin effects)
kdecoration/                the KWin window decoration (title bar, buttons, Aurora's lights behind the window)
ng/
├── breeze-ng               build, env, use soft|glass|aurora, relogin, status (see its header)
├── kirigami-platform/      Aurora for Qt Quick applications: Kirigami's platform plugin (see its README)
├── install-desktop.sh      the NG desktop installer (into the home folder), install-gui.py its graphical front
├── install-system.sh       the same, system-wide under /opt/ng
├── look-and-feel/          the global themes (one per look and colour scheme)
├── plasma-style/           builds the Plasma styles from Plasma's default one
└── color-schemes/          BreezeNG.colors (dark) and BreezeNGLight.colors
packaging/breeze-ng.spec    the Fedora package (COPR morzilla/desktop-ng)
```

Code changed or added in Breeze's files is marked with a `Breeze NG` comment.

## Build and run

```sh
ng/breeze-ng build          # build and install into ~/.local/breeze-ng, Kirigami plugin included
ng/breeze-ng env            # once: put the plugins in the session's path (next login)
ng/breeze-ng use aurora     # select a look: soft, glass or aurora
ng/breeze-ng relogin        # says whether KWin needs a new login to show the current decoration
```

KWin loads the decoration once per session: a rebuilt decoration shows only after logging out and in, and a running
`plasmashell` keeps the Kirigami plugin it loaded. Applications started after a build get the new style.
`BREEZE_NG_DEBUG_QUICK=1` (or `2`) makes the Kirigami plugin log what it does to a Qt Quick window.

## Conventions

- **Rules, not exceptions.** The style serves every application: fix a look with a rule that holds for all of them
  (a kind of widget, a role, a property), not with a check on an application's name.
- **Keep upstream recognisable.** Mark NG code, do not reformat what you do not change.
- **Comments say why**, in plain English sentences. A guard that looks removable says what it prevents.
- **Colours come from the palette.** The accent is the palette's Highlight; never hard-code a blue. A nearly
  colourless accent (saturation under 0.15) must stay neutral: do not lift its saturation.
- **Check every combination you touch**: Soft, Glass, Aurora; dark and light; widget applications and Qt Quick ones
  (System Settings, Discover); the decoration against the window body (they must match at the edge).
- **Narrow changes.** Fix what was asked, where the cause is.

## Checking a change

There are no unit tests: a change is checked by building it and looking at windows on screen. Never send synthetic
input to someone's desktop; use a test machine or session. Useful: `ng/demo-controls.py` (a window with the common
controls), `BREEZE_NG_SETTINGS_PAGE=N` (the settings window opens on page N), `QT_STYLE_OVERRIDE=breeze` or
`-style fusion` to compare with another style. Report what you built and looked at, and what you could not check.

## Commits and packages

- Commit on `ng`. The subject says what changed for the user; the body says why, and what was wrong before.
- `packaging/breeze-ng.spec` builds a snapshot: after the code is pushed, set `commit` and `snapdate`
  (`YYYYMMDDHHMM` of that commit) and add a changelog line. Publishing packages and releases is the maintainer's
  decision: ask first.
- The installers download and build from this repository's `ng` branch: a change to them reaches users at once.
- Keep secrets and personal details out of code, commits and logs.
