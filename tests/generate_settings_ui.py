"""Test production Settings intents and completion, with only hardware/UI stubbed."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()
functions = []
for name in ('visible_settings', 'apply_settings_preview', 'close_settings',
             'handle_settings_button', 'finish_settings_save'):
    import re
    match = re.search(r'^static [^\n]*\b' + name + r'\(', source, re.M)
    assert match, name
    end = source.index('\n}\n', match.start()) + 3
    functions.append(source[match.start():end])
fixture = r'''
#include "user_settings.h"
#include "pocket_policy.h"
#include "passport_progress.h"
#include <assert.h>
#define ESP_OK 0
#define ESP_LOGI(...) ((void)0)
#define pdTRUE 1
typedef enum { UI_HOME, UI_SETTINGS } ui_state_t;
typedef enum { BSP_BTN_UP, BSP_BTN_DOWN, BSP_BTN_OK } bsp_btn_t;
typedef struct { city_settings_t settings; int error; } settings_result_t;
static ui_state_t s_state;
static city_settings_t s_settings, s_settings_draft, queued;
static uint8_t s_settings_selection;
static bool s_settings_editing, s_settings_saving, s_settings_error, s_settings_load_error;
static void *s_settings_requests = &queued;
static struct { bool screen_off; } s_pocket;
static struct { uint16_t buddy_species_id; } s_bestiary;
static int s_battery_soc = 90;
static unsigned writes, brightness, volume, cry;
static bool muted, fail_queue;
static void set_state(ui_state_t state) { s_state = state; }
static void bsp_display_backlight(uint8_t percent) { brightness = percent; }
static void pokemon_audio_set_preferences(uint8_t percent, bool mute) { volume = percent; muted = mute; }
static void pokemon_audio_play(uint16_t id) { cry = id; }
static void pokemon_audio_stop(void) { cry = 0; }
static int xQueueOverwrite(void *queue, const void *data) {
    assert(queue == &queued);
    if (fail_queue) return 0;
    queued = *(const city_settings_t *)data; ++writes; return pdTRUE;
}
/* PRODUCTION */
static void enter(void) {
    s_state = UI_SETTINGS; s_settings_draft = s_settings;
    s_settings_selection = 0; s_settings_editing = false; s_settings_error = false;
}
int main(void) {
    s_settings = city_settings_defaults(); enter(); apply_settings_preview();
    assert(volume == 60 && brightness == 60 && !muted &&
           s_settings.language == CITY_LANGUAGE_ENGLISH);
    handle_settings_button(BSP_BTN_OK, false);
    assert(s_settings_draft.language == CITY_LANGUAGE_SIMPLIFIED_CHINESE &&
           s_settings.language == CITY_LANGUAGE_ENGLISH && !writes);
    handle_settings_button(BSP_BTN_OK, true);
    assert(s_state == UI_HOME && s_settings.language == CITY_LANGUAGE_ENGLISH);
    enter();
    handle_settings_button(BSP_BTN_DOWN, false);
    handle_settings_button(BSP_BTN_OK, false); assert(s_settings_editing);
    handle_settings_button(BSP_BTN_UP, false); assert(volume == 70 && s_settings.volume == 60 && !writes);
    handle_settings_button(BSP_BTN_OK, false); assert(!s_settings_editing && cry == CITY_SPECIES_PIKACHU);
    handle_settings_button(BSP_BTN_DOWN, false); handle_settings_button(BSP_BTN_OK, false);
    assert(muted && s_settings_draft.volume == 70);
    handle_settings_button(BSP_BTN_DOWN, false); handle_settings_button(BSP_BTN_OK, false);
    handle_settings_button(BSP_BTN_DOWN, false); assert(brightness == 50);
    handle_settings_button(BSP_BTN_OK, true);
    assert(s_state == UI_HOME && brightness == 60 && volume == 60 && !muted && !cry && !writes);
    enter(); handle_settings_button(BSP_BTN_UP, false); assert(s_settings_selection == 5);
    handle_settings_button(BSP_BTN_OK, false); assert(s_state == UI_HOME);
    enter(); s_settings_selection = 4; handle_settings_button(BSP_BTN_OK, false);
    assert(!writes && s_state == UI_HOME); /* Unchanged settings don't wear flash. */
    enter(); s_settings_draft.volume = 20; s_settings_draft.muted = true;
    s_settings_draft.language = CITY_LANGUAGE_SIMPLIFIED_CHINESE;
    s_settings_selection = 4; fail_queue = true; handle_settings_button(BSP_BTN_OK, false);
    assert(s_settings_error && !s_settings_saving && s_state == UI_SETTINGS);
    fail_queue = false; handle_settings_button(BSP_BTN_OK, false);
    assert(s_settings_saving && writes == 1 && s_settings.volume == 60);
    handle_settings_button(BSP_BTN_OK, true); assert(s_state == UI_SETTINGS && s_settings_saving);
    settings_result_t result = {queued, -1}; finish_settings_save(&result);
    assert(s_state == UI_SETTINGS && s_settings_error && s_settings.volume == 60);
    handle_settings_button(BSP_BTN_OK, false); assert(writes == 2 && s_settings_saving);
    result.error = ESP_OK; finish_settings_save(&result);
    assert(s_state == UI_HOME && !s_settings_saving && s_settings.volume == 20 &&
           s_settings.muted &&
           s_settings.language == CITY_LANGUAGE_SIMPLIFIED_CHINESE);
    assert(volume == 20 && muted);
    s_settings.brightness = 80; s_battery_soc = 5; apply_settings_preview(); assert(brightness == 30);
    s_battery_soc = 90; apply_settings_preview(); assert(brightness == 80);
    s_pocket.screen_off = true; apply_settings_preview(); assert(brightness == 0);
    s_pocket.screen_off = false; apply_settings_preview(); assert(brightness == 80);
    return 0;
}
'''
Path(sys.argv[2]).write_text(fixture.replace('/* PRODUCTION */', '\n'.join(functions)))
