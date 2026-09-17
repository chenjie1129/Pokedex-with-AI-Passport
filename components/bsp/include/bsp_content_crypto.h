#pragma once
#include "content_pack.h"
#include "mbedtls/ecp.h"
#include "mbedtls/sha256.h"

/* One worker owns this context; never share across concurrent verifications.
 * Verification may allocate inside mbedTLS. No test key or embedded private key.
 * Call free after any init attempt, and before reinitializing a live context. */
typedef struct {
    mbedtls_sha256_context hash;
    mbedtls_ecp_group group;
    mbedtls_ecp_point public_key;
    bool ready;
} bsp_content_crypto_t;
bool bsp_content_crypto_init(bsp_content_crypto_t *, const uint8_t *trusted_key, size_t size);
void bsp_content_crypto_free(bsp_content_crypto_t *);
city_pack_crypto_t bsp_content_crypto_callbacks(bsp_content_crypto_t *);
