#!/usr/bin/env python3
"""Audit a DLC character against a stock GH2 bank contract, without extracting it.

Name coverage is necessary, not proof of matching motion or runtime dispatch.
Stock variants are a conservative inventory, not all mandatory direct calls.
"""
import argparse
import hashlib
import json
from pathlib import Path

import re_anim_audit as parser
from gh3_midori_animation_call_compatibility import bank_records


def compare(stock_clips, stock_groups, clips, groups):
    return {
        'stock_clip_count': len(stock_clips),
        'covered_clip_names': sorted(set(stock_clips) & set(clips)),
        'missing_clip_names': sorted(set(stock_clips) - set(clips)),
        'missing_group_names': sorted(set(stock_groups) - set(groups)),
        'empty_groups': sorted(n for n, g in groups.items() if not g['members']),
        'dangling_groups': {n: sorted(set(g['members']) - set(clips))
                            for n, g in groups.items() if set(g['members']) - set(clips)},
        'groups': {n: {'stock_members': g['members'],
                       'candidate_members': groups.get(n, {}).get('members', [])}
                   for n, g in stock_groups.items()},
    }


def build(args):
    package = args.package.resolve()
    manifest = json.loads((package / 'manifest.json').read_text())
    characters = [c for c in manifest['characters'] if c['id'] == args.character]
    if len(characters) != 1:
        raise ValueError('Select exactly one character from the package manifest')
    entries = {e.full_path.casefold(): e for e in parser.ark_entries(args.stock_hdr)}
    content = (package / manifest.get('content_root', 'content')).resolve()
    banks = {}
    for outfit in characters[0]['outfits']:
        for role in ('main', 'ui', 'fret', 'strum'):
            relative = outfit[role + '_anim']
            key = role + ':' + relative
            if key in banks:
                banks[key]['outfits'].append(outfit['selection'])
                continue
            candidate_path = (content / relative).resolve()
            if not candidate_path.is_relative_to(content):
                raise ValueError('Bank path escapes content root')
            stock_path = f'char/{args.stock_character}/anims/gen/{args.stock_character}_{role}.milo_ps2'
            stock = parser.read_ark_entry(args.stock_ark, entries[stock_path.casefold()])
            candidate = candidate_path.read_bytes()
            sc, sg, sd, sgd = bank_records(parser, stock)
            cc, cg, cd, cgd = bank_records(parser, candidate)
            bank = compare(sc, sg, cc, cg)
            bank.update(role=role, stock_path=stock_path, candidate_path=relative,
                        stock_sha256=hashlib.sha256(stock).hexdigest(),
                        candidate_sha256=hashlib.sha256(candidate).hexdigest(),
                        outfits=[outfit['selection']],
                        duplicate_names={'stock_clips': sd, 'stock_groups': sgd,
                                         'candidate_clips': cd, 'candidate_groups': cgd})
            banks[key] = bank
    incomplete = any(b['missing_clip_names'] or b['missing_group_names'] or
                     b['empty_groups'] or b['dangling_groups'] or
                     any(b['duplicate_names'].values()) for b in banks.values())
    mappings = []
    for path in args.recipe:
        recipe = json.loads(path.read_text())
        mappings.extend({'recipe': path.name, 'role': a['role'],
                         'source': a['source'], 'target_aliases': a['aliases'],
                         'semantic_verification': 'not_established_by_name_audit'}
                        for a in recipe['animations'])
    return {
        'status': 'incomplete_call_inventory' if incomplete else 'name_inventory_covered_only',
        'character': args.character, 'stock_character': args.stock_character,
        'banks': banks, 'declared_mappings': mappings,
        'fully_compatible': False,
        'remaining_verification': [
            'Determine required runtime calls versus optional stock variants from GH2 dispatch contracts',
            'Verify source action semantics for each target call and group',
            'Verify selection flags, tempo, looping, transitions, events and layer ownership',
            'Exercise actual dispatch with runtime traces and visual proof',
        ],
    }


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('stock-hdr', 'stock-ark', 'package', 'output'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--character', required=True)
    p.add_argument('--stock-character', default='rock1')
    p.add_argument('--recipe', type=Path, action='append', default=[])
    args = p.parse_args()
    report = build(args)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'status': report['status'], 'fully_compatible': False,
                      'banks': len(report['banks']), 'output': str(args.output)}))
    return 1 if report['status'] == 'incomplete_call_inventory' else 0


if __name__ == '__main__':
    raise SystemExit(main())
