#pragma once
#include "bestiary_service.h"

/* Immutable, bounded catalog. Returned definitions live for the firmware lifetime.
 * Indices are iteration cursors only; persistence always stores species_id. */
typedef struct {
    uint16_t revision;
    uint8_t count;
    const city_species_definition_t *(*at)(uint8_t index);
} city_catalog_provider_t;

extern const city_catalog_provider_t CITY_CATALOG_DEFAULT;
