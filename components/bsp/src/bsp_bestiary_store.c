#include "bsp_bestiary_store.h"

#include "esp_log.h"
#include "nvs.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const char *TAG = "bsp_bestiary";

const bsp_bestiary_store_t BSP_BESTIARY_STORE_DEFAULT = {
    .namespace_name = "pokedex",
    .blob_key = "bestiary_004",
    .legacy_count_key = "caught_004",
};

static bool store_is_valid(const bsp_bestiary_store_t *store)
{
    return store != NULL && store->namespace_name != NULL &&
           store->blob_key != NULL && store->legacy_count_key != NULL &&
           strcmp(store->blob_key, store->legacy_count_key) != 0;
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

esp_err_t bsp_bestiary_store_load(
    const bsp_bestiary_store_t *store,
    city_bestiary_t *bestiary,
    bool *migrated)
{
    if (!store_is_valid(store) || bestiary == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (migrated != NULL) {
        *migrated = false;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(
        store->namespace_name, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t length = 0U;
    err = nvs_get_blob(handle, store->blob_key, NULL, &length);
    if (err == ESP_OK) {
        if (length != CITY_BESTIARY_ENCODED_BYTES) {
            nvs_close(handle);
            return ESP_ERR_INVALID_SIZE;
        }

        uint8_t encoded[CITY_BESTIARY_ENCODED_BYTES];
        err = nvs_get_blob(handle, store->blob_key, encoded, &length);
        nvs_close(handle);
        if (err != ESP_OK) {
            return err;
        }
        if (!city_bestiary_decode(encoded, length, bestiary)) {
            return ESP_ERR_INVALID_STATE;
        }
        return ESP_OK;
    }
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }

    uint32_t legacy_count = 0U;
    err = nvs_get_u32(
        handle, store->legacy_count_key, &legacy_count);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        city_bestiary_init(bestiary);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    city_bestiary_t imported;
    if (!city_bestiary_import_legacy_count(
            &imported, legacy_count)) {
        nvs_close(handle);
        return ESP_ERR_INVALID_STATE;
    }
    err = encode_and_stage(handle, store->blob_key, &imported);
    if (err == ESP_OK) {
        err = nvs_erase_key(handle, store->legacy_count_key);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        return err;
    }

    *bestiary = imported;
    if (migrated != NULL) {
        *migrated = true;
    }
    ESP_LOGI(
        TAG, "Migrated legacy capture count=%lu",
        (unsigned long)legacy_count);
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
