/* 镜像任务索引存储：SPI Flash 顶部 64KB 裸扇区，不经过 FAT。
 * bank A 0x00FF0000、bank B 0x00FF4000；每份 11,220B，预留 16KB。
 * 写入方式：先把 4KB 扇区在 DATA_BUFFER 中组好，擦除后用
 * GD25QXX_Write_NoCheck 整扇区写入；写入后回读 CRC 校验。
 * 所有函数只在主任务、且没有 FatFs/烧录并发时调用。 */
#include "image_task.h"
#include "bsp_gd25qxx.h"
#include <string.h>

#define IMAGE_BANK_SECTORS      3U
#define IMAGE_SECTOR_SIZE       4096U
#define IMAGE_SECTOR2_DATA_LEN  (IMAGE_BANK_DATA_SIZE - 2U * IMAGE_SECTOR_SIZE) /* 3024 */
#define IMAGE_CRC_IN_SECTOR2    IMAGE_SECTOR2_DATA_LEN

static image_bank_header_t g_hdr;
static image_record_t      g_current_rec;
static uint8_t             g_valid;
static uint8_t             g_fault;
static uint8_t             g_current = IMAGE_ID_NONE;
static uint32_t            g_active_addr;
static uint32_t            g_active_crc;
static uint8_t             g_io[128];

/* 标准反射 CRC32：init=0xFFFFFFFF，poly=0xEDB88320，xorout=0xFFFFFFFF。
 * 与 selection_store/burn_quota 的算法一致；测试向量 "123456789"=0xCBF43926。 */
static uint32_t image_crc32_update(uint32_t crc, const uint8_t *p, uint16_t len)
{
    uint16_t i;
    while(len--)
    {
        crc ^= (uint32_t)(*p++);
        for(i = 0; i < 8U; i++)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
    }
    return crc;
}

static uint8_t chip_supports_raw(void)
{
    uint16_t id = GD25QXX_ReadID();
    /* 顶部 64KB 裸区要求芯片至少 16MB；Q128/Q256 均满足。 */
    return (uint8_t)((id == GD25Q128) || (id == GD25Q256));
}

static uint8_t seq_newer(uint32_t a, uint32_t b)
{
    return (uint8_t)((int32_t)(a - b) > 0);
}

static void read_header(uint32_t addr, image_bank_header_t *h)
{
    uint8_t b[IMAGE_BANK_HEADER_SIZE];
    GD25QXX_Read(b, addr, IMAGE_BANK_HEADER_SIZE);
    memcpy(h, b, IMAGE_BANK_HEADER_SIZE);
}

static uint8_t bank_crc_ok(uint32_t addr, uint32_t *crc_out)
{
    uint32_t crc = 0xFFFFFFFFUL, stored;
    uint16_t off = 0, n;
    uint8_t tail[4];
    while(off < IMAGE_BANK_DATA_SIZE)
    {
        n = (uint16_t)(IMAGE_BANK_DATA_SIZE - off);
        if(n > sizeof(g_io)) n = sizeof(g_io);
        GD25QXX_Read(g_io, addr + off, n);
        crc = image_crc32_update(crc, g_io, n);
        off = (uint16_t)(off + n);
    }
    crc ^= 0xFFFFFFFFUL;
    GD25QXX_Read(tail, addr + IMAGE_BANK_DATA_SIZE, 4);
    stored = (uint32_t)tail[0] | ((uint32_t)tail[1] << 8) |
             ((uint32_t)tail[2] << 16) | ((uint32_t)tail[3] << 24);
    if(crc != stored) return 0;
    if(crc_out) *crc_out = crc;
    return 1;
}

static uint8_t bank_valid(uint32_t addr, image_bank_header_t *h, uint32_t *crc_out)
{
    read_header(addr, h);
    if(h->magic != IMAGE_BANK_MAGIC || h->version != IMAGE_BANK_VERSION ||
       h->slots != IMAGE_SLOTS || !(h->flags & IMAGE_BANK_FLAG_VALID))
        return 0;
    if(h->current != IMAGE_ID_NONE && h->current >= IMAGE_SLOTS)
        return 0;
    return bank_crc_ok(addr, crc_out);
}

static uint8_t record_usable(const image_record_t *r)
{
    if(!(r->flags & IMAGE_REC_FLAG_VALID)) return 0;
    if(!r->algo[0] || !r->bin[0]) return 0;
    if(!memchr(r->algo, 0, sizeof(r->algo))) return 0;
    if(!memchr(r->bin, 0, sizeof(r->bin))) return 0;
    return 1;
}

static void load_current_record(void)
{
    g_current = IMAGE_ID_NONE;
    memset(&g_current_rec, 0, sizeof(g_current_rec));
    if(!g_valid || g_hdr.current >= IMAGE_SLOTS) return;
    if(!image_store_read_record(g_hdr.current, &g_current_rec)) return;
    if(!record_usable(&g_current_rec))
    {
        memset(&g_current_rec, 0, sizeof(g_current_rec));
        return;
    }
    g_current = g_hdr.current;
}

void image_store_init(void)
{
    image_bank_header_t ha, hb;
    uint8_t va, vb;
    uint32_t crc_a = 0, crc_b = 0;

    g_valid = 0;
    g_fault = 0;
    g_current = IMAGE_ID_NONE;
    g_active_addr = 0;
    g_active_crc = 0;
    memset(&g_hdr, 0, sizeof(g_hdr));
    memset(&g_current_rec, 0, sizeof(g_current_rec));

    if(!chip_supports_raw())
    {
        g_fault = 1;
        return;
    }

    va = bank_valid(IMAGE_BANK_A_ADDR, &ha, &crc_a);
    vb = bank_valid(IMAGE_BANK_B_ADDR, &hb, &crc_b);
    if(!va && !vb) return; /* 无索引表不是故障，等待首次迁移 */

    if(va && vb)
    {
        if(seq_newer(hb.seq, ha.seq)) { g_hdr = hb; g_active_addr = IMAGE_BANK_B_ADDR; g_active_crc = crc_b; }
        else                          { g_hdr = ha; g_active_addr = IMAGE_BANK_A_ADDR; g_active_crc = crc_a; }
    }
    else if(va) { g_hdr = ha; g_active_addr = IMAGE_BANK_A_ADDR; g_active_crc = crc_a; }
    else        { g_hdr = hb; g_active_addr = IMAGE_BANK_B_ADDR; g_active_crc = crc_b; }

    g_valid = 1;
    load_current_record();
}

uint8_t image_store_valid(void) { return g_valid; }
uint8_t image_store_fault(void) { return g_fault; }
uint8_t image_store_current(void) { return g_valid ? g_current : IMAGE_ID_NONE; }

const image_record_t *image_store_current_record(void)
{
    return (g_valid && g_current < IMAGE_SLOTS) ? &g_current_rec : NULL;
}

uint8_t image_store_select_ram(uint8_t id)
{
    image_record_t rec;
    if(!g_valid || id >= IMAGE_SLOTS) return 0;
    if(!image_store_read_record(id, &rec) || !record_usable(&rec)) return 0;
    g_current = id;
    g_current_rec = rec;
    return 1;
}

void image_store_clear_current_ram(void)
{
    g_current = IMAGE_ID_NONE;
    memset(&g_current_rec, 0, sizeof(g_current_rec));
}

uint8_t image_store_read_record(uint8_t id, image_record_t *out)
{
    if(!out || id >= IMAGE_SLOTS || !g_valid) return 0;
    GD25QXX_Read(g_io, g_active_addr + IMAGE_BANK_HEADER_SIZE +
                 (uint32_t)id * IMAGE_RECORD_SIZE, IMAGE_RECORD_SIZE);
    memcpy(out, g_io, IMAGE_RECORD_SIZE);
    return 1;
}

uint8_t image_store_find_match(const uint8_t *algo, const uint8_t *bin, uint8_t opt,
                               uint32_t checksum, uint8_t checksum_valid, uint8_t *id_out)
{
    uint8_t id, match = IMAGE_ID_NONE, count = 0, current_match = 0;
    image_record_t rec;

    if(id_out) *id_out = IMAGE_ID_NONE;
    if(!algo || !bin || !g_valid) return 0;

    for(id = 0; id < IMAGE_SLOTS; id++)
    {
        if(!image_store_read_record(id, &rec)) continue;
        if(!record_usable(&rec)) continue;
        if(memcmp(rec.algo, algo, sizeof(rec.algo)) != 0) continue;
        if(memcmp(rec.bin, bin, sizeof(rec.bin)) != 0) continue;
        if((rec.flags & IMAGE_REC_FLAG_OPT) != (opt ? IMAGE_REC_FLAG_OPT : 0)) continue;
        /* 无 SUM 位的旧迁移记录允许按名字/OPT 匹配；有 SUM 位则必须一致。 */
        if(checksum_valid && (rec.flags & IMAGE_REC_FLAG_SUM) && rec.checksum != checksum)
            continue;
        count++;
        if(match == IMAGE_ID_NONE) match = id;
        if(id == g_current) current_match = 1;
    }

    if(count == 0) return 0;
    if(count == 1) { if(id_out) *id_out = match; return 1; }
    if(current_match) { if(id_out) *id_out = g_current; return 1; }
    return 0;
}

/* 把 bank 的第 sector 个 4KB 扇区组到 buf；返回本扇区参与 CRC 的字节数。 */
static uint16_t build_sector(uint8_t sector, const image_bank_header_t *hdr,
                             const image_record_t *replace, uint8_t *buf)
{
    uint32_t base = (uint32_t)sector * IMAGE_SECTOR_SIZE;
    uint32_t rs, re, end;
    uint16_t i, s, e;
    image_record_t rec;

    if(g_valid) GD25QXX_Read(buf, g_active_addr + base, IMAGE_SECTOR_SIZE);
    else memset(buf, 0xFF, IMAGE_SECTOR_SIZE);
    if(base == 0U) memcpy(buf, hdr, IMAGE_BANK_HEADER_SIZE);

    for(i = 0; i < IMAGE_SLOTS; i++)
    {
        if(g_valid && (!replace || replace->id != i)) continue;
        rs = IMAGE_BANK_HEADER_SIZE + (uint32_t)i * IMAGE_RECORD_SIZE;
        re = rs + IMAGE_RECORD_SIZE;
        end = base + IMAGE_SECTOR_SIZE;
        if(re <= base || rs >= end) continue;
        if(replace && replace->id == i) rec = *replace;
        else { memset(&rec, 0, sizeof(rec)); rec.id = (uint8_t)i; }
        s = (uint16_t)((rs > base) ? (rs - base) : 0U);
        e = (uint16_t)((re < end) ? (re - base) : IMAGE_SECTOR_SIZE);
        memcpy(buf + s, (const uint8_t *)&rec + ((rs > base) ? 0U : (base - rs)), e - s);
    }

    if(base >= IMAGE_BANK_DATA_SIZE) return 0;
    if(IMAGE_BANK_DATA_SIZE - base >= IMAGE_SECTOR_SIZE) return IMAGE_SECTOR_SIZE;
    return (uint16_t)(IMAGE_BANK_DATA_SIZE - base);
}

static uint32_t write_bank(uint32_t addr, const image_bank_header_t *hdr,
                       const image_record_t *replace)
{
    uint32_t crc = 0xFFFFFFFFUL, stored;
    uint16_t len;
    uint8_t sec;

    for(sec = 0; sec < IMAGE_BANK_SECTORS; sec++)
    {
        len = build_sector(sec, hdr, replace, DATA_BUFFER);
        crc = image_crc32_update(crc, DATA_BUFFER, len);
        if(sec == 2U)
        {
            stored = crc ^ 0xFFFFFFFFUL;
            DATA_BUFFER[IMAGE_CRC_IN_SECTOR2 + 0U] = (uint8_t)stored;
            DATA_BUFFER[IMAGE_CRC_IN_SECTOR2 + 1U] = (uint8_t)(stored >> 8);
            DATA_BUFFER[IMAGE_CRC_IN_SECTOR2 + 2U] = (uint8_t)(stored >> 16);
            DATA_BUFFER[IMAGE_CRC_IN_SECTOR2 + 3U] = (uint8_t)(stored >> 24);
        }
        GD25QXX_Erase_Sector((addr + (uint32_t)sec * IMAGE_SECTOR_SIZE) / IMAGE_SECTOR_SIZE);
        GD25QXX_Write_NoCheck(DATA_BUFFER, addr + (uint32_t)sec * IMAGE_SECTOR_SIZE,
                              IMAGE_SECTOR_SIZE);
    }
    return crc ^ 0xFFFFFFFFUL;
}

/* Sector-sized copy-on-write: a replacement may straddle two sectors. */
static uint8_t write_selection(uint8_t id, const image_record_t *replace)
{
    image_bank_header_t hdr = g_hdr, verify;
    uint32_t dst, expected, stored;
    if(g_fault || (!g_valid && !chip_supports_raw())) return 0;
    /* A bad source bank must not acquire a new, valid checksum during an upsert. */
    if(g_valid && replace && (!bank_valid(g_active_addr, &verify, &stored) ||
       stored != g_active_crc || memcmp(&g_hdr, &verify, sizeof(verify))))
    {
        g_fault = 1;
        return 0;
    }
    hdr.magic = IMAGE_BANK_MAGIC;
    hdr.version = IMAGE_BANK_VERSION;
    hdr.slots = IMAGE_SLOTS;
    hdr.seq = g_valid ? g_hdr.seq + 1U : 1U;
    hdr.current = id;
    hdr.flags = IMAGE_BANK_FLAG_VALID;
    dst = (g_valid && g_active_addr == IMAGE_BANK_A_ADDR) ? IMAGE_BANK_B_ADDR : IMAGE_BANK_A_ADDR;
    expected = write_bank(dst, &hdr, replace);
    if(!bank_valid(dst, &verify, &stored) || memcmp(&hdr, &verify, sizeof(hdr)) || stored != expected)
    {
        g_fault = 1;
        return 0;
    }
    g_active_addr = dst;
    g_active_crc = stored;
    g_hdr = hdr;
    g_valid = 1;
    load_current_record();
    return (uint8_t)(g_current == id);
}

uint8_t image_store_upsert(const image_record_t *rec)
{
    if(!rec || rec->id >= IMAGE_SLOTS || !record_usable(rec)) return 0;
    return write_selection(rec->id, rec);
}

uint8_t image_store_migrate_from_selection(const program_selection_t *sel)
{
    image_record_t rec;
    if(g_valid) return 1;
    if(!sel || !sel->algorithm_name[0] || !sel->program_name[0]) return 0;
    memset(&rec, 0, sizeof(rec));
    rec.id = 0U;
    rec.flags = IMAGE_REC_FLAG_VALID;
    if(sel->opt_enabled) rec.flags |= IMAGE_REC_FLAG_OPT;
    if(sel->checksum_valid)
    {
        rec.flags |= IMAGE_REC_FLAG_SUM;
        rec.checksum = sel->checksum;
    }
    memcpy(rec.algo, sel->algorithm_name, sizeof(rec.algo));
    memcpy(rec.bin, sel->program_name, sizeof(rec.bin));
    rec.algo[sizeof(rec.algo) - 1U] = 0;
    rec.bin[sizeof(rec.bin) - 1U] = 0;
    memcpy(rec.desc, "IMAGE 000", 9U);

    return image_store_upsert(&rec);
}

uint8_t image_store_set_current(uint8_t id)
{
    image_record_t rec;
    if(!g_valid) return 0;
    if(id != IMAGE_ID_NONE && (!image_store_read_record(id, &rec) || !record_usable(&rec))) return 0;
    if(id == g_hdr.current && id == g_current) return 1;
    return write_selection(id, NULL);
}

uint32_t image_store_seq(void) { return g_valid ? g_hdr.seq : 0; }
uint32_t image_store_crc(void) { return g_valid ? g_active_crc : 0; }

/* 上位机提交：先把整表写入非活动 bank，再回读校验。
 * 表内容由上位机按 11,220B 精确生成；设备只校验，不改写 seq/current。 */
uint8_t image_store_commit_from_file(FIL *fil, uint32_t expected_seq,
                                     uint32_t expected_crc, uint8_t *detail)
{
    image_bank_header_t hdr;
    uint8_t hb[IMAGE_BANK_HEADER_SIZE], tail[4];
    uint32_t crc = 0xFFFFFFFFUL, stored, target, off, n;
    UINT br;
    uint8_t sec;

    if(detail) *detail = 0;
    if(!fil) { if(detail) *detail = 2; return 0; }
    if(f_size(fil) != IMAGE_BANK_SIZE) { if(detail) *detail = 1; return 0; }
    if(f_lseek(fil, 0) != FR_OK) { if(detail) *detail = 2; return 0; }
    if(f_read(fil, hb, sizeof(hb), &br) != FR_OK || br != sizeof(hb))
    { if(detail) *detail = 2; return 0; }
    memcpy(&hdr, hb, sizeof(hdr));
    if(hdr.magic != IMAGE_BANK_MAGIC || hdr.version != IMAGE_BANK_VERSION ||
       hdr.slots != IMAGE_SLOTS || !(hdr.flags & IMAGE_BANK_FLAG_VALID) ||
       (hdr.current != IMAGE_ID_NONE && hdr.current >= IMAGE_SLOTS))
    { if(detail) *detail = 1; return 0; }
    if(hdr.seq != expected_seq + 1U) { if(detail) *detail = 3; return 0; }
    if(g_valid && expected_seq != g_hdr.seq) { if(detail) *detail = 3; return 0; }

    /* 读表头后指针在 16，CRC 扫描必须从文件开头重新读，否则会越过 EOF
     * （这是首次实测“暂存表读取失败 detail=2”的根因）。 */
    if(f_lseek(fil, 0) != FR_OK) { if(detail) *detail = 2; return 0; }
    /* CRC 覆盖 header + 100 records（11,216B）。 */
    off = 0;
    while(off < IMAGE_BANK_DATA_SIZE)
    {
        n = IMAGE_BANK_DATA_SIZE - off;
        if(n > sizeof(g_io)) n = sizeof(g_io);
        if(f_read(fil, g_io, (UINT)n, &br) != FR_OK || br != n)
        { if(detail) *detail = 2; return 0; }
        crc = image_crc32_update(crc, g_io, (uint16_t)n);
        off += n;
    }
    crc ^= 0xFFFFFFFFUL;
    if(f_read(fil, tail, 4, &br) != FR_OK || br != 4)
    { if(detail) *detail = 2; return 0; }
    stored = (uint32_t)tail[0] | ((uint32_t)tail[1] << 8) |
             ((uint32_t)tail[2] << 16) | ((uint32_t)tail[3] << 24);
    if(crc != stored || crc != expected_crc)
    { if(detail) *detail = 1; return 0; }

    target = (g_valid && g_active_addr == IMAGE_BANK_A_ADDR) ? IMAGE_BANK_B_ADDR : IMAGE_BANK_A_ADDR;
    if(f_lseek(fil, 0) != FR_OK) { if(detail) *detail = 2; return 0; }
    for(sec = 0; sec < IMAGE_BANK_SECTORS; sec++)
    {
        memset(DATA_BUFFER, 0xFF, IMAGE_SECTOR_SIZE);
        n = (sec == 2U) ? (IMAGE_BANK_SIZE - 2U * IMAGE_SECTOR_SIZE) : IMAGE_SECTOR_SIZE;
        if(f_read(fil, DATA_BUFFER, (UINT)n, &br) != FR_OK || br != n)
        { if(detail) *detail = 2; return 0; }
        GD25QXX_Erase_Sector((target + (uint32_t)sec * IMAGE_SECTOR_SIZE) / IMAGE_SECTOR_SIZE);
        GD25QXX_Write_NoCheck(DATA_BUFFER, target + (uint32_t)sec * IMAGE_SECTOR_SIZE,
                              IMAGE_SECTOR_SIZE);
    }

    image_store_init();
    if(g_valid && g_active_addr == target && g_hdr.seq == hdr.seq)
        return 1;
    if(detail) *detail = 4;
    return 0;
}
