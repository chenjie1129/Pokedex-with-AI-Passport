#include "place_scan_coordinator.h"

#include "bsp_place_identity.h"
#include "bsp_place_store.h"
#include "bsp_wifi_scan.h"
#include "location_mode.h"
#include "place_fingerprint.h"

#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PLACE_SCAN_MAX_PASSES 3U
#define PLACE_SCAN_TASK_STACK_BYTES 6144U

typedef enum {
    PLACE_SCAN_COMMAND_START = 0,
} place_scan_command_t;

typedef struct {
    QueueHandle_t command_queue;
    QueueHandle_t result_queue;
    place_scan_coordinator_config_t config;
    uint8_t identity_key[CITY_PRIVACY_KEY_BYTES];
    city_place_catalog_t catalog;
    city_location_state_t location;
    bool in_flight;
    bool ready;
} place_scan_context_t;

static place_scan_context_t s_context;

const place_scan_coordinator_config_t
    PLACE_SCAN_COORDINATOR_CONFIG_DEFAULT = {
        .identity_store = {
            .namespace_name = "city_places",
            .identity_key = "token_key",
            .catalog_key = "catalog_v1",
        },
        .place_store = {
            .namespace_name = "city_places",
            .blob_key = "catalog_v1",
        },
    };

static uint64_t monotonic_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static void secure_zero(void *data, size_t length)
{
    volatile uint8_t *bytes = data;
    while (length > 0U) {
        *bytes++ = 0U;
        --length;
    }
}

static uint32_t elapsed_ms(uint64_t started_ms, uint64_t finished_ms)
{
    const uint64_t elapsed =
        finished_ms >= started_ms ? finished_ms - started_ms : 0U;
    return elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
}

static place_scan_result_t result_from_output(
    const city_location_output_t *output,
    uint8_t ap_count,
    uint32_t duration_ms,
    uint32_t free_heap_before)
{
    place_scan_result_t result = {
        .kind = PLACE_RESULT_SCAN_ERROR,
        .place_id = output->region_id,
        .confidence_permille = output->confidence_permille,
        .ap_count = ap_count,
        .duration_ms = duration_ms,
        .free_heap_before = free_heap_before,
        .free_heap_after = esp_get_free_heap_size(),
        .minimum_free_heap = esp_get_minimum_free_heap_size(),
        .error = ESP_OK,
    };

    const city_place_scan_decision_t decision =
        city_place_scan_decide(output, false);
    result.kind = decision.kind;
    result.encounter_eligible = decision.encounter_eligible;
    if (result.kind == PLACE_RESULT_SCAN_ERROR) {
        result.error = ESP_ERR_INVALID_STATE;
    }
    return result;
}

static bool next_place_id(
    const city_place_catalog_t *catalog,
    uint16_t *place_id)
{
    if (catalog == NULL || place_id == NULL ||
        catalog->count >= CITY_PLACE_MAX_COUNT) {
        return false;
    }

    uint16_t maximum = 0U;
    for (uint16_t i = 0U; i < catalog->count; ++i) {
        if (catalog->places[i].place_id > maximum) {
            maximum = catalog->places[i].place_id;
        }
    }
    if (maximum >= CITY_PLACE_INVALID_ID - 1U) {
        return false;
    }
    *place_id = (uint16_t)(maximum + 1U);
    return true;
}

static place_scan_result_t persist_confirmed_place(
    const city_location_output_t *output,
    uint8_t ap_count,
    uint32_t duration_ms,
    uint64_t now_ms,
    uint32_t free_heap_before)
{
    place_scan_result_t result = {
        .kind = PLACE_RESULT_STORAGE_ERROR,
        .place_id = CITY_PLACE_INVALID_ID,
        .confidence_permille = output->confidence_permille,
        .ap_count = ap_count,
        .duration_ms = duration_ms,
        .free_heap_before = free_heap_before,
        .free_heap_after = esp_get_free_heap_size(),
        .minimum_free_heap = esp_get_minimum_free_heap_size(),
        .error = ESP_ERR_INVALID_STATE,
    };

    uint16_t place_id = CITY_PLACE_INVALID_ID;
    if (!next_place_id(&s_context.catalog, &place_id)) {
        result.kind = PLACE_RESULT_CAPACITY_FULL;
        result.error = ESP_ERR_NO_MEM;
        return result;
    }

    city_place_fingerprint_t fingerprint =
        s_context.location.candidate;
    city_location_state_t next_location = s_context.location;
    if (!city_location_mode_commit_place(
            &next_location, place_id, now_ms)) {
        secure_zero(&fingerprint, sizeof(fingerprint));
        secure_zero(&next_location, sizeof(next_location));
        return result;
    }

    city_place_catalog_t next_catalog = s_context.catalog;
    city_place_profile_t *profile =
        &next_catalog.places[next_catalog.count];
    memset(profile, 0, sizeof(*profile));
    profile->schema_version = CITY_PLACE_PROFILE_SCHEMA_VERSION;
    profile->place_id = place_id;
    profile->confidence_permille = output->confidence_permille;
    profile->last_confirmed_ms = now_ms;
    profile->fingerprint = fingerprint;
    ++next_catalog.count;
    secure_zero(&fingerprint, sizeof(fingerprint));

    const esp_err_t err =
        bsp_place_store_save(
            &s_context.config.place_store, &next_catalog);
    if (err != ESP_OK) {
        secure_zero(&next_catalog, sizeof(next_catalog));
        secure_zero(&next_location, sizeof(next_location));
        result.error = err;
        return result;
    }

    s_context.catalog = next_catalog;
    s_context.location = next_location;
    secure_zero(&next_catalog, sizeof(next_catalog));
    secure_zero(&next_location, sizeof(next_location));
    const city_place_scan_decision_t decision =
        city_place_scan_decide(output, true);
    result.kind = decision.kind;
    result.encounter_eligible = decision.encounter_eligible;
    result.place_id = place_id;
    result.free_heap_after = esp_get_free_heap_size();
    result.minimum_free_heap = esp_get_minimum_free_heap_size();
    result.error = ESP_OK;
    return result;
}

static place_scan_result_t scan_once(void)
{
    const uint32_t free_heap_before = esp_get_free_heap_size();
    const uint64_t started_ms = monotonic_ms();
    bsp_wifi_ap_t raw_aps[BSP_WIFI_SCAN_MAX] = {0};
    city_wifi_ap_observation_t observations[BSP_WIFI_SCAN_MAX] = {0};
    size_t ap_count = 0U;

    const esp_err_t scan_err =
        bsp_wifi_scan_once(raw_aps, BSP_WIFI_SCAN_MAX, &ap_count);
    if (scan_err != ESP_OK) {
        secure_zero(raw_aps, sizeof(raw_aps));
        const city_location_input_t input = {
            .scan_status = CITY_SCAN_ERROR,
            .now_ms = monotonic_ms(),
            .fingerprint = NULL,
            .catalog = &s_context.catalog,
        };
        (void)city_location_mode_step(&s_context.location, &input);
        const place_scan_result_t result = {
            .kind = PLACE_RESULT_SCAN_ERROR,
            .place_id = CITY_PLACE_INVALID_ID,
            .confidence_permille = 0U,
            .ap_count = 0U,
            .duration_ms = elapsed_ms(started_ms, monotonic_ms()),
            .free_heap_before = free_heap_before,
            .free_heap_after = esp_get_free_heap_size(),
            .minimum_free_heap = esp_get_minimum_free_heap_size(),
            .error = scan_err,
        };
        return result;
    }

    for (size_t i = 0U; i < ap_count; ++i) {
        memcpy(
            observations[i].bssid,
            raw_aps[i].bssid,
            sizeof(observations[i].bssid));
        observations[i].rssi = raw_aps[i].rssi;
    }
    secure_zero(raw_aps, sizeof(raw_aps));

    city_place_fingerprint_t fingerprint;
    memset(&fingerprint, 0, sizeof(fingerprint));
    city_scan_status_t status = CITY_SCAN_EMPTY;
    if (ap_count > 0U) {
        if (!city_place_fingerprint_build(
                &fingerprint,
                observations,
                ap_count,
                s_context.identity_key)) {
            secure_zero(observations, sizeof(observations));
            secure_zero(&fingerprint, sizeof(fingerprint));
            const place_scan_result_t result = {
                .kind = PLACE_RESULT_SCAN_ERROR,
                .place_id = CITY_PLACE_INVALID_ID,
                .confidence_permille = 0U,
                .ap_count = (uint8_t)ap_count,
                .duration_ms = elapsed_ms(started_ms, monotonic_ms()),
                .free_heap_before = free_heap_before,
                .free_heap_after = esp_get_free_heap_size(),
                .minimum_free_heap = esp_get_minimum_free_heap_size(),
                .error = ESP_ERR_INVALID_STATE,
            };
            return result;
        }
        status = CITY_SCAN_EVIDENCE;
    }
    secure_zero(observations, sizeof(observations));

    const uint64_t now_ms = monotonic_ms();
    const city_location_input_t input = {
        .scan_status = status,
        .now_ms = now_ms,
        .fingerprint =
            status == CITY_SCAN_EVIDENCE ? &fingerprint : NULL,
        .catalog = &s_context.catalog,
    };
    const city_location_output_t output =
        city_location_mode_step(&s_context.location, &input);
    const uint32_t duration_ms = elapsed_ms(started_ms, now_ms);
    const uint8_t result_ap_count = (uint8_t)ap_count;
    secure_zero(&fingerprint, sizeof(fingerprint));

    if (output.event == CITY_LOCATION_EVENT_NEW_PLACE_READY) {
        return persist_confirmed_place(
            &output,
            result_ap_count,
            duration_ms,
            now_ms,
            free_heap_before);
    }
    return result_from_output(
        &output, result_ap_count, duration_ms, free_heap_before);
}

static void wait_for_candidate_confirmation(void)
{
    const uint64_t ready_at =
        s_context.location.candidate_started_ms +
        CITY_LOCATION_CONFIRM_DELAY_MS;
    const uint64_t now_ms = monotonic_ms();
    if (now_ms < ready_at) {
        vTaskDelay(pdMS_TO_TICKS((uint32_t)(ready_at - now_ms)));
    }
}

static void scan_task(void *argument)
{
    (void)argument;
    place_scan_command_t command;

    for (;;) {
        if (xQueueReceive(
                s_context.command_queue, &command, portMAX_DELAY) != pdPASS) {
            continue;
        }

        for (uint8_t pass = 0U; pass < PLACE_SCAN_MAX_PASSES; ++pass) {
            place_scan_result_t result = scan_once();
            if (result.kind != PLACE_RESULT_CANDIDATE_WAIT) {
                (void)xQueueSend(
                    s_context.result_queue, &result, portMAX_DELAY);
                break;
            }

            if (pass + 1U >= PLACE_SCAN_MAX_PASSES) {
                city_location_mode_init(&s_context.location);
                result.kind = PLACE_RESULT_UNSTABLE;
                (void)xQueueSend(
                    s_context.result_queue, &result, portMAX_DELAY);
                break;
            }

            (void)xQueueSend(
                s_context.result_queue, &result, portMAX_DELAY);
            wait_for_candidate_confirmation();
        }
    }
}

esp_err_t place_scan_coordinator_start(
    const place_scan_coordinator_config_t *config,
    bool *identity_created,
    bool *catalog_found,
    uint16_t *place_count)
{
    if (config == NULL ||
        config->identity_store.namespace_name == NULL ||
        config->identity_store.identity_key == NULL ||
        config->identity_store.catalog_key == NULL ||
        config->place_store.namespace_name == NULL ||
        config->place_store.blob_key == NULL ||
        strcmp(
            config->identity_store.namespace_name,
            config->place_store.namespace_name) != 0 ||
        strcmp(
            config->identity_store.catalog_key,
            config->place_store.blob_key) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_context.ready) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(&s_context, 0, sizeof(s_context));
    s_context.config = *config;
    if (identity_created != NULL) {
        *identity_created = false;
    }
    if (catalog_found != NULL) {
        *catalog_found = false;
    }
    if (place_count != NULL) {
        *place_count = 0U;
    }

    esp_err_t err = bsp_place_identity_load_or_create(
        &s_context.config.identity_store,
        s_context.identity_key,
        identity_created);
    if (err != ESP_OK) {
        return err;
    }

    err = bsp_place_store_load(
        &s_context.config.place_store,
        &s_context.catalog,
        catalog_found);
    if (err != ESP_OK) {
        secure_zero(
            s_context.identity_key, sizeof(s_context.identity_key));
        return err;
    }

    s_context.command_queue =
        xQueueCreate(1U, sizeof(place_scan_command_t));
    s_context.result_queue =
        xQueueCreate(2U, sizeof(place_scan_result_t));
    if (s_context.command_queue == NULL ||
        s_context.result_queue == NULL) {
        if (s_context.command_queue != NULL) {
            vQueueDelete(s_context.command_queue);
        }
        if (s_context.result_queue != NULL) {
            vQueueDelete(s_context.result_queue);
        }
        secure_zero(
            s_context.identity_key, sizeof(s_context.identity_key));
        memset(&s_context, 0, sizeof(s_context));
        return ESP_ERR_NO_MEM;
    }

    city_location_mode_init(&s_context.location);
    if (xTaskCreate(
            scan_task,
            "place_scan",
            PLACE_SCAN_TASK_STACK_BYTES,
            NULL,
            4,
            NULL) != pdPASS) {
        vQueueDelete(s_context.command_queue);
        vQueueDelete(s_context.result_queue);
        secure_zero(
            s_context.identity_key, sizeof(s_context.identity_key));
        memset(&s_context, 0, sizeof(s_context));
        return ESP_ERR_NO_MEM;
    }

    s_context.ready = true;
    if (place_count != NULL) {
        *place_count = s_context.catalog.count;
    }
    return ESP_OK;
}

bool place_scan_coordinator_request(void)
{
    if (!s_context.ready || s_context.in_flight) {
        return false;
    }
    const place_scan_command_t command = PLACE_SCAN_COMMAND_START;
    if (xQueueSend(s_context.command_queue, &command, 0U) != pdPASS) {
        return false;
    }
    s_context.in_flight = true;
    return true;
}

bool place_scan_coordinator_receive(place_scan_result_t *result)
{
    if (!s_context.ready || result == NULL ||
        xQueueReceive(s_context.result_queue, result, 0U) != pdPASS) {
        return false;
    }
    if (result->kind != PLACE_RESULT_CANDIDATE_WAIT) {
        s_context.in_flight = false;
    }
    return true;
}

bool place_scan_coordinator_passport(city_passport_stamps_t *stamps)
{
    // The worker mutates the catalog only during a requested scan. Its final
    // result queue handoff precedes receive() clearing in_flight on the UI task.
    if (!s_context.ready || s_context.in_flight) return false;
    return city_passport_stamps_from_catalog(&s_context.catalog, stamps);
}
