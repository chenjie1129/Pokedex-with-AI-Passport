#include "pokemon_cries.h"
#include "pokemon_cries.inc"

const pokemon_cry_t *pokemon_cry_find(uint16_t species_id)
{
    for (size_t i = 0; i < sizeof(cries) / sizeof(cries[0]); ++i)
        if (cries[i].species_id == species_id) return &cries[i];
    return NULL;
}
