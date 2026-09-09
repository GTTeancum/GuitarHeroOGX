import unittest
from build_gh3_ps2_playing_probe import validate_recipe_contract


class RecipeContractTests(unittest.TestCase):
    def test_required_dispatch_rejects_missing_calls_and_empty_or_dangling_groups(self):
        contract = {'banks': {'main': {'groups': ['solo']}, 'fret': {'clips': ['hold']}}}
        recipe = {'animations': [{'role': 'main', 'aliases': ['source_solo']}], 'groups': {'main': {'solo': ['source_solo']}}}
        with self.assertRaisesRegex(ValueError, 'Missing required fret'):
            validate_recipe_contract(recipe, contract)
        recipe['animations'].append({'role': 'fret', 'aliases': ['hold']})
        validate_recipe_contract(recipe, contract)
        for members in ([], ['absent']):
            recipe['groups']['main']['solo'] = members
            with self.assertRaisesRegex(ValueError, 'unplayable required main group'):
                validate_recipe_contract(recipe, contract)

    def test_duplicate_alias_fails_before_conversion(self):
        recipe = {'animations': [{'role': 'main', 'aliases': ['pose']}, {'role': 'main', 'aliases': ['pose']}]}
        with self.assertRaisesRegex(ValueError, 'Duplicate main call'):
            validate_recipe_contract(recipe, {})


if __name__ == '__main__': unittest.main()
