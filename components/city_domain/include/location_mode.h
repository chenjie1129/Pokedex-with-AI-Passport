#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "city_environment.h"
#include "place_fingerprint.h"

#define CITY_LOCATION_CONFIRM_DELAY_MS UINT64_C(20000)
#define CITY_LOCATION_LOCK_DURATION_MS UINT64_C(300000)

typedef enum {
    CITY_LOCATION_SCANNING = 0,
    CITY_LOCATION_KNOWN_PLACE,
    CITY_LOCATION_GRAY_ZONE,
    CITY_LOCATION_CANDIDATE,
    CITY_LOCATION_WILD,
} city_location_mode_t;

typedef enum {
    CITY_LOCATION_EVENT_NONE = 0,
    CITY_LOCATION_EVENT_SCAN_ERROR,
    CITY_LOCATION_EVENT_LOCKED,
    CITY_LOCATION_EVENT_KNOWN_PLACE,
    CITY_LOCATION_EVENT_GRAY_ZONE,
    CITY_LOCATION_EVENT_WILD,
    CITY_LOCATION_EVENT_NEW_PENDING,
    CITY_LOCATION_EVENT_NEW_UNSTABLE,
    CITY_LOCATION_EVENT_NEW_PLACE_READY,
    CITY_LOCATION_EVENT_INVALID_INPUT,
} city_location_event_t;

typedef struct {
    city_location_mode_t mode;
    uint16_t region_id;
    uint16_t confidence_permille;
    uint64_t locked_until_ms;
    bool candidate_valid;
    bool candidate_ready;
    uint64_t candidate_started_ms;
    city_place_fingerprint_t candidate;
} city_location_state_t;

typedef struct {
    city_scan_status_t scan_status;
    uint64_t now_ms;
    const city_place_fingerprint_t *fingerprint;
    const city_place_catalog_t *catalog;
} city_location_input_t;

typedef struct {
    city_location_event_t event;
    city_location_mode_t mode;
    uint16_t region_id;
    uint16_t confidence_permille;
    /* Fresh usable evidence only; retained display state never authorizes play. */
    bool encounter_eligible;
} city_location_output_t;

void city_location_mode_init(city_location_state_t *state);

uint32_t city_location_confirmation_remaining_seconds(
    uint64_t started_ms,
    uint64_t now_ms);

city_location_output_t city_location_mode_step(
    city_location_state_t *state,
    const city_location_input_t *input);

bool city_location_mode_commit_place(
    city_location_state_t *state,
    uint16_t place_id,
    uint64_t now_ms);
