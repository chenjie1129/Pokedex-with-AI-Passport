#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
bool pokemon_content_cry_size(uint16_t, uint32_t *);
bool pokemon_content_cry_read(uint16_t, uint32_t, void *, size_t);
