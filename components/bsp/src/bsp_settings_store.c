#include "bsp_settings_store.h"
#include "nvs.h"
#include <string.h>

static const char *const settings_namespace = "preferences";
static const char *const settings_key = "settings_v1";

esp_err_t bsp_settings_load(city_settings_t *settings)
{
    if (!settings) return ESP_ERR_INVALID_ARG;
    *settings = city_settings_defaults();
    nvs_handle_t handle;
    esp_err_t err = nvs_open(settings_namespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    uint8_t bytes[CITY_SETTINGS_BYTES];
    size_t length = sizeof(bytes);
    err = nvs_get_blob(handle, settings_key, bytes, &length);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err == ESP_OK && !city_settings_decode(bytes, length, settings)) return ESP_ERR_INVALID_STATE;
    return err;
}

esp_err_t bsp_settings_save(const city_settings_t *settings)
{
    uint8_t bytes[CITY_SETTINGS_BYTES], actual[CITY_SETTINGS_BYTES];
    if (!city_settings_encode(settings, bytes)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(settings_namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, settings_key, bytes, sizeof(bytes));
    if (err == ESP_OK) err = nvs_commit(handle);
    size_t length = sizeof(actual);
    if (err == ESP_OK) err = nvs_get_blob(handle, settings_key, actual, &length);
    if (err == ESP_OK && (length != sizeof(actual) || memcmp(bytes, actual, sizeof(actual))))
        err = ESP_ERR_INVALID_STATE;
    nvs_close(handle);
    return err;
}
