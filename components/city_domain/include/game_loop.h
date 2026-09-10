#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bestiary_service.h"
#include "capture_engine.h"
#include "place_fingerprint.h"

#define CITY_GAME_CAPTURE_ATTEMPTS 3U

typedef enum {
    CITY_GAME_WAITING_FOR_PLACE = 0,
    CITY_GAME_ENCOUNTER,
    CITY_GAME_CAPTURE,
    CITY_GAME_CAPTURED,
    CITY_GAME_ESCAPED,
    CITY_GAME_STORAGE_ERROR,
    CITY_GAME_REWARD_ERROR,
    CITY_GAME_BESTIARY,
} city_game_stage_t;

typedef enum {
    CITY_GAME_EVENT_INVALID = 0,
    CITY_GAME_EVENT_ENCOUNTER_STARTED,
    CITY_GAME_EVENT_CAPTURE_STARTED,
    CITY_GAME_EVENT_THROW_MISSED,
    CITY_GAME_EVENT_CAPTURED,
    CITY_GAME_EVENT_ESCAPED,
    CITY_GAME_EVENT_STORAGE_FAILED,
    CITY_GAME_EVENT_REWARD_REJECTED,
    CITY_GAME_EVENT_BESTIARY_OPENED,
} city_game_event_t;

typedef struct {
    city_game_stage_t stage;
    uint16_t place_id;
    uint16_t species_id;
    uint64_t encounter_id;
    uint32_t capture_seed;
    uint8_t attempts_remaining;
    bool reward_pending;
    city_capture_round_t capture_round;
} city_game_session_t;

void city_game_init(city_game_session_t *session);

city_game_event_t city_game_arrive(
    city_game_session_t *session,
    uint16_t place_id,
    uint64_t encounter_id,
    uint32_t seed);

city_game_event_t city_game_begin_capture(
    city_game_session_t *session,
    uint64_t now_ms);

city_game_event_t city_game_throw(
    city_game_session_t *session,
    uint64_t now_ms,
    city_bestiary_t *bestiary,
    city_bestiary_persist_fn persist,
    void *context);

city_game_event_t city_game_retry_persist(
    city_game_session_t *session,
    city_bestiary_t *bestiary,
    city_bestiary_persist_fn persist,
    void *context);

city_game_event_t city_game_open_bestiary(
    city_game_session_t *session);
