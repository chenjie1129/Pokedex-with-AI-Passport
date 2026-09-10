#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CITY_BESTIARY_SCHEMA_VERSION 2U
#define CITY_BESTIARY_MAGIC UINT32_C(0x31545342)
#define CITY_BESTIARY_LEDGER_CAPACITY 16U
#define CITY_BESTIARY_ENCODED_BYTES 156U
#define CITY_SPECIES_CHARMANDER 4U

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
} city_species_definition_t;

typedef struct {
    uint16_t species_id;
    city_discovery_state_t state;
    uint32_t capture_count;
    uint16_t last_place_id;
} city_creature_record_t;

typedef struct {
    uint16_t schema_version;
    city_creature_record_t charmander;
    uint8_t ledger_count;
    uint8_t ledger_next;
    uint64_t encounter_ids[CITY_BESTIARY_LEDGER_CAPACITY];
} city_bestiary_t;

typedef bool (*city_bestiary_persist_fn)(
    const city_bestiary_t *next,
    void *context);

typedef enum {
    CITY_BESTIARY_INVALID = 0,
    CITY_BESTIARY_APPLIED,
    CITY_BESTIARY_DUPLICATE,
    CITY_BESTIARY_COUNTER_FULL,
    CITY_BESTIARY_STORAGE_FAILED,
} city_bestiary_result_t;

const city_species_definition_t *city_species_definition(
    uint16_t species_id);

void city_bestiary_init(city_bestiary_t *bestiary);

bool city_bestiary_import_legacy_count(
    city_bestiary_t *bestiary,
    uint32_t capture_count);

city_bestiary_result_t city_bestiary_capture(
    city_bestiary_t *bestiary,
    uint64_t encounter_id,
    uint16_t species_id,
    uint16_t place_id,
    city_bestiary_persist_fn persist,
    void *context);

bool city_bestiary_encode(
    const city_bestiary_t *bestiary,
    uint8_t output[CITY_BESTIARY_ENCODED_BYTES]);

bool city_bestiary_decode(
    const uint8_t data[CITY_BESTIARY_ENCODED_BYTES],
    size_t length,
    city_bestiary_t *bestiary);
