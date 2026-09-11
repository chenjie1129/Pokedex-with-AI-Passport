#include "bsp_place_identity.h"
#include "bsp_place_store.h"
#include "place_profile_codec.h"
#include "privacy_tokenizer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#define TEST_NAMESPACE "city_pl_tst"
#define BAD_KEY_NAMESPACE "city_key_tst"
#define TEST_PHASE_KEY "phase"

static const char *TAG = "place_store_test";

static const bsp_place_identity_store_t TEST_IDENTITY_STORE = {
    .namespace_name = TEST_NAMESPACE,
    .identity_key = "token_key",
    .catalog_key = "catalog_v1",
};

static const bsp_place_store_t TEST_PLACE_STORE = {
    .namespace_name = TEST_NAMESPACE,
    .blob_key = "catalog_v1",
};

static city_place_catalog_t s_catalog_a;
static city_place_catalog_t s_catalog_b;
static uint8_t s_encoded[CITY_PLACE_CATALOG_MAX_ENCODED_BYTES];

static void fail(const char *reason)
{
    ESP_LOGE(TAG, "DEVICE_PLACE_TEST_FAIL reason=%s", reason);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static bool bytes_are_zero(const uint8_t *data, size_t length)
{
    uint8_t combined = 0U;
    for (size_t i = 0; i < length; ++i) {
        combined |= data[i];
    }
    return combined == 0U;
}

static bool bytes_are_value(
    const void *data,
    size_t length,
    uint8_t expected)
{
    const uint8_t *bytes = data;
    for (size_t i = 0; i < length; ++i) {
        if (bytes[i] != expected) {
            return false;
        }
    }
    return true;
}

static void erase_namespace(const char *namespace_name)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace_name, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        fail("cleanup_open");
    }
    err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        fail("cleanup_commit");
    }
}

static void fill_sample_catalog(city_place_catalog_t *catalog)
{
    memset(catalog, 0, sizeof(*catalog));
    catalog->count = 1U;
    catalog->places[0].schema_version = CITY_PLACE_PROFILE_SCHEMA_VERSION;
    catalog->places[0].place_id = 7U;
    catalog->places[0].confidence_permille = 1000U;
    catalog->places[0].last_confirmed_ms = UINT64_C(123456);
    catalog->places[0].fingerprint.count = 4U;
    catalog->places[0].fingerprint.tokens[0] = UINT64_C(101);
    catalog->places[0].fingerprint.tokens[1] = UINT64_C(102);
    catalog->places[0].fingerprint.tokens[2] = UINT64_C(103);
    catalog->places[0].fingerprint.tokens[3] = UINT64_C(104);
}

static uint8_t read_phase(void)
{
    nvs_handle_t handle;
    esp_err_t err =
        nvs_open(TEST_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        fail("phase_open");
    }

    uint8_t phase = 0U;
    err = nvs_get_u8(handle, TEST_PHASE_KEY, &phase);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return 0U;
    }
    if (err != ESP_OK) {
        fail("phase_read");
    }
    return phase;
}

static void write_phase(uint8_t phase)
{
    nvs_handle_t handle;
    esp_err_t err =
        nvs_open(TEST_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        fail("phase_open_write");
    }
    err = nvs_set_u8(handle, TEST_PHASE_KEY, phase);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        fail("phase_write");
    }
}

static void run_first_boot(void)
{
    erase_namespace(TEST_NAMESPACE);

    bool found = true;
    if (bsp_place_store_load(
            &TEST_PLACE_STORE, &s_catalog_a, &found) != ESP_OK ||
        found || s_catalog_a.count != 0U) {
        fail("empty_catalog");
    }

    uint8_t key[CITY_PRIVACY_KEY_BYTES];
    bool created = false;
    if (bsp_place_identity_load_or_create(
            &TEST_IDENTITY_STORE, key, &created) != ESP_OK ||
        !created || !city_privacy_key_is_valid(key)) {
        fail("identity_create");
    }
    memset(key, 0, sizeof(key));

    fill_sample_catalog(&s_catalog_a);
    if (bsp_place_store_save(&TEST_PLACE_STORE, &s_catalog_a) != ESP_OK) {
        fail("catalog_save");
    }

    write_phase(1U);
    ESP_LOGI(TAG, "DEVICE_PLACE_PHASE1_PASS identity=created catalog=saved");
    vTaskDelay(pdMS_TO_TICKS(250));
    esp_restart();
}

static void corrupt_catalog(void)
{
    nvs_handle_t handle;
    esp_err_t err =
        nvs_open(TEST_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        fail("catalog_corrupt_open");
    }
    size_t length = CITY_PLACE_CATALOG_MAX_ENCODED_BYTES;
    err = nvs_get_blob(
        handle, TEST_PLACE_STORE.blob_key, s_encoded, &length);
    if (err == ESP_OK && length > 0U) {
        s_encoded[length - 1U] ^= UINT8_C(0x01);
        err = nvs_set_blob(
            handle, TEST_PLACE_STORE.blob_key, s_encoded, length);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        fail("catalog_corrupt");
    }
}

static void test_invalid_identity_size(void)
{
    erase_namespace(BAD_KEY_NAMESPACE);
    const bsp_place_identity_store_t store = {
        .namespace_name = BAD_KEY_NAMESPACE,
        .identity_key = "token_key",
        .catalog_key = "catalog_v1",
    };

    nvs_handle_t handle;
    uint64_t short_key = UINT64_C(0x12345678);
    esp_err_t err =
        nvs_open(BAD_KEY_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        fail("invalid_key_open");
    }
    err = nvs_set_blob(
        handle, store.identity_key, &short_key, sizeof(short_key));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        fail("invalid_key_seed");
    }

    uint8_t key[CITY_PRIVACY_KEY_BYTES];
    memset(key, UINT8_C(0xa5), sizeof(key));
    if (bsp_place_identity_load_or_create(&store, key, NULL) !=
            ESP_ERR_INVALID_SIZE ||
        !bytes_are_zero(key, sizeof(key))) {
        fail("invalid_key_size");
    }
    erase_namespace(BAD_KEY_NAMESPACE);
}

static void run_second_boot(void)
{
    uint8_t key[CITY_PRIVACY_KEY_BYTES];
    bool created = true;
    if (bsp_place_identity_load_or_create(
            &TEST_IDENTITY_STORE, key, &created) != ESP_OK ||
        created || !city_privacy_key_is_valid(key)) {
        fail("identity_restore");
    }
    memset(key, 0, sizeof(key));

    fill_sample_catalog(&s_catalog_a);
    bool found = false;
    if (bsp_place_store_load(
            &TEST_PLACE_STORE, &s_catalog_b, &found) != ESP_OK ||
        !found ||
        memcmp(&s_catalog_b, &s_catalog_a, sizeof(s_catalog_b)) != 0) {
        fail("catalog_restore");
    }

    corrupt_catalog();
    memset(&s_catalog_b, UINT8_C(0xa5), sizeof(s_catalog_b));
    found = true;
    if (bsp_place_store_load(
            &TEST_PLACE_STORE, &s_catalog_b, &found) !=
            ESP_ERR_INVALID_STATE ||
        found ||
        !bytes_are_value(
            &s_catalog_b, sizeof(s_catalog_b), UINT8_C(0xa5))) {
        fail("catalog_crc");
    }
    if (bsp_place_store_save(&TEST_PLACE_STORE, &s_catalog_a) != ESP_OK) {
        fail("catalog_restore_after_crc");
    }

    nvs_handle_t handle;
    esp_err_t err =
        nvs_open(TEST_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        fail("identity_erase_open");
    }
    err = nvs_erase_key(handle, TEST_IDENTITY_STORE.identity_key);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        fail("identity_erase");
    }

    memset(key, UINT8_C(0xa5), sizeof(key));
    if (bsp_place_identity_load_or_create(
            &TEST_IDENTITY_STORE, key, &created) != ESP_ERR_INVALID_STATE ||
        !bytes_are_zero(key, sizeof(key))) {
        fail("identity_fail_closed");
    }

    test_invalid_identity_size();
    if (bsp_place_identity_reset(&TEST_IDENTITY_STORE) != ESP_OK ||
        bsp_place_identity_load_or_create(
            &TEST_IDENTITY_STORE, key, &created) != ESP_OK ||
        !created || !city_privacy_key_is_valid(key)) {
        fail("identity_factory_reset");
    }
    bool found_after_reset = true;
    if (bsp_place_store_load(
            &TEST_PLACE_STORE, &s_catalog_b, &found_after_reset) != ESP_OK ||
        found_after_reset || s_catalog_b.count != 0U) {
        fail("catalog_factory_reset");
    }
    memset(key, 0, sizeof(key));
    erase_namespace(TEST_NAMESPACE);
    ESP_LOGI(
        TAG,
        "DEVICE_PLACE_TEST_PASS reboot=stable crc=rejected "
        "missing_identity=blocked reset=atomic");
}

void app_main(void)
{
    if (nvs_flash_init() != ESP_OK) {
        fail("nvs_init");
    }

    if (read_phase() == 0U) {
        run_first_boot();
    }
    run_second_boot();

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
