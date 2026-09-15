#include "bestiary_service.h"

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
    city_bestiary_t last;
} persist_probe_t;

static bool persist_probe(
    const city_bestiary_t *next,
    void *context)
{
    persist_probe_t *probe = context;
    ++probe->calls;
    probe->last = *next;
    return probe->succeed;
}

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

static size_t compact_current_snapshot(
    const uint8_t full[CITY_BESTIARY_ENCODED_BYTES],
    const uint8_t *record_indexes,
    uint16_t count,
    uint8_t compact[CITY_BESTIARY_ENCODED_BYTES])
{
    if (count == 0U || count >= CITY_SPECIES_COUNT) {
        return 0U;
    }
    memset(compact, 0, CITY_BESTIARY_ENCODED_BYTES);
    memcpy(compact, full, CITY_BESTIARY_HEADER_BYTES);
    write_u16_le(compact + 6U, count);
    for (uint16_t i = 0U; i < count; ++i) {
        memcpy(
            compact + CITY_BESTIARY_HEADER_BYTES +
                i * CITY_BESTIARY_RECORD_BYTES,
            full + CITY_BESTIARY_HEADER_BYTES +
                record_indexes[i] * CITY_BESTIARY_RECORD_BYTES,
            CITY_BESTIARY_RECORD_BYTES);
    }
    const size_t full_owned_offset = CITY_BESTIARY_HEADER_BYTES +
        CITY_SPECIES_COUNT * CITY_BESTIARY_RECORD_BYTES;
    const size_t compact_owned_offset = CITY_BESTIARY_HEADER_BYTES +
        count * CITY_BESTIARY_RECORD_BYTES;
    const size_t owned_bytes =
        CITY_MAX_OWNED_POKEMON * CITY_OWNED_POKEMON_BYTES;
    memcpy(compact + compact_owned_offset, full + full_owned_offset, owned_bytes);
    const size_t length = compact_owned_offset + owned_bytes + 4U;
    write_u32_le(compact + length - 4U, crc32(compact, length - 4U));
    return length;
}

static void test_catalog_contains_three_species(void)
{
    const city_species_definition_t *bulbasaur =
        city_species_definition(CITY_SPECIES_BULBASAUR);
    const city_species_definition_t *definition =
        city_species_definition(CITY_SPECIES_CHARMANDER);
    const city_species_definition_t *squirtle =
        city_species_definition(CITY_SPECIES_SQUIRTLE);
    CHECK(bulbasaur != NULL);
    CHECK(strcmp(bulbasaur->name, "Bulbasaur") == 0);
    CHECK(definition != NULL);
    CHECK(definition->species_id == CITY_SPECIES_CHARMANDER);
    CHECK(strcmp(definition->name, "Charmander") == 0);
    CHECK(strcmp(definition->element, "Fire") == 0);
    CHECK(squirtle != NULL);
    CHECK(strcmp(squirtle->name, "Squirtle") == 0);
    CHECK(city_species_definition(999U) == NULL);
}

static void test_species_stats_track_latest_and_best(void)
{
    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);
    persist_probe_t probe = {.succeed = true};
    const city_creature_stats_t first = {
        .hp = 47U, .attack = 53U, .defense = 50U};
    const city_creature_stats_t weaker = {
        .hp = 46U, .attack = 50U, .defense = 50U};

    CHECK(city_bestiary_capture_with_stats(
              &bestiary, 1U, CITY_SPECIES_BULBASAUR, 2U, &first,
              persist_probe, &probe) == CITY_BESTIARY_APPLIED);
    CHECK(bestiary.records[0].capture_count == 1U);
    CHECK(bestiary.records[0].latest_stats.attack == 53U);
    CHECK(bestiary.records[0].best_stats.attack == 53U);
    CHECK(city_bestiary_capture_with_stats(
              &bestiary, 2U, CITY_SPECIES_BULBASAUR, 3U, &weaker,
              persist_probe, &probe) == CITY_BESTIARY_APPLIED);
    CHECK(bestiary.records[0].capture_count == 2U);
    CHECK(bestiary.records[0].latest_stats.attack == 50U);
    CHECK(bestiary.records[0].best_stats.attack == 53U);
    CHECK(city_bestiary_discovered_count(&bestiary) == 1U);
    CHECK(city_bestiary_captured_count(&bestiary) == 1U);

    uint8_t encoded[CITY_BESTIARY_ENCODED_BYTES];
    city_bestiary_t decoded;
    CHECK(city_bestiary_encode(&bestiary, encoded));
    CHECK(city_bestiary_decode(encoded, sizeof(encoded), &decoded));
    CHECK(decoded.records[0].latest_stats.attack == 50U);
    CHECK(decoded.records[0].best_stats.attack == 53U);
}

static void test_capture_commits_only_after_persistence(void)
{
    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);
    const city_bestiary_t before = bestiary;
    persist_probe_t probe;
    memset(&probe, 0, sizeof(probe));

    CHECK(city_bestiary_capture(
              &bestiary,
              UINT64_C(0x1001),
              CITY_SPECIES_CHARMANDER,
              1U,
              persist_probe,
              &probe) == CITY_BESTIARY_STORAGE_FAILED);
    CHECK(probe.calls == 1U);
    CHECK(memcmp(&bestiary, &before, sizeof(bestiary)) == 0);

    probe.succeed = true;
    CHECK(city_bestiary_capture(
              &bestiary,
              UINT64_C(0x1001),
              CITY_SPECIES_CHARMANDER,
              1U,
              persist_probe,
              &probe) == CITY_BESTIARY_APPLIED);
    CHECK(bestiary.records[1].state == CITY_DISCOVERY_CAPTURED);
    CHECK(bestiary.records[1].capture_count == 1U);
    CHECK(bestiary.records[1].last_place_id == 1U);
    CHECK(bestiary.last_settled_sequence == UINT64_C(0x1001));
    CHECK(probe.calls == 2U);
}

static void test_duplicate_encounter_is_idempotent(void)
{
    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);
    persist_probe_t probe = {.succeed = true};

    CHECK(city_bestiary_capture(
              &bestiary,
              77U,
              CITY_SPECIES_CHARMANDER,
              1U,
              persist_probe,
              &probe) == CITY_BESTIARY_APPLIED);
    CHECK(city_bestiary_capture(
              &bestiary,
              77U,
              CITY_SPECIES_CHARMANDER,
              1U,
              persist_probe,
              &probe) == CITY_BESTIARY_DUPLICATE);
    CHECK(bestiary.records[1].capture_count == 1U);
    CHECK(bestiary.last_settled_sequence == 77U);
    CHECK(probe.calls == 1U);
}

static void test_seen_transition_is_transactional(void)
{
    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);
    persist_probe_t probe = {0};

    CHECK(city_bestiary_mark_seen(
              &bestiary,
              CITY_SPECIES_CHARMANDER,
              persist_probe,
              &probe) == CITY_BESTIARY_STORAGE_FAILED);
    CHECK(bestiary.records[1].state == CITY_DISCOVERY_UNKNOWN);
    CHECK(probe.calls == 1U);

    probe.succeed = true;
    CHECK(city_bestiary_mark_seen(
              &bestiary,
              CITY_SPECIES_CHARMANDER,
              persist_probe,
              &probe) == CITY_BESTIARY_APPLIED);
    CHECK(bestiary.records[1].state == CITY_DISCOVERY_SEEN);
    CHECK(bestiary.records[1].capture_count == 0U);
    CHECK(probe.calls == 2U);

    CHECK(city_bestiary_mark_seen(
              &bestiary,
              CITY_SPECIES_CHARMANDER,
              persist_probe,
              &probe) == CITY_BESTIARY_UNCHANGED);
    CHECK(probe.calls == 2U);

    CHECK(city_bestiary_capture(
              &bestiary,
              UINT64_C(7001),
              CITY_SPECIES_CHARMANDER,
              2U,
              persist_probe,
              &probe) == CITY_BESTIARY_APPLIED);
    CHECK(bestiary.records[1].state == CITY_DISCOVERY_CAPTURED);
    CHECK(bestiary.records[1].capture_count == 1U);
}

static void test_legacy_count_import_preserves_unknown_history(void)
{
    city_bestiary_t bestiary;
    CHECK(city_bestiary_import_legacy_count(&bestiary, 4U));
    CHECK(bestiary.records[1].state == CITY_DISCOVERY_CAPTURED);
    CHECK(bestiary.records[1].capture_count == 4U);
    CHECK(bestiary.records[1].last_place_id == UINT16_MAX);
    CHECK(bestiary.last_settled_sequence == 4U);

    uint8_t encoded[CITY_BESTIARY_ENCODED_BYTES];
    CHECK(city_bestiary_encode(&bestiary, encoded));
    city_bestiary_t decoded;
    city_bestiary_init(&decoded);
    CHECK(city_bestiary_decode(encoded, sizeof(encoded), &decoded));
    CHECK(memcmp(&decoded, &bestiary, sizeof(bestiary)) == 0);

    persist_probe_t probe = {.succeed = true};
    CHECK(city_bestiary_capture(
              &decoded,
              UINT64_C(9001),
              CITY_SPECIES_CHARMANDER,
              1U,
              persist_probe,
              &probe) == CITY_BESTIARY_APPLIED);
    CHECK(decoded.records[1].capture_count == 5U);
    CHECK(decoded.records[1].last_place_id == 1U);
    CHECK(decoded.last_settled_sequence == UINT64_C(9001));
}

static void test_zero_legacy_count_remains_unknown(void)
{
    city_bestiary_t bestiary;
    CHECK(city_bestiary_import_legacy_count(&bestiary, 0U));
    CHECK(bestiary.records[1].state == CITY_DISCOVERY_UNKNOWN);
    CHECK(bestiary.records[1].capture_count == 0U);
    CHECK(!city_bestiary_import_legacy_count(NULL, 1U));
}

static void test_high_water_rejects_all_old_sequences(void)
{
    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);
    persist_probe_t probe = {.succeed = true};

    for (uint64_t sequence = 1U; sequence <= 100U; ++sequence) {
        CHECK(city_bestiary_capture(
                  &bestiary,
                  sequence,
                  CITY_SPECIES_CHARMANDER,
                  3U,
                  persist_probe,
                  &probe) == CITY_BESTIARY_APPLIED);
    }
    CHECK(bestiary.records[1].capture_count == 100U);
    CHECK(bestiary.last_settled_sequence == 100U);
    CHECK(probe.calls == 100U);

    const uint64_t stale_sequences[] = {1U, 50U, 100U};
    for (size_t i = 0;
         i < sizeof(stale_sequences) / sizeof(stale_sequences[0]);
         ++i) {
        CHECK(city_bestiary_capture(
                  &bestiary,
                  stale_sequences[i],
                  CITY_SPECIES_CHARMANDER,
                  3U,
                  persist_probe,
                  &probe) == CITY_BESTIARY_DUPLICATE);
    }
    CHECK(bestiary.records[1].capture_count == 100U);
    CHECK(probe.calls == 100U);

    uint64_t next_sequence = 0U;
    CHECK(city_bestiary_next_encounter_sequence(
        &bestiary, &next_sequence));
    CHECK(next_sequence == 101U);
    CHECK(city_bestiary_capture(
              &bestiary,
              next_sequence,
              CITY_SPECIES_CHARMANDER,
              3U,
              persist_probe,
              &probe) == CITY_BESTIARY_APPLIED);
    CHECK(bestiary.records[1].capture_count == 101U);
    CHECK(bestiary.last_settled_sequence == 101U);
}

static void test_high_water_rolls_back_when_persistence_fails(void)
{
    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);
    persist_probe_t probe = {.succeed = true};

    CHECK(city_bestiary_capture(
              &bestiary,
              1U,
              CITY_SPECIES_CHARMANDER,
              4U,
              persist_probe,
              &probe) == CITY_BESTIARY_APPLIED);
    const city_bestiary_t before = bestiary;

    probe.succeed = false;
    CHECK(city_bestiary_capture(
              &bestiary,
              2U,
              CITY_SPECIES_CHARMANDER,
              4U,
              persist_probe,
              &probe) == CITY_BESTIARY_STORAGE_FAILED);
    CHECK(memcmp(&bestiary, &before, sizeof(bestiary)) == 0);
    CHECK(bestiary.records[1].capture_count == 1U);
    CHECK(bestiary.last_settled_sequence == 1U);
}

static void test_ambiguous_commit_is_safe_after_reload(void)
{
    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);
    persist_probe_t probe = {.succeed = false};

    CHECK(city_bestiary_capture(
              &bestiary,
              1U,
              CITY_SPECIES_CHARMANDER,
              4U,
              persist_probe,
              &probe) == CITY_BESTIARY_STORAGE_FAILED);
    CHECK(bestiary.records[1].capture_count == 0U);

    bestiary = probe.last;
    probe.succeed = true;
    CHECK(city_bestiary_capture(
              &bestiary,
              1U,
              CITY_SPECIES_CHARMANDER,
              4U,
              persist_probe,
              &probe) == CITY_BESTIARY_DUPLICATE);
    CHECK(bestiary.records[1].capture_count == 1U);
    CHECK(bestiary.last_settled_sequence == 1U);
    CHECK(probe.calls == 1U);
}

static void test_invalid_place_never_calls_storage(void)
{
    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);
    persist_probe_t probe = {.succeed = true};

    CHECK(city_bestiary_capture(
              &bestiary,
              90U,
              CITY_SPECIES_CHARMANDER,
              UINT16_MAX,
              persist_probe,
              &probe) == CITY_BESTIARY_INVALID);
    CHECK(probe.calls == 0U);
    CHECK(bestiary.records[1].state == CITY_DISCOVERY_UNKNOWN);
}

static void test_codec_round_trip_and_corruption(void)
{
    city_bestiary_t original;
    city_bestiary_init(&original);
    persist_probe_t probe = {.succeed = true};
    CHECK(city_bestiary_capture(
              &original,
              1234U,
              CITY_SPECIES_CHARMANDER,
              7U,
              persist_probe,
              &probe) == CITY_BESTIARY_APPLIED);

    uint8_t encoded[CITY_BESTIARY_ENCODED_BYTES];
    CHECK(city_bestiary_encode(&original, encoded));
    city_bestiary_t decoded;
    city_bestiary_init(&decoded);
    CHECK(city_bestiary_decode(encoded, sizeof(encoded), &decoded));
    CHECK(memcmp(&decoded, &original, sizeof(original)) == 0);

    encoded[24] ^= 0x01U;
    CHECK(!city_bestiary_decode(encoded, sizeof(encoded), &decoded));
}

static void test_smaller_catalog_migrates_by_stable_id(void)
{
    city_bestiary_t original;
    city_bestiary_init(&original);
    persist_probe_t probe = {.succeed = true};
    CHECK(city_bestiary_mark_seen(
              &original,
              CITY_SPECIES_BULBASAUR,
              persist_probe,
              &probe) == CITY_BESTIARY_APPLIED);
    CHECK(city_bestiary_capture(
              &original,
              1U,
              CITY_SPECIES_CHARMANDER,
              2U,
              persist_probe,
              &probe) == CITY_BESTIARY_APPLIED);

    uint8_t full[CITY_BESTIARY_ENCODED_BYTES];
    uint8_t compact[CITY_BESTIARY_ENCODED_BYTES];
    CHECK(city_bestiary_encode(&original, full));
    const uint8_t indexes[] = {
        city_species_index(CITY_SPECIES_CHARMANDER),
        city_species_index(CITY_SPECIES_BULBASAUR),
    };
    const size_t compact_length = compact_current_snapshot(
        full, indexes, 2U, compact);
    CHECK(compact_length > 0U);

    city_bestiary_t migrated;
    CHECK(city_bestiary_decode(compact, compact_length, &migrated));
    const city_creature_record_t *charmander =
        city_bestiary_record_const(&migrated, CITY_SPECIES_CHARMANDER);
    const city_creature_record_t *bulbasaur =
        city_bestiary_record_const(&migrated, CITY_SPECIES_BULBASAUR);
    const city_creature_record_t *squirtle =
        city_bestiary_record_const(&migrated, CITY_SPECIES_SQUIRTLE);
    CHECK(charmander != NULL &&
          charmander->state == CITY_DISCOVERY_CAPTURED &&
          charmander->capture_count == 1U);
    CHECK(bulbasaur != NULL && bulbasaur->state == CITY_DISCOVERY_SEEN);
    CHECK(squirtle != NULL &&
          squirtle->state == CITY_DISCOVERY_UNKNOWN &&
          squirtle->capture_count == 0U);
    CHECK(migrated.owned_count == 1U);
    CHECK(migrated.owned[0].species_id == CITY_SPECIES_CHARMANDER);
    CHECK(city_bestiary_is_valid(&migrated));

    uint8_t upgraded[CITY_BESTIARY_ENCODED_BYTES];
    CHECK(city_bestiary_encode(&migrated, upgraded));
    CHECK(((uint16_t)upgraded[6] | ((uint16_t)upgraded[7] << 8U)) ==
          CITY_SPECIES_COUNT);

    const uint8_t duplicate_indexes[] = {
        city_species_index(CITY_SPECIES_CHARMANDER),
        city_species_index(CITY_SPECIES_CHARMANDER),
    };
    const size_t duplicate_length = compact_current_snapshot(
        full, duplicate_indexes, 2U, compact);
    CHECK(duplicate_length > 0U);
    CHECK(!city_bestiary_decode(compact, duplicate_length, &migrated));

    const size_t unknown_length = compact_current_snapshot(
        full, indexes, 2U, compact);
    CHECK(unknown_length > 0U);
    write_u16_le(compact + CITY_BESTIARY_HEADER_BYTES, UINT16_MAX);
    write_u32_le(
        compact + unknown_length - 4U,
        crc32(compact, unknown_length - 4U));
    CHECK(!city_bestiary_decode(compact, unknown_length, &migrated));

    const size_t corrupt_length = compact_current_snapshot(
        full, indexes, 2U, compact);
    CHECK(corrupt_length > 0U);
    compact[CITY_BESTIARY_HEADER_BYTES + 2U] ^= 1U;
    CHECK(!city_bestiary_decode(compact, corrupt_length, &migrated));
}

static void test_future_catalog_count_is_rejected(void)
{
    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);
    uint8_t encoded[CITY_BESTIARY_ENCODED_BYTES];
    CHECK(city_bestiary_encode(&bestiary, encoded));
    write_u16_le(encoded + 6U, (uint16_t)(CITY_SPECIES_COUNT + 1U));
    write_u32_le(
        encoded + CITY_BESTIARY_ENCODED_BYTES - 4U,
        crc32(encoded, CITY_BESTIARY_ENCODED_BYTES - 4U));

    city_bestiary_t decoded;
    CHECK(!city_bestiary_decode(encoded, sizeof(encoded), &decoded));
}

static void test_v1_snapshot_migrates_to_high_water(void)
{
    enum {
        ledger_offset = 16,
        checksum_offset = CITY_BESTIARY_LEGACY_BYTES - 4,
    };
    uint8_t encoded[CITY_BESTIARY_LEGACY_BYTES];
    memset(encoded, 0, sizeof(encoded));
    write_u32_le(encoded, CITY_BESTIARY_MAGIC);
    write_u16_le(encoded + 4U, 1U);
    write_u16_le(encoded + 6U, CITY_SPECIES_CHARMANDER);
    encoded[8] = CITY_DISCOVERY_CAPTURED;
    encoded[9] = 2U;
    write_u16_le(encoded + 10U, 7U);
    write_u32_le(encoded + 12U, 2U);
    write_u64_le(encoded + ledger_offset, UINT64_C(1001));
    write_u64_le(encoded + ledger_offset + 8U, UINT64_C(1002));
    write_u32_le(
        encoded + checksum_offset,
        crc32(encoded, checksum_offset));

    city_bestiary_t migrated;
    city_bestiary_init(&migrated);
    CHECK(city_bestiary_decode(encoded, sizeof(encoded), &migrated));
    CHECK(migrated.schema_version == CITY_BESTIARY_SCHEMA_VERSION);
    CHECK(migrated.records[1].capture_count == 2U);
    CHECK(migrated.last_settled_sequence == 2U);
}

static void test_v2_snapshot_migrates_to_high_water(void)
{
    enum {
        ledger_offset = 16,
        ledger_next_offset = 144,
        checksum_offset = CITY_BESTIARY_LEGACY_BYTES - 4,
    };
    uint8_t encoded[CITY_BESTIARY_LEGACY_BYTES];
    memset(encoded, 0, sizeof(encoded));
    write_u32_le(encoded, CITY_BESTIARY_MAGIC);
    write_u16_le(encoded + 4U, 2U);
    write_u16_le(encoded + 6U, CITY_SPECIES_CHARMANDER);
    encoded[8] = CITY_DISCOVERY_CAPTURED;
    encoded[9] = 16U;
    write_u16_le(encoded + 10U, 7U);
    write_u32_le(encoded + 12U, 20U);
    for (uint8_t i = 0; i < 16U; ++i) {
        write_u64_le(
            encoded + ledger_offset + ((size_t)i * 8U),
            (uint64_t)i + 5U);
    }
    encoded[ledger_next_offset] = 4U;
    write_u32_le(
        encoded + checksum_offset,
        crc32(encoded, checksum_offset));

    city_bestiary_t migrated;
    city_bestiary_init(&migrated);
    CHECK(city_bestiary_decode(encoded, sizeof(encoded), &migrated));
    CHECK(migrated.schema_version == CITY_BESTIARY_SCHEMA_VERSION);
    CHECK(migrated.records[1].capture_count == 20U);
    CHECK(migrated.last_settled_sequence == 20U);

    uint64_t next_sequence = 0U;
    CHECK(city_bestiary_next_encounter_sequence(
        &migrated, &next_sequence));
    CHECK(next_sequence == 21U);
}

static void test_inconsistent_high_water_is_rejected(void)
{
    city_bestiary_t inconsistent;
    city_bestiary_init(&inconsistent);
    inconsistent.last_settled_sequence = 1U;

    uint8_t encoded[CITY_BESTIARY_ENCODED_BYTES];
    CHECK(!city_bestiary_encode(&inconsistent, encoded));

    CHECK(city_bestiary_import_legacy_count(&inconsistent, 4U));
    inconsistent.last_settled_sequence = 0U;
    CHECK(!city_bestiary_encode(&inconsistent, encoded));

    inconsistent.last_settled_sequence = UINT64_MAX;
    uint64_t next_sequence = 0U;
    CHECK(!city_bestiary_next_encounter_sequence(
        &inconsistent, &next_sequence));
}

static void test_invalid_arguments_are_rejected(void)
{
    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);
    persist_probe_t probe = {.succeed = true};

    CHECK(city_bestiary_capture(
              NULL,
              1U,
              CITY_SPECIES_CHARMANDER,
              1U,
              persist_probe,
              &probe) == CITY_BESTIARY_INVALID);
    CHECK(city_bestiary_capture(
              &bestiary,
              0U,
              CITY_SPECIES_CHARMANDER,
              1U,
              persist_probe,
              &probe) == CITY_BESTIARY_INVALID);
    CHECK(city_bestiary_capture(
              &bestiary,
              1U,
              999U,
              1U,
              persist_probe,
              &probe) == CITY_BESTIARY_INVALID);
    CHECK(city_bestiary_capture(
              &bestiary,
              1U,
              CITY_SPECIES_CHARMANDER,
              1U,
              NULL,
              &probe) == CITY_BESTIARY_INVALID);
}

int main(void)
{
    test_catalog_contains_three_species();
    test_species_stats_track_latest_and_best();
    test_capture_commits_only_after_persistence();
    test_duplicate_encounter_is_idempotent();
    test_seen_transition_is_transactional();
    test_legacy_count_import_preserves_unknown_history();
    test_zero_legacy_count_remains_unknown();
    test_high_water_rejects_all_old_sequences();
    test_high_water_rolls_back_when_persistence_fails();
    test_ambiguous_commit_is_safe_after_reload();
    test_invalid_place_never_calls_storage();
    test_codec_round_trip_and_corruption();
    test_smaller_catalog_migrates_by_stable_id();
    test_future_catalog_count_is_rejected();
    test_v1_snapshot_migrates_to_high_water();
    test_v2_snapshot_migrates_to_high_water();
    test_inconsistent_high_water_is_rejected();
    test_invalid_arguments_are_rejected();

    if (failures == 0) {
        puts("test_bestiary_service: ALL PASS");
        return 0;
    }
    fprintf(stderr, "test_bestiary_service: %d failure(s)\n", failures);
    return 1;
}
