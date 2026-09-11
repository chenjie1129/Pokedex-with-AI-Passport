#include "game_loop.h"

#include <string.h>

static uint64_t saturating_add_ms(uint64_t now_ms, uint32_t duration_ms)
{
    if (UINT64_MAX - now_ms < duration_ms) {
        return UINT64_MAX;
    }
    return now_ms + duration_ms;
}

static city_game_event_t escape_encounter(city_game_session_t *session)
{
    session->attempts_remaining = 0U;
    session->capture_round.active = false;
    session->stage = CITY_GAME_ESCAPED;
    return CITY_GAME_EVENT_ESCAPED;
}

static city_game_event_t advance_after_failed_attempt(
    city_game_session_t *session,
    uint64_t now_ms,
    bool timed_out)
{
    if (session->attempts_remaining > 0U) {
        --session->attempts_remaining;
    }
    if (session->attempts_remaining == 0U ||
        now_ms >= session->capture_deadline_ms) {
        return escape_encounter(session);
    }

    if (!city_capture_begin(
            &session->capture_round,
            session->capture_seed +
                (CITY_GAME_CAPTURE_ATTEMPTS -
                 session->attempts_remaining),
            now_ms)) {
        return CITY_GAME_EVENT_INVALID;
    }
    return timed_out ? CITY_GAME_EVENT_ATTEMPT_TIMED_OUT
                     : CITY_GAME_EVENT_THROW_MISSED;
}

static city_game_event_t persist_pending_reward(
    city_game_session_t *session,
    city_bestiary_t *bestiary,
    city_bestiary_persist_fn persist,
    void *context)
{
    const city_bestiary_result_t result = city_bestiary_capture_with_stats(
        bestiary,
        session->encounter_sequence,
        session->species_id,
        session->place_id,
        &session->stats,
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
    uint64_t encounter_sequence,
    uint32_t seed)
{
    const city_species_definition_t *definition =
        city_species_definition(CITY_SPECIES_CHARMANDER);
    const city_creature_stats_t stats = {
        .hp = definition->base_hp,
        .attack = definition->base_attack,
        .defense = definition->base_defense,
    };
    return city_game_arrive_with_species(
        session, place_id, CITY_SPECIES_CHARMANDER, &stats,
        encounter_sequence, seed);
}

city_game_event_t city_game_arrive_with_species(
    city_game_session_t *session,
    uint16_t place_id,
    uint16_t species_id,
    const city_creature_stats_t *stats,
    uint64_t encounter_sequence,
    uint32_t seed)
{
    if (session == NULL || session->stage != CITY_GAME_WAITING_FOR_PLACE ||
        place_id == CITY_PLACE_INVALID_ID || encounter_sequence == 0U ||
        city_species_definition(species_id) == NULL || stats == NULL ||
        stats->hp == 0U || stats->attack == 0U ||
        stats->defense == 0U) {
        return CITY_GAME_EVENT_INVALID;
    }

    session->stage = CITY_GAME_ENCOUNTER;
    session->place_id = place_id;
    session->species_id = species_id;
    session->stats = *stats;
    session->encounter_sequence = encounter_sequence;
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

    session->capture_started_ms = now_ms;
    session->capture_deadline_ms = saturating_add_ms(
        now_ms, CITY_GAME_CAPTURE_BUDGET_MS);
    session->stage = CITY_GAME_CAPTURE;
    return CITY_GAME_EVENT_CAPTURE_STARTED;
}

city_game_event_t city_game_abandon(city_game_session_t *session)
{
    if (session == NULL ||
        (session->stage != CITY_GAME_ENCOUNTER &&
         session->stage != CITY_GAME_CAPTURE)) {
        return CITY_GAME_EVENT_INVALID;
    }

    session->attempts_remaining = 0U;
    session->capture_round.active = false;
    session->reward_pending = false;
    session->stage = CITY_GAME_ABANDONED;
    return CITY_GAME_EVENT_ABANDONED;
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

    if (now_ms >= session->capture_deadline_ms) {
        return escape_encounter(session);
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

    return advance_after_failed_attempt(
        session, now_ms, result == CITY_CAPTURE_TIMEOUT);
}

city_game_event_t city_game_tick(
    city_game_session_t *session,
    uint64_t now_ms)
{
    if (session == NULL || session->stage != CITY_GAME_CAPTURE ||
        now_ms < session->capture_round.started_ms) {
        return CITY_GAME_EVENT_INVALID;
    }
    if (now_ms >= session->capture_deadline_ms) {
        return escape_encounter(session);
    }
    if (now_ms - session->capture_round.started_ms <
        session->capture_round.duration_ms) {
        return CITY_GAME_EVENT_NONE;
    }

    session->capture_round.active = false;
    return advance_after_failed_attempt(session, now_ms, true);
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
