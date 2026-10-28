/* 固件次数账本：内部日志保存已提交前缀的长度及CRC，损坏/丢失不能恢复无限额度。
 * Firmware quota checkpoints. The internal journal owns the committed file
 * prefix length/CRC; trailing uncommitted bytes never grant a quota reset.
 * Call only from the serialized main-task filesystem service (FatFs is not
 * reentrant). Per-burn writes remain in the internal Flash journal. */
#include "burn_quota.h"
#include "ff.h"
#include <string.h>

#define CATALOG_PATH "0:/system/burnquota.dat"
#define CATALOG_MAGIC 0x31435142UL /* BQC1 */
#define CATALOG_NAME_SIZE 32U
#define CATALOG_RECORD_SIZE 48U

typedef struct {
    uint32_t magic;
    uint8_t name[CATALOG_NAME_SIZE];
    uint32_t enabled;
    uint32_t remaining;
    uint32_t crc;
} catalog_record_t;

/* A FIL includes a sector buffer. Keep it off the small task stack; all other
 * catalog buffers are one record/name, independent of firmware/file count. */
static FIL catalog_file;

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
    return record->magic == CATALOG_MAGIC && record->enabled <= 1U &&
           record->remaining <= BURN_QUOTA_MAX &&
           (record->enabled || !record->remaining) &&
           normalize_name(record->name, normalized) &&
           !memcmp(record->name, normalized, CATALOG_NAME_SIZE) &&
           record->crc == (crc_step(0xFFFFFFFFUL, record, CATALOG_RECORD_SIZE - 4U) ^ 0xFFFFFFFFUL);
}

/* Scan every byte of the anchored prefix before returning a matching state.
 * A missing file, malformed record or lost prefix fails even for a name with
 * no record. Only the explicit empty anchor permits no backing file. */
static uint8_t scan(const burn_quota_catalog_t *catalog, const uint8_t *name,
                    uint8_t *enabled, uint32_t *remaining)
{
    catalog_record_t record;
    uint32_t offset, crc = 0xFFFFFFFFUL, found_remaining = 0;
    uint8_t found_enabled = 0, valid = 1;
    UINT received;
    if(!catalog || catalog->length % CATALOG_RECORD_SIZE) return 0;
    if(!catalog->length)
    {
        if(catalog->crc) return 0;
        *enabled = 0; *remaining = 0;
        return 1;
    }
    if(f_open(&catalog_file, CATALOG_PATH, FA_READ) != FR_OK) return 0;
    if(f_size(&catalog_file) < catalog->length) valid = 0;
    for(offset = 0; valid && offset < catalog->length; offset += CATALOG_RECORD_SIZE)
    {
        received = 0;
        if(f_read(&catalog_file, &record, sizeof(record), &received) != FR_OK ||
           received != sizeof(record) || !record_valid(&record))
        {
            valid = 0;
            break;
        }
        crc = crc_step(crc, &record, sizeof(record));
        if(!memcmp(record.name, name, CATALOG_NAME_SIZE))
        {
            found_enabled = (uint8_t)record.enabled;
            found_remaining = record.remaining;
        }
    }
    if(f_close(&catalog_file) != FR_OK) valid = 0;
    if(!valid || (crc ^ 0xFFFFFFFFUL) != catalog->crc) return 0;
    *enabled = found_enabled; *remaining = found_remaining;
    return 1;
}

/* 验证整个已提交账本后才返回指定固件余额；不存在该固件记录才视为不限。 */
uint8_t burn_quota_catalog_load(const burn_quota_catalog_t *catalog,
                               const uint8_t *name, uint8_t *enabled,
                               uint32_t *remaining)
{
    uint8_t normalized[CATALOG_NAME_SIZE];
    if(!enabled || !remaining || !normalize_name(name, normalized)) return 0;
    return scan(catalog, normalized, enabled, remaining);
}

/* 切换前追加当前余额，同步并回读验证后才发布新的长度/CRC；失败保持旧根。 */
uint8_t burn_quota_catalog_save(const burn_quota_catalog_t *catalog,
                               const uint8_t *name, uint8_t enabled,
                               uint32_t remaining, burn_quota_catalog_t *updated)
{
    catalog_record_t record;
    burn_quota_catalog_t next;
    uint8_t normalized[CATALOG_NAME_SIZE], old_enabled, verified_enabled, valid = 1;
    uint32_t old_remaining, verified_remaining;
    UINT written = 0;
    if(!updated || enabled > 1U || remaining > BURN_QUOTA_MAX ||
       (!enabled && remaining) || !normalize_name(name, normalized) ||
       !scan(catalog, normalized, &old_enabled, &old_remaining)) return 0;
    if(old_enabled == enabled && old_remaining == remaining)
    {
        *updated = *catalog;
        return 1;
    }
    if(catalog->length > 0xFFFFFFFFUL - CATALOG_RECORD_SIZE) return 0;
    record.magic = CATALOG_MAGIC;
    memcpy(record.name, normalized, sizeof(record.name));
    record.enabled = enabled;
    record.remaining = remaining;
    record.crc = crc_step(0xFFFFFFFFUL, &record, CATALOG_RECORD_SIZE - 4U) ^ 0xFFFFFFFFUL;
    next.length = catalog->length + sizeof(record);
    next.crc = crc_step(catalog->crc ^ 0xFFFFFFFFUL, &record, sizeof(record)) ^ 0xFFFFFFFFUL;
    /* Never truncate committed data: overwrite only an uncommitted tail, if
     * any. A failed append leaves the internal journal's old root unchanged. */
    if(f_open(&catalog_file, CATALOG_PATH, FA_READ | FA_WRITE | FA_OPEN_ALWAYS) != FR_OK) return 0;
    if(f_size(&catalog_file) < catalog->length ||
       f_lseek(&catalog_file, catalog->length) != FR_OK ||
       f_write(&catalog_file, &record, sizeof(record), &written) != FR_OK ||
       written != sizeof(record) || f_sync(&catalog_file) != FR_OK) valid = 0;
    if(f_close(&catalog_file) != FR_OK) valid = 0;
    if(!valid || !scan(&next, normalized, &verified_enabled, &verified_remaining) ||
       verified_enabled != enabled || verified_remaining != remaining) return 0;
    *updated = next;
    return 1;
}
