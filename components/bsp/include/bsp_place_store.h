#pragma once

#include "esp_err.h"
#include "place_fingerprint.h"

#include <stdbool.h>

typedef struct {
    const char *namespace_name;
    const char *blob_key;
} bsp_place_store_t;

extern const bsp_place_store_t BSP_PLACE_STORE_DEFAULT;

esp_err_t bsp_place_store_load(
    const bsp_place_store_t *store,
    city_place_catalog_t *catalog,
    bool *found);

esp_err_t bsp_place_store_save(
    const bsp_place_store_t *store,
    const city_place_catalog_t *catalog);
