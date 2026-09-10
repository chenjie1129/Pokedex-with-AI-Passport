#include "privacy_tokenizer.h"

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

static void test_reference_vector(void)
{
    uint8_t key[CITY_PRIVACY_KEY_BYTES];
    uint8_t bssid[CITY_WIFI_BSSID_BYTES];
    for (uint8_t i = 0; i < CITY_PRIVACY_KEY_BYTES; ++i) {
        key[i] = i;
    }
    for (uint8_t i = 0; i < CITY_WIFI_BSSID_BYTES; ++i) {
        bssid[i] = i;
    }

    city_ap_token_t token = 0U;
    CHECK(city_privacy_tokenize_ap(key, bssid, &token));
    CHECK(token == UINT64_C(0xcbc9466e58fee3ce));
}

static void test_determinism_and_key_isolation(void)
{
    const uint8_t key_a[CITY_PRIVACY_KEY_BYTES] = {
        1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U,
        9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U,
    };
    const uint8_t key_b[CITY_PRIVACY_KEY_BYTES] = {
        16U, 15U, 14U, 13U, 12U, 11U, 10U, 9U,
        8U, 7U, 6U, 5U, 4U, 3U, 2U, 1U,
    };
    const uint8_t bssid_a[CITY_WIFI_BSSID_BYTES] =
        {0x02U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U};
    const uint8_t bssid_b[CITY_WIFI_BSSID_BYTES] =
        {0x02U, 0x11U, 0x22U, 0x33U, 0x44U, 0x56U};
    city_ap_token_t first = 0U;
    city_ap_token_t second = 0U;
    city_ap_token_t other_key = 0U;
    city_ap_token_t other_ap = 0U;

    CHECK(city_privacy_tokenize_ap(key_a, bssid_a, &first));
    CHECK(city_privacy_tokenize_ap(key_a, bssid_a, &second));
    CHECK(city_privacy_tokenize_ap(key_b, bssid_a, &other_key));
    CHECK(city_privacy_tokenize_ap(key_a, bssid_b, &other_ap));
    CHECK(first == second);
    CHECK(first != other_key);
    CHECK(first != other_ap);
}

static void test_invalid_inputs(void)
{
    const uint8_t valid_key[CITY_PRIVACY_KEY_BYTES] = {1U};
    const uint8_t zero_key[CITY_PRIVACY_KEY_BYTES] = {0U};
    const uint8_t bssid[CITY_WIFI_BSSID_BYTES] = {0U};
    city_ap_token_t token = UINT64_C(0xfeedface);

    CHECK(!city_privacy_key_is_valid(NULL));
    CHECK(!city_privacy_key_is_valid(zero_key));
    CHECK(city_privacy_key_is_valid(valid_key));
    CHECK(!city_privacy_tokenize_ap(NULL, bssid, &token));
    CHECK(!city_privacy_tokenize_ap(zero_key, bssid, &token));
    CHECK(!city_privacy_tokenize_ap(valid_key, NULL, &token));
    CHECK(!city_privacy_tokenize_ap(valid_key, bssid, NULL));
    CHECK(token == UINT64_C(0xfeedface));
}

int main(void)
{
    test_reference_vector();
    test_determinism_and_key_isolation();
    test_invalid_inputs();

    if (failures == 0) {
        puts("test_privacy_tokenizer: ALL PASS");
        return 0;
    }
    fprintf(stderr, "test_privacy_tokenizer: %d failure(s)\n", failures);
    return 1;
}
