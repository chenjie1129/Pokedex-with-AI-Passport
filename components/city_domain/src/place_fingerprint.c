#include "place_fingerprint.h"

#include <string.h>

static bool token_exists(
    const city_place_fingerprint_t *fingerprint,
    city_ap_token_t token)
{
    for (uint8_t i = 0; i < fingerprint->count; ++i) {
        if (fingerprint->tokens[i] == token) {
            return true;
        }
    }
    return false;
}

bool city_place_fingerprint_build(
    city_place_fingerprint_t *fingerprint,
    const city_wifi_ap_observation_t *aps,
    size_t ap_count,
    const uint8_t key[CITY_PRIVACY_KEY_BYTES])
{
    if (fingerprint == NULL || ap_count > CITY_ENVIRONMENT_MAX_APS ||
        !city_privacy_key_is_valid(key) ||
        (aps == NULL && ap_count != 0U)) {
        return false;
    }

    memset(fingerprint, 0, sizeof(*fingerprint));
    while (fingerprint->count < CITY_PLACE_FINGERPRINT_TOKENS) {
        size_t strongest = ap_count;

        for (size_t i = 0; i < ap_count; ++i) {
            bool already_selected = false;
            for (uint8_t j = 0; j < fingerprint->count; ++j) {
                city_ap_token_t token = 0U;
                if (!city_privacy_tokenize_ap(key, aps[i].bssid, &token)) {
                    return false;
                }
                if (fingerprint->tokens[j] == token) {
                    already_selected = true;
                    break;
                }
            }
            if (already_selected) {
                continue;
            }

            if (strongest == ap_count || aps[i].rssi > aps[strongest].rssi) {
                strongest = i;
            }
        }

        if (strongest == ap_count) {
            break;
        }

        city_ap_token_t token = 0U;
        if (!city_privacy_tokenize_ap(key, aps[strongest].bssid, &token)) {
            return false;
        }
        if (!token_exists(fingerprint, token)) {
            fingerprint->tokens[fingerprint->count++] = token;
        }
    }

    return true;
}

bool city_place_fingerprint_is_usable(
    const city_place_fingerprint_t *fingerprint)
{
    return fingerprint != NULL &&
           fingerprint->count >= CITY_PLACE_MIN_USABLE_APS &&
           fingerprint->count <= CITY_PLACE_FINGERPRINT_TOKENS;
}

uint16_t city_place_similarity_permille(
    const city_place_fingerprint_t *left,
    const city_place_fingerprint_t *right)
{
    if (!city_place_fingerprint_is_usable(left) ||
        !city_place_fingerprint_is_usable(right)) {
        return 0U;
    }

    uint16_t intersection = 0U;
    for (uint8_t i = 0; i < left->count; ++i) {
        if (token_exists(right, left->tokens[i])) {
            ++intersection;
        }
    }

    const uint8_t denominator =
        left->count < right->count ? left->count : right->count;
    return (uint16_t)(((uint32_t)intersection * 1000U) / denominator);
}

city_place_relation_t city_place_classify(uint16_t score_permille)
{
    if (score_permille > CITY_PLACE_KNOWN_THRESHOLD_PERMILLE) {
        return CITY_PLACE_RELATION_KNOWN;
    }
    if (score_permille >= CITY_PLACE_NEW_THRESHOLD_PERMILLE) {
        return CITY_PLACE_RELATION_GRAY;
    }
    return CITY_PLACE_RELATION_NEW;
}

city_place_match_t city_place_catalog_find(
    const city_place_catalog_t *catalog,
    const city_place_fingerprint_t *fingerprint)
{
    city_place_match_t match = {
        .has_profile = false,
        .index = CITY_PLACE_INVALID_ID,
        .place_id = CITY_PLACE_INVALID_ID,
        .score_permille = 0U,
    };
    if (catalog == NULL || !city_place_fingerprint_is_usable(fingerprint)) {
        return match;
    }

    const uint16_t count =
        catalog->count < CITY_PLACE_MAX_COUNT
            ? catalog->count
            : CITY_PLACE_MAX_COUNT;
    for (uint16_t i = 0; i < count; ++i) {
        const uint16_t score = city_place_similarity_permille(
            &catalog->places[i].fingerprint, fingerprint);
        if (!match.has_profile || score > match.score_permille) {
            match.has_profile = true;
            match.index = i;
            match.place_id = catalog->places[i].place_id;
            match.score_permille = score;
        }
    }
    return match;
}
