#include "bsp_bestiary_store.h"

#include "esp_log.h"
#include "nvs.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const char *TAG = "bsp_bestiary";

const bsp_bestiary_store_t BSP_BESTIARY_STORE_DEFAULT = {
    .namespace_name = "pokedex",
    .blob_key = "bestiary_v6",
    .legacy_blob_key = "bestiary_004",
    .legacy_count_key = "caught_004",
};

static bool store_is_valid(const bsp_bestiary_store_t *store)
{
    return store != NULL && store->namespace_name != NULL &&
           store->blob_key != NULL && store->legacy_count_key != NULL &&
           strcmp(store->blob_key, store->legacy_count_key) != 0 &&
           (!store->legacy_blob_key || strcmp(store->blob_key, store->legacy_blob_key) != 0);
}

static esp_err_t encode_and_stage(
    nvs_handle_t handle,
    const char *key,
    const city_bestiary_t *bestiary)
{
    uint8_t encoded[CITY_BESTIARY_ENCODED_BYTES];
    if (!city_bestiary_encode(bestiary, encoded)) {
        return ESP_ERR_INVALID_ARG;
    }
    return nvs_set_blob(handle, key, encoded, sizeof(encoded));
}

static esp_err_t read_model(nvs_handle_t handle, const char *key, city_bestiary_t *model, bool *outdated)
{
    size_t length = 0;
    esp_err_t err = nvs_get_blob(handle, key, NULL, &length);
    if (err != ESP_OK) return err;
    if (length > CITY_BESTIARY_ENCODED_BYTES || length < 12) return ESP_ERR_INVALID_SIZE;
    uint8_t bytes[CITY_BESTIARY_ENCODED_BYTES];
    err = nvs_get_blob(handle, key, bytes, &length);
    if (err != ESP_OK) return err;
    if (!city_bestiary_decode(bytes, length, model)) return ESP_ERR_INVALID_STATE;
    if (outdated) *outdated = ((uint16_t)bytes[4] | ((uint16_t)bytes[5] << 8)) != CITY_BESTIARY_SCHEMA_VERSION;
    return ESP_OK;
}

esp_err_t bsp_bestiary_store_load(
    const bsp_bestiary_store_t *store, city_bestiary_t *bestiary, bool *migrated)
{
    if (!store_is_valid(store) || !bestiary) return ESP_ERR_INVALID_ARG;
    if (migrated) *migrated = false;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(store->namespace_name, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    city_bestiary_t next;
    bool outdated = false;
    err = read_model(handle, store->blob_key, &next, &outdated);
    if (err == ESP_OK) {
        if (outdated) goto save_upgrade;
        nvs_close(handle); *bestiary = next; return ESP_OK;
    }
    // Never hide a corrupt/newer save by falling back to stale legacy progress.
    if (err != ESP_ERR_NVS_NOT_FOUND) { nvs_close(handle); return err; }
    if (store->legacy_blob_key)
        err = read_model(handle, store->legacy_blob_key, &next, NULL);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        uint32_t count = 0;
        err = nvs_get_u32(handle, store->legacy_count_key, &count);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            nvs_close(handle); city_bestiary_init(bestiary); return ESP_OK;
        }
        if (err == ESP_OK && !city_bestiary_import_legacy_count(&next, count)) err = ESP_ERR_INVALID_STATE;
    }
    if (err != ESP_OK) { nvs_close(handle); return err; }
save_upgrade:
    err = encode_and_stage(handle, store->blob_key, &next);
    if (err == ESP_OK) err = nvs_commit(handle);
    if (err == ESP_OK) {
        city_bestiary_t readback;
        err = read_model(handle, store->blob_key, &readback, NULL);
        if (err == ESP_OK) {
            uint8_t expected[CITY_BESTIARY_ENCODED_BYTES], actual[CITY_BESTIARY_ENCODED_BYTES];
            if (!city_bestiary_encode(&next, expected) || !city_bestiary_encode(&readback, actual) ||
                memcmp(expected, actual, sizeof(expected)) != 0) err = ESP_ERR_INVALID_STATE;
        }
    }
    nvs_close(handle);
    if (err != ESP_OK) return err;
    *bestiary = next;
    if (migrated) *migrated = true;
    ESP_LOGI(TAG, "Migrated bestiary to schema=%u species=%u",
             CITY_BESTIARY_SCHEMA_VERSION, CITY_SPECIES_COUNT);
    return ESP_OK;
}

bool bsp_bestiary_store_persist(
    const city_bestiary_t *next,
    void *context)
{
    const bsp_bestiary_store_t *store = context;
    if (!store_is_valid(store) || next == NULL) {
        return false;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(
        store->namespace_name, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return false;
    }

    err = encode_and_stage(handle, store->blob_key, next);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Persist failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}
