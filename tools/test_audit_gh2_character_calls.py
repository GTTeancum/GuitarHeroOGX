import unittest

from audit_gh2_character_calls import compare


class CallInventoryTests(unittest.TestCase):
    def test_clip_alias_does_not_replace_group_dispatch(self):
        result = compare({'special_01': {}}, {'star_power': {'members': ['special_01']}},
                         {'special_01': {}}, {})
        self.assertEqual(result['missing_clip_names'], [])
        self.assertEqual(result['missing_group_names'], ['star_power'])

    def test_existing_group_must_resolve_members(self):
        result = compare({}, {}, {}, {'normal': {'members': ['missing']}})
        self.assertEqual(result['dangling_groups'], {'normal': ['missing']})

    def test_empty_group_does_not_count_as_playable(self):
        result = compare({}, {}, {}, {'normal': {'members': []}})
        self.assertEqual(result['empty_groups'], ['normal'])

    def test_replaced_motion_is_exposed_for_semantic_review(self):
        result = compare({'special_01': {}}, {'star_power': {'members': ['special_01']}},
                         {'idle': {}}, {'star_power': {'members': ['idle']}})
        self.assertEqual(result['groups']['star_power']['candidate_members'], ['idle'])
        self.assertEqual(result['missing_clip_names'], ['special_01'])


if __name__ == '__main__':
    unittest.main()
