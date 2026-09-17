#include "bsp_content_crypto.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/version.h"
#include <string.h>

#if MBEDTLS_VERSION_MAJOR < 3
#define SHA_START mbedtls_sha256_starts_ret
#define SHA_UPDATE mbedtls_sha256_update_ret
#define SHA_FINISH mbedtls_sha256_finish_ret
#else
#define SHA_START mbedtls_sha256_starts
#define SHA_UPDATE mbedtls_sha256_update
#define SHA_FINISH mbedtls_sha256_finish
#endif

bool bsp_content_crypto_init(bsp_content_crypto_t *c, const uint8_t *key, size_t size)
{
    if (!c) return false;
    memset(c, 0, sizeof(*c));
    mbedtls_sha256_init(&c->hash);
    mbedtls_ecp_group_init(&c->group);
    mbedtls_ecp_point_init(&c->public_key);
    c->ready = key && size == 65 && key[0] == 4 &&
        mbedtls_ecp_group_load(&c->group, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
        mbedtls_ecp_point_read_binary(&c->group, &c->public_key, key, size) == 0 &&
        mbedtls_ecp_check_pubkey(&c->group, &c->public_key) == 0;
    return c->ready;
}
void bsp_content_crypto_free(bsp_content_crypto_t *c)
{
    if (!c) return;
    mbedtls_sha256_free(&c->hash);
    mbedtls_ecp_point_free(&c->public_key);
    mbedtls_ecp_group_free(&c->group);
    c->ready = false;
}
static bool begin(void *arg)
{
    bsp_content_crypto_t *c = arg;
    return c && c->ready && SHA_START(&c->hash, 0) == 0;
}
static bool update(void *arg, const void *bytes, size_t size)
{
    bsp_content_crypto_t *c = arg;
    return c && c->ready && SHA_UPDATE(&c->hash, bytes, size) == 0;
}
static bool finish(void *arg, uint8_t digest[32])
{
    bsp_content_crypto_t *c = arg;
    return c && c->ready && SHA_FINISH(&c->hash, digest) == 0;
}
static bool verify(void *arg, uint16_t algorithm, const uint8_t digest[32], const uint8_t sig[64])
{
    bsp_content_crypto_t *c = arg;
    if (!c || !c->ready || algorithm != CITY_PACK_P256) return false;
    uint8_t hash[32];
    if (!begin(c) || !update(c, CITY_PACK_DOMAIN, sizeof(CITY_PACK_DOMAIN)) ||
        !update(c, digest, 32) || !finish(c, hash)) return false;
    mbedtls_mpi r, s;
    mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
    bool ok = mbedtls_mpi_read_binary(&r, sig, 32) == 0 &&
        mbedtls_mpi_read_binary(&s, sig + 32, 32) == 0 &&
        mbedtls_ecdsa_verify(&c->group, hash, sizeof(hash), &c->public_key, &r, &s) == 0;
    mbedtls_mpi_free(&r); mbedtls_mpi_free(&s);
    return ok;
}
city_pack_crypto_t bsp_content_crypto_callbacks(bsp_content_crypto_t *c)
{
    return (city_pack_crypto_t){c, begin, update, finish, verify};
}
