#!/usr/bin/env python3
"""Generate src/parts/srcfieldmap.h: the field contract between VPX parts and a
vpxtool source tree.

Both sides state the mapping in their own source, keyed by the same four character
BIFF tag, so nothing here is guessed:

  Visual Pinball  src/parts/<part>.cpp   writer.WriteFloat(FID(HTBT), m_d.m_heightbottom)
  vpin            gameitem/<part>.rs     "HTBT" => wall.height_bottom = reader.get_f32()

The Rust field name is the key vpxtool writes, and the Rust type says how the value
is written: an f32 is a number, a Color is "#rrggbb", an enum is a lowercase name.
The two sides are cross checked (a bool must meet a bool), and anything that only one
side knows about is reported rather than mapped.
"""
import json
import os
import re
import sys

# (vpx part source, vpx class, source tree type name, vpin module)
PARTS = [
    ('surface.cpp', 'Surface', 'Wall', 'wall'),
    ('ramp.cpp', 'Ramp', 'Ramp', 'ramp'),
    ('rubber.cpp', 'Rubber', 'Rubber', 'rubber'),
    ('light.cpp', 'Light', 'Light', 'light'),
    ('trigger.cpp', 'Trigger', 'Trigger', 'trigger'),
    ('flasher.cpp', 'Flasher', 'Flasher', 'flasher'),
    ('bumper.cpp', 'Bumper', 'Bumper', 'bumper'),
    ('kicker.cpp', 'Kicker', 'Kicker', 'kicker'),
    ('flipper.cpp', 'Flipper', 'Flipper', 'flipper'),
    ('gate.cpp', 'Gate', 'Gate', 'gate'),
    ('spinner.cpp', 'Spinner', 'Spinner', 'spinner'),
    ('plunger.cpp', 'Plunger', 'Plunger', 'plunger'),
    ('primitive.cpp', 'Primitive', 'Primitive', 'primitive'),
    ('hittarget.cpp', 'HitTarget', 'HitTarget', 'hittarget'),
    ('decal.cpp', 'Decal', 'Decal', 'decal'),
    ('textbox.cpp', 'Textbox', 'TextBox', 'textbox'),
    ('timer.cpp', 'Timer', 'Timer', 'timer'),
    ('dispreel.cpp', 'DispReel', 'Reel', 'reel'),
    ('lightseq.cpp', 'LightSeq', 'LightSequencer', 'lightsequencer'),
    # a drag point is a sub object of the parts that have them, with its own keys
    ('dragpoint.cpp', 'IHaveDragPoints', 'DragPoint', 'dragpoint'),
    # the table itself, which the tree keeps in gamedata.json
    ('pintable.cpp', 'PinTable', 'GameData', 'gamedata'),
]

# the drag point object keeps the shared attributes in the point itself
NO_SHARED_SPLIT = {'DragPoint'}

# tags every part shares; they live in the gameitems.json index, not the part file
SHARED_INDEX_TAGS = {'LOCK': 'is_locked', 'LAYR': 'editor_layer', 'LANR': 'editor_layer_name',
                     'LVIS': 'editor_layer_visibility'}

# what the part's own Save() writes for the scripting timer
TIMER_TAGS = {'TMON': 'is_timer_enabled', 'TMIN': 'timer_interval'}

# how a Rust field type is written as JSON
KIND_OF_RUST = {
    'bool': 'Bool', 'u32': 'UInt', 'i32': 'Int', 'f32': 'Float', 'String': 'String',
    'Vertex2D': 'Vertex2D', 'Vertex3D': 'Vertex3D', 'Color': 'Color', 'Font': 'Font',
}

# which writer calls can legitimately feed which JSON kind
COMPATIBLE = {
    'Bool': {'WriteBool'},
    'Int': {'WriteInt', 'WriteUInt'},
    'UInt': {'WriteInt', 'WriteUInt'},
    'Float': {'WriteFloat'},
    'String': {'WriteString', 'WriteWideString', 'WriteScript'},
    'Vertex2D': {'WriteVector2'},
    'Vertex3D': {'WriteVector3', 'WriteVector4'},
    'Color': {'WriteInt', 'WriteUInt'},
    'Quantized8': {'WriteInt', 'WriteUInt'},
    'Enum': {'WriteInt', 'WriteUInt'},
    'Font': {'WriteFontDescriptor'},
}


def parse_vpx_save(path, cls):
    """Ordered [(tag, writer method)] from a part's Save(IObjectWriter&)."""
    with open(path, encoding='utf-8') as f:
        text = f.read()
    fn = 'SavePoints' if cls == 'IHaveDragPoints' else 'Save'
    m = re.search(r'^void %s::%s\(IObjectWriter\s*&' % (re.escape(cls), fn), text, re.M)
    if not m:
        raise SystemExit('no Save() for ' + cls)
    end = text.index('\n}\n', m.start())
    body = re.sub(r'//[^\n]*', '', text[m.start():end])
    out = []
    for wm in re.finditer(r'writer\.(Write\w+)\(FID\((\w{4})\)', body):
        out.append((wm.group(2), wm.group(1)))
    # The table writes its three view setups through a table of tags rather than naming
    # each one at its write call, so those tags are only seen as bare FIDs; the value kind
    # then comes from the tree's side alone.
    seen = { tag for tag, _ in out }
    for wm in re.finditer(r'FID\((\w{4})\)', body):
        if wm.group(1) not in seen:
            seen.add(wm.group(1))
            out.append((wm.group(1), None))
    return out


def unwrap(rust_type):
    t = rust_type.strip()
    while True:
        m = re.match(r'^Option<(.+)>$', t)
        if not m:
            return t
        t = m.group(1).strip()


def main():
    vpx_src = sys.argv[1]
    vpin = json.load(open(sys.argv[2]))
    out_path = sys.argv[3]
    notes_path = sys.argv[4] if len(sys.argv) > 4 else out_path + '.notes' 

    enums = {name: {int(k): v for k, v in table.items()} for name, table in vpin['enums'].items()}
    used_enums = {}
    types = []
    notes = []

    for cpp, cls, type_name, module in PARTS:
        saved = parse_vpx_save(os.path.join(vpx_src, 'parts', cpp), cls)
        info = vpin['types'][type_name]
        fields, tags = info['fields'], info['tags']
        renames = info['renames']

        split_shared = type_name not in NO_SHARED_SPLIT
        mapped, skipped = [], []
        for tag, method in saved:
            if split_shared and (tag in SHARED_INDEX_TAGS or tag in TIMER_TAGS):
                continue  # handled outside the per field table
            entry = tags.get(tag)
            if entry is None:
                skipped.append((tag, method, 'the source tree has no field for it'))
                continue
            field = entry['field']
            rust = unwrap(fields.get(field, ''))
            key = renames.get(field, field)

            index = entry['index']
            if entry.get('quantized'):
                # the tree holds the value the table means, the file holds the byte it was
                # quantized to, so the two sides need the conversion in between
                kind = 'Quantized8'
                mapped.append({'tag': tag, 'key': key, 'key2': None, 'kind': kind,
                               'index': -1, 'enum': None})
                continue
            if index is not None:
                kind = 'FloatAt'
                if not rust.startswith('[f32'):
                    skipped.append((tag, method, 'indexed field is ' + rust))
                    continue
            elif rust in KIND_OF_RUST:
                kind = KIND_OF_RUST[rust]
            elif rust in enums:
                kind = 'Enum'
                used_enums[rust] = enums[rust]
            else:
                skipped.append((tag, method, 'no JSON representation for ' + (rust or '?')))
                continue

            if method is not None and (type_name, tag) != ('DragPoint', 'VCEN') \
                    and method not in COMPATIBLE.get(kind if kind != 'FloatAt' else 'Float', set()):
                skipped.append((tag, method, 'writes a %s but the tree holds %s' % (method, rust)))
                continue

            key2 = None
            if type_name == 'DragPoint' and tag == 'VCEN':
                kind, key, key2 = 'Vertex2DXY', 'x', 'y'  # one tag, two keys

            mapped.append({'tag': tag, 'key': key, 'key2': key2, 'kind': kind,
                           'index': index if index is not None else -1,
                           'enum': rust if kind == 'Enum' else None})

        # tags every part shares, which its own biff_read never sees because they are
        # read by a trait: the timer and the part group live in the part's own file
        if split_shared:
            if 'timer' in fields:
                mapped.append({'tag': 'TMON', 'key': 'is_timer_enabled', 'key2': None,
                               'kind': 'Bool', 'index': -1, 'enum': None})
                mapped.append({'tag': 'TMIN', 'key': 'timer_interval', 'key2': None,
                               'kind': 'Int', 'index': -1, 'enum': None})
            if 'part_group_name' in fields:
                mapped.append({'tag': 'GRUP', 'key': 'part_group_name', 'key2': None,
                               'kind': 'String', 'index': -1, 'enum': None})

        types.append((type_name, mapped))
        for tag, method, why in skipped:
            notes.append('%s: %s (%s) %s' % (type_name, tag, method, why))
        # fields the tree has that no Save() call feeds
        written = {f['tag'] for f in mapped}
        unfed = [t for t, e in tags.items()
                 if t not in written and t not in ('DPNT', 'PNTS')
                 and (not split_shared or (t not in SHARED_INDEX_TAGS and t not in TIMER_TAGS))]
        for t in sorted(unfed):
            notes.append('%s: %s is in the tree but VPX never writes it' % (type_name, t))

    emit(out_path, types, used_enums)
    with open(notes_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(notes) + '\n')
    total = sum(len(m) for _, m in types)
    print('mapped %d fields across %d types, %d notes' % (total, len(types), len(notes)))


def emit(path, types, used_enums):
    lines = []
    w = lines.append
    w('// license:GPLv3+')
    w('')
    w('// GENERATED by tools/srcmap/gen_srcfieldmap.py -- do not edit by hand.')
    w('//')
    w('// The field contract between a VPX part and its file in a vpxtool source tree,')
    w('// joined on the four character tag both sides use: the tag a part writes in its')
    w('// Save(IObjectWriter&), and the field vpin reads that tag into (whose name is the')
    w('// key vpxtool writes, and whose type is how the value is spelled).')
    w('')
    w('#pragma once')
    w('')
    w('#include <cstdint>')
    w('')
    w('enum class SrcValueKind : uint8_t')
    w('{')
    w('   Bool, Int, UInt, Float, String, Vertex2D, Vertex2DXY, Vertex3D, Color, Enum, Font, FloatAt, Quantized8')
    w('};')
    w('')
    w('struct SrcEnumValue')
    w('{')
    w('   int value;')
    w('   const char *name;')
    w('};')
    w('')
    w('struct SrcField')
    w('{')
    w('   uint32_t tag;')
    w('   const char *key;')
    w('   const char *key2; // the second key of a Vertex2DXY, otherwise null')
    w('   SrcValueKind kind;')
    w('   int8_t index; // element of an array valued key, -1 when the key is scalar')
    w('   const SrcEnumValue *values;')
    w('   uint8_t valueCount;')
    w('};')
    w('')
    w('struct SrcTypeFields')
    w('{')
    w('   const char *typeName;')
    w('   const SrcField *fields;')
    w('   uint32_t count;')
    w('};')
    w('')
    for name, table in sorted(used_enums.items()):
        w('static constexpr SrcEnumValue c_srcEnum%s[] = {' % name)
        for value, text in sorted(table.items()):
            w('   { %d, "%s" },' % (value, text))
        w('};')
        w('')
    for type_name, mapped in types:
        w('static constexpr SrcField c_srcFields%s[] = {' % type_name)
        for f in mapped:
            tag = f['tag']
            fid = "'%s' | ('%s' << 8) | ('%s' << 16) | ('%s' << 24)" % tuple(tag)
            if f['enum']:
                values = 'c_srcEnum%s' % f['enum']
                count = len(used_enums[f['enum']])
            else:
                values, count = 'nullptr', 0
            key2 = ('"%s"' % f['key2']) if f.get('key2') else 'nullptr'
            w('   { %s, "%s", %s, SrcValueKind::%s, %d, %s, %d },'
              % (fid, f['key'], key2, f['kind'], f['index'], values, count))
        w('};')
        w('')
    w('// The shared attributes do not live in the part file: vpxtool keeps them in the')
    w('// gameitems.json index, one entry per part.')
    w('//')
    w("// An entry the tree already has keeps its legacy layer index and name (LAYR, LANR).")
    w('// VPX replaced layers with part groups and only re-derives those two when it saves,')
    w('// renumbering the index and inventing a name wherever the table never recorded one,')
    w('// so writing them would rewrite every entry of the index for a save that changed')
    w('// nothing. A part that is new to the tree has no recorded value to keep, so its')
    w("// entry takes VPX's.")
    index_fields = [('LOCK', 'is_locked', 'Bool'), ('LVIS', 'editor_layer_visibility', 'Bool')]
    new_fields = index_fields + [('LAYR', 'editor_layer', 'Int'), ('LANR', 'editor_layer_name', 'String')]
    for array, entries in [('c_srcIndexFields', index_fields), ('c_srcIndexFieldsNew', new_fields)]:
        w('static constexpr SrcField %s[] = {' % array)
        for tag, key, kind in entries:
            fid = "'%s' | ('%s' << 8) | ('%s' << 16) | ('%s' << 24)" % tuple(tag)
            w('   { %s, "%s", nullptr, SrcValueKind::%s, -1, nullptr, 0 },' % (fid, key, kind))
        w('};')
        w('')
    w('static constexpr SrcTypeFields c_srcIndexType = { "gameitems.json", c_srcIndexFields, %d };' % len(index_fields))
    w('static constexpr SrcTypeFields c_srcIndexTypeNew = { "gameitems.json", c_srcIndexFieldsNew, %d };' % len(new_fields))
    w('')
    w('static constexpr SrcTypeFields c_srcTypeFields[] = {')
    for type_name, mapped in types:
        w('   { "%s", c_srcFields%s, %d },' % (type_name, type_name, len(mapped)))
    w('};')
    w('')
    with open(path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))


if __name__ == '__main__':
    main()
