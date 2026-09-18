#!/usr/bin/env python3
"""Read-only, fail-closed USB installation plan from this device's full backup.

The plan never writes flash. Use only after the corresponding firmware tests pass.
It preserves all legacy occupied space, identity, recovery, NVS and the bootloader.
The device authenticates staged content and commits activation on the next boot.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib
import tempfile
import pokedex_content_pack as signed
TRUST=Path(__file__).resolve().parents[1]/"content/trust/owner-p256-public.pem"
from check_passport_backup import check_backup, EXPECTED, CONTENT

def entries(data):
    result={}
    for offset in range(0,len(data),32):
        magic,kind,subtype,address,size,name,flags=struct.unpack_from('<HBBII16sI',data,offset)
        if magic!=0x50aa: break
        if flags: raise ValueError('Flagged partition')
        label=name.split(b'\0')[0].decode('ascii')
        if label in result: raise ValueError('Duplicate partition')
        result[label]=(kind,subtype,address,size)
    return result

def record(raw):
    if len(raw)!=80 or raw[:8]!=b'CITYACT1' or zlib.crc32(raw[:76])!=struct.unpack_from('<I',raw,76)[0]:return None
    generation,slot,size,revision,high=struct.unpack_from('<5I',raw,8)
    if not generation or slot>1 or not 92<=size<=0x140000 or not 0<revision<=high or any(raw[60:76]):return None
    return dict(generation=generation,slot=slot,size=size,revision=revision,high=high,manifest=raw[28:60].hex())

def plan(backup, table=None, content=None):
    check_backup(backup);blob=Path(backup).read_bytes();layout=entries(blob[0x8000:0x9000]);writes=[]
    if layout==EXPECTED:
        if table is None:raise ValueError('Legacy device needs the reviewed extended partition table')
        binary=Path(table).read_bytes()
        if len(binary)>4096 or len(binary)%32 or entries(binary)!={**EXPECTED,**CONTENT}:raise ValueError('Unexpected replacement table')
        # Verify the ESP-IDF partition-table MD5 record, not just its entries.
        pos=32*len(CONTENT|EXPECTED)
        if binary[pos:pos+16]!=b'\xeb\xeb'+b'\xff'*14 or binary[pos+16:pos+32]!=hashlib.md5(binary[:pos]).digest():
            raise ValueError('Invalid partition table MD5')
        for _,_,start,size in CONTENT.values():
            if any(b!=255 for b in blob[start:start+size]):raise ValueError('Proposed content space contains data; preserve it')
        writes.append(dict(offset=0x8000,path=str(Path(table).resolve()),bytes=len(binary)))
    records=[record(blob[a:a+80]) for a in (0x6a0000,0x6a1000)]
    records=[r for r in records if r]
    # Refuse damaged recorded packs: the device may fall back to the other slot,
    # so a host cannot infer a safe inactive slot from journal CRC alone.
    with tempfile.TemporaryDirectory(prefix='passport-plan-') as temporary:
        for r in records:
            offset=(0x420000,0x560000)[r['slot']]
            path=Path(temporary)/'installed.pack';path.write_bytes(blob[offset:offset+r['size']])
            checked=signed.verify_pack(path,TRUST)
            if checked['revision']!=r['revision'] or checked['manifest_sha256']!=r['manifest']:raise ValueError('Recorded pack differs; reconcile on device')
    current=max(records,key=lambda r:r['generation']) if records else None
    if content:
        signed.verify_pack(Path(content),TRUST)
        data=Path(content).read_bytes()
        if len(data)<92 or len(data)>0x140000 or data[:8]!=b'CITYPK01':raise ValueError('Content size or header invalid')
        version,algorithm,revision,objects,payload,reserved=struct.unpack_from('<HH4I',data,8)
        if version!=1 or algorithm!=2 or reserved or objects%3 or not 0<objects<=30000 or 92+objects*44+payload!=len(data):
            raise ValueError('Unsupported content header')
        if records and revision<=max(r['high'] for r in records):raise ValueError('Revision must exceed installed high water')
        # The tool conservatively refuses ambiguous/corrupt selectors when content
        # bytes exist. Recover/reconcile on-device before choosing a writable slot.
        if not records and any(b!=255 for b in blob[0x6a0000:0x6a2000]):raise ValueError('Selector needs device reconciliation')
        slot=1-current['slot'] if current else 0
        writes.append(dict(offset=(0x420000,0x560000)[slot],path=str(Path(content).resolve()),bytes=len(data),slot=slot))
    return dict(backup_sha256=hashlib.sha256(blob).hexdigest(),writes=writes,
                protected=['bootloader','nvs','phy_init','cardid','recovery','legacy occupied space'],
                activation='device signature, typed-content, font and save checks at boot')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('backup',type=Path)
    p.add_argument('--partition-table',type=Path);p.add_argument('--content',type=Path);a=p.parse_args()
    try:print(json.dumps(plan(a.backup,a.partition_table,a.content),indent=2))
    except (ValueError,OSError,UnicodeError,struct.error) as e:p.exit(1,'STOP: '+str(e)+'\n')
