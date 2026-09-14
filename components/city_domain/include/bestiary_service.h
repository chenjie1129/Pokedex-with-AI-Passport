#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wild_reward_guard.h"

#include "species_catalog.h"
#define CITY_BESTIARY_SCHEMA_VERSION 11U
#define CITY_BESTIARY_MAGIC UINT32_C(0x31545342)
#define CITY_BESTIARY_LEGACY_BYTES 156U
#define CITY_BESTIARY_RECORD_BYTES 22U
#define CITY_MAX_OWNED_POKEMON 160U
#define CITY_OWNED_POKEMON_BYTES 20U
#define CITY_BESTIARY_HEADER_BYTES 48U
#define CITY_BESTIARY_ENCODED_BYTES (CITY_BESTIARY_HEADER_BYTES + \
    CITY_SPECIES_COUNT * CITY_BESTIARY_RECORD_BYTES + \
    CITY_MAX_OWNED_POKEMON * CITY_OWNED_POKEMON_BYTES + 4U)
#define CITY_WILD_PLACE_ID 0U

typedef enum {
    CITY_DISCOVERY_UNKNOWN = 0,
    CITY_DISCOVERY_SEEN,
    CITY_DISCOVERY_CAPTURED,
} city_discovery_state_t;

typedef struct {
    uint16_t species_id;
    const char *name;
    const char *element;
    /* Encyclopedia display types; element retains the existing game category. */
    const char *type_label;
    const char *description;
    uint8_t base_hp;
    uint8_t base_attack;
    uint8_t base_defense;
    uint8_t place_pool;
    bool wild_eligible;
    uint16_t evolves_from;
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
    uint16_t friendship;
    uint16_t buddy_places;
    bool evolution_obtained;
    uint8_t current_hp;
} city_creature_record_t;

/* Stable wire IDs: never reorder or use a content-table index as saved meaning. */
typedef enum {
    CITY_PERSONALITY_CURIOUS = 0, CITY_PERSONALITY_BRAVE = 1,
    CITY_PERSONALITY_CALM = 2, CITY_PERSONALITY_PLAYFUL = 3,
    CITY_PERSONALITY_AFFECTIONATE = 4, CITY_PERSONALITY_INDEPENDENT = 5,
    CITY_PERSONALITY_COUNT = 6
} city_personality_t;

typedef struct {
    uint32_t instance_id;
    uint16_t species_id;
    uint16_t place_id;
    city_creature_stats_t stats;
    uint8_t current_hp;
    bool evolved;
    bool migrated;
    uint8_t personality;
    uint8_t friendship;
    uint16_t friendship_places;
    uint16_t last_friendship_place;
} city_owned_pokemon_t;

typedef struct {
    uint16_t schema_version;
    city_creature_record_t records[CITY_SPECIES_COUNT];
    uint64_t last_settled_sequence;
    bool wild_cooldown_active;
    uint16_t buddy_species_id; /* Zero means no buddy selected. */
    uint32_t buddy_instance_id; /* Zero: old species buddy needs explicit copy selection. */
    uint64_t last_visit_sequence;
    uint16_t owned_count;
    uint32_t next_instance_id;
    city_owned_pokemon_t owned[CITY_MAX_OWNED_POKEMON];
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

uint16_t city_species_id_at(uint8_t index);
uint8_t city_species_index(uint16_t species_id);
bool city_bestiary_encounter_status(const city_bestiary_t *bestiary, uint16_t species_id,
                                    city_discovery_state_t *previous_state);
const city_species_definition_t *city_species_definition(
    uint16_t species_id);

city_creature_record_t *city_bestiary_record(
    city_bestiary_t *bestiary,
    uint16_t species_id);

const city_creature_record_t *city_bestiary_record_const(
    const city_bestiary_t *bestiary,
    uint16_t species_id);

bool city_bestiary_is_valid(const city_bestiary_t *bestiary);

uint8_t city_bestiary_discovered_count(const city_bestiary_t *bestiary);

uint8_t city_bestiary_captured_count(const city_bestiary_t *bestiary);
uint16_t city_bestiary_owned_count(
    const city_bestiary_t *bestiary, uint16_t species_id);
const city_owned_pokemon_t *city_bestiary_owned_at(
    const city_bestiary_t *bestiary, uint16_t species_id, uint16_t ordinal);

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
    const uint8_t *data,
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

#define CITY_BUDDY_MAX_FRIENDSHIP 100U
/* Progress belongs to each species and survives switching buddies. */
city_bestiary_result_t city_bestiary_choose_buddy(
    city_bestiary_t *bestiary, uint16_t species_id,
    city_bestiary_persist_fn persist, void *context);

uint8_t city_bestiary_max_hp(const city_creature_record_t *record);
city_bestiary_result_t city_bestiary_apply_damage(
    city_bestiary_t *bestiary, uint16_t species_id, uint8_t damage,
    city_bestiary_persist_fn persist, void *context);
city_bestiary_result_t city_bestiary_recover(
    city_bestiary_t *bestiary, uint16_t species_id,
    city_bestiary_persist_fn persist, void *context);
city_bestiary_result_t city_bestiary_release(
    city_bestiary_t *bestiary, uint16_t species_id,
    city_bestiary_persist_fn persist, void *context);
city_bestiary_result_t city_bestiary_release_instance(
    city_bestiary_t *bestiary, uint32_t instance_id,
    city_bestiary_persist_fn persist, void *context);

#define CITY_EVOLUTION_BOND 30U
#define CITY_EVOLUTION_PLACES 3U
uint16_t city_evolution_target(uint16_t source_id);
uint8_t city_buddy_place_count(const city_creature_record_t *record);
bool city_evolution_ready(const city_bestiary_t *bestiary, uint16_t source_id);
city_bestiary_result_t city_bestiary_evolve(city_bestiary_t *bestiary,
    uint16_t source_id, city_bestiary_persist_fn persist, void *context);

/* All new companion mutations target a stable ID. Species APIs are legacy only. */
const city_owned_pokemon_t *city_bestiary_owned_by_id(const city_bestiary_t *, uint32_t);
city_bestiary_result_t city_bestiary_choose_buddy_instance(city_bestiary_t *, uint32_t,
    city_bestiary_persist_fn, void *);
city_bestiary_result_t city_bestiary_recover_instance(city_bestiary_t *, uint32_t,
    city_bestiary_persist_fn, void *);
city_bestiary_result_t city_bestiary_damage_instance(city_bestiary_t *, uint32_t, uint8_t,
    city_bestiary_persist_fn, void *);
/* An eligible confirmed scan, independent of whether the player catches anything.
 * A revisit requires this individual to have visited another place in between.
 * The caller binds recipient/sequence while input is locked and retries unchanged. */
city_bestiary_result_t city_bestiary_visit(city_bestiary_t *, uint64_t, uint32_t,
    uint16_t, uint16_t, city_bestiary_persist_fn, void *);
/* Inject an already uniform [0,6) draw, generated before asynchronous settlement. */
city_bestiary_result_t city_bestiary_capture_personality(city_bestiary_t *, uint64_t,
    uint16_t, uint16_t, const city_creature_stats_t *, uint8_t,
    city_bestiary_persist_fn, void *);
uint8_t city_friendship_band(uint8_t points);
