#!/usr/bin/env python3
"""Package a matching app image and build identity; never flash a device."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
from build_identity import identity


def package(repo, build, output, allow_dirty=False):
    saved = json.loads((build / 'identity/build-identity.json').read_text())
    current = identity(repo)
    if saved != current:
        raise ValueError('Source changed since configuration; rebuild before packaging')
    if saved['dirty'] and not allow_dirty:
        raise ValueError('Release requires clean source; use --allow-dirty for a labeled device test')
    binary = build / 'Pokedex-AI-Passport.bin'
    data = binary.read_bytes()
    if len(data) > 0x300000:
        raise ValueError('App exceeds the 3 MiB factory partition')
    if len(data) < 288 or data[0] != 0xE9 or data[32:36] != bytes.fromhex('3254cdab'):
        raise ValueError('Invalid ESP application header')
    embedded = data[48:80].split(b'\0')[0].decode('ascii')
    if embedded != saved['version']:
        raise ValueError('Image version does not match current build identity')
    config = build.parent / 'sdkconfig'
    # IDF records the authoritative config path in project_description.json.
    description = json.loads((build / 'project_description.json').read_text())
    config = Path(description.get('config_file', config))
    manifest = dict(saved,
                    firmware_sha256=hashlib.sha256(data).hexdigest(),
                    firmware_bytes=len(data),
                    sdkconfig_sha256=hashlib.sha256(config.read_bytes()).hexdigest(),
                    elf_sha256=data[176:208].hex(),
                    esp_idf=data[144:176].split(b'\0')[0].decode('ascii'),
                    flash_address='0x10000', device_test_build=saved['dirty'])
    if output.exists() and any(output.iterdir()):
        raise ValueError('Output directory must be empty')
    output.mkdir(parents=True, exist_ok=True)
    shutil.copy2(binary, output / binary.name)
    shutil.copy2(config, output / 'sdkconfig')
    shutil.copy2(repo / 'docs/device-installation-guide.md', output / 'device-installation-guide.md')
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2, sort_keys=True)+'\n')
    sums = ''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n'
                   for p in sorted(output.iterdir()) if p.is_file())
    (output / 'SHA256SUMS').write_text(sums)
    return manifest


if __name__ == '__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[1])
    parser.add_argument('--build-dir',type=Path,required=True)
    parser.add_argument('--output-dir',type=Path,required=True)
    parser.add_argument('--allow-dirty',action='store_true')
    args=parser.parse_args()
    print(json.dumps(package(args.repo.resolve(),args.build_dir.resolve(),
                             args.output_dir.resolve(),args.allow_dirty),indent=2))
