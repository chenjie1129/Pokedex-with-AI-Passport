#include "bestiary_service.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#define TEST_NAMESPACE "city_bst_tst"
#define TEST_KEY "snapshot"

static const char *TAG = "bestiary_test";

typedef struct {
    nvs_handle_t handle;
} device_store_t;

static void fail(const char *reason)
{
    ESP_LOGE(TAG, "DEVICE_TEST_FAIL reason=%s", reason);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static bool persist_snapshot(
    const city_bestiary_t *next,
    void *context)
{
    device_store_t *store = context;
    uint8_t encoded[CITY_BESTIARY_ENCODED_BYTES];
    return city_bestiary_encode(next, encoded) &&
           nvs_set_blob(
               store->handle, TEST_KEY, encoded, sizeof(encoded)) ==
               ESP_OK &&
           nvs_commit(store->handle) == ESP_OK;
}

static bool load_snapshot(
    nvs_handle_t handle,
    city_bestiary_t *bestiary)
{
    uint8_t encoded[CITY_BESTIARY_ENCODED_BYTES];
    size_t length = sizeof(encoded);
    return nvs_get_blob(handle, TEST_KEY, encoded, &length) == ESP_OK &&
           length == sizeof(encoded) &&
           city_bestiary_decode(encoded, length, bestiary);
}

static void run_first_boot(device_store_t *store)
{
    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);

    for (uint64_t encounter_id = 1U; encounter_id <= 20U;
         ++encounter_id) {
        if (city_bestiary_capture(
                &bestiary,
                encounter_id,
                CITY_SPECIES_CHARMANDER,
                1U,
                persist_snapshot,
                store) != CITY_BESTIARY_APPLIED) {
            fail("capture_1_to_20");
        }
    }
    if (bestiary.charmander.capture_count != 20U ||
        bestiary.ledger_count != CITY_BESTIARY_LEDGER_CAPACITY ||
        bestiary.ledger_next != 4U) {
        fail("phase1_state");
    }

    ESP_LOGI(
        TAG,
        "DEVICE_TEST_PHASE1_PASS count=%" PRIu32
        " ledger=%u next=%u",
        bestiary.charmander.capture_count,
        bestiary.ledger_count,
        bestiary.ledger_next);
    nvs_close(store->handle);
    vTaskDelay(pdMS_TO_TICKS(250));
    esp_restart();
}

static void run_second_boot(device_store_t *store)
{
    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);
    if (!load_snapshot(store->handle, &bestiary) ||
        bestiary.charmander.capture_count != 20U ||
        bestiary.ledger_count != CITY_BESTIARY_LEDGER_CAPACITY ||
        bestiary.ledger_next != 4U) {
        fail("reboot_restore");
    }

    if (city_bestiary_capture(
            &bestiary,
            20U,
            CITY_SPECIES_CHARMANDER,
            1U,
            persist_snapshot,
            store) != CITY_BESTIARY_DUPLICATE ||
        bestiary.charmander.capture_count != 20U) {
        fail("recent_duplicate");
    }

    if (city_bestiary_capture(
            &bestiary,
            21U,
            CITY_SPECIES_CHARMANDER,
            1U,
            persist_snapshot,
            store) != CITY_BESTIARY_APPLIED ||
        bestiary.charmander.capture_count != 21U ||
        bestiary.ledger_count != CITY_BESTIARY_LEDGER_CAPACITY ||
        bestiary.ledger_next != 5U) {
        fail("capture_21");
    }

    city_bestiary_t persisted;
    city_bestiary_init(&persisted);
    if (!load_snapshot(store->handle, &persisted) ||
        persisted.charmander.capture_count != 21U ||
        persisted.ledger_next != 5U) {
        fail("nvs_round_trip");
    }

    if (nvs_erase_key(store->handle, TEST_KEY) != ESP_OK ||
        nvs_commit(store->handle) != ESP_OK) {
        fail("cleanup");
    }
    ESP_LOGI(
        TAG,
        "DEVICE_TEST_PASS count=%" PRIu32
        " ledger=%u next=%u duplicate=protected reboot=verified nvs=verified",
        persisted.charmander.capture_count,
        persisted.ledger_count,
        persisted.ledger_next);
}

void app_main(void)
{
    if (nvs_flash_init() != ESP_OK) {
        fail("nvs_init");
    }

    device_store_t store;
    if (nvs_open(
            TEST_NAMESPACE, NVS_READWRITE, &store.handle) != ESP_OK) {
        fail("nvs_open");
    }

    size_t length = 0U;
    const esp_err_t probe =
        nvs_get_blob(store.handle, TEST_KEY, NULL, &length);
    if (probe == ESP_ERR_NVS_NOT_FOUND) {
        run_first_boot(&store);
    }
    if (probe != ESP_OK ||
        length != CITY_BESTIARY_ENCODED_BYTES) {
        fail("snapshot_probe");
    }
    run_second_boot(&store);

    nvs_close(store.handle);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
