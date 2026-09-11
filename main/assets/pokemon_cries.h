#pragma once
#include <stddef.h>
#include <stdint.h>

#define POKEMON_CRY_SAMPLE_RATE 16000U
typedef struct {
    uint16_t species_id;
    const int16_t *samples;
    size_t sample_count;
} pokemon_cry_t;

/* Unknown IDs return NULL, never a different species' sound. */
const pokemon_cry_t *pokemon_cry_find(uint16_t species_id);
