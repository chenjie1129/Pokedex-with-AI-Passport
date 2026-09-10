#include "location_mode.h"

#include <string.h>

static uint64_t saturating_add_ms(uint64_t now_ms, uint64_t duration_ms)
{
    if (UINT64_MAX - now_ms < duration_ms) {
        return UINT64_MAX;
    }
    return now_ms + duration_ms;
}

static void clear_candidate(city_location_state_t *state)
{
    state->candidate_valid = false;
    state->candidate_ready = false;
    state->candidate_started_ms = 0U;
    memset(&state->candidate, 0, sizeof(state->candidate));
}

static city_location_output_t output_from_state(
    const city_location_state_t *state,
    city_location_event_t event)
{
    const city_location_output_t output = {
        .event = event,
        .mode = state->mode,
        .region_id = state->region_id,
        .confidence_permille = state->confidence_permille,
    };
    return output;
}

void city_location_mode_init(city_location_state_t *state)
{
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->mode = CITY_LOCATION_SCANNING;
    state->region_id = CITY_PLACE_INVALID_ID;
}

city_location_output_t city_location_mode_step(
    city_location_state_t *state,
    const city_location_input_t *input)
{
    if (state == NULL || input == NULL) {
        const city_location_output_t invalid = {
            .event = CITY_LOCATION_EVENT_INVALID_INPUT,
            .mode = CITY_LOCATION_SCANNING,
            .region_id = CITY_PLACE_INVALID_ID,
            .confidence_permille = 0U,
        };
        return invalid;
    }

    if (input->scan_status == CITY_SCAN_ERROR) {
        return output_from_state(state, CITY_LOCATION_EVENT_SCAN_ERROR);
    }

    if (state->mode == CITY_LOCATION_KNOWN_PLACE &&
        input->now_ms < state->locked_until_ms) {
        return output_from_state(state, CITY_LOCATION_EVENT_LOCKED);
    }

    if (input->scan_status == CITY_SCAN_EMPTY ||
        (input->scan_status == CITY_SCAN_EVIDENCE &&
         input->fingerprint != NULL &&
         !city_place_fingerprint_is_usable(input->fingerprint))) {
        state->mode = CITY_LOCATION_WILD;
        state->region_id = CITY_PLACE_INVALID_ID;
        state->confidence_permille = 0U;
        state->locked_until_ms = 0U;
        clear_candidate(state);
        return output_from_state(state, CITY_LOCATION_EVENT_WILD);
    }

    if (input->scan_status != CITY_SCAN_EVIDENCE ||
        input->fingerprint == NULL || input->catalog == NULL) {
        return output_from_state(state, CITY_LOCATION_EVENT_INVALID_INPUT);
    }

    const city_place_match_t match =
        city_place_catalog_find(input->catalog, input->fingerprint);
    const city_place_relation_t relation =
        city_place_classify(match.score_permille);

    if (match.has_profile && relation == CITY_PLACE_RELATION_KNOWN) {
        state->mode = CITY_LOCATION_KNOWN_PLACE;
        state->region_id = match.place_id;
        state->confidence_permille = match.score_permille;
        state->locked_until_ms = saturating_add_ms(
            input->now_ms, CITY_LOCATION_LOCK_DURATION_MS);
        clear_candidate(state);
        return output_from_state(
            state, CITY_LOCATION_EVENT_KNOWN_PLACE);
    }

    if (match.has_profile && relation == CITY_PLACE_RELATION_GRAY) {
        state->mode = CITY_LOCATION_GRAY_ZONE;
        state->region_id = CITY_PLACE_INVALID_ID;
        state->confidence_permille = match.score_permille;
        state->locked_until_ms = 0U;
        clear_candidate(state);
        return output_from_state(state, CITY_LOCATION_EVENT_GRAY_ZONE);
    }

    if (!state->candidate_valid) {
        state->candidate_valid = true;
        state->candidate_ready = false;
        state->candidate_started_ms = input->now_ms;
        state->candidate = *input->fingerprint;
        state->mode = CITY_LOCATION_CANDIDATE;
        state->region_id = CITY_PLACE_INVALID_ID;
        state->confidence_permille = match.score_permille;
        state->locked_until_ms = 0U;
        return output_from_state(state, CITY_LOCATION_EVENT_NEW_PENDING);
    }

    const uint16_t candidate_score = city_place_similarity_permille(
        &state->candidate, input->fingerprint);
    if (city_place_classify(candidate_score) != CITY_PLACE_RELATION_KNOWN ||
        input->now_ms < state->candidate_started_ms) {
        state->candidate_ready = false;
        state->candidate_started_ms = input->now_ms;
        state->candidate = *input->fingerprint;
        state->confidence_permille = candidate_score;
        return output_from_state(state, CITY_LOCATION_EVENT_NEW_PENDING);
    }

    state->confidence_permille = candidate_score;
    if (input->now_ms - state->candidate_started_ms <
        CITY_LOCATION_CONFIRM_DELAY_MS) {
        state->candidate_ready = false;
        return output_from_state(state, CITY_LOCATION_EVENT_NEW_PENDING);
    }

    state->candidate_ready = true;
    return output_from_state(state, CITY_LOCATION_EVENT_NEW_PLACE_READY);
}

bool city_location_mode_commit_place(
    city_location_state_t *state,
    uint16_t place_id,
    uint64_t now_ms)
{
    if (state == NULL || !state->candidate_valid || !state->candidate_ready ||
        place_id == CITY_PLACE_INVALID_ID) {
        return false;
    }

    state->mode = CITY_LOCATION_KNOWN_PLACE;
    state->region_id = place_id;
    state->locked_until_ms = saturating_add_ms(
        now_ms, CITY_LOCATION_LOCK_DURATION_MS);
    clear_candidate(state);
    return true;
}
