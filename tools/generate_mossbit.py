#!/usr/bin/env python3
"""Reproduce original CC0 Mossbit pixel artwork and a short synthesized cry."""
import hashlib
import json
import math
from pathlib import Path
import struct
from PIL import Image, ImageDraw
ROOT = Path(__file__).resolve().parents[1]
image = Image.new('RGBA', (32, 32))
d = ImageDraw.Draw(image)
d.polygon([(5,13),(10,9),(12,4),(16,8),(22,5),(22,10),(27,14),(26,25),(22,28),(9,28),(5,24)], fill='#244b40')
d.rectangle((8,14,24,24), fill='#73b87c')
d.rectangle((11,10,20,23), fill='#8bd49a')
d.rectangle((10,18,12,20), fill='#183b39')
d.rectangle((20,18,22,20), fill='#183b39')
d.rectangle((15,23,18,23), fill='#183b39')
d.rectangle((8,27,12,29), fill='#244b40')
d.rectangle((21,27,25,29), fill='#244b40')
image.resize((128,128),Image.Resampling.NEAREST).save(ROOT/'demo/assets/mossbit.png')
samples = [int(4500 * math.sin(math.pi*i/4800)**2 * math.sin(2*math.pi*(500*i/16000+300*(i/16000)**2))) for i in range(4800)]
raw=struct.pack('<4800h',*samples)
(ROOT/'content/cries/60000.pcm').write_bytes(raw)
p=ROOT/'content/cries/manifest.json'; manifest=json.loads(p.read_text())
manifest['species']=[r for r in manifest['species'] if r['id']!=60000]
manifest['species'].append(dict(id=60000,name='Mossbit',origin='original',license='CC0-1.0',source_url='project:tools/generate_mossbit.py',pcm_sha256=hashlib.sha256(raw).hexdigest(),samples=len(samples)))
p.write_text(json.dumps(manifest,indent=2)+'\n')
