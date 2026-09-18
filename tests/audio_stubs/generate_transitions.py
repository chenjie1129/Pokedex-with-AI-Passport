"""Exercise the production screen transition function without display hardware."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text()
start = source.index('static void set_state(ui_state_t state)\n{')
end = source.index('\n}\n', start) + 3
fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
typedef enum { UI_HOME, UI_ENCOUNTER, UI_AIM, UI_BESTIARY_LIST,
               UI_BESTIARY_DETAIL, UI_POKEMON_ACTIONS } ui_state_t;
typedef int lv_obj_t;
static lv_obj_t *s_screen;
static ui_state_t s_state;
static uint64_t s_state_started_ms;
static struct { uint64_t last_activity_ms; } s_pocket;
static uint16_t s_current_species_id;
static uint32_t s_bestiary_selection;
static unsigned commands;
static uint16_t last_id;
static uint64_t now_ms(void) { return 1; }
static void build_state(void) {}
static void lv_obj_delete(lv_obj_t *obj) { (void)obj; }
static uint16_t city_species_runtime_id(uint32_t i) { assert(i == 3); return 25; }
static void pokemon_audio_play(uint16_t id) { ++commands; last_id = id; }
static void pokemon_audio_stop(void) { pokemon_audio_play(0); }
/* PRODUCTION */
int main(void) {
    s_current_species_id = 4;
    set_state(UI_ENCOUNTER); assert(commands == 1 && last_id == 4);
    set_state(UI_ENCOUNTER); assert(commands == 1); /* Select CATCH/RUN */
    set_state(UI_ENCOUNTER); assert(commands == 1);
    set_state(UI_AIM); assert(commands == 2 && last_id == 0);
    set_state(UI_HOME); assert(commands == 2);
    set_state(UI_BESTIARY_LIST); assert(commands == 2);
    s_bestiary_selection = 3;
    set_state(UI_BESTIARY_DETAIL); assert(commands == 3 && last_id == 25);
    set_state(UI_BESTIARY_DETAIL); assert(commands == 3);
    set_state(UI_POKEMON_ACTIONS); assert(commands == 4 && last_id == 0);
    set_state(UI_BESTIARY_DETAIL); assert(commands == 4); /* HP recovery */
    set_state(UI_BESTIARY_LIST); assert(commands == 5 && last_id == 0);
    set_state(UI_BESTIARY_DETAIL); assert(commands == 6 && last_id == 25);
    set_state(UI_HOME); assert(commands == 7 && last_id == 0);
    s_current_species_id = 1;
    set_state(UI_ENCOUNTER); assert(commands == 8 && last_id == 1);
    return 0;
}
'''
Path(sys.argv[2]).write_text(fixture.replace('/* PRODUCTION */', source[start:end]))
