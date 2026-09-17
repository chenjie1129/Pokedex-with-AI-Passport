#include "catalog_provider.h"
#include <stddef.h>
#include "species_catalog.inc"

static const city_species_definition_t *compiled_at(uint8_t index)
{
    return index < CITY_SPECIES_COUNT ? &species_definitions[index] : NULL;
}
const city_catalog_provider_t CITY_CATALOG_DEFAULT = {
    .revision = CITY_CATALOG_VERSION,
    .count = CITY_SPECIES_COUNT,
    .at = compiled_at,
};

const city_species_definition_t *city_species_definition(uint16_t species_id)
{
    for (size_t i = 0U; i < CITY_SPECIES_COUNT; ++i) {
        if (CITY_CATALOG_DEFAULT.at(i)->species_id == species_id) {
            return CITY_CATALOG_DEFAULT.at(i);
        }
    }
    return NULL;
}

uint16_t city_species_id_at(uint8_t index)
{
    return index < CITY_SPECIES_COUNT ? CITY_CATALOG_DEFAULT.at(index)->species_id : UINT16_MAX;
}
uint8_t city_species_index(uint16_t species_id)
{
    for (uint8_t i = 0; i < CITY_SPECIES_COUNT; ++i)
        if (CITY_CATALOG_DEFAULT.at(i)->species_id == species_id) return i;
    return CITY_SPECIES_COUNT;
}
