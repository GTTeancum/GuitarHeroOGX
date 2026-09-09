#!/usr/bin/env python3
"""Build GH2 movement links by matching existing source poses, without editing them.

Accepts uncompressed ACP18 files emitted by gh3_ps2_source_character. The recipe
supplies semantic groups; pose distance selects transition times only. Stationary
clips receive zero virtual facing channels so GH2's motion predictor can evaluate
them. The generated graph still requires in-game contact/transition review.
"""
import argparse
import json
import struct
from pathlib import Path

import numpy as np

from gh3_ps2_source_character import write_acp

WIDTH = {'pos': 3, 'scale': 3, 'quat': 4, 'rotx': 1, 'roty': 1,
         'rotz': 1, 'drotx': 1, 'droty': 1, 'drotz': 1}
TIME_FLAGS = {0: 0, 1: 0x1000, 2: 0x2000, 4: 0x4000, 8: 0x8000, 16: 0x200, 32: 0x400}


def read_acp(path):
    data = path.read_bytes()
    offset = 0
    def take(fmt):
        nonlocal offset
        result = struct.unpack_from('<'+fmt, data, offset)
        offset += struct.calcsize('<'+fmt)
        return result[0] if len(result) == 1 else result
    def string():
        nonlocal offset
        size = take('I')
        value = data[offset:offset+size].decode('ascii')
        offset += size
        return value
    if string() != 'AnimClipSamples': raise ValueError('Expected ACP samples')
    name = string()
    if take('I') != 18: raise ValueError('Only generated ACP18 is supported')
    start, duration, bps, flags, time, blend, revision = take('fffIIfI')
    if start != 0 or bps != 1 or revision != 5: raise ValueError('Unexpected generated ACP timing')
    channels = [string() for _ in range(take('I'))]
    count, *reserved = take('IIIII')
    if any(reserved): raise ValueError('Compressed or secondary sample sets are unsupported')
    width = sum(WIDTH[c.rsplit('.', 1)[1]] for c in channels)
    values = np.frombuffer(data, '<f4', offset=offset).reshape(count, width).copy()
    if not np.isfinite(values).all() or count < 2: raise ValueError('Invalid generated samples')
    return dict(name=name, channels=channels, values=values, duration=duration,
                flags=flags, time=TIME_FLAGS[time], blend=blend)


def channel_values(clip):
    result = {}
    offset = 0
    for name in clip['channels']:
        width = WIDTH[name.rsplit('.', 1)[1]]
        result[name] = clip['values'][:, offset:offset+width]
        offset += width
    return result


def distances(a, b, b_frame):
    """Mean local joint rotation distance; ignore global facing and prop frames."""
    av, bv = channel_values(a), channel_values(b)
    keys = sorted(k for k in av.keys() & bv.keys() if k.endswith('.quat') and
                  any(part in k for part in ('pelvis', 'thigh', 'calf', 'foot', 'spine')))
    if not keys: raise ValueError('No common body joints for transition matching')
    errors = []
    for key in keys:
        q = av[key] / np.maximum(np.linalg.norm(av[key], axis=1, keepdims=True), 1e-9)
        target = bv[key][b_frame] / max(np.linalg.norm(bv[key][b_frame]), 1e-9)
        errors.append(2 * np.arccos(np.clip(np.abs(q @ target), 0, 1)))
    return np.mean(errors, axis=0)


def build(bank, recipe, output):
    groups = json.loads(recipe.read_text())['groups']['main']
    clips = {p.stem: read_acp(p) for p in bank.glob('*.acp')}
    for name, clip in clips.items():
        if 'bone_facing.pos' not in clip['channels']:
            clip['channels'] += ['bone_facing.pos', 'bone_facing.rotz']
            clip['values'] = np.column_stack((clip['values'], np.zeros((len(clip['values']), 4))))
            write_acp(bank/(name+'.acp'), clip['name'], clip['channels'], clip['values'],
                      clip['duration'], clip['flags'], clip['blend'], clip['time'])
    turn, walk, stop = (groups[n] for n in ('walk_turn', 'walk_walk', 'walk_stop'))
    stationary = sorted(set(sum((groups.get(g, []) for g in
        ('normal', 'idle', 'solo', 'extreme', 'bad', 'star_power', 'intro', 'sync_jump')), [])))
    rows, evidence = [], []
    def edge(a, b, indexes, target_frame):
        ca, cb = clips[a], clips[b]
        error = distances(ca, cb, target_frame)
        for index in indexes:
            current = index * ca['duration'] / (len(ca['values'])-1)
            nxt = target_frame * cb['duration'] / (len(cb['values'])-1)
            rows.append(f'{a}\t{b}\t{current:.8f}\t{nxt:.8f}')
            evidence.append(dict(source=a, target=b, current=current, next=nxt,
                                 mean_joint_angle_radians=float(error[index])))
    for a in stationary:
        for b in turn:
            ca = clips[a]
            errors = distances(ca, clips[b], 0)
            samples_per_second = max(1, round((len(errors)-1)/ca['duration']))
            indexes = [i+int(np.argmin(errors[i:i+samples_per_second]))
                       for i in range(0, len(errors), samples_per_second)]
            edge(a, b, indexes, 0)
    for a in turn:
        for b in walk:
            target = int(np.argmin(distances(clips[b], clips[a], -1)))
            edge(a, b, [len(clips[a]['values'])-1], target)
    for a in walk:
        for b in stop:
            edge(a, b, [int(np.argmin(distances(clips[a], clips[b], 0)))], 0)
    for a in stop:
        for b in groups['normal']:
            target = int(np.argmin(distances(clips[b], clips[a], -1)))
            edge(a, b, [len(clips[a]['values'])-1], target)
    output.write_text('# source target current_beat next_beat\n'+'\n'.join(rows)+'\n')
    output.with_suffix('.json').write_text(json.dumps(dict(
        method='Existing source pose matching; no body pose edits',
        visually_verified=False, transitions=evidence), indent=2))
    print(f'Generated {len(rows)} movement transition nodes')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('bank', 'recipe', 'output'): p.add_argument('--'+name, type=Path, required=True)
    a = p.parse_args()
    build(a.bank, a.recipe, a.output)
