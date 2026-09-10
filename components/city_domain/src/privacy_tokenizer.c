#include "privacy_tokenizer.h"

#include <stddef.h>

static uint64_t rotate_left(uint64_t value, unsigned int shift)
{
    return (value << shift) | (value >> (64U - shift));
}

static uint64_t load_u64_le(const uint8_t bytes[8])
{
    uint64_t value = 0;
    for (size_t i = 0; i < 8U; ++i) {
        value |= ((uint64_t)bytes[i]) << (8U * i);
    }
    return value;
}

static void sip_round(uint64_t *v0, uint64_t *v1, uint64_t *v2, uint64_t *v3)
{
    *v0 += *v1;
    *v1 = rotate_left(*v1, 13U);
    *v1 ^= *v0;
    *v0 = rotate_left(*v0, 32U);
    *v2 += *v3;
    *v3 = rotate_left(*v3, 16U);
    *v3 ^= *v2;
    *v0 += *v3;
    *v3 = rotate_left(*v3, 21U);
    *v3 ^= *v0;
    *v2 += *v1;
    *v1 = rotate_left(*v1, 17U);
    *v1 ^= *v2;
    *v2 = rotate_left(*v2, 32U);
}

static uint64_t siphash24(
    const uint8_t key[CITY_PRIVACY_KEY_BYTES],
    const uint8_t *data,
    size_t length)
{
    const uint64_t k0 = load_u64_le(key);
    const uint64_t k1 = load_u64_le(key + 8U);
    uint64_t v0 = UINT64_C(0x736f6d6570736575) ^ k0;
    uint64_t v1 = UINT64_C(0x646f72616e646f6d) ^ k1;
    uint64_t v2 = UINT64_C(0x6c7967656e657261) ^ k0;
    uint64_t v3 = UINT64_C(0x7465646279746573) ^ k1;
    uint64_t final_block = ((uint64_t)length) << 56U;

    for (size_t i = 0; i < length; ++i) {
        final_block |= ((uint64_t)data[i]) << (8U * i);
    }

    v3 ^= final_block;
    sip_round(&v0, &v1, &v2, &v3);
    sip_round(&v0, &v1, &v2, &v3);
    v0 ^= final_block;
    v2 ^= UINT64_C(0xff);
    sip_round(&v0, &v1, &v2, &v3);
    sip_round(&v0, &v1, &v2, &v3);
    sip_round(&v0, &v1, &v2, &v3);
    sip_round(&v0, &v1, &v2, &v3);

    return v0 ^ v1 ^ v2 ^ v3;
}

bool city_privacy_key_is_valid(
    const uint8_t key[CITY_PRIVACY_KEY_BYTES])
{
    if (key == NULL) {
        return false;
    }

    uint8_t combined = 0U;
    for (size_t i = 0; i < CITY_PRIVACY_KEY_BYTES; ++i) {
        combined |= key[i];
    }
    return combined != 0U;
}

bool city_privacy_tokenize_ap(
    const uint8_t key[CITY_PRIVACY_KEY_BYTES],
    const uint8_t bssid[CITY_WIFI_BSSID_BYTES],
    city_ap_token_t *token)
{
    if (!city_privacy_key_is_valid(key) || bssid == NULL || token == NULL) {
        return false;
    }

    *token = siphash24(key, bssid, CITY_WIFI_BSSID_BYTES);
    return true;
}
