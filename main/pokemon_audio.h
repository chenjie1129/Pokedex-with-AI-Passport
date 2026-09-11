#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Call once at startup. Audio failure is nonfatal to exploration. */
bool pokemon_audio_init(void);
/* Nonblocking, latest request wins. A zero ID cancels playback. */
void pokemon_audio_play(uint16_t species_id);
void pokemon_audio_stop(void);
