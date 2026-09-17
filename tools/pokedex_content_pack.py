#!/usr/bin/env python3
"""Host-only signed content container prototype; never activates device content.

Requires OpenSSL 3 with Ed25519. A trusted public key is supplied out of band.
Objects are opaque in this increment; passing verification is not game-schema,
asset-license, capacity, or firmware compatibility approval.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

MAGIC = b'CITYPK01'
HEADER = struct.Struct('<8sHHIIQ')
ENTRY = struct.Struct('<HBBII32s')
VERSION = 1
ALGORITHM = 1  # Ed25519 over DOMAIN || SHA256(header || index)
DOMAIN = b'CityPassport.ContentPack.v1\0'
SIGNATURE_BYTES = 64
CHUNK = 65536
MAX_OBJECTS = 30000
MAX_OBJECT_BYTES = 1024 * 1024
MAX_PACK_BYTES = 8 * 1024 * 1024  # Host ceiling, NOT allocated device capacity.
PUBLIC_DER_PREFIX = bytes.fromhex('302a300506032b6570032100')


class PackError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise PackError(message)


def openssl(*args):
    try:
        result = subprocess.run(['openssl', *map(str, args)], capture_output=True,
                                timeout=30, check=False, stdin=subprocess.DEVNULL)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise PackError('OpenSSL 3 with Ed25519 is required') from error
    require(result.returncode == 0, 'Key/signature operation failed')
    return result.stdout


def check_key(key, private=False):
    args = ['pkey', '-in', key, '-pubout', '-outform', 'DER']
    if not private:
        args.append('-pubin')
    der = openssl(*args)
    require(len(der) == len(PUBLIC_DER_PREFIX) + 32 and
            der.startswith(PUBLIC_DER_PREFIX), 'Only Ed25519 keys are accepted')


def signature_operation(digest, key, signature=None):
    check_key(key, private=signature is None)
    with tempfile.TemporaryDirectory(prefix='city-pack-sign-') as directory:
        root = Path(directory)
        message = root / 'message'
        message.write_bytes(DOMAIN + digest)
        if signature is None:
            signed = openssl('pkeyutl', '-sign', '-rawin', '-inkey', key, '-in', message)
            require(len(signed) == SIGNATURE_BYTES, 'Invalid signature size')
            return signed
        sigfile = root / 'signature'
        sigfile.write_bytes(signature)
        openssl('pkeyutl', '-verify', '-rawin', '-pubin', '-inkey', key,
                '-in', message, '-sigfile', sigfile)


def read_exact(stream, size):
    data = stream.read(size)
    require(len(data) == size, 'Truncated pack')
    return data


def object_hash(stream, size, output=None):
    digest = hashlib.sha256()
    while size:
        block = read_exact(stream, min(size, CHUNK))
        digest.update(block)
        if output is not None:
            output.write(block)
        size -= len(block)
    return digest.digest()


def read_index(stream, count, payload_size, digest):
    """Validate canonical triples without allocating an object table."""
    offset = 0
    species = 0
    for index in range(count):
        raw = read_exact(stream, ENTRY.size)
        digest.update(raw)
        sid, kind, reserved, start, size, _ = ENTRY.unpack(raw)
        require(0 < sid < 65535 and kind == index % 3 + 1 and reserved == 0,
                'Invalid ID, object kind, order, or reserved byte')
        if kind == 1:
            require(sid > species, 'Species IDs must be unique and ascending')
            species = sid
        else:
            require(sid == species, 'Each species needs metadata, sprite and cry')
        require(0 < size <= MAX_OBJECT_BYTES and start == offset,
                'Invalid object size or noncontiguous payload')
        offset += size
        require(offset <= payload_size, 'Object extends outside payload')
    require(offset == payload_size, 'Unindexed payload bytes')


def verify_pack(path, public_key, max_bytes=MAX_PACK_BYTES):
    """Two passes over the index; payload hashing uses at most CHUNK bytes/read."""
    require(0 < max_bytes <= MAX_PACK_BYTES, 'Invalid caller size budget')
    with Path(path).open('rb') as stream:
        size = os.fstat(stream.fileno()).st_size
        require(HEADER.size + SIGNATURE_BYTES <= size <= max_bytes, 'Pack exceeds size budget or is truncated')
        header = read_exact(stream, HEADER.size)
        magic, version, algorithm, revision, count, payload_size = HEADER.unpack(header)
        require(magic == MAGIC and version == VERSION and algorithm == ALGORITHM,
                'Unsupported pack format or signature algorithm')
        require(revision > 0 and 0 < count <= MAX_OBJECTS and count % 3 == 0,
                'Invalid revision or object count')
        payload_start = HEADER.size + count * ENTRY.size + SIGNATURE_BYTES
        require(payload_start + payload_size == size, 'Length mismatch or trailing bytes')
        digest = hashlib.sha256(header)
        read_index(stream, count, payload_size, digest)
        signature = read_exact(stream, SIGNATURE_BYTES)
        signature_operation(digest.digest(), public_key, signature)
        # Authentication precedes object reads. Check the index again while using
        # it, detecting concurrent index modification as well as payload damage.
        second_digest = hashlib.sha256(header)
        for index in range(count):
            stream.seek(HEADER.size + index * ENTRY.size)
            raw = read_exact(stream, ENTRY.size)
            second_digest.update(raw)
            _, _, _, offset, length, expected = ENTRY.unpack(raw)
            require(0 < length <= MAX_OBJECT_BYTES and offset + length <= payload_size,
                    'Index changed during verification')
            stream.seek(payload_start + offset)
            require(object_hash(stream, length) == expected, 'Object SHA-256 mismatch')
        require(second_digest.digest() == digest.digest(), 'Index changed during verification')
        return {'status': 'verified_host_container', 'revision': revision,
                'species': count // 3, 'objects': count, 'bytes': size,
                'manifest_sha256': digest.hexdigest(), 'device_compatible': False}


def build_pack(recipe_path, private_key, output):
    recipe_path, output = Path(recipe_path), Path(output)
    require(not output.exists(), 'Output already exists')
    recipe = json.loads(recipe_path.read_text())
    revision, objects = recipe['revision'], recipe['objects']
    require(type(revision) is int and 0 < revision <= 0xffffffff, 'Invalid revision')
    require(0 < len(objects) <= MAX_OBJECTS and len(objects) % 3 == 0, 'Invalid object count')
    for item in objects:
        require(type(item['species_id']) is int and 0 < item['species_id'] < 65535
                and type(item['kind']) is int and item['kind'] in (1, 2, 3), 'Invalid object identity')
    objects = sorted(objects, key=lambda item: (item['species_id'], item['kind']))
    records, sources, payload_size = [], [], 0
    for item in objects:
        source = recipe_path.parent / item['path']
        with source.open('rb') as stream:
            size = os.fstat(stream.fileno()).st_size
            require(0 < size <= MAX_OBJECT_BYTES, 'Invalid object size')
            digest = object_hash(stream, size)
        records.append(ENTRY.pack(item['species_id'], item['kind'], 0, payload_size, size, digest))
        sources.append((source, size, digest))
        payload_size += size
        require(HEADER.size + len(objects) * ENTRY.size + SIGNATURE_BYTES + payload_size
                <= MAX_PACK_BYTES, 'Pack exceeds host size budget')
    header = HEADER.pack(MAGIC, VERSION, ALGORITHM, revision, len(objects), payload_size)
    digest = hashlib.sha256(header)
    # Share the verifier's structural invariants with the publisher.
    import io
    read_index(io.BytesIO(b''.join(records)), len(records), payload_size, digest)
    signature = signature_operation(digest.digest(), private_key)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent, prefix='.city-pack-', delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(header)
            for raw in records:
                stream.write(raw)
            stream.write(signature)
            for source, size, expected in sources:
                with source.open('rb') as src:
                    require(object_hash(src, size, stream) == expected and src.read(1) == b'',
                            'Source changed while building')
            stream.flush()
            os.fsync(stream.fileno())
        # Publish only a complete file, without overwriting an existing pack.
        os.link(temporary, output)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    build = commands.add_parser('build')
    build.add_argument('--recipe', required=True, type=Path)
    build.add_argument('--private-key', required=True, type=Path)
    build.add_argument('--output', required=True, type=Path)
    verify = commands.add_parser('verify')
    verify.add_argument('pack', type=Path)
    verify.add_argument('--public-key', required=True, type=Path)
    verify.add_argument('--max-bytes', type=int, default=MAX_PACK_BYTES)
    args = parser.parse_args()
    try:
        if args.command == 'build':
            build_pack(args.recipe, args.private_key, args.output)
            print(json.dumps({'status': 'built_host_container', 'path': str(args.output)}))
        else:
            print(json.dumps(verify_pack(args.pack, args.public_key, args.max_bytes)))
    except (ValueError, OSError, KeyError, TypeError, struct.error) as error:
        parser.exit(1, f'STOP: {error}\n')


if __name__ == '__main__':
    main()
