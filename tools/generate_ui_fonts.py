#!/usr/bin/env python3
"""Generate UI fonts with the Pokédex accent using lv_font_conv 1.5.3.

Usage: python3 tools/generate_ui_fonts.py /path/to/lv_font_conv
Reuses the bundled Montserrat and symbol fonts and the original LVGL options.
"""
import re
import shlex
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
lvgl = root / 'managed_components/lvgl__lvgl'
for size in (14, 20):
    original = (lvgl / f'src/font/lv_font_montserrat_{size}.c').read_text()
    args = shlex.split(re.search(r' \* Opts: (.*)', original).group(1))
    args[args.index('-r') + 1] += ',0xE9'
    args[args.index('-o') + 1] = f'../../../../main/assets/city_font_{size}.c'
    args.extend(['--lv-include', 'lvgl.h', '--lv-font-name', f'city_font_{size}'])
    subprocess.run([sys.argv[1], *args], cwd=lvgl / 'scripts/built_in_font', check=True)
