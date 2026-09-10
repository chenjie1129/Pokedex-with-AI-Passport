#include "wild_reward_guard.h"

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

static void test_first_reward_starts_cooldown(void)
{
    city_wild_reward_guard_t guard;
    city_wild_reward_guard_init(&guard, NULL, 100U);

    CHECK(city_wild_reward_available(&guard, 100U));
    CHECK(city_wild_reward_settle(&guard, 100U));
    CHECK(!city_wild_reward_available(
        &guard, 100U + CITY_WILD_REWARD_COOLDOWN_MS - 1U));
    CHECK(!city_wild_reward_settle(
        &guard, 100U + CITY_WILD_REWARD_COOLDOWN_MS - 1U));
    CHECK(city_wild_reward_available(
        &guard, 100U + CITY_WILD_REWARD_COOLDOWN_MS));
}

static void test_active_snapshot_blocks_after_restart(void)
{
    const city_wild_reward_snapshot_t snapshot = {
        .schema_version = CITY_WILD_REWARD_SCHEMA_VERSION,
        .cooldown_active = true,
    };
    city_wild_reward_guard_t guard;
    city_wild_reward_guard_init(&guard, &snapshot, 500U);

    CHECK(!city_wild_reward_available(&guard, 500U));
    CHECK(!city_wild_reward_available(
        &guard, 500U + CITY_WILD_REWARD_COOLDOWN_MS - 1U));
    CHECK(city_wild_reward_available(
        &guard, 500U + CITY_WILD_REWARD_COOLDOWN_MS));
}

static void test_unknown_snapshot_version_fails_closed(void)
{
    const city_wild_reward_snapshot_t snapshot = {
        .schema_version =
            (uint16_t)(CITY_WILD_REWARD_SCHEMA_VERSION + 1U),
        .cooldown_active = false,
    };
    city_wild_reward_guard_t guard;
    city_wild_reward_guard_init(&guard, &snapshot, 0U);

    CHECK(!city_wild_reward_available(&guard, 0U));
    CHECK(city_wild_reward_available(
        &guard, CITY_WILD_REWARD_COOLDOWN_MS));
}

static void test_snapshot_only_marks_live_cooldown(void)
{
    city_wild_reward_guard_t guard;
    city_wild_reward_guard_init(&guard, NULL, 0U);
    CHECK(city_wild_reward_settle(&guard, 0U));

    city_wild_reward_snapshot_t snapshot =
        city_wild_reward_snapshot(&guard, 1U);
    CHECK(snapshot.schema_version == CITY_WILD_REWARD_SCHEMA_VERSION);
    CHECK(snapshot.cooldown_active);

    snapshot = city_wild_reward_snapshot(
        &guard, CITY_WILD_REWARD_COOLDOWN_MS);
    CHECK(!snapshot.cooldown_active);
}

static void test_invalid_guard_is_never_available(void)
{
    CHECK(!city_wild_reward_available(NULL, 0U));
    CHECK(!city_wild_reward_settle(NULL, 0U));
    const city_wild_reward_snapshot_t snapshot =
        city_wild_reward_snapshot(NULL, 0U);
    CHECK(snapshot.schema_version == CITY_WILD_REWARD_SCHEMA_VERSION);
    CHECK(!snapshot.cooldown_active);
}

int main(void)
{
    test_first_reward_starts_cooldown();
    test_active_snapshot_blocks_after_restart();
    test_unknown_snapshot_version_fails_closed();
    test_snapshot_only_marks_live_cooldown();
    test_invalid_guard_is_never_available();

    if (failures == 0) {
        puts("test_wild_reward_guard: ALL PASS");
        return 0;
    }
    fprintf(stderr, "test_wild_reward_guard: %d failure(s)\n", failures);
    return 1;
}
