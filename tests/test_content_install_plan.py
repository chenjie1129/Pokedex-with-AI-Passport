import hashlib
import json
from pathlib import Path
import struct
import re
import sys
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
import plan_content_install as planner
from check_passport_backup import EXPECTED,CONTENT
import pokedex_content_pack as pack
import install_content_usb as installer

def table(entries):
    rows=b''.join(struct.pack('<HBBII16sI',0x50aa,*entry[:2],*entry[2:],name.encode(),0)
                  for name,entry in sorted(entries.items(),key=lambda item:item[1][2]))
    return (rows+b'\xeb\xeb'+b'\xff'*14+hashlib.md5(rows).digest()).ljust(3072,b'\xff')

class InstallPlan(unittest.TestCase):
    def test_usb_reset_deasserts_dtr_before_boot_capture(self):
        events=[]
        class Port:
            def __init__(self,*args,**kwargs):events.append('open')
            def __enter__(self):return self
            def __exit__(self,*args):events.append('close')
            def setDTR(self,value):events.append(('dtr',value))
            def reset_input_buffer(self):events.append('clear')
            def read(self,size):events.append('read');return b'boot'
        def reset(port,uses_usb):
            self.assertTrue(uses_usb)
            return lambda: events.append('reset')
        with patch.dict(sys.modules,{'serial':SimpleNamespace(Serial=Port),
                                     'esptool.reset':SimpleNamespace(HardReset=reset)}), \
             patch.object(installer.time,'monotonic',side_effect=[0,0,21]):
            self.assertEqual(installer.capture_boot('fixture'),b'boot')
        self.assertEqual(events,['open',('dtr',False),'clear','reset','read','close'])

    def test_public_trust_is_identical_in_publisher_and_device(self):
        raw=pack.check_key(planner.TRUST,private=False,algorithm=pack.P256)
        self.assertEqual(raw,(ROOT/'content/trust/owner-p256-public.bin').read_bytes())
        header=(ROOT/'main/content_trust_key.h').read_text().split('{',1)[1].split('}',1)[0]
        self.assertEqual(raw,bytes(int(n) for n in re.findall(r'\d+',header)))

    def test_blank_regions_and_exact_partition_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);backup=root/'backup';binary=root/'table'
            data=bytearray(b'\xff'*0x800000);data[0x8000:0x8c00]=table(EXPECTED);data[0x10000]=0xe9
            data[0x3fa000:0x3fa004]=b'KEEP' # Legacy unnamed data must remain outside proposed slots.
            backup.write_bytes(data);binary.write_bytes(table({**EXPECTED,**CONTENT}))
            result=planner.plan(backup,binary)
            self.assertEqual([w['offset'] for w in result['writes']],[0x8000])
            for start in (0x420000,0x560000,0x6a0000):
                changed=bytearray(data);changed[start]=0;backup.write_bytes(changed)
                with self.assertRaises(ValueError):planner.plan(backup,binary)
            backup.write_bytes(data)
            changed=bytearray(binary.read_bytes());changed[270]^=1;binary.write_bytes(changed)
            with self.assertRaises(ValueError):planner.plan(backup,binary)
            modified={**EXPECTED,**CONTENT};modified['recovery']=(0,0x20,0x700000,0xf0000)
            binary.write_bytes(table(modified))
            with self.assertRaises(ValueError):planner.plan(backup,binary)
            backup.write_bytes(data[:-1])
            with self.assertRaises(ValueError):planner.plan(backup,binary)

    def test_only_trusted_new_content_is_planned(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);key=root/'key';public=root/'public';backup=root/'backup'
            pack.openssl('genpkey','-algorithm','EC','-pkeyopt','ec_paramgen_curve:P-256','-out',key)
            public.write_bytes(pack.openssl('pkey','-in',key,'-pubout'))
            data=bytearray(b'\xff'*0x800000);data[0x8000:0x8c00]=table({**EXPECTED,**CONTENT});data[0x10000]=0xe9
            backup.write_bytes(data);(root/'object').write_bytes(b'fixture')
            recipe=root/'recipe';recipe.write_text(json.dumps(dict(revision=1,objects=[dict(species_id=1,kind=k,path='object') for k in (1,2,3)])))
            target=root/'pack';pack.build_pack(recipe,key,target,pack.P256)
            original=planner.TRUST;planner.TRUST=public
            try:
                result=planner.plan(backup,content=target);self.assertEqual(result['writes'][0]['offset'],0x420000)
                changed=bytearray(target.read_bytes());changed[-1]^=1;target.write_bytes(changed)
                with self.assertRaises(ValueError):planner.plan(backup,content=target)
                data[0x6a0000]=0;backup.write_bytes(data);target.unlink()
                pack.build_pack(recipe,key,target,pack.P256)
                with self.assertRaises(ValueError):planner.plan(backup,content=target)
            finally:planner.TRUST=original

if __name__=='__main__':unittest.main()
