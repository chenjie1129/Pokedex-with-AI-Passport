#pragma once
#include "catalog_provider.h"
#include "user_settings.h"
#include "lvgl.h"
/* Boot owns verification/activation; afterward storage and provider are immutable.
 * UI metadata cache belongs to LVGL; audio cache belongs to the audio worker. */
bool pokemon_content_boot(void);
bool pokemon_content_active(void);
const city_species_definition_t *pokemon_content_definition(uint16_t, city_language_t);
const lv_image_dsc_t *pokemon_content_sprite(uint16_t);
bool pokemon_content_cry_size(uint16_t, uint32_t *samples);
bool pokemon_content_cry_read(uint16_t, uint32_t sample, void *, size_t bytes);
/* Read-only cache and pagination diagnostic; boot/smoke worker under UI lock. */
bool pokemon_content_verify_cache(void);
