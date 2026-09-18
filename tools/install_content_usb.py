#!/usr/bin/env python3
"""Install signed content over USB with a fresh backup and protected-byte audit.

Run with the ESP-IDF Python environment (esptool and pyserial). The device must
already run compatible content firmware, or supply a tested --firmware image.
Keep the resulting private backup directory. Never use another device's backup.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time
from plan_content_install import plan


def install(args):
    directory=args.backup_directory.resolve();directory.mkdir(parents=True,exist_ok=False);directory.chmod(0o700)
    log=directory/'usb.log'
    def flash(*command):
        with log.open('ab') as stream:
            subprocess.run([sys.executable,'-m','esptool','--chip','esp32c3','--port',args.port,
                '--baud','460800','--after','no_reset',*map(str,command)],stdout=stream,stderr=subprocess.STDOUT,check=True)
    before=directory/'before.bin';flash('read_flash','0','0x800000',before)
    proposal=plan(before,args.partition_table,args.content)
    writes=proposal['writes']
    if args.firmware:
        image=args.firmware.read_bytes()
        if not image or image[0]!=0xe9 or len(image)>0x300000:raise ValueError('Unsupported application image')
        writes.append(dict(offset=0x10000,path=str(args.firmware.resolve()),bytes=len(image)))
    if not writes:raise ValueError('No installation requested')
    (directory/'plan.json').write_text(json.dumps(proposal,indent=2)+'\n')
    command=['write_flash']
    for item in sorted(writes,key=lambda item:item['offset']):command.extend([hex(item['offset']),item['path']])
    flash(*command)
    after=directory/'programmed.bin';flash('read_flash','0','0x800000',after)
    original=before.read_bytes();readback=after.read_bytes();allowed=[]
    for item in writes:
        data=Path(item['path']).read_bytes();start=item['offset']
        if readback[start:start+len(data)]!=data:raise ValueError('Programmed bytes differ from tested input')
        allowed.append((start,(start+len(data)+4095)//4096*4096))
    cursor=0
    for start,end in sorted(allowed):
        if original[cursor:start]!=readback[cursor:start]:raise ValueError('Protected flash changed')
        cursor=end
    if original[cursor:]!=readback[cursor:]:raise ValueError('Protected flash changed')
    import serial
    from esptool.reset import HardReset
    with serial.Serial(args.port,115200,timeout=.2) as port:
        HardReset(port,uses_usb=True)();start=time.monotonic();boot=bytearray()
        while time.monotonic()-start<20:boot.extend(port.read(8192))
    (directory/'boot.log').write_bytes(boot)
    if b'panic' in boot or b'Guru Meditation' in boot:raise ValueError('Device boot failed; retain backup and logs')
    if args.content:
        revision=int.from_bytes(args.content.read_bytes()[12:16],'little')
        if not re.search(rb'CONTENT_READY source=pack .*revision='+str(revision).encode()+rb'\b',boot):
            raise ValueError('Device rejected staged content; inspect boot.log; prior active pack was preserved')
    if b'BESTIARY_READY' not in boot:raise ValueError('Player save did not become ready')
    receipt=dict(protected_flash_unchanged=True,inputs=[dict(path=w['path'],sha256=hashlib.sha256(Path(w['path']).read_bytes()).hexdigest()) for w in writes])
    (directory/'verified.json').write_text(json.dumps(receipt,indent=2)+'\n')
    for child in directory.iterdir():child.chmod(0o600)
    print('Device boot and full flash readback passed. Private evidence: '+str(directory))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--port',required=True)
    parser.add_argument('--content',type=Path);parser.add_argument('--firmware',type=Path)
    parser.add_argument('--partition-table',type=Path);parser.add_argument('--backup-directory',type=Path,required=True)
    args=parser.parse_args()
    try:install(args)
    except (ValueError,OSError,subprocess.CalledProcessError) as error:parser.exit(1,'STOP: '+str(error)+'\n')
