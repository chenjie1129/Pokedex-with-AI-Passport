"""Render the production Home/Passport functions, with only data and hardware stubbed."""
from pathlib import Path
import re
import sys
source = Path(sys.argv[1]).read_text()
functions = []
for name in ('style_plain', 'label_at', 'new_screen', 'create_menu_row', 'build_home', 'build_passport'):
    match = re.search(r'^static [^\n]*\b' + name + r'\(', source, re.M)
    assert match, name
    end = source.index('\n}\n', match.start()) + 3
    functions.append(source[match.start():end])
constants = '\n'.join(re.findall(r'^#define (?:COLOR_\w+|SCREEN_WIDTH|TITLE_Y|META_Y|FOOTER_Y)\s+[^\n]+', source, re.M))
harness = Path(__file__).with_name('harness.c').read_text()
Path(sys.argv[2]).write_text(harness.replace('/* PRODUCTION */', constants + '\n' + '\n'.join(functions)))
