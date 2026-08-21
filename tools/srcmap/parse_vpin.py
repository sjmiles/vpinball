#!/usr/bin/env python3
"""Extract the source tree's field contract from vpin's gameitem sources.

vpin is the library vpxtool uses to write <table>_src, so its biff_read match arms
state the mapping outright: a four character BIFF tag on the left, the struct field
it lands in on the right. The struct field name is the JSON key (serde), and the
field's Rust type says how the value is written (a colour becomes "#rrggbb", an enum
becomes a lowercase name, ...).
"""
import json
import os
import re
import sys

if len(sys.argv) < 2:
    raise SystemExit('usage: parse_vpin.py <checkout of github.com/francisdb/vpin>')
VPIN = sys.argv[1]
GAMEITEM = os.path.join(VPIN, 'src/vpx/gameitem')

# rust module -> the type name the source tree uses (GameItemEnum::type_name)
MODULES = {
    'wall': 'Wall', 'flipper': 'Flipper', 'timer': 'Timer', 'plunger': 'Plunger',
    'textbox': 'TextBox', 'bumper': 'Bumper', 'trigger': 'Trigger', 'light': 'Light',
    'kicker': 'Kicker', 'decal': 'Decal', 'gate': 'Gate', 'spinner': 'Spinner',
    'ramp': 'Ramp', 'reel': 'Reel', 'lightsequencer': 'LightSequencer',
    'primitive': 'Primitive', 'flasher': 'Flasher', 'rubber': 'Rubber',
    'hittarget': 'HitTarget', 'dragpoint': 'DragPoint',
}


def read(name, directory=None):
    with open(os.path.join(directory or GAMEITEM, name), encoding='utf-8') as f:
        return f.read()


def strip_comments(text):
    text = re.sub(r'//[^\n]*', '', text)
    return text


def find_block(text, start):
    """Return the text of the brace delimited block that starts at or after `start`."""
    depth = 0
    i = text.index('{', start)
    begin = i
    while i < len(text):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[begin + 1:i], i
        i += 1
    raise ValueError('unbalanced braces')


def take_expression(text):
    """The text of a match arm written as an expression, up to its trailing comma."""
    depth = 0
    for i, c in enumerate(text):
        if c in '([{':
            depth += 1
        elif c in ')]}':
            if depth == 0:
                return text[:i]
            depth -= 1
        elif c == ',' and depth == 0:
            return text[:i]
    return text


def parse_struct_fields(text, struct_name):
    """field -> rust type, in declaration order."""
    m = re.search(r'\bpub struct %s\b' % re.escape(struct_name), text)
    if not m:
        return {}
    body, _ = find_block(text, m.end())
    fields = {}
    for fm in re.finditer(r'^\s*pub\s+(\w+)\s*:\s*([^,]+),', strip_comments(body), re.M):
        fields[fm.group(1)] = fm.group(2).strip()
    return fields


def parse_biff_read(text, struct_name, reader_fn=None):
    """tag -> (field, index or None). Reads the match arms of the biff reader."""
    m = re.search(reader_fn if reader_fn else r'impl BiffRead for %s\b' % re.escape(struct_name), text)
    if not m:
        return {}
    body, _ = find_block(text, m.end())
    body = strip_comments(body)
    out = {}
    # an arm is  "TAG" => { ... }  or  "TAG" => expr,  and may list several tags
    for am in re.finditer(r'((?:"\w{4}"\s*\|\s*)*"\w{4}")\s*=>\s*', body):
        tags = re.findall(r'"(\w{4})"', am.group(1))
        rest = body[am.end():]
        if rest.lstrip().startswith('{'):
            arm, _ = find_block(body, am.end())
        else:
            arm = take_expression(rest)
        # the field this arm assigns from the record: an arm may set a flag of its own
        # first (EFSS notes a beta file format), so take the one fed by the reader
        assignments = list(re.finditer(r'\b\w+\.(\w+)(?:\[(\d+)\])?\s*=([^;]*)', arm))
        fm = next((a for a in assignments if re.search(r'\b(reader|sub_data)\b', a.group(3))), None)
        if fm is None:
            continue
        quantized = 'dequantize_unsigned' in fm.group(3)
        for tag in tags:
            out[tag] = (fm.group(1), int(fm.group(2)) if fm.group(2) else None, quantized)
    return out


def parse_json_struct(text, struct_name):
    """The <Type>Json struct decides the key names and their order.

    Returns (ordered list of keys, field -> key renames). serde flatten pulls the
    nested struct's own keys in at that position.
    """
    m = re.search(r'\bstruct %sJson\b' % re.escape(struct_name), text)
    if not m:
        return None, {}
    body, _ = find_block(text, m.end())
    body = strip_comments(body)
    keys = []
    renames = {}
    for fm in re.finditer(r'((?:^[ \t]*#\[[^\]]*\]\s*$\n)*)^[ \t]*(?:pub(?:\([^)]*\))?\s+)?(\w+)\s*:\s*([^,]+),',
                          body, re.M):
        attrs, field, _rtype = fm.group(1), fm.group(2), fm.group(3)
        if 'flatten' in attrs:
            keys.append(('flatten', field))
            continue
        rn = re.search(r'rename\s*=\s*"([^"]+)"', attrs)
        key = rn.group(1) if rn else field
        renames[field] = key
        keys.append(('field', key))
    return keys, renames


def parse_enums(text):
    """enum type -> {int value: json string}.

    The numeric value is either an explicit discriminant on the variant or the arm of
    the From<enum> for u32 impl; the string is the arm of the Serialize impl, written
    either as serialize_str("...") directly or through a `let value = match self`.
    """
    enums = {}
    for m in re.finditer(r'\bpub enum (\w+)\s*\{', text):
        name = m.group(1)
        variants, _ = find_block(text, m.end() - 1)
        numbers = {}
        for vm in re.finditer(r'^\s*(\w+)\s*=\s*(\d+)\s*,', strip_comments(variants), re.M):
            numbers[vm.group(1)] = int(vm.group(2))
        if not numbers:
            nm = re.search(r'impl From<&?%s> for u32\s*\{' % re.escape(name), text)
            if nm:
                body, _ = find_block(text, nm.end() - 1)
                for vm in re.finditer(r'%s::(\w+)\s*=>\s*(\d+)' % re.escape(name), body):
                    numbers[vm.group(1)] = int(vm.group(2))
        sm = re.search(r'impl Serialize for %s\b' % re.escape(name), text)
        strings = {}
        if sm:
            body, _ = find_block(text, sm.end())
            for vm in re.finditer(r'%s::(\w+)\s*=>\s*(?:serializer\.serialize_str\(")?"?([\w]+)"'
                                  % re.escape(name), body):
                strings[vm.group(1)] = vm.group(2)
        if numbers and strings:
            missing = [v for v in strings if v not in numbers]
            if missing:
                print('enum %s: no number for %s' % (name, missing), file=sys.stderr)
            enums[name] = {numbers[v]: s for v, s in strings.items() if v in numbers}
    return enums


def main():
    result = {'types': {}, 'enums': {}}
    shared = read('select.rs')
    result['enums'].update(parse_enums(shared))

    # types defined outside the item's own file
    extra_files = ['ramp_image_alignment.rs', 'font.rs', 'dragpoint.rs']
    for name in extra_files:
        result['enums'].update(parse_enums(read(name)))

    for module, type_name in MODULES.items():
        text = read(module + '.rs')
        struct_name = type_name
        fields = parse_struct_fields(text, struct_name)
        tags = parse_biff_read(text, struct_name)
        json_keys, renames = parse_json_struct(text, struct_name)
        result['enums'].update(parse_enums(text))
        result['types'][type_name] = {
            'module': module,
            'fields': fields,
            'tags': {t: {'field': f, 'index': i, 'quantized': q} for t, (f, i, q) in tags.items()},
            'json_keys': json_keys,
            'renames': renames,
        }
    # the table itself, which the tree keeps in gamedata.json
    text = read('gamedata.rs', os.path.join(VPIN, 'src/vpx'))
    json_keys, renames = parse_json_struct(text, 'GameData')
    result['enums'].update(parse_enums(text))
    result['types']['GameData'] = {
        'module': 'gamedata',
        'fields': parse_struct_fields(text, 'GameData'),
        'tags': {t: {'field': f, 'index': i, 'quantized': q}
                 for t, (f, i, q) in parse_biff_read(text, 'GameData', r'fn read_all_gamedata_records').items()},
        'json_keys': json_keys,
        'renames': renames,
    }

    json.dump(result, sys.stdout, indent=1)


if __name__ == '__main__':
    main()
