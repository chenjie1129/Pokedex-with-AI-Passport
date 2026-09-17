#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CITY_PACK_READ_BYTES 512U
#define CITY_PACK_MAX_BYTES (8U * 1024U * 1024U)
#define CITY_PACK_MAX_SPECIES 10000U
#define CITY_PACK_ED25519 1U
#define CITY_PACK_P256 2U
#define CITY_PACK_DOMAIN "CityPassport.ContentPack.v1"

/* Storage must be immutable from open through the final read. These callbacks
 * belong in adapters, never UI callbacks. No allocation or I/O policy here. */
typedef bool (*city_pack_read_fn)(void *, uint32_t, void *, size_t);
typedef struct {
    void *context;
    bool (*begin)(void *);
    bool (*update)(void *, const void *, size_t);
    bool (*finish)(void *, uint8_t digest[32]);
    /* Algorithm 1: Ed25519 over DOMAIN including NUL then digest.
     * Algorithm 2: P-256/SHA256 over that same message, raw big-endian r || s,
     * using an out-of-band trusted public key. No accept-all production stub. */
    bool (*verify)(void *, uint16_t algorithm, const uint8_t digest[32], const uint8_t signature[64]);
} city_pack_crypto_t;

typedef struct {
    uint16_t species_id, evolves_from;
    uint8_t flags, pool, type1, type2, hp, attack, defense;
    char name_en[33], name_zh[49], description_en[91], description_zh[181];
} city_pack_species_t;

typedef struct {
    uint32_t offset, length;
} city_pack_asset_t;

typedef struct {
    city_pack_read_fn read;
    void *context;
    uint32_t bytes, revision, count, payload;
    uint8_t manifest[32];
    bool verified;
} city_pack_t;

/* Clears out on any failure. Validates structure, signature, all SHA-256 hashes,
 * typed payloads and evolution references. Does not activate or modify saves. */
bool city_pack_open(city_pack_t *out, city_pack_read_fn read, void *context,
                    uint32_t bytes, uint32_t budget, const city_pack_crypto_t *crypto);
/* Output is caller-owned and unchanged on error; no pointer into a shared cache.
 * at/find are O(1)/O(log N) index reads. They require immutable storage. */
bool city_pack_at(const city_pack_t *, uint32_t index, city_pack_species_t *out);
bool city_pack_find(const city_pack_t *, uint16_t id, city_pack_species_t *out);
bool city_pack_asset(const city_pack_t *, uint16_t id, uint8_t kind, city_pack_asset_t *out);
bool city_pack_read_asset(const city_pack_t *, uint16_t id, uint8_t kind,
                          uint32_t offset, void *out, size_t length);
