#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CITY_WILD_REWARD_COOLDOWN_MS UINT64_C(1800000)
#define CITY_WILD_REWARD_SCHEMA_VERSION 1U

typedef struct {
    uint16_t schema_version;
    bool cooldown_active;
} city_wild_reward_snapshot_t;

typedef struct {
    bool blocked;
    uint64_t blocked_until_ms;
} city_wild_reward_guard_t;

void city_wild_reward_guard_init(
    city_wild_reward_guard_t *guard,
    const city_wild_reward_snapshot_t *snapshot,
    uint64_t now_ms);

bool city_wild_reward_available(
    city_wild_reward_guard_t *guard,
    uint64_t now_ms);

bool city_wild_reward_settle(
    city_wild_reward_guard_t *guard,
    uint64_t now_ms);

city_wild_reward_snapshot_t city_wild_reward_snapshot(
    const city_wild_reward_guard_t *guard,
    uint64_t now_ms);

uint64_t city_wild_reward_remaining_ms(const city_wild_reward_guard_t *guard, uint64_t now_ms);
