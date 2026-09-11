#pragma once

#include "bestiary_service.h"
#include "esp_err.h"

#include <stdbool.h>

typedef struct {
    const char *namespace_name;
    const char *blob_key;
    const char *legacy_count_key;
    const char *legacy_blob_key;
} bsp_bestiary_store_t;

extern const bsp_bestiary_store_t BSP_BESTIARY_STORE_DEFAULT;

esp_err_t bsp_bestiary_store_load(
    const bsp_bestiary_store_t *store,
    city_bestiary_t *bestiary,
    bool *migrated);

bool bsp_bestiary_store_persist(
    const city_bestiary_t *next,
    void *context);
