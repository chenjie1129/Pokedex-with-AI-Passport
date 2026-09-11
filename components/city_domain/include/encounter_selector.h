#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bestiary_service.h"

typedef struct {
    uint16_t species_id;
    city_creature_stats_t stats;
} city_encounter_selection_t;

uint8_t city_encounter_place_weight(
    uint16_t place_id,
    uint16_t species_id);

bool city_encounter_select(
    uint16_t place_id,
    bool first_encounter_at_place,
    const city_bestiary_t *bestiary,
    uint32_t seed,
    city_encounter_selection_t *selection);
