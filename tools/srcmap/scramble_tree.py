#!/usr/bin/env python3
"""Scramble every value the field map claims to write, in a copy of a source tree, so
that saving the table over it has to restore all of them.

    python3 scramble_tree.py <copy of a tree> ../../src/utils/SrcFieldMap.h
    VPinballX_BGFX -SaveToSrc <table>.vpx -Project <copy of a tree>
    diff -r <untouched tree> <copy of a tree>

Anything left over is a field the editor does not write back. Part names are left
alone: they are how a part finds its file."""
import glob
import json
import os
import re
import sys

src = sys.argv[1]
header = open(sys.argv[2], encoding='utf-8').read()

mapped = {}
for m in re.finditer(r'static constexpr SrcField c_srcFields(\w+)\[\] = \{(.*?)\n\};', header, re.S):
    mapped[m.group(1)] = set(re.findall(r'"(\w+)", (?:"\w+"|nullptr), SrcValueKind', m.group(2)))
index_keys = set(re.findall(r'"(\w+)", (?:"\w+"|nullptr), SrcValueKind',
                            re.search(r'c_srcIndexFields\[\] = \{(.*?)\n\};', header, re.S).group(1)))

SKIP = {'name'}


def scramble(value):
    if isinstance(value, bool):
        return not value
    if isinstance(value, (int, float)):
        return type(value)(value + 17) if not isinstance(value, bool) else value
    if isinstance(value, str):
        return 'scrambled'
    if isinstance(value, dict):
        return {k: scramble(v) for k, v in value.items()}
    if isinstance(value, list):
        return [scramble(v) for v in value]
    return value  # null stays null: the writer leaves those alone by design


def scramble_part(part, keys):
    for k in list(part):
        if k in SKIP or k not in keys or part[k] is None:
            continue
        part[k] = scramble(part[k])


count = 0
for f in sorted(glob.glob(os.path.join(src, 'gameitems', '*.json'))):
    doc = json.load(open(f))
    t = list(doc)[0]
    keys = mapped.get(t, set()) | {'drag_points'}
    scramble_part(doc[t], keys)
    if 'drag_points' in doc[t] and isinstance(doc[t]['drag_points'], list):
        for point in doc[t]['drag_points']:
            scramble_part(point, mapped.get('DragPoint', set()) | {'x', 'y'})
    open(f, 'w').write(json.dumps(doc, indent=2))
    count += 1

game = os.path.join(src, 'gamedata.json')
if os.path.exists(game):
    doc = json.load(open(game))
    scramble_part(doc, mapped.get('GameData', set()))
    open(game, 'w').write(json.dumps(doc, indent=2))

index_path = os.path.join(src, 'gameitems.json')
index = json.load(open(index_path))
for entry in index:
    scramble_part(entry, index_keys)
open(index_path, 'w').write(json.dumps(index, indent=2))

print('scrambled %d part files and %d index entries' % (count, len(index)))
