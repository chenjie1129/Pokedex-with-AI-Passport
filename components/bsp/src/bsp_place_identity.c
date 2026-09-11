#include "bsp_place_identity.h"

#include "esp_random.h"
#include "nvs.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

const bsp_place_identity_store_t BSP_PLACE_IDENTITY_STORE_DEFAULT = {
    .namespace_name = "city_places",
    .identity_key = "token_key",
    .catalog_key = "catalog_v1",
};

static bool store_is_valid(const bsp_place_identity_store_t *store)
{
    return store != NULL && store->namespace_name != NULL &&
           store->namespace_name[0] != '\0' && store->identity_key != NULL &&
           store->identity_key[0] != '\0' && store->catalog_key != NULL &&
           store->catalog_key[0] != '\0' &&
           strcmp(store->identity_key, store->catalog_key) != 0;
}

static void secure_zero(void *data, size_t length)
{
    volatile uint8_t *bytes = data;
    while (length > 0U) {
        *bytes++ = 0U;
        --length;
    }
}

static esp_err_t load_existing_key(
    nvs_handle_t handle,
    const char *key_name,
    uint8_t key[CITY_PRIVACY_KEY_BYTES])
{
    size_t length = 0U;
    esp_err_t err = nvs_get_blob(handle, key_name, NULL, &length);
    if (err != ESP_OK) {
        return err;
    }
    if (length != CITY_PRIVACY_KEY_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t loaded[CITY_PRIVACY_KEY_BYTES];
    err = nvs_get_blob(handle, key_name, loaded, &length);
    if (err == ESP_OK && !city_privacy_key_is_valid(loaded)) {
        err = ESP_ERR_INVALID_STATE;
    }
    if (err == ESP_OK) {
        memcpy(key, loaded, sizeof(loaded));
    }
    secure_zero(loaded, sizeof(loaded));
    return err;
}

static esp_err_t catalog_presence(
    nvs_handle_t handle,
    const char *catalog_key,
    bool *exists)
{
    size_t length = 0U;
    const esp_err_t err =
        nvs_get_blob(handle, catalog_key, NULL, &length);
    if (err == ESP_OK) {
        *exists = true;
        return ESP_OK;
    }
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *exists = false;
        return ESP_OK;
    }
    return err;
}

esp_err_t bsp_place_identity_load_or_create(
    const bsp_place_identity_store_t *store,
    uint8_t key[CITY_PRIVACY_KEY_BYTES],
    bool *created)
{
    if (!store_is_valid(store) || key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(key, 0, CITY_PRIVACY_KEY_BYTES);
    if (created != NULL) {
        *created = false;
    }

    nvs_handle_t handle;
    esp_err_t err =
        nvs_open(store->namespace_name, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = load_existing_key(handle, store->identity_key, key);
    if (err == ESP_OK) {
        nvs_close(handle);
        return ESP_OK;
    }
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }

    bool catalog_exists = false;
    err = catalog_presence(handle, store->catalog_key, &catalog_exists);
    if (err != ESP_OK || catalog_exists) {
        nvs_close(handle);
        return err != ESP_OK ? err : ESP_ERR_INVALID_STATE;
    }

    uint8_t generated[CITY_PRIVACY_KEY_BYTES] = {0};
    for (uint8_t attempt = 0U;
         attempt < 4U && !city_privacy_key_is_valid(generated);
         ++attempt) {
        esp_fill_random(generated, sizeof(generated));
    }
    if (!city_privacy_key_is_valid(generated)) {
        secure_zero(generated, sizeof(generated));
        nvs_close(handle);
        return ESP_ERR_INVALID_STATE;
    }

    err = nvs_set_blob(
        handle, store->identity_key, generated, sizeof(generated));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    if (err == ESP_OK) {
        memcpy(key, generated, sizeof(generated));
        if (created != NULL) {
            *created = true;
        }
    }
    secure_zero(generated, sizeof(generated));
    nvs_close(handle);
    return err;
}

static esp_err_t erase_if_present(nvs_handle_t handle, const char *key)
{
    const esp_err_t err = nvs_erase_key(handle, key);
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

esp_err_t bsp_place_identity_reset(
    const bsp_place_identity_store_t *store)
{
    if (!store_is_valid(store)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err =
        nvs_open(store->namespace_name, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = erase_if_present(handle, store->identity_key);
    if (err == ESP_OK) {
        err = erase_if_present(handle, store->catalog_key);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
