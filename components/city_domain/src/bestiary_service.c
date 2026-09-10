#include "bestiary_service.h"

#include <limits.h>
#include <string.h>

#define CITY_BESTIARY_LEDGER_OFFSET 16U
#define CITY_BESTIARY_LEDGER_NEXT_OFFSET 144U
#define CITY_BESTIARY_CHECKSUM_OFFSET                                    \
    (CITY_BESTIARY_ENCODED_BYTES - 4U)
#define CITY_BESTIARY_LEGACY_SCHEMA_VERSION 1U

static const city_species_definition_t charmander_definition = {
    .species_id = CITY_SPECIES_CHARMANDER,
    .name = "Charmander",
    .element = "Fire",
    .description = "The flame on its tail shows the strength of its life.",
};

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

static bool bestiary_is_valid(const city_bestiary_t *bestiary)
{
    if (bestiary == NULL ||
        bestiary->schema_version != CITY_BESTIARY_SCHEMA_VERSION ||
        bestiary->charmander.species_id != CITY_SPECIES_CHARMANDER ||
        bestiary->charmander.state > CITY_DISCOVERY_CAPTURED ||
        bestiary->ledger_count > CITY_BESTIARY_LEDGER_CAPACITY ||
        bestiary->ledger_next >= CITY_BESTIARY_LEDGER_CAPACITY) {
        return false;
    }
    if (bestiary->charmander.state == CITY_DISCOVERY_CAPTURED &&
        (bestiary->charmander.capture_count == 0U ||
         bestiary->charmander.capture_count < bestiary->ledger_count ||
         (bestiary->ledger_count > 0U &&
          bestiary->charmander.last_place_id == UINT16_MAX))) {
        return false;
    }
    if (bestiary->charmander.state != CITY_DISCOVERY_CAPTURED &&
        (bestiary->charmander.capture_count != 0U ||
         bestiary->ledger_count != 0U ||
         bestiary->ledger_next != 0U ||
         bestiary->charmander.last_place_id != UINT16_MAX)) {
        return false;
    }
    if (bestiary->ledger_count < CITY_BESTIARY_LEDGER_CAPACITY &&
        bestiary->ledger_next != bestiary->ledger_count) {
        return false;
    }
    for (uint8_t i = 0; i < bestiary->ledger_count; ++i) {
        if (bestiary->encounter_ids[i] == 0U) {
            return false;
        }
        for (uint8_t j = (uint8_t)(i + 1U);
             j < bestiary->ledger_count;
             ++j) {
            if (bestiary->encounter_ids[i] ==
                bestiary->encounter_ids[j]) {
                return false;
            }
        }
    }
    return true;
}

const city_species_definition_t *city_species_definition(
    uint16_t species_id)
{
    return species_id == CITY_SPECIES_CHARMANDER
               ? &charmander_definition
               : NULL;
}

void city_bestiary_init(city_bestiary_t *bestiary)
{
    if (bestiary == NULL) {
        return;
    }
    memset(bestiary, 0, sizeof(*bestiary));
    bestiary->schema_version = CITY_BESTIARY_SCHEMA_VERSION;
    bestiary->charmander.species_id = CITY_SPECIES_CHARMANDER;
    bestiary->charmander.state = CITY_DISCOVERY_UNKNOWN;
    bestiary->charmander.last_place_id = UINT16_MAX;
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
        bestiary->charmander.state = CITY_DISCOVERY_CAPTURED;
        bestiary->charmander.capture_count = capture_count;
    }
    return bestiary_is_valid(bestiary);
}

city_bestiary_result_t city_bestiary_capture(
    city_bestiary_t *bestiary,
    uint64_t encounter_id,
    uint16_t species_id,
    uint16_t place_id,
    city_bestiary_persist_fn persist,
    void *context)
{
    if (!bestiary_is_valid(bestiary) || encounter_id == 0U ||
        place_id == UINT16_MAX ||
        city_species_definition(species_id) == NULL || persist == NULL) {
        return CITY_BESTIARY_INVALID;
    }
    for (uint8_t i = 0; i < bestiary->ledger_count; ++i) {
        if (bestiary->encounter_ids[i] == encounter_id) {
            return CITY_BESTIARY_DUPLICATE;
        }
    }
    if (bestiary->charmander.capture_count == UINT32_MAX) {
        return CITY_BESTIARY_COUNTER_FULL;
    }

    city_bestiary_t next = *bestiary;
    next.charmander.state = CITY_DISCOVERY_CAPTURED;
    ++next.charmander.capture_count;
    next.charmander.last_place_id = place_id;
    next.encounter_ids[next.ledger_next] = encounter_id;
    if (next.ledger_count < CITY_BESTIARY_LEDGER_CAPACITY) {
        ++next.ledger_count;
    }
    next.ledger_next =
        (uint8_t)((next.ledger_next + 1U) %
                  CITY_BESTIARY_LEDGER_CAPACITY);

    if (!persist(&next, context)) {
        return CITY_BESTIARY_STORAGE_FAILED;
    }
    *bestiary = next;
    return CITY_BESTIARY_APPLIED;
}

bool city_bestiary_encode(
    const city_bestiary_t *bestiary,
    uint8_t output[CITY_BESTIARY_ENCODED_BYTES])
{
    if (!bestiary_is_valid(bestiary) || output == NULL) {
        return false;
    }

    memset(output, 0, CITY_BESTIARY_ENCODED_BYTES);
    write_u32_le(output, CITY_BESTIARY_MAGIC);
    write_u16_le(output + 4U, bestiary->schema_version);
    write_u16_le(output + 6U, bestiary->charmander.species_id);
    output[8] = (uint8_t)bestiary->charmander.state;
    output[9] = bestiary->ledger_count;
    write_u16_le(output + 10U, bestiary->charmander.last_place_id);
    write_u32_le(output + 12U, bestiary->charmander.capture_count);
    for (uint8_t i = 0; i < bestiary->ledger_count; ++i) {
        write_u64_le(
            output + CITY_BESTIARY_LEDGER_OFFSET + ((size_t)i * 8U),
            bestiary->encounter_ids[i]);
    }
    output[CITY_BESTIARY_LEDGER_NEXT_OFFSET] = bestiary->ledger_next;
    write_u32_le(
        output + CITY_BESTIARY_CHECKSUM_OFFSET,
        crc32(output, CITY_BESTIARY_CHECKSUM_OFFSET));
    return true;
}

bool city_bestiary_decode(
    const uint8_t data[CITY_BESTIARY_ENCODED_BYTES],
    size_t length,
    city_bestiary_t *bestiary)
{
    if (data == NULL || bestiary == NULL ||
        length != CITY_BESTIARY_ENCODED_BYTES ||
        read_u32_le(data) != CITY_BESTIARY_MAGIC ||
        read_u32_le(data + CITY_BESTIARY_CHECKSUM_OFFSET) !=
            crc32(data, CITY_BESTIARY_CHECKSUM_OFFSET)) {
        return false;
    }

    const uint16_t stored_schema_version = read_u16_le(data + 4U);
    if (stored_schema_version != CITY_BESTIARY_LEGACY_SCHEMA_VERSION &&
        stored_schema_version != CITY_BESTIARY_SCHEMA_VERSION) {
        return false;
    }

    city_bestiary_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    decoded.schema_version = CITY_BESTIARY_SCHEMA_VERSION;
    decoded.charmander.species_id = read_u16_le(data + 6U);
    decoded.charmander.state = (city_discovery_state_t)data[8];
    decoded.ledger_count = data[9];
    decoded.charmander.last_place_id = read_u16_le(data + 10U);
    decoded.charmander.capture_count = read_u32_le(data + 12U);
    if (decoded.ledger_count > CITY_BESTIARY_LEDGER_CAPACITY) {
        return false;
    }
    for (uint8_t i = 0; i < decoded.ledger_count; ++i) {
        decoded.encounter_ids[i] = read_u64_le(
            data + CITY_BESTIARY_LEDGER_OFFSET + ((size_t)i * 8U));
    }
    if (stored_schema_version == CITY_BESTIARY_LEGACY_SCHEMA_VERSION) {
        if (decoded.charmander.capture_count != decoded.ledger_count) {
            return false;
        }
        decoded.ledger_next =
            (uint8_t)(decoded.ledger_count %
                      CITY_BESTIARY_LEDGER_CAPACITY);
    } else {
        decoded.ledger_next = data[CITY_BESTIARY_LEDGER_NEXT_OFFSET];
    }
    if (!bestiary_is_valid(&decoded)) {
        return false;
    }

    *bestiary = decoded;
    return true;
}
