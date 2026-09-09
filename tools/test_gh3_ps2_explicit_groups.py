"""Run with MILO_CONVERT_TOOL pointing to the built converter."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from gh3_ps2_source_character import write_acp
from gh3_midori_animation_call_compatibility import bank_records
import re_anim_audit as parser


@unittest.skipUnless(os.environ.get('MILO_CONVERT_TOOL'), 'MILO_CONVERT_TOOL not set')
class ExplicitGroupTests(unittest.TestCase):
    def test_transition_table_rejects_dangling_and_out_of_range_nodes(self):
        with tempfile.TemporaryDirectory(prefix='gh3-transition-test-') as temp:
            root = Path(temp)
            for name in ('stand', 'turn'):
                write_acp(root/'acp'/(name+'.acp'), name, ['bone_pelvis.quat'],
                          [[0, 0, 0, 1], [0, 0, 0, 1]], 1)
            table = root/'edges.tsv'
            args = [os.environ['MILO_CONVERT_TOOL'], 'build-clipset-from-acp',
                    str(root/'acp'), '--name', 'test', '--role', 'guitar-main',
                    '--out', str(root/'bank.milo_ps2'), '--transitions', str(table)]
            for row in ('stand missing 1 0', 'stand turn 2 0', 'stand turn 0 -1',
                        'stand turn nan 0', 'stand turn 0 0 extra'):
                table.write_text(row)
                with self.subTest(row=row):
                    result = subprocess.run(args, capture_output=True)
                    self.assertNotEqual(result.returncode, 0)
            table.write_text('stand turn 1 0\n')
            result = subprocess.run(args, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_virtual_facing_channels_are_not_generated_as_charbones(self):
        with tempfile.TemporaryDirectory(prefix='gh3-facing-test-') as temp:
            root = Path(temp)
            write_acp(root / 'acp/walk.acp', 'walk',
                      ['Control_Root.quat', 'bone_facing.pos', 'bone_facing.rotz'],
                      [[0,0,0,1,0,0,0,0], [0,0,0,1,0,10,0,1]], 1)
            output = root / 'bank.milo_ps2'
            result = subprocess.run([os.environ['MILO_CONVERT_TOOL'],
                'build-clipset-from-acp', str(root / 'acp'), '--name', 'test',
                '--role', 'guitar-main', '--move-self', '1', '--out', str(output)],
                capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            payload = parser.inflate_milo(output.read_bytes())
            entries = parser.parse_dir(payload)[3]
            self.assertFalse(any(e.typ == 'CharBone' and e.name.startswith('bone_facing')
                                 for e in entries))

    def test_group_contract_rejects_unplayable_members(self):
        with tempfile.TemporaryDirectory(prefix='gh3-group-test-') as temp:
            root = Path(temp)
            write_acp(root / 'acp/stroke.acp', 'stroke', ['bone_R-hand.quat'],
                      [[0, 0, 0, 1], [0, 0, 0, 1]], 1)
            output = root / 'bank.milo_ps2'
            args = [os.environ['MILO_CONVERT_TOOL'], 'build-clipset-from-acp',
                    str(root / 'acp'), '--name', 'test', '--role', 'guitar-strum',
                    '--out', str(output)]
            for spec in ['action=missing', 'action=', 'action=stroke,stroke']:
                with self.subTest(spec=spec):
                    result = subprocess.run(args + ['--group', spec], capture_output=True)
                    self.assertNotEqual(result.returncode, 0)
            result = subprocess.run(args + ['--group', 'action=stroke'], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            _, groups, _, _ = bank_records(parser, output.read_bytes())
            self.assertEqual(groups['action']['members'], ['stroke'])
            self.assertNotIn('normal', groups)
            result = subprocess.run(args + ['--group', 'action=stroke',
                                           '--group', 'action=stroke'], capture_output=True)
            self.assertNotEqual(result.returncode, 0)


if __name__ == '__main__':
    unittest.main()
