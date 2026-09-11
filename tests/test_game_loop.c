#include "game_loop.h"

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

typedef struct {
    bool succeed;
    unsigned int calls;
} game_store_t;

static bool game_persist(
    const city_bestiary_t *next,
    void *context)
{
    game_store_t *store = context;
    ++store->calls;
    return store->succeed &&
           city_bestiary_captured_count(next) > 0U;
}

static uint64_t target_time(const city_game_session_t *session)
{
    return session->capture_round.started_ms +
           session->capture_round.target_center_ms;
}

static void test_complete_loop_opens_captured_bestiary(void)
{
    city_game_session_t session;
    city_game_init(&session);
    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);
    game_store_t store = {.succeed = true};

    CHECK(city_game_arrive(&session, 1U, 1001U, 42U) ==
          CITY_GAME_EVENT_ENCOUNTER_STARTED);
    CHECK(session.species_id == CITY_SPECIES_CHARMANDER);
    CHECK(city_game_begin_capture(&session, 500U) ==
          CITY_GAME_EVENT_CAPTURE_STARTED);
    CHECK(city_game_throw(
              &session,
              target_time(&session),
              &bestiary,
              game_persist,
              &store) == CITY_GAME_EVENT_CAPTURED);
    CHECK(session.stage == CITY_GAME_CAPTURED);
    CHECK(bestiary.charmander.capture_count == 1U);
    CHECK(store.calls == 1U);
    CHECK(city_game_open_bestiary(&session) ==
          CITY_GAME_EVENT_BESTIARY_OPENED);
    CHECK(session.stage == CITY_GAME_BESTIARY);
}

static void test_selected_species_and_stats_are_persisted(void)
{
    city_game_session_t session;
    city_game_init(&session);
    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);
    game_store_t store = {.succeed = true};
    const city_creature_stats_t stats = {
        .hp = 54U, .attack = 55U, .defense = 76U};

    CHECK(city_game_arrive_with_species(
              &session, 3U, CITY_SPECIES_SQUIRTLE, &stats,
              1005U, 77U) == CITY_GAME_EVENT_ENCOUNTER_STARTED);
    CHECK(session.species_id == CITY_SPECIES_SQUIRTLE);
    CHECK(session.stats.defense == 76U);
    CHECK(city_game_begin_capture(&session, 500U) ==
          CITY_GAME_EVENT_CAPTURE_STARTED);
    CHECK(city_game_throw(
              &session, target_time(&session), &bestiary,
              game_persist, &store) == CITY_GAME_EVENT_CAPTURED);
    CHECK(bestiary.squirtle.capture_count == 1U);
    CHECK(bestiary.squirtle.latest_stats.hp == 54U);
    CHECK(bestiary.squirtle.best_stats.defense == 76U);
}

static void test_three_misses_end_encounter_without_reward(void)
{
    city_game_session_t session;
    city_game_init(&session);
    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);
    game_store_t store = {.succeed = true};

    CHECK(city_game_arrive(&session, 1U, 1002U, 1U) ==
          CITY_GAME_EVENT_ENCOUNTER_STARTED);
    CHECK(city_game_begin_capture(&session, 100U) ==
          CITY_GAME_EVENT_CAPTURE_STARTED);
    CHECK(city_game_throw(
              &session, 100U, &bestiary, game_persist, &store) ==
          CITY_GAME_EVENT_THROW_MISSED);
    CHECK(session.attempts_remaining == 2U);
    CHECK(city_game_throw(
              &session, 100U, &bestiary, game_persist, &store) ==
          CITY_GAME_EVENT_THROW_MISSED);
    CHECK(session.attempts_remaining == 1U);
    CHECK(city_game_throw(
              &session, 100U, &bestiary, game_persist, &store) ==
          CITY_GAME_EVENT_ESCAPED);
    CHECK(session.stage == CITY_GAME_ESCAPED);
    CHECK(bestiary.charmander.capture_count == 0U);
    CHECK(store.calls == 0U);
}

static void test_idle_timeouts_consume_three_attempts(void)
{
    city_game_session_t session;
    city_game_init(&session);
    CHECK(city_game_arrive(&session, 1U, 1003U, 9U) ==
          CITY_GAME_EVENT_ENCOUNTER_STARTED);
    CHECK(city_game_begin_capture(&session, 100U) ==
          CITY_GAME_EVENT_CAPTURE_STARTED);
    CHECK(session.capture_deadline_ms ==
          100U + CITY_GAME_CAPTURE_BUDGET_MS);

    CHECK(city_game_tick(
              &session,
              100U + CITY_CAPTURE_DURATION_MS - 1U) ==
          CITY_GAME_EVENT_NONE);
    uint64_t now = 100U + CITY_CAPTURE_DURATION_MS;
    CHECK(city_game_tick(&session, now) ==
          CITY_GAME_EVENT_ATTEMPT_TIMED_OUT);
    CHECK(session.attempts_remaining == 2U);

    now += CITY_CAPTURE_DURATION_MS;
    CHECK(city_game_tick(&session, now) ==
          CITY_GAME_EVENT_ATTEMPT_TIMED_OUT);
    CHECK(session.attempts_remaining == 1U);

    now += CITY_CAPTURE_DURATION_MS;
    CHECK(city_game_tick(&session, now) == CITY_GAME_EVENT_ESCAPED);
    CHECK(session.stage == CITY_GAME_ESCAPED);
    CHECK(session.attempts_remaining == 0U);
    CHECK(now - session.capture_started_ms <=
          CITY_GAME_CAPTURE_BUDGET_MS);
}

static void test_shared_deadline_forces_escape(void)
{
    city_game_session_t session;
    city_game_init(&session);
    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);
    game_store_t store = {.succeed = true};

    CHECK(city_game_arrive(&session, 1U, 1004U, 12U) ==
          CITY_GAME_EVENT_ENCOUNTER_STARTED);
    CHECK(city_game_begin_capture(&session, 500U) ==
          CITY_GAME_EVENT_CAPTURE_STARTED);
    CHECK(city_game_throw(
              &session,
              500U + CITY_GAME_CAPTURE_BUDGET_MS,
              &bestiary,
              game_persist,
              &store) == CITY_GAME_EVENT_ESCAPED);
    CHECK(store.calls == 0U);
    CHECK(bestiary.charmander.capture_count == 0U);
}

static void test_storage_failure_is_retryable_and_not_visible(void)
{
    city_game_session_t session;
    city_game_init(&session);
    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);
    game_store_t store = {.succeed = false};

    CHECK(city_game_arrive(&session, 2U, 1003U, 3U) ==
          CITY_GAME_EVENT_ENCOUNTER_STARTED);
    CHECK(city_game_begin_capture(&session, 1000U) ==
          CITY_GAME_EVENT_CAPTURE_STARTED);
    CHECK(city_game_throw(
              &session,
              target_time(&session),
              &bestiary,
              game_persist,
              &store) == CITY_GAME_EVENT_STORAGE_FAILED);
    CHECK(session.stage == CITY_GAME_STORAGE_ERROR);
    CHECK(session.reward_pending);
    CHECK(bestiary.charmander.state == CITY_DISCOVERY_UNKNOWN);
    CHECK(bestiary.charmander.capture_count == 0U);

    store.succeed = true;
    CHECK(city_game_retry_persist(
              &session, &bestiary, game_persist, &store) ==
          CITY_GAME_EVENT_CAPTURED);
    CHECK(session.stage == CITY_GAME_CAPTURED);
    CHECK(!session.reward_pending);
    CHECK(bestiary.charmander.capture_count == 1U);
    CHECK(store.calls == 2U);
}

static void test_duplicate_event_does_not_increment_count(void)
{
    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);
    game_store_t store = {.succeed = true};
    CHECK(city_bestiary_capture(
              &bestiary,
              1004U,
              CITY_SPECIES_CHARMANDER,
              1U,
              game_persist,
              &store) == CITY_BESTIARY_APPLIED);

    city_game_session_t session;
    city_game_init(&session);
    CHECK(city_game_arrive(&session, 1U, 1004U, 5U) ==
          CITY_GAME_EVENT_ENCOUNTER_STARTED);
    CHECK(city_game_begin_capture(&session, 0U) ==
          CITY_GAME_EVENT_CAPTURE_STARTED);
    CHECK(city_game_throw(
              &session,
              target_time(&session),
              &bestiary,
              game_persist,
              &store) == CITY_GAME_EVENT_CAPTURED);
    CHECK(bestiary.charmander.capture_count == 1U);
    CHECK(store.calls == 1U);
}

static void test_abandon_before_and_during_capture_never_rewards(void)
{
    city_game_session_t session;
    city_game_init(&session);
    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);
    game_store_t store = {.succeed = true};

    CHECK(city_game_arrive(&session, 1U, 1006U, 21U) ==
          CITY_GAME_EVENT_ENCOUNTER_STARTED);
    CHECK(city_game_abandon(&session) == CITY_GAME_EVENT_ABANDONED);
    CHECK(session.stage == CITY_GAME_ABANDONED);
    CHECK(session.attempts_remaining == 0U);
    CHECK(!session.reward_pending);
    CHECK(store.calls == 0U);
    CHECK(bestiary.charmander.capture_count == 0U);
    CHECK(city_game_begin_capture(&session, 500U) == CITY_GAME_EVENT_INVALID);

    city_game_init(&session);
    CHECK(city_game_arrive(&session, 2U, 1007U, 22U) ==
          CITY_GAME_EVENT_ENCOUNTER_STARTED);
    CHECK(city_game_begin_capture(&session, 600U) ==
          CITY_GAME_EVENT_CAPTURE_STARTED);
    CHECK(city_game_abandon(&session) == CITY_GAME_EVENT_ABANDONED);
    CHECK(session.stage == CITY_GAME_ABANDONED);
    CHECK(!session.capture_round.active);
    CHECK(store.calls == 0U);
    CHECK(bestiary.charmander.capture_count == 0U);
    CHECK(city_game_throw(
              &session, 600U, &bestiary, game_persist, &store) ==
          CITY_GAME_EVENT_INVALID);
}

static void test_invalid_transitions_do_not_mutate_state(void)
{
    city_game_session_t session;
    city_game_init(&session);
    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);
    game_store_t store = {.succeed = true};

    CHECK(city_game_begin_capture(&session, 0U) ==
          CITY_GAME_EVENT_INVALID);
    CHECK(city_game_open_bestiary(&session) ==
          CITY_GAME_EVENT_INVALID);
    CHECK(city_game_abandon(&session) == CITY_GAME_EVENT_INVALID);
    CHECK(city_game_throw(
              &session, 0U, &bestiary, game_persist, &store) ==
          CITY_GAME_EVENT_INVALID);
    CHECK(city_game_arrive(
              &session, CITY_PLACE_INVALID_ID, 1U, 0U) ==
          CITY_GAME_EVENT_INVALID);
    CHECK(city_game_arrive(&session, 1U, 0U, 0U) ==
          CITY_GAME_EVENT_INVALID);
    CHECK(session.stage == CITY_GAME_WAITING_FOR_PLACE);
}

int main(void)
{
    test_complete_loop_opens_captured_bestiary();
    test_selected_species_and_stats_are_persisted();
    test_three_misses_end_encounter_without_reward();
    test_idle_timeouts_consume_three_attempts();
    test_shared_deadline_forces_escape();
    test_storage_failure_is_retryable_and_not_visible();
    test_duplicate_event_does_not_increment_count();
    test_abandon_before_and_during_capture_never_rewards();
    test_invalid_transitions_do_not_mutate_state();

    if (failures == 0) {
        puts("test_game_loop: ALL PASS");
        return 0;
    }
    fprintf(stderr, "test_game_loop: %d failure(s)\n", failures);
    return 1;
}
