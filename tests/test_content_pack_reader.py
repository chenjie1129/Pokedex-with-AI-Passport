"""Python publisher -> actual C reader -> OpenSSL host crypto adapter."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import pokedex_content_pack as pack
import encode_pokedex_content as typed

READER = sys.argv.pop(1)


def row(sid=1, **changes):
    value = dict(species_id=sid, flags=2, pool=0, types=['Grass'], hp=45, attack=40,
                 defense=50, name_en='Fixture', name_zh='测试', description_en='An offline fixture.',
                 description_zh='测试内容。')
    value.update(changes)
    return value


class ReaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='city-reader-')
        cls.root = Path(cls.temp.name)
        cls.key = cls.root / 'private.pem'; cls.public = cls.root / 'public.pem'
        pack.openssl('genpkey', '-algorithm', 'ED25519', '-out', cls.key)
        cls.public.write_bytes(pack.openssl('pkey', '-in', cls.key, '-pubout'))
        cls.number = 0

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def fixture(self, count=1, mutations=None, rows=None, large=False):
        type(self).number += 1
        root = self.root / str(self.number); root.mkdir()
        objects = []
        source_rows = rows or [row(sid) for sid in range(1, count + 1)]
        image = typed.sprite(110, 110, b'\xff' * (110 * 110 * 3)) if large else typed.sprite(1, 1, b'\xff\x07\xff')
        sound = typed.cry(b'\x01\0' * (32000 if large else 1))
        (root / 'sprite').write_bytes(image); (root / 'cry').write_bytes(sound)
        for value in source_rows:
            sid = value['species_id']
            for kind, data in [(1, typed.metadata(value)), (2, image), (3, sound)]:
                if mutations:
                    data = mutations(sid, kind, data)
                name = str(sid) + '-' + str(kind) if kind == 1 or mutations else ('sprite' if kind == 2 else 'cry')
                if kind == 1 or mutations:
                    (root / name).write_bytes(data)
                objects.append(dict(species_id=sid, kind=kind, path=name))
        recipe = root / 'recipe.json'; recipe.write_text(json.dumps(dict(revision=1, objects=objects)))
        output = root / 'fixture.pack'; pack.build_pack(recipe, self.key, output)
        return output

    def run_reader(self, path, ok=True, fault=None):
        result = subprocess.run([READER, str(path), str(self.public)] + ([fault] if fault else []),
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0 if ok else 1, result.stderr + result.stdout)
        report = json.loads(result.stdout)
        self.assertEqual(report['ok'], ok)
        self.assertLessEqual(report['largest_read'], 512)
        return report

    def test_roundtrip_unicode_sparse_ids_and_evolution(self):
        path = self.fixture(rows=[row(1), row(2, evolves_from=1, flags=0), row(60000, flags=6)])
        self.assertEqual(self.run_reader(path)['species'], 3)

    def test_signed_invalid_typed_objects_are_rejected(self):
        def byte_at(kind, index, value):
            def mutate(sid, actual, data):
                if actual == kind:
                    data = bytearray(data); data[index] = value; return data
                return data
            return mutate
        cases = [(1, 4, 2), (1, 6, 9), (1, 10, 128), (1, 11, 3),
                 (1, 12, 19), (1, 14, 0), (1, 17, 1), (1, 18, 255),
                 (1, 26, 0), (1, 26, 0xc0), (1, 26, 0xff),
                 (2, 4, 111), (2, 8, 2), (2, 9, 1), (3, 4, 1), (3, 6, 2), (3, 7, 2)]
        for kind, index, value in cases:
            with self.subTest(kind=kind, index=index):
                path = self.fixture(mutations=byte_at(kind, index, value))
                pack.verify_pack(path, self.public)  # Authentic, but semantically unusable.
                self.run_reader(path, False)
        self.run_reader(self.fixture(mutations=lambda sid, kind, data: data + b'\0'), False)

    def test_missing_parent_and_cycles_are_rejected(self):
        self.run_reader(self.fixture(rows=[row(2, evolves_from=1, flags=0)]), False)
        self.run_reader(self.fixture(rows=[row(1, evolves_from=2, flags=0), row(2, evolves_from=1, flags=0)]), False)
        self.run_reader(self.fixture(rows=[row(1), row(2, evolves_from=1, flags=0),
                                                  row(3, evolves_from=2, flags=0)]), False)

    def test_faults_never_publish_a_reader_or_read_unauthenticated_payload(self):
        path = self.fixture()
        for fault in ('read', 'hash', 'hash-update', 'hash-finish', 'signature', 'no-crypto', 'budget'):
            with self.subTest(fault=fault):
                report = self.run_reader(path, False, fault)
                self.assertEqual(report['payload_reads'], 0)
                self.assertEqual(report['species'], 0)
        wrong_private = self.root / 'wrong-private.pem'
        wrong_public = self.root / 'wrong-public.pem'
        pack.openssl('genpkey', '-algorithm', 'ED25519', '-out', wrong_private)
        wrong_public.write_bytes(pack.openssl('pkey', '-in', wrong_private, '-pubout'))
        result = subprocess.run([READER, str(path), str(wrong_public)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(json.loads(result.stdout)['payload_reads'], 0)

    def test_corruption_truncation_unsigned_unknown_algorithm_and_bounds(self):
        path = self.fixture(); original = path.read_bytes()
        sig_at = pack.HEADER.size + 3 * pack.ENTRY.size
        for index in (0, 8, 10, 16, 24, pack.HEADER.size, sig_at, len(original) - 1):
            data = bytearray(original); data[index] ^= 1; path.write_bytes(data)
            self.run_reader(path, False)
        for data in (b'', original[:10], original[:-1], original + b'\0',
                     original[:sig_at] + bytes(64) + original[sig_at + 64:]):
            path.write_bytes(data); self.run_reader(path, False)

    def test_large_objects_and_catalogs_have_bounded_reads_and_rows(self):
        for count in (100, 1000, 10000):
            path = self.fixture(count=count)
            report = self.run_reader(path)
            self.assertEqual(report['species'], count)
            self.assertLess(report['handle_bytes'], 128)
            self.assertLess(report['row_bytes'] * 4, 1600)
            print(json.dumps(report))
        self.assertEqual(self.run_reader(self.fixture(large=True))['largest_read'], 512)

    def test_preparation_tool_and_text_bounds(self):
        root = self.root / 'prepare'; root.mkdir()
        (root / 'pixels').write_bytes(bytes(3)); (root / 'pcm').write_bytes(bytes(2))
        value = row(sprite_rgb565a8='pixels', cry_pcm='pcm', width=1, height=1)
        source = root / 'source.json'; source.write_text(json.dumps(dict(revision=1, species=[value])))
        typed.prepare(source, root / 'objects')
        output = root / 'prepared.pack'
        pack.build_pack(root / 'objects/recipe.json', self.key, output)
        self.run_reader(output)
        for changes in ({'name_en': 'x' * 33}, {'name_zh': '\0'}, {'hp': True},
                        {'species_id': 60000}, {'types': ['Grass', 'Grass']}):
            with self.subTest(changes=changes), self.assertRaises(pack.PackError):
                typed.metadata(row(**changes))


if __name__ == '__main__':
    unittest.main()
