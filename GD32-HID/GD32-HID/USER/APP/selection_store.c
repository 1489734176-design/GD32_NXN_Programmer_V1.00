/* 中文注释：配置日志采用CRC、序号反码和最后提交标记，两页轮换保证掉电可恢复。 */
#include "selection_store.h"

#define SELECTION_MAGIC 0x314C4553UL /* SEL1，配置日志格式版本1 */
#define SELECTION_COMMIT 0x54494D43UL
#define SELECTION_WORDS 23U
#define SELECTION_RECORD_SIZE (SELECTION_WORDS * 4U)
#define SELECTION_SLOTS (SELECTION_STORE_PAGE_SIZE / SELECTION_RECORD_SIZE)

static program_selection_t selection;
static uint32_t selection_sequence;
static uint8_t selection_page, selection_valid, selection_fault;
static uint8_t occupied[2], corrupt_pages;

/* 仅计算本模块独立预留区内的记录地址。 */
static uint32_t record_address(uint8_t page, uint8_t slot)
{
    return SELECTION_STORE_FLASH_BASE + page * SELECTION_STORE_PAGE_SIZE +
           slot * SELECTION_RECORD_SIZE;
}

/* 清零结构体和尾部填充，避免无关内存参与比较或返回。 */
static void clear_selection(program_selection_t *value)
{
    uint8_t i;
    for(i = 0; i < sizeof(*value); i++) ((uint8_t *)value)[i] = 0;
}

/* 文件名只能是ASCII单个文件名，最多31字节；NUL后的内容不参与保存。 */
static uint8_t copy_name(uint8_t *out, const uint8_t *in)
{
    uint8_t i, c;
    for(i = 0; i < SELECTION_STORE_NAME_SIZE; i++)
    {
        c = in[i];
        if(!c)
        {
            if((i == 1 && out[0] == '.') ||
               (i == 2 && out[0] == '.' && out[1] == '.')) return 0;
            return 1;
        }
        if(c < 0x20U || c >= 0x7FU || c == '/' || c == '\\' || c == ':' ||
           c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return 0;
        out[i] = c;
    }
    return 0;
}

/* 规范化部分选择与失效校验：没有固件时不能声明校验有效；缺项时关闭OPT。 */
static uint8_t normalize(const program_selection_t *in, program_selection_t *out)
{
    if(!in || in->opt_enabled > 1 || in->checksum_valid > 1) return 0;
    clear_selection(out);
    if(!copy_name(out->algorithm_name, in->algorithm_name) ||
       !copy_name(out->program_name, in->program_name) ||
       (!out->program_name[0] && in->checksum_valid)) return 0;
    out->checksum_valid = in->checksum_valid;
    out->checksum = in->checksum_valid ? in->checksum : 0;
    out->opt_enabled = out->algorithm_name[0] && out->program_name[0] ? in->opt_enabled : 0;
    return 1;
}

/* 比较有效字段和规范化文件名，相同配置不磨损Flash。 */
static uint8_t same_selection(const program_selection_t *a, const program_selection_t *b)
{
    uint8_t i;
    if(a->checksum != b->checksum || a->opt_enabled != b->opt_enabled ||
       a->checksum_valid != b->checksum_valid) return 0;
    for(i = 0; i < SELECTION_STORE_NAME_SIZE; i++)
        if(a->algorithm_name[i] != b->algorithm_name[i] ||
           a->program_name[i] != b->program_name[i]) return 0;
    return 1;
}

/* CRC32覆盖前80字节：标识、序号、标志、SUM32和两个文件名。 */
static uint32_t checksum(const uint32_t *words)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint8_t i, byte, bit;
    for(i = 0; i < 20; i++)
        for(byte = 0; byte < 4; byte++)
        {
            crc ^= (words[i] >> (byte * 8)) & 0xFFU;
            for(bit = 0; bit < 8; bit++)
                crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
        }
    return crc ^ 0xFFFFFFFFUL;
}

/* 验证已提交记录及其规范化内容；损坏的旧记录也必须报告异常。 */
static uint8_t record_valid(const uint32_t *r, program_selection_t *out)
{
    program_selection_t raw;
    uint8_t i;
    if(r[0] != SELECTION_MAGIC || r[2] > 3 || r[20] != checksum(r) ||
       r[21] != ~r[1] || r[22] != SELECTION_COMMIT) return 0;
    clear_selection(&raw);
    for(i = 0; i < SELECTION_STORE_NAME_SIZE; i++)
    {
        raw.algorithm_name[i] = ((const uint8_t *)&r[4])[i];
        raw.program_name[i] = ((const uint8_t *)&r[12])[i];
    }
    raw.checksum = r[3];
    raw.opt_enabled = (uint8_t)(r[2] & 1U);
    raw.checksum_valid = (uint8_t)((r[2] >> 1) & 1U);
    return normalize(&raw, out) && same_selection(&raw, out);
}

/* 扫描两页，序号使用有符号差值比较，以支持FFFFFFFF到00000000回绕。 */
void selection_store_init(void)
{
    uint32_t r[SELECTION_WORDS];
    program_selection_t value;
    uint8_t page, slot, i, empty, any_data = 0;
    clear_selection(&selection);
    selection_sequence = 0;
    selection_page = selection_valid = selection_fault = corrupt_pages = 0;
    occupied[0] = occupied[1] = 0;
    for(page = 0; page < 2; page++)
        for(slot = 0; slot < SELECTION_SLOTS; slot++)
        {
            empty = 1;
            for(i = 0; i < SELECTION_WORDS; i++)
            {
                r[i] = selection_store_read_word(record_address(page, slot) + i * 4U);
                if(r[i] != 0xFFFFFFFFUL) empty = 0;
            }
            if(empty) continue;
            any_data = 1;
            occupied[page] = slot + 1;
            if(!record_valid(r, &value))
            {
                /* 未提交槽跳过且不再次使用；已提交损坏时不能回退到较旧配置。 */
                if(r[22] == SELECTION_COMMIT)
                {
                    selection_fault = 1;
                    corrupt_pages |= (uint8_t)(1U << page);
                }
                continue;
            }
            if(!selection_valid || (int32_t)(r[1] - selection_sequence) > 0)
            {
                selection = value;
                selection_sequence = r[1];
                selection_page = page;
                selection_valid = 1;
            }
        }
    /* 只有撕裂数据、没有完整配置时保持不可恢复状态，等待显式保存。 */
    if(any_data && !selection_valid) selection_fault = 1;
}

uint8_t selection_store_load(program_selection_t *out)
{
    if(!out || selection_fault || !selection_valid) return 0;
    *out = selection;
    return 1;
}

uint8_t selection_store_fault(void) { return selection_fault; }

/* 擦除后逐字验证整页，避免把未擦干净的槽当作可写空间。 */
static uint8_t erase_page(uint8_t page)
{
    uint32_t address = record_address(page, 0), offset;
    if(!selection_store_erase_page(address)) return 0;
    for(offset = 0; offset < SELECTION_STORE_PAGE_SIZE; offset += 4U)
        if(selection_store_read_word(address + offset) != 0xFFFFFFFFUL) return 0;
    return 1;
}

/* 先写数据、CRC和反码，最后一个字提交；换页前保留旧配置所在页。 */
uint8_t selection_store_save(const program_selection_t *in)
{
    program_selection_t value;
    uint32_t r[SELECTION_WORDS], address;
    uint8_t i, page, slot, preserved, repair;
    if(!normalize(in, &value)) return 0;
    if(!selection_fault && selection_valid && same_selection(&selection, &value)) return 1;
    repair = selection_fault;
    if(repair)
    {
        /* 重扫可跳过本次启动中写失败的槽，并识别提交后清理失败的最新记录。 */
        selection_store_init();
        selection_fault = 1;
    }
    preserved = selection_page;
    if(repair)
    {
        /* 保存损坏页直到新记录提交，防止先擦掉异常证据而复活另一页的旧配置。 */
        if(corrupt_pages) preserved = (corrupt_pages & 1U) ? 0 : 1;
        else if(!selection_valid) preserved = occupied[0] ? 0 : 1;
    }
    page = preserved;
    slot = occupied[page];
    if(repair || slot >= SELECTION_SLOTS)
    {
        page ^= 1U;
        slot = 0;
        if(!erase_page(page)) goto storage_error;
    }
    address = record_address(page, slot);
    r[0] = SELECTION_MAGIC;
    r[1] = selection_sequence + 1U;
    r[2] = value.opt_enabled | ((uint32_t)value.checksum_valid << 1);
    r[3] = value.checksum;
    for(i = 0; i < SELECTION_STORE_NAME_SIZE; i++)
    {
        ((uint8_t *)&r[4])[i] = value.algorithm_name[i];
        ((uint8_t *)&r[12])[i] = value.program_name[i];
    }
    r[20] = checksum(r);
    r[21] = ~r[1];
    r[22] = SELECTION_COMMIT;
    for(i = 0; i < SELECTION_WORDS; i++)
        if(!selection_store_program_word(address + i * 4U, r[i]) ||
           selection_store_read_word(address + i * 4U) != r[i]) goto storage_error;
    /* 修复时新配置已经提交，才删除旧异常页；删除失败仍禁止本次启动恢复。 */
    if(repair && !erase_page(preserved)) goto storage_error;
    selection = value;
    selection_sequence = r[1];
    selection_page = page;
    occupied[page] = slot + 1;
    if(repair) occupied[preserved] = 0;
    selection_valid = 1;
    selection_fault = corrupt_pages = 0;
    return 1;
storage_error:
    selection_fault = 1;
    return 0;
}
