#include "place_scan_policy.h"
#include <stddef.h>

city_place_scan_decision_t city_place_scan_decide(
    const city_location_output_t *output, bool new_place_persisted)
{
    city_place_scan_decision_t result = {
        .kind = PLACE_RESULT_SCAN_ERROR, .encounter_eligible = false,
    };
    if (output == NULL) return result;
    switch (output->event) {
    case CITY_LOCATION_EVENT_LOCKED:
    case CITY_LOCATION_EVENT_KNOWN_PLACE:
        if (output->encounter_eligible &&
            output->mode == CITY_LOCATION_KNOWN_PLACE &&
            output->region_id != CITY_PLACE_INVALID_ID) {
            result.kind = PLACE_RESULT_KNOWN;
            result.encounter_eligible = true;
        }
        break;
    case CITY_LOCATION_EVENT_NEW_PLACE_READY:
        result.kind = new_place_persisted
                          ? PLACE_RESULT_NEW_CONFIRMED
                          : PLACE_RESULT_STORAGE_ERROR;
        result.encounter_eligible = new_place_persisted;
        break;
    case CITY_LOCATION_EVENT_NEW_PENDING:
        result.kind = PLACE_RESULT_CANDIDATE_WAIT;
        break;
    case CITY_LOCATION_EVENT_GRAY_ZONE:
        result.kind = PLACE_RESULT_GRAY;
        break;
    case CITY_LOCATION_EVENT_WILD:
        result.kind = PLACE_RESULT_WILD;
        break;
    default:
        break;
    }
    return result;
}
