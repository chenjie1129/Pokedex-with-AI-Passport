#!/usr/bin/env python3
"""Read-only check of an AI Passport full-flash backup before app installation."""
import argparse
from pathlib import Path
import struct

EXPECTED = {
    'nvs': (1, 2, 0x9000, 0x6000),
    'phy_init': (1, 1, 0xF000, 0x1000),
    'factory': (0, 0, 0x10000, 0x300000),
    'cardid': (1, 2, 0x356000, 0x4000),
    'recovery': (0, 0x20, 0x700000, 0x100000),
}


def check_backup(path):
    blob = Path(path).read_bytes()
    if len(blob) != 0x800000:
        raise ValueError('Expected a complete 8 MB flash backup')
    found = {}
    for offset in range(0x8000, 0x9000, 32):
        magic, kind, subtype, address, size, name, flags = struct.unpack_from('<HBBII16sI', blob, offset)
        if magic != 0x50AA:
            break
        label = name.split(b'\0')[0].decode('ascii')
        if label in found or flags != 0:
            raise ValueError('Unsupported duplicate or flagged partition')
        found[label] = (kind, subtype, address, size)
    if found != EXPECTED:
        raise ValueError('Partition layout differs from the supported AI Passport layout; do not flash this app')
    if blob[0x10000] != 0xE9:
        raise ValueError('Expected an existing app in the factory partition')
    return True


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('backup', type=Path)
    args = parser.parse_args()
    try:
        check_backup(args.backup)
    except (ValueError, OSError, UnicodeError) as error:
        parser.exit(1, f'STOP: {error}\n')
    print('Backup is complete and partition layout matches. App installation address: 0x10000.')
