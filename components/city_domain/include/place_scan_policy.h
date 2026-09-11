#pragma once
#include "location_mode.h"

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
    bool encounter_eligible;
} city_place_scan_decision_t;

/* A ready candidate is eligible only after its catalogue write succeeds. */
city_place_scan_decision_t city_place_scan_decide(
    const city_location_output_t *output, bool new_place_persisted);
