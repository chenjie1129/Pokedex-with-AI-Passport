#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "place_fingerprint.h"

#define CITY_PLACE_CATALOG_MAGIC UINT32_C(0x31505343)
#define CITY_PLACE_CATALOG_HEADER_BYTES 8U
#define CITY_PLACE_PROFILE_ENCODED_BYTES 112U
#define CITY_PLACE_CATALOG_CHECKSUM_BYTES 4U
#define CITY_PLACE_CATALOG_MAX_ENCODED_BYTES                              \
    (CITY_PLACE_CATALOG_HEADER_BYTES +                                    \
     (CITY_PLACE_MAX_COUNT * CITY_PLACE_PROFILE_ENCODED_BYTES) +          \
     CITY_PLACE_CATALOG_CHECKSUM_BYTES)

size_t city_place_catalog_encoded_size(const city_place_catalog_t *catalog);

bool city_place_catalog_encode(
    const city_place_catalog_t *catalog,
    uint8_t *output,
    size_t capacity,
    size_t *written);

bool city_place_catalog_decode(
    const uint8_t *data,
    size_t length,
    city_place_catalog_t *catalog);
