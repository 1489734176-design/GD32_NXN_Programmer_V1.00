/* 中文说明：每个固件独立额度。账本保存非当前固件，内部日志保护当前余额和账本校验。 */
#include "burn_quota.h"
#include <string.h>

#define QUOTA_MAGIC 0x32545142UL /* BQT2 */
#define LEGACY_MAGIC 0x31545142UL
#define QUOTA_COMMIT 0x54494D43UL
#define QUOTA_WORDS 18U
#define QUOTA_RECORD_SIZE (QUOTA_WORDS * 4U)
#define QUOTA_SLOTS (BURN_QUOTA_PAGE_SIZE / QUOTA_RECORD_SIZE)

static uint32_t quota_sequence, quota_remaining;
static uint8_t quota_name[BURN_QUOTA_NAME_SIZE];
static burn_quota_catalog_t quota_catalog;
static uint8_t quota_enabled, quota_fault, quota_reserved, quota_valid;
static uint8_t quota_page, occupied[2], corrupt_pages, legacy_pending;
static uint8_t view_selected, view_fault;

static uint32_t address_of(uint8_t page, uint8_t slot, uint8_t legacy)
{
    return BURN_QUOTA_FLASH_BASE + page * BURN_QUOTA_PAGE_SIZE + slot * (legacy ? 32U : QUOTA_RECORD_SIZE);
}

/* FatFs文件名大小写不敏感，拒绝路径和会被FAT折叠的末尾点/空格。 */
static uint8_t normalize_name(uint8_t *out, const uint8_t *in)
{
    uint8_t i, c;
    if(!in) return 0;
    memset(out, 0, BURN_QUOTA_NAME_SIZE);
    for(i = 0; i < BURN_QUOTA_NAME_SIZE; i++)
    {
        c = in[i];
        if(!c) return !i || (out[i-1] != '.' && out[i-1] != ' ');
        if(c < 0x20 || c >= 0x7F || c == '/' || c == '\\' || c == ':' ||
           c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return 0;
        out[i] = c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
    }
    return 0;
}

static uint32_t checksum(const uint32_t *words, uint8_t count)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint8_t i, byte, bit;
    for(i = 0; i < count; i++)
        for(byte = 0; byte < 4; byte++)
        {
            crc ^= (words[i] >> (byte * 8)) & 0xFFU;
            for(bit = 0; bit < 8; bit++)
                crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
        }
    return crc ^ 0xFFFFFFFFUL;
}

static uint8_t record_valid(const uint32_t *r, uint8_t legacy)
{
    uint8_t name[BURN_QUOTA_NAME_SIZE];
    if(r[2] > 1 || r[3] > BURN_QUOTA_MAX || (!r[2] && r[3]) || r[4] != ~r[3]) return 0;
    if(legacy)
        return r[0] == LEGACY_MAGIC && r[5] == checksum(r, 5) && r[6] == ~r[1] && r[7] == QUOTA_COMMIT;
    if(r[0] != QUOTA_MAGIC || r[15] != checksum(r, 15) || r[16] != ~r[1] || r[17] != QUOTA_COMMIT ||
       (!r[13] && r[14]) || !normalize_name(name, (const uint8_t *)&r[5]) ||
       memcmp(name, &r[5], BURN_QUOTA_NAME_SIZE) || (!name[0] && r[2])) return 0;
    return 1;
}

/* 每页只放一种格式；BQT2迁移提交后才忽略旧BQT1页。 */
void burn_quota_init(void)
{
    uint32_t r[QUOTA_WORDS];
    uint8_t pass, page, slot, i, legacy, words, slots, empty, any_data = 0;
    quota_sequence = quota_remaining = 0;
    memset(quota_name, 0, sizeof(quota_name));
    quota_catalog.length = quota_catalog.crc = 0;
    quota_enabled = quota_fault = quota_reserved = quota_valid = quota_page = legacy_pending = 0;
    occupied[0] = occupied[1] = corrupt_pages = view_selected = view_fault = 0;
    /* 先找BQT2，再仅在没有新版完整记录时读取旧日志。 */
    for(pass = 0; pass < 2; pass++)
    {
        if(pass && quota_valid) break;
        for(page = 0; page < 2; page++)
        {
            legacy = burn_quota_read_word(address_of(page, 0, 0)) == LEGACY_MAGIC;
            if(legacy != pass) continue;
            words = legacy ? 8 : QUOTA_WORDS;
            slots = legacy ? 32 : QUOTA_SLOTS;
            for(slot = 0; slot < slots; slot++)
            {
                empty = 1;
                for(i = 0; i < words; i++)
                {
                    r[i] = burn_quota_read_word(address_of(page, slot, legacy) + i * 4U);
                    if(r[i] != 0xFFFFFFFFUL) empty = 0;
                }
                if(empty) continue;
                any_data = 1;
                occupied[page] = slot + 1;
                if(!record_valid(r, legacy))
                {
                    if(r[words-1] == QUOTA_COMMIT)
                    { quota_fault = 1; corrupt_pages |= (uint8_t)(1U << page); }
                    continue;
                }
                if(!quota_valid || (int32_t)(r[1] - quota_sequence) > 0)
                {
                    quota_valid = 1; quota_sequence = r[1]; quota_enabled = (uint8_t)r[2];
                    quota_remaining = r[3]; quota_page = page; legacy_pending = legacy;
                    if(!legacy)
                    {
                        memcpy(quota_name, &r[5], sizeof(quota_name));
                        quota_catalog.length = r[13]; quota_catalog.crc = r[14];
                    }
                }
            }
        }
    }
    if(any_data && !quota_valid) quota_fault = 1;
    view_selected = quota_name[0] != 0;
}

static uint8_t erase_page(uint8_t page)
{
    uint32_t a = address_of(page, 0, 0), offset;
    if(!burn_quota_erase_page(a)) return 0;
    for(offset = 0; offset < BURN_QUOTA_PAGE_SIZE; offset += 4)
        if(burn_quota_read_word(a + offset) != 0xFFFFFFFFUL) return 0;
    return 1;
}

/* 最后一个字提交；换页前保留旧页，错误页只能在新记录提交后清除。 */
static uint8_t append(const uint8_t *name, uint8_t enabled, uint32_t remaining,
                      const burn_quota_catalog_t *catalog, uint8_t repair)
{
    uint32_t r[QUOTA_WORDS], address;
    uint8_t i, page = quota_page, slot = occupied[quota_page], preserved = quota_page;
    if(repair || legacy_pending || slot >= QUOTA_SLOTS)
    {
        if(repair && corrupt_pages) preserved = (corrupt_pages & 1) ? 0 : 1;
        page = preserved ^ 1U; slot = 0;
        if(!erase_page(page)) goto storage_error;
    }
    r[0] = QUOTA_MAGIC; r[1] = quota_sequence + 1U; r[2] = enabled; r[3] = remaining; r[4] = ~remaining;
    memcpy(&r[5], name, BURN_QUOTA_NAME_SIZE);
    r[13] = catalog->length; r[14] = catalog->crc; r[15] = checksum(r, 15);
    r[16] = ~r[1]; r[17] = QUOTA_COMMIT;
    address = address_of(page, slot, 0);
    /* 即使写入失败也不再使用这个槽，避免重复在未擦除的字上编程。 */
    occupied[page] = slot + 1;
    for(i = 0; i < QUOTA_WORDS; i++)
        if(!burn_quota_program_word(address + i * 4U, r[i]) ||
           burn_quota_read_word(address + i * 4U) != r[i]) goto storage_error;
    if(repair && !erase_page(preserved)) goto storage_error;
    quota_sequence = r[1]; quota_enabled = enabled; quota_remaining = remaining;
    memcpy(quota_name, name, BURN_QUOTA_NAME_SIZE); quota_catalog = *catalog;
    quota_page = page; quota_valid = 1; quota_fault = corrupt_pages = legacy_pending = view_fault = 0;
    view_selected = name[0] != 0;
    return BURN_QUOTA_OK;
storage_error:
    quota_fault = 1;
    return BURN_QUOTA_STORAGE;
}

uint8_t burn_quota_migrate_legacy(const uint8_t *name)
{
    uint8_t normalized[BURN_QUOTA_NAME_SIZE];
    if(!legacy_pending) return quota_fault ? BURN_QUOTA_STORAGE : BURN_QUOTA_OK;
    if(quota_fault || !normalize_name(normalized, name)) return BURN_QUOTA_STORAGE;
    /* 没有已保存的固件名时无法推测归属；旧额度不套用到后来选择的任意文件。 */
    return append(normalized, normalized[0] ? quota_enabled : 0,
                  normalized[0] ? quota_remaining : 0, &quota_catalog, 0);
}

uint8_t burn_quota_select(const uint8_t *name)
{
    uint8_t normalized[BURN_QUOTA_NAME_SIZE], enabled;
    uint32_t remaining;
    burn_quota_catalog_t updated;
    if(quota_reserved) return BURN_QUOTA_BUSY;
    if(!normalize_name(normalized, name)) { view_fault = 1; return BURN_QUOTA_STORAGE; }
    if(!normalized[0]) { view_selected = view_fault = 0; return BURN_QUOTA_OK; }
    /* 首条记录未提交就掉电时，允许明确指定要重新导入的固件，但保留异常。
     * 只有随后显式set成功才能解除禁烧，普通选择/读取不会把它变成不限。 */
    if(!quota_valid && quota_fault && !corrupt_pages)
    {
        memcpy(quota_name, normalized, sizeof(quota_name));
        view_selected = 1; view_fault = 0;
        return BURN_QUOTA_OK;
    }
    if(!memcmp(normalized, quota_name, sizeof(quota_name)) && !legacy_pending)
    { view_selected = 1; view_fault = 0; return BURN_QUOTA_OK; }
    view_fault = 1;
    if(quota_fault || legacy_pending) return BURN_QUOTA_STORAGE;
    updated = quota_catalog;
    /* 写入账本后仍由旧内部记录保护旧前缀，只有新内部记录提交才采用新根。 */
    if(quota_name[0] && !burn_quota_catalog_save(&quota_catalog, quota_name,
                                              quota_enabled, quota_remaining, &updated)) return BURN_QUOTA_STORAGE;
    if(!burn_quota_catalog_load(&updated, normalized, &enabled, &remaining)) return BURN_QUOTA_STORAGE;
    return append(normalized, enabled, remaining, &updated, 0);
}

uint8_t burn_quota_enabled(void) { return view_selected ? quota_enabled : 0; }
uint32_t burn_quota_remaining(void) { return view_selected ? quota_remaining : 0; }
uint8_t burn_quota_fault(void) { return quota_fault || view_fault || legacy_pending; }

uint8_t burn_quota_set(uint8_t enabled, uint32_t remaining)
{
    if(quota_reserved) return BURN_QUOTA_BUSY;
    if(!view_selected || view_fault || legacy_pending || enabled > 1 || remaining > BURN_QUOTA_MAX || (!enabled && remaining))
        return BURN_QUOTA_STORAGE;
    if(!quota_fault && enabled == quota_enabled && remaining == quota_remaining) return BURN_QUOTA_OK;
    return append(quota_name, enabled, remaining, &quota_catalog, quota_fault);
}

uint8_t burn_quota_begin(void)
{
    uint8_t result;
    if(quota_reserved) return BURN_QUOTA_BUSY;
    if(!view_selected || burn_quota_fault()) return BURN_QUOTA_STORAGE;
    if(quota_enabled && !quota_remaining) return BURN_QUOTA_EMPTY;
    result = quota_enabled ? append(quota_name, 1, quota_remaining - 1U, &quota_catalog, 0) : BURN_QUOTA_OK;
    /* 不限次数同样锁住归属，防止烧录中途切换固件。 */
    if(result == BURN_QUOTA_OK) quota_reserved = 1;
    return result;
}

uint8_t burn_quota_finish(uint8_t success)
{
    if(!quota_reserved) return BURN_QUOTA_OK;
    quota_reserved = 0;
    if(success || !quota_enabled) return BURN_QUOTA_OK;
    return append(quota_name, 1, quota_remaining + 1U, &quota_catalog, 0);
}
