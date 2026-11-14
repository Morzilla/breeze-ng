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
"""
import gzip
import json
import os
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


def pieces(prefix, r, ox, oy, fill=None, edge=None, mask=False):
    """The nine pieces of a rounded frame, drawn with fills only so that every bounding box is exact.
    fill and edge are (colour class, opacity) or None; the edge is one pixel wide, inside the frame."""
    out = []
    xs, ys = [ox, ox + r, ox + r + W], [oy, oy + r, oy + r + W]
    ws = [r, W, r]
    paint = {'fill': '#000000'} if mask else ({'class': fill[0], 'fill': 'currentColor', 'fill-opacity': str(fill[1])} if fill else None)
    line = {'class': edge[0], 'fill': 'currentColor', 'fill-opacity': str(edge[1])} if (edge and not mask) else None
    pin = {'fill': '#000000', 'fill-opacity': '0.001'}  # keeps the piece's bounds when nothing else is painted
    for i, name in enumerate(NAMES):
        cx, cy = i % 3, i // 3
        x, y, w, h = xs[cx], ys[cy], ws[cx], ws[cy]
        g = E.Element(f'{{{NS}}}g', {'id': prefix + name})
        E.SubElement(g, f'{{{NS}}}rect', {'x': str(x), 'y': str(y), 'width': str(w), 'height': str(h), **(paint if (paint and mask) else pin)})
        if cx != 1 and cy != 1:  # corner: a quarter disc around the inner corner of the piece
            px, py = (x + w if cx == 0 else x), (y + h if cy == 0 else y)   # centre of the arc
            ax, ay = (x if cx == 0 else x + w), (y if cy == 0 else y + h)   # outer corner
            sweep = 1 if (cx == 0) == (cy == 0) else 0
            if mask:
                g.remove(g[0])
            if paint:
                E.SubElement(g, f'{{{NS}}}path', {'d': f'M {ax},{py} A {r},{r} 0 0 {sweep} {px},{ay} L {px},{py} Z', **paint})
            if line:
                ix, iy = (ax + 1 if cx == 0 else ax - 1), (ay + 1 if cy == 0 else ay - 1)
                d = (f'M {ax},{py} A {r},{r} 0 0 {sweep} {px},{ay} L {px},{iy} '
                     f'A {r - 1},{r - 1} 0 0 {1 - sweep} {ix},{py} Z')
                E.SubElement(g, f'{{{NS}}}path', {'d': d, **line})
        else:
            if paint and not mask:
                E.SubElement(g, f'{{{NS}}}rect', {'x': str(x), 'y': str(y), 'width': str(w), 'height': str(h), **paint})
            if line and (cx, cy) != (1, 1):
                if cy == 1:
                    E.SubElement(g, f'{{{NS}}}rect', {'x': str(x if cx == 0 else x + w - 1), 'y': str(y), 'width': '1', 'height': str(h), **line})
                else:
                    E.SubElement(g, f'{{{NS}}}rect', {'x': str(x), 'y': str(y if cy == 0 else y + h - 1), 'width': str(w), 'height': '1', **line})
        out.append(g)
    return out


def redraw(rel, frames, drop=()):
    """Replace the nine pieces of the given frames in one file of the default style.
    frames: list of (prefix, radius, fill, edge[, mask]); drop: prefixes whose pieces are removed altogether."""
    root = E.fromstring(gzip.open(f'{SRC}/{rel}.svgz').read())
    ids = {f[0] + n for f in frames for n in NAMES} | {p + n for p in drop for n in NAMES}
    for parent in list(root.iter()):
        for child in list(parent):
            if child.get('id') in ids:
                parent.remove(child)
    ox = 2000  # far from the default drawings, which stay in the file for the hints
    for f in frames:
        prefix, r, fill, edge = f[:4]
        mask = len(f) > 4 and f[4]
        for g in pieces(prefix, r, ox, 2000, fill, edge, mask):
            root.append(g)
        ox += 2 * r + W + 10
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
glass_popup = 0.50 if AURORA else 0.62

# popups (the panel's, the launcher) and tooltips
redraw('dialogs/background', [('', R_POPUP, (BG, 1 if not GLASS else glass_popup), (INK, popup_rim)), ('mask-', R_POPUP, None, None, True)])
redraw('translucent/dialogs/background', [('', R_POPUP, (BG, glass_popup if GLASS else 0.86), (INK, popup_rim)), ('mask-', R_POPUP, None, None, True)])
redraw('widgets/tooltip', [('', R_TIP, (BG, 1), (INK, popup_rim)), ('mask-', R_TIP, None, None, True)])
redraw('translucent/widgets/tooltip', [('', R_TIP, (BG, 0.72 if GLASS else 0.9), (INK, popup_rim)), ('mask-', R_TIP, None, None, True)])

# buttons: raised ones (normal, pressed, with hover and focus drawn over them) and flat ones (toolbutton-*)
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
    ('hover-', R_ITEM, (INK, sheet), rim),
    ('selected-', R_ITEM, (HL, 0.30), (HL, 0.45) if GLASS else None),
    ('selected+hover-', R_ITEM, (HL, 0.38), (HL, 0.55) if GLASS else None),
])

# menu bar items (application menus in the panel)
redraw('widgets/menubaritem', [
    ('normal-', R_ITEM, None, None),
    ('hover-', R_ITEM, (INK, sheet), rim),
    ('pressed-', R_ITEM, (INK, pressed), (INK, 0.22) if GLASS else None),
])

# fields (the launcher's search)
redraw('widgets/lineedit', [
    ('base-', R_BUTTON, (INK, 0.06), rim),
    ('hover-', R_BUTTON, None, (INK, 0.24)),
    ('focus-', R_BUTTON, None, focus),
    ('focusframe-', R_BUTTON, None, focus),
])

# tabs (the launcher's Applications / Places)
redraw('widgets/tabbar', [(f'{side}-active-tab-', R_ITEM, (INK, 0.12), rim) for side in ('north', 'south', 'east', 'west')])

json.dump({'KPlugin': {'Id': f'breeze-ng-{VARIANT}', 'Name': f'Breeze NG {VARIANT.capitalize()}',
                       'Description': 'The look of the Breeze NG widget style for Plasma: popups, buttons, lists, fields',
                       'Authors': [{'Name': 'Morzilla'}], 'License': 'LGPL-2.0-or-later', 'Version': '1.0',
                       'Website': 'https://github.com/Morzilla/breeze-ng'},
           'X-Plasma-API': '5.0'}, open(f'{DST}/metadata.json', 'w'), indent=4)
if os.path.exists(f'{SRC}/plasmarc'):
    shutil.copyfile(f'{SRC}/plasmarc', f'{DST}/plasmarc')
print('written', DST)
