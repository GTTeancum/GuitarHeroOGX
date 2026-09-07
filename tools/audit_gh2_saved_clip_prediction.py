"""Compare production GH2 facing evaluation with resident retail sample buffers.

Reads an existing PCSX2 save; no emulator, desktop control, or extraction.
The binding report supplies driver identities; its EE hash must still match.
"""
import argparse
import hashlib
import json
import math
import os
import struct
import subprocess
from pathlib import Path

from read_pcsx2_savestate import read_member


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binding-report', type=Path, required=True)
    parser.add_argument('--test-exe', type=Path, required=True)
    parser.add_argument('--hdr', type=Path, required=True)
    parser.add_argument('--ark', type=Path, required=True)
    parser.add_argument('--clip-set', action='append', required=True, help='name=ARK/path.milo_ps2')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    binding = json.loads(args.binding_report.read_text(encoding='utf-8'))
    memory = read_member(Path(binding['state']), 'eeMemory.bin')
    memory_hash = hashlib.sha256(memory).hexdigest()
    if memory_hash != binding['ee_sha256']:
        raise RuntimeError('Saved EE no longer matches the validated binding report')
    sources = dict(value.split('=', 1) for value in args.clip_set)

    def u32(address):
        if not 0 <= address <= len(memory) - 4:
            raise RuntimeError('EE pointer out of bounds')
        return struct.unpack_from('<I', memory, address)[0]

    def text(address):
        end = memory.find(b'\0', address, address + 256)
        if not 0x100000 <= address < end < len(memory):
            raise RuntimeError('Invalid retail symbol')
        return memory[address:end].decode('ascii')

    def read_channel(clip, wanted, kind):
        # Channel 0x16B0B0 searches full before one. The resident layout is
        # NOT the on-disc layout: vectors occupy 16 bytes, not 12.
        for section in (0x84, 0x138):
            block = clip + section
            first, last = u32(block + 4), u32(block + 8)
            if not 0x100000 <= first <= last < len(memory) or last - first > 4096:
                raise RuntimeError('Invalid sample symbol vector')
            names = [text(u32(p)) for p in range(first, last, 4)]
            if wanted not in names:
                continue
            index = names.index(wanted)
            counts = struct.unpack_from('<10I', memory, block + 0x14)
            offsets = struct.unpack_from('<10I', memory, block + 0x3C)
            if not counts[kind] <= index < counts[kind + 1]:
                raise RuntimeError('Facing symbol is in the wrong retail type bucket')
            compression = u32(block)
            if compression not in (0, 1):
                raise RuntimeError('Unsupported resident compression')
            width = 16 if kind == 0 else (2 if compression else 4)
            offset = offsets[kind] + (index - counts[kind]) * width
            count, data, interpolate = (u32(block + p) for p in (0x94, 0x98, 0x9C))
            stride = u32(block + 0x64)
            if not 0 < count < 10000 or offset + width > stride or data + count * stride > len(memory):
                raise RuntimeError('Invalid resident sample range')
            values = []
            for sample in range(count):
                address = data + sample * stride + offset
                if kind == 0:
                    values.append(list(struct.unpack_from('<3f', memory, address)))
                else:
                    value = (struct.unpack_from('<h', memory, address)[0] * 0.0006103515625
                             if compression else struct.unpack_from('<f', memory, address)[0])
                    values.append([value])
            return {'section': hex(section), 'kind': kind, 'count': count, 'interpolate': bool(interpolate),
                    'compression': compression, 'stride': stride, 'offset': offset,
                    'values': values}
        raise RuntimeError('Requested retail facing channel missing: ' + wanted)

    def sample(channel, fraction):
        # FracToSample 0x1937F0 followed by the vector/scalar branches of
        # EvaluateChannel 0x1938F8; independent of the native clip decoder.
        frame = f32(min(1, max(0, fraction)) * (channel['count'] - 1))
        index = min(int(frame + (0 if channel['interpolate'] else .5)), channel['count'] - 1)
        following = min(index + 1, channel['count'] - 1) if channel['interpolate'] else index
        amount = f32(frame - index) if following != index else 0
        if channel['kind'] == 0:
            return [f32(f32(b * amount) + f32(a * f32(1 - amount)))
                    for a, b in zip(channel['values'][index], channel['values'][following])]
        return [f32(a + f32(f32(b - a) * amount))
                for a, b in zip(channel['values'][index], channel['values'][following])]

    rows = []
    for servo in binding['rows']:
        for driver in servo['drivers']:
            source = sources.get(driver['clip_set'])
            if source is None:
                continue
            address = int(driver['address'], 16)
            if u32(address + 0x18) not in (0x3E7380, 0x3E7490):
                raise RuntimeError('Driver identity changed')
            node = u32(address + 0x38)
            if not node:
                raise RuntimeError('Selected driver has no current clip')
            clip = u32(node + 0x24)
            if u32(clip + 0x80) != 0x3E7130:
                raise RuntimeError('Selected clip is not original CharClipSamples')
            name = text(u32(u32(clip) + 0x14))
            start, end = struct.unpack_from('<2f', memory, clip + 0x18)
            position = read_channel(clip, 'bone_facing.pos', 0)
            rotation = read_channel(clip, 'bone_facing.rotz', 5)
            # Saved interpolation policy must equal the default currently
            # consumed by the native loader; never hide a mode mismatch.
            if not position['interpolate'] or not rotation['interpolate']:
                raise RuntimeError('Retail changes default facing interpolation policy')
            run = subprocess.run([str(args.test_exe), '--facing', str(args.hdr), str(args.ark), source, name],
                                 capture_output=True, text=True, encoding='utf-8', errors='replace',
                                 creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0,
                                 timeout=120)
            if run.returncode:
                raise RuntimeError('Native facing probe failed: ' + run.stderr[-1200:])
            metadata = None
            samples = []
            for line in run.stdout.splitlines():
                fields = line.split('\t')
                if fields[0] == 'FACING':
                    metadata = fields
                elif fields[0] == 'SAMPLE':
                    beat = f32(float(fields[1]))
                    fraction = f32(f32(beat - start) / f32(end - start))
                    expected = sample(position, fraction) + sample(rotation, fraction)
                    actual = [float(value) for value in fields[2:]]
                    if len(actual) != 4 or not all(math.isfinite(value) for value in actual):
                        raise RuntimeError('Malformed native facing sample')
                    error = max(abs(a - b) for a, b in zip(actual, expected))
                    samples.append({'beat': beat, 'retail': expected, 'native': actual,
                                    'max_error': error, 'pass': math.isfinite(error) and error < .0001})
            metadata_pass = (metadata is not None and metadata[1] == name
                             and f32(float(metadata[2])) == start and f32(float(metadata[3])) == end
                             and int(metadata[4]) == position['count'] and int(metadata[5]) == rotation['count'])
            for channel in (position, rotation):
                del channel['values']
            rows.append({'driver': driver['name'], 'clip_set': driver['clip_set'], 'source': source,
                         'clip': name, 'retail_address': hex(clip), 'start_beat': start, 'end_beat': end,
                         'position': position, 'rotation': rotation, 'metadata_pass': metadata_pass,
                         'samples': samples, 'pass': metadata_pass and len(samples) == 7 and all(s['pass'] for s in samples)})
    passed = len(rows) == len(sources) and all(row['pass'] for row in rows)
    report = {'state': binding['state'], 'ee_sha256': memory_hash,
              'native_test_sha256': hashlib.sha256(args.test_exe.read_bytes()).hexdigest(),
              'scope': 'Current clips of explicitly named saved drivers; not full-venue visual parity',
              'clip_count': len(rows), 'all_pass': passed, 'rows': rows}
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f"Retail/native prediction: {sum(r['pass'] for r in rows)}/{len(rows)} clips, "
          f"{sum(len(r['samples']) for r in rows)} endpoints, all_pass={passed}")
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
