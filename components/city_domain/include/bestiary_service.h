#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wild_reward_guard.h"

#define CITY_BESTIARY_SCHEMA_VERSION 5U
#define CITY_BESTIARY_MAGIC UINT32_C(0x31545342)
#define CITY_BESTIARY_ENCODED_BYTES 156U
#define CITY_SPECIES_COUNT 3U
#define CITY_WILD_PLACE_ID 0U
#define CITY_SPECIES_BULBASAUR 1U
#define CITY_SPECIES_CHARMANDER 4U
#define CITY_SPECIES_SQUIRTLE 7U

typedef enum {
    CITY_DISCOVERY_UNKNOWN = 0,
    CITY_DISCOVERY_SEEN,
    CITY_DISCOVERY_CAPTURED,
} city_discovery_state_t;

typedef struct {
    uint16_t species_id;
    const char *name;
    const char *element;
    const char *description;
    uint8_t base_hp;
    uint8_t base_attack;
    uint8_t base_defense;
} city_species_definition_t;

typedef struct {
    uint8_t hp;
    uint8_t attack;
    uint8_t defense;
} city_creature_stats_t;

typedef struct {
    uint16_t species_id;
    city_discovery_state_t state;
    uint32_t capture_count;
    uint16_t last_place_id;
    city_creature_stats_t latest_stats;
    city_creature_stats_t best_stats;
} city_creature_record_t;

typedef struct {
    uint16_t schema_version;
    city_creature_record_t bulbasaur;
    city_creature_record_t charmander;
    city_creature_record_t squirtle;
    uint64_t last_settled_sequence;
    bool wild_cooldown_active;
} city_bestiary_t;

typedef bool (*city_bestiary_persist_fn)(
    const city_bestiary_t *next,
    void *context);

typedef enum {
    CITY_BESTIARY_INVALID = 0,
    CITY_BESTIARY_APPLIED,
    CITY_BESTIARY_UNCHANGED,
    CITY_BESTIARY_DUPLICATE,
    CITY_BESTIARY_COUNTER_FULL,
    CITY_BESTIARY_STORAGE_FAILED,
    CITY_BESTIARY_COOLDOWN,
} city_bestiary_result_t;

const city_species_definition_t *city_species_definition(
    uint16_t species_id);

city_creature_record_t *city_bestiary_record(
    city_bestiary_t *bestiary,
    uint16_t species_id);

const city_creature_record_t *city_bestiary_record_const(
    const city_bestiary_t *bestiary,
    uint16_t species_id);

uint8_t city_bestiary_discovered_count(const city_bestiary_t *bestiary);

uint8_t city_bestiary_captured_count(const city_bestiary_t *bestiary);

void city_bestiary_init(city_bestiary_t *bestiary);

bool city_bestiary_import_legacy_count(
    city_bestiary_t *bestiary,
    uint32_t capture_count);

city_bestiary_result_t city_bestiary_mark_seen(
    city_bestiary_t *bestiary,
    uint16_t species_id,
    city_bestiary_persist_fn persist,
    void *context);

bool city_bestiary_next_encounter_sequence(
    const city_bestiary_t *bestiary,
    uint64_t *sequence);

city_bestiary_result_t city_bestiary_capture(
    city_bestiary_t *bestiary,
    uint64_t encounter_sequence,
    uint16_t species_id,
    uint16_t place_id,
    city_bestiary_persist_fn persist,
    void *context);

city_bestiary_result_t city_bestiary_capture_with_stats(
    city_bestiary_t *bestiary,
    uint64_t encounter_sequence,
    uint16_t species_id,
    uint16_t place_id,
    const city_creature_stats_t *stats,
    city_bestiary_persist_fn persist,
    void *context);

bool city_bestiary_encode(
    const city_bestiary_t *bestiary,
    uint8_t output[CITY_BESTIARY_ENCODED_BYTES]);

bool city_bestiary_decode(
    const uint8_t data[CITY_BESTIARY_ENCODED_BYTES],
    size_t length,
    city_bestiary_t *bestiary);

/* Reserve before showing a Wild encounter; discovery and cooldown share one blob. */
city_bestiary_result_t city_bestiary_reserve_wild(
    city_bestiary_t *bestiary, city_wild_reward_guard_t *guard,
    uint64_t now_ms, uint16_t species_id,
    city_bestiary_persist_fn persist, void *context);
city_bestiary_result_t city_bestiary_clear_wild_cooldown(
    city_bestiary_t *bestiary, city_wild_reward_guard_t *guard,
    uint64_t now_ms, city_bestiary_persist_fn persist, void *context);
