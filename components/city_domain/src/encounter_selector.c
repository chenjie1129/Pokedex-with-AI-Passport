#include "encounter_selector.h"

#include <stddef.h>

#include "place_fingerprint.h"
#include "catalog_provider.h"

static uint32_t mix32(uint32_t value)
{
    value ^= value >> 16U;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15U;
    value *= UINT32_C(0x846ca68b);
    value ^= value >> 16U;
    return value;
}

uint8_t city_encounter_place_weight(uint16_t place_id, uint16_t species_id)
{
    city_species_definition_t copy;
    const city_species_definition_t *def = city_species_get(species_id, &copy) ? &copy : NULL;
    if (place_id == 0 || place_id == CITY_PLACE_INVALID_ID || !def || !def->place_eligible || def->evolves_from) return 0;
    return def->place_pool == (place_id - 1U) % 3U ? 6 : 1;
}

static city_creature_stats_t generate_stats(
    uint16_t species_id,
    uint16_t place_id,
    uint32_t seed)
{
    city_species_definition_t copy;
    const city_species_definition_t *definition = city_species_get(species_id, &copy) ? &copy : NULL;
    city_creature_stats_t stats = {0};
    if (definition == NULL) {
        return stats;
    }

    const uint32_t basis =
        seed ^ ((uint32_t)place_id << 16U) ^ species_id;
    stats.hp = (uint8_t)(
        definition->base_hp + (mix32(basis ^ UINT32_C(0x48500001)) % 16U));
    stats.attack = (uint8_t)(
        definition->base_attack +
        (mix32(basis ^ UINT32_C(0x4154544b)) % 16U));
    stats.defense = (uint8_t)(
        definition->base_defense +
        (mix32(basis ^ UINT32_C(0x44454600)) % 16U));
    return stats;
}

bool city_encounter_select(uint16_t place_id, bool first_encounter_at_place,
    const city_bestiary_t *bestiary, uint32_t seed, city_encounter_selection_t *selection)
{
    if (!selection || !city_bestiary_is_valid(bestiary) || place_id == 0 ||
        place_id == CITY_PLACE_INVALID_ID) return false;
    city_discovery_state_t priority = CITY_DISCOVERY_CAPTURED;
    if (first_encounter_at_place) {
        for (uint32_t i = 0; i < city_species_count(); ++i) {
            uint16_t id = city_species_runtime_id(i);
            const city_creature_record_t *record = city_bestiary_record_const(bestiary, id);
            if (!record) return false;
            if (city_encounter_place_weight(place_id, id) && record->state < priority) priority = record->state;
        }
    }
    uint32_t total = 0;
    for (uint32_t i = 0; i < city_species_count(); ++i) {
        uint16_t id = city_species_runtime_id(i);
        const city_creature_record_t *record = city_bestiary_record_const(bestiary, id);
        if (!record) return false;
        if (record->state <= priority) total += city_encounter_place_weight(place_id, id);
    }
    if (!total) return false;
    uint32_t roll = mix32(seed ^ ((uint32_t)place_id * UINT32_C(0x9e3779b9))) % total;
    for (uint32_t i = 0; i < city_species_count(); ++i) {
        uint16_t id = city_species_runtime_id(i);
        const city_creature_record_t *record = city_bestiary_record_const(bestiary, id);
        if (!record) return false;
        unsigned weight = record->state <= priority ? city_encounter_place_weight(place_id, id) : 0;
        if (roll < weight) {
            selection->species_id = id;
            selection->stats = generate_stats(id, place_id, seed); return true;
        }
        roll -= weight;
    }
    return false;
}

bool city_wild_encounter_select(uint32_t seed, city_encounter_selection_t *selection)
{
    if (!selection) return false;
    uint32_t total = 0;
    city_species_definition_t d;
    for (uint32_t i = 0; i < city_species_count(); ++i) {
        if (!city_species_get_at(i, &d)) return false;
        total += d.wild_eligible;
    }
    if (!total) return false;
    uint32_t roll = mix32(seed) % total;
    for (uint32_t i = 0; i < city_species_count(); ++i) {
        if (!city_species_get_at(i, &d)) return false;
        if (d.wild_eligible && roll-- == 0) {
            selection->species_id = d.species_id;
            selection->stats = generate_stats(d.species_id, CITY_WILD_PLACE_ID, seed); return true;
        }
    }
    return false;
}
