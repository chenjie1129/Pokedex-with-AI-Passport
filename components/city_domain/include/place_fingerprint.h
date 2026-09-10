#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "city_environment.h"
#include "privacy_tokenizer.h"

#define CITY_PLACE_FINGERPRINT_TOKENS 12U
#define CITY_PLACE_MAX_COUNT 16U
#define CITY_PLACE_MIN_USABLE_APS 4U
#define CITY_PLACE_KNOWN_THRESHOLD_PERMILLE 600U
#define CITY_PLACE_NEW_THRESHOLD_PERMILLE 300U
#define CITY_PLACE_PROFILE_SCHEMA_VERSION 1U
#define CITY_PLACE_INVALID_ID UINT16_MAX

typedef struct {
    city_ap_token_t tokens[CITY_PLACE_FINGERPRINT_TOKENS];
    uint8_t count;
} city_place_fingerprint_t;

typedef struct {
    uint16_t schema_version;
    uint16_t place_id;
    uint16_t confidence_permille;
    uint64_t last_confirmed_ms;
    city_place_fingerprint_t fingerprint;
} city_place_profile_t;

typedef struct {
    uint16_t count;
    city_place_profile_t places[CITY_PLACE_MAX_COUNT];
} city_place_catalog_t;

typedef enum {
    CITY_PLACE_RELATION_NEW = 0,
    CITY_PLACE_RELATION_GRAY,
    CITY_PLACE_RELATION_KNOWN,
} city_place_relation_t;

typedef struct {
    bool has_profile;
    uint16_t index;
    uint16_t place_id;
    uint16_t score_permille;
} city_place_match_t;

bool city_place_fingerprint_build(
    city_place_fingerprint_t *fingerprint,
    const city_wifi_ap_observation_t *aps,
    size_t ap_count,
    const uint8_t key[CITY_PRIVACY_KEY_BYTES]);

bool city_place_fingerprint_is_usable(
    const city_place_fingerprint_t *fingerprint);

uint16_t city_place_similarity_permille(
    const city_place_fingerprint_t *left,
    const city_place_fingerprint_t *right);

city_place_relation_t city_place_classify(uint16_t score_permille);

city_place_match_t city_place_catalog_find(
    const city_place_catalog_t *catalog,
    const city_place_fingerprint_t *fingerprint);
