#include "lvgl.h"
#include "charmander_sprite.h"
#include "starter_sprites.h"
#include "roster_sprites.h"
#include "passport_progress.h"
#include "pocket_policy.h"
#include "user_settings.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#define ESP_LOGI(...) ((void)0)
static lv_obj_t *s_screen, *s_status, *s_battery_label, *s_field;
static lv_obj_t *s_ball, *s_ball_red, *s_ball_band, *s_ball_button, *s_ball_button_inner;
static uint16_t s_evolution_source_id;
static uint8_t s_evolution_selection;
static uint8_t s_action_selection, s_release_selection;
static uint16_t s_release_copy_selection;
static uint32_t s_release_instance_id;
static uint8_t s_encounter_selection, s_bestiary_selection;
static uint16_t s_current_species_id;
static city_creature_stats_t s_current_stats;
static city_discovery_state_t s_encounter_previous_state;
static int s_battery_soc = 95;
static uint8_t s_home_selection, s_passport_page;
static city_settings_t s_settings_draft;
static uint8_t s_settings_selection;
static bool s_settings_editing, s_settings_saving, s_settings_error, s_settings_load_error;
static city_bestiary_t s_bestiary;
static bool s_bestiary_ready = true;
static bool have_places = true;
static city_passport_stamps_t fixture;
static bool place_scan_coordinator_passport(city_passport_stamps_t *s)
{ *s = fixture; return have_places; }
/* PRODUCTION */
static uint16_t framebuffer[240 * 320], draw_buffer[240 * 20];
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *pixels)
{
    const uint16_t *src = (const uint16_t *)pixels;
    for (int y = a->y1; y <= a->y2; ++y) {
        memcpy(framebuffer + y * 240 + a->x1, src, (a->x2 - a->x1 + 1) * 2);
        src += a->x2 - a->x1 + 1;
    }
    lv_display_flush_ready(d);
}
static void check_labels(lv_obj_t *o)
{
    if (lv_obj_check_type(o, &lv_label_class)) {
        lv_point_t size;
        const bool wrapped = lv_label_get_long_mode(o) == LV_LABEL_LONG_WRAP;
        lv_text_get_size(&size, lv_label_get_text(o), lv_obj_get_style_text_font(o, 0),
                        lv_obj_get_style_text_letter_space(o, 0),
                        lv_obj_get_style_text_line_space(o, 0),
                        wrapped ? lv_obj_get_content_width(o) : LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x > lv_obj_get_content_width(o) || size.y > lv_obj_get_content_height(o)) {
            fprintf(stderr, "Clipped label: %s (%dx%d in %dx%d)\n", lv_label_get_text(o), size.x, size.y,
                    (int)lv_obj_get_content_width(o), (int)lv_obj_get_content_height(o));
            assert(0);
        }
        lv_area_t bounds; lv_obj_get_coords(o, &bounds);
        assert(bounds.x1 >= 0 && bounds.y1 >= 0 && bounds.x2 < 240 && bounds.y2 < 320);
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); ++i) check_labels(lv_obj_get_child(o, i));
}
static void snapshot(const char *name, unsigned mode)
{
    lv_obj_t *old = s_screen;
    if (mode == 13) build_settings();
    else if (mode == 7) build_evolution();
    else if (mode == 8) build_evolved();
    else if (mode == 9) build_pokemon_actions();
    else if (mode == 10) build_release_picker();
    else if (mode == 11) build_release_confirm();
    else if (mode == 12) build_released();
    else if (mode == 6) build_throwing();
    else if (mode == 5) build_catching();
    else if (mode == 1) build_home();
    else if (mode == 2) build_encounter();
    else if (mode == 3) build_bestiary_list();
    else if (mode == 4) build_bestiary_detail();
    else build_passport();
    lv_screen_load(s_screen);
    if (old) lv_obj_delete(old);
    lv_obj_update_layout(s_screen);
    check_labels(s_screen);
    lv_refr_now(NULL);
    char path[100]; snprintf(path, sizeof(path), "%s.ppm", name);
    FILE *f = fopen(path, "wb"); assert(f);
    fprintf(f, "P6\n240 320\n255\n");
    for (unsigned i = 0; i < 240 * 320; ++i) {
        uint16_t c = framebuffer[i];
        uint8_t rgb[3] = {((c >> 11) & 31) * 255 / 31, ((c >> 5) & 63) * 255 / 63, (c & 31) * 255 / 31};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
static bool persist(const city_bestiary_t *b, void *ctx) { (void)b; (void)ctx; return true; }
int main(void)
{
    lv_init();
    lv_display_t *d = lv_display_create(240, 320);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d, draw_buffer, NULL, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush);
    city_bestiary_init(&s_bestiary);
    snapshot("ball-fixed", 5);
    snapshot("ball-launch", 6);
    snapshot("home-no-buddy", 1);
    snapshot("passport-empty", false);
    snapshot("bestiary-unknown", 3);
    s_home_selection = 3; snapshot("home-settings", 1); s_home_selection = 0;
    s_settings_draft = city_settings_defaults();
    snapshot("settings-default", 13);
    for (unsigned selected = 0; selected < 5; ++selected) {
        char name[40]; s_settings_selection = selected;
        snprintf(name, sizeof(name), "settings-row-%u", selected); snapshot(name, 13);
    }
    s_settings_selection = 0; s_settings_editing = true; snapshot("settings-volume-edit", 13);
    s_settings_draft.volume = 100; s_settings_draft.brightness = 100;
    snapshot("settings-max", 13);
    s_settings_draft.muted = true; snapshot("settings-muted", 13);
    s_settings_editing = false; s_settings_draft.volume = 0; s_settings_draft.brightness = 10;
    snapshot("settings-min", 13);
    s_settings_error = true; snapshot("settings-error", 13); s_settings_error = false;
    s_settings_load_error = true; snapshot("settings-load-error", 13); s_settings_load_error = false;
    s_settings_saving = true; snapshot("settings-saving", 13); s_settings_saving = false;
    s_battery_soc = 5; snapshot("settings-low-battery", 13); s_battery_soc = 95;
    /* Information is readable even when an encounter has not been caught. */
    for (unsigned i = 0; i < CITY_SPECIES_COUNT; ++i) {
        assert(city_bestiary_mark_seen(&s_bestiary, city_species_id_at(i), persist, NULL) == CITY_BESTIARY_APPLIED);
        s_bestiary_selection = i;
        char name[80];
        snprintf(name, sizeof(name), "seen-detail-%03u", city_species_id_at(i));
        snapshot(name, 4);
    }
    for (unsigned i = 0; i < CITY_SPECIES_COUNT; ++i)
        assert(city_bestiary_capture(&s_bestiary, i + 1, city_species_id_at(i), 1, persist, NULL) == CITY_BESTIARY_APPLIED);
    for (unsigned i = 0; i <= CITY_SPECIES_COUNT; ++i) {
        char name[80];
        s_bestiary_selection = i;
        snprintf(name,sizeof(name),"bestiary-row-%02u",i); snapshot(name,3);
        if (i == CITY_SPECIES_COUNT) continue;
        snprintf(name,sizeof(name),"detail-%03u",city_species_id_at(i)); snapshot(name,4);
        s_action_selection = 0; snprintf(name,sizeof(name),"actions-%03u",city_species_id_at(i)); snapshot(name,9);
        s_current_species_id = city_species_id_at(i);
        s_current_stats = s_bestiary.records[i].latest_stats;
        for (unsigned status = 0; status < 3; ++status) {
            s_encounter_previous_state = status;
            snprintf(name,sizeof(name),"encounter-%03u-status-%u",s_current_species_id,status); snapshot(name,2);
        }
    }
    s_bestiary_selection = city_species_index(CITY_SPECIES_PIKACHU);
    assert(city_bestiary_capture(&s_bestiary, CITY_SPECIES_COUNT + 1U,
        CITY_SPECIES_PIKACHU, 1, persist, NULL) == CITY_BESTIARY_APPLIED);
    assert(city_bestiary_capture(&s_bestiary, CITY_SPECIES_COUNT + 2U,
        CITY_SPECIES_PIKACHU, 1, persist, NULL) == CITY_BESTIARY_APPLIED);
    s_release_copy_selection = 1; snapshot("release-picker",10);
    const city_owned_pokemon_t *release = city_bestiary_owned_at(
        &s_bestiary, CITY_SPECIES_PIKACHU, s_release_copy_selection);
    assert(release); s_release_instance_id = release->instance_id;
    s_release_selection = 1; snapshot("release-confirm-keep",11);
    s_release_selection = 0; snapshot("release-confirm-release",11);
    assert(city_bestiary_release_instance(&s_bestiary, s_release_instance_id,
        persist, NULL) == CITY_BESTIARY_APPLIED);
    assert(city_bestiary_record_const(&s_bestiary,
        CITY_SPECIES_PIKACHU)->capture_count == 2U);
    snapshot("released",12);
    for (unsigned i = 0; i < CITY_SPECIES_COUNT; ++i) {
        assert(city_bestiary_choose_buddy(&s_bestiary, city_species_id_at(i), persist, NULL) == CITY_BESTIARY_APPLIED);
        char name[80];
        for (unsigned points = 0; points <= 100; points += 50) {
            s_bestiary.records[i].friendship = points;
            snprintf(name,sizeof(name),"buddy-%03u-%u",city_species_id_at(i),points); snapshot(name,1);
            s_bestiary_selection = i;
            snprintf(name,sizeof(name),"buddy-detail-%03u-%u",city_species_id_at(i),points); snapshot(name,4);
        }
    }
    for (unsigned i = 0; i < 3; ++i) {
        s_evolution_source_id = city_species_id_at(i);
        city_bestiary_t empty; city_bestiary_init(&empty);
        unsigned target_index = city_species_index(city_evolution_target(s_evolution_source_id));
        const uint16_t target_id = city_evolution_target(s_evolution_source_id);
        for (uint16_t j = 0; j < s_bestiary.owned_count; ) {
            if (s_bestiary.owned[j].species_id != target_id) { ++j; continue; }
            for (uint16_t k = j + 1U; k < s_bestiary.owned_count; ++k)
                s_bestiary.owned[k - 1U] = s_bestiary.owned[k];
            --s_bestiary.owned_count;
            memset(&s_bestiary.owned[s_bestiary.owned_count], 0,
                   sizeof(s_bestiary.owned[0]));
        }
        s_bestiary.records[target_index] = empty.records[target_index];
        s_bestiary.buddy_species_id = s_evolution_source_id;
        char name[80];
        for (unsigned ready = 0; ready < 2; ++ready) {
            s_bestiary.records[i].friendship = ready ? 30 : 29;
            s_bestiary.records[i].buddy_places = ready ? 7 : 3;
            s_evolution_selection = ready ? 0 : 1;
            snprintf(name,sizeof(name),"evolution-%u-ready-%u",s_evolution_source_id,ready);snapshot(name,7);
        }
        assert(city_bestiary_evolve(&s_bestiary,s_evolution_source_id,persist,NULL)==CITY_BESTIARY_APPLIED);
        snprintf(name,sizeof(name),"evolved-%u",s_evolution_source_id);snapshot(name,8);
        s_bestiary_selection=city_species_index(city_evolution_target(s_evolution_source_id));
        snprintf(name,sizeof(name),"evolved-detail-%u",s_evolution_source_id);snapshot(name,4);
    }
    fixture.count = 2; fixture.place_ids[0] = 1; fixture.place_ids[1] = 2;
    s_home_selection = 2;
    snapshot("home-passport", true);
    snapshot("passport-two-places", false);
    fixture.count = 16;
    for (unsigned i = 0; i < 16; ++i) fixture.place_ids[i] = i + 1;
    s_passport_page = 2;
    snapshot("passport-complete", false);
    have_places = false; s_bestiary_ready = false;
    snapshot("passport-unavailable", false);
    puts("Production LVGL renders and label bounds passed");
    return 0;
}
