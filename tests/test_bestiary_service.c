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
    test_invalid_place_never_calls_storage();
    test_codec_round_trip_and_corruption();
    test_inconsistent_ledger_is_rejected();
    test_invalid_arguments_are_rejected();

    if (failures == 0) {
        puts("test_bestiary_service: ALL PASS");
        return 0;
    }
    fprintf(stderr, "test_bestiary_service: %d failure(s)\n", failures);
    return 1;
}
