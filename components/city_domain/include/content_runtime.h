#pragma once
#include "content_pack.h"
/* Boot worker only, before catalog consumers run. Missing/incompatible content
 * never resets progress. A platform supplies its actual font/render gate. */
typedef struct {
    const uint8_t *save;
    size_t save_bytes;
    const city_pack_t *previous;
    bool (*text_compatible)(const city_pack_species_t *);
} city_content_runtime_policy_t;
bool city_content_accept_runtime(void *, const city_pack_t *);
