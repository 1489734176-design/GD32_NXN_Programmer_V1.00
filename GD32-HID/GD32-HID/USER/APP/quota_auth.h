#ifndef QUOTA_AUTH_H
#define QUOTA_AUTH_H
#include <stdint.h>

#define QUOTA_AUTH_TAG_SIZE 16U
typedef struct {
    uint32_t state[8];
    uint32_t total_low, total_high, used;
    uint8_t block[64];
} quota_sha256_ctx_t;
typedef struct {
    quota_sha256_ctx_t inner;
    uint8_t outer_pad[64];
} quota_auth_ctx_t;

void quota_sha256_init(quota_sha256_ctx_t *ctx);
void quota_sha256_update(quota_sha256_ctx_t *ctx, const void *data, uint32_t length);
void quota_sha256_final(quota_sha256_ctx_t *ctx, uint8_t out[32]);
void quota_hmac_init(quota_auth_ctx_t *ctx, const void *key, uint32_t length);
void quota_hmac_final(quota_auth_ctx_t *ctx, uint8_t out[32]);
void quota_auth_init(quota_auth_ctx_t *ctx, uint8_t domain);
void quota_auth_update(quota_auth_ctx_t *ctx, const void *data, uint32_t length);
void quota_auth_final(quota_auth_ctx_t *ctx, uint8_t out[16]);
void quota_auth_tag(uint8_t domain, const void *data, uint32_t length, uint8_t out[16]);
uint8_t quota_auth_equal(const void *left, const void *right, uint32_t length);
/* Implemented by the MCU port; the UID is a binding, not a secret key. */
void burn_quota_device_id(uint32_t out[3]);
#endif
