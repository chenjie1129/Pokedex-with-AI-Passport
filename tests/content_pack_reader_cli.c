/* Host adapter exercises the exact portable reader with real Ed25519/SHA256.
 * This is not linked into device firmware. */
#include "content_pack.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

typedef struct {
    FILE *file;
    EVP_MD_CTX *hash;
    EVP_PKEY *key;
    const char *fault;
    size_t largest_read;
    uint32_t payload, payload_reads;
    bool authenticated;
} context_t;

static bool read_bytes(void *arg, uint32_t offset, void *out, size_t size)
{
    context_t *c = arg;
    if (size > c->largest_read) c->largest_read = size;
    assert(size <= CITY_PACK_READ_BYTES);
    if (offset >= c->payload) {
        assert(c->authenticated); /* Never expose payload before authentication. */
        ++c->payload_reads;
    }
    if (!strcmp(c->fault, "read")) return false;
    return fseek(c->file, (long)offset, SEEK_SET) == 0 && fread(out, 1, size, c->file) == size;
}
static bool hash_begin(void *arg)
{
    context_t *c = arg;
    return strcmp(c->fault, "hash") && EVP_DigestInit_ex(c->hash, EVP_sha256(), NULL) == 1;
}
static bool hash_update(void *arg, const void *bytes, size_t size)
{
    context_t *c = arg;
    return strcmp(c->fault, "hash-update") && EVP_DigestUpdate(c->hash, bytes, size) == 1;
}
static bool hash_finish(void *arg, uint8_t digest[32])
{
    unsigned size = 0;
    context_t *c = arg;
    return strcmp(c->fault, "hash-finish") && EVP_DigestFinal_ex(c->hash, digest, &size) == 1 && size == 32;
}
static bool verify(void *arg, const uint8_t digest[32], const uint8_t signature[64])
{
    context_t *c = arg;
    uint8_t message[sizeof(CITY_PACK_DOMAIN) + 32];
    memcpy(message, CITY_PACK_DOMAIN, sizeof(CITY_PACK_DOMAIN));
    memcpy(message + sizeof(CITY_PACK_DOMAIN), digest, 32);
    EVP_MD_CTX *md = EVP_MD_CTX_new();
    bool ok = md && strcmp(c->fault, "signature") && EVP_PKEY_is_a(c->key, "ED25519") &&
        EVP_DigestVerifyInit(md, NULL, NULL, NULL, c->key) == 1 &&
        EVP_DigestVerify(md, signature, 64, message, sizeof(message)) == 1;
    EVP_MD_CTX_free(md);
    c->authenticated = ok;
    return ok;
}

int main(int argc, char **argv)
{
    assert(argc == 3 || argc == 4);
    context_t c = {.fault = argc == 4 ? argv[3] : "", .payload = UINT32_MAX};
    c.file = fopen(argv[1], "rb"); assert(c.file);
    assert(!fseek(c.file, 0, SEEK_END)); long bytes = ftell(c.file); assert(bytes >= 0);
    assert(!fseek(c.file, 0, SEEK_SET));
    uint8_t header[28];
    if (fread(header, 1, sizeof(header), c.file) == sizeof(header)) {
        uint32_t count = (uint32_t)header[16] | (uint32_t)header[17] << 8 |
                         (uint32_t)header[18] << 16 | (uint32_t)header[19] << 24;
        if (count <= 30000) c.payload = 28 + count * 44 + 64;
    }
    FILE *key = fopen(argv[2], "rb"); assert(key);
    c.key = PEM_read_PUBKEY(key, NULL, NULL, NULL); fclose(key); assert(c.key);
    c.hash = EVP_MD_CTX_new(); assert(c.hash);
    city_pack_crypto_t crypto = {&c, hash_begin, hash_update, hash_finish, verify};
    if (!strcmp(c.fault, "no-crypto")) crypto.verify = NULL;
    city_pack_t pack;
    memset(&pack, 0xa5, sizeof(pack));
    bool ok = city_pack_open(&pack, read_bytes, &c, (uint32_t)bytes,
                            !strcmp(c.fault, "budget") ? 100 : CITY_PACK_MAX_BYTES, &crypto);
    if (ok) {
        /* Four rows at a time, all entries, preserving each caller-owned row. */
        for (uint32_t i = 0; i < pack.count; i += 4) {
            city_pack_species_t rows[4];
            for (uint32_t j = 0; j < 4 && i + j < pack.count; ++j) {
                assert(city_pack_at(&pack, i + j, &rows[j]));
                assert(rows[j].name_en[0] && rows[j].name_zh[0]);
                if (j) assert(rows[j].species_id > rows[j - 1].species_id);
            }
        }
        uint32_t targets[] = {0, pack.count / 2, pack.count - 1};
        for (unsigned i = 0; i < 3; ++i) {
            city_pack_species_t row, found;
            assert(city_pack_at(&pack, targets[i], &row));
            assert(city_pack_find(&pack, row.species_id, &found));
            assert(!memcmp(&row, &found, sizeof(row)));
            for (uint8_t kind = 2; kind <= 3; ++kind) {
                city_pack_asset_t asset;
                assert(city_pack_asset(&pack, row.species_id, kind, &asset));
                uint8_t block[CITY_PACK_READ_BYTES];
                assert(city_pack_read_asset(&pack, row.species_id, kind, 0, block, 12));
                assert(!city_pack_read_asset(&pack, row.species_id, kind, asset.length, block, 1));
                assert(!city_pack_read_asset(&pack, row.species_id, kind, 0, block, sizeof(block) + 1));
            }
        }
        city_pack_species_t row, saved;
        memset(&row, 0xa5, sizeof(row)); saved = row;
        assert(!city_pack_at(&pack, pack.count, &row));
        assert(!city_pack_find(&pack, UINT16_MAX, &row));
        assert(!memcmp(&row, &saved, sizeof(row)));
        c.fault = "read";
        assert(!city_pack_at(&pack, 0, &row));
        assert(!memcmp(&row, &saved, sizeof(row)));
    } else {
        city_pack_t zero = {0};
        assert(!memcmp(&pack, &zero, sizeof(pack)));
        city_pack_species_t row;
        assert(!city_pack_at(&pack, 0, &row));
    }
    printf("{\"ok\":%s,\"species\":%u,\"largest_read\":%zu,\"handle_bytes\":%zu,"
           "\"row_bytes\":%zu,\"payload_reads\":%u}\n",
           ok ? "true" : "false", (unsigned)pack.count, c.largest_read,
           sizeof(pack), sizeof(city_pack_species_t), (unsigned)c.payload_reads);
    EVP_MD_CTX_free(c.hash); EVP_PKEY_free(c.key); fclose(c.file);
    return ok ? 0 : 1;
}
