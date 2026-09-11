#include "bsp_place_store.h"

#include "nvs.h"
#include "place_profile_codec.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

const bsp_place_store_t BSP_PLACE_STORE_DEFAULT = {
    .namespace_name = "city_places",
    .blob_key = "catalog_v1",
};

static bool store_is_valid(const bsp_place_store_t *store)
{
    return store != NULL && store->namespace_name != NULL &&
           store->namespace_name[0] != '\0' && store->blob_key != NULL &&
           store->blob_key[0] != '\0';
}

esp_err_t bsp_place_store_load(
    const bsp_place_store_t *store,
    city_place_catalog_t *catalog,
    bool *found)
{
    if (!store_is_valid(store) || catalog == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (found != NULL) {
        *found = false;
    }

    nvs_handle_t handle;
    esp_err_t err =
        nvs_open(store->namespace_name, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        memset(catalog, 0, sizeof(*catalog));
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    size_t length = 0U;
    err = nvs_get_blob(handle, store->blob_key, NULL, &length);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        memset(catalog, 0, sizeof(*catalog));
        return ESP_OK;
    }
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }
    if (length < CITY_PLACE_CATALOG_HEADER_BYTES +
                     CITY_PLACE_CATALOG_CHECKSUM_BYTES ||
        length > CITY_PLACE_CATALOG_MAX_ENCODED_BYTES) {
        nvs_close(handle);
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t *encoded = malloc(length);
    if (encoded == NULL) {
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }

    err = nvs_get_blob(handle, store->blob_key, encoded, &length);
    nvs_close(handle);
    if (err == ESP_OK &&
        !city_place_catalog_decode(encoded, length, catalog)) {
        err = ESP_ERR_INVALID_STATE;
    }
    free(encoded);
    if (err == ESP_OK && found != NULL) {
        *found = true;
    }
    return err;
}

esp_err_t bsp_place_store_save(
    const bsp_place_store_t *store,
    const city_place_catalog_t *catalog)
{
    if (!store_is_valid(store) || catalog == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t length = city_place_catalog_encoded_size(catalog);
    if (length == 0U || length > CITY_PLACE_CATALOG_MAX_ENCODED_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t *encoded = malloc(length);
    if (encoded == NULL) {
        return ESP_ERR_NO_MEM;
    }
    size_t written = 0U;
    if (!city_place_catalog_encode(
            catalog, encoded, length, &written) ||
        written != length) {
        free(encoded);
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err =
        nvs_open(store->namespace_name, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        free(encoded);
        return err;
    }

    err = nvs_set_blob(handle, store->blob_key, encoded, written);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    free(encoded);
    return err;
}
