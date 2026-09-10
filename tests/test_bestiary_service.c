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

static void test_catalog_contains_charmander(void)
{
    const city_species_definition_t *definition =
        city_species_definition(CITY_SPECIES_CHARMANDER);
    CHECK(definition != NULL);
    CHECK(definition->species_id == CITY_SPECIES_CHARMANDER);
    CHECK(strcmp(definition->name, "Charmander") == 0);
    CHECK(strcmp(definition->element, "Fire") == 0);
    CHECK(city_species_definition(999U) == NULL);
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
    CHECK(bestiary.charmander.state == CITY_DISCOVERY_CAPTURED);
    CHECK(bestiary.charmander.capture_count == 1U);
    CHECK(bestiary.charmander.last_place_id == 1U);
    CHECK(bestiary.ledger_count == 1U);
    CHECK(bestiary.ledger_next == 1U);
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
    CHECK(bestiary.charmander.capture_count == 1U);
    CHECK(bestiary.ledger_count == 1U);
    CHECK(bestiary.ledger_next == 1U);
    CHECK(probe.calls == 1U);
}

static void test_capture_count_outlives_idempotency_window(void)
{
    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);
    persist_probe_t probe = {.succeed = true};

    for (uint64_t encounter_id = 1U; encounter_id <= 20U;
         ++encounter_id) {
        CHECK(city_bestiary_capture(
                  &bestiary,
                  encounter_id,
                  CITY_SPECIES_CHARMANDER,
                  3U,
                  persist_probe,
                  &probe) == CITY_BESTIARY_APPLIED);
    }

    CHECK(bestiary.charmander.capture_count == 20U);
    CHECK(bestiary.ledger_count == CITY_BESTIARY_LEDGER_CAPACITY);
    CHECK(bestiary.ledger_next == 4U);
    CHECK(probe.calls == 20U);

    CHECK(city_bestiary_capture(
              &bestiary,
              20U,
              CITY_SPECIES_CHARMANDER,
              3U,
              persist_probe,
              &probe) == CITY_BESTIARY_DUPLICATE);
    CHECK(bestiary.charmander.capture_count == 20U);
    CHECK(probe.calls == 20U);

    CHECK(city_bestiary_capture(
              &bestiary,
              1U,
              CITY_SPECIES_CHARMANDER,
              3U,
              persist_probe,
              &probe) == CITY_BESTIARY_APPLIED);
    CHECK(bestiary.charmander.capture_count == 21U);
    CHECK(bestiary.ledger_next == 5U);
    CHECK(probe.calls == 21U);

    uint8_t encoded[CITY_BESTIARY_ENCODED_BYTES];
    CHECK(city_bestiary_encode(&bestiary, encoded));
    city_bestiary_t decoded;
    city_bestiary_init(&decoded);
    CHECK(city_bestiary_decode(encoded, sizeof(encoded), &decoded));
    CHECK(memcmp(&decoded, &bestiary, sizeof(bestiary)) == 0);
}

static void test_full_window_rolls_back_when_persistence_fails(void)
{
    city_bestiary_t bestiary;
    city_bestiary_init(&bestiary);
    persist_probe_t probe = {.succeed = true};

    for (uint64_t encounter_id = 1U;
         encounter_id <= CITY_BESTIARY_LEDGER_CAPACITY;
         ++encounter_id) {
        CHECK(city_bestiary_capture(
                  &bestiary,
                  encounter_id,
                  CITY_SPECIES_CHARMANDER,
                  4U,
                  persist_probe,
                  &probe) == CITY_BESTIARY_APPLIED);
    }
    const city_bestiary_t before = bestiary;

    probe.succeed = false;
    CHECK(city_bestiary_capture(
              &bestiary,
              17U,
              CITY_SPECIES_CHARMANDER,
              4U,
              persist_probe,
              &probe) == CITY_BESTIARY_STORAGE_FAILED);
    CHECK(memcmp(&bestiary, &before, sizeof(bestiary)) == 0);
    CHECK(bestiary.charmander.capture_count ==
          CITY_BESTIARY_LEDGER_CAPACITY);
    CHECK(bestiary.encounter_ids[0] == 1U);
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
    CHECK(bestiary.charmander.state == CITY_DISCOVERY_UNKNOWN);
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

static void test_v1_snapshot_migrates_to_ring_window(void)
{
    enum {
        ledger_offset = 16,
        checksum_offset = CITY_BESTIARY_ENCODED_BYTES - 4,
    };
    uint8_t encoded[CITY_BESTIARY_ENCODED_BYTES];
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
    CHECK(migrated.charmander.capture_count == 2U);
    CHECK(migrated.ledger_count == 2U);
    CHECK(migrated.ledger_next == 2U);
    CHECK(migrated.encounter_ids[0] == UINT64_C(1001));
    CHECK(migrated.encounter_ids[1] == UINT64_C(1002));
}

static void test_inconsistent_ledger_is_rejected(void)
{
    city_bestiary_t inconsistent;
    city_bestiary_init(&inconsistent);
    inconsistent.ledger_count = 1U;
    inconsistent.encounter_ids[0] = 99U;

    uint8_t encoded[CITY_BESTIARY_ENCODED_BYTES];
    CHECK(!city_bestiary_encode(&inconsistent, encoded));
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
    test_catalog_contains_charmander();
    test_capture_commits_only_after_persistence();
    test_duplicate_encounter_is_idempotent();
    test_capture_count_outlives_idempotency_window();
    test_full_window_rolls_back_when_persistence_fails();
    test_invalid_place_never_calls_storage();
    test_codec_round_trip_and_corruption();
    test_v1_snapshot_migrates_to_ring_window();
    test_inconsistent_ledger_is_rejected();
    test_invalid_arguments_are_rejected();

    if (failures == 0) {
        puts("test_bestiary_service: ALL PASS");
        return 0;
    }
    fprintf(stderr, "test_bestiary_service: %d failure(s)\n", failures);
    return 1;
}
