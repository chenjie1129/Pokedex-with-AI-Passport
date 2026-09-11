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

#include "esp_log.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

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
} ui_state_t;

typedef enum {
    WRITE_NONE = 0,
    WRITE_DISCOVERY,
    WRITE_CAPTURE,
    WRITE_WILD,
    WRITE_WILD_CLEAR,
} write_operation_t;

static const char *TAG = "pokedex";

static ui_state_t s_state;
static uint64_t s_state_started_ms;
static uint8_t s_attempts = 3;
static city_bestiary_t s_bestiary;
static bool s_bestiary_ready;
static bool s_save_in_progress;
static uint8_t s_home_selection;
static uint8_t s_passport_page;
static uint8_t s_encounter_selection;
static city_discovery_state_t s_encounter_previous_state;
static uint8_t s_bestiary_selection;
static write_operation_t s_pending_write;
static uint64_t s_encounter_sequence;
static bool s_throw_hit;
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
        "storage_error", "low_battery", "passport",
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
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label, width);
    lv_obj_set_height(label, lv_font_get_line_height(font) + 1);
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
    label_at(header, "Pokedex", &lv_font_montserrat_14, 0x247052, 12, 9, 90);
    char battery_text[20];
    if (s_battery_soc < 0) snprintf(battery_text, sizeof(battery_text), "BAT --");
    else snprintf(battery_text, sizeof(battery_text), "%s%d%%",
                  city_battery_low(s_battery_soc) ? "LOW " : "", s_battery_soc);
    s_battery_label = label_at(header, battery_text, &lv_font_montserrat_14,
        city_battery_low(s_battery_soc) ? COLOR_CORAL : COLOR_MUTED, 126, 9, 102);
    lv_obj_set_style_text_align(s_battery_label, LV_TEXT_ALIGN_RIGHT, 0);

    label_at(
        screen, title, &lv_font_montserrat_20,
        COLOR_INK, 10, TITLE_Y, 220);

    lv_obj_t *footer = lv_obj_create(screen);
    style_plain(footer, 0xF7FBF8);
    lv_obj_set_size(footer, SCREEN_WIDTH, 36);
    lv_obj_set_pos(footer, 0, FOOTER_Y);
    label_at(footer, "A", &lv_font_montserrat_14, 0xFFFFFF, 12, 8, 20);
    lv_obj_set_style_bg_color(lv_obj_get_child(footer, 0), lv_color_hex(COLOR_CORAL), 0);
    lv_obj_set_style_bg_opa(lv_obj_get_child(footer, 0), LV_OPA_COVER, 0);
    lv_obj_set_style_radius(lv_obj_get_child(footer, 0), LV_RADIUS_CIRCLE, 0);
    s_status = label_at(
        footer, action, &lv_font_montserrat_14, COLOR_INK, 39, 9, 188);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_LEFT, 0);
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

    lv_obj_set_size(s_ball_red, size, size / 2 + 1);
    lv_obj_set_pos(s_ball_red, 0, 0);
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
    lv_obj_set_style_border_width(s_ball, 2, 0);
    lv_obj_set_style_border_color(s_ball, lv_color_hex(COLOR_INK), 0);
    lv_obj_remove_flag(s_ball, LV_OBJ_FLAG_SCROLLABLE);

    s_ball_red = lv_obj_create(s_ball);
    style_plain(s_ball_red, COLOR_CORAL);

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

static lv_obj_t *create_menu_row(
    lv_obj_t *parent,
    int y,
    const char *title,
    const char *subtitle,
    bool selected)
{
    lv_obj_t *row = lv_obj_create(parent);
    style_plain(row, selected ? 0xE4F4E8 : 0xF7FBF8);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_border_width(row, selected ? 2 : 1, 0);
    lv_obj_set_style_border_color(
        row, lv_color_hex(selected ? COLOR_GREEN : 0xD5E0DD), 0);
    lv_obj_set_size(row, 220, 50);
    lv_obj_set_pos(row, 10, y);

    lv_obj_t *title_label = label_at(
        row, title, &lv_font_montserrat_20, COLOR_INK, 14, 3, 160);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_t *subtitle_label = label_at(
        row, subtitle, &lv_font_montserrat_14, COLOR_MUTED, 14, 29, 178);
    lv_obj_set_style_text_align(subtitle_label, LV_TEXT_ALIGN_LEFT, 0);
    label_at(
        row, selected ? ">" : "", &lv_font_montserrat_20,
        selected ? COLOR_CORAL : COLOR_MUTED, 188, 13, 20);
    return row;
}

static void build_home(void)
{
    s_screen = new_screen("CITY SPIRITS", "UP/DN SELECT  OK OPEN");
    char progress[32];
    snprintf(
        progress, sizeof(progress), "%u SEEN  %u CAUGHT",
        city_bestiary_discovered_count(&s_bestiary),
        city_bestiary_captured_count(&s_bestiary));

    create_menu_row(
        s_screen, 76, "EXPLORE", "Find a nearby spirit",
        s_home_selection == 0U);
    create_menu_row(
        s_screen, 132, "BESTIARY", progress,
        s_home_selection == 1U);
    create_menu_row(s_screen, 188, "PASSPORT", "Stamps and next goal",
                    s_home_selection == 2U);
    label_at(
        s_screen, "Hold UP: screen off", &lv_font_montserrat_14,
        COLOR_MUTED, 10, META_Y, 220);
}

static void build_passport(void)
{
    city_passport_stamps_t stamps;
    const bool have_places = place_scan_coordinator_passport(&stamps);
    const city_passport_progress_t progress = city_passport_progress(
        have_places ? &stamps : NULL, s_bestiary_ready ? &s_bestiary : NULL);
    if (s_passport_page >= progress.pages) s_passport_page = 0;
    s_screen = new_screen("PASSPORT", "UP/DN PAGE  OK HOME");
    char text[64];
    if (progress.places_ready)
        snprintf(text, sizeof(text), "%u/%u PLACES   PAGE %u/%u", progress.places,
                 CITY_PLACE_MAX_COUNT, s_passport_page + 1, progress.pages);
    else snprintf(text, sizeof(text), "PLACE DATA UNAVAILABLE");
    label_at(s_screen, text, &lv_font_montserrat_14, COLOR_INK, 10, 77, 220);
    if (progress.collection_ready)
        snprintf(text, sizeof(text), "SEEN %u/%u   CAUGHT %u/%u", progress.discovered,
                 CITY_SPECIES_COUNT, progress.captured, CITY_SPECIES_COUNT);
    else snprintf(text, sizeof(text), "COLLECTION UNAVAILABLE");
    label_at(s_screen, text, &lv_font_montserrat_14, COLOR_MUTED, 10, 101, 220);
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
        if (earned) snprintf(text, sizeof(text), "PLACE %02u", stamps.place_ids[index]);
        else snprintf(text, sizeof(text), "--");
        label_at(stamp, text, &lv_font_montserrat_14,
                 earned ? COLOR_GRASS_D : COLOR_MUTED, 2, 7, 96);
    }
    switch (progress.goal) {
    case CITY_PASSPORT_FIRST_CAPTURE:
        snprintf(text, sizeof(text), "Goal: catch your first spirit"); break;
    case CITY_PASSPORT_NEW_PLACE:
        snprintf(text, sizeof(text), "Goal: explore %u places", progress.target); break;
    case CITY_PASSPORT_CATCH_SPECIES:
        snprintf(text, sizeof(text), "Goal: %s %s", progress.target_seen ? "catch" : "find",
                 city_species_definition(progress.target)->name); break;
    case CITY_PASSPORT_COMPLETE:
        snprintf(text, sizeof(text), "All stamps and spirits collected"); break;
    default:
        snprintf(text, sizeof(text), "Saved data unavailable"); break;
    }
    label_at(s_screen, text, &lv_font_montserrat_14, COLOR_INK, 5, META_Y, 230);
    ESP_LOGI(TAG, "PASSPORT places=%u seen=%u caught=%u page=%u/%u goal=%u target=%u ready=%u",
             progress.places, progress.discovered, progress.captured, s_passport_page + 1,
             progress.pages, progress.goal, progress.target,
             progress.places_ready && progress.collection_ready);
}

static void build_scanning(void)
{
    s_screen = new_screen("SCANNING...", "PLEASE WAIT");
    s_field = create_field(s_screen);
    lv_obj_t *ring = lv_obj_create(s_field);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, 5, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(COLOR_CORAL), 0);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(ring, 82, 82);
    lv_obj_set_pos(ring, 69, 39);
    label_at(ring, "...", &lv_font_montserrat_20, 0x247052, 18, 22, 46);
}

static void build_place_status(
    const char *title,
    const char *action,
    const char *message)
{
    s_screen = new_screen(title, action);
    s_field = create_field(s_screen);
    label_at(
        s_field, "?", &lv_font_montserrat_20,
        COLOR_CORAL, 90, 48, 40);
    label_at(
        s_screen, message, &lv_font_montserrat_14,
        COLOR_MUTED, 10, META_Y, 220);
}

static void build_encounter(void)
{
    const city_species_definition_t *definition =
        city_species_definition(s_current_species_id);
    char title[32];
    char meta[40];
    snprintf(title, sizeof(title), "WILD %s", definition->name);
    snprintf(
        meta, sizeof(meta), "No.%03u  %s  %u/%u/%u",
        definition->species_id, definition->element,
        s_current_stats.hp, s_current_stats.attack,
        s_current_stats.defense);
    s_screen = new_screen(title, "UP/DN  OK CONFIRM");
    s_field = create_field(s_screen);
    create_species(s_field, s_current_species_id, false);
    const char *record_status = s_encounter_previous_state == CITY_DISCOVERY_CAPTURED
        ? "CAUGHT - IN BESTIARY" : s_encounter_previous_state == CITY_DISCOVERY_SEEN
        ? "SEEN - NOT CAUGHT" : "NEW - FIRST ENCOUNTER";
    lv_obj_t *badge = label_at(s_field, record_status, &lv_font_montserrat_14,
        s_encounter_previous_state == CITY_DISCOVERY_CAPTURED ? COLOR_GRASS_D : COLOR_INK,
        5, 5, 210);
    lv_obj_set_style_bg_color(badge, lv_color_hex(0xF7FBF8), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);

    const char *choices[] = {"CATCH", "LEAVE"};
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
            choice, choices[i], &lv_font_montserrat_14,
            COLOR_INK, 4, 6, 70);
    }
    label_at(
        s_screen, meta, &lv_font_montserrat_14,
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

static void build_aim(void)
{
    const city_species_definition_t *definition =
        city_species_definition(s_current_species_id);
    char title[32];
    snprintf(title, sizeof(title), "AIM %s", definition->name);
    s_screen = new_screen(title, "OK THROW  HOLD OK LEAVE");
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

    create_ball(s_field, 80, 118, 60);
    label_at(s_screen, "Throw when ring turns green", &lv_font_montserrat_14,
             COLOR_MUTED, 10, META_Y, 220);
}

static void build_throwing(void)
{
    s_screen = new_screen("FIRST PERSON THROW", "THROWING");
    s_field = create_field(s_screen);
    create_species(s_field, s_current_species_id, true);
    create_ball(s_field, 80, 118, 60);
    label_at(s_screen, "Ball in flight", &lv_font_montserrat_14,
             COLOR_MUTED, 10, META_Y, 220);
}

static void build_catching(void)
{
    s_screen = new_screen("CAPTURING...", "PLEASE WAIT");
    s_field = create_field(s_screen);
    create_ball(s_field, 88, 72, 44);
    label_at(s_screen, "One... Two... Three...", &lv_font_montserrat_14,
             COLOR_MUTED, 10, META_Y, 220);
}

static void build_captured(void)
{
    const city_species_definition_t *definition =
        city_species_definition(s_current_species_id);
    char message[48];
    snprintf(
        message, sizeof(message), "%s %u/%u/%u",
        definition->name, s_current_stats.hp,
        s_current_stats.attack, s_current_stats.defense);
    s_screen = new_screen("GOTCHA!", "OK  VIEW POKEDEX");
    s_field = create_field(s_screen);
    create_ball(s_field, 88, 69, 44);

    label_at(s_field, "*", &lv_font_montserrat_20, COLOR_YELLOW, 49, 46, 24);
    label_at(s_field, "*", &lv_font_montserrat_20, COLOR_YELLOW, 149, 57, 24);
    label_at(
        s_screen, message, &lv_font_montserrat_14,
        COLOR_MUTED, 10, META_Y, 220);
}

static void build_escaped(void)
{
    s_screen = new_screen("IT GOT AWAY", "OK  HOME");
    s_field = create_field(s_screen);
    create_species(s_field, s_current_species_id, false);
    label_at(s_screen, "Adjust your timing", &lv_font_montserrat_14,
             COLOR_MUTED, 10, META_Y, 220);
}

static void build_abandoned(void)
{
    const city_species_definition_t *definition =
        city_species_definition(s_current_species_id);
    char message[48];
    snprintf(message, sizeof(message), "You left %s alone", definition->name);
    s_screen = new_screen("ENCOUNTER ENDED", "OK  HOME");
    s_field = create_field(s_screen);
    create_species(s_field, s_current_species_id, false);
    label_at(s_screen, message, &lv_font_montserrat_14,
             COLOR_MUTED, 10, META_Y, 220);
}

static void build_bestiary_list(void)
{
    s_screen = new_screen("BESTIARY", "OK OPEN / HOLD HOME");
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
        lv_obj_set_size(row, 220, 38);
        lv_obj_set_pos(row, 10, 74 + row_index * 42);
        if (back) {
            label_at(row, "BACK TO HOME", &lv_font_montserrat_14, COLOR_INK, 8, 10, 196);
            continue;
        }
        const uint16_t id = city_species_id_at(i);
        const city_creature_record_t *record = city_bestiary_record_const(&s_bestiary, id);
        const city_species_definition_t *definition = city_species_definition(id);
        char name[40];
        snprintf(name, sizeof(name), "%03u %s", id,
                 record->state == CITY_DISCOVERY_UNKNOWN ? "???" : definition->name);
        lv_obj_t *label = label_at(row, name, &lv_font_montserrat_14, COLOR_INK, 7, 3, 132);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
        const char *status = record->state == CITY_DISCOVERY_CAPTURED ? "CAUGHT" :
                             record->state == CITY_DISCOVERY_SEEN ? "SEEN" : "NEW";
        label_at(row, status, &lv_font_montserrat_14,
                 record->state == CITY_DISCOVERY_CAPTURED ? COLOR_GRASS_D : COLOR_MUTED, 145, 3, 68);
        if (record->state == CITY_DISCOVERY_CAPTURED) {
            char count[32]; snprintf(count, sizeof(count), "Caught %lu", (unsigned long)record->capture_count);
            label = label_at(row, count, &lv_font_montserrat_14, COLOR_MUTED, 7, 19, 198);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
        }
    }
    char progress[48];
    snprintf(progress, sizeof(progress), "SEEN %u/%u   PAGE %u/%u",
             city_bestiary_discovered_count(&s_bestiary), CITY_SPECIES_COUNT,
             s_bestiary_selection / 4U + 1U, (CITY_SPECIES_COUNT + 4U) / 4U);
    label_at(s_screen, progress, &lv_font_montserrat_14, COLOR_MUTED, 5, META_Y, 230);
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
    char title[32];
    snprintf(
        title, sizeof(title), "No.%03u %s",
        species_id, definition->name);
    s_screen = new_screen(title, "OK  BACK");
    s_field = create_field(s_screen);

    lv_obj_t *image = create_species(s_field, species_id, true);
    lv_obj_set_pos(image, 9, 16);
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
    lv_obj_set_pos(tag, 132, 13);
    label_at(
        tag, record->state == CITY_DISCOVERY_CAPTURED ? "CAUGHT" : "SEEN",
        &lv_font_montserrat_14, 0xFFFFFF, 3, 5, 72);

    char count[32];
    snprintf(
        count, sizeof(count), "CAUGHT %lu",
        (unsigned long)record->capture_count);
    lv_obj_t *count_label = label_at(
        s_field, count, &lv_font_montserrat_14, COLOR_INK, 118, 53, 96);
    lv_obj_set_style_text_align(count_label, LV_TEXT_ALIGN_LEFT, 0);

    char place[40];
    if (record->last_place_id == UINT16_MAX) {
        snprintf(place, sizeof(place), "PLACE --");
    } else if (record->last_place_id == CITY_WILD_PLACE_ID) {
        snprintf(place, sizeof(place), "WILD");
    } else {
        snprintf(place, sizeof(place), "PLACE %02u", record->last_place_id);
    }
    lv_obj_t *place_label = label_at(
        s_field, place, &lv_font_montserrat_14, COLOR_MUTED, 118, 80, 96);
    lv_obj_set_style_text_align(place_label, LV_TEXT_ALIGN_LEFT, 0);

    if (record->state == CITY_DISCOVERY_CAPTURED) {
        char latest[40];
        char best[40];
        snprintf(
            latest, sizeof(latest), "LAST  HP%u AT%u DF%u",
            record->latest_stats.hp, record->latest_stats.attack,
            record->latest_stats.defense);
        snprintf(
            best, sizeof(best), "BEST  HP%u AT%u DF%u",
            record->best_stats.hp, record->best_stats.attack,
            record->best_stats.defense);
        lv_obj_t *latest_label = label_at(
            s_field, latest, &lv_font_montserrat_14,
            COLOR_INK, 10, 126, 200);
        lv_obj_set_style_text_align(latest_label, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_t *best_label = label_at(
            s_field, best, &lv_font_montserrat_14,
            COLOR_INK, 10, 146, 200);
        lv_obj_set_style_text_align(best_label, LV_TEXT_ALIGN_LEFT, 0);
    }
    label_at(
        s_screen,
        record->state == CITY_DISCOVERY_CAPTURED
            ? "Collection entry complete"
            : "Seen - capture to complete",
        &lv_font_montserrat_14, COLOR_MUTED, 10, META_Y, 220);
}

static void build_storage_error(void)
{
    s_screen = new_screen("SAVE FAILED", "OK  RETRY");
    s_field = create_field(s_screen);
    if (s_pending_write == WRITE_DISCOVERY || s_pending_write == WRITE_WILD) {
        lv_obj_t *beacon = lv_obj_create(s_field);
        style_plain(beacon, 0xF7FBF8);
        lv_obj_set_style_radius(beacon, 8, 0);
        lv_obj_set_size(beacon, 68, 76);
        lv_obj_set_pos(beacon, 76, 42);
        label_at(beacon, "?", &lv_font_montserrat_20,
                 COLOR_CORAL, 14, 23, 40);
        label_at(s_screen, "Discovery was not recorded",
                 &lv_font_montserrat_14, COLOR_CORAL, 10, META_Y, 220);
    } else {
        create_ball(s_field, 88, 69, 44);
        label_at(s_screen, "Capture was not recorded",
                 &lv_font_montserrat_14, COLOR_CORAL, 10, META_Y, 220);
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

    switch (s_state) {
    case UI_PASSPORT:
        build_passport();
        break;
    case UI_HOME:
        build_home();
        break;
    case UI_SCANNING:
        build_scanning();
        break;
    case UI_PLACE_PENDING:
        build_place_status(
            "NEW PLACE?", "VERIFYING", "Waiting for a second scan");
        break;
    case UI_PLACE_GRAY:
        build_place_status(
            "SIGNAL UNCLEAR", "OK  HOME", "Move a little and try again");
        break;
    case UI_PLACE_WILD:
        s_screen = new_screen("WILD MODE", "OK SEARCH / UP HOME");
        s_field = create_field(s_screen);
        s_wild_countdown = label_at(s_field, "", &lv_font_montserrat_20,
                                    COLOR_INK, 5, 25, 210);
        label_at(s_field, "One try every 30 minutes", &lv_font_montserrat_14,
                 COLOR_INK, 5, 70, 210);
        label_at(s_field, "Leaving uses the try", &lv_font_montserrat_14,
                 COLOR_MUTED, 5, 100, 210);
        label_at(s_screen, "Restart: wait up to 30m", &lv_font_montserrat_14,
                 COLOR_MUTED, 5, META_Y, 230);
        break;
    case UI_LOW_BATTERY:
        build_place_status("LOW BATTERY", "OK  HOME", "Charge before exploring");
        break;
    case UI_PLACE_UNSTABLE:
        build_place_status(
            "PLACE CHANGED", "OK  HOME", "Environment did not stabilize");
        break;
    case UI_PLACE_ERROR:
        build_place_status(
            "SCAN FAILED", "OK  RETRY", "Wi-Fi scan was not completed");
        break;
    case UI_PLACE_STORAGE_ERROR:
        build_place_status(
            "PLACE SAVE FAILED", "OK  RETRY", "New place was not recorded");
        break;
    case UI_PLACE_FULL:
        build_place_status(
            "PLACE MEMORY FULL", "OK  HOME", "No place record was overwritten");
        break;
    case UI_ENCOUNTER:
        build_encounter();
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
    s_state = state;
    s_state_started_ms = now_ms();
    s_pocket.last_activity_ms = s_state_started_ms;
    build_state();
    if (old != NULL) {
        lv_obj_delete(old);
    }
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

static void bestiary_write_task(void *argument)
{
    (void)argument;
    const write_operation_t operation = s_pending_write;
    const bool saved = operation == WRITE_WILD
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
        set_state((operation == WRITE_DISCOVERY || operation == WRITE_WILD)
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
            4096,
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
        ESP_LOGI(TAG, "CAPTURE_TIMEOUT budget_ms=%u",
                 CITY_GAME_CAPTURE_BUDGET_MS);
        set_state(UI_ESCAPED);
        return;
    }

    const uint64_t elapsed = now - s_round.started_ms;
    if (elapsed >= s_round.duration_ms) {
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
        inverse * inverse * 151 +
        2 * inverse * (int)permille * 45 +
        (int)permille * (int)permille * 61) / 1000000;
    int size = 60 - (int)((38U * permille) / 1000U);
    ball_geometry(center_x - size / 2, center_y - size / 2, size);
}

static void tick(lv_timer_t *timer)
{
    (void)timer;
    uint64_t now = now_ms();
    int soc;
    if (s_battery_queue && xQueueReceive(s_battery_queue, &soc, 0) == pdTRUE) {
        s_battery_soc = soc;
        char text[20];
        if (soc < 0) snprintf(text, sizeof(text), "BAT --");
        else snprintf(text, sizeof(text), "%s%d%%", city_battery_low(soc) ? "LOW " : "", soc);
        lv_label_set_text(s_battery_label, text);
        lv_obj_set_style_text_color(s_battery_label,
            lv_color_hex(city_battery_low(soc) ? COLOR_CORAL : COLOR_MUTED), 0);
        if (!s_pocket.screen_off) bsp_display_backlight(city_battery_low(soc) ? 30 : 100);
    }
    const bool busy = s_save_in_progress || s_state == UI_SCANNING ||
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
        lv_label_set_text_fmt(s_wild_countdown, seconds ? "Wait %02u:%02u" : "Ready to search", seconds / 60U, seconds % 60U);
        lv_label_set_text(s_status, seconds ? "UP  HOME" : "OK SEARCH / UP HOME");
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
    city_capture_result_t result = city_capture_throw(&s_round, now_ms());
    s_throw_hit = result == CITY_CAPTURE_HIT;
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
            bsp_display_backlight(city_battery_low(s_battery_soc) ? 30 : 100);
            lv_timer_set_period(s_tick, 33);
            ESP_LOGI(TAG, "SCREEN_WAKE");
        }
        bsp_lvgl_unlock();
        return;
    }
    if (s_pocket.screen_off || s_pocket.consume_gesture || s_save_in_progress) {
        bsp_lvgl_unlock(); return;
    }
    const bool handles_long_ok = event == BSP_BTN_LONG && button == BSP_BTN_OK &&
        (s_state == UI_AIM || s_state == UI_BESTIARY_LIST);
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
        } else {
            s_bestiary_selection = 0U;
            set_state(UI_HOME);
        }
        bsp_lvgl_unlock();
        return;
    }

    if (s_state == UI_HOME) {
        if (button == BSP_BTN_UP || button == BSP_BTN_DOWN) {
            s_home_selection = city_passport_turn_page(s_home_selection, 3U, button == BSP_BTN_DOWN);
            set_state(UI_HOME);
        } else if (button == BSP_BTN_OK) {
            if (s_home_selection == 0U) {
                begin_place_scan();
            } else if (s_home_selection == 1U) {
                s_bestiary_selection = 0U;
                set_state(UI_BESTIARY_LIST);
            } else {
                s_passport_page = 0U;
                set_state(UI_PASSPORT);
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
                if (record->state != CITY_DISCOVERY_UNKNOWN) {
                    set_state(UI_BESTIARY_DETAIL);
                }
            }
        }
        bsp_lvgl_unlock();
        return;
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
            start_capture_session();
        } else {
            abandon_encounter();
        }
        break;
    case UI_AIM:
        throw_ball();
        break;
    case UI_CAPTURED:
        s_bestiary_selection =
            species_selection_index(s_current_species_id);
        set_state(UI_BESTIARY_DETAIL);
        break;
    case UI_ESCAPED:
    case UI_ABANDONED:
        set_state(UI_HOME);
        break;
    case UI_BESTIARY_DETAIL:
        s_bestiary_selection = 0U;
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

    ESP_ERROR_CHECK(bsp_i2c_init());
    s_battery_queue = xQueueCreate(1, sizeof(int));
    if (s_battery_queue && xTaskCreate(battery_task, "battery", 3072, NULL, 2, NULL) != pdPASS) {
        ESP_LOGW(TAG, "Battery worker unavailable");
    }
    ESP_ERROR_CHECK(bsp_display_init());
    if (bsp_lvgl_init() == NULL) {
        ESP_LOGE(TAG, "LVGL init failed");
        return;
    }
    bsp_display_backlight(100);

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
        bsp_lvgl_unlock();
    }

    ESP_ERROR_CHECK(bsp_button_init(on_button, NULL));
    ESP_LOGI(
        TAG,
        "READY display=1 buttons=1 capture_count=%lu place_data=%u",
        (unsigned long)total_capture_count(),
        s_place_data_ready ? 1U : 0U);
}
