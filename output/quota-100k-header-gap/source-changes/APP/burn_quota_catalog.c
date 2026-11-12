/* 固件次数账本：内部日志认证已提交前缀，损坏/丢失不能恢复无限额度。
 * Firmware quota checkpoints. The internal journal owns the committed file
 * prefix length/HMAC; trailing uncommitted bytes never grant a quota reset.
 * Call only from the serialized main-task filesystem service (FatFs is not
 * reentrant). BQC1 record bytes remain compatible with the legacy catalog. */
#include "burn_quota.h"
#include "quota_auth.h"
#include "ff.h"
#include <string.h>

#define CATALOG_PATH "0:/system/burnquota.dat"
#define CATALOG_MAGIC 0x31435142UL /* BQC1 */
#define CATALOG_STATS_MAGIC 0x32435142UL /* BQC2, same 48-byte record size */
#define CATALOG_WIDE_MAGIC 0x33435142UL /* BQC3, full remaining/used */
#define CATALOG_NAME_SIZE 32U
#define CATALOG_RECORD_SIZE 48U
#define CATALOG_AUTH_DOMAIN 2U

typedef struct {
    uint32_t magic;
    uint8_t name[CATALOG_NAME_SIZE];
    uint32_t enabled;
    uint32_t remaining;
    uint32_t crc; /* BQC1/2 CRC; BQC3 full successful-used, protected by HMAC. */
} catalog_record_t;

/* FIL and HMAC contexts stay off the small task stack. The retained prefix
 * context lets save compute an appended root after verifying the old root.
 * These buffers require the same serialized service as the FatFs handle. */
static FIL catalog_file;
static quota_auth_ctx_t catalog_auth;
static quota_auth_ctx_t catalog_auth_verify;

static uint32_t crc_step(uint32_t crc, const void *buffer, uint32_t length)
{
    const uint8_t *bytes = (const uint8_t *)buffer;
    uint8_t bit;
    while(length--)
    {
        crc ^= *bytes++;
        for(bit = 0; bit < 8U; bit++)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
    }
    return crc;
}

/* Match FatFs ASCII case folding and reject aliases/path components. Names
 * must terminate within the same 32-byte field used by the application. */
static uint8_t normalize_name(const uint8_t *name, uint8_t *normalized)
{
    uint8_t i, c;
    if(!name || !normalized) return 0;
    memset(normalized, 0, CATALOG_NAME_SIZE);
    for(i = 0; i < CATALOG_NAME_SIZE; i++)
    {
        c = name[i];
        if(!c)
        {
            if(!i || normalized[i - 1U] == '.' || normalized[i - 1U] == ' ') return 0;
            return 1;
        }
        if(i == CATALOG_NAME_SIZE - 1U || c < 0x20U || c >= 0x7FU ||
           c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
           c == '"' || c == '<' || c == '>' || c == '|') return 0;
        normalized[i] = (c >= 'A' && c <= 'Z') ? (uint8_t)(c + ('a' - 'A')) : c;
    }
    return 0;
}

static uint8_t record_valid(const catalog_record_t *record)
{
    uint8_t normalized[CATALOG_NAME_SIZE];
    uint32_t state = record->enabled, remaining, total;
    if(record->magic == CATALOG_WIDE_MAGIC) {
        total = state >> 2;
        if(total > BURN_QUOTA_MAX || record->remaining > BURN_QUOTA_MAX ||
           (!(state & 1U) && record->remaining) ||
           (!(state & 2U) && (total || record->crc))) return 0;
    } else if(record->magic == CATALOG_STATS_MAGIC) {
        /* 1 enabled + 1 known + 10 remaining + 10 total; full 32-bit used
         * replaces the old remaining field. CRC and the HMAC root remain. */
        remaining = (state >> 2) & 1023U; total = (state >> 12) & 1023U;
        if(state >> 22 || remaining > BURN_QUOTA_LEGACY_MAX || total > BURN_QUOTA_LEGACY_MAX ||
           (!(state & 1U) && remaining) ||
           (!(state & 2U) && (total || record->remaining))) return 0;
    } else if(record->magic != CATALOG_MAGIC || state > 1U ||
              record->remaining > BURN_QUOTA_LEGACY_MAX || (!state && record->remaining)) return 0;
    return
           normalize_name(record->name, normalized) &&
           !memcmp(record->name, normalized, CATALOG_NAME_SIZE) &&
           /* BQC3 uses the old CRC word for used. scan authenticates every
            * byte through the committed root, including this word. */
           (record->magic == CATALOG_WIDE_MAGIC ||
            record->crc == (crc_step(0xFFFFFFFFUL, record, CATALOG_RECORD_SIZE - 4U) ^ 0xFFFFFFFFUL));
}

/* Read and validate the complete prefix, retaining its HMAC context. The CRC
 * output is only for one-time legacy migration; it is not authentication.
 * Unknown names have no quota until explicitly configured. */
static uint8_t read_prefix(uint32_t length, const uint8_t *name,
                           uint8_t *enabled, uint32_t *remaining,
                           burn_quota_stats_t *stats, uint32_t *legacy_crc)
{
    catalog_record_t record;
    uint32_t offset, crc = 0xFFFFFFFFUL, found_remaining = 0;
    uint8_t found_enabled = 1, valid = 1;
    burn_quota_stats_t found_stats;
    UINT received;
    if(length % CATALOG_RECORD_SIZE) return 0;
    memset(&found_stats, 0, sizeof(found_stats));
    quota_auth_init(&catalog_auth, CATALOG_AUTH_DOMAIN);
    if(length)
    {
        if(f_open(&catalog_file, CATALOG_PATH, FA_READ) != FR_OK) return 0;
        if(f_size(&catalog_file) < length) valid = 0;
        for(offset = 0; valid && offset < length; offset += CATALOG_RECORD_SIZE)
        {
            received = 0;
            if(f_read(&catalog_file, &record, sizeof(record), &received) != FR_OK ||
               received != sizeof(record) || !record_valid(&record) ||
               (legacy_crc && record.magic != CATALOG_MAGIC))
            {
                valid = 0;
                break;
            }
            quota_auth_update(&catalog_auth, &record, sizeof(record));
            if(legacy_crc) crc = crc_step(crc, &record, sizeof(record));
            if(name && !memcmp(record.name, name, CATALOG_NAME_SIZE))
            {
                memset(&found_stats, 0, sizeof(found_stats));
                found_enabled = (uint8_t)(record.enabled & 1U);
                found_remaining = record.remaining;
                if(record.magic == CATALOG_WIDE_MAGIC) {
                    found_stats.known = (uint8_t)((record.enabled >> 1) & 1U);
                    found_stats.total = record.enabled >> 2;
                    found_stats.used = record.crc;
                } else if(record.magic == CATALOG_STATS_MAGIC) {
                    found_remaining = (record.enabled >> 2) & 1023U;
                    found_stats.known = (uint8_t)((record.enabled >> 1) & 1U);
                    found_stats.total = (record.enabled >> 12) & 1023U;
                    found_stats.used = record.remaining;
                }
            }
        }
        if(f_close(&catalog_file) != FR_OK) valid = 0;
    }
    if(!valid) return 0;
    if(enabled) *enabled = found_enabled;
    if(remaining) *remaining = found_remaining;
    if(stats) *stats = found_stats;
    if(legacy_crc) *legacy_crc = crc ^ 0xFFFFFFFFUL;
    return 1;
}

/* A caller may use the zero root only when its internal journal record was
 * authenticated. Nonempty roots authenticate every committed byte before a
 * matching or absent name can be returned. Never trust a record's CRC alone. */
static uint8_t scan(const burn_quota_catalog_t *catalog, const uint8_t *name,
                    uint8_t *enabled, uint32_t *remaining, burn_quota_stats_t *stats)
{
    uint8_t tag[QUOTA_AUTH_TAG_SIZE], found_enabled;
    uint32_t found_remaining;
    burn_quota_stats_t found_stats;
    if(!catalog || !read_prefix(catalog->length, name, &found_enabled,
                                &found_remaining, &found_stats, 0)) return 0;
    if(catalog->length)
    {
        catalog_auth_verify = catalog_auth;
        quota_auth_final(&catalog_auth_verify, tag);
    }
    else memset(tag, 0, sizeof(tag));
    if(!quota_auth_equal(tag, catalog->tag, sizeof(tag))) return 0;
    *enabled = found_enabled; *remaining = found_remaining;
    if(stats) *stats = found_stats;
    return 1;
}

/* 验证整个已提交账本后才返回余额；未登记固件默认受限且剩余零次。 */
uint8_t burn_quota_catalog_load(const burn_quota_catalog_t *catalog,
                               const uint8_t *name, uint8_t *enabled,
                               uint32_t *remaining)
{ return burn_quota_catalog_load_stats(catalog, name, enabled, remaining, 0); }

uint8_t burn_quota_catalog_load_stats(const burn_quota_catalog_t *catalog,
                                     const uint8_t *name, uint8_t *enabled,
                                     uint32_t *remaining, burn_quota_stats_t *stats)
{
    uint8_t normalized[CATALOG_NAME_SIZE];
    if(!enabled || !remaining || !normalize_name(name, normalized)) return 0;
    return scan(catalog, normalized, enabled, remaining, stats);
}

/* 切换前追加当前余额，同步并回读验证后才发布新认证根；失败保持旧根。 */
uint8_t burn_quota_catalog_save(const burn_quota_catalog_t *catalog,
                               const uint8_t *name, uint8_t enabled,
                               uint32_t remaining, burn_quota_catalog_t *updated)
{
    burn_quota_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    return burn_quota_catalog_save_stats(catalog, name, enabled, remaining, &stats, updated);
}

uint8_t burn_quota_catalog_save_stats(const burn_quota_catalog_t *catalog,
                                     const uint8_t *name, uint8_t enabled,
                                     uint32_t remaining, const burn_quota_stats_t *stats,
                                     burn_quota_catalog_t *updated)
{
    catalog_record_t record;
    burn_quota_catalog_t next;
    uint8_t normalized[CATALOG_NAME_SIZE], old_enabled, verified_enabled, valid = 1;
    uint32_t old_remaining, verified_remaining;
    burn_quota_stats_t old_stats, verified_stats;
    UINT written = 0;
    if(!updated || !stats || stats->known > 1U || stats->total > BURN_QUOTA_MAX ||
       (!stats->known && (stats->total || stats->used)) ||
       enabled > 1U || remaining > BURN_QUOTA_MAX ||
       (!enabled && remaining) || !normalize_name(name, normalized) ||
       !scan(catalog, normalized, &old_enabled, &old_remaining, &old_stats)) return 0;
    if(old_enabled == enabled && old_remaining == remaining && old_stats.known == stats->known &&
       old_stats.total == stats->total && old_stats.used == stats->used)
    {
        *updated = *catalog;
        return 1;
    }
    if(catalog->length > 0xFFFFFFFFUL - CATALOG_RECORD_SIZE) return 0;
    record.magic = CATALOG_WIDE_MAGIC;
    memcpy(record.name, normalized, sizeof(record.name));
    record.enabled = enabled | ((uint32_t)stats->known << 1) | (stats->total << 2);
    record.remaining = remaining;
    record.crc = stats->used;
    next.length = catalog->length + sizeof(record);
    quota_auth_update(&catalog_auth, &record, sizeof(record));
    quota_auth_final(&catalog_auth, next.tag);
    /* Never truncate committed data: overwrite only an uncommitted tail, if
     * any. A failed append leaves the internal journal's old root unchanged. */
    if(f_open(&catalog_file, CATALOG_PATH, FA_READ | FA_WRITE | FA_OPEN_ALWAYS) != FR_OK) return 0;
    if(f_size(&catalog_file) < catalog->length ||
       f_lseek(&catalog_file, catalog->length) != FR_OK ||
       f_write(&catalog_file, &record, sizeof(record), &written) != FR_OK ||
       written != sizeof(record) || f_sync(&catalog_file) != FR_OK) valid = 0;
    if(f_close(&catalog_file) != FR_OK) valid = 0;
    if(!valid || !scan(&next, normalized, &verified_enabled, &verified_remaining, &verified_stats) ||
       verified_enabled != enabled || verified_remaining != remaining ||
       verified_stats.known != stats->known || verified_stats.total != stats->total ||
       verified_stats.used != stats->used) return 0;
    *updated = next;
    return 1;
}

/* Only the one-time internal journal upgrade may call this legacy entry point.
 * Verify the entire old CRC-anchored prefix before publishing its HMAC root;
 * retain every valid old record and leave uncommitted tail bytes untouched. */
uint8_t burn_quota_catalog_migrate(uint32_t length, uint32_t crc,
                                  burn_quota_catalog_t *updated)
{
    burn_quota_catalog_t next;
    uint32_t actual_crc;
    if(!updated || !read_prefix(length, 0, 0, 0, 0, &actual_crc) || actual_crc != crc) return 0;
    next.length = length;
    if(length) quota_auth_final(&catalog_auth, next.tag);
    else memset(next.tag, 0, sizeof(next.tag));
    *updated = next;
    return 1;
}
