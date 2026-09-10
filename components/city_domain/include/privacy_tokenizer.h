#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "city_environment.h"

#define CITY_PRIVACY_KEY_BYTES 16U

typedef uint64_t city_ap_token_t;

bool city_privacy_key_is_valid(
    const uint8_t key[CITY_PRIVACY_KEY_BYTES]);

bool city_privacy_tokenize_ap(
    const uint8_t key[CITY_PRIVACY_KEY_BYTES],
    const uint8_t bssid[CITY_WIFI_BSSID_BYTES],
    city_ap_token_t *token);
