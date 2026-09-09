"""Resolve frontend animation tables against the actual PS2 character PAK.

This distinguishes duplicate SKA basenames in frontend and gameplay folders.
It reports authored candidates; it does not judge their visual suitability.
"""
import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path

import gh3_datap as datap


def key(name):
    return zlib.crc32(name.lower().replace('/', '\\').encode()) ^ 0xffffffff


def audit(iso, character):
    _, wad, entries = datap.load_entries(iso)
    by_path = {e.normalized_path.lower(): e for e in entries}

    def read(path):
        return datap.read_datap_entry(iso, wad, by_path[path])

    pak, pab = read('pak/qb.pak.ps2'), read('pak/qb.pab.ps2')
    suffix = ('\\guitar_animation_data_' + character + '.qb.ps2').encode()
    table = None
    offset = 0
    while offset + 32 <= len(pak):
        words = struct.unpack_from('<8I', pak, offset)
        if words[0] in (0x2cb3ef3b, 0xb524565f):
            break
        named = bool(words[7] & 0x20)
        name = pak[offset + 32:offset + 192].split(b'\0', 1)[0] if named else b''
        if name.endswith(suffix):
            start = offset + words[1] - len(pak)
            if start < 0 or start + words[2] > len(pab):
                raise ValueError('QB payload outside PAB')
            table = pab[start:start + words[2]]
            break
        offset += 192 if named else 32
    if table is None:
        raise ValueError('Character animation table not found')

    def u(at):
        return struct.unpack_from('<I', table, at)[0]

    def fields(at):
        if u(at) != 0x10000:
            raise ValueError('Expected PS2 QB structure')
        current, seen = u(at + 4), set()
        while current:
            if current in seen:
                raise ValueError('Cyclic QB structure')
            seen.add(current)
            kind, name, value, current = struct.unpack_from('<4I', table, current)
            yield kind, name, value

    def animation_keys(at):
        result = set()
        for kind, _, value in fields(at):
            if kind == 0x1500:
                result.update(animation_keys(value))
            elif kind == 0x1b00:
                result.add(value)
            elif kind == 0x1900 and u(value) == 0xd0100:
                count = u(value + 4)
                start = value + 8 if count == 1 else u(value + 8)
                result.update(u(start + 4 * i) for i in range(count))
        return result

    model_path = f'pak/models/guitarists/{character}_1/{character}_1_anims.pak.ps2'
    model_pak = read(model_path)
    stances = {}
    for kind, name, value in fields(u(40)):
        for stance in ('stance_frontend', 'stance_frontend_guitar'):
            if kind != 0x1500 or name != key(stance):
                continue
            idle = next(v for k, n, v in fields(value)
                        if k == 0x1500 and n == key("idle"))
            wanted = animation_keys(idle)
            matches = []
            for entry in entries:
                path = entry.normalized_path
                if not path.endswith('.ska.ps2') or key(Path(path).name[:-8]) not in wanted:
                    continue
                data = datap.read_datap_entry(iso, wad, entry)
                if data in model_pak:
                    matches.append({'path': path, 'size': len(data),
                                    'sha256': hashlib.sha256(data).hexdigest()})
            stances[stance] = matches
    if not stances.get('stance_frontend'):
        raise ValueError('No frontend clips verified against character PAK')
    return {'platform': 'PS2', 'character': character, 'package': model_path,
            'table_sha256': hashlib.sha256(table).hexdigest(), 'stances': stances}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', type=Path, required=True)
    parser.add_argument('--character', default='midori')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(json.dumps(audit(args.iso, args.character), indent=2) + '\n')
