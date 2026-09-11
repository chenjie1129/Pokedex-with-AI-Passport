#include "bestiary_service.h"
#include "bsp_bestiary_store.h"
#include "game_loop.h"

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

static void test_legacy_store_migration(void)
{
    const bsp_bestiary_store_t migration_store = {
        .namespace_name = "city_mig_tst",
        .blob_key = "snapshot",
        .legacy_count_key = "caught_004",
    };

    nvs_handle_t handle;
    if (nvs_open(
            migration_store.namespace_name,
            NVS_READWRITE,
            &handle) != ESP_OK ||
        nvs_erase_all(handle) != ESP_OK ||
        nvs_set_u32(
            handle, migration_store.legacy_count_key, 4U) != ESP_OK ||
        nvs_commit(handle) != ESP_OK) {
        fail("migration_seed");
    }
    nvs_close(handle);

    city_bestiary_t migrated_bestiary;
    bool migrated = false;
    if (bsp_bestiary_store_load(
            &migration_store,
            &migrated_bestiary,
            &migrated) != ESP_OK ||
        !migrated ||
        migrated_bestiary.charmander.capture_count != 4U ||
        migrated_bestiary.charmander.last_place_id != UINT16_MAX ||
        migrated_bestiary.last_settled_sequence != 4U) {
        fail("legacy_migration");
    }

    if (city_bestiary_capture(
            &migrated_bestiary,
            UINT64_C(8001),
            CITY_SPECIES_CHARMANDER,
            1U,
            bsp_bestiary_store_persist,
            (void *)&migration_store) != CITY_BESTIARY_APPLIED) {
        fail("migrated_capture");
    }

    city_bestiary_t restored;
    migrated = true;
    if (bsp_bestiary_store_load(
            &migration_store, &restored, &migrated) != ESP_OK ||
        migrated || restored.charmander.capture_count != 5U ||
        restored.charmander.last_place_id != 1U ||
        restored.last_settled_sequence != UINT64_C(8001)) {
        fail("migrated_restore");
    }

    uint32_t legacy_count = 0U;
    if (nvs_open(
            migration_store.namespace_name,
            NVS_READWRITE,
            &handle) != ESP_OK ||
        nvs_get_u32(
            handle,
            migration_store.legacy_count_key,
            &legacy_count) != ESP_ERR_NVS_NOT_FOUND ||
        nvs_erase_key(handle, migration_store.blob_key) != ESP_OK ||
        nvs_commit(handle) != ESP_OK) {
        fail("migration_cleanup");
    }
    nvs_close(handle);
    ESP_LOGI(
        TAG,
        "DEVICE_MIGRATION_PASS legacy=4 count=%" PRIu32
        " sequence=%" PRIu64 " old_key=removed",
        restored.charmander.capture_count,
        restored.last_settled_sequence);
}

static void test_seen_store_round_trip(void)
{
    const bsp_bestiary_store_t seen_store = {
        .namespace_name = "city_seen_tst",
        .blob_key = "snapshot",
        .legacy_count_key = "caught_004",
    };

    nvs_handle_t handle;
    if (nvs_open(
            seen_store.namespace_name,
            NVS_READWRITE,
            &handle) != ESP_OK ||
        nvs_erase_all(handle) != ESP_OK ||
        nvs_commit(handle) != ESP_OK) {
        fail("seen_seed");
    }
    nvs_close(handle);

    city_bestiary_t bestiary;
    bool migrated = true;
    if (bsp_bestiary_store_load(
            &seen_store, &bestiary, &migrated) != ESP_OK ||
        migrated ||
        bestiary.charmander.state != CITY_DISCOVERY_UNKNOWN) {
        fail("seen_empty_load");
    }
    if (city_bestiary_mark_seen(
            &bestiary,
            CITY_SPECIES_CHARMANDER,
            bsp_bestiary_store_persist,
            (void *)&seen_store) != CITY_BESTIARY_APPLIED) {
        fail("seen_commit");
    }

    city_bestiary_t restored;
    if (bsp_bestiary_store_load(
            &seen_store, &restored, NULL) != ESP_OK ||
        restored.charmander.state != CITY_DISCOVERY_SEEN ||
        restored.charmander.capture_count != 0U) {
        fail("seen_restore");
    }
    if (city_bestiary_capture(
            &restored,
            UINT64_C(7001),
            CITY_SPECIES_CHARMANDER,
            1U,
            bsp_bestiary_store_persist,
            (void *)&seen_store) != CITY_BESTIARY_APPLIED) {
        fail("seen_capture");
    }

    city_bestiary_t captured;
    if (bsp_bestiary_store_load(
            &seen_store, &captured, NULL) != ESP_OK ||
        captured.charmander.state != CITY_DISCOVERY_CAPTURED ||
        captured.charmander.capture_count != 1U ||
        captured.last_settled_sequence != UINT64_C(7001)) {
        fail("seen_capture_restore");
    }

    if (nvs_open(
            seen_store.namespace_name,
            NVS_READWRITE,
            &handle) != ESP_OK ||
        nvs_erase_key(handle, seen_store.blob_key) != ESP_OK ||
        nvs_commit(handle) != ESP_OK) {
        fail("seen_cleanup");
    }
    nvs_close(handle);
    ESP_LOGI(
        TAG,
        "DEVICE_SEEN_PASS state=%u count=%" PRIu32,
        captured.charmander.state,
        captured.charmander.capture_count);
}

static bool persist_before_write_failure(
    const city_bestiary_t *next,
    void *context)
{
    (void)next;
    (void)context;
    return false;
}

static bool persist_commit_report_failure(
    const city_bestiary_t *next,
    void *context)
{
    (void)bsp_bestiary_store_persist(next, context);
    return false;
}

static void test_interrupted_commit_recovery(void)
{
    const bsp_bestiary_store_t fault_store = {
        .namespace_name = "city_fault_tst",
        .blob_key = "snapshot",
        .legacy_count_key = "caught_004",
    };
    nvs_handle_t handle;
    if (nvs_open(
            fault_store.namespace_name,
            NVS_READWRITE,
            &handle) != ESP_OK ||
        nvs_erase_all(handle) != ESP_OK ||
        nvs_commit(handle) != ESP_OK) {
        fail("fault_seed");
    }
    nvs_close(handle);

    city_bestiary_t bestiary;
    if (bsp_bestiary_store_load(
            &fault_store, &bestiary, NULL) != ESP_OK ||
        city_bestiary_mark_seen(
            &bestiary,
            CITY_SPECIES_CHARMANDER,
            bsp_bestiary_store_persist,
            (void *)&fault_store) != CITY_BESTIARY_APPLIED) {
        fail("fault_seen_seed");
    }

    if (city_bestiary_capture(
            &bestiary,
            1U,
            CITY_SPECIES_CHARMANDER,
            1U,
            persist_before_write_failure,
            (void *)&fault_store) != CITY_BESTIARY_STORAGE_FAILED ||
        bestiary.charmander.state != CITY_DISCOVERY_SEEN) {
        fail("prewrite_interrupt");
    }

    city_bestiary_t restored;
    if (bsp_bestiary_store_load(
            &fault_store, &restored, NULL) != ESP_OK ||
        restored.charmander.state != CITY_DISCOVERY_SEEN ||
        restored.last_settled_sequence != 0U) {
        fail("prewrite_restore");
    }

    if (city_bestiary_capture(
            &restored,
            1U,
            CITY_SPECIES_CHARMANDER,
            1U,
            persist_commit_report_failure,
            (void *)&fault_store) != CITY_BESTIARY_STORAGE_FAILED ||
        restored.charmander.state != CITY_DISCOVERY_SEEN) {
        fail("postcommit_ambiguous");
    }

    city_bestiary_t committed;
    if (bsp_bestiary_store_load(
            &fault_store, &committed, NULL) != ESP_OK ||
        committed.charmander.capture_count != 1U ||
        committed.last_settled_sequence != 1U ||
        city_bestiary_capture(
            &committed,
            1U,
            CITY_SPECIES_CHARMANDER,
            1U,
            bsp_bestiary_store_persist,
            (void *)&fault_store) != CITY_BESTIARY_DUPLICATE ||
        committed.charmander.capture_count != 1U) {
        fail("postcommit_retry");
    }

    if (nvs_open(
            fault_store.namespace_name,
            NVS_READWRITE,
            &handle) != ESP_OK ||
        nvs_erase_key(handle, fault_store.blob_key) != ESP_OK ||
        nvs_commit(handle) != ESP_OK) {
        fail("fault_cleanup");
    }
    nvs_close(handle);
    ESP_LOGI(TAG, "DEVICE_FAULT_PASS prewrite=rollback postcommit=deduplicated");
}

static void test_capture_timeout_contract(void)
{
    city_game_session_t session;
    city_game_init(&session);
    if (city_game_arrive(&session, 1U, 1U, 17U) !=
            CITY_GAME_EVENT_ENCOUNTER_STARTED ||
        city_game_begin_capture(&session, 100U) !=
            CITY_GAME_EVENT_CAPTURE_STARTED) {
        fail("capture_budget_start");
    }

    uint64_t now = 100U + CITY_CAPTURE_DURATION_MS;
    if (city_game_tick(&session, now) !=
            CITY_GAME_EVENT_ATTEMPT_TIMED_OUT ||
        session.attempts_remaining != 2U) {
        fail("capture_timeout_1");
    }
    now += CITY_CAPTURE_DURATION_MS;
    if (city_game_tick(&session, now) !=
            CITY_GAME_EVENT_ATTEMPT_TIMED_OUT ||
        session.attempts_remaining != 1U) {
        fail("capture_timeout_2");
    }
    now += CITY_CAPTURE_DURATION_MS;
    if (city_game_tick(&session, now) != CITY_GAME_EVENT_ESCAPED ||
        now - session.capture_started_ms > CITY_GAME_CAPTURE_BUDGET_MS) {
        fail("capture_timeout_3");
    }
    ESP_LOGI(
        TAG,
        "DEVICE_CAPTURE_BUDGET_PASS elapsed=%" PRIu64 " budget=%u",
        now - session.capture_started_ms,
        CITY_GAME_CAPTURE_BUDGET_MS);
}

static void run_first_boot(device_store_t *store)
{
    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);

    for (uint64_t encounter_sequence = 1U; encounter_sequence <= 20U;
         ++encounter_sequence) {
        if (city_bestiary_capture(
                &bestiary,
                encounter_sequence,
                CITY_SPECIES_CHARMANDER,
                1U,
                persist_snapshot,
                store) != CITY_BESTIARY_APPLIED) {
            fail("capture_1_to_20");
        }
    }
    if (bestiary.charmander.capture_count != 20U ||
        bestiary.last_settled_sequence != 20U) {
        fail("phase1_state");
    }

    ESP_LOGI(
        TAG,
        "DEVICE_TEST_PHASE1_PASS count=%" PRIu32
        " sequence=%" PRIu64,
        bestiary.charmander.capture_count,
        bestiary.last_settled_sequence);
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
        bestiary.last_settled_sequence != 20U) {
        fail("reboot_restore");
    }

    if (city_bestiary_capture(
            &bestiary,
            20U,
            CITY_SPECIES_CHARMANDER,
            1U,
            persist_snapshot,
            store) != CITY_BESTIARY_DUPLICATE ||
        bestiary.charmander.capture_count != 20U ||
        city_bestiary_capture(
            &bestiary,
            1U,
            CITY_SPECIES_CHARMANDER,
            1U,
            persist_snapshot,
            store) != CITY_BESTIARY_DUPLICATE ||
        bestiary.charmander.capture_count != 20U) {
        fail("stale_duplicate");
    }

    if (city_bestiary_capture(
            &bestiary,
            21U,
            CITY_SPECIES_CHARMANDER,
            1U,
            persist_snapshot,
            store) != CITY_BESTIARY_APPLIED ||
        bestiary.charmander.capture_count != 21U ||
        bestiary.last_settled_sequence != 21U) {
        fail("capture_21");
    }

    city_bestiary_t persisted;
    city_bestiary_init(&persisted);
    if (!load_snapshot(store->handle, &persisted) ||
        persisted.charmander.capture_count != 21U ||
        persisted.last_settled_sequence != 21U) {
        fail("nvs_round_trip");
    }

    if (nvs_erase_key(store->handle, TEST_KEY) != ESP_OK ||
        nvs_commit(store->handle) != ESP_OK) {
        fail("cleanup");
    }
    ESP_LOGI(
        TAG,
        "DEVICE_TEST_PASS count=%" PRIu32
        " sequence=%" PRIu64 " stale=protected reboot=verified nvs=verified",
        persisted.charmander.capture_count,
        persisted.last_settled_sequence);
}

void app_main(void)
{
    if (nvs_flash_init() != ESP_OK) {
        fail("nvs_init");
    }
    test_legacy_store_migration();
    test_seen_store_round_trip();
    test_interrupted_commit_recovery();
    test_capture_timeout_contract();

    device_store_t store;
    if (nvs_open(
            TEST_NAMESPACE, NVS_READWRITE, &store.handle) != ESP_OK) {
        fail("nvs_open");
    }

    size_t length = 0U;
    const esp_err_t probe =
        nvs_get_blob(store.handle, TEST_KEY, NULL, &length);
    bool continue_after_reboot = false;
    if (probe == ESP_OK && length == CITY_BESTIARY_ENCODED_BYTES) {
        city_bestiary_t persisted;
        continue_after_reboot =
            load_snapshot(store.handle, &persisted) &&
            persisted.schema_version == CITY_BESTIARY_SCHEMA_VERSION &&
            persisted.charmander.capture_count == 20U &&
            persisted.last_settled_sequence == 20U;
    }
    if (!continue_after_reboot) {
        if (probe == ESP_OK && nvs_erase_key(store.handle, TEST_KEY) != ESP_OK) {
            fail("stale_snapshot_cleanup");
        }
        if (nvs_commit(store.handle) != ESP_OK) {
            fail("stale_snapshot_commit");
        }
        run_first_boot(&store);
    }
    run_second_boot(&store);

    nvs_close(store.handle);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
