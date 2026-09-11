#include "encounter_selector.h"

#include <stddef.h>

#include "place_fingerprint.h"

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
    const city_species_definition_t *def = city_species_definition(species_id);
    if (place_id == 0 || place_id == CITY_PLACE_INVALID_ID || !def || def->evolves_from) return 0;
    return def->place_pool == (place_id - 1U) % 3U ? 6 : 1;
}

static city_creature_stats_t generate_stats(
    uint16_t species_id,
    uint16_t place_id,
    uint32_t seed)
{
    const city_species_definition_t *definition =
        city_species_definition(species_id);
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
        for (uint8_t i = 0; i < CITY_SPECIES_COUNT; ++i)
            if (city_encounter_place_weight(place_id, city_species_id_at(i)) && bestiary->records[i].state < priority) priority = bestiary->records[i].state;
    }
    uint16_t total = 0;
    uint8_t weights[CITY_SPECIES_COUNT];
    for (uint8_t i = 0; i < CITY_SPECIES_COUNT; ++i) {
        weights[i] = bestiary->records[i].state > priority ? 0 : city_encounter_place_weight(place_id, city_species_id_at(i));
        total += weights[i];
    }
    if (total == 0) return false;
    uint16_t roll = mix32(seed ^ ((uint32_t)place_id * UINT32_C(0x9e3779b9))) % total;
    for (uint8_t i = 0; i < CITY_SPECIES_COUNT; ++i) {
        if (roll < weights[i]) {
            selection->species_id = city_species_id_at(i);
            selection->stats = generate_stats(selection->species_id, place_id, seed);
            return true;
        }
        roll -= weights[i];
    }
    return false;
}

bool city_wild_encounter_select(uint32_t seed, city_encounter_selection_t *selection)
{
    if (!selection) return false;
    uint16_t total = 0;
    for (uint8_t i = 0; i < CITY_SPECIES_COUNT; ++i)
        total += city_species_definition(city_species_id_at(i))->wild_eligible;
    if (total == 0) return false;
    uint16_t roll = mix32(seed) % total;
    for (uint8_t i = 0; i < CITY_SPECIES_COUNT; ++i) {
        const uint16_t id = city_species_id_at(i);
        if (!city_species_definition(id)->wild_eligible) continue;
        if (roll-- == 0) {
            selection->species_id = id;
            selection->stats = generate_stats(id, CITY_WILD_PLACE_ID, seed);
            return true;
        }
    }
    return false;
}
