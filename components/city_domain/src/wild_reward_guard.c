#include "wild_reward_guard.h"

#include <stddef.h>

static uint64_t saturating_add_ms(uint64_t now_ms, uint64_t duration_ms)
{
    if (UINT64_MAX - now_ms < duration_ms) {
        return UINT64_MAX;
    }
    return now_ms + duration_ms;
}

void city_wild_reward_guard_init(
    city_wild_reward_guard_t *guard,
    const city_wild_reward_snapshot_t *snapshot,
    uint64_t now_ms)
{
    if (guard == NULL) {
        return;
    }

    guard->blocked =
        snapshot != NULL &&
        (snapshot->schema_version != CITY_WILD_REWARD_SCHEMA_VERSION ||
         snapshot->cooldown_active);
    guard->blocked_until_ms =
        guard->blocked
            ? saturating_add_ms(now_ms, CITY_WILD_REWARD_COOLDOWN_MS)
            : 0U;
}

bool city_wild_reward_available(
    city_wild_reward_guard_t *guard,
    uint64_t now_ms)
{
    if (guard == NULL) {
        return false;
    }
    if (guard->blocked && guard->blocked_until_ms != UINT64_MAX &&
        now_ms >= guard->blocked_until_ms) {
        guard->blocked = false;
        guard->blocked_until_ms = 0U;
    }
    return !guard->blocked;
}

bool city_wild_reward_settle(
    city_wild_reward_guard_t *guard,
    uint64_t now_ms)
{
    if (!city_wild_reward_available(guard, now_ms)) {
        return false;
    }
    guard->blocked = true;
    guard->blocked_until_ms =
        saturating_add_ms(now_ms, CITY_WILD_REWARD_COOLDOWN_MS);
    return true;
}

city_wild_reward_snapshot_t city_wild_reward_snapshot(
    const city_wild_reward_guard_t *guard,
    uint64_t now_ms)
{
    const city_wild_reward_snapshot_t snapshot = {
        .schema_version = CITY_WILD_REWARD_SCHEMA_VERSION,
        .cooldown_active =
            guard != NULL && guard->blocked &&
            (guard->blocked_until_ms == UINT64_MAX ||
             now_ms < guard->blocked_until_ms),
    };
    return snapshot;
}
