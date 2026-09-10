#include "capture_engine.h"

#include <stddef.h>

#define CITY_CAPTURE_TARGET_MIN_MS UINT32_C(4000)
#define CITY_CAPTURE_TARGET_SPAN_MS UINT32_C(4001)

bool city_capture_begin(
    city_capture_round_t *round,
    uint32_t seed,
    uint64_t now_ms)
{
    if (round == NULL) {
        return false;
    }

    round->started_ms = now_ms;
    round->target_center_ms =
        CITY_CAPTURE_TARGET_MIN_MS + (seed % CITY_CAPTURE_TARGET_SPAN_MS);
    round->target_half_width_ms = CITY_CAPTURE_WINDOW_HALF_MS;
    round->duration_ms = CITY_CAPTURE_DURATION_MS;
    round->active = true;
    return true;
}

uint16_t city_capture_progress_permille(
    const city_capture_round_t *round,
    uint64_t now_ms)
{
    if (round == NULL || !round->active || now_ms <= round->started_ms) {
        return 0U;
    }

    const uint64_t elapsed = now_ms - round->started_ms;
    if (elapsed >= round->duration_ms) {
        return 1000U;
    }
    return (uint16_t)((elapsed * 1000U) / round->duration_ms);
}

city_capture_result_t city_capture_throw(
    city_capture_round_t *round,
    uint64_t now_ms)
{
    if (round == NULL || !round->active || now_ms < round->started_ms) {
        return CITY_CAPTURE_INVALID;
    }

    const uint64_t elapsed = now_ms - round->started_ms;
    round->active = false;
    if (elapsed >= round->duration_ms) {
        return CITY_CAPTURE_TIMEOUT;
    }

    const uint32_t window_start =
        round->target_center_ms - round->target_half_width_ms;
    const uint32_t window_end =
        round->target_center_ms + round->target_half_width_ms;
    return elapsed >= window_start && elapsed <= window_end
               ? CITY_CAPTURE_HIT
               : CITY_CAPTURE_MISS;
}
