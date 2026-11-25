#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.0-or-later
"""Builds the Breeze NG Plasma styles (Soft and Glass): Plasma's default style with the look of the Breeze NG
widget style, for what Plasma draws itself (the panel's popups, the launcher, its buttons, lists and fields).

    make-plasma-style.py soft|glass|aurora [destination]
        default destination: ~/.local/share/plasma/desktoptheme/breeze-ng-soft (or -glass)

Only the frames below are redrawn, as nine pieces each; hints, shadows of the popups, icons and every other
file come from the default style (copied, and Plasma falls back to it for anything missing). Colours are the
colour scheme's (ColorScheme-Text as "ink", ColorScheme-Highlight), so both styles follow light and dark schemes.

  popups and tooltips   rounder; Glass translucent with a thin rim
  buttons               an ink sheet (Glass: with a rim), hover a little more ink, pressed more, focus a ring
  flat buttons          nothing at rest, a pill on hover and when pressed
  list items            a pill on hover, the highlight colour when selected
  fields                an ink sheet, a rim on hover, the highlight colour on focus
  menu bar items, tabs  pills
  scroll bars           a pill of ink, no rim, no groove
"""
import gzip
import json
import os
import re
import shutil
import sys
import xml.etree.ElementTree as E

SRC = '/usr/share/plasma/desktoptheme/default'
VARIANT = sys.argv[1] if len(sys.argv) > 1 else ''
if VARIANT not in ('soft', 'glass', 'aurora'):
    sys.exit(__doc__)
AURORA = VARIANT == 'aurora'   # Glass pushed further: more see-through popups, a brighter rim
GLASS = VARIANT in ('glass', 'aurora')
DST = sys.argv[2] if len(sys.argv) > 2 else os.path.expanduser(f'~/.local/share/plasma/desktoptheme/breeze-ng-{VARIANT}')
NS = 'http://www.w3.org/2000/svg'
E.register_namespace('', NS)
for p, u in (('sodipodi', 'http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd'), ('inkscape', 'http://www.inkscape.org/namespaces/inkscape'),
             ('xlink', 'http://www.w3.org/1999/xlink'), ('rdf', 'http://www.w3.org/1999/02/22-rdf-syntax-ns#'),
             ('cc', 'http://creativecommons.org/ns#'), ('dc', 'http://purl.org/dc/elements/1.1/')):
    E.register_namespace(p, u)
NAMES = ['topleft', 'top', 'topright', 'left', 'center', 'right', 'bottomleft', 'bottom', 'bottomright']
W = 20  # length of the stretched pieces
INK, BG, HL = 'ColorScheme-Text', 'ColorScheme-Background', 'ColorScheme-Highlight'
HLT = 'ColorScheme-HighlightedText'  # (near white on light and dark schemes alike)


def pieces(prefix, r, ox, oy, fill=None, edge=None, mask=False, pad=0):
    """The nine pieces of a rounded frame, drawn with fills only so that every bounding box is exact.
    fill and edge are (colour class, opacity) or None; fill can also be a list of them, laid one over the other.
    The edge is one pixel wide, inside the frame."""
    out = []
    # (pad: a transparent band round the visible shape, the corners and the edges thicker by it: the frame drawn inset
    # in its item, apart from its neighbours. The pieces' sizes are then not the content's margins: hints give those)
    P = pad
    xs, ys = [ox, ox + r + P, ox + r + P + W], [oy, oy + r + P, oy + r + P + W]
    ws = [r + P, W, r + P]
    fills = fill if isinstance(fill, list) else ([fill] if fill else [])
    paints = [{'class': f[0], 'fill': 'currentColor', 'fill-opacity': str(f[1])} for f in fills]
    paint = {'fill': '#000000'} if mask else (paints[0] if paints else None)
    line = {'class': edge[0], 'fill': 'currentColor', 'fill-opacity': str(edge[1])} if (edge and not mask) else None
    pin = {'fill': '#000000', 'fill-opacity': '0.001'}  # keeps the piece's bounds when nothing else is painted
    for i, name in enumerate(NAMES):
        cx, cy = i % 3, i // 3
        bx, by, bw, bh = xs[cx], ys[cy], ws[cx], ws[cy]   # the piece's bounds, with the band
        g = E.Element(f'{{{NS}}}g', {'id': prefix + name})
        E.SubElement(g, f'{{{NS}}}rect', {'x': str(bx), 'y': str(by), 'width': str(bw), 'height': str(bh), **(paint if (paint and mask and not P) else pin)})
        # what is painted: the piece less the band on its outer sides
        x = bx + (P if cx == 0 else 0)
        y = by + (P if cy == 0 else 0)
        w = bw - (P if cx != 1 else 0)
        h = bh - (P if cy != 1 else 0)
        if cx != 1 and cy != 1:  # corner: a quarter disc around the inner corner of the piece
            px, py = (x + w if cx == 0 else x), (y + h if cy == 0 else y)   # centre of the arc
            ax, ay = (x if cx == 0 else x + w), (y if cy == 0 else y + h)   # outer corner
            sweep = 1 if (cx == 0) == (cy == 0) else 0
            if mask:
                g.remove(g[0])
            for one in ([paint] if mask else paints):
                E.SubElement(g, f'{{{NS}}}path', {'d': f'M {ax},{py} A {r},{r} 0 0 {sweep} {px},{ay} L {px},{py} Z', **one})
            if line:
                ix, iy = (ax + 1 if cx == 0 else ax - 1), (ay + 1 if cy == 0 else ay - 1)
                d = (f'M {ax},{py} A {r},{r} 0 0 {sweep} {px},{ay} L {px},{iy} '
                     f'A {r - 1},{r - 1} 0 0 {1 - sweep} {ix},{py} Z')
                E.SubElement(g, f'{{{NS}}}path', {'d': d, **line})
        else:
            for one in ([] if mask else paints):
                E.SubElement(g, f'{{{NS}}}rect', {'x': str(x), 'y': str(y), 'width': str(w), 'height': str(h), **one})
            if line and (cx, cy) != (1, 1):
                if cy == 1:
                    E.SubElement(g, f'{{{NS}}}rect', {'x': str(x if cx == 0 else x + w - 1), 'y': str(y), 'width': '1', 'height': str(h), **line})
                else:
                    E.SubElement(g, f'{{{NS}}}rect', {'x': str(x), 'y': str(y if cy == 0 else y + h - 1), 'width': str(w), 'height': '1', **line})
        out.append(g)
    return out


HINTS = ['hint-top-margin', 'hint-bottom-margin', 'hint-left-margin', 'hint-right-margin']


def redraw(rel, frames, drop=()):
    """Replace the nine pieces of the given frames in one file of the default style.
    frames: list of (prefix, radius, fill, edge[, mask[, pad, margin]]); drop: prefixes whose pieces are removed
    altogether. A frame with a pad (a transparent band round it) gets its content margins as hints (margin)."""
    root = E.fromstring(gzip.open(f'{SRC}/{rel}.svgz').read())
    ids = {f[0] + n for f in frames for n in NAMES} | {p + n for p in drop for n in NAMES}
    ids |= {f[0] + h for f in frames if len(f) > 5 and f[5] for h in HINTS}
    for parent in list(root.iter()):
        for child in list(parent):
            if child.get('id') in ids:
                parent.remove(child)
    ox = 2000  # far from the default drawings, which stay in the file for the hints
    for f in frames:
        prefix, r, fill, edge = f[:4]
        mask = len(f) > 4 and f[4]
        pad = f[5] if len(f) > 5 else 0
        for g in pieces(prefix, r, ox, 2000, fill, edge, mask, pad):
            root.append(g)
        if pad:
            margin = f[6]
            for i, h in enumerate(HINTS):
                horizontal = i < 2   # top and bottom: a bar as tall as the margin
                E.SubElement(root, f'{{{NS}}}rect', {'id': prefix + h, 'x': str(ox + 40 * i), 'y': '1900', 'width': str(20 if horizontal else margin),
                                                     'height': str(margin if horizontal else 20), 'fill': '#000000', 'fill-opacity': '0.001'})
        ox += 2 * (r + pad) + W + 10
    os.makedirs(os.path.dirname(f'{DST}/{rel}'), exist_ok=True)
    with gzip.open(f'{DST}/{rel}.svgz', 'wb') as out:
        out.write(E.tostring(root, xml_declaration=True, encoding='utf-8'))


# the tones of the Breeze NG widget style (kstyle/breezehelper.cpp, renderNgButtonFrame)
sheet = 0.09 if GLASS else 0.08            # an ink sheet at rest
rim = (INK, 0.14) if GLASS else None        # Glass: a thin rim
hover = 0.06                                # a little more ink under the pointer
pressed = 0.18 if GLASS else 0.16
focus = (HL, 0.85)
R_BUTTON, R_ITEM, R_POPUP, R_TIP = 9, 7, 14, 9
popup_rim = 0.22 if AURORA else 0.16 if GLASS else 0.10
# (Aurora: 50 % let the wallpaper through, boosted by the contrast effect's saturation: dark text on a strong blue,
# hard to read. The launcher and the panel popups hold as much sheet as the applications' menus)
glass_popup = 0.80 if AURORA else 0.62
# (Aurora with compositing: the popup's own sheet is faint; Breeze NG's Kirigami plugin lays the windows' glass, wash and
# lights over it (PlasmaLights), so that a popup is the same glass as a window. Without the plugin the popup stays
# readable, if lighter)
AURORA_POPUP_SVG = 0.20

# Aurora: the controls of the widget style's Aurora (the 2026-11-24 mockups), one SVG for light and dark schemes:
#   a key is the background colour with a veil of near white over it: white on a light scheme, a lighter grey key
#   on a dark one; a field is the background colour, dense: white, or a dark slot in the glass; a selection a sheet
#   of the accent; no rims but the keys' faint edge, no separator lines, no header and footer bands in the popups
KEY = [(BG, 0.70), (HLT, 0.22)]
KEY_EDGE = (INK, 0.12)

# popups (the panel's, the launcher) and tooltips
redraw('dialogs/background', [('', R_POPUP, (BG, 1 if not GLASS else glass_popup), (INK, popup_rim)), ('mask-', R_POPUP, None, None, True)])
redraw('translucent/dialogs/background', [('', R_POPUP, (BG, AURORA_POPUP_SVG if AURORA else glass_popup if GLASS else 0.86), (INK, popup_rim)), ('mask-', R_POPUP, None, None, True)])
redraw('widgets/tooltip', [('', R_TIP, (BG, 1), (INK, popup_rim)), ('mask-', R_TIP, None, None, True)])
redraw('translucent/widgets/tooltip', [('', R_TIP, (BG, 0.72 if GLASS else 0.9), (INK, popup_rim)), ('mask-', R_TIP, None, None, True)])

# the panel (Aurora): glass as the popups, the desktop showing through blurred (the default's is nearly solid)
if AURORA:
    redraw('translucent/widgets/panel-background', [('', 2, (BG, 0.55), None)])

# buttons: raised ones (normal, pressed, with hover and focus drawn over them) and flat ones (toolbutton-*)
if AURORA:
    redraw('widgets/button', [
        ('normal-', R_BUTTON, KEY, KEY_EDGE),
        ('pressed-', R_BUTTON, [(BG, 0.70), (INK, 0.10)], KEY_EDGE),
        ('hover-', R_BUTTON, (HLT, 0.12), None),
        ('focus-', R_BUTTON, None, focus),
        ('mask-normal-', R_BUTTON, None, None, True),
        ('toolbutton-hover-', R_BUTTON, (INK, 0.06), None),
        ('toolbutton-pressed-', R_BUTTON, KEY, KEY_EDGE),
        ('toolbutton-focus-', R_BUTTON, None, focus),
    ], drop=('shadow-',))
else:
  redraw('widgets/button', [
    ('normal-', R_BUTTON, (INK, sheet), rim),
    ('pressed-', R_BUTTON, (INK, pressed), (INK, 0.26) if GLASS else None),
    ('hover-', R_BUTTON, (INK, hover), (INK, 0.22) if GLASS else None),
    ('focus-', R_BUTTON, None, focus),
    ('mask-normal-', R_BUTTON, None, None, True),
    ('toolbutton-hover-', R_BUTTON, (INK, sheet), rim),
    ('toolbutton-pressed-', R_BUTTON, (INK, pressed), (INK, 0.22) if GLASS else None),
    ('toolbutton-focus-', R_BUTTON, None, focus),
  ], drop=('shadow-',))

# list items (the launcher's applications and categories, the panel popups' lists)
redraw('widgets/viewitem', [
    ('normal-', R_ITEM, None, None),
    ('hover-', R_ITEM, (INK, 0.05 if AURORA else sheet), None if AURORA else rim),
    ('selected-', R_ITEM, (HL, 0.26 if AURORA else 0.30), None if AURORA else (HL, 0.45) if GLASS else None),
    ('selected+hover-', R_ITEM, (HL, 0.34 if AURORA else 0.38), None if AURORA else (HL, 0.55) if GLASS else None),
])

# menu bar items (application menus in the panel)
redraw('widgets/menubaritem', [
    ('normal-', R_ITEM, None, None),
    ('hover-', R_ITEM, (INK, sheet), rim),
    ('pressed-', R_ITEM, (INK, pressed), (INK, 0.22) if GLASS else None),
])

# fields (the launcher's search)
redraw('widgets/lineedit', [
    ('base-', R_BUTTON, (BG, 0.85) if AURORA else (INK, 0.06), (INK, 0.08) if AURORA else rim),
    ('hover-', R_BUTTON, None, (INK, 0.24)),
    ('focus-', R_BUTTON, None, focus),
    ('focusframe-', R_BUTTON, None, focus),
])

# tabs (the launcher's Applications / Places)
# (Aurora: the active tab is a key, as the segments of the widget style's page tabs, a little inside its tab: the system
# tray's open applet is shown with it too, and filling the cell it reached into the icons beside)
if AURORA:
    redraw('widgets/tabbar', [(f'{side}-active-tab-', R_ITEM, KEY, KEY_EDGE, False, 3, R_ITEM) for side in ('north', 'south', 'east', 'west')])
else:
    redraw('widgets/tabbar', [(f'{side}-active-tab-', R_ITEM, (INK, 0.12), rim) for side in ('north', 'south', 'east', 'west')])

# the task manager's buttons (Aurora): keys apart from each other, not blocks as tall as the panel that touch their
# neighbours. A running application a breath of ink, the active one a key, hover a little more ink, minimised less;
# attention and progress stay the default's
if AURORA:
    TASK_PAD, TASK_MARGIN = 4, 6
    redraw('widgets/tasks', [(f'{loc}{state}-', R_BUTTON, fill, edge, False, TASK_PAD, TASK_MARGIN)
                             for loc in ('', 'north-', 'east-', 'west-')
                             for state, fill, edge in (('normal', (INK, 0.07), None), ('focus', KEY, KEY_EDGE),
                                                      ('hover', (INK, 0.10), None), ('minimized', (INK, 0.04), None))])

# the header and footer bands of the panel popups (the launcher's user and search row, its tabs and buttons): a
# lighter band with a line under it. Aurora: nothing, the popup is one sheet of glass
if AURORA:
    redraw('widgets/plasmoidheading', [('header-', 6, None, None), ('footer-', 6, None, None)])

# scroll bars (the launcher's lists, the panel popups'): the default slider is a solid grey bar with a white rim, a
# bright line on the glass. A pill of ink with no rim, a little more ink under the pointer, and no groove
R_SCROLL = 3
redraw('widgets/scrollbar', [
    ('slider-', R_SCROLL, (INK, 0.26 if GLASS else 0.30), None),
    ('mouseover-slider-', R_SCROLL, (INK, 0.42 if GLASS else 0.45), None),
    ('background-vertical-', R_SCROLL, None, None),
    ('background-horizontal-', R_SCROLL, None, None),
])

# separator lines (the launcher's, the panel popups'): the default draws them in the scheme's frame colour, black
# lines on a dark glass. Breeze NG has no separator lines: a breath of ink, enough to tell two areas apart (Aurora: none,
# the areas are told apart by their tone)
line = gzip.open(f'{SRC}/widgets/line.svgz').read().decode()
line = line.replace('class="ColorScheme-Frame"\n       style="fill:currentColor"', f'class="{INK}"\n       style="fill:currentColor;fill-opacity:{0 if AURORA else 0.07}"')
if 'fill-opacity' in line:
    with gzip.open(f'{DST}/widgets/line.svgz', 'wb') as out:
        out.write(line.encode())
else:
    print('warning: the default line.svgz has changed, separator lines left as they are', file=sys.stderr)

json.dump({'KPlugin': {'Id': f'breeze-ng-{VARIANT}', 'Name': f'Breeze NG {VARIANT.capitalize()}',
                       'Description': 'The look of the Breeze NG widget style for Plasma: popups, buttons, lists, fields',
                       'Authors': [{'Name': 'Morzilla'}], 'License': 'LGPL-2.0-or-later', 'Version': '1.0',
                       'Website': 'https://github.com/Morzilla/breeze-ng'},
           'X-Plasma-API': '5.0'}, open(f'{DST}/metadata.json', 'w'), indent=4)
if os.path.exists(f'{SRC}/plasmarc'):
    rc = open(f'{SRC}/plasmarc').read()
    if GLASS:
        # the blur behind the popups at its own colours: the default saturation (1.5) turned a blue wallpaper into a
        # strong blue under the text
        # (whatever the default's value: only the key of the ContrastEffect group carries this name)
        rc = re.sub(r'(?m)^saturation=.*$', 'saturation=1.0', rc)
    if AURORA:
        # (Aurora: no contrast effect at all. The windows have the blur only, and with it a popup came out whiter and
        # more uniform than a window of the same glass, the wallpaper hardly showing through)
        rc = re.sub(r'(?ms)^(\[ContrastEffect\][^\[]*?)^enabled=true', r'\1enabled=false', rc)
    open(f'{DST}/plasmarc', 'w').write(rc)
print('written', DST)
