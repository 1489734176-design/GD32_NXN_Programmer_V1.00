/* HMAC-SHA256 authentication (128-bit tag), not data encryption.
 * Keep this build secret stable across updates. Hardware read protection and
 * administrative authorization are separate from this two-copy consistency check. */
#include "quota_auth.h"
#include <string.h>

static const uint8_t firmware_secret[32] = {
    0x98,0x2c,0x54,0xa5,0xe2,0x6b,0x0e,0xf9,
    0x41,0x97,0x8e,0xfd,0xa0,0x65,0x39,0xf0,
    0x29,0x53,0x89,0x8d,0xfd,0x38,0xe5,0xf9,
    0x95,0x77,0xe8,0x90,0x2a,0xcf,0x09,0x27
};
static const uint32_t round_constant[64] = {
    0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
    0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
    0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
    0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
    0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
    0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
    0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
    0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U
};
#define ROR(x,n) (((x) >> (n)) | ((x) << (32U-(n))))
#define SMALL0(x) (ROR(x,7)^ROR(x,18)^((x)>>3))
#define SMALL1(x) (ROR(x,17)^ROR(x,19)^((x)>>10))

static void transform(quota_sha256_ctx_t *ctx)
{
    uint32_t w[16], a,b,c,d,e,f,g,h,t1,t2,x,y;
    uint8_t i, j;
    for(i=0;i<16;i++) {
        j=(uint8_t)(i*4U);
        w[i]=((uint32_t)ctx->block[j]<<24)|((uint32_t)ctx->block[j+1]<<16)|
             ((uint32_t)ctx->block[j+2]<<8)|ctx->block[j+3];
    }
    a=ctx->state[0]; b=ctx->state[1]; c=ctx->state[2]; d=ctx->state[3];
    e=ctx->state[4]; f=ctx->state[5]; g=ctx->state[6]; h=ctx->state[7];
    for(i=0;i<64;i++) {
        if(i>=16) {
            x=w[(i-15U)&15U]; y=w[(i-2U)&15U];
            w[i&15U]+=SMALL0(x)+w[(i-7U)&15U]+SMALL1(y);
        }
        t1=h+(ROR(e,6)^ROR(e,11)^ROR(e,25))+((e&f)^((~e)&g))+round_constant[i]+w[i&15U];
        t2=(ROR(a,2)^ROR(a,13)^ROR(a,22))+((a&b)^(a&c)^(b&c));
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    ctx->state[0]+=a; ctx->state[1]+=b; ctx->state[2]+=c; ctx->state[3]+=d;
    ctx->state[4]+=e; ctx->state[5]+=f; ctx->state[6]+=g; ctx->state[7]+=h;
}

void quota_sha256_init(quota_sha256_ctx_t *ctx)
{
    static const uint32_t initial[8]={0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,
                                    0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};
    memcpy(ctx->state,initial,sizeof(initial));
    ctx->total_low=ctx->total_high=ctx->used=0;
}

void quota_sha256_update(quota_sha256_ctx_t *ctx, const void *data, uint32_t length)
{
    const uint8_t *bytes=(const uint8_t *)data;
    uint32_t old=ctx->total_low, take;
    ctx->total_low+=length;
    if(ctx->total_low<old) ctx->total_high++;
    while(length) {
        take=64U-ctx->used;
        if(take>length) take=length;
        memcpy(ctx->block+ctx->used,bytes,take);
        ctx->used+=take; bytes+=take; length-=take;
        if(ctx->used==64U) { transform(ctx); ctx->used=0; }
    }
}

void quota_sha256_final(quota_sha256_ctx_t *ctx, uint8_t out[32])
{
    uint32_t low=ctx->total_low<<3, high=(ctx->total_high<<3)|(ctx->total_low>>29);
    uint8_t i;
    ctx->block[ctx->used++]=0x80;
    if(ctx->used>56U) {
        memset(ctx->block+ctx->used,0,64U-ctx->used); transform(ctx); ctx->used=0;
    }
    memset(ctx->block+ctx->used,0,56U-ctx->used);
    for(i=0;i<4;i++) {
        ctx->block[56+i]=(uint8_t)(high>>(24U-i*8U));
        ctx->block[60+i]=(uint8_t)(low>>(24U-i*8U));
    }
    transform(ctx);
    for(i=0;i<32;i++) out[i]=(uint8_t)(ctx->state[i/4]>>(24U-(i&3U)*8U));
}

void quota_hmac_init(quota_auth_ctx_t *ctx, const void *key, uint32_t length)
{
    uint8_t i;
    memset(ctx->outer_pad,0,sizeof(ctx->outer_pad));
    if(length>64U) {
        quota_sha256_init(&ctx->inner); quota_sha256_update(&ctx->inner,key,length);
        quota_sha256_final(&ctx->inner,ctx->outer_pad);
    } else if(length) memcpy(ctx->outer_pad,key,length);
    for(i=0;i<64;i++) ctx->outer_pad[i]^=0x36;
    quota_sha256_init(&ctx->inner);
    quota_sha256_update(&ctx->inner,ctx->outer_pad,64);
    for(i=0;i<64;i++) ctx->outer_pad[i]^=(0x36^0x5c);
}

void quota_auth_update(quota_auth_ctx_t *ctx, const void *data, uint32_t length)
{ quota_sha256_update(&ctx->inner,data,length); }

void quota_hmac_final(quota_auth_ctx_t *ctx, uint8_t out[32])
{
    uint8_t digest[32];
    quota_sha256_final(&ctx->inner,digest);
    quota_sha256_init(&ctx->inner); quota_sha256_update(&ctx->inner,ctx->outer_pad,64);
    quota_sha256_update(&ctx->inner,digest,32); quota_sha256_final(&ctx->inner,out);
    memset(digest,0,sizeof(digest));
}

void quota_auth_init(quota_auth_ctx_t *ctx, uint8_t domain)
{
    uint8_t prefix[22]={'D','A','P','-','B','Q','T','3',1,0}, derived[32];
    uint32_t uid[3];
    uint8_t i,j;
    burn_quota_device_id(uid); prefix[9]=domain;
    for(i=0;i<3;i++) for(j=0;j<4;j++) prefix[10+i*4+j]=(uint8_t)(uid[i]>>(j*8U));
    quota_hmac_init(ctx,firmware_secret,sizeof(firmware_secret));
    quota_auth_update(ctx,prefix,sizeof(prefix)); quota_hmac_final(ctx,derived);
    quota_hmac_init(ctx,derived,sizeof(derived));
    memset(derived,0,sizeof(derived));
}

void quota_auth_final(quota_auth_ctx_t *ctx, uint8_t out[16])
{
    uint8_t digest[32];
    quota_hmac_final(ctx,digest); memcpy(out,digest,16); memset(digest,0,sizeof(digest));
}

void quota_auth_tag(uint8_t domain, const void *data, uint32_t length, uint8_t out[16])
{
    quota_auth_ctx_t ctx;
    quota_auth_init(&ctx,domain); quota_auth_update(&ctx,data,length); quota_auth_final(&ctx,out);
}

uint8_t quota_auth_equal(const void *left, const void *right, uint32_t length)
{
    const uint8_t *a=(const uint8_t *)left, *b=(const uint8_t *)right;
    uint32_t mismatch=0;
    while(length--) mismatch|=*a++ ^ *b++;
    return (uint8_t)(mismatch==0);
}
