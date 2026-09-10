#include "capture_engine.h"

#include <stdio.h>

static int failures;

#define CHECK(condition)                                                  \
    do {                                                                  \
        if (!(condition)) {                                               \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,       \
                    #condition);                                          \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

static void test_seed_produces_bounded_deterministic_target(void)
{
    city_capture_round_t first;
    city_capture_round_t second;
    CHECK(city_capture_begin(&first, 42U, 1000U));
    CHECK(city_capture_begin(&second, 42U, 9000U));
    CHECK(first.target_center_ms == second.target_center_ms);
    CHECK(first.target_center_ms >= 4000U);
    CHECK(first.target_center_ms <= 8000U);
    CHECK(first.target_half_width_ms == CITY_CAPTURE_WINDOW_HALF_MS);
    CHECK(first.duration_ms == CITY_CAPTURE_DURATION_MS);
}

static void test_window_boundaries_are_hits(void)
{
    city_capture_round_t early;
    city_capture_round_t late;
    CHECK(city_capture_begin(&early, 10U, 100U));
    late = early;

    CHECK(city_capture_throw(
              &early,
              early.started_ms + early.target_center_ms -
                  early.target_half_width_ms) == CITY_CAPTURE_HIT);
    CHECK(city_capture_throw(
              &late,
              late.started_ms + late.target_center_ms +
                  late.target_half_width_ms) == CITY_CAPTURE_HIT);
}

static void test_outside_window_is_miss(void)
{
    city_capture_round_t round;
    CHECK(city_capture_begin(&round, 10U, 100U));
    CHECK(city_capture_throw(
              &round,
              round.started_ms + round.target_center_ms -
                  round.target_half_width_ms - 1U) == CITY_CAPTURE_MISS);
    CHECK(!round.active);
}

static void test_deadline_is_timeout(void)
{
    city_capture_round_t round;
    CHECK(city_capture_begin(&round, 10U, 100U));
    CHECK(city_capture_throw(
              &round,
              round.started_ms + CITY_CAPTURE_DURATION_MS) ==
          CITY_CAPTURE_TIMEOUT);
}

static void test_progress_and_invalid_inputs(void)
{
    city_capture_round_t round;
    CHECK(!city_capture_begin(NULL, 0U, 0U));
    CHECK(city_capture_begin(&round, 0U, 100U));
    CHECK(city_capture_progress_permille(&round, 100U) == 0U);
    CHECK(city_capture_progress_permille(
              &round, 100U + CITY_CAPTURE_DURATION_MS / 2U) == 500U);
    CHECK(city_capture_progress_permille(
              &round, 100U + CITY_CAPTURE_DURATION_MS) == 1000U);
    CHECK(city_capture_throw(&round, 99U) == CITY_CAPTURE_INVALID);
    CHECK(city_capture_throw(NULL, 0U) == CITY_CAPTURE_INVALID);
}

int main(void)
{
    test_seed_produces_bounded_deterministic_target();
    test_window_boundaries_are_hits();
    test_outside_window_is_miss();
    test_deadline_is_timeout();
    test_progress_and_invalid_inputs();

    if (failures == 0) {
        puts("test_capture_engine: ALL PASS");
        return 0;
    }
    fprintf(stderr, "test_capture_engine: %d failure(s)\n", failures);
    return 1;
}
