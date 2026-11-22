#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate client keycodes and VIA layout from this repository's QMK sources."""
import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
values = {}
for name, expression in re.findall(r'^\s*([A-Z][A-Z0-9_]+)\s*=\s*(0x[0-9A-Fa-f]+|[A-Z][A-Z0-9_]+)\s*,', (ROOT / 'quantum/keycodes.h').read_text(), re.M):
    if expression.startswith('0x'):
        values[name] = int(expression, 16)
    elif expression in values:
        values[name] = values[expression]
# Shifted punctuation and their long aliases live outside keycodes.h.
for name, basic, alias in re.findall(r'^#define\s+(KC_\w+)\s+(?:S\((KC_\w+)\)|(KC_\w+))', (ROOT / 'quantum/keymap_extras/keymap_us.h').read_text(), re.M):
    if basic:
        values[name] = 0x0200 | values[basic]
    elif alias in values:
        values[name] = values[alias]
custom = re.search(r'enum planck_keycodes\s*\{(.*?)\}', (ROOT / 'keyboards/ciank67/keymaps/default/keymap.c').read_text(), re.S)[1]
for i, item in enumerate(custom.split(',')):
    values[item.split('=')[0].strip()] = values['QK_USER'] + i
(HERE / 'Qmknrf5.Core/ViaKeycodes.json').write_text(json.dumps(values, indent=2) + '\n')
info = json.loads((ROOT / 'keyboards/ciank67/keyboard.json').read_text())
layout = info['layouts']['LAYOUT']['layout']
rows = []
for y in sorted({k['y'] for k in layout}):
    row = []; previous = 0
    for key in (k for k in layout if k['y'] == y):
        props = {n: key[n] for n in ('w', 'h') if n in key}
        if key['x'] != previous:
            props['x'] = key['x'] - previous
        if props: row.append(props)
        row.append(','.join(map(str, key['matrix'])))
        previous = key['x'] + key.get('w', 1)
    rows.append(row)
definition = {'name': 'ciank67', 'vendorId': info['usb']['vid'], 'productId': info['usb']['pid'],
              'matrix': {'rows': 5, 'cols': 14}, 'menus': ['qmk_rgb_matrix', 'qmk_rgblight', 'qmk_audio'],
              'customKeycodes': [{'name': n.split('=')[0].strip(), 'title': n.split('=')[0].strip(), 'shortName': n.split('=')[0].strip()} for n in custom.split(',')],
              'layouts': {'keymap': rows}}
(HERE / 'ciank67-via.json').write_text(json.dumps(definition, indent=2) + '\n')

(HERE / 'Qmknrf5.App/Layouts/ciank67.json').write_text(json.dumps({'name': 'ciank67', 'matrix': {'rows': 5, 'cols': 14}, 'layout': layout, 'custom_keycodes': {k: v for k, v in values.items() if v >= values['QK_USER'] and not k.startswith('QK_')}}, indent=2) + '\n')
