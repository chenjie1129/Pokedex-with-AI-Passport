#pragma once

#include "bsp_place_identity.h"
#include "bsp_place_store.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PLACE_RESULT_KNOWN = 0,
    PLACE_RESULT_CANDIDATE_WAIT,
    PLACE_RESULT_NEW_CONFIRMED,
    PLACE_RESULT_GRAY,
    PLACE_RESULT_WILD,
    PLACE_RESULT_UNSTABLE,
    PLACE_RESULT_SCAN_ERROR,
    PLACE_RESULT_STORAGE_ERROR,
    PLACE_RESULT_CAPACITY_FULL,
} place_result_kind_t;

typedef struct {
    place_result_kind_t kind;
    uint16_t place_id;
    uint16_t confidence_permille;
    uint8_t ap_count;
    uint32_t duration_ms;
    uint32_t free_heap_before;
    uint32_t free_heap_after;
    uint32_t minimum_free_heap;
    esp_err_t error;
} place_scan_result_t;

typedef struct {
    bsp_place_identity_store_t identity_store;
    bsp_place_store_t place_store;
} place_scan_coordinator_config_t;

extern const place_scan_coordinator_config_t
    PLACE_SCAN_COORDINATOR_CONFIG_DEFAULT;

esp_err_t place_scan_coordinator_start(
    const place_scan_coordinator_config_t *config,
    bool *identity_created,
    bool *catalog_found,
    uint16_t *place_count);

bool place_scan_coordinator_request(void);

bool place_scan_coordinator_receive(place_scan_result_t *result);
