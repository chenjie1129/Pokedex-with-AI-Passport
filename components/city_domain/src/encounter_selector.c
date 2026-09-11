#include "encounter_selector.h"

#include <stddef.h>

#include "place_fingerprint.h"

static const uint16_t encounter_species[CITY_SPECIES_COUNT] = {
    CITY_SPECIES_CHARMANDER,
    CITY_SPECIES_BULBASAUR,
    CITY_SPECIES_SQUIRTLE,
};

static uint32_t mix32(uint32_t value)
{
    value ^= value >> 16U;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15U;
    value *= UINT32_C(0x846ca68b);
    value ^= value >> 16U;
    return value;
}

static size_t species_index(uint16_t species_id)
{
    for (size_t i = 0U; i < CITY_SPECIES_COUNT; ++i) {
        if (encounter_species[i] == species_id) {
            return i;
        }
    }
    return CITY_SPECIES_COUNT;
}

uint8_t city_encounter_place_weight(
    uint16_t place_id,
    uint16_t species_id)
{
    if (place_id == CITY_PLACE_INVALID_ID) {
        return 0U;
    }
    const size_t requested = species_index(species_id);
    if (requested == CITY_SPECIES_COUNT) {
        return 0U;
    }

    const size_t affinity = (size_t)((place_id - 1U) %
                                     CITY_SPECIES_COUNT);
    if (requested == affinity) {
        return 60U;
    }
    if (requested == (affinity + 1U) % CITY_SPECIES_COUNT) {
        return 25U;
    }
    return 15U;
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

bool city_encounter_select(
    uint16_t place_id,
    bool first_encounter_at_place,
    const city_bestiary_t *bestiary,
    uint32_t seed,
    city_encounter_selection_t *selection)
{
    if (place_id == CITY_PLACE_INVALID_ID || bestiary == NULL ||
        selection == NULL) {
        return false;
    }

    uint8_t weights[CITY_SPECIES_COUNT];
    uint16_t total = 0U;
    bool has_unknown = false;
    for (size_t i = 0U; i < CITY_SPECIES_COUNT; ++i) {
        const city_creature_record_t *record =
            city_bestiary_record_const(bestiary, encounter_species[i]);
        if (record == NULL) {
            return false;
        }
        if (record->state == CITY_DISCOVERY_UNKNOWN) {
            has_unknown = true;
        }
        weights[i] = city_encounter_place_weight(
            place_id, encounter_species[i]);
    }

    if (first_encounter_at_place && has_unknown) {
        total = 0U;
        for (size_t i = 0U; i < CITY_SPECIES_COUNT; ++i) {
            const city_creature_record_t *record =
                city_bestiary_record_const(bestiary, encounter_species[i]);
            if (record->state != CITY_DISCOVERY_UNKNOWN) {
                weights[i] = 0U;
            }
            total = (uint16_t)(total + weights[i]);
        }
    } else {
        total = 100U;
    }
    if (total == 0U) {
        return false;
    }

    uint16_t roll = (uint16_t)(mix32(
        seed ^ ((uint32_t)place_id * UINT32_C(0x9e3779b9))) % total);
    size_t selected_index = 0U;
    for (; selected_index < CITY_SPECIES_COUNT; ++selected_index) {
        if (roll < weights[selected_index]) {
            break;
        }
        roll = (uint16_t)(roll - weights[selected_index]);
    }
    if (selected_index == CITY_SPECIES_COUNT) {
        return false;
    }

    selection->species_id = encounter_species[selected_index];
    selection->stats = generate_stats(
        selection->species_id, place_id, seed);
    return true;
}

bool city_wild_encounter_select(uint32_t seed, city_encounter_selection_t *selection)
{
    if (selection == NULL) return false;
    selection->species_id = mix32(seed) % 100U < 70U
        ? CITY_SPECIES_BULBASAUR : CITY_SPECIES_SQUIRTLE;
    selection->stats = generate_stats(selection->species_id, CITY_WILD_PLACE_ID, seed);
    return true;
}
