#pragma once

#include "user_settings.h"

const char *ui_text(city_language_t language, const char *english);
const char *ui_species_name(city_language_t language, const char *english);
const char *ui_species_type(city_language_t language, const char *english);
const char *ui_species_description(city_language_t language, const char *english);

/* context: 0 detail, 1 new place together, 2 revisit, 3 completed recovery. */
const char *ui_personality_name(city_language_t, uint8_t);
const char *ui_personality_line(city_language_t, uint8_t, uint8_t, uint8_t, uint32_t);

/* Compact saved-fact and invitation wording; no generated claims. */
const char *ui_memory_line(city_language_t, uint8_t);
const char *ui_invitation_line(city_language_t, uint8_t);
