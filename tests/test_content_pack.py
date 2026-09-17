"""Actual signatures, malformed containers and bounded host verification."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import tracemalloc
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import pokedex_content_pack as pack


class ContentPackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='city-pack-test-')
        cls.root = Path(cls.temp.name)
        cls.private = cls.root / 'test-only-private.pem'
        cls.public = cls.root / 'test-only-public.pem'
        pack.openssl('genpkey', '-algorithm', 'ED25519', '-out', cls.private)
        cls.public.write_bytes(pack.openssl('pkey', '-in', cls.private, '-pubout'))
        cls.wrong_private = cls.root / 'wrong-private.pem'
        cls.wrong_public = cls.root / 'wrong-public.pem'
        pack.openssl('genpkey', '-algorithm', 'ED25519', '-out', cls.wrong_private)
        cls.wrong_public.write_bytes(pack.openssl('pkey', '-in', cls.wrong_private, '-pubout'))
        (cls.root / 'object').write_bytes(b'synthetic')

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def make_pack(self, name, count=1):
        recipe = self.root / (name + '.json')
        recipe.write_text(json.dumps({'revision': 7, 'objects': [
            {'species_id': sid, 'kind': kind, 'path': 'object'}
            for sid in range(1, count + 1) for kind in (1, 2, 3)]}))
        output = self.root / (name + '.pack')
        pack.build_pack(recipe, self.private, output)
        return output

    def test_signed_round_trip_and_no_overwrite(self):
        path = self.make_pack('roundtrip')
        result = pack.verify_pack(path, self.public)
        self.assertEqual((result['species'], result['objects']), (1, 3))
        self.assertFalse(result['device_compatible'])
        original = path.read_bytes()
        repeat = self.root / 'repeat.pack'
        pack.build_pack(self.root / 'roundtrip.json', self.private, repeat)
        self.assertEqual(repeat.read_bytes(), original)
        with self.assertRaises(pack.PackError):
            pack.build_pack(self.root / 'roundtrip.json', self.private, path)
        self.assertEqual(path.read_bytes(), original)

    def test_wrong_key_unsigned_truncated_trailing_and_size_budget(self):
        path = self.make_pack('invalid')
        original = path.read_bytes()
        with self.assertRaises(pack.PackError):
            pack.verify_pack(path, self.wrong_public)
        with self.assertRaises(pack.PackError):
            pack.verify_pack(path, self.public, len(original) - 1)
        sig = pack.HEADER.size + 3 * pack.ENTRY.size
        variants = [b'', original[:pack.HEADER.size - 1], original[:-1],
                    original + b'\0', original[:sig] + bytes(64) + original[sig + 64:]]
        bad = self.root / 'bad.pack'
        for content in variants:
            bad.write_bytes(content)
            with self.assertRaises(pack.PackError):
                pack.verify_pack(bad, self.public)

    def test_every_byte_mutation_of_small_pack_is_rejected(self):
        data = bytearray(self.make_pack('mutation').read_bytes())
        path = self.root / 'mutated.pack'
        for index in range(len(data)):
            data[index] ^= 1
            path.write_bytes(data)
            with self.subTest(byte=index), self.assertRaises(pack.PackError):
                pack.verify_pack(path, self.public)
            data[index] ^= 1

    def test_valid_signature_does_not_bypass_structural_validation(self):
        original = self.make_pack('structure').read_bytes()
        start = pack.HEADER.size
        signature_at = start + 3 * pack.ENTRY.size
        bad = self.root / 'signed-malformed.pack'
        # Sign malformed metadata with the trusted key; these must still fail.
        mutations = [(0, 0), (1, 2), (2, 1), (3, 1), (4, 0), (4, pack.MAX_OBJECT_BYTES + 1)]
        for field, value in mutations:
            raw = bytearray(original)
            entry = list(pack.ENTRY.unpack_from(raw, start))
            entry[field] = value
            raw[start:start + pack.ENTRY.size] = pack.ENTRY.pack(*entry)
            digest = hashlib.sha256(raw[:signature_at]).digest()
            raw[signature_at:signature_at + 64] = pack.signature_operation(digest, self.private)
            bad.write_bytes(raw)
            with self.subTest(field=field, value=value), self.assertRaises(pack.PackError):
                pack.verify_pack(bad, self.public)

    def test_duplicate_or_missing_objects_cannot_be_published(self):
        path = self.root / 'duplicate.json'
        path.write_text(json.dumps({'revision': 1, 'objects': [
            {'species_id': 1, 'kind': 1, 'path': 'object'}] * 3}))
        output = self.root / 'duplicate.pack'
        with self.assertRaises(pack.PackError):
            pack.build_pack(path, self.private, output)
        self.assertFalse(output.exists())

    def test_large_catalog_verification_does_not_materialize_objects(self):
        peaks = []
        for count in (100, 1000, 10000):
            path = self.make_pack('scale-' + str(count), count)
            tracemalloc.start()
            result = pack.verify_pack(path, self.public)
            _, peak = tracemalloc.get_traced_memory()
            tracemalloc.stop()
            self.assertEqual(result['species'], count)
            self.assertLess(peak, 1024 * 1024)
            peaks.append(peak)
            print(json.dumps({'synthetic_species': count, 'objects': result['objects'],
                              'pack_bytes': result['bytes'], 'host_python_peak_bytes': peak}))
        self.assertLess(max(peaks) - min(peaks), 128 * 1024)

    def test_large_payload_is_streamed_and_non_ed25519_key_is_rejected(self):
        source = self.root / 'large-object'
        source.write_bytes(b'x' * pack.MAX_OBJECT_BYTES)
        recipe = self.root / 'large-object.json'
        recipe.write_text(json.dumps({'revision': 1, 'objects': [
            {'species_id': 1, 'kind': kind, 'path': source.name} for kind in (1, 2, 3)]}))
        path = self.root / 'large-object.pack'
        pack.build_pack(recipe, self.private, path)
        tracemalloc.start()
        pack.verify_pack(path, self.public)
        _, peak = tracemalloc.get_traced_memory()
        tracemalloc.stop()
        self.assertLess(peak, 512 * 1024)
        rsa = self.root / 'wrong-algorithm.pem'
        pack.openssl('genpkey', '-algorithm', 'RSA', '-pkeyopt', 'rsa_keygen_bits:2048', '-out', rsa)
        with self.assertRaises(pack.PackError):
            pack.build_pack(recipe, rsa, self.root / 'wrong-algorithm.pack')


if __name__ == '__main__':
    unittest.main()
