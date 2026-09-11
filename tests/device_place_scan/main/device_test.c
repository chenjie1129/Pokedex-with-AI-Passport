#include "bsp_place_identity.h"
#include "bsp_place_store.h"
#include "place_fingerprint.h"
#include "place_scan_coordinator.h"

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#define TEST_NAMESPACE "city_scan_tst"
#define TEST_TIMEOUT_MS UINT64_C(70000)

static const char *TAG = "place_scan_test";

static const place_scan_coordinator_config_t TEST_CONFIG = {
    .identity_store = {
        .namespace_name = TEST_NAMESPACE,
        .identity_key = "token_key",
        .catalog_key = "catalog_v1",
    },
    .place_store = {
        .namespace_name = TEST_NAMESPACE,
        .blob_key = "catalog_v1",
    },
};

static city_place_catalog_t s_catalog;

static void fail(const char *reason)
{
    ESP_LOGE(TAG, "DEVICE_SCAN_TEST_FAIL reason=%s", reason);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static uint64_t now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static void erase_test_namespace(void)
{
    nvs_handle_t handle;
    esp_err_t err =
        nvs_open(TEST_NAMESPACE, NVS_READWRITE, &handle);
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

static bool result_is_terminal(place_result_kind_t kind)
{
    return kind != PLACE_RESULT_CANDIDATE_WAIT;
}

static void verify_persisted_place(const place_scan_result_t *result)
{
    if (result->kind != PLACE_RESULT_NEW_CONFIRMED) {
        return;
    }

    bool found = false;
    if (bsp_place_store_load(
            &TEST_CONFIG.place_store, &s_catalog, &found) != ESP_OK ||
        !found || s_catalog.count != 1U ||
        s_catalog.places[0].place_id != result->place_id) {
        fail("confirmed_place_restore");
    }
}

void app_main(void)
{
    if (nvs_flash_init() != ESP_OK) {
        fail("nvs_init");
    }
    erase_test_namespace();

    bool identity_created = false;
    bool catalog_found = true;
    uint16_t place_count = UINT16_MAX;
    if (place_scan_coordinator_start(
            &TEST_CONFIG,
            &identity_created,
            &catalog_found,
            &place_count) != ESP_OK ||
        !identity_created || catalog_found || place_count != 0U) {
        fail("coordinator_start");
    }

    if (!place_scan_coordinator_request() ||
        place_scan_coordinator_request()) {
        fail("single_in_flight");
    }

    const uint64_t started_ms = now_ms();
    uint32_t heartbeat = 0U;
    for (;;) {
        place_scan_result_t result;
        if (place_scan_coordinator_receive(&result)) {
            ESP_LOGI(
                TAG,
                "DEVICE_SCAN_RESULT kind=%d place_id=%u score=%u aps=%u "
                "duration_ms=%lu heap_before=%lu heap_after=%lu heap_min=%lu",
                (int)result.kind,
                result.place_id,
                result.confidence_permille,
                result.ap_count,
                (unsigned long)result.duration_ms,
                (unsigned long)result.free_heap_before,
                (unsigned long)result.free_heap_after,
                (unsigned long)result.minimum_free_heap);

            if (result.free_heap_before == 0U ||
                result.free_heap_after == 0U ||
                result.minimum_free_heap == 0U) {
                fail("heap_metrics");
            }
            if (!result_is_terminal(result.kind)) {
                continue;
            }
            if (result.kind == PLACE_RESULT_SCAN_ERROR ||
                result.kind == PLACE_RESULT_STORAGE_ERROR ||
                result.kind == PLACE_RESULT_CAPACITY_FULL) {
                fail("terminal_error");
            }

            verify_persisted_place(&result);
            if (bsp_place_identity_reset(
                    &TEST_CONFIG.identity_store) != ESP_OK) {
                fail("cleanup_reset");
            }
            ESP_LOGI(
                TAG,
                "DEVICE_SCAN_TEST_PASS result=%d heartbeat=%lu elapsed_ms=%llu",
                (int)result.kind,
                (unsigned long)heartbeat,
                (unsigned long long)(now_ms() - started_ms));
            break;
        }

        if (now_ms() - started_ms > TEST_TIMEOUT_MS) {
            fail("timeout");
        }
        ++heartbeat;
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
