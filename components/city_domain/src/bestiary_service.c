#include "bestiary_service.h"

#include <limits.h>
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
#define CITY_BESTIARY_WILD_OFFSET 76U
#define CITY_BESTIARY_V3_SEQUENCE_OFFSET 16U

#include "species_catalog.inc"

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

const city_species_definition_t *city_species_definition(uint16_t species_id)
{
    for (size_t i = 0U; i < CITY_SPECIES_COUNT; ++i) {
        if (species_definitions[i].species_id == species_id) {
            return &species_definitions[i];
        }
    }
    return NULL;
}

uint16_t city_species_id_at(uint8_t index)
{
    return index < CITY_SPECIES_COUNT ? species_definitions[index].species_id : UINT16_MAX;
}
uint8_t city_species_index(uint16_t species_id)
{
    for (uint8_t i = 0; i < CITY_SPECIES_COUNT; ++i)
        if (species_definitions[i].species_id == species_id) return i;
    return CITY_SPECIES_COUNT;
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
        record->friendship > CITY_BUDDY_MAX_FRIENDSHIP) {
        return false;
    }
    if (record->state == CITY_DISCOVERY_CAPTURED) {
        return record->capture_count > 0U &&
               stats_match_species(
                   record->species_id, &record->latest_stats) &&
               stats_match_species(
                   record->species_id, &record->best_stats) &&
               stats_total(&record->best_stats) >=
                   stats_total(&record->latest_stats);
    }
    return record->friendship == 0U && record->buddy_places == 0U &&
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
    uint64_t total = 0;
    for (uint8_t i = 0; i < CITY_SPECIES_COUNT; ++i) {
        if (bestiary->records[i].species_id != city_species_id_at(i) ||
            !record_is_valid(&bestiary->records[i])) return false;
        total += bestiary->records[i].capture_count;
    }
    return total == 0 ? bestiary->last_settled_sequence == 0 : bestiary->last_settled_sequence >= total;
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
        bestiary->last_settled_sequence = capture_count;
    }
    return city_bestiary_is_valid(bestiary);
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
        bestiary->last_settled_sequence == UINT64_MAX) {
        return false;
    }
    *sequence = bestiary->last_settled_sequence + 1U;
    return true;
}

city_bestiary_result_t city_bestiary_capture_with_stats(
    city_bestiary_t *bestiary,
    uint64_t encounter_sequence,
    uint16_t species_id,
    uint16_t place_id,
    const city_creature_stats_t *stats,
    city_bestiary_persist_fn persist,
    void *context)
{
    city_creature_record_t *record =
        city_bestiary_record(bestiary, species_id);
    if (!city_bestiary_is_valid(bestiary) || encounter_sequence == 0U ||
        place_id == UINT16_MAX || record == NULL ||
        !stats_match_species(species_id, stats) || persist == NULL) {
        return CITY_BESTIARY_INVALID;
    }
    if (encounter_sequence <= bestiary->last_settled_sequence) {
        return CITY_BESTIARY_DUPLICATE;
    }
    if (record->capture_count == UINT32_MAX) {
        return CITY_BESTIARY_COUNTER_FULL;
    }

    city_bestiary_t next = *bestiary;
    city_creature_record_t *next_record =
        city_bestiary_record(&next, species_id);
    next_record->state = CITY_DISCOVERY_CAPTURED;
    ++next_record->capture_count;
    next_record->last_place_id = place_id;
    next_record->latest_stats = *stats;
    if (next_record->capture_count == 1U ||
        stats_total(stats) > stats_total(&next_record->best_stats)) {
        next_record->best_stats = *stats;
    }
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
    for (uint8_t i = 0; i < CITY_SPECIES_COUNT; ++i)
        encode_record(output + 32U + i * 20U, &bestiary->records[i]);
    write_u32_le(
        output + CITY_BESTIARY_CHECKSUM_OFFSET,
        crc32(output, CITY_BESTIARY_CHECKSUM_OFFSET));
    return true;
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

bool city_bestiary_decode(
    const uint8_t *data,
    size_t length,
    city_bestiary_t *bestiary)
{
    if (!data || !bestiary || length < 12 || length > CITY_BESTIARY_ENCODED_BYTES || read_u32_le(data) != CITY_BESTIARY_MAGIC ||
        read_u32_le(data + length - 4) != crc32(data, length - 4)) return false;
    const uint16_t version = read_u16_le(data + 4);
    city_bestiary_t decoded;
    city_bestiary_init(&decoded);
    if (version == 6 || version == CITY_BESTIARY_SCHEMA_VERSION) {
        const uint16_t count = read_u16_le(data + 6);
        if (count == 0 || count > CITY_SPECIES_COUNT || length != 32U + count * 20U + 4U || data[16] > 1) return false;
        decoded.last_settled_sequence = read_u64_le(data + 8);
        decoded.wild_cooldown_active = data[16] == 1;
        if (version >= 7) decoded.buddy_species_id = read_u16_le(data + 24);
        bool present[CITY_SPECIES_COUNT] = {false};
        for (uint16_t i = 0; i < count; ++i) {
            city_creature_record_t record;
            decode_record(data + 32U + i * 20U, &record);
            if (version >= 7) {
                record.friendship = read_u16_le(data + 32U + i * 20U + 16U);
                record.buddy_places = read_u16_le(data + 32U + i * 20U + 18U);
            }
            const uint8_t index = city_species_index(record.species_id);
            if (index == CITY_SPECIES_COUNT || present[index]) return false;
            present[index] = true;
            decoded.records[index] = record;
        }
    } else if (version == 4 || version == 5) {
        if (length != CITY_BESTIARY_LEGACY_BYTES || read_u16_le(data + 6) != 3) return false;
        if (version == 5 && data[76] > 1) return false;
        decoded.wild_cooldown_active = version == 5 && data[76] == 1;
        decoded.last_settled_sequence = read_u64_le(data + 8);
        for (uint8_t i = 0; i < 3; ++i) {
            decode_record(data + 16U + i * 20U, &decoded.records[i]);
            if (decoded.records[i].species_id != city_species_id_at(i)) return false;
        }
    } else if (version >= 1 && version <= 3) {
        if (length != CITY_BESTIARY_LEGACY_BYTES || !decode_legacy(data, version, &decoded)) return false;
    } else return false;

    if (!city_bestiary_is_valid(&decoded)) {
        return false;
    }
    *bestiary = decoded;
    return true;
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
    if (!persist(&next, context)) return CITY_BESTIARY_STORAGE_FAILED;
    *bestiary = next;
    return CITY_BESTIARY_APPLIED;
}
