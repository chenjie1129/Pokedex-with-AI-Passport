#include "bestiary_service.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define CITY_BESTIARY_V4_SEQUENCE_OFFSET 8U
#define CITY_BESTIARY_V4_RECORDS_OFFSET 16U
#define CITY_BESTIARY_V4_RECORD_BYTES 20U
#define CITY_BESTIARY_V2_LEDGER_OFFSET 16U
#define CITY_BESTIARY_V2_LEDGER_NEXT_OFFSET 144U
#define CITY_BESTIARY_V2_LEDGER_CAPACITY 16U
#define CITY_BESTIARY_CHECKSUM_OFFSET (CITY_BESTIARY_ENCODED_BYTES - 4U)
#define CITY_BESTIARY_SCHEMA_V1 1U
#define CITY_BESTIARY_SCHEMA_V2 2U
#define CITY_BESTIARY_SCHEMA_V3 3U
#define CITY_BESTIARY_SCHEMA_V4 4U
#define CITY_BESTIARY_LEGACY_RECORD_BYTES 20U
#define CITY_BESTIARY_OLD_HEADER_BYTES 32U
#define CITY_BESTIARY_WILD_OFFSET 76U
#define CITY_BESTIARY_V3_SEQUENCE_OFFSET 16U

#include "catalog_provider.h"

static void write_u16_le(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8U);
}

static void write_u32_le(uint8_t *output, uint32_t value)
{
    for (size_t i = 0; i < 4U; ++i) {
        output[i] = (uint8_t)(value >> (8U * i));
    }
}

static void write_u64_le(uint8_t *output, uint64_t value)
{
    for (size_t i = 0; i < 8U; ++i) {
        output[i] = (uint8_t)(value >> (8U * i));
    }
}

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t read_u32_le(const uint8_t *data)
{
    uint32_t value = 0U;
    for (size_t i = 0; i < 4U; ++i) {
        value |= ((uint32_t)data[i]) << (8U * i);
    }
    return value;
}

static uint64_t read_u64_le(const uint8_t *data)
{
    uint64_t value = 0U;
    for (size_t i = 0; i < 8U; ++i) {
        value |= ((uint64_t)data[i]) << (8U * i);
    }
    return value;
}

static uint32_t crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8U; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1U) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return ~crc;
}

static bool stats_are_zero(const city_creature_stats_t *stats)
{
    return stats->hp == 0U && stats->attack == 0U &&
           stats->defense == 0U;
}

static bool stats_are_valid(const city_creature_stats_t *stats)
{
    return stats->hp > 0U && stats->attack > 0U &&
           stats->defense > 0U;
}

static uint16_t stats_total(const city_creature_stats_t *stats)
{
    return (uint16_t)stats->hp + stats->attack + stats->defense;
}

static bool stats_match_species(
    uint16_t species_id,
    const city_creature_stats_t *stats)
{
    const city_species_definition_t *definition =
        city_species_definition(species_id);
    return definition != NULL && stats_are_valid(stats) &&
           stats->hp >= definition->base_hp &&
           stats->hp <= definition->base_hp + 15U &&
           stats->attack >= definition->base_attack &&
           stats->attack <= definition->base_attack + 15U &&
           stats->defense >= definition->base_defense &&
           stats->defense <= definition->base_defense + 15U;
}

static city_creature_stats_t base_stats(uint16_t species_id)
{
    const city_species_definition_t *definition =
        city_species_definition(species_id);
    city_creature_stats_t stats = {0};
    if (definition != NULL) {
        stats.hp = definition->base_hp;
        stats.attack = definition->base_attack;
        stats.defense = definition->base_defense;
    }
    return stats;
}

city_creature_record_t *city_bestiary_record(city_bestiary_t *bestiary, uint16_t species_id)
{
    const uint8_t i = city_species_index(species_id);
    return bestiary && i < CITY_SPECIES_COUNT ? &bestiary->records[i] : NULL;
}
const city_creature_record_t *city_bestiary_record_const(const city_bestiary_t *bestiary, uint16_t species_id)
{
    const uint8_t i = city_species_index(species_id);
    return bestiary && i < CITY_SPECIES_COUNT ? &bestiary->records[i] : NULL;
}

uint16_t city_bestiary_owned_count(
    const city_bestiary_t *bestiary, uint16_t species_id)
{
    if (!bestiary || city_species_definition(species_id) == NULL) return 0U;
    uint16_t count = 0U;
    for (uint16_t i = 0U; i < bestiary->owned_count; ++i)
        if (bestiary->owned[i].species_id == species_id) ++count;
    return count;
}

const city_owned_pokemon_t *city_bestiary_owned_at(
    const city_bestiary_t *bestiary, uint16_t species_id, uint16_t ordinal)
{
    if (!bestiary) return NULL;
    for (uint16_t i = 0U; i < bestiary->owned_count; ++i) {
        if (bestiary->owned[i].species_id != species_id) continue;
        if (ordinal == 0U) return &bestiary->owned[i];
        --ordinal;
    }
    return NULL;
}

static bool materialize_legacy_owned(city_bestiary_t *bestiary)
{
    if (!bestiary) return false;
    if (bestiary->owned_count > 0U) return true;
    uint32_t total = 0U;
    for (uint8_t i = 0U; i < CITY_SPECIES_COUNT; ++i) {
        total += bestiary->records[i].capture_count;
        total += bestiary->records[i].evolution_obtained ? 1U : 0U;
    }
    if (total > CITY_MAX_OWNED_POKEMON) return false;
    uint32_t next_id = 1U;
    for (uint8_t i = 0U; i < CITY_SPECIES_COUNT; ++i) {
        const city_creature_record_t *record = &bestiary->records[i];
        for (uint32_t copy = 0U; copy < record->capture_count; ++copy) {
            city_owned_pokemon_t *owned = &bestiary->owned[bestiary->owned_count++];
            memset(owned, 0, sizeof(*owned));
            owned->instance_id = next_id++;
            owned->species_id = record->species_id;
            owned->place_id = record->last_place_id;
            owned->stats = copy + 1U == record->capture_count ?
                record->latest_stats : record->best_stats;
            owned->current_hp = copy == 0U ? record->current_hp : owned->stats.hp;
            owned->migrated = true;
            owned->personality = (uint8_t)((owned->instance_id - 1U) % CITY_PERSONALITY_COUNT);
        }
        if (record->evolution_obtained) {
            city_owned_pokemon_t *owned = &bestiary->owned[bestiary->owned_count++];
            memset(owned, 0, sizeof(*owned));
            owned->instance_id = next_id++;
            owned->species_id = record->species_id;
            owned->place_id = record->last_place_id;
            owned->stats = record->best_stats;
            owned->current_hp = record->capture_count == 0U ? record->current_hp : owned->stats.hp;
            owned->evolved = true;
            owned->migrated = true;
            owned->personality = (uint8_t)((owned->instance_id - 1U) % CITY_PERSONALITY_COUNT);
        }
    }
    bestiary->next_instance_id = next_id;
    return true;
}
bool city_bestiary_encounter_status(const city_bestiary_t *bestiary, uint16_t species_id,
                                    city_discovery_state_t *previous_state)
{
    if (!city_bestiary_is_valid(bestiary) || !previous_state) return false;
    const city_creature_record_t *record = city_bestiary_record_const(bestiary, species_id);
    if (!record) return false;
    *previous_state = record->state;
    return true;
}

static bool record_is_valid(const city_creature_record_t *record)
{
    if (record == NULL ||
        city_species_definition(record->species_id) == NULL ||
        record->state > CITY_DISCOVERY_CAPTURED ||
        record->friendship > CITY_BUDDY_MAX_FRIENDSHIP ||
        (record->evolution_obtained && !city_species_definition(record->species_id)->evolves_from)) {
        return false;
    }
    if (record->state == CITY_DISCOVERY_CAPTURED) {
        return (record->capture_count > 0U || record->evolution_obtained) &&
               record->current_hp <= city_bestiary_max_hp(record) &&
               stats_match_species(
                   record->species_id, &record->latest_stats) &&
               stats_match_species(
                   record->species_id, &record->best_stats) &&
               stats_total(&record->best_stats) >=
                   stats_total(&record->latest_stats);
    }
    return !record->evolution_obtained && record->current_hp == 0U &&
           record->friendship == 0U && record->buddy_places == 0U &&
           record->capture_count == 0U &&
           record->last_place_id == UINT16_MAX &&
           stats_are_zero(&record->latest_stats) &&
           stats_are_zero(&record->best_stats);
}

bool city_bestiary_is_valid(const city_bestiary_t *bestiary)
{
    if (!bestiary || bestiary->schema_version != CITY_BESTIARY_SCHEMA_VERSION) return false;
    if (bestiary->buddy_species_id) {
        const city_creature_record_t *buddy = city_bestiary_record_const(bestiary, bestiary->buddy_species_id);
        if (!buddy || buddy->state != CITY_DISCOVERY_CAPTURED) return false;
    }
    if (bestiary->buddy_instance_id) {
        const city_owned_pokemon_t *buddy = city_bestiary_owned_by_id(bestiary, bestiary->buddy_instance_id);
        if (!buddy || buddy->species_id != bestiary->buddy_species_id) return false;
    }
    if (bestiary->owned_count > CITY_MAX_OWNED_POKEMON || bestiary->next_instance_id == 0U) return false;
    uint64_t total = 0;
    bool has_history = false;
    for (uint8_t i = 0; i < CITY_SPECIES_COUNT; ++i) {
        if (bestiary->records[i].species_id != city_species_id_at(i) ||
            !record_is_valid(&bestiary->records[i])) return false;
        total += bestiary->records[i].capture_count;
        has_history = has_history || bestiary->records[i].state != CITY_DISCOVERY_UNKNOWN;
    }
    uint32_t captured[CITY_SPECIES_COUNT] = {0};
    bool evolved[CITY_SPECIES_COUNT] = {false};
    uint32_t maximum_id = 0U;
    for (uint16_t i = 0U; i < bestiary->owned_count; ++i) {
        const city_owned_pokemon_t *owned = &bestiary->owned[i];
        const uint8_t index = city_species_index(owned->species_id);
        if (owned->instance_id == 0U || index == CITY_SPECIES_COUNT ||
            (owned->place_id == UINT16_MAX && !owned->migrated) ||
            !stats_match_species(owned->species_id, &owned->stats) ||
            owned->personality >= CITY_PERSONALITY_COUNT || owned->friendship > 100U ||
            owned->last_friendship_place > 16U ||
            (owned->last_friendship_place && !(owned->friendship_places & (1U << (owned->last_friendship_place - 1U)))) ||
            owned->current_hp > owned->stats.hp || (owned->evolved && evolved[index])) return false;
        if (owned->memory_count > CITY_MEMORY_CAPACITY) return false;
        for (unsigned m = 0; m < CITY_MEMORY_CAPACITY; ++m) {
            const city_memory_t event = owned->memories[m];
            if (m >= owned->memory_count) {
                if (event.kind || event.place) return false;
            } else {
                if (event.kind < CITY_MEMORY_FIRST_OUTING || event.kind > CITY_MEMORY_CLOSE) return false;
                if (event.kind <= CITY_MEMORY_REVISIT) {
                    if (!event.place || event.place > 16U ||
                        !(owned->friendship_places & (1U << (event.place - 1U)))) return false;
                } else if (event.place) return false;
            }
        }
        for (uint16_t j = 0U; j < i; ++j)
            if (bestiary->owned[j].instance_id == owned->instance_id) return false;
        if (owned->evolved) evolved[index] = true;
        else ++captured[index];
        if (owned->instance_id > maximum_id) maximum_id = owned->instance_id;
    }
    for (uint8_t i = 0U; i < CITY_SPECIES_COUNT; ++i)
        if (captured[i] != bestiary->records[i].capture_count ||
            evolved[i] != bestiary->records[i].evolution_obtained) return false;
    return bestiary->next_instance_id > maximum_id &&
           bestiary->last_settled_sequence >= total &&
           (bestiary->last_settled_sequence == 0U || total > 0U || has_history);
}
uint8_t city_bestiary_discovered_count(const city_bestiary_t *bestiary)
{
    if (!city_bestiary_is_valid(bestiary)) return 0;
    uint8_t count = 0;
    for (uint8_t i = 0; i < CITY_SPECIES_COUNT; ++i) count += bestiary->records[i].state != CITY_DISCOVERY_UNKNOWN;
    return count;
}
uint8_t city_bestiary_captured_count(const city_bestiary_t *bestiary)
{
    if (!city_bestiary_is_valid(bestiary)) return 0;
    uint8_t count = 0;
    for (uint8_t i = 0; i < CITY_SPECIES_COUNT; ++i) count += bestiary->records[i].state == CITY_DISCOVERY_CAPTURED;
    return count;
}

static void init_record(city_creature_record_t *record, uint16_t species_id)
{
    memset(record, 0, sizeof(*record));
    record->species_id = species_id;
    record->state = CITY_DISCOVERY_UNKNOWN;
    record->last_place_id = UINT16_MAX;
}

void city_bestiary_init(city_bestiary_t *bestiary)
{
    if (bestiary == NULL) {
        return;
    }
    memset(bestiary, 0, sizeof(*bestiary));
    bestiary->schema_version = CITY_BESTIARY_SCHEMA_VERSION;
    bestiary->next_instance_id = 1U;
    for (uint8_t i = 0; i < CITY_SPECIES_COUNT; ++i)
        init_record(&bestiary->records[i], city_species_id_at(i));
}

bool city_bestiary_import_legacy_count(
    city_bestiary_t *bestiary,
    uint32_t capture_count)
{
    if (bestiary == NULL) {
        return false;
    }
    city_bestiary_init(bestiary);
    if (capture_count > 0U) {
        bestiary->records[1].state = CITY_DISCOVERY_CAPTURED;
        bestiary->records[1].capture_count = capture_count;
        bestiary->records[1].latest_stats =
            base_stats(CITY_SPECIES_CHARMANDER);
        bestiary->records[1].best_stats =
            bestiary->records[1].latest_stats;
        bestiary->records[1].current_hp = bestiary->records[1].latest_stats.hp;
        bestiary->last_settled_sequence = capture_count;
    }
    return materialize_legacy_owned(bestiary) && city_bestiary_is_valid(bestiary);
}

city_bestiary_result_t city_bestiary_mark_seen(
    city_bestiary_t *bestiary,
    uint16_t species_id,
    city_bestiary_persist_fn persist,
    void *context)
{
    city_creature_record_t *record =
        city_bestiary_record(bestiary, species_id);
    if (!city_bestiary_is_valid(bestiary) || record == NULL ||
        persist == NULL) {
        return CITY_BESTIARY_INVALID;
    }
    if (record->state != CITY_DISCOVERY_UNKNOWN) {
        return CITY_BESTIARY_UNCHANGED;
    }

    city_bestiary_t next = *bestiary;
    city_bestiary_record(&next, species_id)->state = CITY_DISCOVERY_SEEN;
    if (!persist(&next, context)) {
        return CITY_BESTIARY_STORAGE_FAILED;
    }
    *bestiary = next;
    return CITY_BESTIARY_APPLIED;
}

bool city_bestiary_next_encounter_sequence(
    const city_bestiary_t *bestiary,
    uint64_t *sequence)
{
    if (!city_bestiary_is_valid(bestiary) || sequence == NULL ||
        (bestiary->last_settled_sequence == UINT64_MAX || bestiary->last_visit_sequence == UINT64_MAX)) {
        return false;
    }
    *sequence = (bestiary->last_settled_sequence > bestiary->last_visit_sequence ?
        bestiary->last_settled_sequence : bestiary->last_visit_sequence) + 1U;
    return true;
}

city_bestiary_result_t city_bestiary_capture_personality(
    city_bestiary_t *bestiary,
    uint64_t encounter_sequence,
    uint16_t species_id,
    uint16_t place_id,
    const city_creature_stats_t *stats,
    uint8_t personality,
    city_bestiary_persist_fn persist,
    void *context)
{
    city_creature_record_t *record =
        city_bestiary_record(bestiary, species_id);
    if (!city_bestiary_is_valid(bestiary) || encounter_sequence == 0U ||
        place_id == UINT16_MAX || record == NULL ||
        !stats || personality >= CITY_PERSONALITY_COUNT || !stats_match_species(species_id, stats) || persist == NULL) {
        return CITY_BESTIARY_INVALID;
    }
    if (encounter_sequence <= bestiary->last_settled_sequence || encounter_sequence < bestiary->last_visit_sequence) {
        return CITY_BESTIARY_DUPLICATE;
    }
    if (record->capture_count == UINT32_MAX) {
        return CITY_BESTIARY_COUNTER_FULL;
    }

    city_bestiary_t next = *bestiary;
    if (!materialize_legacy_owned(&next) ||
        next.owned_count >= CITY_MAX_OWNED_POKEMON ||
        next.next_instance_id == UINT32_MAX) {
        return CITY_BESTIARY_COUNTER_FULL;
    }
    city_creature_record_t *next_record =
        city_bestiary_record(&next, species_id);
    next_record->state = CITY_DISCOVERY_CAPTURED;
    ++next_record->capture_count;
    next_record->last_place_id = place_id;
    next_record->latest_stats = *stats;
    const bool new_best = record->state != CITY_DISCOVERY_CAPTURED ||
        stats_total(stats) > stats_total(&next_record->best_stats);
    if (new_best) {
        next_record->best_stats = *stats;
    }
    /* Health must belong to the same individual as the displayed best stats. */
    if (new_best) {
        next_record->current_hp = stats->hp;
    } else if (next_record->current_hp > next_record->best_stats.hp) {
        next_record->current_hp = next_record->best_stats.hp;
    }
    city_owned_pokemon_t *owned = &next.owned[next.owned_count++];
    memset(owned, 0, sizeof(*owned));
    owned->instance_id = next.next_instance_id++;
    owned->species_id = species_id;
    owned->place_id = place_id;
    owned->stats = *stats;
    owned->current_hp = stats->hp;
    owned->personality = personality;
    next.last_settled_sequence = encounter_sequence;
    if (next.buddy_species_id) {
        city_creature_record_t *buddy = city_bestiary_record(&next, next.buddy_species_id);
        unsigned gain = 1; /* A successfully settled capture, including Wild. */
        if (place_id >= 1 && place_id <= 16) {
            const uint16_t bit = (uint16_t)(1U << (place_id - 1));
            if (!(buddy->buddy_places & bit)) { gain += 2; buddy->buddy_places |= bit; }
        }
        unsigned points = buddy->friendship + gain;
        buddy->friendship = points > CITY_BUDDY_MAX_FRIENDSHIP ? CITY_BUDDY_MAX_FRIENDSHIP : points;
    }

    if (!persist(&next, context)) {
        return CITY_BESTIARY_STORAGE_FAILED;
    }
    *bestiary = next;
    return CITY_BESTIARY_APPLIED;
}

city_bestiary_result_t city_bestiary_capture_with_stats(
    city_bestiary_t *bestiary, uint64_t sequence, uint16_t species, uint16_t place,
    const city_creature_stats_t *stats, city_bestiary_persist_fn persist, void *context)
{
    /* Compatibility callers get a deterministic assignment; runtime injects a draw. */
    return city_bestiary_capture_personality(bestiary, sequence, species, place, stats,
        (uint8_t)(sequence % CITY_PERSONALITY_COUNT), persist, context);
}

city_bestiary_result_t city_bestiary_capture(
    city_bestiary_t *bestiary,
    uint64_t encounter_sequence,
    uint16_t species_id,
    uint16_t place_id,
    city_bestiary_persist_fn persist,
    void *context)
{
    const city_creature_stats_t stats = base_stats(species_id);
    return city_bestiary_capture_with_stats(
        bestiary, encounter_sequence, species_id, place_id, &stats,
        persist, context);
}

static void encode_record(
    uint8_t *output,
    const city_creature_record_t *record)
{
    write_u16_le(output, record->species_id);
    output[2] = (uint8_t)record->state;
    output[3] = record->evolution_obtained ? 1 : 0;
    write_u32_le(output + 4U, record->capture_count);
    write_u16_le(output + 8U, record->last_place_id);
    output[10] = record->latest_stats.hp;
    output[11] = record->latest_stats.attack;
    output[12] = record->latest_stats.defense;
    output[13] = record->best_stats.hp;
    output[14] = record->best_stats.attack;
    output[15] = record->best_stats.defense;
    write_u16_le(output + 16, record->friendship);
    write_u16_le(output + 18, record->buddy_places);
    output[20] = record->current_hp;
}

static void decode_record(
    const uint8_t *data,
    city_creature_record_t *record)
{
    memset(record, 0, sizeof(*record));
    record->species_id = read_u16_le(data);
    record->state = (city_discovery_state_t)data[2];
    record->capture_count = read_u32_le(data + 4U);
    record->last_place_id = read_u16_le(data + 8U);
    record->latest_stats.hp = data[10];
    record->latest_stats.attack = data[11];
    record->latest_stats.defense = data[12];
    record->best_stats.hp = data[13];
    record->best_stats.attack = data[14];
    record->best_stats.defense = data[15];
}

static void encode_owned(uint8_t *output, const city_owned_pokemon_t *owned)
{
    write_u32_le(output, owned->instance_id);
    write_u16_le(output + 4U, owned->species_id);
    write_u16_le(output + 6U, owned->place_id);
    output[8] = owned->stats.hp;
    output[9] = owned->stats.attack;
    output[10] = owned->stats.defense;
    output[11] = owned->current_hp;
    output[12] = (owned->evolved ? 1U : 0U) | (owned->migrated ? 2U : 0U);
    output[13] = owned->personality;
    output[14] = owned->friendship;
    write_u16_le(output + 16U, owned->friendship_places);
    write_u16_le(output + 18U, owned->last_friendship_place);
    output[20] = owned->memory_count;
    for (unsigned i = 0; i < CITY_MEMORY_CAPACITY; ++i) {
        output[21 + i * 2] = owned->memories[i].kind;
        output[22 + i * 2] = owned->memories[i].place;
    }
}

static bool decode_owned(const uint8_t *data, city_owned_pokemon_t *owned, uint16_t version)
{
    const bool legacy = version == 10U;
    if ((data[12] & ~3U) != 0U || data[15] != 0U || (legacy && (data[13] != 0U || data[14] != 0U)))
        return false;
    memset(owned, 0, sizeof(*owned));
    owned->instance_id = read_u32_le(data);
    owned->species_id = read_u16_le(data + 4U);
    owned->place_id = read_u16_le(data + 6U);
    owned->stats.hp = data[8];
    owned->stats.attack = data[9];
    owned->stats.defense = data[10];
    owned->current_hp = data[11];
    owned->evolved = (data[12] & 1U) != 0U;
    owned->migrated = (data[12] & 2U) != 0U;
    owned->personality = legacy ? (uint8_t)((owned->instance_id - 1U) % CITY_PERSONALITY_COUNT) : data[13];
    if (!legacy) {
        owned->friendship = data[14];
        owned->friendship_places = read_u16_le(data + 16U);
        owned->last_friendship_place = read_u16_le(data + 18U);
    }
    if (version >= 12U) {
        if (data[31]) return false;
        owned->memory_count = data[20];
        for (unsigned i = 0; i < CITY_MEMORY_CAPACITY; ++i) {
            owned->memories[i].kind = data[21 + i * 2];
            owned->memories[i].place = data[22 + i * 2];
        }
    }
    return true;
}

bool city_bestiary_encode(
    const city_bestiary_t *bestiary,
    uint8_t output[CITY_BESTIARY_ENCODED_BYTES])
{
    if (!city_bestiary_is_valid(bestiary) || output == NULL) {
        return false;
    }

    memset(output, 0, CITY_BESTIARY_ENCODED_BYTES);
    write_u32_le(output, CITY_BESTIARY_MAGIC);
    write_u16_le(output + 4U, bestiary->schema_version);
    write_u16_le(output + 6U, CITY_SPECIES_COUNT);
    write_u64_le(
        output + CITY_BESTIARY_V4_SEQUENCE_OFFSET,
        bestiary->last_settled_sequence);
    output[16] = bestiary->wild_cooldown_active ? 1U : 0U;
    write_u32_le(output + 20U, CITY_CATALOG_VERSION);
    write_u16_le(output + 24U, bestiary->buddy_species_id);
    write_u16_le(output + 26U, bestiary->owned_count);
    write_u32_le(output + 28U, bestiary->next_instance_id);
    write_u32_le(output + 32U, bestiary->buddy_instance_id);
    write_u64_le(output + 40U, bestiary->last_visit_sequence);
    for (uint8_t i = 0; i < CITY_SPECIES_COUNT; ++i)
        encode_record(output + CITY_BESTIARY_HEADER_BYTES + i * CITY_BESTIARY_RECORD_BYTES, &bestiary->records[i]);
    const size_t owned_offset = CITY_BESTIARY_HEADER_BYTES +
        CITY_SPECIES_COUNT * CITY_BESTIARY_RECORD_BYTES;
    for (uint16_t i = 0U; i < bestiary->owned_count; ++i)
        encode_owned(output + owned_offset + i * CITY_OWNED_POKEMON_BYTES, &bestiary->owned[i]);
    write_u32_le(
        output + CITY_BESTIARY_CHECKSUM_OFFSET,
        crc32(output, CITY_BESTIARY_CHECKSUM_OFFSET));
    return true;
}

bool city_bestiary_encode_sparse(const city_bestiary_t *b, uint8_t *out,
                                 size_t capacity, size_t *written)
{
    if (!out || !written || !city_bestiary_is_valid(b)) return false;
    uint16_t count = 0;
    for (unsigned i = 0; i < CITY_SPECIES_COUNT; ++i)
        count += b->records[i].state != CITY_DISCOVERY_UNKNOWN;
    size_t bytes = CITY_BESTIARY_HEADER_BYTES + count * CITY_BESTIARY_RECORD_BYTES +
        b->owned_count * CITY_OWNED_POKEMON_BYTES + 4U;
    if (capacity < bytes) return false;
    memset(out, 0, bytes);
    write_u32_le(out, CITY_BESTIARY_MAGIC);
    write_u16_le(out + 4, CITY_BESTIARY_STORAGE_VERSION);
    write_u16_le(out + 6, count);
    write_u64_le(out + 8, b->last_settled_sequence);
    out[16] = b->wild_cooldown_active;
    write_u32_le(out + 20, CITY_CATALOG_VERSION);
    write_u16_le(out + 24, b->buddy_species_id);
    write_u16_le(out + 26, b->owned_count);
    write_u32_le(out + 28, b->next_instance_id);
    write_u32_le(out + 32, b->buddy_instance_id);
    write_u64_le(out + 40, b->last_visit_sequence);
    uint16_t previous = 0;
    for (unsigned n = 0; n < count; ++n) {
        const city_creature_record_t *next = NULL;
        for (unsigned i = 0; i < CITY_SPECIES_COUNT; ++i) {
            const city_creature_record_t *row = &b->records[i];
            if (row->state != CITY_DISCOVERY_UNKNOWN && row->species_id > previous &&
                (!next || row->species_id < next->species_id)) next = row;
        }
        /* Validated model has distinct nonzero IDs, so next always exists. */
        encode_record(out + CITY_BESTIARY_HEADER_BYTES + n * CITY_BESTIARY_RECORD_BYTES, next);
        previous = next->species_id;
    }
    size_t owned = CITY_BESTIARY_HEADER_BYTES + count * CITY_BESTIARY_RECORD_BYTES;
    for (unsigned i = 0; i < b->owned_count; ++i)
        encode_owned(out + owned + i * CITY_OWNED_POKEMON_BYTES, &b->owned[i]);
    write_u32_le(out + bytes - 4, crc32(out, bytes - 4));
    *written = bytes; return true;
}

static bool decode_legacy(
    const uint8_t *data,
    uint16_t stored_schema_version,
    city_bestiary_t *decoded)
{
    city_bestiary_init(decoded);
    city_creature_record_t *record = &decoded->records[1];
    record->species_id = read_u16_le(data + 6U);
    record->state = (city_discovery_state_t)data[8];
    record->last_place_id = read_u16_le(data + 10U);
    record->capture_count = read_u32_le(data + 12U);

    if (stored_schema_version == CITY_BESTIARY_SCHEMA_V3) {
        decoded->last_settled_sequence =
            read_u64_le(data + CITY_BESTIARY_V3_SEQUENCE_OFFSET);
    } else {
        const uint8_t ledger_count = data[9];
        const uint8_t ledger_next =
            stored_schema_version == CITY_BESTIARY_SCHEMA_V1
                ? (uint8_t)(ledger_count %
                            CITY_BESTIARY_V2_LEDGER_CAPACITY)
                : data[CITY_BESTIARY_V2_LEDGER_NEXT_OFFSET];
        if (ledger_count > CITY_BESTIARY_V2_LEDGER_CAPACITY ||
            ledger_next >= CITY_BESTIARY_V2_LEDGER_CAPACITY ||
            (ledger_count < CITY_BESTIARY_V2_LEDGER_CAPACITY &&
             ledger_next != ledger_count) ||
            (stored_schema_version == CITY_BESTIARY_SCHEMA_V1 &&
             record->capture_count != ledger_count)) {
            return false;
        }
        if (record->state == CITY_DISCOVERY_CAPTURED &&
            (record->capture_count == 0U ||
             record->capture_count < ledger_count ||
             (ledger_count > 0U &&
              record->last_place_id == UINT16_MAX))) {
            return false;
        }
        if (record->state != CITY_DISCOVERY_CAPTURED &&
            (record->capture_count != 0U ||
             ledger_count != 0U || ledger_next != 0U ||
             record->last_place_id != UINT16_MAX)) {
            return false;
        }
        for (uint8_t i = 0U; i < ledger_count; ++i) {
            const uint64_t encounter_id = read_u64_le(
                data + CITY_BESTIARY_V2_LEDGER_OFFSET +
                    ((size_t)i * 8U));
            if (encounter_id == 0U) {
                return false;
            }
            for (uint8_t j = (uint8_t)(i + 1U);
                 j < ledger_count; ++j) {
                if (encounter_id == read_u64_le(
                        data + CITY_BESTIARY_V2_LEDGER_OFFSET +
                            ((size_t)j * 8U))) {
                    return false;
                }
            }
        }
        if (record->state == CITY_DISCOVERY_CAPTURED) {
            decoded->last_settled_sequence = record->capture_count;
        }
    }

    if (record->state == CITY_DISCOVERY_CAPTURED) {
        if (record->capture_count == 0U ||
            (stored_schema_version != CITY_BESTIARY_SCHEMA_V3 &&
             record->last_place_id == UINT16_MAX)) {
            return false;
        }
        record->latest_stats = base_stats(CITY_SPECIES_CHARMANDER);
        record->best_stats = record->latest_stats;
    } else if (record->capture_count != 0U ||
               record->last_place_id != UINT16_MAX) {
        return false;
    }
    return true;
}

static bool decode_into(
    const uint8_t *data,
    size_t length,
    city_bestiary_t *bestiary)
{
    if (!data || !bestiary || length < 12 || length > CITY_BESTIARY_ENCODED_BYTES || read_u32_le(data) != CITY_BESTIARY_MAGIC ||
        read_u32_le(data + length - 4) != crc32(data, length - 4)) return false;
    const uint16_t version = read_u16_le(data + 4);
    city_bestiary_t *decoded = bestiary;
    city_bestiary_init(decoded);
    if (version == 10U || version == 11U || version == CITY_BESTIARY_SCHEMA_VERSION ||
        version == CITY_BESTIARY_STORAGE_VERSION) {
        const bool sparse = version == CITY_BESTIARY_STORAGE_VERSION;
        if (length < CITY_BESTIARY_HEADER_BYTES && version >= 11U) return false;
        const size_t header_bytes = version == 10U ? 40U : CITY_BESTIARY_HEADER_BYTES;
        const size_t owned_bytes = version == 10U ? 16U : version == 11U ? 20U : CITY_OWNED_POKEMON_BYTES;
        const uint16_t count = read_u16_le(data + 6U);
        if ((!sparse && count == 0U) || count > CITY_SPECIES_COUNT) return false;
        const size_t expected_length = header_bytes + count * CITY_BESTIARY_RECORD_BYTES +
            (sparse ? read_u16_le(data + 26U) : CITY_MAX_OWNED_POKEMON) * owned_bytes + 4U;
        if (length != expected_length ||
            data[16] > 1U || data[17] != 0U || data[18] != 0U || data[19] != 0U ||
            (version == 10U && read_u32_le(data + 32U) != 0U) ||
            data[36] != 0U || data[37] != 0U || data[38] != 0U || data[39] != 0U) return false;
        decoded->last_settled_sequence = read_u64_le(data + 8U);
        decoded->wild_cooldown_active = data[16] == 1U;
        decoded->buddy_species_id = read_u16_le(data + 24U);
        decoded->owned_count = read_u16_le(data + 26U);
        decoded->next_instance_id = read_u32_le(data + 28U);
        if (version >= 11U) {
            decoded->buddy_instance_id = read_u32_le(data + 32U);
            decoded->last_visit_sequence = read_u64_le(data + 40U);
        }
        if (decoded->owned_count > CITY_MAX_OWNED_POKEMON) return false;
        bool present[CITY_SPECIES_COUNT] = {false};
        uint16_t previous = 0;
        for (uint16_t i = 0U; i < count; ++i) {
            city_creature_record_t record;
            const uint8_t *record_data = data + header_bytes +
                i * CITY_BESTIARY_RECORD_BYTES;
            decode_record(record_data, &record);
            if (record_data[3U] > 1U || record_data[21U] != 0U) return false;
            record.evolution_obtained = record_data[3U] == 1U;
            record.friendship = read_u16_le(record_data + 16U);
            record.buddy_places = read_u16_le(record_data + 18U);
            record.current_hp = record_data[20U];
            if (sparse && (record.state == CITY_DISCOVERY_UNKNOWN || record.species_id <= previous)) return false;
            previous = record.species_id;
            const uint8_t index = city_species_index(record.species_id);
            if (index == CITY_SPECIES_COUNT || present[index]) return false;
            present[index] = true;
            decoded->records[index] = record;
        }
        const size_t owned_offset = header_bytes +
            count * CITY_BESTIARY_RECORD_BYTES;
        for (uint16_t i = 0U; i < decoded->owned_count; ++i)
            if (!decode_owned(data + owned_offset + i * owned_bytes,
                              &decoded->owned[i], version)) return false;
        for (size_t i = owned_offset + decoded->owned_count * owned_bytes;
             i < length - 4U; ++i)
            if (data[i] != 0U) return false;
    } else if (version >= 6 && version <= 9) {
        const uint16_t count = read_u16_le(data + 6);
        const size_t record_bytes = version >= 9 ? CITY_BESTIARY_RECORD_BYTES : CITY_BESTIARY_LEGACY_RECORD_BYTES;
        if (count == 0 || count > CITY_SPECIES_COUNT || length != CITY_BESTIARY_OLD_HEADER_BYTES + count * record_bytes + 4U || data[16] > 1) return false;
        decoded->last_settled_sequence = read_u64_le(data + 8);
        decoded->wild_cooldown_active = data[16] == 1;
        if (version >= 7) decoded->buddy_species_id = read_u16_le(data + 24);
        bool present[CITY_SPECIES_COUNT] = {false};
        for (uint16_t i = 0; i < count; ++i) {
            city_creature_record_t record;
            const uint8_t *record_data = data + CITY_BESTIARY_OLD_HEADER_BYTES + i * record_bytes;
            decode_record(record_data, &record);
            if (version >= 8) {
                if (record_data[3U] > 1) return false;
                record.evolution_obtained = record_data[3U] == 1;
            }
            if (version >= 7) {
                record.friendship = read_u16_le(record_data + 16U);
                record.buddy_places = read_u16_le(record_data + 18U);
            }
            record.current_hp = version >= 9 ? record_data[20U] :
                (record.state == CITY_DISCOVERY_CAPTURED ? record.best_stats.hp : 0U);
            if (version >= 9 && record_data[21U] != 0U) return false;
            const uint8_t index = city_species_index(record.species_id);
            if (index == CITY_SPECIES_COUNT || present[index]) return false;
            present[index] = true;
            decoded->records[index] = record;
        }
    } else if (version == 4 || version == 5) {
        if (length != CITY_BESTIARY_LEGACY_BYTES || read_u16_le(data + 6) != 3) return false;
        if (version == 5 && data[76] > 1) return false;
        decoded->wild_cooldown_active = version == 5 && data[76] == 1;
        decoded->last_settled_sequence = read_u64_le(data + 8);
        for (uint8_t i = 0; i < 3; ++i) {
            decode_record(data + 16U + i * 20U, &decoded->records[i]);
            if (decoded->records[i].species_id != city_species_id_at(i)) return false;
        }
    } else if (version >= 1 && version <= 3) {
        if (length != CITY_BESTIARY_LEGACY_BYTES || !decode_legacy(data, version, decoded)) return false;
    } else return false;

    if (version < 9) {
        for (uint8_t i = 0; i < CITY_SPECIES_COUNT; ++i) {
            if (decoded->records[i].state == CITY_DISCOVERY_CAPTURED) {
                decoded->records[i].current_hp = decoded->records[i].best_stats.hp;
            }
        }
    }

    if (version < CITY_BESTIARY_SCHEMA_VERSION && !materialize_legacy_owned(decoded)) return false;

    if (!city_bestiary_is_valid(decoded)) {
        return false;
    }
    return true;
}

bool city_bestiary_decode(const uint8_t *data, size_t length, city_bestiary_t *bestiary)
{
    if (!bestiary) return false;
    city_bestiary_t *next = malloc(sizeof(*next));
    if (!next) return false;
    const bool ok = decode_into(data, length, next);
    if (ok) *bestiary = *next;
    free(next);
    return ok;
}

city_bestiary_result_t city_bestiary_reserve_wild(
    city_bestiary_t *bestiary, city_wild_reward_guard_t *guard,
    uint64_t now_ms, uint16_t species_id,
    city_bestiary_persist_fn persist, void *context)
{
    if (!city_bestiary_is_valid(bestiary) || guard == NULL || persist == NULL ||
        (city_species_definition(species_id) == NULL || !city_species_definition(species_id)->wild_eligible)) {
        return CITY_BESTIARY_INVALID;
    }
    if (!city_wild_reward_available(guard, now_ms)) return CITY_BESTIARY_COOLDOWN;
    city_bestiary_t next = *bestiary;
    city_creature_record_t *record = city_bestiary_record(&next, species_id);
    if (record->state == CITY_DISCOVERY_UNKNOWN) record->state = CITY_DISCOVERY_SEEN;
    next.wild_cooldown_active = true;
    if (!persist(&next, context)) return CITY_BESTIARY_STORAGE_FAILED;
    *bestiary = next;
    (void)city_wild_reward_settle(guard, now_ms);
    return CITY_BESTIARY_APPLIED;
}

city_bestiary_result_t city_bestiary_clear_wild_cooldown(
    city_bestiary_t *bestiary, city_wild_reward_guard_t *guard,
    uint64_t now_ms, city_bestiary_persist_fn persist, void *context)
{
    if (!city_bestiary_is_valid(bestiary) || guard == NULL || persist == NULL)
        return CITY_BESTIARY_INVALID;
    if (!city_wild_reward_available(guard, now_ms)) return CITY_BESTIARY_COOLDOWN;
    if (!bestiary->wild_cooldown_active) return CITY_BESTIARY_UNCHANGED;
    city_bestiary_t next = *bestiary;
    next.wild_cooldown_active = false;
    if (!persist(&next, context)) return CITY_BESTIARY_STORAGE_FAILED;
    *bestiary = next;
    return CITY_BESTIARY_APPLIED;
}

city_bestiary_result_t city_bestiary_choose_buddy(
    city_bestiary_t *bestiary, uint16_t species_id,
    city_bestiary_persist_fn persist, void *context)
{
    if (!city_bestiary_is_valid(bestiary) || !persist) return CITY_BESTIARY_INVALID;
    const city_creature_record_t *record = city_bestiary_record_const(bestiary, species_id);
    if (!record || record->state != CITY_DISCOVERY_CAPTURED) return CITY_BESTIARY_INVALID;
    if (bestiary->buddy_species_id == species_id) return CITY_BESTIARY_UNCHANGED;
    city_bestiary_t next = *bestiary;
    next.buddy_species_id = species_id;
    next.buddy_instance_id = 0U;
    if (!persist(&next, context)) return CITY_BESTIARY_STORAGE_FAILED;
    *bestiary = next;
    return CITY_BESTIARY_APPLIED;
}

uint16_t city_evolution_target(uint16_t source_id)
{
    for (unsigned i = 0; i < CITY_SPECIES_COUNT; ++i)
        if (CITY_CATALOG_DEFAULT.at(i)->evolves_from == source_id && source_id)
            return CITY_CATALOG_DEFAULT.at(i)->species_id;
    return 0;
}
uint8_t city_buddy_place_count(const city_creature_record_t *record)
{
    if (!record) return 0;
    uint16_t bits = record->buddy_places;
    uint8_t count = 0;
    while (bits) { count += bits & 1U; bits >>= 1; }
    return count;
}
bool city_evolution_ready(const city_bestiary_t *bestiary, uint16_t source_id)
{
    if (!city_bestiary_is_valid(bestiary) || bestiary->buddy_species_id != source_id) return false;
    const city_creature_record_t *source = city_bestiary_record_const(bestiary, source_id);
    const city_creature_record_t *target = city_bestiary_record_const(bestiary, city_evolution_target(source_id));
    return source && target && !target->evolution_obtained && source->state == CITY_DISCOVERY_CAPTURED &&
        source->friendship >= CITY_EVOLUTION_BOND && city_buddy_place_count(source) >= CITY_EVOLUTION_PLACES;
}
city_bestiary_result_t city_bestiary_evolve(city_bestiary_t *bestiary,
    uint16_t source_id, city_bestiary_persist_fn persist, void *context)
{
    if (!city_bestiary_is_valid(bestiary) || !persist) return CITY_BESTIARY_INVALID;
    const uint16_t target_id = city_evolution_target(source_id);
    const city_creature_record_t *existing = city_bestiary_record_const(bestiary, target_id);
    if (!existing) return CITY_BESTIARY_INVALID;
    if (existing->evolution_obtained) return CITY_BESTIARY_UNCHANGED;
    if (!city_evolution_ready(bestiary, source_id)) return CITY_BESTIARY_INVALID;
    city_bestiary_t next = *bestiary;
    if (!materialize_legacy_owned(&next) ||
        next.owned_count >= CITY_MAX_OWNED_POKEMON ||
        next.next_instance_id == UINT32_MAX) return CITY_BESTIARY_COUNTER_FULL;
    const city_creature_record_t *source = city_bestiary_record_const(&next, source_id);
    city_creature_record_t *target = city_bestiary_record(&next, target_id);
    const city_species_definition_t *from = city_species_definition(source_id);
    const city_species_definition_t *to = city_species_definition(target_id);
    const city_creature_stats_t stats = {
        to->base_hp + source->best_stats.hp - from->base_hp,
        to->base_attack + source->best_stats.attack - from->base_attack,
        to->base_defense + source->best_stats.defense - from->base_defense
    };
    target->state = CITY_DISCOVERY_CAPTURED;
    target->evolution_obtained = true;
    target->last_place_id = source->last_place_id;
    target->latest_stats = stats;
    if (stats_total(&stats) > stats_total(&target->best_stats)) target->best_stats = stats;
    if (source->friendship > target->friendship) target->friendship = source->friendship;
    target->buddy_places |= source->buddy_places;
    target->current_hp = stats.hp;
    city_owned_pokemon_t *owned = &next.owned[next.owned_count++];
    memset(owned, 0, sizeof(*owned));
    owned->instance_id = next.next_instance_id++;
    owned->species_id = target_id;
    owned->place_id = source->last_place_id;
    owned->stats = stats;
    owned->current_hp = stats.hp;
    owned->evolved = true;
    owned->personality = (uint8_t)((owned->instance_id - 1U) % CITY_PERSONALITY_COUNT);
    next.buddy_species_id = target_id;
    next.buddy_instance_id = owned->instance_id;
    if (!persist(&next, context)) return CITY_BESTIARY_STORAGE_FAILED;
    *bestiary = next;
    return CITY_BESTIARY_APPLIED;
}

uint8_t city_bestiary_max_hp(const city_creature_record_t *record)
{
    return record && record->state == CITY_DISCOVERY_CAPTURED ? record->best_stats.hp : 0U;
}

city_bestiary_result_t city_bestiary_apply_damage(
    city_bestiary_t *bestiary, uint16_t species_id, uint8_t damage,
    city_bestiary_persist_fn persist, void *context)
{
    if (!city_bestiary_is_valid(bestiary) || !persist || damage == 0U) return CITY_BESTIARY_INVALID;
    if (bestiary->buddy_instance_id && bestiary->buddy_species_id == species_id)
        return city_bestiary_damage_instance(bestiary, bestiary->buddy_instance_id, damage, persist, context);
    const city_creature_record_t *record = city_bestiary_record_const(bestiary, species_id);
    if (!record || record->state != CITY_DISCOVERY_CAPTURED) return CITY_BESTIARY_INVALID;
    if (record->current_hp == 0U) return CITY_BESTIARY_UNCHANGED;
    city_bestiary_t next = *bestiary;
    if (!materialize_legacy_owned(&next)) return CITY_BESTIARY_INVALID;
    city_creature_record_t *changed = city_bestiary_record(&next, species_id);
    changed->current_hp = damage >= changed->current_hp ? 0U : (uint8_t)(changed->current_hp - damage);
    for (uint16_t i = 0U; i < next.owned_count; ++i) {
        city_owned_pokemon_t *owned = &next.owned[i];
        if (owned->species_id == species_id &&
            memcmp(&owned->stats, &record->best_stats, sizeof(owned->stats)) == 0) {
            owned->current_hp = changed->current_hp;
            break;
        }
    }
    if (!persist(&next, context)) return CITY_BESTIARY_STORAGE_FAILED;
    *bestiary = next;
    return CITY_BESTIARY_APPLIED;
}

city_bestiary_result_t city_bestiary_recover(
    city_bestiary_t *bestiary, uint16_t species_id,
    city_bestiary_persist_fn persist, void *context)
{
    if (!city_bestiary_is_valid(bestiary) || !persist) return CITY_BESTIARY_INVALID;
    if (bestiary->buddy_instance_id && bestiary->buddy_species_id == species_id)
        return city_bestiary_recover_instance(bestiary, bestiary->buddy_instance_id, persist, context);
    const city_creature_record_t *record = city_bestiary_record_const(bestiary, species_id);
    if (!record || record->state != CITY_DISCOVERY_CAPTURED) return CITY_BESTIARY_INVALID;
    const uint8_t maximum = city_bestiary_max_hp(record);
    if (record->current_hp == maximum) return CITY_BESTIARY_UNCHANGED;
    city_bestiary_t next = *bestiary;
    if (!materialize_legacy_owned(&next)) return CITY_BESTIARY_INVALID;
    city_bestiary_record(&next, species_id)->current_hp = maximum;
    for (uint16_t i = 0U; i < next.owned_count; ++i) {
        city_owned_pokemon_t *owned = &next.owned[i];
        if (owned->species_id == species_id &&
            memcmp(&owned->stats, &record->best_stats, sizeof(owned->stats)) == 0) {
            owned->current_hp = owned->stats.hp;
            break;
        }
    }
    if (!persist(&next, context)) return CITY_BESTIARY_STORAGE_FAILED;
    *bestiary = next;
    return CITY_BESTIARY_APPLIED;
}

static void rebuild_species_after_release(city_bestiary_t *bestiary, uint16_t species_id,
                                          const city_creature_record_t *previous)
{
    city_creature_record_t *record = city_bestiary_record(bestiary, species_id);
    const uint16_t friendship = previous->friendship;
    const uint16_t buddy_places = previous->buddy_places;
    init_record(record, species_id);
    record->state = CITY_DISCOVERY_SEEN;
    const city_owned_pokemon_t *latest = NULL;
    const city_owned_pokemon_t *best = NULL;
    for (uint16_t i = 0U; i < bestiary->owned_count; ++i) {
        const city_owned_pokemon_t *owned = &bestiary->owned[i];
        if (owned->species_id != species_id) continue;
        if (!latest || owned->instance_id > latest->instance_id) latest = owned;
        if (!best || stats_total(&owned->stats) > stats_total(&best->stats)) best = owned;
        if (owned->evolved) record->evolution_obtained = true;
        else ++record->capture_count;
    }
    if (latest && best) {
        record->state = CITY_DISCOVERY_CAPTURED;
        record->last_place_id = latest->place_id;
        record->latest_stats = latest->stats;
        record->best_stats = best->stats;
        record->current_hp = best->current_hp;
        record->friendship = friendship;
        record->buddy_places = buddy_places;
    } else if (bestiary->buddy_species_id == species_id) {
        bestiary->buddy_species_id = 0U;
    }
}

city_bestiary_result_t city_bestiary_release_instance(
    city_bestiary_t *bestiary, uint32_t instance_id,
    city_bestiary_persist_fn persist, void *context)
{
    if (!city_bestiary_is_valid(bestiary) || !persist || instance_id == 0U)
        return CITY_BESTIARY_INVALID;
    city_bestiary_t next = *bestiary;
    if (!materialize_legacy_owned(&next)) return CITY_BESTIARY_INVALID;
    uint16_t index = next.owned_count;
    for (uint16_t i = 0U; i < next.owned_count; ++i)
        if (next.owned[i].instance_id == instance_id) { index = i; break; }
    if (index == next.owned_count) return CITY_BESTIARY_INVALID;
    const uint16_t species_id = next.owned[index].species_id;
    if (next.buddy_instance_id == instance_id) {
        next.buddy_instance_id = 0U;
        next.buddy_species_id = 0U;
    }
    const city_creature_record_t previous = *city_bestiary_record_const(&next, species_id);
    for (uint16_t i = index + 1U; i < next.owned_count; ++i)
        next.owned[i - 1U] = next.owned[i];
    --next.owned_count;
    memset(&next.owned[next.owned_count], 0, sizeof(next.owned[0]));
    rebuild_species_after_release(&next, species_id, &previous);
    if (!persist(&next, context)) return CITY_BESTIARY_STORAGE_FAILED;
    *bestiary = next;
    return CITY_BESTIARY_APPLIED;
}

city_bestiary_result_t city_bestiary_release(
    city_bestiary_t *bestiary, uint16_t species_id,
    city_bestiary_persist_fn persist, void *context)
{
    if (!city_bestiary_is_valid(bestiary) || !persist) return CITY_BESTIARY_INVALID;
    const uint16_t count = city_bestiary_owned_count(bestiary, species_id);
    const city_owned_pokemon_t *owned = count == 0U ? NULL :
        city_bestiary_owned_at(bestiary, species_id, (uint16_t)(count - 1U));
    return owned ? city_bestiary_release_instance(
        bestiary, owned->instance_id, persist, context) : CITY_BESTIARY_INVALID;
}

const city_owned_pokemon_t *city_bestiary_owned_by_id(const city_bestiary_t *b, uint32_t id)
{
    if (!b || !id || b->owned_count > CITY_MAX_OWNED_POKEMON) return NULL;
    for (uint16_t i = 0; i < b->owned_count; ++i)
        if (b->owned[i].instance_id == id) return &b->owned[i];
    return NULL;
}

uint8_t city_friendship_band(uint8_t points)
{
    return points >= 60U ? 2U : points >= 20U ? 1U : 0U;
}

city_bestiary_result_t city_bestiary_choose_buddy_instance(city_bestiary_t *b, uint32_t id,
    city_bestiary_persist_fn persist, void *context)
{
    if (!city_bestiary_is_valid(b) || !persist) return CITY_BESTIARY_INVALID;
    const city_owned_pokemon_t *owned = city_bestiary_owned_by_id(b, id);
    if (!owned) return CITY_BESTIARY_INVALID;
    if (b->buddy_instance_id == id) return CITY_BESTIARY_UNCHANGED;
    city_bestiary_t next = *b;
    next.buddy_instance_id = id;
    next.buddy_species_id = owned->species_id;
    if (!persist(&next, context)) return CITY_BESTIARY_STORAGE_FAILED;
    *b = next;
    return CITY_BESTIARY_APPLIED;
}

static void remember(city_owned_pokemon_t *owned, uint8_t kind, uint8_t place)
{
    if (owned->memory_count == CITY_MEMORY_CAPACITY) {
        memmove(owned->memories, owned->memories + 1,
                (CITY_MEMORY_CAPACITY - 1U) * sizeof(owned->memories[0]));
        --owned->memory_count;
    }
    owned->memories[owned->memory_count++] = (city_memory_t){kind, place};
}

uint8_t city_companion_place_count(const city_owned_pokemon_t *owned)
{
    uint8_t count = 0;
    if (owned) for (unsigned i = 0; i < 16; ++i) count += (owned->friendship_places >> i) & 1U;
    return count;
}

city_companion_invitation_t city_companion_invitation(const city_owned_pokemon_t *owned)
{
    if (owned && owned->current_hp < owned->stats.hp) return CITY_INVITE_REST;
    const uint8_t places = city_companion_place_count(owned);
    return !places ? CITY_INVITE_FIRST_OUTING : places < 16U ? CITY_INVITE_NEW_PLACE : CITY_INVITE_REVISIT;
}

static city_bestiary_result_t instance_health(city_bestiary_t *b, uint32_t id, uint8_t damage,
    bool recover, city_bestiary_persist_fn persist, void *context)
{
    if (!city_bestiary_is_valid(b) || !persist) return CITY_BESTIARY_INVALID;
    const city_owned_pokemon_t *owned = city_bestiary_owned_by_id(b, id);
    if (!owned) return CITY_BESTIARY_INVALID;
    uint8_t hp = owned->stats.hp;
    if (!recover)
        hp = damage >= owned->current_hp ? 0U : (uint8_t)(owned->current_hp - damage);
    if (hp == owned->current_hp) return CITY_BESTIARY_UNCHANGED;
    city_bestiary_t next = *b;
    next.owned[owned - b->owned].current_hp = hp;
    if (recover) remember(&next.owned[owned - b->owned], CITY_MEMORY_REST, 0);
    const city_creature_record_t previous = *city_bestiary_record_const(b, owned->species_id);
    rebuild_species_after_release(&next, owned->species_id, &previous);
    if (!persist(&next, context)) return CITY_BESTIARY_STORAGE_FAILED;
    *b = next;
    return CITY_BESTIARY_APPLIED;
}

city_bestiary_result_t city_bestiary_recover_instance(city_bestiary_t *b, uint32_t id,
    city_bestiary_persist_fn persist, void *context)
{ return instance_health(b, id, 0U, true, persist, context); }

city_bestiary_result_t city_bestiary_damage_instance(city_bestiary_t *b, uint32_t id, uint8_t damage,
    city_bestiary_persist_fn persist, void *context)
{ return instance_health(b, id, damage, false, persist, context); }

city_bestiary_result_t city_bestiary_visit(city_bestiary_t *b, uint64_t sequence, uint32_t buddy_id,
    uint16_t place, uint16_t seen_species, city_bestiary_persist_fn persist, void *context)
{
    if (!city_bestiary_is_valid(b) || !persist || !sequence || place < 1U || place > 16U ||
        !city_species_definition(seen_species) || buddy_id != b->buddy_instance_id)
        return CITY_BESTIARY_INVALID;
    if (sequence <= b->last_visit_sequence || sequence <= b->last_settled_sequence)
        return CITY_BESTIARY_DUPLICATE;
    city_bestiary_t next = *b;
    city_creature_record_t *seen = city_bestiary_record(&next, seen_species);
    if (seen->state == CITY_DISCOVERY_UNKNOWN) seen->state = CITY_DISCOVERY_SEEN;
    const city_owned_pokemon_t *selected = city_bestiary_owned_by_id(b, buddy_id);
    if (selected) {
        city_owned_pokemon_t *owned = &next.owned[selected - b->owned];
        if (owned->last_friendship_place != place) {
            const uint16_t bit = (uint16_t)(1U << (place - 1U));
            const bool revisit = (owned->friendship_places & bit) != 0;
            const uint8_t previous_band = city_friendship_band(owned->friendship);
            const uint8_t event = !owned->friendship_places ? CITY_MEMORY_FIRST_OUTING :
                revisit ? CITY_MEMORY_REVISIT : CITY_MEMORY_NEW_PLACE;
            const unsigned gain = revisit ? 2U : 5U;
            const unsigned points = owned->friendship + gain;
            owned->friendship = points > 100U ? 100U : points;
            owned->friendship_places |= bit;
            owned->last_friendship_place = place;
            remember(owned, event, (uint8_t)place);
            if (city_friendship_band(owned->friendship) > previous_band)
                remember(owned, city_friendship_band(owned->friendship) == 1U ?
                         CITY_MEMORY_FAMILIAR : CITY_MEMORY_CLOSE, 0);
        }
    }
    if (seen->state == city_bestiary_record_const(b, seen_species)->state &&
        (!selected || memcmp(&next.owned[selected - b->owned], selected, sizeof(*selected)) == 0))
        return CITY_BESTIARY_UNCHANGED;
    next.last_visit_sequence = sequence;
    if (!persist(&next, context)) return CITY_BESTIARY_STORAGE_FAILED;
    *b = next;
    return CITY_BESTIARY_APPLIED;
}
