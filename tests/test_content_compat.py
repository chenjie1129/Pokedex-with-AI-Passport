"""Authenticated pack activation must preserve current schema-12 player data."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import pokedex_content_pack as pack
import encode_pokedex_content as typed
CLI = sys.argv.pop(1)


class CompatibilityTests(unittest.TestCase):
    def test_real_save_and_signed_pack_lifecycle(self):
        with tempfile.TemporaryDirectory(prefix='city-save-compat-') as directory:
            root = Path(directory)
            key = root / 'private.pem'; rawkey = root / 'public.bin'
            pack.openssl('genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:P-256', '-out', key)
            rawkey.write_bytes(pack.check_key(key, private=True, algorithm=pack.P256))
            source = json.loads((ROOT / 'content/species.json').read_text())['species']
            rows = []
            for item in source:
                if item['id'] not in (1, 2, 25, 60000):
                    continue
                rows.append(dict(species_id=item['id'], evolves_from=item.get('evolves_from', 0),
                                 flags=int(item['wild']) | int(item.get('place', not item.get('evolves_from', 0))) * 2 |
                                 (4 if item['id'] >= 60000 else 0), pool=item['pool'], types=item['types'],
                                 hp=item['hp'], attack=item['attack'], defense=item['defense'],
                                 name_en=item['name'], name_zh='测试', description_en=item['description'], description_zh='测试内容。'))
            changed_stats = copy.deepcopy(rows); next(r for r in changed_stats if r['species_id'] == 1)['hp'] += 1
            missing_evolution = [r for r in rows if r['species_id'] != 2]
            changed_types = copy.deepcopy(rows); next(r for r in changed_types if r['species_id'] == 1)['types'] = ['Grass', 'Fire']
            missing_owned = [r for r in rows if r['species_id'] != 25]
            cases = [[r for r in rows if r['species_id'] == 25], rows, changed_stats,
                     missing_evolution, changed_types, missing_owned]
            paths = []
            for i, entries in enumerate(cases):
                target = root / str(i); target.mkdir(); objects = []
                for row in entries:
                    for kind, data in ((1, typed.metadata(row)), (2, typed.sprite(1, 1, bytes(3))), (3, typed.cry(bytes(2)))):
                        name = '%d-%d' % (row['species_id'], kind); (target / name).write_bytes(data)
                        objects.append(dict(species_id=row['species_id'], kind=kind, path=name))
                recipe = target / 'recipe.json'
                recipe.write_text(json.dumps(dict(revision=min(i + 1, 3), objects=objects)))
                out = target / 'content.pack'; pack.build_pack(recipe, key, out, pack.P256); paths.append(out)
            result = subprocess.run([CLI, str(rawkey), *map(str, paths)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main()
