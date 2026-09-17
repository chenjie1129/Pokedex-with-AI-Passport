"""OpenSSL publisher -> actual mbedTLS BSP adapter -> transactional C installer."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import pokedex_content_pack as pack
import encode_pokedex_content as typed
CLI = sys.argv.pop(1)


class DeviceCryptoTests(unittest.TestCase):
    def test_interop_mutations_keys_and_interrupted_install(self):
        with tempfile.TemporaryDirectory(prefix='city-device-pack-') as directory:
            root = Path(directory)
            key, public, rawkey = [root / name for name in ('private.pem', 'public.pem', 'public.bin')]
            pack.openssl('genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:P-256', '-out', key)
            public.write_bytes(pack.openssl('pkey', '-in', key, '-pubout'))
            rawkey.write_bytes(pack.check_key(public, algorithm=pack.P256))
            row = dict(species_id=1, flags=2, pool=0, types=['Grass'], hp=45, attack=40,
                       defense=50, name_en='Fixture', name_zh='测试', description_en='Offline.', description_zh='测试。')
            objects = []
            for kind, data in ((1, typed.metadata(row)), (2, typed.sprite(12, 12, bytes(432))), (3, typed.cry(bytes(2)))):
                (root / str(kind)).write_bytes(data)
                objects.append(dict(species_id=1, kind=kind, path=str(kind)))
            recipe = root / 'recipe.json'
            paths = []
            for revision in (1, 2, 3):
                recipe.write_text(json.dumps(dict(revision=revision, objects=objects)))
                output = root / ('%d.pack' % revision)
                pack.build_pack(recipe, key, output, algorithm=pack.P256)
                self.assertEqual(pack.verify_pack(output, public)['revision'], revision)
                paths.append(output)
            def run(path, expected=1, keyfile=rawkey):
                result = subprocess.run([CLI, str(keyfile), str(path)], capture_output=True, text=True)
                self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
            run(paths[0], 0)
            result = subprocess.run([CLI, str(rawkey), *map(str, paths)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())
            original = paths[0].read_bytes(); bad = root / 'bad.pack'
            # Every byte of a real signed typed pack is bound or structurally checked.
            for i in range(len(original)):
                data = bytearray(original); data[i] ^= 1; bad.write_bytes(data); run(bad)
            for data in (original[:-1], original + b'\0', b''):
                bad.write_bytes(data); run(bad)
            sig = 28 + 3 * 44
            for signature in (bytes(64), bytes([255]) * 64):
                bad.write_bytes(original[:sig] + signature + original[sig + 64:]); run(bad)
            badkey = root / 'bad-key.bin'
            for data in (b'', bytes(65), b'\x04' + bytes(64), rawkey.read_bytes()[:-1]):
                badkey.write_bytes(data); run(paths[0], keyfile=badkey)
            other = root / 'other.pem'
            pack.openssl('genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:P-256', '-out', other)
            badkey.write_bytes(pack.check_key(other, private=True, algorithm=pack.P256))
            run(paths[0], keyfile=badkey)
            # Ed25519 compatibility stays host-only; the device fails closed.
            ed = root / 'ed.pem'; pack.openssl('genpkey', '-algorithm', 'ED25519', '-out', ed)
            edpack = root / 'ed.pack'; pack.build_pack(recipe, ed, edpack); run(edpack)
            for wrongkey, algorithm in ((ed, pack.P256), (key, pack.ALGORITHM)):
                with self.assertRaises(pack.PackError):
                    pack.build_pack(recipe, wrongkey, root / 'wrong.pack', algorithm)
            other_public = root / 'other-public.pem'
            other_public.write_bytes(pack.openssl('pkey', '-in', other, '-pubout'))
            with self.assertRaises(pack.PackError):
                pack.verify_pack(paths[0], other_public)
            p384 = root / 'p384.pem'
            pack.openssl('genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:P-384', '-out', p384)
            with self.assertRaises(pack.PackError):
                pack.build_pack(recipe, p384, root / 'p384.pack', pack.P256)


if __name__ == '__main__':
    unittest.main()
