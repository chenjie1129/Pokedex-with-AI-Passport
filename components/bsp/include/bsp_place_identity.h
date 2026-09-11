#pragma once

#include "esp_err.h"
#include "privacy_tokenizer.h"

#include <stdbool.h>

typedef struct {
    const char *namespace_name;
    const char *identity_key;
    const char *catalog_key;
} bsp_place_identity_store_t;

extern const bsp_place_identity_store_t BSP_PLACE_IDENTITY_STORE_DEFAULT;

/*
 * Loads the device-private fingerprint key, creating it only when no place
 * catalog exists. Callers must serialize this operation with place writes.
 */
esp_err_t bsp_place_identity_load_or_create(
    const bsp_place_identity_store_t *store,
    uint8_t key[CITY_PRIVACY_KEY_BYTES],
    bool *created);

/* Erases the identity and catalog together for an explicit factory reset. */
esp_err_t bsp_place_identity_reset(
    const bsp_place_identity_store_t *store);
