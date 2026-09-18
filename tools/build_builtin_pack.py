#!/usr/bin/env python3
"""Package the existing roster's exact generated sprites and verified cries.

No network download or image conversion. Private keys and output packs are local.
Use encode_pokedex_content.py for a separately authored additional-content source.
"""
import argparse
import json
from pathlib import Path
import re
import hashlib
import encode_pokedex_content as typed
import pokedex_content_pack as pack
ROOT=Path(__file__).resolve().parents[1]

def build(output, key, revision):
    output=Path(output); output.mkdir(parents=True,exist_ok=False)
    rows=json.loads((ROOT/'content/species.json').read_text())['species']
    manifests={r['id']:r for r in json.loads((ROOT/'content/cries/manifest.json').read_text())['species']}
    text='\n'.join((ROOT/'main/assets'/name).read_text() for name in
                   ('charmander_sprite.c','starter_sprites.c','roster_sprites.c'))
    objects=[]
    for row in rows:
        sid=row['id']; symbol=row['name'].lower()
        match=re.search(r'const uint8_t '+symbol+r'_large_map\[\] = \{(.*?)\};',text,re.S)
        if not match:
            raise ValueError('Missing generated large sprite: '+symbol)
        pixels=bytes(int(x,16) for x in re.findall(r'0x([0-9A-Fa-f]{2})',match[1]))
        pcm=(ROOT/f'content/cries/{sid}.pcm').read_bytes()
        if hashlib.sha256(pcm).hexdigest()!=manifests[sid]['pcm_sha256']:
            raise ValueError('Cry digest mismatch')
        meta=dict(species_id=sid,evolves_from=row.get('evolves_from',0),
            flags=int(row['wild'])|int(row.get('place',not row.get('evolves_from',0)))*2|(4 if sid>=60000 else 0),
            pool=row['pool'],types=row['types'],hp=row['hp'],attack=row['attack'],defense=row['defense'],
            name_en=row['name'],name_zh=row['name'],description_en=row['description'],description_zh=row['description'])
        # Built-in names/descriptions retain the firmware's Chinese translation adapter.
        for kind,data in ((1,typed.metadata(meta)),(2,typed.sprite(110,110,pixels)),(3,typed.cry(pcm))):
            name=f'{sid}-{kind}.bin';(output/name).write_bytes(data)
            objects.append(dict(species_id=sid,kind=kind,path=name))
    recipe=output/'recipe.json'; recipe.write_text(json.dumps(dict(revision=revision,objects=objects),indent=2)+'\n')
    pack.build_pack(recipe,Path(key),output/'content.pack',pack.P256)
    return output/'content.pack'

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output',type=Path);parser.add_argument('--private-key',required=True,type=Path)
    parser.add_argument('--revision',required=True,type=int);args=parser.parse_args()
    print(build(args.output,args.private_key,args.revision))
