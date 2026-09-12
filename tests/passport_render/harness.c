#include "lvgl.h"
#include "ui_fonts.h"
#include "src/misc/lv_text_private.h"
#include "src/widgets/label/lv_label_private.h"
#include "charmander_sprite.h"
#include "starter_sprites.h"
#include "roster_sprites.h"
#include "passport_progress.h"
#include "pocket_policy.h"
#include "user_settings.h"
#include "capture_engine.h"
#include "game_loop.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#define ESP_LOGI(...) ((void)0)
static lv_obj_t *s_screen, *s_status, *s_battery_label, *s_field, *s_wild_countdown;
static lv_obj_t *s_ball, *s_ball_red, *s_ball_band, *s_ball_button, *s_ball_button_inner;
static uint16_t s_evolution_source_id;
static uint8_t s_evolution_selection;
static uint8_t s_action_selection, s_release_selection;
static uint16_t s_release_copy_selection, s_owned_selection;
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
static city_capture_round_t s_round;
static uint8_t s_attempts = 3;
static uint64_t s_capture_deadline_ms = 15000;
#define UI_ESCAPED 0
static void set_state(int state) { (void)state; assert(!"Unexpected state change during render"); }
static void start_capture_round(uint64_t now) { (void)now; assert(!"Unexpected new round during render"); }
static bool s_new_place_stamp;
static uint16_t s_current_place_id = 2, s_capture_bond_gain;
static const char *s_capture_feedback = "Wait";
static lv_obj_t *s_meter, *s_target, *s_marker, *s_ring, *s_aim_cue, *s_aim_status;
typedef enum { WRITE_NONE, WRITE_DISCOVERY, WRITE_CAPTURE, WRITE_WILD, WRITE_WILD_CLEAR,
    WRITE_BUDDY, WRITE_EVOLUTION, WRITE_RECOVER, WRITE_RELEASE } write_operation_t;
static write_operation_t s_pending_write;
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
static lv_area_t label_bounds[80];
static const char *label_text[80];
static unsigned label_count;
static void check_labels(lv_obj_t *o)
{
    if (lv_obj_check_type(o, &lv_label_class)) {
        const char *text = lv_label_get_text(o);
        if (((lv_label_t *)o)->dot_begin != UINT32_MAX) {
            fprintf(stderr,"Ellipsized label: %s\n",text); assert(0);
        }
        const lv_font_t *font = lv_obj_get_style_text_font(o, 0);
        for (uint32_t i=0; text[i]; ) {
            const uint32_t cp = lv_text_encoded_next(text, &i);
            if (cp == '\n' || cp == '\r') continue;
            lv_font_glyph_dsc_t glyph;
            assert(lv_font_get_glyph_dsc(font, &glyph, cp, 0) && !glyph.is_placeholder);
        }
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
        lv_area_t parent; lv_obj_get_coords(lv_obj_get_parent(o), &parent);
        if (!(bounds.x1 >= parent.x1 && bounds.y1 >= parent.y1 && bounds.x2 <= parent.x2 && bounds.y2 <= parent.y2)) {
            fprintf(stderr,"Outside parent: %s (%d,%d,%d,%d) in (%d,%d,%d,%d)\n",text,
                    bounds.x1,bounds.y1,bounds.x2,bounds.y2,parent.x1,parent.y1,parent.x2,parent.y2); assert(0);
        }
        if (*text) {
            for (unsigned i=0;i<label_count;++i) {
                const lv_area_t *b=&label_bounds[i];
                if (bounds.x1<=b->x2 && bounds.x2>=b->x1 && bounds.y1<=b->y2 && bounds.y2>=b->y1) {
                    fprintf(stderr,"Text overlap: %s / %s\n",text,label_text[i]); assert(0);
                }
            }
            assert(label_count<80); label_bounds[label_count]=bounds; label_text[label_count++]=text;
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); ++i) check_labels(lv_obj_get_child(o, i));
}
static void snapshot(const char *name, unsigned mode)
{
    lv_obj_t *old = s_screen;
    if (mode == 14) build_capture_ready();
    else if (mode == 15 || mode == 22 || mode == 23) {
        build_aim();
        update_aim(mode == 22 ? 2100 : 1000);
    }
    else if (mode == 16) build_captured();
    else if (mode == 17) build_escaped();
    else if (mode == 18) build_bestiary_hint();
    else if (mode == 19) build_scanning();
    else if (mode == 20) render_UI_PLACE_ERROR();
    else if (mode == 24) render_UI_PLACE_PENDING();
    else if (mode == 25) render_UI_PLACE_GRAY();
    else if (mode == 26 || mode == 27) {
        render_UI_PLACE_WILD();
        lv_label_set_text(s_wild_countdown, mode == 26 ? "Ready!" : "Wait 30:00");
        if (mode == 27) lv_label_set_text(s_status, LV_SYMBOL_UP " Go home");
    }
    else if (mode == 28) render_UI_LOW_BATTERY();
    else if (mode == 29) render_UI_PLACE_UNSTABLE();
    else if (mode == 30) render_UI_PLACE_STORAGE_ERROR();
    else if (mode == 31) render_UI_PLACE_FULL();
    else if (mode == 21) build_storage_error();
    else if (mode == 32) build_owned_detail();
    else if (mode == 13) build_settings();
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
    label_count=0;
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
int main(int argc, char **argv)
{
    lv_init();
    lv_display_t *d = lv_display_create(240, 320);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(d, draw_buffer, NULL, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush);
    city_bestiary_init(&s_bestiary);
    s_current_species_id = CITY_SPECIES_CHARMANDER;
    s_round = (city_capture_round_t){.duration_ms=4200, .target_center_ms=2100, .target_half_width_ms=700};
    snapshot("capture-guide", 14);
    snapshot("aim", 15);
    snapshot("aim-now", 22);
    s_attempts = 2; s_capture_feedback = "Too early"; snapshot("aim-retry", 23); s_attempts = 3; s_capture_feedback = "Wait";
    snapshot("capture-first", 16);
    snapshot("escape-early", 17);
    snapshot("unknown-hint", 18);
    snapshot("scanning", 19);
    snapshot("scan-error", 20);
    snapshot("place-pending",24); snapshot("place-gray",25);
    snapshot("wild-ready",26); snapshot("wild-wait",27);
    snapshot("low-battery",28); snapshot("place-unstable",29);
    snapshot("place-save-error",30); snapshot("place-full",31);
    const char *feedback[] = {"Too early", "Too late", "No throw", "Time is up! Watch for NOW."};
    for (unsigned i=0;i<4;++i) {
        char name[40]; s_capture_feedback = feedback[i];
        snprintf(name,sizeof(name),"escape-%u",i); snapshot(name,17);
    }
    s_capture_feedback = "Wait";
    for (unsigned op = WRITE_DISCOVERY; op <= WRITE_RELEASE; ++op) {
        s_pending_write = op;
        char name[40]; snprintf(name,sizeof(name),"storage-error-%u",op); snapshot(name,21);
    }
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
    s_settings_selection = 2; s_settings_editing = true; snapshot("settings-light-edit", 13);
    s_settings_selection = 0; snapshot("settings-volume-edit", 13);
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
        snprintf(name,sizeof(name),"aim-%03u",s_current_species_id); snapshot(name,15);
        snprintf(name,sizeof(name),"capture-guide-%03u",s_current_species_id); snapshot(name,14);
        snprintf(name,sizeof(name),"capture-saved-%03u",s_current_species_id); snapshot(name,16);
        s_release_copy_selection = 0;
        snprintf(name,sizeof(name),"release-picker-%03u",s_current_species_id); snapshot(name,10);
        s_release_selection = 1;
        snprintf(name,sizeof(name),"release-last-%03u",s_current_species_id); snapshot(name,11);
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
    s_release_copy_selection = city_bestiary_owned_count(&s_bestiary, CITY_SPECIES_PIKACHU);
    snapshot("release-back", 10);
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
    s_current_species_id = CITY_SPECIES_CHARMANDER;
    s_new_place_stamp = true; s_capture_bond_gain = 3;
    snapshot("capture-progress",16);
    s_new_place_stamp = false; s_capture_bond_gain = 0;
    snapshot("capture-bond-max",16);
    s_home_selection = 2;
    snapshot("home-passport", true);
    snapshot("passport-two-places", false);
    fixture.count = 16;
    for (unsigned i = 0; i < 16; ++i) fixture.place_ids[i] = i + 1;
    s_passport_page = 2;
    snapshot("passport-complete", false);
    /* Actual individual values remain distinct across encode/decode and rendering. */
    city_bestiary_init(&s_bestiary);
    const city_creature_stats_t copy_stats[] = {{35,55,40}, {49,66,51}};
    for (unsigned i=0;i<2;++i)
        assert(city_bestiary_capture_with_stats(&s_bestiary,i+1,25,i+1,&copy_stats[i],persist,NULL)==CITY_BESTIARY_APPLIED);
    assert(city_bestiary_apply_damage(&s_bestiary,25,5,persist,NULL)==CITY_BESTIARY_APPLIED);
    s_bestiary_selection=city_species_index(25);s_action_selection=0;
    snapshot("actions-needs-heal",9);
    for (unsigned i=0;i<2;++i) {
        char name[40];s_owned_selection=i;snprintf(name,sizeof(name),"own-copy-%u",i+1);snapshot(name,32);
    }
    s_bestiary.owned[0].migrated=true;s_owned_selection=0;snapshot("own-legacy",32);
    assert(city_bestiary_recover(&s_bestiary,25,persist,NULL)==CITY_BESTIARY_APPLIED);
    snapshot("actions-full",9);
    /* Optional local, already-decoded device save; no hardware or writes involved. */
    if (argc==2) {
        uint8_t encoded[CITY_BESTIARY_ENCODED_BYTES];
        FILE *save=fopen(argv[1],"rb");assert(save);
        assert(fread(encoded,1,sizeof(encoded),save)==sizeof(encoded));fclose(save);
        assert(city_bestiary_decode(encoded,sizeof(encoded),&s_bestiary));
        for (unsigned species=0;species<CITY_SPECIES_COUNT;++species) {
            s_bestiary_selection=species;
            const uint16_t id=city_species_id_at(species);
            const uint16_t count=city_bestiary_owned_count(&s_bestiary,id);
            if (!count) continue;
            char name[60];snprintf(name,sizeof(name),"device-actions-%03u",id);snapshot(name,9);
            for (unsigned copy=0;copy<count;++copy) {
                s_owned_selection=copy;snprintf(name,sizeof(name),"device-copy-%03u-%u",id,copy+1);snapshot(name,32);
            }
        }
    }
    have_places = false; s_bestiary_ready = false;
    snapshot("passport-unavailable", false);
    puts("Production renders passed: text fit, glyphs, parent bounds, and no text overlaps");
    return 0;
}
