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

/* Boot-selected immutable catalog. Callbacks return numeric traits by copy;
 * strings in these copies are intentionally NULL for package content. Bind only
 * before consumers start. No callback may mutate storage or allocate by count. */
typedef struct {
    void *context;
    uint32_t count;
    bool (*at)(void *, uint32_t, city_species_definition_t *);
    bool (*find)(void *, uint16_t, city_species_definition_t *);
} city_runtime_catalog_t;
bool city_catalog_bind(const city_runtime_catalog_t *);
uint32_t city_species_count(void);
bool city_species_get(uint16_t, city_species_definition_t *);
bool city_species_get_at(uint32_t, city_species_definition_t *);
uint32_t city_species_position(uint16_t);
uint16_t city_species_runtime_id(uint32_t);

#include "content_pack.h"
bool city_catalog_bind_pack(const city_pack_t *);
