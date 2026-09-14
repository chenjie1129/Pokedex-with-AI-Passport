"""Render the production Home/Passport functions, with only data and hardware stubbed."""
from pathlib import Path
import re
import sys
source = Path(sys.argv[1]).read_text()
functions = []
for name in ('style_plain', 'label_at', 'new_screen', 'create_field', 'create_species', 'build_home', 'build_settings', 'build_passport', 'ball_geometry', 'create_ball', 'build_catching', 'position_capture_target', 'build_capture_ready', 'build_aim', 'update_aim', 'build_captured', 'build_escaped', 'build_bestiary_hint', 'build_scanning', 'build_place_status', 'build_place_pending', 'build_storage_error', 'build_throwing', 'build_evolution', 'evolution_reveal_y', 'build_evolved', 'build_encounter', 'build_bestiary_list', 'build_bestiary_detail', 'build_pokemon_actions', 'build_owned_detail', 'build_companion', 'build_buddy_reaction', 'build_release_picker', 'build_release_confirm', 'build_released'):
    match = re.search(r'^static [^\n]*\b' + name + r'\(', source, re.M)
    assert match, name
    end = source.index('\n}\n', match.start()) + 3
    function = source[match.start():end]
    if name == 'label_at':
        function = function.replace('    lv_obj_t *label = lv_label_create(parent);', '    const char *rendered = tr(text);\n    lv_point_t original_size;\n    lv_text_get_size(&original_size, rendered, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);\n    if (original_size.x > width) {\n        fprintf(stderr, "Original text clips: %s (%d > %d)\\n", rendered, (int)original_size.x, width);\n        assert(0);\n    }\n    lv_obj_t *label = lv_label_create(parent);')
    functions.append(function)
# Render place feedback directly from the production switch, not copied strings.
for state in ('UI_PLACE_PENDING', 'UI_PLACE_GRAY', 'UI_PLACE_WILD', 'UI_LOW_BATTERY',
              'UI_PLACE_UNSTABLE', 'UI_PLACE_ERROR', 'UI_PLACE_STORAGE_ERROR', 'UI_PLACE_FULL'):
    match = re.search(r'case ' + state + r':\n(.*?)        break;', source, re.S)
    assert match, state
    functions.append('static void render_' + state + '(void)\n{\n' + match.group(1) + '}\n')
constants = '\n'.join(re.findall(r'^#define (?:COLOR_\w+|SCREEN_WIDTH|TITLE_Y|META_Y|FOOTER_Y|CONTENT_TOP|CONTENT_HEIGHT)\s+[^\n]+', source, re.M))
harness = Path(__file__).with_name('harness.c').read_text()
Path(sys.argv[2]).write_text(harness.replace('/* PRODUCTION */', constants + '\n' + '\n'.join(functions)))
