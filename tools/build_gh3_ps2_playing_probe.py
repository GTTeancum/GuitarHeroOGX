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


def build(source, converter, recipe_path, work, output):
    recipe = json.loads(recipe_path.read_text())
    outfit = recipe.get('outfit', 'midori_1')
    model_id = 'gh3_' + outfit
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
    content = output / 'content'
    bank_root = content / 'char/gh3_midori/anims/gen'
    bank_root.mkdir(parents=True, exist_ok=True)
    for role, folder in banks.items():
        run([converter, 'build-clipset-from-acp', folder, '--name', 'gh3_midori_' + role,
             '--role', 'guitar-' + role, '--out', bank_root / ('gh3_midori_' + role + '.milo_ps2'),
             '--move-self', '0'], role + '-bank')
    # Explicit probe limitation: the reviewed bank is also the UI placeholder.
    if 'ui' not in banks:
        shutil.copyfile(bank_root / 'gh3_midori_main.milo_ps2', bank_root / 'gh3_midori_ui.milo_ps2')
    model = content / f'char/{model_id}/og/gen/{model_id}.milo_ps2'
    model.parent.mkdir(parents=True, exist_ok=True)
    args = [converter, 'build-character-from-meshbundle', work / 'model.meshbundle',
            '--name', model_id, '--out', model, '--preserve-guitar-proxies']
    for role in ['main', 'strum', 'fret']:
        args += ['--' + role + '-anim', f'char/gh3_midori/anims/gen/gh3_midori_{role}.milo_ps2']
    run(args, 'model')
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
