#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CITY_CAPTURE_DURATION_MS UINT32_C(12000)
#define CITY_CAPTURE_WINDOW_HALF_MS UINT32_C(900)

typedef struct {
    uint64_t started_ms;
    uint32_t target_center_ms;
    uint32_t target_half_width_ms;
    uint32_t duration_ms;
    bool active;
} city_capture_round_t;

typedef enum {
    CITY_CAPTURE_INVALID = 0,
    CITY_CAPTURE_HIT,
    CITY_CAPTURE_MISS,
    CITY_CAPTURE_TIMEOUT,
} city_capture_result_t;

bool city_capture_begin(
    city_capture_round_t *round,
    uint32_t seed,
    uint64_t now_ms);

uint16_t city_capture_progress_permille(
    const city_capture_round_t *round,
    uint64_t now_ms);

city_capture_result_t city_capture_throw(
    city_capture_round_t *round,
    uint64_t now_ms);
