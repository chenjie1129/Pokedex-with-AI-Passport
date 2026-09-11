#pragma once

#include "bsp_place_identity.h"
#include "bsp_place_store.h"
#include "esp_err.h"
#include "place_scan_policy.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    place_result_kind_t kind;
    bool encounter_eligible;
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
