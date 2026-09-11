#include "bsp_bestiary_store.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "capture_engine.h"
#include "charmander_sprite.h"

#include "esp_log.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
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

typedef enum {
    UI_HOME = 0,
    UI_SCANNING,
    UI_ENCOUNTER,
    UI_AIM,
    UI_THROWING,
    UI_CATCHING,
    UI_CAPTURED,
    UI_ESCAPED,
    UI_BESTIARY_LIST,
    UI_BESTIARY_DETAIL,
    UI_STORAGE_ERROR,
} ui_state_t;

typedef enum {
    WRITE_NONE = 0,
    WRITE_DISCOVERY,
    WRITE_CAPTURE,
} write_operation_t;

static const char *TAG = "pokedex";

static ui_state_t s_state;
static uint64_t s_state_started_ms;
static uint8_t s_attempts = 3;
static city_bestiary_t s_bestiary;
static bool s_bestiary_ready;
static bool s_save_in_progress;
static uint8_t s_home_selection;
static uint8_t s_bestiary_selection;
static write_operation_t s_pending_write;
static uint64_t s_encounter_id;
static bool s_throw_hit;
static city_capture_round_t s_round;

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

static const char *state_name(ui_state_t state)
{
    static const char *const names[] = {
        "home", "scanning", "encounter", "aim", "throwing",
        "catching", "captured", "escaped", "bestiary_list",
        "bestiary_detail", "storage_error",
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
    lv_obj_set_pos(label, x, y);
    return label;
}

static lv_obj_t *new_screen(const char *title, const char *action)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    style_plain(screen, COLOR_PAPER);

    lv_obj_t *header = lv_obj_create(screen);
    style_plain(header, 0xF7FBF8);
    lv_obj_set_size(header, 240, 34);
    lv_obj_set_pos(header, 0, 0);
    label_at(header, "Pokedex", &lv_font_montserrat_14, 0x247052, 12, 9, 90);
    lv_obj_t *battery = label_at(
        header, "USB", &lv_font_montserrat_14, COLOR_MUTED, 184, 9, 42);
    lv_obj_set_style_text_align(battery, LV_TEXT_ALIGN_RIGHT, 0);

    label_at(screen, title, &lv_font_montserrat_20, COLOR_INK, 10, 42, 220);

    lv_obj_t *footer = lv_obj_create(screen);
    style_plain(footer, 0xF7FBF8);
    lv_obj_set_size(footer, 240, 36);
    lv_obj_set_pos(footer, 0, 284);
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
    lv_obj_set_size(field, 220, 170);
    lv_obj_set_pos(field, 10, 73);

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

static lv_obj_t *create_charmander(lv_obj_t *parent, bool small)
{
    lv_obj_t *image = lv_image_create(parent);
    lv_image_set_src(
        image, small ? &charmander_small : &charmander_large);
    lv_obj_set_pos(image, small ? 69 : 55, small ? 12 : 26);
    return image;
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
    lv_obj_set_size(row, 220, 62);
    lv_obj_set_pos(row, 10, y);

    lv_obj_t *title_label = label_at(
        row, title, &lv_font_montserrat_20, COLOR_INK, 14, 8, 160);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_t *subtitle_label = label_at(
        row, subtitle, &lv_font_montserrat_14, COLOR_MUTED, 14, 37, 178);
    lv_obj_set_style_text_align(subtitle_label, LV_TEXT_ALIGN_LEFT, 0);
    label_at(
        row, selected ? ">" : "", &lv_font_montserrat_20,
        selected ? COLOR_CORAL : COLOR_MUTED, 188, 19, 20);
    return row;
}

static void build_home(void)
{
    s_screen = new_screen("CITY SPIRITS", "UP/DN SELECT  OK OPEN");
    const city_discovery_state_t discovery =
        s_bestiary.charmander.state;
    const char *progress = discovery == CITY_DISCOVERY_UNKNOWN
                               ? "0 SEEN  0 CAUGHT"
                               : discovery == CITY_DISCOVERY_SEEN
                                     ? "1 SEEN  0 CAUGHT"
                                     : "1 SEEN  1 CAUGHT";

    create_menu_row(
        s_screen, 79, "EXPLORE", "Find a nearby spirit",
        s_home_selection == 0U);
    create_menu_row(
        s_screen, 151, "BESTIARY", progress,
        s_home_selection == 1U);
    label_at(
        s_screen, "Offline collection", &lv_font_montserrat_14,
        COLOR_MUTED, 10, 251, 220);
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

static void build_encounter(void)
{
    s_screen = new_screen("WILD CHARMANDER", "OK  CATCH");
    s_field = create_field(s_screen);
    create_charmander(s_field, false);
    label_at(s_screen, "No.004  FIRE", &lv_font_montserrat_14,
             COLOR_MUTED, 10, 251, 220);
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
    s_screen = new_screen("AIM CHARMANDER", "OK  THROW");
    s_field = create_field(s_screen);
    create_charmander(s_field, true);

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
             COLOR_MUTED, 10, 251, 220);
}

static void build_throwing(void)
{
    s_screen = new_screen("FIRST PERSON THROW", "THROWING");
    s_field = create_field(s_screen);
    create_charmander(s_field, true);
    create_ball(s_field, 80, 118, 60);
    label_at(s_screen, "Ball in flight", &lv_font_montserrat_14,
             COLOR_MUTED, 10, 251, 220);
}

static void build_catching(void)
{
    s_screen = new_screen("CAPTURING...", "PLEASE WAIT");
    s_field = create_field(s_screen);
    create_ball(s_field, 88, 72, 44);
    label_at(s_screen, "One... Two... Three...", &lv_font_montserrat_14,
             COLOR_MUTED, 10, 251, 220);
}

static void build_captured(void)
{
    s_screen = new_screen("GOTCHA!", "OK  VIEW POKEDEX");
    s_field = create_field(s_screen);
    create_ball(s_field, 88, 69, 44);

    label_at(s_field, "*", &lv_font_montserrat_20, COLOR_YELLOW, 49, 46, 24);
    label_at(s_field, "*", &lv_font_montserrat_20, COLOR_YELLOW, 149, 57, 24);
    label_at(s_screen, "Charmander was caught", &lv_font_montserrat_14,
             COLOR_MUTED, 10, 251, 220);
}

static void build_escaped(void)
{
    s_screen = new_screen("IT GOT AWAY", "OK  HOME");
    s_field = create_field(s_screen);
    create_charmander(s_field, false);
    label_at(s_screen, "Adjust your timing", &lv_font_montserrat_14,
             COLOR_MUTED, 10, 251, 220);
}

static void build_bestiary_list(void)
{
    const city_creature_record_t *record = &s_bestiary.charmander;
    if (record->state == CITY_DISCOVERY_UNKNOWN) {
        s_screen = new_screen("BESTIARY", "OK  BACK");
        lv_obj_t *empty = lv_obj_create(s_screen);
        style_plain(empty, 0xF7FBF8);
        lv_obj_set_style_radius(empty, 8, 0);
        lv_obj_set_size(empty, 220, 146);
        lv_obj_set_pos(empty, 10, 76);
        label_at(empty, "?", &lv_font_montserrat_20, COLOR_CORAL, 90, 22, 40);
        label_at(empty, "NO ENTRIES YET", &lv_font_montserrat_20,
                 COLOR_INK, 10, 59, 200);
        label_at(empty, "Explore to discover one", &lv_font_montserrat_14,
                 COLOR_MUTED, 10, 94, 200);
        label_at(s_screen, "Discovered 0 / 1", &lv_font_montserrat_14,
                 COLOR_MUTED, 10, 251, 220);
        return;
    }

    s_screen = new_screen("BESTIARY", "UP/DN SELECT  OK OPEN");
    lv_obj_t *row = lv_obj_create(s_screen);
    style_plain(row, s_bestiary_selection == 0U ? 0xE4F4E8 : 0xF7FBF8);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_border_width(row, s_bestiary_selection == 0U ? 2 : 1, 0);
    lv_obj_set_style_border_color(
        row, lv_color_hex(s_bestiary_selection == 0U
                              ? COLOR_GREEN
                              : 0xD5E0DD), 0);
    lv_obj_set_size(row, 220, 96);
    lv_obj_set_pos(row, 10, 74);

    lv_obj_t *image = create_charmander(row, true);
    lv_obj_set_pos(image, 8, 8);
    if (record->state == CITY_DISCOVERY_SEEN) {
        lv_obj_set_style_image_recolor(
            image, lv_color_hex(COLOR_MUTED), 0);
        lv_obj_set_style_image_recolor_opa(image, LV_OPA_60, 0);
    }
    lv_obj_t *name = label_at(
        row, "No.004 CHARMANDER", &lv_font_montserrat_14,
        COLOR_INK, 91, 15, 118);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_t *status = label_at(
        row, record->state == CITY_DISCOVERY_CAPTURED ? "CAUGHT" : "SEEN",
        &lv_font_montserrat_14,
        record->state == CITY_DISCOVERY_CAPTURED ? COLOR_CORAL : COLOR_GREEN,
        91, 43, 104);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_LEFT, 0);

    char count[24];
    snprintf(
        count, sizeof(count), "COUNT %lu",
        (unsigned long)record->capture_count);
    lv_obj_t *count_label = label_at(
        row, count, &lv_font_montserrat_14, COLOR_MUTED, 91, 67, 104);
    lv_obj_set_style_text_align(count_label, LV_TEXT_ALIGN_LEFT, 0);

    create_menu_row(
        s_screen, 181, "BACK", "Return to home",
        s_bestiary_selection == 1U);
    label_at(s_screen, "Discovered 1 / 1", &lv_font_montserrat_14,
             COLOR_MUTED, 10, 251, 220);
}

static void build_bestiary_detail(void)
{
    const city_creature_record_t *record = &s_bestiary.charmander;
    const city_species_definition_t *definition =
        city_species_definition(CITY_SPECIES_CHARMANDER);
    s_screen = new_screen("No.004 CHARMANDER", "OK  BACK");
    s_field = create_field(s_screen);

    lv_obj_t *image = create_charmander(s_field, true);
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

    char count[24];
    snprintf(
        count, sizeof(count), "CAUGHT %lu",
        (unsigned long)record->capture_count);
    lv_obj_t *count_label = label_at(
        s_field, count, &lv_font_montserrat_14, COLOR_INK, 118, 53, 96);
    lv_obj_set_style_text_align(count_label, LV_TEXT_ALIGN_LEFT, 0);

    char place[24];
    if (record->last_place_id == UINT16_MAX) {
        snprintf(place, sizeof(place), "PLACE --");
    } else {
        snprintf(place, sizeof(place), "PLACE %02u", record->last_place_id);
    }
    lv_obj_t *place_label = label_at(
        s_field, place, &lv_font_montserrat_14, COLOR_MUTED, 118, 80, 96);
    lv_obj_set_style_text_align(place_label, LV_TEXT_ALIGN_LEFT, 0);

    lv_obj_t *description = label_at(
        s_field, definition->description, &lv_font_montserrat_14,
        COLOR_INK, 10, 126, 200);
    lv_label_set_long_mode(description, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(description, 38);
    lv_obj_set_style_text_align(description, LV_TEXT_ALIGN_LEFT, 0);
    label_at(
        s_screen,
        record->state == CITY_DISCOVERY_CAPTURED
            ? "Collection entry complete"
            : "Seen - capture to complete",
        &lv_font_montserrat_14, COLOR_MUTED, 10, 251, 220);
}

static void build_storage_error(void)
{
    s_screen = new_screen("SAVE FAILED", "OK  RETRY");
    s_field = create_field(s_screen);
    if (s_pending_write == WRITE_DISCOVERY) {
        lv_obj_t *beacon = lv_obj_create(s_field);
        style_plain(beacon, 0xF7FBF8);
        lv_obj_set_style_radius(beacon, 8, 0);
        lv_obj_set_size(beacon, 68, 76);
        lv_obj_set_pos(beacon, 76, 42);
        label_at(beacon, "?", &lv_font_montserrat_20,
                 COLOR_CORAL, 14, 23, 40);
        label_at(s_screen, "Discovery was not recorded",
                 &lv_font_montserrat_14, COLOR_CORAL, 10, 251, 220);
    } else {
        create_ball(s_field, 88, 69, 44);
        label_at(s_screen, "Capture was not recorded",
                 &lv_font_montserrat_14, COLOR_CORAL, 10, 251, 220);
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

    switch (s_state) {
    case UI_HOME:
        build_home();
        break;
    case UI_SCANNING:
        build_scanning();
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
        (unsigned long)s_bestiary.charmander.capture_count);
}

static void set_state(ui_state_t state)
{
    lv_obj_t *old = s_screen;
    s_state = state;
    s_state_started_ms = now_ms();
    build_state();
    if (old != NULL) {
        lv_obj_delete(old);
    }
}

static void start_capture_round(void)
{
    city_capture_begin(&s_round, esp_random(), now_ms());
    set_state(UI_AIM);
}

static uint64_t new_encounter_id(void)
{
    uint64_t encounter_id = ((uint64_t)esp_random() << 32U) |
                            (uint32_t)esp_timer_get_time();
    return encounter_id == 0U ? 1U : encounter_id;
}

static bool persist_discovery(void)
{
    if (!s_bestiary_ready) {
        ESP_LOGE(TAG, "Bestiary unavailable; discovery not persisted");
        return false;
    }

    const city_bestiary_result_t result = city_bestiary_mark_seen(
        &s_bestiary,
        CITY_SPECIES_CHARMANDER,
        bsp_bestiary_store_persist,
        (void *)&BSP_BESTIARY_STORE_DEFAULT);
    if (result != CITY_BESTIARY_APPLIED &&
        result != CITY_BESTIARY_UNCHANGED) {
        ESP_LOGE(TAG, "Discovery commit rejected: result=%d", (int)result);
        return false;
    }
    ESP_LOGI(
        TAG, "DISCOVERY_COMMITTED species=004 result=%s",
        result == CITY_BESTIARY_APPLIED ? "applied" : "unchanged");
    return true;
}

static bool persist_capture(void)
{
    if (!s_bestiary_ready || s_encounter_id == 0U) {
        ESP_LOGE(TAG, "Bestiary unavailable; capture not persisted");
        return false;
    }

    const city_bestiary_result_t result = city_bestiary_capture(
        &s_bestiary,
        s_encounter_id,
        CITY_SPECIES_CHARMANDER,
        1U,
        bsp_bestiary_store_persist,
        (void *)&BSP_BESTIARY_STORE_DEFAULT);
    if (result != CITY_BESTIARY_APPLIED &&
        result != CITY_BESTIARY_DUPLICATE) {
        ESP_LOGE(TAG, "Capture commit rejected: result=%d", (int)result);
        return false;
    }

    ESP_LOGI(
        TAG,
        "CAPTURE_COMMITTED species=004 count=%lu encounter=%016llx result=%s",
        (unsigned long)s_bestiary.charmander.capture_count,
        (unsigned long long)s_encounter_id,
        result == CITY_BESTIARY_APPLIED ? "applied" : "duplicate");
    return true;
}

static void bestiary_write_task(void *argument)
{
    (void)argument;
    const write_operation_t operation = s_pending_write;
    const bool saved = operation == WRITE_DISCOVERY
                           ? persist_discovery()
                           : operation == WRITE_CAPTURE
                                 ? persist_capture()
                                 : false;

    while (!bsp_lvgl_lock(1000)) {
        ESP_LOGW(TAG, "Waiting to publish bestiary result");
    }
    s_save_in_progress = false;
    if (saved) {
        s_pending_write = WRITE_NONE;
        set_state(operation == WRITE_DISCOVERY
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
    ESP_LOGI(
        TAG,
        "BESTIARY_READY schema=%u count=%lu ledger=%u migrated=%u",
        s_bestiary.schema_version,
        (unsigned long)s_bestiary.charmander.capture_count,
        s_bestiary.ledger_count,
        migrated ? 1U : 0U);
}

static void update_aim(uint64_t now)
{
    uint64_t elapsed = now - s_round.started_ms;
    if (elapsed >= s_round.duration_ms) {
        city_capture_begin(&s_round, esp_random(), now);
        position_capture_target();
        elapsed = 0;
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

static void finish_throw(void)
{
    if (s_throw_hit) {
        set_state(UI_CATCHING);
        return;
    }

    s_attempts -= 1U;
    if (s_attempts == 0U) {
        set_state(UI_ESCAPED);
    } else {
        start_capture_round();
    }
}

static void update_throw(uint64_t now)
{
    uint64_t elapsed = now - s_state_started_ms;
    if (elapsed >= THROW_MS) {
        finish_throw();
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

    if (s_state == UI_SCANNING &&
        now - s_state_started_ms >= 900U &&
        !s_save_in_progress) {
        s_attempts = 3U;
        s_encounter_id = new_encounter_id();
        if (!request_bestiary_write(WRITE_DISCOVERY)) {
            set_state(UI_STORAGE_ERROR);
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
    if (event != BSP_BTN_CLICK) {
        return;
    }
    if (!bsp_lvgl_lock(500)) {
        ESP_LOGE(TAG, "button dropped: LVGL lock timeout");
        return;
    }

    ESP_LOGI(TAG, "BUTTON key=%d state=%s",
             (int)button, state_name(s_state));
    if (s_state == UI_HOME) {
        if (button == BSP_BTN_UP || button == BSP_BTN_DOWN) {
            s_home_selection = s_home_selection == 0U ? 1U : 0U;
            set_state(UI_HOME);
        } else if (button == BSP_BTN_OK) {
            if (s_home_selection == 0U) {
                set_state(UI_SCANNING);
            } else {
                s_bestiary_selection = 0U;
                set_state(UI_BESTIARY_LIST);
            }
        }
        bsp_lvgl_unlock();
        return;
    }

    if (s_state == UI_BESTIARY_LIST) {
        if (s_bestiary.charmander.state == CITY_DISCOVERY_UNKNOWN) {
            if (button == BSP_BTN_OK) {
                set_state(UI_HOME);
            }
        } else if (button == BSP_BTN_UP || button == BSP_BTN_DOWN) {
            s_bestiary_selection =
                s_bestiary_selection == 0U ? 1U : 0U;
            set_state(UI_BESTIARY_LIST);
        } else if (button == BSP_BTN_OK) {
            set_state(s_bestiary_selection == 0U
                          ? UI_BESTIARY_DETAIL
                          : UI_HOME);
        }
        bsp_lvgl_unlock();
        return;
    }

    if (button != BSP_BTN_OK) {
        bsp_lvgl_unlock();
        return;
    }

    switch (s_state) {
    case UI_ENCOUNTER:
        start_capture_round();
        break;
    case UI_AIM:
        throw_ball();
        break;
    case UI_CAPTURED:
        set_state(UI_BESTIARY_DETAIL);
        break;
    case UI_ESCAPED:
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

void app_main(void)
{
    ESP_LOGI(TAG, "Pokedex AI Passport boot");
    ESP_LOGI(TAG, "wake_cause=%d", (int)esp_sleep_get_wakeup_cause());

    city_bestiary_init(&s_bestiary);
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(nvs_err));
    } else {
        load_bestiary();
    }

    ESP_ERROR_CHECK(bsp_i2c_init());
    ESP_ERROR_CHECK(bsp_display_init());
    if (bsp_lvgl_init() == NULL) {
        ESP_LOGE(TAG, "LVGL init failed");
        return;
    }
    bsp_display_backlight(100);
    ESP_ERROR_CHECK(bsp_button_init(on_button, NULL));

    if (bsp_lvgl_lock(1000)) {
        s_state = UI_HOME;
        s_state_started_ms = now_ms();
        build_state();
        s_tick = lv_timer_create(tick, 33, NULL);
        bsp_lvgl_unlock();
    }

    ESP_LOGI(TAG, "READY display=1 buttons=1 capture_count=%lu",
             (unsigned long)s_bestiary.charmander.capture_count);
}
