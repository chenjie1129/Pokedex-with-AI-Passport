#include "game_loop.h"

#include <string.h>

static city_game_event_t persist_pending_reward(
    city_game_session_t *session,
    city_bestiary_t *bestiary,
    city_bestiary_persist_fn persist,
    void *context)
{
    const city_bestiary_result_t result = city_bestiary_capture(
        bestiary,
        session->encounter_id,
        session->species_id,
        session->place_id,
        persist,
        context);

    if (result == CITY_BESTIARY_APPLIED ||
        result == CITY_BESTIARY_DUPLICATE) {
        session->reward_pending = false;
        session->stage = CITY_GAME_CAPTURED;
        return CITY_GAME_EVENT_CAPTURED;
    }
    if (result == CITY_BESTIARY_STORAGE_FAILED) {
        session->stage = CITY_GAME_STORAGE_ERROR;
        return CITY_GAME_EVENT_STORAGE_FAILED;
    }

    session->reward_pending = false;
    session->stage = CITY_GAME_REWARD_ERROR;
    return CITY_GAME_EVENT_REWARD_REJECTED;
}

void city_game_init(city_game_session_t *session)
{
    if (session == NULL) {
        return;
    }
    memset(session, 0, sizeof(*session));
    session->stage = CITY_GAME_WAITING_FOR_PLACE;
    session->place_id = CITY_PLACE_INVALID_ID;
}

city_game_event_t city_game_arrive(
    city_game_session_t *session,
    uint16_t place_id,
    uint64_t encounter_id,
    uint32_t seed)
{
    if (session == NULL || session->stage != CITY_GAME_WAITING_FOR_PLACE ||
        place_id == CITY_PLACE_INVALID_ID || encounter_id == 0U) {
        return CITY_GAME_EVENT_INVALID;
    }

    session->stage = CITY_GAME_ENCOUNTER;
    session->place_id = place_id;
    session->species_id = CITY_SPECIES_CHARMANDER;
    session->encounter_id = encounter_id;
    session->capture_seed = seed;
    session->attempts_remaining = CITY_GAME_CAPTURE_ATTEMPTS;
    session->reward_pending = false;
    return CITY_GAME_EVENT_ENCOUNTER_STARTED;
}

city_game_event_t city_game_begin_capture(
    city_game_session_t *session,
    uint64_t now_ms)
{
    if (session == NULL || session->stage != CITY_GAME_ENCOUNTER ||
        !city_capture_begin(
            &session->capture_round, session->capture_seed, now_ms)) {
        return CITY_GAME_EVENT_INVALID;
    }

    session->stage = CITY_GAME_CAPTURE;
    return CITY_GAME_EVENT_CAPTURE_STARTED;
}

city_game_event_t city_game_throw(
    city_game_session_t *session,
    uint64_t now_ms,
    city_bestiary_t *bestiary,
    city_bestiary_persist_fn persist,
    void *context)
{
    if (session == NULL || session->stage != CITY_GAME_CAPTURE ||
        bestiary == NULL || persist == NULL) {
        return CITY_GAME_EVENT_INVALID;
    }

    const city_capture_result_t result =
        city_capture_throw(&session->capture_round, now_ms);
    if (result == CITY_CAPTURE_INVALID) {
        return CITY_GAME_EVENT_INVALID;
    }
    if (result == CITY_CAPTURE_HIT) {
        session->reward_pending = true;
        return persist_pending_reward(session, bestiary, persist, context);
    }

    if (session->attempts_remaining > 0U) {
        --session->attempts_remaining;
    }
    if (session->attempts_remaining == 0U) {
        session->stage = CITY_GAME_ESCAPED;
        return CITY_GAME_EVENT_ESCAPED;
    }

    city_capture_begin(
        &session->capture_round,
        session->capture_seed +
            (CITY_GAME_CAPTURE_ATTEMPTS - session->attempts_remaining),
        now_ms);
    return CITY_GAME_EVENT_THROW_MISSED;
}

city_game_event_t city_game_retry_persist(
    city_game_session_t *session,
    city_bestiary_t *bestiary,
    city_bestiary_persist_fn persist,
    void *context)
{
    if (session == NULL || session->stage != CITY_GAME_STORAGE_ERROR ||
        !session->reward_pending || bestiary == NULL || persist == NULL) {
        return CITY_GAME_EVENT_INVALID;
    }
    return persist_pending_reward(session, bestiary, persist, context);
}

city_game_event_t city_game_open_bestiary(city_game_session_t *session)
{
    if (session == NULL || session->stage != CITY_GAME_CAPTURED) {
        return CITY_GAME_EVENT_INVALID;
    }
    session->stage = CITY_GAME_BESTIARY;
    return CITY_GAME_EVENT_BESTIARY_OPENED;
}
