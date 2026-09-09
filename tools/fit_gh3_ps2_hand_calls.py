#!/usr/bin/env python3
"""Propose disjoint source finger layers for stock GH2 chord-call poses.

This fits distal-joint bend magnitudes, not complete hand contact or semantics.
The emitted candidate recipe requires visual verification before acceptance.
Input poses come from character_clip_binding_test --poses hdr ark fret.milo.
"""
import argparse
import copy
import gzip
import json
from pathlib import Path

import numpy as np
from scipy.spatial.transform import Rotation

from gh3_midori_bone_names import checksum_name_map, resolved_bone_name
from gh3_midori_gh2_bridge import GH3_TO_GH2_BONES


def fit(source, stock_path, recipe_path, calls):
    manifest = json.loads((source / 'midori_source_ir_manifest.json').read_text())
    bones = manifest['skeleton']['bones']
    lookup = checksum_name_map(bones)
    names = {i: GH3_TO_GH2_BONES.get(resolved_bone_name(b, lookup), '')
             for i, b in enumerate(bones)}
    target = {}
    for line in stock_path.read_text().splitlines():
        if not line.startswith('POSE\t'): continue
        _, clip, frame, bone, kind, *raw = line.split('\t')
        values = list(map(float, raw))
        if kind == '2': angle = Rotation.from_quat(values[3:7]).magnitude()
        elif kind in ('3', '4', '5'): angle = abs(values[7])
        else: continue
        target.setdefault(clip, {})[bone.removesuffix('.mesh')] = float(angle)
    candidates = {}
    with gzip.open(source / 'animations/midori_ska_ir.jsonl.gz', 'rt') as stream:
        for raw in stream:
            clip = json.loads(raw)
            if '/hands/' not in clip['path'] or clip['header']['duration_seconds'] != 0:
                continue
            if not clip['header']['flags'] & 0x200:
                continue
            angles = {names[b['index']]: float(Rotation.from_quat(b['quat_keys'][0]['raw_xyzw']).magnitude())
                      for b in clip['bones'] if b['quat_keys']}
            candidates[clip['path']] = angles
    recipe = json.loads(recipe_path.read_text())
    base = next(a for a in recipe['animations'] if a['role'] == 'fret' and a['aliases'] == ['finger_open'])
    actions, report = [], []
    for call in calls:
        action = copy.deepcopy(base)
        action['aliases'] = [call]
        action['pose_layers'] = []
        fits = []
        for finger in ('index', 'middlefinger', 'ringfinger', 'pinky'):
            joints = [f'bone_L-{finger}{i:02}' for i in (1, 2, 3)]
            distal = joints[1:]
            expected = np.array([target[call][n] for n in distal])
            scores = sorted((float(np.linalg.norm(np.array([angles[n] for n in distal]) - expected)), path)
                            for path, angles in candidates.items() if all(n in angles for n in joints))
            if not scores: raise ValueError('No source finger pose candidates')
            error, path = scores[0]
            action['pose_layers'].append({'source': path, 'bones': joints})
            fits.append({'finger': finger, 'source': path, 'distal_angle_error_radians': error})
        actions.append(action)
        report.append({'call': call, 'fits': fits, 'visually_verified': False})
    return actions, report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'stock-poses', 'recipe', 'output', 'report'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--calls', required=True, help='Comma-separated stock call names')
    args = parser.parse_args()
    actions, report = fit(args.source, args.stock_poses, args.recipe, args.calls.split(','))
    args.output.write_text(json.dumps(actions, indent=2) + '\n')
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(f'Proposed {len(actions)} source-composed chord calls; visual verification required')
