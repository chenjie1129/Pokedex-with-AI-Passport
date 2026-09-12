#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CITY_SETTINGS_BYTES 12U
#define CITY_SETTINGS_DEFAULT_PERCENT 60U

typedef enum {
    CITY_LANGUAGE_ENGLISH = 0,
    CITY_LANGUAGE_SIMPLIFIED_CHINESE = 1,
} city_language_t;

typedef struct {
    uint8_t volume;
    uint8_t brightness;
    bool muted;
    city_language_t language;
} city_settings_t;

city_settings_t city_settings_defaults(void);
bool city_settings_valid(const city_settings_t *settings);
bool city_settings_equal(const city_settings_t *a, const city_settings_t *b);
city_settings_t city_settings_adjust(city_settings_t settings, bool brightness, int delta);
city_settings_t city_settings_toggle_mute(city_settings_t settings);
city_settings_t city_settings_toggle_language(city_settings_t settings);
uint8_t city_settings_backlight(const city_settings_t *settings, bool screen_off, bool low_battery);
bool city_settings_encode(const city_settings_t *settings, uint8_t bytes[CITY_SETTINGS_BYTES]);
bool city_settings_decode(const uint8_t *bytes, size_t length, city_settings_t *settings);
