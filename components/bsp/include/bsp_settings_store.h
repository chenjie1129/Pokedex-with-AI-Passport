#pragma once
#include "esp_err.h"
#include "user_settings.h"

/* Missing preferences use defaults without writing. Errors also return safe defaults. */
esp_err_t bsp_settings_load(city_settings_t *settings);
/* Call from a worker. Separate namespace; never touches collection or place data. */
esp_err_t bsp_settings_save(const city_settings_t *settings);
