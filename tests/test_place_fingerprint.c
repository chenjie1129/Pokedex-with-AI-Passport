#include "place_fingerprint.h"
#include "place_profile_codec.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition)                                                  \
    do {                                                                  \
        if (!(condition)) {                                               \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,       \
                    #condition);                                          \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

static const uint8_t test_key[CITY_PRIVACY_KEY_BYTES] = {
    1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U,
    9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U,
};

static city_wifi_ap_observation_t make_ap(uint8_t id, int8_t rssi)
{
    city_wifi_ap_observation_t ap;
    memset(&ap, 0, sizeof(ap));
    ap.bssid[0] = 0x02U;
    ap.bssid[5] = id;
    ap.rssi = rssi;
    return ap;
}

static city_place_fingerprint_t make_fingerprint(
    const uint64_t *tokens,
    uint8_t count)
{
    city_place_fingerprint_t fingerprint;
    memset(&fingerprint, 0, sizeof(fingerprint));
    fingerprint.count = count;
    for (uint8_t i = 0; i < count; ++i) {
        fingerprint.tokens[i] = tokens[i];
    }
    return fingerprint;
}

static bool contains_bytes(
    const uint8_t *haystack,
    size_t haystack_length,
    const uint8_t *needle,
    size_t needle_length)
{
    if (needle_length == 0U || haystack_length < needle_length) {
        return false;
    }
    for (size_t i = 0; i <= haystack_length - needle_length; ++i) {
        if (memcmp(haystack + i, needle, needle_length) == 0) {
            return true;
        }
    }
    return false;
}

static void test_build_selects_strongest_unique_aps(void)
{
    city_wifi_ap_observation_t aps[16];
    for (uint8_t i = 0; i < 14U; ++i) {
        aps[i] = make_ap((uint8_t)(i + 1U), (int8_t)(-30 - (int8_t)i));
    }
    aps[14] = make_ap(3U, -100);
    aps[15] = make_ap(7U, -110);

    city_place_fingerprint_t fingerprint;
    CHECK(city_place_fingerprint_build(
        &fingerprint, aps, 16U, test_key));
    CHECK(fingerprint.count == CITY_PLACE_FINGERPRINT_TOKENS);

    for (uint8_t i = 0; i < CITY_PLACE_FINGERPRINT_TOKENS; ++i) {
        city_ap_token_t expected = 0U;
        CHECK(city_privacy_tokenize_ap(
            test_key, aps[i].bssid, &expected));
        CHECK(fingerprint.tokens[i] == expected);
    }
}

static void test_build_rejects_invalid_inputs(void)
{
    const uint8_t zero_key[CITY_PRIVACY_KEY_BYTES] = {0U};
    city_place_fingerprint_t fingerprint;
    city_wifi_ap_observation_t oversized[CITY_ENVIRONMENT_MAX_APS + 1U];
    memset(oversized, 0, sizeof(oversized));

    CHECK(!city_place_fingerprint_build(
        NULL, NULL, 0U, test_key));
    CHECK(!city_place_fingerprint_build(
        &fingerprint, NULL, 1U, test_key));
    CHECK(!city_place_fingerprint_build(
        &fingerprint, NULL, 0U, zero_key));
    CHECK(!city_place_fingerprint_build(
        &fingerprint,
        oversized,
        CITY_ENVIRONMENT_MAX_APS + 1U,
        test_key));
    CHECK(city_place_fingerprint_build(
        &fingerprint, NULL, 0U, test_key));
    CHECK(fingerprint.count == 0U);
    CHECK(!city_place_fingerprint_is_usable(&fingerprint));
}

static void test_similarity_and_boundaries(void)
{
    const uint64_t base_tokens[10] =
        {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U};
    const uint64_t seven_shared[10] =
        {1U, 2U, 3U, 4U, 5U, 6U, 7U, 20U, 21U, 22U};
    const uint64_t six_shared[10] =
        {1U, 2U, 3U, 4U, 5U, 6U, 20U, 21U, 22U, 23U};
    const uint64_t three_shared[10] =
        {1U, 2U, 3U, 20U, 21U, 22U, 23U, 24U, 25U, 26U};
    const uint64_t two_shared[10] =
        {1U, 2U, 20U, 21U, 22U, 23U, 24U, 25U, 26U, 27U};
    const city_place_fingerprint_t base =
        make_fingerprint(base_tokens, 10U);
    const city_place_fingerprint_t known =
        make_fingerprint(seven_shared, 10U);
    const city_place_fingerprint_t gray_high =
        make_fingerprint(six_shared, 10U);
    const city_place_fingerprint_t gray_low =
        make_fingerprint(three_shared, 10U);
    const city_place_fingerprint_t new_place =
        make_fingerprint(two_shared, 10U);

    CHECK(city_place_similarity_permille(&base, &known) == 700U);
    CHECK(city_place_classify(700U) == CITY_PLACE_RELATION_KNOWN);
    CHECK(city_place_similarity_permille(&base, &gray_high) == 600U);
    CHECK(city_place_classify(600U) == CITY_PLACE_RELATION_GRAY);
    CHECK(city_place_similarity_permille(&base, &gray_low) == 300U);
    CHECK(city_place_classify(300U) == CITY_PLACE_RELATION_GRAY);
    CHECK(city_place_similarity_permille(&base, &new_place) == 200U);
    CHECK(city_place_classify(200U) == CITY_PLACE_RELATION_NEW);
}

static void test_catalog_returns_best_profile(void)
{
    const uint64_t first_tokens[4] = {1U, 2U, 3U, 4U};
    const uint64_t second_tokens[4] = {10U, 11U, 12U, 13U};
    const uint64_t query_tokens[4] = {10U, 11U, 12U, 99U};
    city_place_catalog_t catalog;
    memset(&catalog, 0, sizeof(catalog));
    catalog.count = 2U;
    catalog.places[0].place_id = 7U;
    catalog.places[0].fingerprint =
        make_fingerprint(first_tokens, 4U);
    catalog.places[1].place_id = 9U;
    catalog.places[1].fingerprint =
        make_fingerprint(second_tokens, 4U);
    const city_place_fingerprint_t query =
        make_fingerprint(query_tokens, 4U);

    const city_place_match_t match =
        city_place_catalog_find(&catalog, &query);
    CHECK(match.has_profile);
    CHECK(match.index == 1U);
    CHECK(match.place_id == 9U);
    CHECK(match.score_permille == 750U);
}

static void test_codec_round_trip_and_integrity(void)
{
    const uint8_t raw_bssid[CITY_WIFI_BSSID_BYTES] =
        {0x02U, 0xaaU, 0xbbU, 0xccU, 0xddU, 0xeeU};
    city_wifi_ap_observation_t aps[4] = {
        make_ap(1U, -30),
        make_ap(2U, -40),
        make_ap(3U, -50),
        make_ap(4U, -60),
    };
    memcpy(aps[0].bssid, raw_bssid, sizeof(raw_bssid));

    city_place_catalog_t original;
    memset(&original, 0, sizeof(original));
    original.count = 1U;
    original.places[0].schema_version =
        CITY_PLACE_PROFILE_SCHEMA_VERSION;
    original.places[0].place_id = 42U;
    original.places[0].confidence_permille = 875U;
    original.places[0].last_confirmed_ms = UINT64_C(123456789);
    CHECK(city_place_fingerprint_build(
        &original.places[0].fingerprint, aps, 4U, test_key));

    uint8_t encoded[CITY_PLACE_CATALOG_MAX_ENCODED_BYTES];
    size_t written = 0U;
    CHECK(city_place_catalog_encode(
        &original, encoded, sizeof(encoded), &written));
    CHECK(written == city_place_catalog_encoded_size(&original));
    CHECK(!contains_bytes(
        encoded, written, raw_bssid, sizeof(raw_bssid)));

    city_place_catalog_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    CHECK(city_place_catalog_decode(encoded, written, &decoded));
    CHECK(decoded.count == 1U);
    CHECK(decoded.places[0].place_id == 42U);
    CHECK(decoded.places[0].confidence_permille == 875U);
    CHECK(decoded.places[0].last_confirmed_ms == UINT64_C(123456789));
    CHECK(memcmp(
              &decoded.places[0].fingerprint,
              &original.places[0].fingerprint,
              sizeof(city_place_fingerprint_t)) == 0);

    encoded[20] ^= 0x01U;
    CHECK(!city_place_catalog_decode(encoded, written, &decoded));
}

static void test_codec_rejects_ambiguous_catalog(void)
{
    const uint64_t tokens[4] = {1U, 2U, 3U, 4U};
    city_place_catalog_t catalog;
    memset(&catalog, 0, sizeof(catalog));
    catalog.count = 2U;
    for (uint16_t i = 0; i < catalog.count; ++i) {
        catalog.places[i].schema_version =
            CITY_PLACE_PROFILE_SCHEMA_VERSION;
        catalog.places[i].place_id = 7U;
        catalog.places[i].confidence_permille = 700U;
        catalog.places[i].fingerprint =
            make_fingerprint(tokens, 4U);
    }

    uint8_t encoded[CITY_PLACE_CATALOG_MAX_ENCODED_BYTES];
    size_t written = 0U;
    CHECK(!city_place_catalog_encode(
        &catalog, encoded, sizeof(encoded), &written));

    catalog.count = 1U;
    catalog.places[0].fingerprint.tokens[3] =
        catalog.places[0].fingerprint.tokens[2];
    CHECK(!city_place_catalog_encode(
        &catalog, encoded, sizeof(encoded), &written));
}

int main(void)
{
    test_build_selects_strongest_unique_aps();
    test_build_rejects_invalid_inputs();
    test_similarity_and_boundaries();
    test_catalog_returns_best_profile();
    test_codec_round_trip_and_integrity();
    test_codec_rejects_ambiguous_catalog();

    if (failures == 0) {
        puts("test_place_fingerprint: ALL PASS");
        return 0;
    }
    fprintf(stderr, "test_place_fingerprint: %d failure(s)\n", failures);
    return 1;
}
