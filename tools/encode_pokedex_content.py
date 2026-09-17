#!/usr/bin/env python3
"""Encode typed prototype objects for the signed container; no device install."""
import argparse
import json
from pathlib import Path
import struct

from pokedex_content_pack import PackError, require

# Stable wire values. Zero is allowed only for an absent second type.
TYPES = ('Normal', 'Fire', 'Water', 'Electric', 'Grass', 'Ice', 'Fighting',
         'Poison', 'Ground', 'Flying', 'Psychic', 'Bug', 'Rock', 'Ghost',
         'Dragon', 'Dark', 'Steel', 'Fairy')


def integer(value, low, high):
    require(type(value) is int and low <= value <= high, 'Integer outside typed-content bounds')
    return value


def metadata(row):
    sid = integer(row['species_id'], 1, 65534)
    parent = integer(row.get('evolves_from', 0), 0, 65534)
    flags = integer(row['flags'], 0, 7)
    require(parent != sid and not (parent and flags & 3), 'Invalid evolution eligibility')
    require((sid >= 60000) == bool(flags & 4), 'Original-content flag must match ID namespace')
    pool = integer(row['pool'], 0, 2)
    types = row['types']
    require(1 <= len(types) <= 2 and len(set(types)) == len(types) and
            all(t in TYPES for t in types), 'Invalid species types')
    stats = [integer(row[k], 1, 240) for k in ('hp', 'attack', 'defense')]
    strings = []
    for key, limit in [('name_en', 32), ('name_zh', 48), ('description_en', 90), ('description_zh', 180)]:
        value = row[key]
        require(isinstance(value, str) and value and
                all(ord(c) >= 32 and not 127 <= ord(c) <= 159 for c in value), 'Invalid text')
        raw = value.encode('utf-8')
        require(len(raw) <= limit, 'Text exceeds UTF-8 byte budget')
        strings.append(raw)
    return (struct.pack('<4sHHH8B4H', b'CM01', 1, sid, parent, flags, pool,
                        TYPES.index(types[0]) + 1, TYPES.index(types[1]) + 1 if len(types) == 2 else 0,
                        *stats, 0, *map(len, strings)) + b''.join(strings))


def sprite(width, height, pixels):
    integer(width, 1, 110); integer(height, 1, 110)
    require(len(pixels) == width * height * 3, 'Sprite size differs from RGB565+alpha8 dimensions')
    return struct.pack('<4sHHB3x', b'CS01', width, height, 1) + pixels


def cry(pcm):
    require(0 < len(pcm) <= 64000 and len(pcm) % 2 == 0, 'Cry must be 1-32000 signed 16-bit mono samples')
    return struct.pack('<4sHBBI', b'CA01', 16000, 1, 1, len(pcm) // 2) + pcm


def prepare(source, output):
    source, output = Path(source), Path(output)
    doc = json.loads(source.read_text())
    integer(doc['revision'], 1, 0xffffffff)
    rows = doc['species']
    require(0 < len(rows) <= 10000, 'Invalid species count')
    by_id = {integer(r['species_id'], 1, 65534): r for r in rows}
    require(len(by_id) == len(rows), 'Duplicate species')
    # Validate relationship and text fields before creating an output directory.
    for row in rows:
        metadata(row)
        parent = row.get('evolves_from', 0)
        require(not parent or (parent in by_id and not by_id[parent].get('evolves_from', 0)),
                'Prototype supports one evolution step with its base present')
    output.mkdir(parents=True, exist_ok=False)
    objects = []
    for row in sorted(rows, key=lambda row: row['species_id']):
        sid = row['species_id']
        pixels_path = source.parent / row['sprite_rgb565a8']
        pcm_path = source.parent / row['cry_pcm']
        require(pixels_path.stat().st_size <= 110 * 110 * 3 and pcm_path.stat().st_size <= 64000,
                'Asset exceeds prototype budget')
        values = [metadata(row), sprite(row['width'], row['height'], pixels_path.read_bytes()), cry(pcm_path.read_bytes())]
        for kind, data in enumerate(values, 1):
            name = f'{sid}-{kind}.bin'
            (output / name).write_bytes(data)
            objects.append({'species_id': sid, 'kind': kind, 'path': name})
    (output / 'recipe.json').write_text(json.dumps({'revision': doc['revision'], 'objects': objects}, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output_directory', type=Path)
    args = parser.parse_args()
    try:
        prepare(args.source, args.output_directory)
    except (ValueError, OSError, KeyError, TypeError, struct.error) as error:
        parser.exit(1, f'STOP: {error}\n')
