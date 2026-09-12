#!/usr/bin/env python3
"""Generate subset UI fonts for English and Simplified Chinese.

Usage: python3 tools/generate_ui_fonts.py /path/to/lv_font_conv
Reuses LVGL's bundled Montserrat, Source Han Sans SC and symbol fonts.
"""
import re
import shlex
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
lvgl = root / 'managed_components/lvgl__lvgl'
cjk_source = 'SourceHanSansSC-Normal.otf'
copy_source = ''.join(
    path.read_text()
    for path in (root / 'main/ui_strings.c', root / 'main/main.c')
)
cjk_codepoints = sorted({
    ord(character)
    for character in copy_source
    if ord(character) > 0x7F and character != 'é'
})
cjk_range = ','.join(f'0x{codepoint:X}' for codepoint in cjk_codepoints)

for size in (14, 20):
    original = (lvgl / f'src/font/lv_font_montserrat_{size}.c').read_text()
    args = shlex.split(re.search(r' \* Opts: (.*)', original).group(1))
    args[args.index('-r') + 1] += ',0xE9'
    args[args.index('-o') + 1] = f'../../../../main/assets/city_font_{size}.c'
    format_index = args.index('--format')
    args[format_index:format_index] = [
        '--font', str(cjk_source),
        '-r', cjk_range,
    ]
    args.extend(['--lv-include', 'lvgl.h', '--lv-font-name', f'city_font_{size}'])
    subprocess.run([sys.argv[1], *args], cwd=lvgl / 'scripts/built_in_font', check=True)
