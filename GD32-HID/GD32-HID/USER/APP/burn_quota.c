/* 内外Flash保存相同BQT5记录；烧录前回读校验，确认成功后扣次；双写失败禁烧。 */
#include "burn_quota.h"
#include "burn_quota_mirror.h"
#include "quota_auth.h"
#include <string.h>

#define QUOTA_MAGIC 0x35545142UL
#define V4_MAGIC 0x34545142UL
#define V3_MAGIC 0x33545142UL
#define V2_MAGIC 0x32545142UL
#define V1_MAGIC 0x31545142UL
#define QUOTA_COMMIT 0x54494D43UL
#define QUOTA_WORDS 24U
#define QUOTA_SLOTS (BURN_QUOTA_PAGE_SIZE / BURN_QUOTA_RECORD_SIZE)

static uint32_t quota_record[QUOTA_WORDS], quota_address, quota_sequence;
static uint32_t quota_remaining, legacy_length, legacy_crc;
static uint8_t quota_name[BURN_QUOTA_NAME_SIZE];
static burn_quota_catalog_t quota_catalog;
static burn_quota_stats_t quota_stats;
static uint8_t quota_enabled, quota_fault, quota_reserved, quota_valid;
static uint8_t quota_page, occupied[2], legacy_pending, view_selected, view_fault, quota_checked;

static uint32_t page_address(uint8_t page)
{ return BURN_QUOTA_FLASH_BASE + page * BURN_QUOTA_PAGE_SIZE; }

/* 与FatFs的大小写规则一致，不接受路径和末尾点/空格别名。 */
static uint8_t normalize_name(uint8_t *out, const uint8_t *in)
{
    uint8_t i, c;
    if(!in) return 0;
    memset(out, 0, BURN_QUOTA_NAME_SIZE);
    for(i = 0; i < BURN_QUOTA_NAME_SIZE; i++) {
        c = in[i];
        if(!c) return !i || (out[i-1] != '.' && out[i-1] != ' ');
        if(c < 0x20 || c >= 0x7F || c == '/' || c == '\\' || c == ':' ||
           c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return 0;
        out[i] = c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
    }
    return 0;
}

/* CRC只用于识别旧版，BQT3及新版的授权依据始终是HMAC。 */
static uint32_t checksum(const uint32_t *words, uint8_t count)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint8_t i, byte, bit;
    for(i = 0; i < count; i++) for(byte = 0; byte < 4; byte++) {
        crc ^= (words[i] >> (byte * 8)) & 0xFFU;
        for(bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
    }
    return crc ^ 0xFFFFFFFFUL;
}

static uint8_t record_valid(const uint32_t *r, uint8_t version)
{
    uint8_t name[BURN_QUOTA_NAME_SIZE], tag[16];
    uint32_t remaining = r[3] & 0xFFFFU, total = r[3] >> 16;
    if(version == 3 && r[0] == QUOTA_MAGIC) {
        /* BQT5 retains the authenticated 96-byte slot. Total shares only
         * the two flag bits; remaining and used each keep all 32 bits. */
        total = r[2] >> 2;
        if(total > BURN_QUOTA_MAX || r[3] > BURN_QUOTA_MAX ||
           (!(r[2] & 1U) && r[3]) ||
           (!(r[2] & 2U) && (total || r[4]))) return 0;
    } else if(version == 3 && r[0] == V4_MAGIC) {
        /* Decode BQT4 exactly as written by the 999-limit firmware. */
        if(r[2] > 3U || remaining > BURN_QUOTA_LEGACY_MAX || total > BURN_QUOTA_LEGACY_MAX ||
           (!(r[2] & 1U) && remaining) ||
           (!(r[2] & 2U) && (total || r[4]))) return 0;
    } else if(r[2] > 1 || r[3] > BURN_QUOTA_LEGACY_MAX || (!r[2] && r[3]) || r[4] != ~r[3]) return 0;
    if(version == 1)
        return r[0] == V1_MAGIC && r[5] == checksum(r, 5) && r[6] == ~r[1] && r[7] == QUOTA_COMMIT;
    if(!normalize_name(name, (const uint8_t *)&r[5]) ||
       memcmp(name, &r[5], sizeof(name)) || (!name[0] && r[2])) return 0;
    if(version == 2)
        return r[0] == V2_MAGIC && r[15] == checksum(r, 15) && r[16] == ~r[1] &&
               r[17] == QUOTA_COMMIT && (r[13] || !r[14]);
    if((r[0] != QUOTA_MAGIC && r[0] != V4_MAGIC && r[0] != V3_MAGIC) || r[22] != ~r[1] || r[23] != QUOTA_COMMIT ||
       (!r[13] && (r[14] || r[15] || r[16] || r[17]))) return 0;
    quota_auth_tag(1, r, 18U * 4U, tag);
    return quota_auth_equal(tag, &r[18], sizeof(tag));
}

/* 挂载文件系统前只读内部Flash。存在新版数据时绝不降级信任旧格式。 */
void burn_quota_init(void)
{
    uint32_t r[QUOTA_WORDS], first, address;
    uint8_t version, page, slot, i, words, slots, empty, seen_new = 0;
    quota_sequence = quota_remaining = quota_address = legacy_length = legacy_crc = 0;
    memset(quota_record, 0, sizeof(quota_record));
    memset(quota_name, 0, sizeof(quota_name));
    memset(&quota_catalog, 0, sizeof(quota_catalog));
    memset(&quota_stats, 0, sizeof(quota_stats));
    quota_enabled = quota_reserved = quota_valid = quota_page = legacy_pending = 0;
    occupied[0] = occupied[1] = view_selected = view_fault = quota_fault = quota_checked = 0;
    for(version = 3; version; version--) {
        if(version < 3 && (quota_valid || seen_new)) break;
        for(page = 0; page < 2; page++) {
            first = burn_quota_read_word(page_address(page));
            if(version == 3 && (first == V1_MAGIC || first == V2_MAGIC)) continue;
            if(version == 2 && first != V2_MAGIC) continue;
            if(version == 1 && first != V1_MAGIC) continue;
            words = version == 3 ? QUOTA_WORDS : (version == 2 ? 18 : 8);
            slots = (uint8_t)(BURN_QUOTA_PAGE_SIZE / (words * 4U));
            for(slot = 0; slot < slots; slot++) {
                empty = 1; address = page_address(page) + slot * words * 4U;
                for(i = 0; i < words; i++) {
                    r[i] = burn_quota_read_word(address + i * 4U);
                    if(r[i] != 0xFFFFFFFFUL) empty = 0;
                }
                if(empty) continue;
                if(version == 3) seen_new = 1;
                occupied[page] = slot + 1;
                if(!record_valid(r, version)) {
                    if(r[words-1] == QUOTA_COMMIT) quota_fault = 1;
                    continue;
                }
                if(!quota_valid || (int32_t)(r[1] - quota_sequence) > 0) {
                    quota_valid = 1; quota_sequence = r[1]; quota_enabled = (uint8_t)(r[2] & 1U);
                    quota_remaining = r[0] == V4_MAGIC ? r[3] & 0xFFFFU : r[3];
                    memset(&quota_stats, 0, sizeof(quota_stats));
                    if(r[0] == QUOTA_MAGIC || r[0] == V4_MAGIC) {
                        quota_stats.known = (uint8_t)((r[2] >> 1) & 1U);
                        quota_stats.total = r[0] == QUOTA_MAGIC ? r[2] >> 2 : r[3] >> 16;
                        quota_stats.used = r[4];
                    }
                    quota_page = page; quota_address = address;
                    legacy_pending = version == 3 ? 0 : version;
                    if(version != 1) memcpy(quota_name, &r[5], sizeof(quota_name));
                    if(version == 3) {
                        memcpy(quota_record, r, sizeof(quota_record));
                        quota_catalog.length = r[13]; memcpy(quota_catalog.tag, &r[14], 16);
                    } else if(version == 2) { legacy_length = r[13]; legacy_crc = r[14]; }
                }
            }
        }
    }
    if(!quota_valid) quota_fault = 1;
    view_selected = quota_name[0] != 0;
}

static uint8_t erase_page(uint8_t page)
{
    uint32_t address = page_address(page), offset;
    if(!burn_quota_erase_page(address)) return 0;
    for(offset = 0; offset < BURN_QUOTA_PAGE_SIZE; offset += 4)
        if(burn_quota_read_word(address + offset) != 0xFFFFFFFFUL) return 0;
    occupied[page] = 0;
    return 1;
}

/* 回读物理存储，不能只用RAM旧结果放行。账本也校验整个已提交前缀。 */
static uint8_t verify_pair(void)
{
    uint32_t r[QUOTA_WORDS], remaining;
    uint8_t i, enabled;
    if(!quota_valid || legacy_pending || quota_fault) return 0;
    for(i = 0; i < QUOTA_WORDS; i++) r[i] = burn_quota_read_word(quota_address + 4U * i);
    if(!record_valid(r, 3) || !quota_auth_equal(r, quota_record, sizeof(r))) goto invalid;
    if(!burn_quota_mirror_read(r) || !record_valid(r, 3) ||
       !quota_auth_equal(r, quota_record, sizeof(r))) goto invalid;
    if(!burn_quota_catalog_load(&quota_catalog, quota_name[0] ? quota_name : (const uint8_t *)"_",
                                 &enabled, &remaining)) goto invalid;
    quota_checked = 1;
    return 1;
invalid:
    quota_checked = 0;
    quota_fault = 1;
    return 0;
}

/* 外部先写、内部最后提交。仅显式SET允许修复，正常烧录不得修复不一致。 */
static uint8_t append(const uint8_t *name, uint8_t enabled, uint32_t remaining,
                      const burn_quota_catalog_t *catalog, const burn_quota_stats_t *stats, uint8_t repair)
{
    uint32_t r[QUOTA_WORDS], address;
    uint8_t i, page = quota_page, slot = occupied[quota_page], preserved = quota_page;
    memset(r, 0, sizeof(r));
    r[0] = QUOTA_MAGIC; r[1] = quota_sequence + 1U;
    r[2] = enabled | ((uint32_t)stats->known << 1) | (stats->total << 2);
    r[3] = remaining; r[4] = stats->used;
    memcpy(&r[5], name, BURN_QUOTA_NAME_SIZE);
    r[13] = catalog->length; memcpy(&r[14], catalog->tag, 16);
    quota_auth_tag(1, r, 18U * 4U, (uint8_t *)&r[18]);
    r[22] = ~r[1]; r[23] = QUOTA_COMMIT;
    if(!burn_quota_mirror_write(r, repair)) goto storage_error;
    if(repair || legacy_pending || slot >= QUOTA_SLOTS) {
        page = preserved ^ 1U; slot = 0;
        if(!erase_page(page)) goto storage_error;
    }
    address = page_address(page) + slot * BURN_QUOTA_RECORD_SIZE;
    occupied[page] = slot + 1;
    for(i = 0; i < QUOTA_WORDS; i++)
        if(!burn_quota_program_word(address + i * 4U, r[i]) ||
           burn_quota_read_word(address + i * 4U) != r[i]) goto storage_error;
    /* 修复后清除旧错误页，避免重启时继续报告已处理的损坏。 */
    if(repair && !erase_page(preserved)) goto storage_error;
    quota_sequence = r[1]; quota_enabled = enabled; quota_remaining = remaining;
    quota_stats = *stats;
    memcpy(quota_name, name, sizeof(quota_name)); quota_catalog = *catalog;
    memcpy(quota_record, r, sizeof(r)); quota_address = address;
    quota_page = page; quota_valid = 1; quota_fault = legacy_pending = view_fault = 0;
    view_selected = name[0] != 0;
    return verify_pair() ? BURN_QUOTA_OK : BURN_QUOTA_STORAGE;
storage_error:
    quota_fault = 1;
    return BURN_QUOTA_STORAGE;
}

uint8_t burn_quota_migrate_legacy(const uint8_t *name)
{
    uint8_t normalized[BURN_QUOTA_NAME_SIZE];
    burn_quota_catalog_t upgraded;
    if(!legacy_pending) return quota_fault ? BURN_QUOTA_STORAGE : BURN_QUOTA_OK;
    if(quota_fault || burn_quota_mirror_probe() != 0) goto invalid;
    if(!normalize_name(normalized, legacy_pending == 2 ? quota_name : name)) goto invalid;
    if(!burn_quota_catalog_migrate(legacy_length, legacy_crc, &upgraded)) goto invalid;
    return append(normalized, normalized[0] ? quota_enabled : 0,
                  normalized[0] ? quota_remaining : 0, &upgraded, &quota_stats, 0);
invalid:
    quota_fault = 1;
    return BURN_QUOTA_STORAGE;
}

uint8_t burn_quota_select(const uint8_t *name)
{
    uint8_t normalized[BURN_QUOTA_NAME_SIZE], enabled;
    uint32_t remaining;
    burn_quota_catalog_t updated;
    burn_quota_stats_t stats;
    if(quota_reserved) return BURN_QUOTA_BUSY;
    if(!normalize_name(normalized, name)) { view_fault = 1; return BURN_QUOTA_STORAGE; }
    if(!normalized[0]) { view_selected = view_fault = 0; return BURN_QUOTA_OK; }
    /* 空白/损坏设备只绑定待设置名称；必须随后SET，绝不自动变成不限。 */
    if(!quota_valid) {
        memcpy(quota_name, normalized, sizeof(quota_name));
        quota_enabled = 1; quota_remaining = 0; view_selected = 1; view_fault = 0;
        return BURN_QUOTA_OK;
    }
    /* BQT1没有文件名；中断升级后允许管理员明确选择要重新设置的文件。
     * 保留legacy_pending，普通选择和烧录仍然不能放行。 */
    if(legacy_pending == 1 && !quota_name[0]) {
        memcpy(quota_name, normalized, sizeof(quota_name));
        view_selected = 1; view_fault = 0;
        return BURN_QUOTA_OK;
    }
    if(!memcmp(normalized, quota_name, sizeof(quota_name))) {
        view_selected = 1; view_fault = 0;
        /* 主循环反复绑定同名时不重复遍历账本。启动首次检查；真正烧录、
         * 设置、切换和成功扣次仍始终调用verify_pair回读物理存储。 */
        if(!quota_checked) verify_pair();
        return BURN_QUOTA_OK;
    }
    view_fault = 1;
    if(!verify_pair()) return BURN_QUOTA_STORAGE;
    updated = quota_catalog;
    if(quota_name[0] && !burn_quota_catalog_save_stats(&quota_catalog, quota_name,
                                                     quota_enabled, quota_remaining, &quota_stats, &updated)) return BURN_QUOTA_STORAGE;
    if(!burn_quota_catalog_load_stats(&updated, normalized, &enabled, &remaining, &stats)) return BURN_QUOTA_STORAGE;
    return append(normalized, enabled, remaining, &updated, &stats, 0);
}

uint8_t burn_quota_enabled(void) { return view_selected ? quota_enabled : 0; }
uint32_t burn_quota_remaining(void) { return view_selected ? quota_remaining : 0; }
uint8_t burn_quota_fault(void) { return quota_fault || view_fault || legacy_pending; }
uint8_t burn_quota_stats_known(void) { return view_selected && quota_stats.known; }
uint32_t burn_quota_total(void) { return view_selected ? quota_stats.total : 0; }
uint32_t burn_quota_used(void) { return view_selected ? quota_stats.used : 0; }

static uint8_t set_quota(uint8_t enabled, uint32_t remaining, uint8_t reimport)
{
    uint8_t ignored_enabled;
    uint32_t ignored_remaining;
    burn_quota_catalog_t upgraded;
    burn_quota_stats_t stats;
    if(quota_reserved) return BURN_QUOTA_BUSY;
    if(!view_selected || view_fault || enabled > 1 || remaining > BURN_QUOTA_MAX || (!enabled && remaining))
        return BURN_QUOTA_STORAGE;
    /* Ordinary administrator SET adjusts the available balance, preserving
     * the imported batch history. Replayed SET after a burn must not erase
     * used. Only reimport (or first setup on a blank device) starts history. */
    stats = quota_stats;
    if(reimport || !quota_valid) {
        memset(&stats, 0, sizeof(stats));
        stats.known = 1; stats.total = remaining;
    }
    /* 升级中途断电可由显式SET恢复；仍先验证旧账本，保留其他固件额度。 */
    if(legacy_pending) {
        if(!burn_quota_catalog_migrate(legacy_length, legacy_crc, &upgraded)) return BURN_QUOTA_STORAGE;
        return append(quota_name, enabled, remaining, &upgraded, &stats, 1);
    }
    /* 修复当前镜像也不能擅自丢弃其他固件的已提交账本。 */
    if(!burn_quota_catalog_load(&quota_catalog, quota_name, &ignored_enabled, &ignored_remaining)) return BURN_QUOTA_STORAGE;
    if(verify_pair() && !reimport && enabled == quota_enabled && remaining == quota_remaining) return BURN_QUOTA_OK;
    return append(quota_name, enabled, remaining, &quota_catalog, &stats, quota_fault || !quota_valid);
}

uint8_t burn_quota_set(uint8_t enabled, uint32_t remaining)
{ return set_quota(enabled, remaining, 0); }
uint8_t burn_quota_reimport(uint8_t enabled, uint32_t total)
{ return set_quota(enabled, total, 1); }

/* 只读查询：直接从已提交账本取指定名字的额度，不调用 select，
 * 不写 .chk/.dat、不改当前绑定。用于上位机覆盖确认/镜像面板。 */
uint8_t burn_quota_peek(const uint8_t *name, uint8_t *enabled, uint32_t *remaining,
                        burn_quota_stats_t *stats)
{
    uint8_t normalized[BURN_QUOTA_NAME_SIZE], e;
    uint32_t r;
    burn_quota_stats_t s;
    if(!name || !normalize_name(normalized, name) || !normalized[0]) return 0;
    if(!burn_quota_catalog_load_stats(&quota_catalog, normalized, &e, &r, &s)) return 0;
    if(enabled) *enabled = e;
    if(remaining) *remaining = r;
    if(stats) *stats = s;
    return 1;
}

uint8_t burn_quota_begin(void)
{
    if(quota_reserved) return BURN_QUOTA_BUSY;
    if(!view_selected || burn_quota_fault() || !verify_pair()) return BURN_QUOTA_STORAGE;
    if(quota_enabled && !quota_remaining) return BURN_QUOTA_EMPTY;
    /* RAM-only session, including unlimited: failed/interrupted burns must
     * not change either medium, remaining balance or successful-used. */
    quota_reserved = 1;
    return BURN_QUOTA_OK;
}

uint8_t burn_quota_finish(uint8_t success)
{
    burn_quota_stats_t stats;
    if(!quota_reserved) return BURN_QUOTA_OK;
    quota_reserved = 0;
    if(!success) return BURN_QUOTA_OK;
    if(!verify_pair()) return BURN_QUOTA_STORAGE;
    /* One authenticated transaction commits both values after confirmed
     * success. Unknown legacy history stays unknown; used never wraps. */
    stats = quota_stats;
    if(stats.known && stats.used != 0xFFFFFFFFUL) stats.used++;
    if(!quota_enabled && stats.used == quota_stats.used) return BURN_QUOTA_OK;
    return append(quota_name, quota_enabled,
                  quota_enabled ? quota_remaining - 1U : 0,
                  &quota_catalog, &stats, 0);
}
