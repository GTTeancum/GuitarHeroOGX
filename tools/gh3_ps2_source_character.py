#!/usr/bin/env python3
"""Source-rig GH3 PS2 conversion probe. No donor rig or authored pose offsets.

Consumes the decoded PS2 IR and preserves authored hierarchy and inverse binds.
This probe is intentionally separate from the older Casey-retargeting pipeline.
"""
from __future__ import annotations

import argparse
import gzip
import json
import math
import struct
import sys
from collections import Counter
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.spatial.transform import Rotation, Slerp

from gh3_midori_bone_names import checksum_name_map, resolved_bone_name
from gh3_midori_gh2_bridge import GH3_TO_GH2_BONES

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'rb2_wii/tools'))
from convert_rb2_preset_character import write_bundle, encode_hmx_texture

BASIS = np.array([[-1., 0., 0.], [0., 0., 1.], [0., 1., 0.]])


def source_tracks(clip):
    """Read PS2 masks, including IR exported before its byte-order fix."""
    keyed = {b['index'] for b in clip['bones'] if b['quat_key_count'] or b['trans_key_count']}
    mask = clip.get('partial_animation')
    if mask:
        words = [int(w, 16) for w in mask['words']]
        byte_order = mask.get('word_byte_order')
        if byte_order == 'big':
            words = [int.from_bytes(w.to_bytes(4, 'big'), 'little') for w in words]
        elif byte_order != 'little':
            raise ValueError('Unknown source partial-mask byte order')
        allowed = {i for i in range(clip['header']['bone_count']) if words[i//32] & (1 << (i%32))}
        if allowed != keyed:
            raise ValueError(f'Partial-mask/track mismatch: {clip["path"]}')
    return {b['index']: b for b in clip['bones'] if b['index'] in keyed}


def source_local(bone, quat=None, translation=None):
    m = np.eye(4)
    m[:3, :3] = Rotation.from_quat(bone['raw_quat_xyzw'] if quat is None else quat).as_matrix()
    m[3, :3] = np.array(bone['raw_offset'][:3])
    if translation is not None:
        m[3, :3] += translation
    return m


def worlds(locals_, bones):
    result = {}
    def visit(i):
        if i not in result:
            p = bones[i]['parent_index']
            result[i] = locals_[i] @ visit(p) if p is not None and p >= 0 and p < len(bones) else locals_[i]
        return result[i]
    return [visit(i) for i in range(len(bones))]


def target_matrix(source, scale):
    m = np.eye(4)
    m[:3, :3] = BASIS @ source[:3, :3] @ BASIS
    m[3, :3] = source[3, :3] @ BASIS * scale
    return m


def matrix12(m):
    return list(m[:3, :3].flatten()) + list(m[3, :3])


def write_acp(path, name, channels, samples, duration, clip_flags=0, blend_width=.24, play_flags=0):
    widths = {'pos': 3, 'scale': 3, 'quat': 4, 'rotx': 1, 'roty': 1,
              'rotz': 1, 'drotx': 1, 'droty': 1, 'drotz': 1}
    categories = {name: index for index, name in enumerate(widths)}
    spans = []
    offset = 0
    for channel in channels:
        suffix = channel.rsplit('.', 1)[-1]
        spans.append((categories[suffix], channel, offset, widths[suffix]))
        offset += widths[suffix]
    values = np.asarray(samples, dtype='<f4')
    if values.ndim != 2 or values.shape[1] != offset:
        raise ValueError('ACP sample width differs from channel layout')
    spans.sort(key=lambda item: item[0])
    channels = [item[1] for item in spans]
    values = np.concatenate([values[:, start:start+width] for _, _, start, width in spans], axis=1)
    out = bytearray()
    def u(x): out.extend(struct.pack('<I', x))
    def f(x): out.extend(struct.pack('<f', x))
    def s(x):
        b = x.encode('ascii'); u(len(b)); out.extend(b)
    s('AnimClipSamples'); s(name); u(18)
    # GH1 play bit 1 converts to GH2's one-beat alignment (0x1000), not
    # ordinary playback. It can seek past a short strum as soon as it starts.
    # Let the target driver choose looping and timing, like stock hand clips.
    time_flags = {0: 0, 0x1000: 1, 0x2000: 2, 0x4000: 4, 0x8000: 8, 0x200: 16, 0x400: 32}
    if play_flags not in time_flags: raise ValueError('Unsupported target clip time flags')
    f(0); f(duration); f(1); u(clip_flags); u(time_flags[play_flags]); f(blend_width); u(5)
    u(len(channels))
    for channel in channels: s(channel)
    u(len(samples)); u(0); u(0); u(0); u(0)
    out.extend(values.tobytes())
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(out)


def convert(source, output, scale, clip_match, overlay_matches=(), rebase_frames=None, isolate_transforms=(), clip_aliases=(), gh2_arm_axes=False, outfit='midori_1', clip_settings=None, hold_last_frame=False, pose_layers=(), locomotion=None):
    output.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((source / 'midori_source_ir_manifest.json').read_text())
    selected = next((item for item in manifest['outfits'] if item['name'] == outfit), None)
    if selected is None: raise ValueError(f'Source outfit not found: {outfit}')
    if len(selected['textures']) != 1:
        raise ValueError('Source outfit requires explicit multi-texture material routing')
    atlas = selected['textures'][0]
    bones = manifest['skeleton']['bones']
    lookup = checksum_name_map(bones)
    source_names = [resolved_bone_name(b, lookup) for b in bones]
    names = []
    for b, n in zip(bones, source_names):
        # A facial bone must never collapse onto the head, nor onto another bone.
        target = GH3_TO_GH2_BONES.get(n, n if n == 'Control_Root' else n.lower())
        if target in names or (target == 'bone_head' and n != 'Bone_Head'):
            target = 'bone_gh3_' + b['name_checksum'].removeprefix('0x')
        names.append(target)
    # GH3 hand targets are authored under separate fret/strum controls. Identify
    # those controls by hierarchy, not guessed checksums or bone-list positions.
    for target, control in [('bone_fret_hand', 'bone_fret'), ('bone_strum_hand', 'bone_strum')]:
        i = names.index(target)
        parent = bones[i]['parent_index']
        if parent is None or not 0 <= parent < len(bones):
            raise ValueError(f'Source hand target has no parent: {target}')
        names[parent] = control
    assert len(set(names)) == len(bones)
    rebases = {names.index(name): np.asarray(matrix, dtype=float)
               for name, matrix in (rebase_frames or {}).items()}
    if gh2_arm_axes:
        # GH2 IKElbow writes a Z-axis bend and expects positive-X limb lengths.
        # Rebase the frame, not the mesh or the authored world-space animation.
        for side in ['L', 'R']:
            fore_index = names.index(f'bone_{side}-foreArm')
            bend = Rotation.from_matrix(target_matrix(source_local(bones[fore_index]), scale)[:3, :3]).as_rotvec()
            if np.linalg.norm(bend) < 1e-5:
                raise ValueError('Cannot infer the source elbow plane from a straight bind joint')
            bend /= np.linalg.norm(bend)
            for parent, child in [('upperArm', 'foreArm'), ('foreArm', 'hand')]:
                i = names.index(f'bone_{side}-{parent}')
                j = names.index(f'bone_{side}-{child}')
                offset = target_matrix(source_local(bones[j]), scale)[3, :3]
                if np.linalg.norm(offset[1:]) > 1e-3:
                    raise ValueError('GH2 arm-axis conversion requires an X-aligned source limb')
                if i in rebases: raise ValueError('Duplicate arm-frame rebase')
                x = offset / np.linalg.norm(offset)
                z = bend - np.dot(bend, x) * x
                if np.linalg.norm(z) < 1e-5:
                    raise ValueError('Source bind rotation does not identify an elbow hinge')
                z /= np.linalg.norm(z)
                y = np.cross(z, x)
                matrix = np.eye(4); matrix[:3, :3] = np.array([x, y, z])
                rebases[i] = matrix
    for matrix in rebases.values():
        if matrix.shape != (4, 4) or not np.allclose(matrix[:, 3], [0, 0, 0, 1]):
            raise ValueError('Frame rebasing requires a row-affine 4x4 matrix')
        if not np.allclose(matrix[:3, :3] @ matrix[:3, :3].T, np.eye(3), atol=1e-6) or np.linalg.det(matrix[:3, :3]) < 0:
            raise ValueError('Frame rebasing requires a proper rigid rotation')
    rebase_channels = set(rebases) | {i for i, b in enumerate(bones) if b['parent_index'] in rebases}
    def rebase_local(matrices):
        result = [m.copy() for m in matrices]
        for i, b in enumerate(bones):
            if i in rebases: result[i] = rebases[i] @ result[i]
            if b['parent_index'] in rebases:
                result[i] = result[i] @ np.linalg.inv(rebases[b['parent_index']])
        return result
    rest = [source_local(b) for b in bones]
    source_worlds = worlds(rest, bones)
    bind_error = max(float(np.max(np.abs(w - np.linalg.inv(np.array(b['raw_matrix_rows_nxtools_order'])))))
                     for b, w in zip(bones, source_worlds))
    assert bind_error < 1e-5, bind_error
    transforms = {}
    target_rest = rebase_local([target_matrix(m, scale) for m in rest])
    target_worlds = worlds(target_rest, bones)
    for i, (b, name) in enumerate(zip(bones, names)):
        p = b['parent_index']
        transforms[name+'.mesh'] = dict(source_name=source_names[i], parent_name=names[p]+'.mesh' if p is not None and 0 <= p < len(bones) else '',
                                local=matrix12(target_rest[i]), world=matrix12(target_worlds[i]))
    # Preserve authored targets when attaching props that publish same-name
    # anchors. An identity parent keeps the exact pose and makes ownership explicit.
    for name in isolate_transforms:
        key = name + '.mesh'; trans = transforms[key]
        parent = trans['parent_name']; bridge = name + '_source_parent.mesh'
        transforms[bridge] = dict(source_name=bridge, parent_name=parent,
                                 local=matrix12(np.eye(4)), world=transforms[parent]['world'])
        trans['parent_name'] = bridge
    model = json.loads((source / selected['mesh_ir']['relative_path']).read_text())
    chunks, changes = [], []
    texture_name = Path(atlas['relative_path']).stem + '.tex'
    for mesh in model['meshes']:
        weights = [{w['bone']: w['weight'] for w in v['weights'] if w['weight'] > 0} for v in mesh['vertices']]
        # Resolve only palette-overflow faces, applying each change to the source
        # vertex globally so a chunk boundary cannot introduce a skinning seam.
        while True:
            over = next((t for t in mesh['triangles'] if len(set().union(*(weights[i] for i in t))) > 4), None)
            if over is None: break
            choices = [(sum(weights[i].get(b, 0) for i in over), b) for b in set().union(*(weights[i] for i in over))
                       if all(b not in weights[i] or len(weights[i]) > 1 for i in over)]
            if not choices: raise ValueError('Triangle cannot fit a four-bone palette')
            _, remove = min(choices)
            for i in over:
                if remove in weights[i]:
                    lost = weights[i].pop(remove)
                    total = sum(weights[i].values())
                    weights[i] = {b:w/total for b,w in weights[i].items()}
                    changes.append(dict(mesh=mesh['index'], vertex=i, bone=names[remove], removed_weight=lost))
        partitions = []
        for triangle in mesh['triangles']:
            palette = set().union(*(weights[i] for i in triangle))
            candidates = [(len(palette | p[0]), j) for j,p in enumerate(partitions) if len(palette | p[0]) <= 4]
            if candidates:
                _, j = min(candidates); partitions[j][0].update(palette); partitions[j][1].append(triangle)
            else: partitions.append([set(palette), [triangle]])
        for part, (palette, faces) in enumerate(partitions):
            palette = sorted(palette); indices = sorted(set(i for t in faces for i in t)); remap = {i:j for j,i in enumerate(indices)}
            vertices = []
            for i in indices:
                v = mesh['vertices'][i]
                vertices.append(dict(position=(np.array(v['position']) @ BASIS * (scale/256)).tolist(),
                    normal=(np.array(v['normal']) @ BASIS).tolist(), uv=[v['uv'][0], 1-v['uv'][1]],
                    color_or_weights=[weights[i].get(b, 0) for b in palette]+[0.]*(4-len(palette))))
            positions = np.array([v['position'] for v in vertices]); center = (positions.min(0)+positions.max(0))/2
            chunks.append(dict(name=f'midori_source_{mesh["index"]}_{part}.mesh', material='midori_source.mat', texture=texture_name,
                sphere=[*center.tolist(), float(np.linalg.norm(positions-center, axis=1).max())],
                bone_slots=[names[b]+'.mesh' for b in palette], bind_names=[names[b]+'.mesh' for b in palette], vertices=vertices,
                faces=[[remap[i] for i in t] for t in faces]))
    image = Image.open(source/atlas['relative_path'])
    texture = encode_hmx_texture(image)
    write_bundle(output/'midori.meshbundle', dict(id='gh3_'+outfit, package_name='community.gh3.midori'), chunks, transforms,
                 {texture_name:texture}, {'midori_source.mat':dict(alpha_cut=True, alpha_write=False, z_mode=1, cull=False, blend=1)})
    mask_checked = 0
    clip = None
    overlays = {}
    replacement_clips = {}
    with gzip.open(source/'animations/midori_ska_ir.jsonl.gz', 'rt') as f:
        for line in f:
            candidate = json.loads(line)
            source_tracks(candidate)
            mask_checked += candidate.get('partial_animation') is not None
            if clip_match in candidate['path']:
                if clip is not None: raise ValueError('Clip selector is ambiguous')
                clip = candidate
            for match in overlay_matches:
                if match in candidate['path']:
                    if match in overlays: raise ValueError('Overlay selector is ambiguous')
                    overlays[match] = candidate
            for number, layer in enumerate(pose_layers):
                if candidate['path'] == layer['source']:
                    if number in replacement_clips: raise ValueError('Duplicate replacement source')
                    replacement_clips[number] = candidate
    if clip is None: raise ValueError('Source clip not found')
    if set(overlays) != set(overlay_matches): raise ValueError('Source overlay not found')
    if len(replacement_clips) != len(pose_layers): raise ValueError('Source pose layer not found')
    owned_bones = set()
    for number, layer in enumerate(pose_layers):
        requested = set(layer['bones'])
        if not requested or requested - set(names): raise ValueError('Pose layer contains unknown or no bones')
        if requested & owned_bones: raise ValueError('Pose layers overlap')
        keyed_names = {names[i] for i in source_tracks(replacement_clips[number])}
        if requested - keyed_names: raise ValueError('Pose layer requests unkeyed source bones')
        owned_bones.update(requested)
    assert clip['header']['bone_count'] == len(bones)
    if clip['header']['flags'] & 0x200:
        # A partial hand layer must not acquire unrelated body channels merely
        # because the skeleton's coordinate frames were converted.
        rebase_channels &= set(source_tracks(clip))
    source_duration = clip['header']['duration_seconds']
    # GH3's zero-duration hand poses are persistent holds. GH2 players need a
    # positive interval to stay active; resampling clamped keys changes no pose.
    duration = source_duration if source_duration > 0 and not hold_last_frame else 1.0
    times = np.linspace(0, round(duration*60), round(duration*30)+1)
    qs, ts = {}, {}
    layer_specs = [(clip, None, {}), *((o, None, {}) for o in overlays.values())]
    layer_specs += [(replacement_clips[i], set(spec['bones']), spec) for i, spec in enumerate(pose_layers)]
    for layer, replacement_bones, layer_spec in layer_specs:
        if layer['header']['bone_count'] != len(bones): raise ValueError('Overlay skeleton size differs')
        delta_layer = bool(layer['header']['flags'] & 0x200)
        layer_times = times
        if layer is clip and hold_last_frame:
            layer_times = np.full_like(times, round(source_duration * 60))
        if layer is not clip:
            period = round(layer['header']['duration_seconds']*60)
            if period < 0: raise ValueError('Overlay has a negative duration')
            if period == 0:
                if any(k['time'] != 0 for b in layer['bones']
                       for k in [*b['quat_keys'], *b['trans_keys']]):
                    raise ValueError('Zero-duration overlay has moving keys')
                layer_times = np.zeros_like(times)
            else:
                layer_times = times % period
            if 'ping_pong_seconds' in layer_spec:
                cycle = float(layer_spec['ping_pong_seconds'])
                if not math.isfinite(cycle) or cycle <= 0 or period <= 0:
                    raise ValueError('Ping-pong requires positive cycle and source duration')
                phase = np.mod(times / 60 / cycle, 1.0)
                layer_times = (1.0 - np.abs(phase * 2.0 - 1.0)) * period
        for i,b in source_tracks(layer).items():
            if replacement_bones is not None:
                if names[i] not in replacement_bones: continue
                # Each replacement is an authored source pose relative to its
                # bind, not a delta to compound onto an unrelated hand pose.
                qs.pop(i, None)
                ts.pop(i, None)
            qkeys = {k['time']:k['raw_xyzw'] for k in b['quat_keys']}
            tkeys = {k['time']:k['raw_xyz'] for k in b['trans_keys']}
            if qkeys:
                if i in qs and not delta_layer: raise ValueError(f'Overlapping quaternion layer: {names[i]}')
                kt = sorted(qkeys)
                sampled = Slerp(kt, Rotation.from_quat([qkeys[t] for t in kt]))(np.clip(layer_times, kt[0], kt[-1])).as_quat() if len(kt)>1 else np.tile(qkeys[kt[0]], (len(times),1))
                if delta_layer:
                    base = qs.get(i, np.tile(bones[i]['raw_quat_xyzw'], (len(times),1)))
                    sampled = (Rotation.from_quat(base) * Rotation.from_quat(sampled)).as_quat()
                qs[i] = sampled
            if tkeys:
                if i in ts and not delta_layer: raise ValueError(f'Overlapping translation layer: {names[i]}')
                kt = sorted(tkeys); values=np.array([tkeys[t] for t in kt]); sampled=np.array([np.interp(layer_times,kt,values[:,c]) for c in range(3)]).T
                if delta_layer and i in ts: sampled += ts[i]
                ts[i] = sampled
    samples=[]; parity=0.; facing_samples=[]
    for frame in range(len(times)):
        local=[source_local(b, qs[i][frame] if i in qs else None, ts[i][frame] if i in ts else None) for i,b in enumerate(bones)]
        target=rebase_local([target_matrix(m,scale) for m in local])
        expected=[target_matrix(w,scale) for w in worlds(local,bones)]
        for i, matrix in rebases.items(): expected[i] = matrix @ expected[i]
        if locomotion is not None:
            if bones[0]['parent_index'] not in (None, -1): raise ValueError('Locomotion requires a root at index zero')
            facing = np.eye(4)
            facing[3,:3] = target[0][3,:3] - target_rest[0][3,:3]
            target[0][3,:3] = target_rest[0][3,:3]
            expected = [m @ np.linalg.inv(facing) for m in expected]
            facing_samples.append(facing[3,:3].copy())
        actual=worlds(target,bones)
        parity=max(parity,max(float(np.max(np.abs(a-e))) for a,e in zip(actual,expected)))
        if frame in {0, len(times)//2, len(times)-1}:
            expected_meshes = {}
            bone_index = {name+'.mesh': i for i,name in enumerate(names)}
            for chunk in chunks:
                positions = np.array([v['position']+[1.] for v in chunk['vertices']])
                weights = np.array([v['color_or_weights'] for v in chunk['vertices']])
                posed = np.zeros((len(positions), 3))
                for slot, name in enumerate(chunk['bone_slots']):
                    i = bone_index[name]
                    skin = np.linalg.inv(target_worlds[i]) @ expected[i]
                    posed += (positions @ skin)[:,:3] * weights[:,slot,None]
                expected_meshes[chunk['name']] = posed
            np.savez_compressed(output/f'expected-pose-{frame}.npz', **expected_meshes)
        row=[]
        for i in sorted(set(ts) | rebase_channels): row.extend(target[i][3,:3])
        for i in sorted(set(qs) | rebase_channels): row.extend(Rotation.from_matrix(target[i][:3,:3].T).as_quat())
        samples.append(row)
    channels=[names[i]+'.pos' for i in sorted(set(ts) | rebase_channels)]+[names[i]+'.quat' for i in sorted(set(qs) | rebase_channels)]
    if locomotion is not None:
        # GH3 translates Control_Root; GH2 CharWalk consumes virtual facing
        # channels and applies their delta to Character.local. Keep the skin
        # in place to avoid applying the same displacement twice.
        source_path = np.asarray(facing_samples)
        position = np.zeros(3)
        turn = math.radians(float(locomotion.get('turn_degrees', 0)))
        for frame, row in enumerate(samples):
            phase = frame / max(1, len(samples)-1)
            angle = turn * phase
            if frame:
                delta = source_path[frame] - source_path[frame-1]
                c, s = math.cos(angle), math.sin(angle)
                position += delta @ np.array([[c,s,0],[-s,c,0],[0,0,1]])
            row.extend([*position, angle])
        channels += ['bone_facing.pos', 'bone_facing.rotz']
    if parity > 1e-5:
        raise ValueError(f'Frame conversion changed an authored world transform: {parity}')
    # Sparse performance clips delegate absent arm chains to hand IK. Complete
    # authored arm motion must not be overwritten by those controllers.
    clip_flags = 0
    for side, flag in [('L', 0x00400000), ('R', 0x00800000)]:
        if all(names.index(f'bone_{side}-{part}') not in qs for part in ['upperArm','foreArm','hand']):
            clip_flags |= flag
    for alias in clip_aliases or ['idle', 'idle1', 'idle_medium_01', 'ui_loop', 'gh3_source_idle']:
        # Source delta hand strokes begin at note onset. A body-style crossfade
        # longer than the stroke's attack suppresses the authored wrist motion.
        blend_width = 0.0 if clip['header']['flags'] & 0x200 else .24
        settings = (clip_settings or {}).get(alias, {})
        # Target selection flags describe the call contract; arm ownership must
        # come from the source's actual keyed channels, never a donor clip.
        flags = (int(settings.get('flags', 0)) & ~0x00c00000) | clip_flags
        write_acp(output/'acp'/f'{alias}.acp', alias, channels, samples, duration,
                  flags, float(settings.get('blend_width', blend_width)), int(settings.get('play_flags', 0)))
    report=dict(status='conversion_probe_not_published',source_model=selected['skin_path'],source_outfit=outfit,
        source_skeleton=manifest['skeleton']['path'],source_clip=clip['path'],skeleton_unit_scale=scale,
        source_overlays=[c['path'] for c in overlays.values()],
        source_pose_layers=list(pose_layers),
        locomotion=locomotion,
        delta_layer_order='base local rotation @ source delta; requires source runtime validation',
        bones=len(bones), triangles=sum(len(c['faces']) for c in chunks), vertices=sum(len(c['vertices']) for c in chunks),
        chunks=len(chunks), bind_matrix_max_error=bind_error, animation_world_basis_max_error=parity,
        validated_partial_masks=mask_checked, exported_channels=len(channels),
        clip_flags=clip_flags,
        source_duration=source_duration, exported_duration=duration,
        frame_rebases={names[i]:m.tolist() for i,m in rebases.items()}, isolated_transforms=list(isolate_transforms),
        palette_weight_changes=changes, bone_names=[dict(source=s,target=t) for s,t in zip(source_names,names)],
        pending=['native skinning parity','GH2 animation call surface and hand IK','retail validation'])
    (output/'source-conversion-report.json').write_text(json.dumps(report,indent=2))
    print(json.dumps({k:v for k,v in report.items() if k not in ('bone_names','palette_weight_changes')},indent=2))


if __name__ == '__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--source',type=Path,required=True); parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--outfit',default='midori_1',help='Outfit name from the verified PS2 source manifest')
    parser.add_argument('--clip-settings',type=Path,help='Per-alias target selection flags and blend widths')
    parser.add_argument('--hold-last-frame',action='store_true',help='Hold the source terminal body pose while continuing overlay layers')
    parser.add_argument('--pose-layers',type=Path,help='Explicit disjoint source bone-channel replacements')
    parser.add_argument('--locomotion',type=Path,help='Extract source root motion into GH2 facing channels; optional procedural turn angle')
    parser.add_argument('--skeleton-unit-scale',type=float,default=100/2.54)
    parser.add_argument('--clip',default='gh3_guit_mido_a_med_idle01.ska.ps2')
    parser.add_argument('--overlay', action='append', default=[], help='Loop a disjoint source facial/accessory layer into this clip')
    parser.add_argument('--rebase-frames',type=Path,help='JSON of bone names to rigid row-affine frame changes; compensates all children')
    parser.add_argument('--isolate-transform',action='append',default=[],help='Preserve target ownership through an identity parent')
    parser.add_argument('--clip-alias',action='append',default=[],help='Target animation call name; repeat for equivalent calls')
    parser.add_argument('--gh2-arm-axes',action='store_true',help='Derive positive-X arm frames and the GH2 Z hinge from the source bind pose')
    args=parser.parse_args(); convert(args.source,args.output,args.skeleton_unit_scale,args.clip,args.overlay,
        json.loads(args.rebase_frames.read_text()) if args.rebase_frames else None, args.isolate_transform,args.clip_alias,args.gh2_arm_axes,args.outfit,
        json.loads(args.clip_settings.read_text()) if args.clip_settings else None, args.hold_last_frame,
        json.loads(args.pose_layers.read_text()) if args.pose_layers else (),
        json.loads(args.locomotion.read_text()) if args.locomotion else None)
