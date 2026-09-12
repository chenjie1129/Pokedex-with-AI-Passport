#include "bsp_bestiary_store.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "pocket_policy.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "capture_engine.h"
#include "city_build_identity.h"
#include "encounter_selector.h"
#include "game_loop.h"
#include "charmander_sprite.h"
#include "starter_sprites.h"
#include "roster_sprites.h"
#include "place_scan_coordinator.h"
#include "pokemon_audio.h"
#include "bsp_settings_store.h"
#include "ui_strings.h"
#include "user_settings.h"
#ifdef CITY_AUDIO_RENDER_SMOKE
#include "pokemon_cries.h"
#endif

#include "esp_log.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "ui_fonts.h"
#include "nvs_flash.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define COLOR_INK       0x183238
#define COLOR_MUTED     0x60777A
#define COLOR_PAPER     0xFFFDF7
#define COLOR_SKY       0xBCE6F2
#define COLOR_GRASS     0x66AE79
#define COLOR_GRASS_D   0x438A62
#define COLOR_GREEN     0x55B96D
#define COLOR_CORAL     0xEF5B4F
#define COLOR_YELLOW    0xFFD45F
#define THROW_MS        UINT64_C(850)
#define CATCH_MS        UINT64_C(1200)
#define SCREEN_WIDTH    240
#define TITLE_Y         42
#define CONTENT_TOP     73
#define CONTENT_HEIGHT  170
#define META_Y          251
#define FOOTER_Y        284
#define TITLE_MAX_HEIGHT 24
#define META_MAX_HEIGHT  18

_Static_assert(
    TITLE_Y + TITLE_MAX_HEIGHT < CONTENT_TOP,
    "title and content regions overlap");
_Static_assert(
    CONTENT_TOP + CONTENT_HEIGHT < META_Y,
    "content and metadata regions overlap");
_Static_assert(
    META_Y + META_MAX_HEIGHT < FOOTER_Y,
    "metadata and footer regions overlap");

typedef enum {
    UI_HOME = 0,
    UI_SCANNING,
    UI_PLACE_PENDING,
    UI_PLACE_GRAY,
    UI_PLACE_WILD,
    UI_PLACE_UNSTABLE,
    UI_PLACE_ERROR,
    UI_PLACE_STORAGE_ERROR,
    UI_PLACE_FULL,
    UI_ENCOUNTER,
    UI_AIM,
    UI_THROWING,
    UI_CATCHING,
    UI_CAPTURED,
    UI_ESCAPED,
    UI_ABANDONED,
    UI_BESTIARY_LIST,
    UI_BESTIARY_DETAIL,
    UI_STORAGE_ERROR,
    UI_LOW_BATTERY,
    UI_PASSPORT,
    UI_EVOLUTION,
    UI_EVOLVED,
    UI_POKEMON_ACTIONS,
    UI_RELEASE_PICKER,
    UI_RELEASE_CONFIRM,
    UI_RELEASED,
    UI_SETTINGS,
    UI_CAPTURE_READY,
    UI_BESTIARY_HINT,
    UI_OWNED_DETAIL,
} ui_state_t;

typedef enum {
    WRITE_NONE = 0,
    WRITE_DISCOVERY,
    WRITE_CAPTURE,
    WRITE_WILD,
    WRITE_WILD_CLEAR,
    WRITE_BUDDY,
    WRITE_EVOLUTION,
    WRITE_RECOVER,
    WRITE_RELEASE,
} write_operation_t;

static const char *TAG = "pokedex";

static ui_state_t s_state;
static uint64_t s_state_started_ms;
static uint8_t s_attempts = 3;
static city_bestiary_t s_bestiary;
static bool s_bestiary_ready;
static bool s_save_in_progress;
static uint16_t s_evolution_source_id;
static uint8_t s_evolution_selection = 1;
static uint8_t s_action_selection;
static uint16_t s_release_copy_selection;
static uint32_t s_release_instance_id;
static uint16_t s_owned_selection;
static uint8_t s_release_selection = 1;
static uint8_t s_home_selection;
static city_settings_t s_settings, s_settings_draft;
static uint8_t s_settings_selection;
static bool s_settings_editing, s_settings_saving, s_settings_error, s_settings_load_error;
static QueueHandle_t s_settings_requests, s_settings_results;

typedef struct { city_settings_t settings; esp_err_t error; } settings_result_t;

static const city_settings_t *visible_settings(void)
{
    return s_state == UI_SETTINGS ? &s_settings_draft : &s_settings;
}

static city_language_t visible_language(void)
{
    return visible_settings()->language;
}

static const char *tr(const char *english)
{
    return ui_text(visible_language(), english);
}

static const char *species_name(const city_species_definition_t *definition)
{
    return ui_species_name(visible_language(), definition->name);
}

static uint8_t s_passport_page;
static uint8_t s_encounter_selection;
static city_discovery_state_t s_encounter_previous_state;
static uint8_t s_bestiary_selection;
static write_operation_t s_pending_write;
static uint64_t s_encounter_sequence;
static bool s_throw_hit;
static bool s_new_place_stamp;
static uint16_t s_capture_bond_gain;
static const char *s_capture_feedback = "Wait";
static lv_obj_t *s_aim_status, *s_aim_cue;
static city_capture_round_t s_round;
static uint64_t s_capture_deadline_ms;
static bool s_place_data_ready;
static uint16_t s_current_place_id = CITY_PLACE_INVALID_ID;
static uint16_t s_current_species_id = CITY_SPECIES_CHARMANDER;
static city_creature_stats_t s_current_stats;

static city_wild_reward_guard_t s_wild_guard;
static uint64_t s_wild_clear_retry_ms;
static city_pocket_policy_t s_pocket;
static int s_battery_soc = -1;
static QueueHandle_t s_battery_queue;
static lv_obj_t *s_battery_label;
static lv_obj_t *s_wild_countdown;
static lv_obj_t *s_place_countdown;
static lv_obj_t *s_screen;
static lv_obj_t *s_field;
static lv_obj_t *s_ring;
static lv_obj_t *s_meter;
static lv_obj_t *s_marker;
static lv_obj_t *s_target;
static lv_obj_t *s_ball;
static lv_obj_t *s_ball_red;
static lv_obj_t *s_ball_band;
static lv_obj_t *s_ball_button;
static lv_obj_t *s_ball_button_inner;
static lv_obj_t *s_status;
static lv_timer_t *s_tick;

static void apply_settings_preview(void)
{
    const city_settings_t *settings = visible_settings();
    pokemon_audio_set_preferences(settings->volume, settings->muted);
    bsp_display_backlight(city_settings_backlight(settings, s_pocket.screen_off,
                                                  city_battery_low(s_battery_soc)));
}

static void settings_task(void *argument)
{
    (void)argument;
    settings_result_t result;
    for (;;) {
        if (xQueueReceive(s_settings_requests, &result.settings, portMAX_DELAY) != pdTRUE) continue;
        result.error = bsp_settings_save(&result.settings);
        xQueueOverwrite(s_settings_results, &result);
    }
}

static uint64_t now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static uint64_t total_capture_count(void)
{
    uint64_t total = 0;
    for (uint8_t i = 0; i < CITY_SPECIES_COUNT; ++i) total += s_bestiary.records[i].capture_count;
    return total;
}

static const char *state_name(ui_state_t state)
{
    static const char *const names[] = {
        "home", "scanning", "place_pending", "place_gray", "place_wild",
        "place_unstable", "place_error", "place_storage_error",
        "place_full", "encounter", "aim", "throwing", "catching",
        "captured", "escaped", "abandoned", "bestiary_list", "bestiary_detail",
        "storage_error", "low_battery", "passport", "evolution", "evolved",
        "pokemon_actions", "release_picker", "release_confirm", "released", "settings",
        "capture_ready", "bestiary_hint", "owned_detail",
    };
    return names[state];
}

static void style_plain(lv_obj_t *obj, uint32_t color)
{
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
}

static lv_obj_t *label_at(
    lv_obj_t *parent,
    const char *text,
    const lv_font_t *font,
    uint32_t color,
    int x,
    int y,
    int width)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, tr(text));
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label, width);
    unsigned lines = 1;
    for (const char *p = text; *p; ++p) if (*p == '\n') ++lines;
    lv_obj_set_height(label, lines * lv_font_get_line_height(font) + 1);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(label, x, y);
    return label;
}

static lv_obj_t *new_screen(const char *title, const char *action)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    style_plain(screen, COLOR_PAPER);

    lv_obj_t *header = lv_obj_create(screen);
    style_plain(header, 0xF7FBF8);
    lv_obj_set_size(header, SCREEN_WIDTH, 34);
    lv_obj_set_pos(header, 0, 0);
    label_at(header, "Pokédex", &city_font_14, 0x247052, 12, 9, 90);
    char battery_text[20];
    if (s_battery_soc < 0) snprintf(battery_text, sizeof(battery_text), "BAT --");
    else snprintf(battery_text, sizeof(battery_text), "%s%d%%",
                  city_battery_low(s_battery_soc) ? "LOW " : "", s_battery_soc);
    s_battery_label = label_at(header, battery_text, &city_font_14,
        city_battery_low(s_battery_soc) ? COLOR_CORAL : COLOR_MUTED, 126, 9, 102);
    lv_obj_set_style_text_align(s_battery_label, LV_TEXT_ALIGN_RIGHT, 0);

    label_at(
        screen, title, &city_font_20,
        COLOR_INK, 10, TITLE_Y, 220);

    lv_obj_t *footer = lv_obj_create(screen);
    style_plain(footer, 0xF7FBF8);
    lv_obj_set_size(footer, SCREEN_WIDTH, 36);
    lv_obj_set_pos(footer, 0, FOOTER_Y);
    /* Full-width, two-line help keeps the font readable on the small screen. */
    s_status = label_at(footer, action, &city_font_14, COLOR_INK,
                        10, 2, 220);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    return screen;
}

static lv_obj_t *create_field(lv_obj_t *screen)
{
    lv_obj_t *field = lv_obj_create(screen);
    style_plain(field, COLOR_SKY);
    lv_obj_set_style_radius(field, 12, 0);
    lv_obj_set_size(field, 220, CONTENT_HEIGHT);
    lv_obj_set_pos(field, 10, CONTENT_TOP);

    lv_obj_t *hill_left = lv_obj_create(field);
    style_plain(hill_left, 0x87BE91);
    lv_obj_set_style_radius(hill_left, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(hill_left, 130, 58);
    lv_obj_set_pos(hill_left, -28, 89);

    lv_obj_t *hill_right = lv_obj_create(field);
    style_plain(hill_right, 0x87BE91);
    lv_obj_set_style_radius(hill_right, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(hill_right, 144, 62);
    lv_obj_set_pos(hill_right, 115, 86);

    lv_obj_t *ground = lv_obj_create(field);
    style_plain(ground, COLOR_GRASS);
    lv_obj_set_size(ground, 220, 64);
    lv_obj_set_pos(ground, 0, 106);

    lv_obj_t *stage = lv_obj_create(field);
    style_plain(stage, 0x82BF89);
    lv_obj_set_style_radius(stage, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(stage, 176, 38);
    lv_obj_set_pos(stage, 22, 124);
    return field;
}

static lv_obj_t *create_species(
    lv_obj_t *parent,
    uint16_t species_id,
    bool small)
{
    lv_obj_t *image = lv_image_create(parent);
    static const struct { uint16_t id; const lv_image_dsc_t *large, *small; } images[] = {
#include "assets/species_images.inc"
    };
    const lv_image_dsc_t *source = NULL;
    for (unsigned i = 0; i < CITY_SPECIES_COUNT; ++i)
        if (images[i].id == species_id) source = small ? images[i].small : images[i].large;
    if (!source) return image;
    lv_image_set_src(image, source);
    lv_obj_set_pos(image, small ? 69 : 55, small ? 12 : 26);
    return image;
}

static uint8_t species_selection_index(uint16_t species_id)
{
    return city_species_index(species_id);
}

static void ball_geometry(int x, int y, int size)
{
    if (s_ball == NULL) {
        return;
    }
    lv_obj_set_size(s_ball, size, size);
    lv_obj_set_pos(s_ball, x, y);
    lv_obj_set_style_radius(s_ball, LV_RADIUS_CIRCLE, 0);

    lv_obj_set_size(s_ball_red, size - 4, (size - 4) / 2);
    lv_obj_set_pos(s_ball_red, 0, 0);
    lv_obj_set_size(lv_obj_get_child(s_ball_red, 0), size - 4, size - 4);
    lv_obj_set_size(s_ball_band, size, size < 30 ? 4 : 7);
    lv_obj_align(s_ball_band, LV_ALIGN_CENTER, 0, 0);

    int outer = size < 30 ? 9 : 15;
    int inner = size < 30 ? 5 : 9;
    lv_obj_set_size(s_ball_button, outer, outer);
    lv_obj_align(s_ball_button, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_size(s_ball_button_inner, inner, inner);
    lv_obj_align(s_ball_button_inner, LV_ALIGN_CENTER, 0, 0);
}

static lv_obj_t *create_ball(lv_obj_t *parent, int x, int y, int size)
{
    s_ball = lv_obj_create(parent);
    style_plain(s_ball, 0xF4F7F6);
    lv_obj_set_style_border_post(s_ball, true, 0);
    lv_obj_set_style_border_width(s_ball, 2, 0);
    lv_obj_set_style_border_color(s_ball, lv_color_hex(COLOR_INK), 0);
    lv_obj_remove_flag(s_ball, LV_OBJ_FLAG_SCROLLABLE);

    s_ball_red = lv_obj_create(s_ball);
    style_plain(s_ball_red, COLOR_CORAL);
    // Rectangular viewport clips a circle to its upper half without a mask layer.
    lv_obj_set_style_bg_opa(s_ball_red, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_ball_red, 0, 0);
    lv_obj_t *red_disc = lv_obj_create(s_ball_red);
    style_plain(red_disc, COLOR_CORAL);
    lv_obj_set_style_radius(red_disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_pos(red_disc, 0, 0);

    s_ball_band = lv_obj_create(s_ball);
    style_plain(s_ball_band, COLOR_INK);

    s_ball_button = lv_obj_create(s_ball);
    style_plain(s_ball_button, COLOR_INK);
    lv_obj_set_style_radius(s_ball_button, LV_RADIUS_CIRCLE, 0);

    s_ball_button_inner = lv_obj_create(s_ball_button);
    style_plain(s_ball_button_inner, 0xFFFFFF);
    lv_obj_set_style_radius(s_ball_button_inner, LV_RADIUS_CIRCLE, 0);

    ball_geometry(x, y, size);
    return s_ball;
}

static void build_home(void)
{
    s_screen = new_screen("Let's play!", LV_SYMBOL_UP " / " LV_SYMBOL_DOWN " Choose\nPress OK to open");
    const city_creature_record_t *buddy = city_bestiary_record_const(&s_bestiary, s_bestiary.buddy_species_id);
    if (buddy) {
        lv_obj_t *image = create_species(s_screen, buddy->species_id, true);
        lv_obj_set_pos(image, 12, 72);
        const city_species_definition_t *definition = city_species_definition(buddy->species_id);
        label_at(s_screen, species_name(definition), &city_font_14, COLOR_INK, 99, 77, 131);
        char text[96];
        snprintf(text, sizeof(text), tr("Health %u/%u"), buddy->current_hp, city_bestiary_max_hp(buddy));
        label_at(s_screen, text, &city_font_14, COLOR_GRASS_D, 94, 98, 146);
        snprintf(text, sizeof(text), tr("Friendship %u"), buddy->friendship);
        label_at(s_screen, text, &city_font_14, COLOR_GRASS_D, 94, 116, 146);
        label_at(s_screen, city_evolution_ready(&s_bestiary, buddy->species_id) ? "Ready to evolve!" : buddy->friendship >= 100 ? "Best buddies!" : "Go out together",
                 &city_font_14, COLOR_MUTED, 94, 135, 140);
    } else {
        const bool has_captures = city_bestiary_captured_count(&s_bestiary) > 0;
        label_at(s_screen, has_captures ? "Pick your buddy" : "Find a Pokemon!",
                 &city_font_20, COLOR_INK, 10, 85, 220);
        label_at(s_screen, has_captures ? "Pick one in your Pokédex" : "Choose Look around to start",
                 &city_font_14, COLOR_MUTED, 10, 116, 220);
    }
    const char *titles[] = {"Look around", "Pokédex", "My stamps", "Sound & screen"};
    for (unsigned i = 0; i < 4; ++i) {
        lv_obj_t *row = lv_obj_create(s_screen);
        style_plain(row, s_home_selection == i ? 0xE4F4E8 : 0xF7FBF8);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(s_home_selection == i ? COLOR_GREEN : 0xD5E0DD), 0);
        lv_obj_set_pos(row, 10, 158 + i * 26);
        lv_obj_set_size(row, 220, 24);
        label_at(row, titles[i], &city_font_14, COLOR_INK, 10, 3, 174);
        label_at(row, s_home_selection == i ? ">" : "", &city_font_14, COLOR_CORAL, 190, 3, 20);
    }
    label_at(s_screen, "Hold " LV_SYMBOL_UP " to turn screen off", &city_font_14, COLOR_MUTED, 10, 265, 220);
}

static void build_settings(void)
{
    const char *action = s_settings_saving ? "Saving..." : s_settings_editing
        ? (s_settings_selection == 3
           ? LV_SYMBOL_UP " Brighter / " LV_SYMBOL_DOWN " Dimmer\nPress OK when done"
           : LV_SYMBOL_UP " Louder / " LV_SYMBOL_DOWN " Softer\nPress OK when done") : LV_SYMBOL_UP " / " LV_SYMBOL_DOWN " Choose\nPress OK to pick";
    s_screen = new_screen("Settings", action);
    for (unsigned i = 0; i < 6; ++i) {
        lv_obj_t *row = lv_obj_create(s_screen);
        const bool selected = i == s_settings_selection;
        style_plain(row, selected ? 0xE4F4E8 : 0xF7FBF8);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(selected ? COLOR_GREEN : 0xD5E0DD), 0);
        lv_obj_set_pos(row, 10, 70 + i * 28);
        lv_obj_set_size(row, 220, 26);
        char text[96];
        if (i == 0) {
            snprintf(
                text, sizeof(text), tr("Language  %s"),
                tr(s_settings_draft.language == CITY_LANGUAGE_SIMPLIFIED_CHINESE
                       ? "Simplified Chinese"
                       : "English"));
        } else if (i == 1) {
            snprintf(text, sizeof(text), tr("Sound volume  %u%%"), s_settings_draft.volume);
        } else if (i == 2) {
            snprintf(text, sizeof(text), tr("Quiet mode  %s"), tr(s_settings_draft.muted ? "On" : "Off"));
        } else if (i == 3) {
            snprintf(text, sizeof(text), tr("Screen light  %u%%"), s_settings_draft.brightness);
        } else {
            snprintf(text, sizeof(text), "%s", tr(i == 4 ? "Save and go back" : "Undo and go back"));
        }
        label_at(row, text, &city_font_14, COLOR_INK, 8, 5, 181);
        label_at(row, selected ? (s_settings_editing ? "*" : ">") : "",
                 &city_font_14, COLOR_CORAL, 195, 5, 16);
    }
    const char *message = s_settings_error ? "Could not save. Try again." :
        s_settings_load_error ? "Please save these settings" :
        city_battery_low(s_battery_soc) ? "Low battery: light at 30%" :
        s_settings_draft.muted || s_settings_draft.volume == 0 ? "Sound is off" :
        s_settings_editing && s_settings_selection == 1 ? "Press OK to hear it" : "Save to keep your changes";
    label_at(s_screen, message, &city_font_14, COLOR_MUTED, 10, 242, 220);
    label_at(s_screen, "Hold OK to undo changes", &city_font_14, COLOR_MUTED, 10, 264, 220);
}

static void build_passport(void)
{
    city_passport_stamps_t stamps;
    const bool have_places = place_scan_coordinator_passport(&stamps);
    const city_passport_progress_t progress = city_passport_progress(
        have_places ? &stamps : NULL, s_bestiary_ready ? &s_bestiary : NULL);
    if (s_passport_page >= progress.pages) s_passport_page = 0;
    s_screen = new_screen("My stamps", LV_SYMBOL_UP " / " LV_SYMBOL_DOWN " Turn page\nPress OK to go home");
    char text[128];
    if (progress.places_ready)
        snprintf(text, sizeof(text), tr("Stamps %u/%u   Page %u/%u"), progress.places,
                 CITY_PLACE_MAX_COUNT, s_passport_page + 1, progress.pages);
    else snprintf(text, sizeof(text), "%s", tr("Could not open your stamps"));
    label_at(s_screen, text, &city_font_14, COLOR_INK, 10, 77, 220);
    if (progress.collection_ready)
        snprintf(text, sizeof(text), tr("Found %u/%u   Have %u/%u"), progress.discovered,
                 CITY_SPECIES_COUNT, progress.captured, CITY_SPECIES_COUNT);
    else snprintf(text, sizeof(text), "%s", tr("Could not open Pokédex"));
    label_at(s_screen, text, &city_font_14, COLOR_MUTED, 10, 101, 220);
    for (unsigned slot = 0; slot < CITY_PASSPORT_STAMPS_PER_PAGE; ++slot) {
        const unsigned index = s_passport_page * CITY_PASSPORT_STAMPS_PER_PAGE + slot;
        const bool earned = progress.places_ready && index < stamps.count;
        lv_obj_t *stamp = lv_obj_create(s_screen);
        style_plain(stamp, earned ? 0xE4F4E8 : 0xF7FBF8);
        lv_obj_set_style_radius(stamp, 16, 0);
        lv_obj_set_style_border_width(stamp, earned ? 2 : 1, 0);
        lv_obj_set_style_border_color(stamp, lv_color_hex(earned ? COLOR_GRASS_D : 0xD5E0DD), 0);
        lv_obj_set_size(stamp, 104, 32);
        lv_obj_set_pos(stamp, 10 + (slot % 2) * 116, 130 + (slot / 2) * 38);
        if (earned) snprintf(text, sizeof(text), tr("Place %02u"), stamps.place_ids[index]);
        else snprintf(text, sizeof(text), "--");
        label_at(stamp, text, &city_font_14,
                 earned ? COLOR_GRASS_D : COLOR_MUTED, 2, 7, 96);
    }
    switch (progress.goal) {
    case CITY_PASSPORT_FIRST_CAPTURE:
        snprintf(text, sizeof(text), "%s", tr("Next: catch a Pokemon")); break;
    case CITY_PASSPORT_NEW_PLACE:
        snprintf(text, sizeof(text), tr("Next: visit %u places"), progress.target); break;
    case CITY_PASSPORT_CATCH_SPECIES:
        snprintf(text, sizeof(text), tr("Next: %s %s"),
                 tr(progress.target_seen ? "catch" : "find"),
                 species_name(city_species_definition(progress.target))); break;
    case CITY_PASSPORT_EVOLVE_SPECIES:
        snprintf(text, sizeof(text), tr("Next: evolve %s"),
                 species_name(city_species_definition(progress.target))); break;
    case CITY_PASSPORT_COMPLETE:
        snprintf(text, sizeof(text), "%s", tr("You found them all!")); break;
    default:
        snprintf(text, sizeof(text), "%s", tr("Could not open your saves")); break;
    }
    label_at(s_screen, text, &city_font_14, COLOR_INK, 5, META_Y, 230);
    ESP_LOGI(TAG, "PASSPORT places=%u seen=%u caught=%u page=%u/%u goal=%u target=%u ready=%u",
             progress.places, progress.discovered, progress.captured, s_passport_page + 1,
             progress.pages, progress.goal, progress.target,
             progress.places_ready && progress.collection_ready);
}

static void build_scanning(void)
{
    s_screen = new_screen("Looking around...", "Please wait");
    s_field = create_field(s_screen);
    lv_obj_t *ring = lv_obj_create(s_field);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, 5, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(COLOR_CORAL), 0);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(ring, 82, 82);
    lv_obj_set_pos(ring, 69, 39);
    label_at(ring, "...", &city_font_20, 0x247052, 18, 22, 46);
}

static void build_place_status(
    const char *title,
    const char *action,
    const char *message)
{
    s_screen = new_screen(title, action);
    s_field = create_field(s_screen);
    label_at(
        s_field, "?", &city_font_20,
        COLOR_CORAL, 90, 48, 40);
    label_at(
        s_screen, message, &city_font_14,
        COLOR_MUTED, 10, META_Y, 220);
}

static void build_place_pending(void)
{
    s_screen = new_screen("Is this a new place?", "Please wait");
    s_field = create_field(s_screen);
    s_place_countdown = label_at(
        s_field, "Again in 20s", &city_font_20,
        COLOR_INK, 10, 61, 200);
    label_at(
        s_screen, "Checking this place again",
        &city_font_14, COLOR_MUTED, 10, META_Y, 220);
}

static void build_encounter(void)
{
    const city_species_definition_t *definition =
        city_species_definition(s_current_species_id);
    char title[96];
    char meta[96];
    snprintf(title, sizeof(title), tr("Found %s"), species_name(definition));
    snprintf(
        meta, sizeof(meta), tr("Type: %s"),
        ui_species_type(visible_language(), definition->type_label));
    if (s_new_place_stamp)
        snprintf(meta, sizeof(meta), tr("New stamp! Place %02u"), s_current_place_id);
    s_screen = new_screen(title, LV_SYMBOL_UP " / " LV_SYMBOL_DOWN " Choose\nPress OK to pick");
    s_field = create_field(s_screen);
    create_species(s_field, s_current_species_id, false);
    const char *record_status = s_encounter_previous_state == CITY_DISCOVERY_CAPTURED
        ? "You have one!" : s_encounter_previous_state == CITY_DISCOVERY_SEEN
        ? "Not caught yet" : "You found someone new!";
    lv_obj_t *badge = label_at(s_field, record_status, &city_font_14,
        s_encounter_previous_state == CITY_DISCOVERY_CAPTURED ? COLOR_GRASS_D : COLOR_INK,
        5, 5, 210);
    lv_obj_set_style_bg_color(badge, lv_color_hex(0xF7FBF8), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);

    const char *choices[] = {"Catch", "Leave"};
    for (uint8_t i = 0U; i < 2U; ++i) {
        lv_obj_t *choice = lv_obj_create(s_field);
        const bool selected = s_encounter_selection == i;
        style_plain(choice, selected ? 0xFFF3CF : 0xF7FBF8);
        lv_obj_set_style_radius(choice, 6, 0);
        lv_obj_set_style_border_width(choice, selected ? 2 : 1, 0);
        lv_obj_set_style_border_color(
            choice, lv_color_hex(selected ? COLOR_CORAL : 0xD5E0DD), 0);
        lv_obj_set_size(choice, 78, 28);
        lv_obj_set_pos(choice, 137, 88 + (int)i * 34);
        label_at(
            choice, choices[i], &city_font_14,
            COLOR_INK, 4, 6, 70);
    }
    label_at(
        s_screen, meta, &city_font_14,
        COLOR_MUTED, 10, META_Y, 220);
}

static void position_capture_target(void)
{
    if (s_meter == NULL || s_target == NULL) {
        return;
    }
    uint32_t start = s_round.target_center_ms - s_round.target_half_width_ms;
    uint32_t width = s_round.target_half_width_ms * 2U;
    int x = (int)((start * 176U) / s_round.duration_ms);
    int w = (int)((width * 176U) / s_round.duration_ms);
    lv_obj_set_size(s_target, w, 6);
    lv_obj_set_pos(s_target, x, 0);
}

static void build_capture_ready(void)
{
    s_screen = new_screen("Your first catch", "Press OK to start\n" LV_SYMBOL_UP " Go back");
    lv_obj_t *image = create_species(s_screen, s_current_species_id, true);
    lv_obj_set_pos(image, 79, 74);
    label_at(s_screen, "When you see NOW,",
             &city_font_14, COLOR_INK, 10, 164, 220);
    label_at(s_screen, "press OK to throw.",
             &city_font_14, COLOR_INK, 10, 187, 220);
    label_at(s_screen, "3 tries in 15 seconds",
             &city_font_14, COLOR_MUTED, 10, 218, 220);
    label_at(s_screen, "Hold OK to stop playing",
             &city_font_14, COLOR_MUTED, 5, 251, 230);
}

static void build_aim(void)
{
    const city_species_definition_t *definition =
        city_species_definition(s_current_species_id);
    char title[96];
    snprintf(title, sizeof(title), tr("Catch %s"), species_name(definition));
    s_screen = new_screen(title, "Press OK to throw");
    s_field = create_field(s_screen);
    create_species(s_field, s_current_species_id, true);

    s_ring = lv_obj_create(s_field);
    lv_obj_set_style_bg_opa(s_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ring, 4, 0);
    lv_obj_set_style_border_color(s_ring, lv_color_hex(COLOR_CORAL), 0);
    lv_obj_set_style_radius(s_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(s_ring, 92, 92);
    lv_obj_set_pos(s_ring, 64, 7);

    s_meter = lv_obj_create(s_field);
    style_plain(s_meter, 0xC9D4D2);
    lv_obj_set_style_radius(s_meter, 4, 0);
    lv_obj_set_size(s_meter, 180, 6);
    lv_obj_set_pos(s_meter, 20, 154);

    s_target = lv_obj_create(s_meter);
    style_plain(s_target, COLOR_GREEN);
    lv_obj_set_style_radius(s_target, 4, 0);
    position_capture_target();

    s_marker = lv_obj_create(s_meter);
    style_plain(s_marker, COLOR_CORAL);
    lv_obj_set_style_radius(s_marker, 2, 0);
    lv_obj_set_size(s_marker, 4, 12);
    lv_obj_set_pos(s_marker, 0, -3);

    create_ball(s_field, 86, 99, 48);
    s_aim_cue = label_at(s_field, "WAIT", &city_font_14,
                        COLOR_INK, 165, 30, 50);
    char status[96];
    snprintf(status, sizeof(status), tr("%u tries left. Wait for NOW"), s_attempts);
    s_aim_status = label_at(s_screen, status, &city_font_14,
                           COLOR_INK, 5, 247, 230);
    label_at(s_screen, "Hold OK to stop", &city_font_14,
             COLOR_MUTED, 10, 266, 220);
}

static void build_throwing(void)
{
    s_screen = new_screen("Here it goes!", "Here it goes!");
    s_field = create_field(s_screen);
    create_species(s_field, s_current_species_id, true);
    create_ball(s_field, 80, 106, 60);
    label_at(s_screen, "Watch your ball!", &city_font_14,
             COLOR_MUTED, 10, META_Y, 220);
}

static void build_catching(void)
{
    s_screen = new_screen("Will it stay?", "Please wait");
    s_field = create_field(s_screen);
    create_ball(s_field, 88, 72, 44);
    label_at(s_screen, "One... Two... Three...", &city_font_14,
             COLOR_MUTED, 10, META_Y, 220);
}

static void build_captured(void)
{
    s_screen = new_screen("You caught it!", "Press OK for Pokédex");
    s_field = create_field(s_screen);
    lv_obj_set_height(s_field, 118);
    create_species(s_field, s_current_species_id, true);
    label_at(s_field, species_name(city_species_definition(s_current_species_id)),
             &city_font_14, COLOR_INK, 5, 94, 210);
    char text[96];
    if (s_new_place_stamp) snprintf(text, sizeof(text), tr("New stamp! Place %02u"), s_current_place_id);
    else snprintf(text, sizeof(text), "%s", tr("Saved in your Pokédex"));
    label_at(s_screen, text, &city_font_14, COLOR_GRASS_D, 5, 203, 230);
    if (s_bestiary.buddy_species_id)
        snprintf(text, sizeof(text), tr("Friendship +%u"), s_capture_bond_gain);
    else snprintf(text, sizeof(text), "%s", tr("Pick your new buddy!"));
    label_at(s_screen, text, &city_font_14, COLOR_INK, 5, 226, 230);
    city_passport_stamps_t stamps;
    const bool ready = place_scan_coordinator_passport(&stamps);
    const city_passport_progress_t progress = city_passport_progress(ready ? &stamps : NULL, &s_bestiary);
    if (!s_bestiary.buddy_species_id) snprintf(text, sizeof(text), "%s", tr("Next: pick a buddy"));
    else if (progress.goal == CITY_PASSPORT_NEW_PLACE)
        snprintf(text, sizeof(text), tr("Next: visit %u places"), progress.target);
    else snprintf(text, sizeof(text), "%s", tr("Next: look at My stamps"));
    label_at(s_screen, text, &city_font_14, COLOR_MUTED, 5, 255, 230);
}

static void build_escaped(void)
{
    s_screen = new_screen("It got away!", "Press OK to go home");
    s_field = create_field(s_screen);
    create_species(s_field, s_current_species_id, false);
    label_at(s_screen, s_capture_feedback, &city_font_14,
             COLOR_MUTED, 10, META_Y, 220);
}

static void build_abandoned(void)
{
    const city_species_definition_t *definition =
        city_species_definition(s_current_species_id);
    char message[96];
    snprintf(message, sizeof(message), tr("See you, %s!"), species_name(definition));
    s_screen = new_screen("You left it alone", "Press OK to go home");
    s_field = create_field(s_screen);
    create_species(s_field, s_current_species_id, false);
    label_at(s_screen, message, &city_font_14,
             COLOR_MUTED, 10, META_Y, 220);
}

static void build_bestiary_list(void)
{
    s_screen = new_screen("Pokédex", "Press OK to open\nHold OK to go home");
    const uint8_t start = (s_bestiary_selection / 4U) * 4U;
    for (uint8_t row_index = 0; row_index < 4; ++row_index) {
        const uint8_t i = start + row_index;
        if (i > CITY_SPECIES_COUNT) break;
        const bool back = i == CITY_SPECIES_COUNT;
        const bool selected = s_bestiary_selection == i;
        lv_obj_t *row = lv_obj_create(s_screen);
        style_plain(row, selected ? 0xE4F4E8 : 0xF7FBF8);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_border_width(row, selected ? 2 : 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(selected ? COLOR_GREEN : 0xD5E0DD), 0);
        lv_obj_set_size(row, 220, 40);
        lv_obj_set_pos(row, 10, 74 + row_index * 42);
        if (back) {
            label_at(row, "Go home", &city_font_14, COLOR_INK, 8, 10, 196);
            continue;
        }
        const uint16_t id = city_species_id_at(i);
        const city_creature_record_t *record = city_bestiary_record_const(&s_bestiary, id);
        const city_species_definition_t *definition = city_species_definition(id);
        char name[96];
        snprintf(name, sizeof(name), "%03u %s", id,
                 record->state == CITY_DISCOVERY_UNKNOWN ? "???" : species_name(definition));
        lv_obj_t *label = label_at(row, name, &city_font_14, COLOR_INK, 7, 3, 132);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
        const char *status = record->evolution_obtained ? "Evolved" : record->state == CITY_DISCOVERY_CAPTURED ? "Caught" :
                             record->state == CITY_DISCOVERY_SEEN ? "Found" : "---";
        label_at(row, status, &city_font_14,
                 record->state == CITY_DISCOVERY_CAPTURED ? COLOR_GRASS_D : COLOR_MUTED, 145, 3, 68);
        if (record->state == CITY_DISCOVERY_CAPTURED) {
            char count[96];
            if (record->evolution_obtained) snprintf(count, sizeof(count), "%s", tr("Grew by evolving"));
            else snprintf(count, sizeof(count), tr("Caught %lu"), (unsigned long)record->capture_count);
            label = label_at(row, count, &city_font_14, COLOR_MUTED, 7, 21, 198);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
        }
    }
    char progress[96];
    snprintf(progress, sizeof(progress), tr("Found %u/%u   Page %u/%u"),
             city_bestiary_discovered_count(&s_bestiary), CITY_SPECIES_COUNT,
             s_bestiary_selection / 4U + 1U, (CITY_SPECIES_COUNT + 4U) / 4U);
    label_at(s_screen, progress, &city_font_14, COLOR_MUTED, 5, META_Y, 230);
}

static void build_bestiary_hint(void)
{
    s_screen = new_screen("Not found yet", "Press OK to go back");
    label_at(s_screen, "Look around for Pokemon",
             &city_font_14, COLOR_INK, 5, 115, 230);
    label_at(s_screen, "Some Pokemon must evolve",
             &city_font_14, COLOR_MUTED, 5, 149, 230);
    label_at(s_screen, "Look at My stamps for help",
             &city_font_14, COLOR_MUTED, 5, 183, 230);
}

static void build_bestiary_detail(void)
{
    if (s_bestiary_selection >= CITY_SPECIES_COUNT) {
        return;
    }
    const uint16_t species_id = city_species_id_at(s_bestiary_selection);
    const city_creature_record_t *record =
        city_bestiary_record_const(&s_bestiary, species_id);
    const city_species_definition_t *definition =
        city_species_definition(species_id);
    char title[96];
    snprintf(
        title, sizeof(title), "No.%03u %s",
        species_id, species_name(definition));
    s_screen = new_screen(title, record->state == CITY_DISCOVERY_CAPTURED ? LV_SYMBOL_UP " Make buddy  " LV_SYMBOL_DOWN " Back\nPress OK for choices" : "Press OK to go back");
    s_field = create_field(s_screen);
    lv_obj_set_height(s_field, 82);

    lv_obj_t *image = create_species(s_field, species_id, true);
    lv_obj_set_pos(image, 9, 0);
    if (record->state == CITY_DISCOVERY_SEEN) {
        lv_obj_set_style_image_recolor(
            image, lv_color_hex(COLOR_MUTED), 0);
        lv_obj_set_style_image_recolor_opa(image, LV_OPA_60, 0);
    }

    lv_obj_t *tag = lv_obj_create(s_field);
    style_plain(
        tag, record->state == CITY_DISCOVERY_CAPTURED
                 ? COLOR_CORAL
                 : COLOR_GREEN);
    lv_obj_set_style_radius(tag, 8, 0);
    lv_obj_set_size(tag, 78, 24);
    lv_obj_set_pos(tag, 132, 5);
    label_at(
        tag, record->evolution_obtained ? "Evolved" : record->state == CITY_DISCOVERY_CAPTURED ? "Caught" : "Found",
        &city_font_14, 0xFFFFFF, 3, 5, 72);

    char count[96];
    snprintf(
        count, sizeof(count), tr("Have %lu"),
        (unsigned long)record->capture_count);
    lv_obj_t *count_label = label_at(
        s_field, count, &city_font_14, COLOR_INK, 118, 34, 96);
    lv_obj_set_style_text_align(count_label, LV_TEXT_ALIGN_LEFT, 0);

    char place[96];
    if (record->last_place_id == UINT16_MAX) {
        snprintf(place, sizeof(place), "%s", tr("Place --"));
    } else if (record->last_place_id == CITY_WILD_PLACE_ID) {
        snprintf(place, sizeof(place), "%s", tr("Wild"));
    } else {
        snprintf(place, sizeof(place), tr("Place %02u"), record->last_place_id);
    }
    lv_obj_t *place_label = label_at(
        s_field, place, &city_font_14, COLOR_MUTED, 118, 58, 96);
    lv_obj_set_style_text_align(place_label, LV_TEXT_ALIGN_LEFT, 0);

    char type_text[96];
    snprintf(type_text, sizeof(type_text), tr("Type: %s"),
             ui_species_type(visible_language(), definition->type_label));
    label_at(s_screen, type_text, &city_font_14,
             COLOR_GRASS_D, 10, 158, 220);

    /* Authored facts fit three lines; keep them available after an escape. */
    lv_obj_t *description = lv_label_create(s_screen);
    lv_obj_set_style_text_font(description, &city_font_14, 0);
    lv_obj_set_style_text_color(description, lv_color_hex(COLOR_INK), 0);
    lv_obj_set_style_text_align(description, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_line_space(description, 0, 0);
    lv_obj_set_size(description, 220, 48);
    lv_label_set_long_mode(description, LV_LABEL_LONG_WRAP);
    lv_label_set_text(
        description,
        ui_species_description(visible_language(), definition->description));
    lv_obj_set_pos(description, 10, 178);

    if (record->state == CITY_DISCOVERY_CAPTURED) {
        char latest[96];
        char best[96];
        snprintf(
            latest, sizeof(latest), tr("Strongest: Health %u/%u"),
            record->current_hp, city_bestiary_max_hp(record));
        snprintf(
            best, sizeof(best), tr("Attack %u   Defense %u"),
            record->best_stats.attack,
            record->best_stats.defense);
        lv_obj_t *latest_label = label_at(
            s_screen, latest, &city_font_14,
            COLOR_MUTED, 10, 229, 220);
        lv_obj_set_style_text_align(latest_label, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_t *best_label = label_at(
            s_screen, best, &city_font_14,
            COLOR_MUTED, 10, 247, 220);
        lv_obj_set_style_text_align(best_label, LV_TEXT_ALIGN_LEFT, 0);
    }
    char buddy_text[96];
    if (record->state == CITY_DISCOVERY_CAPTURED)
        snprintf(buddy_text, sizeof(buddy_text), tr("Friendship %u/100%s"), record->friendship,
                 s_bestiary.buddy_species_id == species_id ? tr("  Buddy") : "");
    else snprintf(buddy_text, sizeof(buddy_text), "%s", tr("Try to catch this Pokemon"));
    label_at(s_screen, buddy_text, &city_font_14, COLOR_MUTED, 10, 265, 220);
}

static void build_pokemon_actions(void)
{
    const uint16_t species_id = city_species_id_at(s_bestiary_selection);
    const city_creature_record_t *record = city_bestiary_record_const(&s_bestiary, species_id);
    const bool has_evolution = city_evolution_target(species_id) != 0U;
    const bool full = record->current_hp >= city_bestiary_max_hp(record);
    const uint8_t count = has_evolution ? 5U : 4U;
    if (full && s_action_selection == 0U) s_action_selection = 1U;
    s_screen = new_screen("Buddy choices", LV_SYMBOL_UP " / " LV_SYMBOL_DOWN " Choose\nPress OK to pick");
    label_at(s_screen, "Your strongest one", &city_font_14, COLOR_MUTED, 10, 76, 220);
    char health[96];
    snprintf(health, sizeof(health), tr("Health %u/%u"), record->current_hp, city_bestiary_max_hp(record));
    label_at(s_screen, health, &city_font_14, COLOR_INK, 10, 96, 220);
    label_at(s_screen, full ? "Health is full" : "Needs healing", &city_font_14,
             full ? COLOR_GRASS_D : COLOR_CORAL, 10, 116, 220);
    for (uint8_t i = 0; i < count; ++i) {
        const char *item = i == 0U ? "Heal" : i == 1U ? "My Pokemon" :
            has_evolution && i == 2U ? "Evolve" : i == count - 2U ? "Let go" : "Go back";
        char text[96];
        snprintf(text, sizeof(text), "%s%s", s_action_selection == i ? "> " : "", tr(item));
        label_at(s_screen, text, &city_font_14,
                 full && i == 0U ? COLOR_MUTED : s_action_selection == i ? COLOR_GRASS_D : COLOR_INK,
                 12, 145 + i * 26, 216);
    }
}

static void build_owned_detail(void)
{
    const uint16_t species_id = city_species_id_at(s_bestiary_selection);
    const uint16_t count = city_bestiary_owned_count(&s_bestiary, species_id);
    if (s_owned_selection >= count) s_owned_selection = 0U;
    const city_owned_pokemon_t *owned = city_bestiary_owned_at(&s_bestiary, species_id, s_owned_selection);
    char text[96];
    snprintf(text, sizeof(text), tr("My %s"), species_name(city_species_definition(species_id)));
    s_screen = new_screen(text, LV_SYMBOL_UP " / " LV_SYMBOL_DOWN " Pick Pokemon\nPress OK to go back");
    lv_obj_t *image = create_species(s_screen, species_id, true);
    lv_obj_set_pos(image, 79, 78);
    if (!owned) return;
    snprintf(text, sizeof(text), tr("Pokemon %u of %u"), s_owned_selection + 1U, count);
    label_at(s_screen, text, &city_font_14, COLOR_INK, 10, 166, 220);
    snprintf(text, sizeof(text), tr("Health %u/%u"), owned->current_hp, owned->stats.hp);
    label_at(s_screen, text, &city_font_14, COLOR_GRASS_D, 10, 188, 220);
    snprintf(text, sizeof(text), tr("Attack %u   Defense %u"), owned->stats.attack, owned->stats.defense);
    label_at(s_screen, text, &city_font_14, COLOR_INK, 10, 210, 220);
    if (owned->migrated) snprintf(text, sizeof(text), "%s", tr("From your old save"));
    else if (owned->evolved) snprintf(text, sizeof(text), "%s", tr("Grew through evolution"));
    else if (owned->place_id == CITY_WILD_PLACE_ID) snprintf(text, sizeof(text), "%s", tr("Caught in the wild"));
    else snprintf(text, sizeof(text), tr("Caught at Place %02u"), owned->place_id);
    label_at(s_screen, text, &city_font_14, COLOR_MUTED, 10, 236, 220);
    label_at(s_screen, owned->migrated ? "Stats may be shared" : "These are this one's stats",
             &city_font_14, COLOR_MUTED, 10, 256, 220);
}

static void build_release_confirm(void)
{
    const uint16_t species_id = city_species_id_at(s_bestiary_selection);
    const uint16_t owned = city_bestiary_owned_count(&s_bestiary, species_id);
    const city_owned_pokemon_t *selected = city_bestiary_owned_at(&s_bestiary, species_id, s_release_copy_selection);
    s_screen = new_screen("Let this one go?", LV_SYMBOL_UP " / " LV_SYMBOL_DOWN " Choose\nPress OK to pick");
    lv_obj_t *image = create_species(s_screen, species_id, true);
    lv_obj_set_pos(image, 79, 66);
    label_at(s_screen, species_name(city_species_definition(species_id)), &city_font_20, COLOR_INK, 10, 151, 220);
    char message[96];
    if (owned > 1U) snprintf(message, sizeof(message), tr("This is #%u. %u will stay."), s_release_copy_selection + 1U, owned - 1U);
    else snprintf(message, sizeof(message), "%s", tr("This is your last one."));
    label_at(s_screen, message, &city_font_14, COLOR_CORAL, 10, 180, 220);
    if (owned <= 1U) {
        label_at(s_screen, "Its friendship starts over.", &city_font_14, COLOR_INK, 10, 199, 220);
        label_at(s_screen, "Its visits start over too.", &city_font_14, COLOR_INK, 10, 216, 220);
    } else if (selected) {
        snprintf(message, sizeof(message), tr("Health %u/%u"), selected->current_hp, selected->stats.hp);
        label_at(s_screen, message, &city_font_14, COLOR_MUTED, 10, 199, 220);
        snprintf(message, sizeof(message), tr("Attack %u   Defense %u"), selected->stats.attack, selected->stats.defense);
        label_at(s_screen, message, &city_font_14, COLOR_MUTED, 10, 216, 220);
    }
    label_at(s_screen, s_release_selection == 0 ? "> Let go <" : "Let go",
             &city_font_14, COLOR_CORAL, 10, 239, 220);
    label_at(s_screen, s_release_selection == 1 ? "> Keep it <" : "Keep it",
             &city_font_14, COLOR_INK, 10, 261, 220);
}

static void build_release_picker(void)
{
    const uint16_t species_id = city_species_id_at(s_bestiary_selection);
    const uint16_t count = city_bestiary_owned_count(&s_bestiary, species_id);
    const city_owned_pokemon_t *owned = city_bestiary_owned_at(&s_bestiary, species_id, s_release_copy_selection);
    s_screen = new_screen("Pick a Pokemon", LV_SYMBOL_UP " / " LV_SYMBOL_DOWN " Choose\nPress OK to pick");
    lv_obj_t *image = create_species(s_screen, species_id, true);
    lv_obj_set_pos(image, 79, 62);
    label_at(s_screen, owned ? species_name(city_species_definition(species_id)) : "Go back",
             &city_font_20, COLOR_INK, 5, 147, 230);
    char text[96];
    if (owned) {
        snprintf(text, sizeof(text), tr("Pokemon %u of %u"), s_release_copy_selection + 1U, count);
        label_at(s_screen, text, &city_font_14, COLOR_INK, 10, 173, 220);
        snprintf(text, sizeof(text), tr("Health %u/%u"), owned->current_hp, owned->stats.hp);
        label_at(s_screen, text, &city_font_14, COLOR_GRASS_D, 10, 192, 220);
        snprintf(text, sizeof(text), tr("Attack %u   Defense %u"), owned->stats.attack, owned->stats.defense);
        label_at(s_screen, text, &city_font_14, COLOR_MUTED, 10, 211, 220);
        if (owned->evolved) snprintf(text, sizeof(text), "%s", tr("Grew by evolving"));
        else if (owned->migrated) snprintf(text, sizeof(text), "%s", tr("Old save: shared stats"));
        else if (owned->place_id == CITY_WILD_PLACE_ID) snprintf(text, sizeof(text), "%s", tr("Caught in the wild"));
        else snprintf(text, sizeof(text), tr("Caught at Place %02u"), owned->place_id);
        label_at(s_screen, text, &city_font_14, COLOR_MUTED, 10, 230, 220);
    } else label_at(s_screen, "Keep all your Pokemon", &city_font_14, COLOR_INK, 10, 187, 220);
    label_at(s_screen, owned ? "Hold OK to go back" : "> Go back <",
             &city_font_14, COLOR_INK, 5, 262, 230);
}

static void build_released(void)
{
    const uint16_t species_id = city_species_id_at(s_bestiary_selection);
    const city_creature_record_t *record = city_bestiary_record_const(&s_bestiary, species_id);
    const uint32_t remaining = record->capture_count + (record->evolution_obtained ? 1U : 0U);
    s_screen = new_screen("You let it go", "Press OK for Pokédex");
    lv_obj_t *image = create_species(s_screen, species_id, true);
    lv_obj_set_pos(image, 79, 84);
    char text[96];
    if (remaining > 0U) snprintf(text, sizeof(text), tr("%s left. You have %lu."),
        species_name(city_species_definition(species_id)), (unsigned long)remaining);
    else snprintf(text, sizeof(text), tr("%s is free now."),
                  species_name(city_species_definition(species_id)));
    label_at(s_screen, text, &city_font_14, COLOR_GRASS_D, 5, META_Y, 230);
}

static void build_evolution(void)
{
    const city_creature_record_t *source = city_bestiary_record_const(&s_bestiary, s_evolution_source_id);
    const uint16_t target_id = city_evolution_target(s_evolution_source_id);
    const city_creature_record_t *target = city_bestiary_record_const(&s_bestiary, target_id);
    const bool ready = city_evolution_ready(&s_bestiary, s_evolution_source_id);
    s_screen = new_screen("Time to evolve?", LV_SYMBOL_UP " / " LV_SYMBOL_DOWN " Choose\nPress OK to pick");
    lv_obj_t *image = create_species(s_screen, s_evolution_source_id, true);
    lv_obj_set_pos(image, 79, 67);
    char text[96];
    snprintf(text, sizeof(text), tr("Grow into %s"), species_name(city_species_definition(target_id)));
    label_at(s_screen, text, &city_font_14, COLOR_INK, 10, 154, 220);
    snprintf(text, sizeof(text), tr("Friendship %u/30"), source->friendship < 30 ? source->friendship : 30);
    label_at(s_screen, text, &city_font_14, COLOR_GRASS_D, 10, 177, 220);
    const unsigned places = city_buddy_place_count(source);
    snprintf(text, sizeof(text), tr("Places visited %u/3"), places < 3 ? places : 3);
    label_at(s_screen, text, &city_font_14, COLOR_GRASS_D, 10, 195, 220);
    const char *hint = target->evolution_obtained ? "Already evolved" :
        s_bestiary.buddy_species_id != s_evolution_source_id ? "Pick this buddy first" : ready ? "Your buddy is ready!" : "Catch Pokemon together";
    label_at(s_screen, hint, &city_font_14, COLOR_MUTED, 10, 216, 220);
    label_at(s_screen, ready ? (s_evolution_selection == 0 ? "> Evolve <" : "Evolve") : "Not ready yet",
             &city_font_14, ready ? COLOR_GRASS_D : COLOR_MUTED, 10, 239, 220);
    label_at(s_screen, s_evolution_selection == 1 ? "> Later <" : "Later",
             &city_font_14, COLOR_INK, 10, 261, 220);
}
static void evolution_reveal_y(void *image, int32_t y)
{
    lv_obj_set_y(image, y);
}
static void build_evolved(void)
{
    const uint16_t target = city_evolution_target(s_evolution_source_id);
    s_screen = new_screen("Your buddy evolved!", "Press OK to go home");
    s_field = create_field(s_screen);
    lv_obj_t *image = create_species(s_field, target, false);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, image);
    lv_anim_set_values(&animation, 42, 26);
    lv_anim_set_duration(&animation, 600);
    lv_anim_set_exec_cb(&animation, evolution_reveal_y);
    lv_anim_start(&animation);
    label_at(s_field, species_name(city_species_definition(target)), &city_font_20, COLOR_INK, 10, 141, 200);
    label_at(s_screen, "Saved! Meet your new buddy.", &city_font_14, COLOR_GRASS_D, 10, META_Y, 220);
}

static void build_storage_error(void)
{
    s_screen = new_screen("Could not save", "Press OK to try again\n" LV_SYMBOL_UP " Go home");
    label_at(s_screen, "Go home without saving?", &city_font_14, COLOR_MUTED, 5, 267, 230);
    s_field = create_field(s_screen);
    if (s_pending_write == WRITE_EVOLUTION) {
        label_at(s_screen, "Could not save this change.", &city_font_14, COLOR_CORAL, 10, 246, 220);
    } else if (s_pending_write == WRITE_BUDDY) {
        label_at(s_screen, "Your buddy did not change.", &city_font_14, COLOR_CORAL, 10, 246, 220);
    } else if (s_pending_write == WRITE_RECOVER) {
        label_at(s_screen, "Could not save the healing.", &city_font_14, COLOR_CORAL, 10, 246, 220);
    } else if (s_pending_write == WRITE_RELEASE) {
        label_at(s_screen, "Your Pokemon is still here.", &city_font_14, COLOR_CORAL, 10, 246, 220);
    } else if (s_pending_write == WRITE_DISCOVERY || s_pending_write == WRITE_WILD) {
        lv_obj_t *beacon = lv_obj_create(s_field);
        style_plain(beacon, 0xF7FBF8);
        lv_obj_set_style_radius(beacon, 8, 0);
        lv_obj_set_size(beacon, 68, 76);
        lv_obj_set_pos(beacon, 76, 42);
        label_at(beacon, "?", &city_font_20,
                 COLOR_CORAL, 14, 23, 40);
        label_at(s_screen, "Could not save this one.",
                 &city_font_14, COLOR_CORAL, 10, 246, 220);
    } else {
        create_ball(s_field, 88, 69, 44);
        label_at(s_screen, "Could not save your catch.",
                 &city_font_14, COLOR_CORAL, 10, 246, 220);
    }
}

static void build_state(void)
{
    s_field = NULL;
    s_ring = NULL;
    s_meter = NULL;
    s_marker = NULL;
    s_target = NULL;
    s_ball = NULL;
    s_ball_red = NULL;
    s_ball_band = NULL;
    s_ball_button = NULL;
    s_ball_button_inner = NULL;
    s_status = NULL;
    s_wild_countdown = NULL;
    s_place_countdown = NULL;
    s_aim_cue = NULL;
    s_aim_status = NULL;

    switch (s_state) {
    case UI_EVOLUTION:
        build_evolution(); break;
    case UI_EVOLVED:
        build_evolved(); break;
    case UI_POKEMON_ACTIONS:
        build_pokemon_actions(); break;
    case UI_OWNED_DETAIL:
        build_owned_detail(); break;
    case UI_RELEASE_PICKER:
        build_release_picker(); break;
    case UI_RELEASE_CONFIRM:
        build_release_confirm(); break;
    case UI_RELEASED:
        build_released(); break;
    case UI_PASSPORT:
        build_passport();
        break;
    case UI_SETTINGS:
        build_settings();
        break;
    case UI_HOME:
        build_home();
        break;
    case UI_SCANNING:
        build_scanning();
        break;
    case UI_PLACE_PENDING:
        build_place_pending();
        break;
    case UI_PLACE_GRAY:
        build_place_status(
            "Let's try again", "Press OK to go home", "Move a bit, then try again");
        break;
    case UI_PLACE_WILD:
        s_screen = new_screen("In the wild", "Press OK to look around\n" LV_SYMBOL_UP " Go home");
        s_field = create_field(s_screen);
        s_wild_countdown = label_at(s_field, "", &city_font_20,
                                    COLOR_INK, 5, 25, 210);
        label_at(s_field, "One try every 30 minutes", &city_font_14,
                 COLOR_INK, 5, 70, 210);
        label_at(s_field, "Leaving counts as your try", &city_font_14,
                 COLOR_INK, 5, 100, 210);
        label_at(s_screen, "Restarting may add 30 min", &city_font_14,
                 COLOR_MUTED, 5, META_Y, 230);
        break;
    case UI_LOW_BATTERY:
        build_place_status("Time to charge", "Press OK to go home", "Please charge before playing");
        break;
    case UI_PLACE_UNSTABLE:
        build_place_status(
            "Wait here a moment", "Press OK to go home", "Then try Look around again");
        break;
    case UI_PLACE_ERROR:
        build_place_status(
            "Try looking again", "Press OK to try again\n" LV_SYMBOL_UP " Go home", "Could not look around.");
        break;
    case UI_PLACE_STORAGE_ERROR:
        build_place_status(
            "Stamp not saved", "Press OK to try again\n" LV_SYMBOL_UP " Go home", "This stamp was not saved.");
        break;
    case UI_PLACE_FULL:
        build_place_status(
            "Stamp book is full", "Press OK to go home", "Your old stamps are safe.");
        break;
    case UI_ENCOUNTER:
        build_encounter();
        break;
    case UI_CAPTURE_READY:
        build_capture_ready();
        break;
    case UI_BESTIARY_HINT:
        build_bestiary_hint();
        break;
    case UI_AIM:
        build_aim();
        break;
    case UI_THROWING:
        build_throwing();
        break;
    case UI_CATCHING:
        build_catching();
        break;
    case UI_CAPTURED:
        build_captured();
        break;
    case UI_ESCAPED:
        build_escaped();
        break;
    case UI_ABANDONED:
        build_abandoned();
        break;
    case UI_BESTIARY_LIST:
        build_bestiary_list();
        break;
    case UI_BESTIARY_DETAIL:
        build_bestiary_detail();
        break;
    case UI_STORAGE_ERROR:
        build_storage_error();
        break;
    }

    lv_screen_load(s_screen);
    ESP_LOGI(
        TAG,
        "STATE %s attempts=%u capture_count=%lu",
        state_name(s_state),
        s_attempts,
        (unsigned long)total_capture_count());
}

static void set_state(ui_state_t state)
{
    lv_obj_t *old = s_screen;
    const ui_state_t previous = s_state;
    s_state = state;
    s_state_started_ms = now_ms();
    s_pocket.last_activity_ms = s_state_started_ms;
    build_state();
    if (old != NULL) {
        lv_obj_delete(old);
    }
    if (state == UI_ENCOUNTER && previous != UI_ENCOUNTER) {
        pokemon_audio_play(s_current_species_id);
    } else if (state == UI_BESTIARY_DETAIL && previous == UI_BESTIARY_LIST) {
        pokemon_audio_play(city_species_id_at(s_bestiary_selection));
    } else if (state != previous &&
               (previous == UI_ENCOUNTER || previous == UI_BESTIARY_DETAIL)) {
        pokemon_audio_stop();
    }
}

static void close_settings(void)
{
    pokemon_audio_stop();
    set_state(UI_HOME);
    apply_settings_preview();
}

static void handle_settings_button(bsp_btn_t button, bool cancel)
{
    if (s_settings_saving) return;
    if (cancel) { close_settings(); return; }
    if (s_settings_editing) {
        if (button == BSP_BTN_UP || button == BSP_BTN_DOWN) {
            s_settings_draft = city_settings_adjust(s_settings_draft,
                s_settings_selection == 3, button == BSP_BTN_UP ? 10 : -10);
            apply_settings_preview();
        } else if (button == BSP_BTN_OK) {
            s_settings_editing = false;
            if (s_settings_selection == 1)
                pokemon_audio_play(s_bestiary.buddy_species_id ? s_bestiary.buddy_species_id : CITY_SPECIES_PIKACHU);
        }
    } else if (button == BSP_BTN_UP || button == BSP_BTN_DOWN) {
        s_settings_selection = city_passport_turn_page(s_settings_selection, 6, button == BSP_BTN_DOWN);
    } else if (button == BSP_BTN_OK) {
        if (s_settings_selection == 0) {
            s_settings_draft = city_settings_toggle_language(s_settings_draft);
        } else if (s_settings_selection == 1 || s_settings_selection == 3) {
            s_settings_editing = true;
        } else if (s_settings_selection == 2) {
            s_settings_draft = city_settings_toggle_mute(s_settings_draft);
            apply_settings_preview();
        } else if (s_settings_selection == 5) { close_settings(); return; }
        else {
            if (city_settings_equal(&s_settings, &s_settings_draft) && !s_settings_load_error && !s_settings_error) {
                close_settings(); return;
            }
            pokemon_audio_stop();
            s_settings_error = false;
            if (s_settings_requests && xQueueOverwrite(s_settings_requests, &s_settings_draft) == pdTRUE)
                s_settings_saving = true;
            else s_settings_error = true;
        }
    }
    set_state(UI_SETTINGS);
}

static void finish_settings_save(const settings_result_t *result)
{
    s_settings_saving = false;
    s_settings_error = result->error != ESP_OK;
    if (!s_settings_error) {
        s_settings = result->settings;
        s_settings_load_error = false;
        close_settings();
        ESP_LOGI(
            TAG, "SETTINGS_SAVED volume=%u muted=%u brightness=%u language=%u",
            s_settings.volume, s_settings.muted, s_settings.brightness,
            (unsigned)s_settings.language);
    } else set_state(UI_SETTINGS);
}

static void start_capture_round(uint64_t now)
{
    city_capture_begin(&s_round, esp_random(), now);
    set_state(UI_AIM);
}

static void start_capture_session(void)
{
    const uint64_t now = now_ms();
    s_attempts = CITY_GAME_CAPTURE_ATTEMPTS;
    s_capture_feedback = "Wait";
    s_capture_deadline_ms = now + CITY_GAME_CAPTURE_BUDGET_MS;
    start_capture_round(now);
}

static bool new_encounter_sequence(uint64_t *sequence)
{
    return s_bestiary_ready &&
           city_bestiary_next_encounter_sequence(
               &s_bestiary, sequence);
}

static bool persist_discovery(void)
{
    if (!s_bestiary_ready) {
        ESP_LOGE(TAG, "Bestiary unavailable; discovery not persisted");
        return false;
    }

    const city_bestiary_result_t result = city_bestiary_mark_seen(
        &s_bestiary,
        s_current_species_id,
        bsp_bestiary_store_persist,
        (void *)&BSP_BESTIARY_STORE_DEFAULT);
    if (result != CITY_BESTIARY_APPLIED &&
        result != CITY_BESTIARY_UNCHANGED) {
        ESP_LOGE(TAG, "Discovery commit rejected: result=%d", (int)result);
        return false;
    }
    ESP_LOGI(
        TAG, "DISCOVERY_COMMITTED species=%03u result=%s",
        s_current_species_id,
        result == CITY_BESTIARY_APPLIED ? "applied" : "unchanged");
    return true;
}

static bool persist_capture(void)
{
    if (!s_bestiary_ready || s_encounter_sequence == 0U ||
        s_current_place_id == CITY_PLACE_INVALID_ID) {
        ESP_LOGE(TAG, "Bestiary unavailable; capture not persisted");
        return false;
    }

    const city_creature_record_t *buddy = city_bestiary_record_const(&s_bestiary, s_bestiary.buddy_species_id);
    const uint16_t bond_before = buddy ? buddy->friendship : 0;
    const city_bestiary_result_t result = city_bestiary_capture_with_stats(
        &s_bestiary,
        s_encounter_sequence,
        s_current_species_id,
        s_current_place_id,
        &s_current_stats,
        bsp_bestiary_store_persist,
        (void *)&BSP_BESTIARY_STORE_DEFAULT);
    if (result != CITY_BESTIARY_APPLIED &&
        result != CITY_BESTIARY_DUPLICATE) {
        ESP_LOGE(TAG, "Capture commit rejected: result=%d", (int)result);
        return false;
    }

    if (result == CITY_BESTIARY_APPLIED) {
        buddy = city_bestiary_record_const(&s_bestiary, s_bestiary.buddy_species_id);
        s_capture_bond_gain = buddy ? buddy->friendship - bond_before : 0;
    }
    ESP_LOGI(
        TAG,
        "CAPTURE_COMMITTED species=%03u count=%lu sequence=%llu result=%s",
        s_current_species_id,
        (unsigned long)city_bestiary_record_const(
            &s_bestiary, s_current_species_id)->capture_count,
        (unsigned long long)s_encounter_sequence,
        result == CITY_BESTIARY_APPLIED ? "applied" : "duplicate");
    return true;
}

static bool persist_buddy(void)
{
    if (!s_bestiary_ready) return false;
    city_bestiary_result_t result = city_bestiary_choose_buddy(&s_bestiary,
        city_species_id_at(s_bestiary_selection), bsp_bestiary_store_persist,
        (void *)&BSP_BESTIARY_STORE_DEFAULT);
    return result == CITY_BESTIARY_APPLIED || result == CITY_BESTIARY_UNCHANGED;
}

static bool persist_evolution(void)
{
    if (!s_bestiary_ready) return false;
    const city_bestiary_result_t result = city_bestiary_evolve(&s_bestiary, s_evolution_source_id,
        bsp_bestiary_store_persist, (void *)&BSP_BESTIARY_STORE_DEFAULT);
    return result == CITY_BESTIARY_APPLIED || result == CITY_BESTIARY_UNCHANGED;
}

static bool persist_recovery(void)
{
    if (!s_bestiary_ready) return false;
    const city_bestiary_result_t result = city_bestiary_recover(&s_bestiary,
        city_species_id_at(s_bestiary_selection), bsp_bestiary_store_persist,
        (void *)&BSP_BESTIARY_STORE_DEFAULT);
    return result == CITY_BESTIARY_APPLIED || result == CITY_BESTIARY_UNCHANGED;
}

static bool persist_release(void)
{
    if (!s_bestiary_ready) return false;
    return city_bestiary_release_instance(&s_bestiary,
        s_release_instance_id, bsp_bestiary_store_persist,
        (void *)&BSP_BESTIARY_STORE_DEFAULT) == CITY_BESTIARY_APPLIED;
}

static void bestiary_write_task(void *argument)
{
    (void)argument;
    const write_operation_t operation = s_pending_write;
    const bool saved = operation == WRITE_RELEASE ? persist_release() : operation == WRITE_RECOVER ? persist_recovery() :
        operation == WRITE_EVOLUTION ? persist_evolution() : operation == WRITE_BUDDY ? persist_buddy() : operation == WRITE_WILD
        ? city_bestiary_reserve_wild(&s_bestiary, &s_wild_guard, now_ms(),
            s_current_species_id, bsp_bestiary_store_persist,
            (void *)&BSP_BESTIARY_STORE_DEFAULT) == CITY_BESTIARY_APPLIED
        : operation == WRITE_WILD_CLEAR
        ? city_bestiary_clear_wild_cooldown(&s_bestiary, &s_wild_guard, now_ms(),
            bsp_bestiary_store_persist, (void *)&BSP_BESTIARY_STORE_DEFAULT) == CITY_BESTIARY_APPLIED
        : operation == WRITE_DISCOVERY
                           ? persist_discovery()
                           : operation == WRITE_CAPTURE
                                 ? persist_capture()
                                 : false;

    while (!bsp_lvgl_lock(1000)) {
        ESP_LOGW(TAG, "Waiting to publish bestiary result");
    }
    s_save_in_progress = false;
    if (operation == WRITE_WILD_CLEAR) {
        s_pending_write = WRITE_NONE;
        s_wild_clear_retry_ms = now_ms() + 30000U;
        ESP_LOGI(TAG, "WILD_COOLDOWN_CLEAR saved=%u", saved);
    } else if (saved) {
        s_pending_write = WRITE_NONE;
        set_state(operation == WRITE_RELEASE ? UI_RELEASED : operation == WRITE_RECOVER ? UI_BESTIARY_DETAIL :
                  operation == WRITE_EVOLUTION ? UI_EVOLVED : operation == WRITE_BUDDY ? UI_HOME : (operation == WRITE_DISCOVERY || operation == WRITE_WILD)
                      ? UI_ENCOUNTER
                      : UI_CAPTURED);
    } else {
        set_state(UI_STORAGE_ERROR);
    }
    bsp_lvgl_unlock();
    vTaskDelete(NULL);
}

static bool request_bestiary_write(write_operation_t operation)
{
    if (operation == WRITE_NONE) {
        return false;
    }
    if (s_save_in_progress) {
        return operation == s_pending_write;
    }

    s_pending_write = operation;
    s_save_in_progress = true;
    if (xTaskCreate(
            bestiary_write_task,
            "bestiary_write",
            12288,
            NULL,
            4,
            NULL) != pdPASS) {
        s_save_in_progress = false;
        ESP_LOGE(TAG, "Failed to create bestiary write task");
        return false;
    }
    return true;
}

static void load_bestiary(void)
{
    city_bestiary_init(&s_bestiary);
    bool migrated = false;
    const esp_err_t err = bsp_bestiary_store_load(
        &BSP_BESTIARY_STORE_DEFAULT, &s_bestiary, &migrated);
    if (err != ESP_OK) {
        s_bestiary_ready = false;
        ESP_LOGE(TAG, "Bestiary load failed: %s", esp_err_to_name(err));
        return;
    }

    s_bestiary_ready = true;
    ESP_LOGI(TAG, "BUDDY_READY species=%u", s_bestiary.buddy_species_id);
    const city_wild_reward_snapshot_t snapshot = {
        .schema_version = CITY_WILD_REWARD_SCHEMA_VERSION,
        .cooldown_active = s_bestiary.wild_cooldown_active,
    };
    city_wild_reward_guard_init(&s_wild_guard, &snapshot, now_ms());
    ESP_LOGI(
        TAG,
        "BESTIARY_READY schema=%u count=%lu sequence=%llu migrated=%u",
        s_bestiary.schema_version,
        (unsigned long)total_capture_count(),
        (unsigned long long)s_bestiary.last_settled_sequence,
        migrated ? 1U : 0U);
}

static void load_place_data(void)
{
    s_place_data_ready = false;

    bool identity_created = false;
    bool catalog_found = false;
    uint16_t place_count = 0U;
    const esp_err_t err = place_scan_coordinator_start(
        &PLACE_SCAN_COORDINATOR_CONFIG_DEFAULT,
        &identity_created, &catalog_found, &place_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Place scan unavailable: %s", esp_err_to_name(err));
        return;
    }

    s_place_data_ready = true;
    ESP_LOGI(
        TAG,
        "PLACE_DATA_READY identity_created=%u catalog_found=%u places=%u",
        identity_created ? 1U : 0U,
        catalog_found ? 1U : 0U,
        place_count);
}

static void begin_place_scan(void)
{
    if (city_battery_critical(s_battery_soc)) {
        set_state(UI_LOW_BATTERY);
        return;
    }
    s_current_place_id = CITY_PLACE_INVALID_ID;
    if (!s_place_data_ready || !place_scan_coordinator_request()) {
        set_state(UI_PLACE_ERROR);
        return;
    }
    set_state(UI_SCANNING);
}

static void begin_wild_encounter(void)
{
    if (city_battery_critical(s_battery_soc)) { set_state(UI_LOW_BATTERY); return; }
    if (city_wild_reward_remaining_ms(&s_wild_guard, now_ms()) != 0U) return;
    city_encounter_selection_t selection;
    if (!s_bestiary_ready || !new_encounter_sequence(&s_encounter_sequence) ||
        !city_wild_encounter_select(esp_random(), &selection)) return;
    s_new_place_stamp = false;
    s_capture_bond_gain = 0;
    s_current_place_id = CITY_WILD_PLACE_ID;
    s_current_species_id = selection.species_id;
    s_current_stats = selection.stats;
    s_attempts = CITY_GAME_CAPTURE_ATTEMPTS;
    s_encounter_selection = 0U;
    if (!city_bestiary_encounter_status(&s_bestiary, s_current_species_id, &s_encounter_previous_state)) return;
    if (!request_bestiary_write(WRITE_WILD)) set_state(UI_STORAGE_ERROR);
}

static void handle_place_result(const place_scan_result_t *result)
{
    if (result == NULL) {
        return;
    }
    ESP_LOGI(
        TAG,
        "PLACE_RESULT kind=%d eligible=%u place_id=%u score=%u aps=%u duration_ms=%lu "
        "heap_before=%lu heap_after=%lu heap_min=%lu error=%s",
        (int)result->kind,
        result->encounter_eligible ? 1U : 0U,
        result->place_id,
        result->confidence_permille,
        result->ap_count,
        (unsigned long)result->duration_ms,
        (unsigned long)result->free_heap_before,
        (unsigned long)result->free_heap_after,
        (unsigned long)result->minimum_free_heap,
        esp_err_to_name(result->error));

    switch (result->kind) {
    case PLACE_RESULT_KNOWN:
    case PLACE_RESULT_NEW_CONFIRMED:
        if (!result->encounter_eligible) {
            ESP_LOGW(TAG, "ENCOUNTER_BLOCKED no fresh eligible evidence");
            set_state(UI_PLACE_ERROR);
            break;
        }
        s_current_place_id = result->place_id;
        s_new_place_stamp = result->kind == PLACE_RESULT_NEW_CONFIRMED;
        s_capture_bond_gain = 0;
        s_attempts = CITY_GAME_CAPTURE_ATTEMPTS;
        s_encounter_selection = 0U;
        city_encounter_selection_t selection;
        if (s_current_place_id == CITY_PLACE_INVALID_ID ||
            !new_encounter_sequence(&s_encounter_sequence) ||
            !city_encounter_select(
                s_current_place_id,
                result->kind == PLACE_RESULT_NEW_CONFIRMED,
                &s_bestiary,
                esp_random(),
                &selection)) {
            set_state(UI_STORAGE_ERROR);
            break;
        }
        s_current_species_id = selection.species_id;
        s_current_stats = selection.stats;
        ESP_LOGI(
            TAG,
            "ENCOUNTER_SELECTED place_id=%u species=%03u stats=%u/%u/%u",
            s_current_place_id, s_current_species_id,
            s_current_stats.hp, s_current_stats.attack,
            s_current_stats.defense);
        if (!city_bestiary_encounter_status(&s_bestiary, s_current_species_id, &s_encounter_previous_state)) {
            set_state(UI_STORAGE_ERROR); break;
        }
        if (!request_bestiary_write(WRITE_DISCOVERY)) {
            set_state(UI_STORAGE_ERROR);
        }
        break;
    case PLACE_RESULT_CANDIDATE_WAIT:
        set_state(UI_PLACE_PENDING);
        break;
    case PLACE_RESULT_GRAY:
        set_state(UI_PLACE_GRAY);
        break;
    case PLACE_RESULT_WILD:
        set_state(UI_PLACE_WILD);
        break;
    case PLACE_RESULT_UNSTABLE:
        set_state(UI_PLACE_UNSTABLE);
        break;
    case PLACE_RESULT_SCAN_ERROR:
        set_state(UI_PLACE_ERROR);
        break;
    case PLACE_RESULT_STORAGE_ERROR:
        set_state(UI_PLACE_STORAGE_ERROR);
        break;
    case PLACE_RESULT_CAPACITY_FULL:
        set_state(UI_PLACE_FULL);
        break;
    }
}

static void update_aim(uint64_t now)
{
    if (now >= s_capture_deadline_ms) {
        s_attempts = 0U;
        s_capture_feedback = "Time is up! Watch for NOW.";
        ESP_LOGI(TAG, "CAPTURE_TIMEOUT budget_ms=%u",
                 CITY_GAME_CAPTURE_BUDGET_MS);
        set_state(UI_ESCAPED);
        return;
    }

    const uint64_t elapsed = now - s_round.started_ms;
    if (elapsed >= s_round.duration_ms) {
        s_capture_feedback = "No throw";
        if (s_attempts > 0U) {
            --s_attempts;
        }
        ESP_LOGI(TAG, "ATTEMPT_TIMEOUT remaining=%u", s_attempts);
        if (s_attempts == 0U) {
            set_state(UI_ESCAPED);
        } else {
            start_capture_round(now);
        }
        return;
    }

    uint16_t progress = city_capture_progress_permille(&s_round, now);
    lv_obj_set_x(s_marker, (int)((progress * 176U) / 1000U));

    uint32_t start = s_round.target_center_ms - s_round.target_half_width_ms;
    uint32_t end = s_round.target_center_ms + s_round.target_half_width_ms;
    bool target = elapsed >= start && elapsed <= end;
    lv_label_set_text(s_aim_cue, tr(target ? "NOW" : "WAIT"));
    const unsigned seconds = (unsigned)((s_capture_deadline_ms - now + 999U) / 1000U);
    if (visible_language() == CITY_LANGUAGE_SIMPLIFIED_CHINESE) {
        lv_label_set_text_fmt(
            s_aim_status, "%u 次  %u 秒  %s", s_attempts, seconds,
            tr(target ? "Press OK!" : s_capture_feedback));
    } else {
        lv_label_set_text_fmt(
            s_aim_status, "%u %s  %u sec  %s", s_attempts,
            s_attempts == 1 ? "try" : "tries", seconds,
            target ? "Press OK!" : s_capture_feedback);
    }
    lv_obj_set_style_border_color(
        s_ring, lv_color_hex(target ? COLOR_GREEN : COLOR_CORAL), 0);

    int pulse = (int)((elapsed / 30U) % 24U);
    if (pulse > 12) {
        pulse = 24 - pulse;
    }
    int size = 92 - pulse;
    lv_obj_set_size(s_ring, size, size);
    lv_obj_set_pos(s_ring, (220 - size) / 2, 53 - size / 2);
}

static void finish_throw(uint64_t now)
{
    if (s_throw_hit) {
        set_state(UI_CATCHING);
        return;
    }

    s_attempts -= 1U;
    if (s_attempts == 0U || now >= s_capture_deadline_ms) {
        set_state(UI_ESCAPED);
    } else {
        start_capture_round(now);
    }
}

static void update_throw(uint64_t now)
{
    uint64_t elapsed = now - s_state_started_ms;
    if (elapsed >= THROW_MS) {
        finish_throw(now);
        return;
    }

    uint32_t permille = (uint32_t)((elapsed * 1000U) / THROW_MS);
    int inverse = 1000 - (int)permille;
    int center_x = (
        inverse * inverse * 110 +
        2 * inverse * (int)permille * 66 +
        (int)permille * (int)permille * 110) / 1000000;
    int center_y = (
        inverse * inverse * 136 +
        2 * inverse * (int)permille * 45 +
        (int)permille * (int)permille * 61) / 1000000;
    int size = 60 - (int)((38U * permille) / 1000U);
    ball_geometry(center_x - size / 2, center_y - size / 2, size);
}

static void tick(lv_timer_t *timer)
{
    (void)timer;
    uint64_t now = now_ms();
    settings_result_t settings_result;
    if (s_settings_results && xQueueReceive(s_settings_results, &settings_result, 0) == pdTRUE)
        finish_settings_save(&settings_result);
    int soc;
    if (s_battery_queue && xQueueReceive(s_battery_queue, &soc, 0) == pdTRUE) {
        s_battery_soc = soc;
        char text[20];
        if (soc < 0) snprintf(text, sizeof(text), "BAT --");
        else snprintf(text, sizeof(text), "%s%d%%", city_battery_low(soc) ? "LOW " : "", soc);
        lv_label_set_text(s_battery_label, text);
        lv_obj_set_style_text_color(s_battery_label,
            lv_color_hex(city_battery_low(soc) ? COLOR_CORAL : COLOR_MUTED), 0);
        if (!s_pocket.screen_off) bsp_display_backlight(city_settings_backlight(visible_settings(), false, city_battery_low(soc)));
    }
    const bool busy = s_settings_saving || s_state == UI_SETTINGS || s_save_in_progress || s_state == UI_SCANNING ||
        s_state == UI_PLACE_PENDING || s_state == UI_ENCOUNTER ||
        s_state == UI_AIM || s_state == UI_THROWING || s_state == UI_CATCHING ||
        s_state == UI_STORAGE_ERROR;
    if (city_pocket_idle(&s_pocket, now, busy, s_battery_soc)) {
        bsp_display_backlight(0);
        lv_timer_set_period(s_tick, 1000);
        ESP_LOGI(TAG, "SCREEN_OFF idle");
    }
    if (s_save_in_progress) return;
    if (s_state == UI_PLACE_WILD && s_wild_countdown) {
        const unsigned seconds = (unsigned)((city_wild_reward_remaining_ms(&s_wild_guard, now) + 999U) / 1000U);
        lv_label_set_text_fmt(
            s_wild_countdown, tr(seconds ? "Wait %02u:%02u" : "Ready!"),
            seconds / 60U, seconds % 60U);
        lv_label_set_text(
            s_status,
            tr(seconds ? LV_SYMBOL_UP " Go home"
                       : "Press OK to look around\n" LV_SYMBOL_UP " Go home"));
    }
    if (s_state == UI_PLACE_PENDING && s_place_countdown) {
        const uint32_t seconds =
            city_location_confirmation_remaining_seconds(
                s_state_started_ms, now);
        if (seconds > 0U) {
            lv_label_set_text_fmt(
                s_place_countdown, tr("Again in %lus"),
                (unsigned long)seconds);
        } else {
            lv_label_set_text(s_place_countdown, tr("Checking now..."));
        }
    }
    if (!busy && s_bestiary_ready && s_bestiary.wild_cooldown_active &&
        now >= s_wild_clear_retry_ms && city_wild_reward_remaining_ms(&s_wild_guard, now) == 0U) {
        s_wild_clear_retry_ms = now + 30000U;
        if (request_bestiary_write(WRITE_WILD_CLEAR)) return;
        s_pending_write = WRITE_NONE;
    }

    if (s_state == UI_SCANNING || s_state == UI_PLACE_PENDING) {
        place_scan_result_t result;
        if (place_scan_coordinator_receive(&result)) {
            handle_place_result(&result);
        }
    } else if (s_state == UI_AIM) {
        update_aim(now);
    } else if (s_state == UI_THROWING) {
        update_throw(now);
    } else if (s_state == UI_CATCHING &&
               now - s_state_started_ms >= CATCH_MS &&
               !request_bestiary_write(WRITE_CAPTURE)) {
        set_state(UI_STORAGE_ERROR);
    }
}

static void abandon_encounter(void)
{
    s_attempts = 0U;
    s_throw_hit = false;
    s_round.active = false;
    s_capture_deadline_ms = 0U;
    ESP_LOGI(
        TAG, "ENCOUNTER_ABANDONED species=%03u place_id=%u",
        s_current_species_id, s_current_place_id);
    set_state(UI_ABANDONED);
}

static void throw_ball(void)
{
    const uint64_t now = now_ms();
    const uint64_t elapsed = now - s_round.started_ms;
    city_capture_result_t result = city_capture_throw(&s_round, now);
    s_throw_hit = result == CITY_CAPTURE_HIT;
    if (!s_throw_hit) s_capture_feedback = result == CITY_CAPTURE_TIMEOUT ? "Time up" :
        elapsed < s_round.target_center_ms - s_round.target_half_width_ms ? "Too early" : "Too late";
    ESP_LOGI(TAG, "THROW result=%s",
             s_throw_hit ? "hit" : "miss");
    set_state(UI_THROWING);
}

static void on_button(bsp_btn_t button, bsp_btn_ev_t event, void *user)
{
    (void)user;
    if (!bsp_lvgl_lock(500)) return;
    if (event == BSP_BTN_PRESS) {
        if (city_pocket_press(&s_pocket, now_ms())) {
            bsp_display_backlight(city_settings_backlight(visible_settings(), false, city_battery_low(s_battery_soc)));
            lv_timer_set_period(s_tick, 33);
            ESP_LOGI(TAG, "SCREEN_WAKE");
        }
        bsp_lvgl_unlock();
        return;
    }
    if (s_pocket.screen_off || s_pocket.consume_gesture || s_save_in_progress || s_settings_saving) {
        bsp_lvgl_unlock(); return;
    }
    if (s_state == UI_SETTINGS && (event == BSP_BTN_CLICK ||
        (event == BSP_BTN_LONG && button == BSP_BTN_OK))) {
        s_pocket.last_activity_ms = now_ms();
        handle_settings_button(button, event == BSP_BTN_LONG);
        bsp_lvgl_unlock(); return;
    }
    const bool handles_long_ok = event == BSP_BTN_LONG && button == BSP_BTN_OK &&
        (s_state == UI_AIM || s_state == UI_BESTIARY_LIST ||
         s_state == UI_RELEASE_PICKER || s_state == UI_RELEASE_CONFIRM);
    if (event == BSP_BTN_LONG && button == BSP_BTN_UP && s_state == UI_HOME) {
        city_pocket_sleep(&s_pocket);
        bsp_display_backlight(0);
        lv_timer_set_period(s_tick, 1000);
        ESP_LOGI(TAG, "SCREEN_OFF manual");
        bsp_lvgl_unlock(); return;
    }
    if (event != BSP_BTN_CLICK && !handles_long_ok) { bsp_lvgl_unlock(); return; }
    s_pocket.last_activity_ms = now_ms();
    if (s_state == UI_PLACE_WILD) {
        if (button == BSP_BTN_UP) set_state(UI_HOME);
        else if (button == BSP_BTN_OK) begin_wild_encounter();
        bsp_lvgl_unlock(); return;
    }

    ESP_LOGI(TAG, "BUTTON key=%d event=%d state=%s",
             (int)button, (int)event, state_name(s_state));
    if (handles_long_ok) {
        if (s_state == UI_AIM) {
            abandon_encounter();
        } else if (s_state == UI_RELEASE_PICKER || s_state == UI_RELEASE_CONFIRM) {
            set_state(UI_POKEMON_ACTIONS);
        } else {
            s_bestiary_selection = 0U;
            set_state(UI_HOME);
        }
        bsp_lvgl_unlock();
        return;
    }

    if (s_state == UI_HOME) {
        if (button == BSP_BTN_UP || button == BSP_BTN_DOWN) {
            s_home_selection = city_passport_turn_page(s_home_selection, 4U, button == BSP_BTN_DOWN);
            set_state(UI_HOME);
        } else if (button == BSP_BTN_OK) {
            if (s_home_selection == 0U) {
                begin_place_scan();
            } else if (s_home_selection == 1U) {
                s_bestiary_selection = 0U;
                set_state(UI_BESTIARY_LIST);
            } else if (s_home_selection == 2U) {
                s_passport_page = 0U;
                set_state(UI_PASSPORT);
            } else {
                s_settings_draft = s_settings;
                s_settings_selection = 0;
                s_settings_editing = false;
                s_settings_error = false;
                set_state(UI_SETTINGS);
            }
        }
        bsp_lvgl_unlock();
        return;
    }

    if (s_state == UI_PASSPORT) {
        if (button == BSP_BTN_OK) set_state(UI_HOME);
        else if (button == BSP_BTN_UP || button == BSP_BTN_DOWN) {
            city_passport_stamps_t stamps;
            const bool ready = place_scan_coordinator_passport(&stamps);
            const city_passport_progress_t p = city_passport_progress(
                ready ? &stamps : NULL, s_bestiary_ready ? &s_bestiary : NULL);
            s_passport_page = city_passport_turn_page(s_passport_page, p.pages, button == BSP_BTN_DOWN);
            set_state(UI_PASSPORT);
        }
        bsp_lvgl_unlock(); return;
    }

    if (s_state == UI_ENCOUNTER &&
        (button == BSP_BTN_UP || button == BSP_BTN_DOWN)) {
        s_encounter_selection = s_encounter_selection == 0U ? 1U : 0U;
        set_state(UI_ENCOUNTER);
        bsp_lvgl_unlock();
        return;
    }

    if (s_state == UI_BESTIARY_LIST) {
        if (button == BSP_BTN_UP) {
            s_bestiary_selection =
                s_bestiary_selection == 0U
                    ? CITY_SPECIES_COUNT
                    : (uint8_t)(s_bestiary_selection - 1U);
            set_state(UI_BESTIARY_LIST);
        } else if (button == BSP_BTN_DOWN) {
            s_bestiary_selection = (uint8_t)(
                (s_bestiary_selection + 1U) %
                (CITY_SPECIES_COUNT + 1U));
            set_state(UI_BESTIARY_LIST);
        } else if (button == BSP_BTN_OK) {
            if (s_bestiary_selection == CITY_SPECIES_COUNT) {
                set_state(UI_HOME);
            } else {
                const city_creature_record_t *record =
                    city_bestiary_record_const(
                        &s_bestiary,
                        city_species_id_at(s_bestiary_selection));
                set_state(record->state != CITY_DISCOVERY_UNKNOWN ? UI_BESTIARY_DETAIL : UI_BESTIARY_HINT);
            }
        }
        bsp_lvgl_unlock();
        return;
    }

    if (s_state == UI_EVOLUTION) {
        const bool ready = city_evolution_ready(&s_bestiary, s_evolution_source_id);
        if (button == BSP_BTN_UP || button == BSP_BTN_DOWN) {
            s_evolution_selection = ready ? 1U - s_evolution_selection : 1U;
            set_state(UI_EVOLUTION);
        } else if (button == BSP_BTN_OK) {
            if (s_evolution_selection == 1) set_state(UI_BESTIARY_DETAIL);
            else if (ready && !request_bestiary_write(WRITE_EVOLUTION)) set_state(UI_STORAGE_ERROR);
        }
        bsp_lvgl_unlock(); return;
    }

    if (s_state == UI_POKEMON_ACTIONS) {
        const uint16_t id = city_species_id_at(s_bestiary_selection);
        const bool has_evolution = city_evolution_target(id) != 0U;
        const uint8_t action_count = has_evolution ? 5U : 4U;
        const city_creature_record_t *record = city_bestiary_record_const(&s_bestiary, id);
        const bool full = record->current_hp >= city_bestiary_max_hp(record);
        if (button == BSP_BTN_UP || button == BSP_BTN_DOWN) {
            do {
                s_action_selection = (uint8_t)((s_action_selection +
                    (button == BSP_BTN_DOWN ? 1U : action_count - 1U)) % action_count);
            } while (full && s_action_selection == 0U);
            set_state(UI_POKEMON_ACTIONS);
        } else if (button == BSP_BTN_OK) {
            if (s_action_selection == 0U) {
                if (!full &&
                    !request_bestiary_write(WRITE_RECOVER)) set_state(UI_STORAGE_ERROR);
            } else if (s_action_selection == 1U) {
                s_owned_selection = 0U;
                set_state(UI_OWNED_DETAIL);
            } else if (has_evolution && s_action_selection == 2U) {
                s_evolution_source_id = id;
                s_evolution_selection = 1U;
                set_state(UI_EVOLUTION);
            } else if (s_action_selection == action_count - 2U) {
                s_release_copy_selection = 0U;
                s_release_selection = 1U;
                set_state(UI_RELEASE_PICKER);
            } else {
                set_state(UI_BESTIARY_DETAIL);
            }
        }
        bsp_lvgl_unlock(); return;
    }

    if (s_state == UI_OWNED_DETAIL) {
        const uint16_t count = city_bestiary_owned_count(&s_bestiary, city_species_id_at(s_bestiary_selection));
        if ((button == BSP_BTN_UP || button == BSP_BTN_DOWN) && count > 0U) {
            s_owned_selection = (uint16_t)((s_owned_selection +
                (button == BSP_BTN_DOWN ? 1U : count - 1U)) % count);
            set_state(UI_OWNED_DETAIL);
        } else if (button == BSP_BTN_OK) set_state(UI_POKEMON_ACTIONS);
        bsp_lvgl_unlock(); return;
    }

    if (s_state == UI_RELEASE_PICKER) {
        const uint16_t species_id = city_species_id_at(s_bestiary_selection);
        const uint16_t count = city_bestiary_owned_count(&s_bestiary, species_id);
        if ((button == BSP_BTN_UP || button == BSP_BTN_DOWN) && count > 0U) {
            s_release_copy_selection = (uint16_t)((s_release_copy_selection +
                (button == BSP_BTN_DOWN ? 1U : count)) % (count + 1U));
            set_state(UI_RELEASE_PICKER);
        } else if (button == BSP_BTN_OK) {
            const city_owned_pokemon_t *owned = city_bestiary_owned_at(
                &s_bestiary, species_id, s_release_copy_selection);
            if (owned) {
                s_release_instance_id = owned->instance_id;
                s_release_selection = 1U;
                set_state(UI_RELEASE_CONFIRM);
            } else set_state(UI_POKEMON_ACTIONS);
        }
        bsp_lvgl_unlock(); return;
    }

    if (s_state == UI_RELEASE_CONFIRM) {
        if (button == BSP_BTN_UP || button == BSP_BTN_DOWN) {
            s_release_selection = (uint8_t)(1U - s_release_selection);
            set_state(UI_RELEASE_CONFIRM);
        } else if (button == BSP_BTN_OK) {
            if (s_release_selection == 1U) set_state(UI_POKEMON_ACTIONS);
            else if (!request_bestiary_write(WRITE_RELEASE)) set_state(UI_STORAGE_ERROR);
        }
        bsp_lvgl_unlock(); return;
    }

    if (s_state == UI_BESTIARY_DETAIL && button == BSP_BTN_DOWN) {
        set_state(UI_BESTIARY_LIST);
        bsp_lvgl_unlock(); return;
    }
    if ((s_state == UI_PLACE_ERROR || s_state == UI_PLACE_STORAGE_ERROR || s_state == UI_STORAGE_ERROR) &&
        button == BSP_BTN_UP) {
        /* No write is in flight here. Failed transactions have not changed assets.
           An already reserved Wild try remains consumed; never roll it back. */
        s_pending_write = WRITE_NONE;
        set_state(UI_HOME);
        bsp_lvgl_unlock(); return;
    }
    if (s_state == UI_CAPTURE_READY && button == BSP_BTN_UP) {
        set_state(UI_ENCOUNTER);
        bsp_lvgl_unlock(); return;
    }

    if (s_state == UI_BESTIARY_DETAIL && button == BSP_BTN_UP) {
        const city_creature_record_t *record = city_bestiary_record_const(&s_bestiary, city_species_id_at(s_bestiary_selection));
        if (record && record->state == CITY_DISCOVERY_CAPTURED) {
            if (!request_bestiary_write(WRITE_BUDDY)) set_state(UI_STORAGE_ERROR);
        }
        bsp_lvgl_unlock(); return;
    }

    if (button != BSP_BTN_OK) {
        bsp_lvgl_unlock();
        return;
    }

    switch (s_state) {
    case UI_PLACE_ERROR:
    case UI_PLACE_STORAGE_ERROR:
        begin_place_scan();
        break;
    case UI_LOW_BATTERY:
    case UI_PLACE_GRAY:
    case UI_PLACE_WILD:
    case UI_PLACE_UNSTABLE:
    case UI_PLACE_FULL:
        set_state(UI_HOME);
        break;
    case UI_ENCOUNTER:
        if (s_encounter_selection == 0U) {
            if (total_capture_count() == 0U) set_state(UI_CAPTURE_READY);
            else start_capture_session();
        } else {
            abandon_encounter();
        }
        break;
    case UI_CAPTURE_READY:
        start_capture_session();
        break;
    case UI_BESTIARY_HINT:
        set_state(UI_BESTIARY_LIST);
        break;
    case UI_AIM:
        throw_ball();
        break;
    case UI_CAPTURED:
        s_bestiary_selection =
            species_selection_index(s_current_species_id);
        set_state(UI_BESTIARY_DETAIL);
        break;
    case UI_EVOLVED:
    case UI_ESCAPED:
    case UI_ABANDONED:
        set_state(UI_HOME);
        break;
    case UI_BESTIARY_DETAIL:
        if (city_bestiary_record_const(&s_bestiary,
                city_species_id_at(s_bestiary_selection))->state == CITY_DISCOVERY_CAPTURED) {
            const city_creature_record_t *record = city_bestiary_record_const(
                &s_bestiary, city_species_id_at(s_bestiary_selection));
            s_action_selection = record->current_hp < city_bestiary_max_hp(record) ? 0U : 1U;
            set_state(UI_POKEMON_ACTIONS);
        } else {
            set_state(UI_BESTIARY_LIST);
        }
        break;
    case UI_RELEASED:
        set_state(UI_BESTIARY_LIST);
        break;
    case UI_STORAGE_ERROR:
        if (!request_bestiary_write(s_pending_write)) {
            set_state(UI_STORAGE_ERROR);
        }
        break;
    default:
        break;
    }
    bsp_lvgl_unlock();
}

static void battery_task(void *argument)
{
    (void)argument;
    bool ready = false;
    for (;;) {
        if (!ready) ready = bsp_battery_init() == ESP_OK;
        int soc = ready ? bsp_battery_soc() : -1;
        if (soc < 0 || soc > 100) soc = -1;
        xQueueOverwrite(s_battery_queue, &soc);
        ESP_LOGI(TAG, "BATTERY soc=%d", soc);
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Pokedex AI Passport boot");
    ESP_LOGI(TAG, "BUILD_ID version=%s commit=%s source=%s dirty=%u",
             CITY_BUILD_VERSION, CITY_BUILD_GIT_COMMIT,
             CITY_BUILD_SOURCE_SHA256, CITY_BUILD_DIRTY);
    ESP_LOGI(TAG, "wake_cause=%d", (int)esp_sleep_get_wakeup_cause());

    city_bestiary_init(&s_bestiary);
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(nvs_err));
    } else {
        load_bestiary();
        load_place_data();
    }

    s_settings = city_settings_defaults();
    s_settings_load_error = bsp_settings_load(&s_settings) != ESP_OK;
    s_settings_draft = s_settings;
    pokemon_audio_set_preferences(s_settings.volume, s_settings.muted);
    s_settings_requests = xQueueCreate(1, sizeof(city_settings_t));
    s_settings_results = xQueueCreate(1, sizeof(settings_result_t));
    if (!s_settings_requests || !s_settings_results ||
        xTaskCreate(settings_task, "settings", 3072, NULL, 2, NULL) != pdPASS) {
        if (s_settings_requests) vQueueDelete(s_settings_requests);
        if (s_settings_results) vQueueDelete(s_settings_results);
        s_settings_requests = NULL; s_settings_results = NULL;
        ESP_LOGW(TAG, "Settings worker unavailable");
    }
    ESP_LOGI(
        TAG,
        "SETTINGS_READY volume=%u muted=%u brightness=%u language=%u defaults_on_error=%u",
        s_settings.volume, s_settings.muted, s_settings.brightness,
        (unsigned)s_settings.language, s_settings_load_error);
    ESP_ERROR_CHECK(bsp_i2c_init());
    if (!pokemon_audio_init()) ESP_LOGW(TAG, "Audio worker unavailable");
    s_battery_queue = xQueueCreate(1, sizeof(int));
    if (s_battery_queue && xTaskCreate(battery_task, "battery", 3072, NULL, 2, NULL) != pdPASS) {
        ESP_LOGW(TAG, "Battery worker unavailable");
    }
    ESP_ERROR_CHECK(bsp_display_init());
    if (bsp_lvgl_init() == NULL) {
        ESP_LOGE(TAG, "LVGL init failed");
        return;
    }
    bsp_display_backlight(city_settings_backlight(&s_settings, false, city_battery_low(s_battery_soc)));

    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "Initial UI lock unavailable");
        return;
    }
    {
        s_state = UI_HOME;
        s_state_started_ms = now_ms();
        s_pocket.last_activity_ms = s_state_started_ms;
        build_state();
        s_tick = lv_timer_create(tick, 33, NULL);
#if defined(CITY_CAPTURE_RENDER_SMOKE) || defined(CITY_AUDIO_RENDER_SMOKE) || defined(CITY_SETTINGS_SMOKE)
        lv_timer_pause(s_tick);
#endif
        bsp_lvgl_unlock();
    }

#ifdef CITY_PLACE_PENDING_SMOKE
    if (!bsp_lvgl_lock(2000)) {
        ESP_LOGE(TAG, "PLACE_PENDING_SMOKE_FAIL initial_lock");
        return;
    }
    set_state(UI_PLACE_PENDING);
    const uint64_t pending_started_ms = s_state_started_ms;
    bsp_lvgl_unlock();
    vTaskDelay(pdMS_TO_TICKS(CITY_LOCATION_CONFIRM_DELAY_MS + 1000U));
    if (!bsp_lvgl_lock(2000)) {
        ESP_LOGE(TAG, "PLACE_PENDING_SMOKE_FAIL final_lock");
        return;
    }
    const char *countdown_text =
        s_place_countdown != NULL ? lv_label_get_text(s_place_countdown) : "";
    const bool pending_passed =
        city_location_confirmation_remaining_seconds(
            pending_started_ms, now_ms()) == 0U &&
        strcmp(countdown_text, "Checking now...") == 0;
    ESP_LOGI(
        TAG, "PLACE_PENDING_SMOKE_%s elapsed_ms=%llu text=%s",
        pending_passed ? "PASS" : "FAIL",
        (unsigned long long)(now_ms() - pending_started_ms),
        countdown_text);
    set_state(UI_HOME);
    bsp_lvgl_unlock();
    return;
#endif

#ifdef CITY_SETTINGS_SMOKE
#include "settings_smoke.inc"
#endif

#ifdef CITY_AUDIO_RENDER_SMOKE
    /* No timers, buttons, scan or persistence writes in this diagnostic. */
    for (uint8_t i = 0; i < CITY_SPECIES_COUNT; ++i) {
        const uint16_t species_id = city_species_id_at(i);
        const pokemon_cry_t *cry = pokemon_cry_find(species_id);
        if (!cry || !bsp_lvgl_lock(2000)) { ESP_LOGE(TAG, "AUDIO_SMOKE_FAIL encounter"); return; }
        s_current_species_id = species_id;
        s_encounter_previous_state = CITY_DISCOVERY_SEEN;
        set_state(UI_ENCOUNTER);
        set_state(UI_ENCOUNTER); /* A menu redraw must not restart the cry. */
        bsp_lvgl_unlock();
        vTaskDelay(pdMS_TO_TICKS(cry->sample_count * 1000 / POKEMON_CRY_SAMPLE_RATE + 400));
        if (!bsp_lvgl_lock(2000)) { ESP_LOGE(TAG, "AUDIO_SMOKE_FAIL detail"); return; }
        s_bestiary_selection = i;
        city_creature_record_t *record = city_bestiary_record(&s_bestiary, species_id);
        const city_discovery_state_t saved_state = record->state;
        if (record->state == CITY_DISCOVERY_UNKNOWN) record->state = CITY_DISCOVERY_SEEN;
        set_state(UI_BESTIARY_LIST);
        set_state(UI_BESTIARY_DETAIL);
        record->state = saved_state;
        bsp_lvgl_unlock();
        vTaskDelay(pdMS_TO_TICKS(cry->sample_count * 1000 / POKEMON_CRY_SAMPLE_RATE + 400));
        ESP_LOGI(TAG, "AUDIO_DETAIL_PASS species=%u", species_id);
    }
    if (!bsp_lvgl_lock(2000)) { ESP_LOGE(TAG, "AUDIO_SMOKE_FAIL home"); return; }
    set_state(UI_HOME);
    bsp_lvgl_unlock();
    ESP_LOGI(TAG, "AUDIO_SMOKE_PASS captures=%lu", (unsigned long)total_capture_count());
    return;
#endif

#ifdef CITY_CAPTURE_RENDER_SMOKE
    // Dedicated diagnostic build: do not register input or run save/capture timers.
    for (unsigned round = 0; round < 3; ++round) {
        if (!bsp_lvgl_lock(2000)) { ESP_LOGE(TAG, "RENDER_SMOKE_FAIL lock"); return; }
        set_state(UI_THROWING);
        lv_label_set_text(s_status, "RENDER TEST - NO SAVE");
        bsp_lvgl_unlock();
        for (int size = 60; size >= 22; size -= 2) {
            vTaskDelay(pdMS_TO_TICKS(40));
            if (!bsp_lvgl_lock(2000)) { ESP_LOGE(TAG, "RENDER_SMOKE_FAIL frame"); return; }
            ball_geometry(110 - size / 2, 100 - size / 2, size);
            bsp_lvgl_unlock();
        }
        if (!bsp_lvgl_lock(2000)) { ESP_LOGE(TAG, "RENDER_SMOKE_FAIL catching"); return; }
        set_state(UI_CATCHING);
        lv_label_set_text(s_status, "RENDER TEST - NO SAVE");
        bsp_lvgl_unlock();
        vTaskDelay(pdMS_TO_TICKS(500));
        ESP_LOGI(TAG, "RENDER_SMOKE_ROUND %u", round + 1);
    }
    for (unsigned i = 0; i < 3; ++i) {
        if (!bsp_lvgl_lock(2000)) { ESP_LOGE(TAG, "RENDER_SMOKE_FAIL evolution"); return; }
        s_evolution_source_id = city_species_id_at(i);
        s_evolution_selection = 1;
        set_state(UI_EVOLUTION);
        lv_label_set_text(s_status, "RENDER TEST - NO SAVE");
        bsp_lvgl_unlock();
        vTaskDelay(pdMS_TO_TICKS(400));
        if (!bsp_lvgl_lock(2000)) { ESP_LOGE(TAG, "RENDER_SMOKE_FAIL reveal"); return; }
        set_state(UI_EVOLVED);
        lv_label_set_text(s_status, "RENDER TEST - NO SAVE");
        bsp_lvgl_unlock();
        vTaskDelay(pdMS_TO_TICKS(800));
        ESP_LOGI(TAG, "EVOLUTION_RENDER_PASS source=%u", s_evolution_source_id);
    }
    if (!bsp_lvgl_lock(2000)) { ESP_LOGE(TAG, "RENDER_SMOKE_FAIL home"); return; }
    set_state(UI_HOME);
    bsp_lvgl_unlock();
    ESP_LOGI(TAG, "RENDER_SMOKE_PASS captures=%lu", (unsigned long)total_capture_count());
    return;
#endif
    ESP_ERROR_CHECK(bsp_button_init(on_button, NULL));
    ESP_LOGI(
        TAG,
        "READY display=1 buttons=1 capture_count=%lu place_data=%u",
        (unsigned long)total_capture_count(),
        s_place_data_ready ? 1U : 0U);
}
