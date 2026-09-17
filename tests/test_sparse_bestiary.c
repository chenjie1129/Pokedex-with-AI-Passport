#include "bestiary_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool persist(const city_bestiary_t *b, void *arg)
{ (void)arg; return city_bestiary_is_valid(b); }
static void crc(uint8_t *data, size_t length)
{
    uint32_t value = UINT32_MAX;
    for (size_t i = 0; i < length - 4; ++i) {
        value ^= data[i];
        for (unsigned j = 0; j < 8; ++j) value = (value >> 1) ^ (0xedb88320U & (0U - (value & 1U)));
    }
    value = ~value;
    for (unsigned j = 0; j < 4; ++j) data[length - 4 + j] = (uint8_t)(value >> (8 * j));
}
static void roundtrip(const city_bestiary_t *b)
{
    uint8_t legacy[CITY_BESTIARY_ENCODED_BYTES], sparse[CITY_BESTIARY_ENCODED_BYTES], result[CITY_BESTIARY_ENCODED_BYTES];
    city_bestiary_t next; size_t length = 0;
    assert(city_bestiary_encode(b, legacy));
    assert(city_bestiary_decode(legacy, sizeof(legacy), &next));
    assert(city_bestiary_encode_sparse(&next, sparse, sizeof(sparse), &length));
    assert(length == 52U + city_bestiary_discovered_count(b) * 22U + b->owned_count * 32U);
    assert(city_bestiary_decode(sparse, length, &next));
    assert(city_bestiary_encode(&next, result)); assert(!memcmp(legacy, result, sizeof(result)));
    uint8_t bad[CITY_BESTIARY_ENCODED_BYTES];
    city_bestiary_t preserved = next;
    for (size_t i = 0; i < length; ++i) {
        memcpy(bad, sparse, length); bad[i] ^= 1;
        assert(!city_bestiary_decode(bad, length, &next)); assert(!memcmp(&preserved, &next, sizeof(next)));
    }
    for (size_t i = 0; i < length; ++i) assert(!city_bestiary_decode(sparse, i, &next));
    memcpy(bad, sparse, length);
    if (length < sizeof(bad)) { bad[length] = 0; assert(!city_bestiary_decode(bad, length + 1, &next)); }
    size_t unchanged = 999; memset(result, 0xa5, sizeof(result)); memcpy(bad, result, sizeof(result));
    assert(!city_bestiary_encode_sparse(b, result, length - 1, &unchanged));
    assert(unchanged == 999 && !memcmp(result, bad, sizeof(result)));
}
int main(void)
{
    city_bestiary_t b; city_bestiary_init(&b); roundtrip(&b);
    assert(city_bestiary_capture_personality(&b, 1, 25, 1,
        &(city_creature_stats_t){40,60,45}, CITY_PERSONALITY_PLAYFUL, persist, NULL) == CITY_BESTIARY_APPLIED);
    assert(city_bestiary_capture_personality(&b, 2, 25, 2,
        &(city_creature_stats_t){41,61,46}, CITY_PERSONALITY_CALM, persist, NULL) == CITY_BESTIARY_APPLIED);
    assert(city_bestiary_choose_buddy_instance(&b, 2, persist, NULL) == CITY_BESTIARY_APPLIED);
    assert(city_bestiary_damage_instance(&b, 2, 4, persist, NULL) == CITY_BESTIARY_APPLIED);
    assert(city_bestiary_visit(&b, 3, 2, 1, 25, persist, NULL) == CITY_BESTIARY_APPLIED);
    assert(city_bestiary_mark_seen(&b, 1, persist, NULL) == CITY_BESTIARY_APPLIED);
    b.wild_cooldown_active = true; roundtrip(&b);
    uint8_t bytes[CITY_BESTIARY_ENCODED_BYTES], bad[CITY_BESTIARY_ENCODED_BYTES]; size_t length;
    assert(city_bestiary_encode_sparse(&b, bytes, sizeof(bytes), &length));
    const unsigned invalid_offsets[] = {17, 18, 19, 36, 37, 38, 39, 51, 69};
    city_bestiary_t next;
    for (unsigned i = 0; i < sizeof(invalid_offsets)/sizeof(invalid_offsets[0]); ++i) {
        memcpy(bad, bytes, length); bad[invalid_offsets[i]] = 255; crc(bad, length);
        assert(!city_bestiary_decode(bad, length, &next));
    }
    memcpy(bad, bytes, length); memcpy(bad + 48 + 22, bad + 48, 22); crc(bad, length);
    assert(!city_bestiary_decode(bad, length, &next)); /* Duplicate ID, valid CRC. */
    memcpy(bad, bytes, length); bad[48] = 0xfe; bad[49] = 0xff; crc(bad, length);
    assert(!city_bestiary_decode(bad, length, &next)); /* Unsupported ID: never discard progress. */
    memcpy(bad, bytes, length); bad[50] = CITY_DISCOVERY_UNKNOWN; crc(bad, length);
    assert(!city_bestiary_decode(bad, length, &next));
    memcpy(bad, bytes, length); memcpy(bad+48,bytes+70,22); memcpy(bad+70,bytes+48,22); crc(bad,length);
    assert(!city_bestiary_decode(bad, length, &next)); /* Noncanonical order. */
    assert(city_bestiary_release_instance(&b, 1, persist, NULL) == CITY_BESTIARY_APPLIED); roundtrip(&b);
    assert(city_bestiary_release_instance(&b, 2, persist, NULL) == CITY_BESTIARY_APPLIED); roundtrip(&b);
    city_bestiary_init(&b);
    for (unsigned i = 0; i < CITY_MAX_OWNED_POKEMON; ++i)
        assert(city_bestiary_capture(&b, i + 1, 25, 1, persist, NULL) == CITY_BESTIARY_APPLIED);
    for (unsigned i = 0; i < CITY_SPECIES_COUNT; ++i)
        assert(city_bestiary_mark_seen(&b, city_species_id_at(i), persist, NULL) != CITY_BESTIARY_INVALID);
    assert(city_bestiary_encode_sparse(&b, bytes, sizeof(bytes), &length)); assert(length == sizeof(bytes));
    assert(city_bestiary_decode(bytes, length, &next)); assert(next.owned_count == CITY_MAX_OWNED_POKEMON);
    puts("Sparse storage: exact migration, copies, buddy, HP, memory, history, bounds and corruption passed");
    return 0;
}
