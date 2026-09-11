#include "bsp_settings_store.h"
#include "nvs.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static uint8_t saved[CITY_SETTINGS_BYTES], staged[CITY_SETTINGS_BYTES];
static bool exists, fail_open, fail_set, fail_commit, corrupt_read;
static unsigned commits, opened, closed;
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{
    assert(strcmp(name, "preferences") == 0);
    assert(mode == NVS_READONLY || mode == NVS_READWRITE);
    if (fail_open) return ESP_FAIL;
    *handle = 1; ++opened; return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { assert(handle == 1); ++closed; }
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *length)
{
    assert(handle == 1 && strcmp(key, "settings_v1") == 0);
    if (!exists) return ESP_ERR_NVS_NOT_FOUND;
    assert(*length >= sizeof(saved));
    memcpy(out, saved, sizeof(saved)); *length = sizeof(saved);
    if (corrupt_read) ((uint8_t *)out)[6] ^= 1;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t length)
{
    assert(handle == 1 && strcmp(key, "settings_v1") == 0 && length == sizeof(staged));
    if (fail_set) return ESP_FAIL;
    memcpy(staged, data, length); return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle == 1);
    if (fail_commit) return ESP_FAIL;
    ++commits; memcpy(saved, staged, sizeof(saved)); exists = true; return ESP_OK;
}
int main(void)
{
    city_settings_t s, defaults = city_settings_defaults();
    assert(defaults.volume == 60 && defaults.brightness == 60 && !defaults.muted);
    assert(bsp_settings_load(&s) == ESP_OK && city_settings_equal(&s, &defaults) && !commits);
    s = city_settings_adjust(s, false, INT_MIN); assert(s.volume == 0);
    s = city_settings_adjust(s, false, INT_MAX); assert(s.volume == 100);
    s = city_settings_adjust(s, true, INT_MIN); assert(s.brightness == 10);
    s = city_settings_adjust(s, true, INT_MAX); assert(s.brightness == 100);
    s = city_settings_toggle_mute(s); assert(s.muted && s.volume == 100);
    assert(city_settings_backlight(&s, false, false) == 100);
    assert(city_settings_backlight(&s, false, true) == 30);
    assert(city_settings_backlight(&s, true, false) == 0);
    s.brightness = 10; assert(city_settings_backlight(&s, false, true) == 10);
    assert(bsp_settings_save(&s) == ESP_OK && commits == 1);
    city_settings_t loaded; assert(bsp_settings_load(&loaded) == ESP_OK && city_settings_equal(&s, &loaded));
    uint8_t bytes[CITY_SETTINGS_BYTES]; assert(city_settings_encode(&s, bytes));
    for (unsigned i = 0; i < sizeof(bytes); ++i) {
        bytes[i] ^= 1; loaded = defaults;
        assert(!city_settings_decode(bytes, sizeof(bytes), &loaded) && city_settings_equal(&loaded, &defaults));
        bytes[i] ^= 1;
    }
    assert(!city_settings_decode(bytes, sizeof(bytes)-1, &loaded));
    s.brightness = 0; assert(!city_settings_encode(&s, bytes));
    assert(bsp_settings_save(&s) == ESP_ERR_INVALID_ARG);
    s = city_settings_adjust(defaults, false, -30);
    fail_set = true; assert(bsp_settings_save(&s) == ESP_FAIL && commits == 1); fail_set = false;
    fail_commit = true; assert(bsp_settings_save(&s) == ESP_FAIL && commits == 1); fail_commit = false;
    assert(bsp_settings_load(&loaded) == ESP_OK && loaded.muted && loaded.volume == 100);
    corrupt_read = true;
    assert(bsp_settings_load(&loaded) == ESP_ERR_INVALID_STATE && city_settings_equal(&loaded, &defaults));
    assert(bsp_settings_save(&s) == ESP_ERR_INVALID_STATE); corrupt_read = false;
    assert(bsp_settings_load(&loaded) == ESP_OK && city_settings_equal(&loaded, &s));
    fail_open = true;
    assert(bsp_settings_load(&loaded) == ESP_FAIL && city_settings_equal(&loaded, &defaults));
    assert(bsp_settings_save(&s) == ESP_FAIL);
    assert(opened == closed);
    puts("Settings defaults, boundaries, mute, brightness policy, persistence and failure handling passed");
}
