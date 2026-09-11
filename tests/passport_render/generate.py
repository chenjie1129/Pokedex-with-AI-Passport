"""Render the production Home/Passport functions, with only data and hardware stubbed."""
from pathlib import Path
import re
import sys
source = Path(sys.argv[1]).read_text()
functions = []
for name in ('style_plain', 'label_at', 'new_screen', 'create_menu_row', 'build_home', 'build_passport', 'create_field', 'create_species', 'build_encounter', 'build_bestiary_list', 'build_bestiary_detail'):
    match = re.search(r'^static [^\n]*\b' + name + r'\(', source, re.M)
    assert match, name
    end = source.index('\n}\n', match.start()) + 3
    function = source[match.start():end]
    if name == 'label_at':
        function = function.replace('    lv_obj_t *label = lv_label_create(parent);', '    lv_point_t original_size;\n    lv_text_get_size(&original_size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);\n    if (original_size.x > width) {\n        fprintf(stderr, "Original text clips: %s (%d > %d)\\n", text, (int)original_size.x, width);\n        assert(0);\n    }\n    lv_obj_t *label = lv_label_create(parent);')
    functions.append(function)
constants = '\n'.join(re.findall(r'^#define (?:COLOR_\w+|SCREEN_WIDTH|TITLE_Y|META_Y|FOOTER_Y|CONTENT_TOP|CONTENT_HEIGHT)\s+[^\n]+', source, re.M))
harness = Path(__file__).with_name('harness.c').read_text()
Path(sys.argv[2]).write_text(harness.replace('/* PRODUCTION */', constants + '\n' + '\n'.join(functions)))
