#!/usr/bin/env python3
"""Rebuild a reviewable PS2-source playing probe from an explicit asset recipe.

This does not install DLC. --work is disposable conversion scratch, --output is
the candidate package directory. Keep the source recipe with review evidence.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def validate_recipe_contract(recipe, contract):
    aliases = {}
    for item in recipe['animations']:
        known = aliases.setdefault(item['role'], set())
        for name in item['aliases']:
            if name in known: raise ValueError(f'Duplicate {item["role"]} call: {name}')
            known.add(name)
    for role, required in contract.get('banks', {}).items():
        known = aliases.get(role, set())
        missing = set(required.get('clips', [])) - known
        if missing: raise ValueError(f'Missing required {role} calls: {sorted(missing)}')
        groups = recipe.get('groups', {}).get(role, {})
        for group in required.get('groups', []):
            members = groups.get(group, [])
            if not members or set(members) - known:
                raise ValueError(f'Missing or unplayable required {role} group: {group}')


def build(source, converter, recipe_path, work, output):
    recipe = json.loads(recipe_path.read_text())
    contract = json.loads((recipe_path.parent / recipe['call_contract']).read_text()) if recipe.get('call_contract') else {}
    validate_recipe_contract(recipe, contract)
    if len(recipe['manifest']['characters']) != 1:
        raise ValueError('One source character per conversion recipe is required')
    character_id = recipe['manifest']['characters'][0]['id']
    model_prefix = recipe.get('model_prefix', 'gh3_')
    outfit = recipe.get('outfit', 'midori_1')
    work.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.update(PYTHONDONTWRITEBYTECODE='1', OPENBLAS_NUM_THREADS='1', OMP_NUM_THREADS='1')
    tool = Path(__file__).with_name('gh3_ps2_source_character.py')
    flags = (subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS) if os.name == 'nt' else 0

    def run(args, name):
        with (work / (name + '.log')).open('w') as log:
            result = subprocess.run(list(map(str, args)), stdout=log, stderr=subprocess.STDOUT,
                                    env=env, creationflags=flags, check=False)
        if result.returncode:
            raise RuntimeError(f'{name} failed ({result.returncode}); see {work / (name + ".log")}')

    banks = {}
    model_bundles = {}
    for number, item in enumerate(recipe['animations']):
        folder = work / f'clip-{number:02d}'
        args = [sys.executable, tool, '--source', source, '--output', folder,
                '--clip', item['source'], '--gh2-arm-axes', '--outfit', outfit]
        if item.get('frames'):
            frames = work / f'frames-{number:02d}.json'
            frames.write_text(json.dumps(item['frames']))
            args += ['--rebase-frames', frames]
        for value in item.get('overlays', []): args += ['--overlay', value]
        for value in item.get('isolate', []): args += ['--isolate-transform', value]
        for value in item['aliases']: args += ['--clip-alias', value]
        if item.get('hold_last_frame'): args += ['--hold-last-frame']
        if item.get('cycle_hold_overlays'): args += ['--cycle-hold-overlays']
        if item.get('pose_layers'):
            layers = work / f'layers-{number:02d}.json'
            layers.write_text(json.dumps(item['pose_layers']))
            args += ['--pose-layers', layers]
        if 'locomotion' in item:
            motion = work / f'locomotion-{number:02d}.json'
            motion.write_text(json.dumps(item['locomotion']))
            args += ['--locomotion', motion]
        if item.get('settings'):
            settings = work / f'settings-{number:02d}.json'
            settings.write_text(json.dumps(item['settings']))
            args += ['--clip-settings', settings]
        run(args, f'clip-{number:02d}')
        bank = work / ('bank-' + item['role'])
        bank.mkdir(exist_ok=True)
        for path in (folder / 'acp').glob('*.acp'):
            target = bank / path.name
            if target.exists(): raise ValueError(f'Duplicate animation alias: {target.name}')
            shutil.copyfile(path, target)
        banks[item['role']] = bank
        if item.get('model'):
            shutil.copyfile(folder / 'midori.meshbundle', work / 'model.meshbundle')
            model_bundles[outfit] = work / 'model.meshbundle'
            for extra in recipe.get('additional_outfits', []):
                if extra == outfit or extra in model_bundles:
                    raise ValueError('Duplicate additional outfit')
                extra_folder = work / ('model-' + extra)
                extra_args = list(args)
                extra_args[extra_args.index('--outfit')+1] = extra
                extra_args[extra_args.index('--output')+1] = extra_folder
                run(extra_args, 'model-source-' + extra)
                model_bundles[extra] = extra_folder / 'midori.meshbundle'
    content = output / 'content'
    bank_root = content / f'char/{character_id}/anims/gen'
    bank_root.mkdir(parents=True, exist_ok=True)
    transition_path = work / 'movement-transitions.tsv'
    has_locomotion = any('locomotion' in a for a in recipe['animations'])
    if has_locomotion:
        run([sys.executable, tool.with_name('build_gh2_locomotion_graph.py'),
             '--bank', banks['main'], '--recipe', recipe_path,
             '--output', transition_path], 'movement-transitions')
    for role, folder in banks.items():
        bank_args = [converter, 'build-clipset-from-acp', folder, '--name', character_id + '_' + role,
             '--role', 'guitar-' + role, '--out', bank_root / (character_id + '_' + role + '.milo_ps2'),
             '--move-self', '1' if role == 'main' and any('locomotion' in a for a in recipe['animations']) else '0']
        for name, members in recipe.get('groups', {}).get(role, {}).items():
            bank_args += ['--group', name + '=' + ','.join(members)]
        if role == 'main' and has_locomotion:
            bank_args += ['--transitions', transition_path]
        run(bank_args, role + '-bank')
    # Explicit probe limitation: the reviewed bank is also the UI placeholder.
    if 'ui' not in banks:
        shutil.copyfile(bank_root / (character_id + '_main.milo_ps2'), bank_root / (character_id + '_ui.milo_ps2'))
    if not model_bundles: raise ValueError('Recipe has no model source')
    for model_outfit, bundle in model_bundles.items():
        model_id = model_prefix + model_outfit
        model = content / f'char/{model_id}/og/gen/{model_id}.milo_ps2'
        model.parent.mkdir(parents=True, exist_ok=True)
        args = [converter, 'build-character-from-meshbundle', bundle,
                '--name', model_id, '--out', model, '--preserve-guitar-proxies']
        for role in ['main', 'strum', 'fret']:
            args += ['--' + role + '-anim', f'char/{character_id}/anims/gen/{character_id}_{role}.milo_ps2']
        run(args, 'model-' + model_outfit)
    manifest = recipe['manifest']
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2))
    index = {'files': []}
    for name in manifest['files']:
        data = (content / name).read_bytes()
        index['files'].append(dict(path=name, size=len(data), sha256=hashlib.sha256(data).hexdigest()))
    (output / 'content-index.json').write_text(json.dumps(index, indent=2))
    print(f'Built unapproved playing probe: {output}')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ['source', 'converter', 'recipe', 'work', 'output']:
        p.add_argument('--' + name, type=Path, required=True)
    a = p.parse_args()
    build(a.source, a.converter, a.recipe, a.work, a.output)
