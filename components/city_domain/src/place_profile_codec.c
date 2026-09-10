#include "place_profile_codec.h"

#include <string.h>

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

static bool profile_is_valid(const city_place_profile_t *profile)
{
    if (profile->schema_version != CITY_PLACE_PROFILE_SCHEMA_VERSION ||
        profile->place_id == CITY_PLACE_INVALID_ID ||
        profile->confidence_permille > 1000U ||
        !city_place_fingerprint_is_usable(&profile->fingerprint)) {
        return false;
    }
    for (uint8_t i = 0; i < profile->fingerprint.count; ++i) {
        for (uint8_t j = (uint8_t)(i + 1U);
             j < profile->fingerprint.count;
             ++j) {
            if (profile->fingerprint.tokens[i] ==
                profile->fingerprint.tokens[j]) {
                return false;
            }
        }
    }
    return true;
}

static bool catalog_is_valid(const city_place_catalog_t *catalog)
{
    if (catalog == NULL || catalog->count > CITY_PLACE_MAX_COUNT) {
        return false;
    }
    for (uint16_t i = 0; i < catalog->count; ++i) {
        if (!profile_is_valid(&catalog->places[i])) {
            return false;
        }
        for (uint16_t j = (uint16_t)(i + 1U); j < catalog->count; ++j) {
            if (catalog->places[i].place_id == catalog->places[j].place_id) {
                return false;
            }
        }
    }
    return true;
}

size_t city_place_catalog_encoded_size(const city_place_catalog_t *catalog)
{
    if (catalog == NULL || catalog->count > CITY_PLACE_MAX_COUNT) {
        return 0U;
    }
    return CITY_PLACE_CATALOG_HEADER_BYTES +
           ((size_t)catalog->count * CITY_PLACE_PROFILE_ENCODED_BYTES) +
           CITY_PLACE_CATALOG_CHECKSUM_BYTES;
}

bool city_place_catalog_encode(
    const city_place_catalog_t *catalog,
    uint8_t *output,
    size_t capacity,
    size_t *written)
{
    const size_t required = city_place_catalog_encoded_size(catalog);
    if (required == 0U || output == NULL || written == NULL ||
        capacity < required) {
        return false;
    }
    if (!catalog_is_valid(catalog)) {
        return false;
    }

    memset(output, 0, required);
    write_u32_le(output, CITY_PLACE_CATALOG_MAGIC);
    write_u16_le(output + 4U, CITY_PLACE_PROFILE_SCHEMA_VERSION);
    write_u16_le(output + 6U, catalog->count);

    size_t offset = CITY_PLACE_CATALOG_HEADER_BYTES;
    for (uint16_t i = 0; i < catalog->count; ++i) {
        const city_place_profile_t *profile = &catalog->places[i];
        write_u16_le(output + offset, profile->schema_version);
        write_u16_le(output + offset + 2U, profile->place_id);
        write_u16_le(output + offset + 4U, profile->confidence_permille);
        output[offset + 6U] = profile->fingerprint.count;
        write_u64_le(output + offset + 8U, profile->last_confirmed_ms);
        for (uint8_t token = 0; token < profile->fingerprint.count; ++token) {
            write_u64_le(
                output + offset + 16U + ((size_t)token * 8U),
                profile->fingerprint.tokens[token]);
        }
        offset += CITY_PLACE_PROFILE_ENCODED_BYTES;
    }

    write_u32_le(output + required - CITY_PLACE_CATALOG_CHECKSUM_BYTES,
                 crc32(output, required - CITY_PLACE_CATALOG_CHECKSUM_BYTES));
    *written = required;
    return true;
}

bool city_place_catalog_decode(
    const uint8_t *data,
    size_t length,
    city_place_catalog_t *catalog)
{
    if (data == NULL || catalog == NULL ||
        length < CITY_PLACE_CATALOG_HEADER_BYTES +
                     CITY_PLACE_CATALOG_CHECKSUM_BYTES ||
        read_u32_le(data) != CITY_PLACE_CATALOG_MAGIC ||
        read_u16_le(data + 4U) != CITY_PLACE_PROFILE_SCHEMA_VERSION) {
        return false;
    }

    const uint16_t count = read_u16_le(data + 6U);
    if (count > CITY_PLACE_MAX_COUNT) {
        return false;
    }
    const size_t expected =
        CITY_PLACE_CATALOG_HEADER_BYTES +
        ((size_t)count * CITY_PLACE_PROFILE_ENCODED_BYTES) +
        CITY_PLACE_CATALOG_CHECKSUM_BYTES;
    if (length != expected ||
        read_u32_le(data + length - CITY_PLACE_CATALOG_CHECKSUM_BYTES) !=
            crc32(data, length - CITY_PLACE_CATALOG_CHECKSUM_BYTES)) {
        return false;
    }

    city_place_catalog_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    decoded.count = count;

    size_t offset = CITY_PLACE_CATALOG_HEADER_BYTES;
    for (uint16_t i = 0; i < count; ++i) {
        city_place_profile_t *profile = &decoded.places[i];
        profile->schema_version = read_u16_le(data + offset);
        profile->place_id = read_u16_le(data + offset + 2U);
        profile->confidence_permille = read_u16_le(data + offset + 4U);
        profile->fingerprint.count = data[offset + 6U];
        profile->last_confirmed_ms = read_u64_le(data + offset + 8U);
        if (profile->fingerprint.count > CITY_PLACE_FINGERPRINT_TOKENS) {
            return false;
        }
        for (uint8_t token = 0; token < profile->fingerprint.count; ++token) {
            profile->fingerprint.tokens[token] = read_u64_le(
                data + offset + 16U + ((size_t)token * 8U));
        }
        if (!profile_is_valid(profile)) {
            return false;
        }
        offset += CITY_PLACE_PROFILE_ENCODED_BYTES;
    }

    if (!catalog_is_valid(&decoded)) {
        return false;
    }
    *catalog = decoded;
    return true;
}
