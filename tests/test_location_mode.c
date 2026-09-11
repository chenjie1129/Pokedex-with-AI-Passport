#include "location_mode.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition)                                                  \
    do {                                                                  \
        if (!(condition)) {                                               \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,       \
                    #condition);                                          \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

static city_place_fingerprint_t fingerprint_from(
    const uint64_t *tokens,
    uint8_t count)
{
    city_place_fingerprint_t fingerprint;
    memset(&fingerprint, 0, sizeof(fingerprint));
    fingerprint.count = count;
    for (uint8_t i = 0; i < count; ++i) {
        fingerprint.tokens[i] = tokens[i];
    }
    return fingerprint;
}

static city_location_output_t step(
    city_location_state_t *state,
    city_scan_status_t status,
    uint64_t now_ms,
    const city_place_fingerprint_t *fingerprint,
    const city_place_catalog_t *catalog)
{
    const city_location_input_t input = {
        .scan_status = status,
        .now_ms = now_ms,
        .fingerprint = fingerprint,
        .catalog = catalog,
    };
    return city_location_mode_step(state, &input);
}

static void test_scan_error_preserves_state(void)
{
    city_location_state_t state;
    city_location_mode_init(&state);
    state.mode = CITY_LOCATION_WILD;
    state.region_id = 17U;
    state.confidence_permille = 321U;
    state.candidate_valid = true;
    state.candidate_started_ms = 55U;
    const city_location_state_t before = state;

    const city_location_output_t output =
        step(&state, CITY_SCAN_ERROR, 100U, NULL, NULL);

    CHECK(output.event == CITY_LOCATION_EVENT_SCAN_ERROR);
    CHECK(memcmp(&state, &before, sizeof(state)) == 0);
}

static void test_empty_and_sparse_scans_enter_wild(void)
{
    city_location_state_t state;
    city_location_mode_init(&state);
    city_location_output_t output =
        step(&state, CITY_SCAN_EMPTY, 0U, NULL, NULL);
    CHECK(output.event == CITY_LOCATION_EVENT_WILD);
    CHECK(output.mode == CITY_LOCATION_WILD);
    CHECK(output.region_id == CITY_PLACE_INVALID_ID);

    const uint64_t sparse_tokens[3] = {1U, 2U, 3U};
    const city_place_fingerprint_t sparse =
        fingerprint_from(sparse_tokens, 3U);
    output = step(
        &state, CITY_SCAN_EVIDENCE, 1U, &sparse, NULL);
    CHECK(output.event == CITY_LOCATION_EVENT_WILD);
}

static void test_known_place_and_lock(void)
{
    const uint64_t saved_tokens[10] =
        {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U};
    const uint64_t query_tokens[10] =
        {1U, 2U, 3U, 4U, 5U, 6U, 7U, 20U, 21U, 22U};
    const city_place_fingerprint_t saved =
        fingerprint_from(saved_tokens, 10U);
    const city_place_fingerprint_t query =
        fingerprint_from(query_tokens, 10U);
    city_place_catalog_t catalog;
    memset(&catalog, 0, sizeof(catalog));
    catalog.count = 1U;
    catalog.places[0].place_id = 5U;
    catalog.places[0].fingerprint = saved;

    city_location_state_t state;
    city_location_mode_init(&state);
    city_location_output_t output = step(
        &state, CITY_SCAN_EVIDENCE, 100U, &query, &catalog);
    CHECK(output.event == CITY_LOCATION_EVENT_KNOWN_PLACE);
    CHECK(output.mode == CITY_LOCATION_KNOWN_PLACE);
    CHECK(output.region_id == 5U);
    CHECK(output.confidence_permille == 700U);

    const uint64_t locked_until = state.locked_until_ms;
    output = step(
        &state, CITY_SCAN_EVIDENCE, 500U, &query, &catalog);
    CHECK(output.event == CITY_LOCATION_EVENT_LOCKED);
    CHECK(state.locked_until_ms == locked_until);

    output = step(&state, CITY_SCAN_EMPTY, 1000U, NULL, &catalog);
    CHECK(output.event == CITY_LOCATION_EVENT_LOCKED);
    CHECK(output.mode == CITY_LOCATION_KNOWN_PLACE);
    CHECK(output.region_id == 5U);

    output = step(
        &state,
        CITY_SCAN_EMPTY,
        100U + CITY_LOCATION_LOCK_DURATION_MS,
        NULL,
        &catalog);
    CHECK(output.event == CITY_LOCATION_EVENT_WILD);
    CHECK(output.mode == CITY_LOCATION_WILD);
}

static void test_lock_allows_strong_new_place_evidence(void)
{
    const uint64_t saved_tokens[4] = {1U, 2U, 3U, 4U};
    const uint64_t new_tokens[4] = {11U, 12U, 13U, 14U};
    const city_place_fingerprint_t saved =
        fingerprint_from(saved_tokens, 4U);
    const city_place_fingerprint_t new_place =
        fingerprint_from(new_tokens, 4U);
    city_place_catalog_t catalog;
    memset(&catalog, 0, sizeof(catalog));
    catalog.count = 1U;
    catalog.places[0].place_id = 5U;
    catalog.places[0].fingerprint = saved;
    city_location_state_t state;
    city_location_mode_init(&state);

    CHECK(step(&state, CITY_SCAN_EVIDENCE, 100U, &saved, &catalog)
              .event == CITY_LOCATION_EVENT_KNOWN_PLACE);
    city_location_output_t output =
        step(&state, CITY_SCAN_EVIDENCE, 1000U, &new_place, &catalog);

    CHECK(output.event == CITY_LOCATION_EVENT_NEW_PENDING);
    CHECK(output.mode == CITY_LOCATION_CANDIDATE);
    CHECK(state.candidate_valid);
    CHECK(state.candidate_started_ms == 1000U);

    output = step(
        &state,
        CITY_SCAN_EVIDENCE,
        1000U + CITY_LOCATION_CONFIRM_DELAY_MS,
        &new_place,
        &catalog);
    CHECK(output.event == CITY_LOCATION_EVENT_NEW_PLACE_READY);
}

static void test_lock_allows_known_place_change(void)
{
    const uint64_t first_tokens[4] = {1U, 2U, 3U, 4U};
    const uint64_t second_tokens[4] = {11U, 12U, 13U, 14U};
    const city_place_fingerprint_t first =
        fingerprint_from(first_tokens, 4U);
    const city_place_fingerprint_t second =
        fingerprint_from(second_tokens, 4U);
    city_place_catalog_t catalog;
    memset(&catalog, 0, sizeof(catalog));
    catalog.count = 2U;
    catalog.places[0].place_id = 5U;
    catalog.places[0].fingerprint = first;
    catalog.places[1].place_id = 6U;
    catalog.places[1].fingerprint = second;
    city_location_state_t state;
    city_location_mode_init(&state);

    CHECK(step(&state, CITY_SCAN_EVIDENCE, 100U, &first, &catalog)
              .event == CITY_LOCATION_EVENT_KNOWN_PLACE);
    const city_location_output_t output =
        step(&state, CITY_SCAN_EVIDENCE, 1000U, &second, &catalog);

    CHECK(output.event == CITY_LOCATION_EVENT_KNOWN_PLACE);
    CHECK(output.mode == CITY_LOCATION_KNOWN_PLACE);
    CHECK(output.region_id == 6U);
    CHECK(state.locked_until_ms ==
          1000U + CITY_LOCATION_LOCK_DURATION_MS);
}

static void test_lock_suppresses_gray_evidence(void)
{
    const uint64_t saved_tokens[10] =
        {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U};
    const uint64_t gray_tokens[10] =
        {1U, 2U, 3U, 4U, 5U, 6U, 20U, 21U, 22U, 23U};
    const city_place_fingerprint_t saved =
        fingerprint_from(saved_tokens, 10U);
    const city_place_fingerprint_t gray =
        fingerprint_from(gray_tokens, 10U);
    city_place_catalog_t catalog;
    memset(&catalog, 0, sizeof(catalog));
    catalog.count = 1U;
    catalog.places[0].place_id = 5U;
    catalog.places[0].fingerprint = saved;
    city_location_state_t state;
    city_location_mode_init(&state);

    CHECK(step(&state, CITY_SCAN_EVIDENCE, 100U, &saved, &catalog)
              .event == CITY_LOCATION_EVENT_KNOWN_PLACE);
    const city_location_state_t before = state;
    const city_location_output_t output =
        step(&state, CITY_SCAN_EVIDENCE, 1000U, &gray, &catalog);

    CHECK(output.event == CITY_LOCATION_EVENT_LOCKED);
    CHECK(output.mode == CITY_LOCATION_KNOWN_PLACE);
    CHECK(output.region_id == 5U);
    CHECK(memcmp(&state, &before, sizeof(state)) == 0);
}

static void test_gray_zone_never_creates_candidate(void)
{
    const uint64_t saved_tokens[10] =
        {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U};
    const uint64_t query_tokens[10] =
        {1U, 2U, 3U, 4U, 5U, 6U, 20U, 21U, 22U, 23U};
    city_place_catalog_t catalog;
    memset(&catalog, 0, sizeof(catalog));
    catalog.count = 1U;
    catalog.places[0].place_id = 5U;
    catalog.places[0].fingerprint =
        fingerprint_from(saved_tokens, 10U);
    const city_place_fingerprint_t query =
        fingerprint_from(query_tokens, 10U);
    city_location_state_t state;
    city_location_mode_init(&state);

    const city_location_output_t output = step(
        &state, CITY_SCAN_EVIDENCE, 0U, &query, &catalog);
    CHECK(output.event == CITY_LOCATION_EVENT_GRAY_ZONE);
    CHECK(output.mode == CITY_LOCATION_GRAY_ZONE);
    CHECK(output.confidence_permille == 600U);
    CHECK(!state.candidate_valid);
}

static void test_consistent_candidate_requires_twenty_seconds(void)
{
    const uint64_t tokens[4] = {1U, 2U, 3U, 4U};
    const city_place_fingerprint_t candidate =
        fingerprint_from(tokens, 4U);
    city_place_catalog_t empty_catalog;
    memset(&empty_catalog, 0, sizeof(empty_catalog));
    city_location_state_t state;
    city_location_mode_init(&state);

    city_location_output_t output = step(
        &state, CITY_SCAN_EVIDENCE, 1000U, &candidate, &empty_catalog);
    CHECK(output.event == CITY_LOCATION_EVENT_NEW_PENDING);
    CHECK(!city_location_mode_commit_place(&state, 8U, 1000U));
    output = step(
        &state,
        CITY_SCAN_EVIDENCE,
        1000U + CITY_LOCATION_CONFIRM_DELAY_MS - 1U,
        &candidate,
        &empty_catalog);
    CHECK(output.event == CITY_LOCATION_EVENT_NEW_PENDING);
    output = step(
        &state,
        CITY_SCAN_EVIDENCE,
        1000U + CITY_LOCATION_CONFIRM_DELAY_MS,
        &candidate,
        &empty_catalog);
    CHECK(output.event == CITY_LOCATION_EVENT_NEW_PLACE_READY);
    CHECK(state.candidate_valid);
    CHECK(state.mode == CITY_LOCATION_CANDIDATE);
}

static void test_missing_catalog_is_not_treated_as_empty(void)
{
    const uint64_t tokens[4] = {1U, 2U, 3U, 4U};
    const city_place_fingerprint_t fingerprint =
        fingerprint_from(tokens, 4U);
    city_location_state_t state;
    city_location_mode_init(&state);
    const city_location_state_t before = state;

    const city_location_output_t output = step(
        &state, CITY_SCAN_EVIDENCE, 0U, &fingerprint, NULL);
    CHECK(output.event == CITY_LOCATION_EVENT_INVALID_INPUT);
    CHECK(memcmp(&state, &before, sizeof(state)) == 0);
}

static void test_inconsistent_candidate_restarts_confirmation(void)
{
    const uint64_t first_tokens[4] = {1U, 2U, 3U, 4U};
    const uint64_t second_tokens[4] = {10U, 11U, 12U, 13U};
    const city_place_fingerprint_t first =
        fingerprint_from(first_tokens, 4U);
    const city_place_fingerprint_t second =
        fingerprint_from(second_tokens, 4U);
    city_place_catalog_t empty_catalog;
    memset(&empty_catalog, 0, sizeof(empty_catalog));
    city_location_state_t state;
    city_location_mode_init(&state);

    CHECK(step(
              &state, CITY_SCAN_EVIDENCE, 0U, &first, &empty_catalog)
              .event == CITY_LOCATION_EVENT_NEW_PENDING);
    CHECK(step(
              &state,
              CITY_SCAN_EVIDENCE,
              CITY_LOCATION_CONFIRM_DELAY_MS,
              &second,
              &empty_catalog)
              .event == CITY_LOCATION_EVENT_NEW_PENDING);
    CHECK(state.candidate_started_ms == CITY_LOCATION_CONFIRM_DELAY_MS);
    CHECK(step(
              &state,
              CITY_SCAN_EVIDENCE,
              (2U * CITY_LOCATION_CONFIRM_DELAY_MS) - 1U,
              &second,
              &empty_catalog)
              .event == CITY_LOCATION_EVENT_NEW_PENDING);
    CHECK(step(
              &state,
              CITY_SCAN_EVIDENCE,
              2U * CITY_LOCATION_CONFIRM_DELAY_MS,
              &second,
              &empty_catalog)
              .event == CITY_LOCATION_EVENT_NEW_PLACE_READY);
}

static void test_commit_is_explicit_and_starts_lock(void)
{
    const uint64_t tokens[4] = {1U, 2U, 3U, 4U};
    const city_place_fingerprint_t candidate =
        fingerprint_from(tokens, 4U);
    city_place_catalog_t empty_catalog;
    memset(&empty_catalog, 0, sizeof(empty_catalog));
    city_location_state_t state;
    city_location_mode_init(&state);

    CHECK(!city_location_mode_commit_place(&state, 3U, 0U));
    CHECK(step(
              &state, CITY_SCAN_EVIDENCE, 0U, &candidate, &empty_catalog)
              .event == CITY_LOCATION_EVENT_NEW_PENDING);
    CHECK(step(
              &state,
              CITY_SCAN_EVIDENCE,
              CITY_LOCATION_CONFIRM_DELAY_MS,
              &candidate,
              &empty_catalog)
              .event == CITY_LOCATION_EVENT_NEW_PLACE_READY);
    CHECK(city_location_mode_commit_place(
        &state, 3U, CITY_LOCATION_CONFIRM_DELAY_MS));
    CHECK(state.mode == CITY_LOCATION_KNOWN_PLACE);
    CHECK(state.region_id == 3U);
    CHECK(!state.candidate_valid);
    CHECK(state.locked_until_ms ==
          CITY_LOCATION_CONFIRM_DELAY_MS +
              CITY_LOCATION_LOCK_DURATION_MS);
}

int main(void)
{
    test_scan_error_preserves_state();
    test_empty_and_sparse_scans_enter_wild();
    test_known_place_and_lock();
    test_lock_allows_strong_new_place_evidence();
    test_lock_allows_known_place_change();
    test_lock_suppresses_gray_evidence();
    test_gray_zone_never_creates_candidate();
    test_consistent_candidate_requires_twenty_seconds();
    test_missing_catalog_is_not_treated_as_empty();
    test_inconsistent_candidate_restarts_confirmation();
    test_commit_is_explicit_and_starts_lock();

    if (failures == 0) {
        puts("test_location_mode: ALL PASS");
        return 0;
    }
    fprintf(stderr, "test_location_mode: %d failure(s)\n", failures);
    return 1;
}
