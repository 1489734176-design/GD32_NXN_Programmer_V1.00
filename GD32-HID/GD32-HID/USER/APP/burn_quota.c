/* 中文注释补充（2026-10-26）：此前修改说明：保留原烧录/硬件流程，下方以中文说明相关界面与兼容逻辑。 */
#include "burn_quota.h"

#define QUOTA_MAGIC 0x31545142UL /* BQT1，次数日志格式版本1 */
#define QUOTA_COMMIT 0x54494D43UL
#define QUOTA_WORDS 8U
#define QUOTA_RECORD_SIZE (QUOTA_WORDS * 4U)
#define QUOTA_SLOTS (BURN_QUOTA_PAGE_SIZE / QUOTA_RECORD_SIZE)

static uint32_t quota_sequence;
static uint32_t quota_remaining;
static uint8_t quota_enabled, quota_fault, quota_reserved;
static uint8_t quota_page, quota_next, quota_valid;

/* 计算次数日志页与记录槽的内部Flash地址。
 * page - 日志页编号，0或1（两页轮换使用）
 * slot - 页内记录槽编号，0 ~ QUOTA_SLOTS-1
 * 返回 - 该记录槽在内部Flash中的绝对地址 */
static uint32_t record_address(uint8_t page, uint8_t slot)
{
    return BURN_QUOTA_FLASH_BASE + page * BURN_QUOTA_PAGE_SIZE + slot * QUOTA_RECORD_SIZE;
}

/* 计算次数记录CRC32，用于检测记录损坏和掉电写入不完整。
 * words - 记录字数组，只对前5个字（标识、序号、使能、剩余次数及其反码）计算
 * 返回 - CRC32校验值（多项式0xEDB88320，初值与结果均取反） */
static uint32_t checksum(const uint32_t *words)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint8_t i, byte, bit;
    for(i = 0; i < 5; i++)
        for(byte = 0; byte < 4; byte++)
        {
            crc ^= (words[i] >> (byte * 8)) & 0xFFU;
            for(bit = 0; bit < 8; bit++)
                crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
        }
    return crc ^ 0xFFFFFFFFUL;
}

/* 同时验证标识、范围、补码、CRC与提交标记，防止误恢复旧次数。
 * r     - 待校验的8字记录数组
 * 返回  - 1=记录完整有效，0=记录无效（未提交或已损坏） */
static uint8_t record_valid(const uint32_t *r)
{
    return r[0] == QUOTA_MAGIC && r[2] <= 1 && r[3] <= BURN_QUOTA_MAX &&
           (r[2] || r[3] == 0) && r[4] == ~r[3] && r[5] == checksum(r) &&
           r[6] == ~r[1] && r[7] == QUOTA_COMMIT;
}

/* 扫描两页次数日志，选择最新完整记录；未提交记录忽略，已提交损坏记录禁止烧录。
 * 返回 - 无（结果写入quota_*静态变量） */
void burn_quota_init(void)
{
    uint32_t r[QUOTA_WORDS];
    uint8_t page, slot, i, empty, any_data = 0, occupied[2] = {0, 0};
    quota_sequence = quota_remaining = 0;
    quota_enabled = quota_fault = quota_reserved = quota_valid = quota_page = quota_next = 0;
    for(page = 0; page < 2; page++)
        for(slot = 0; slot < QUOTA_SLOTS; slot++)
        {
            empty = 1;
            for(i = 0; i < QUOTA_WORDS; i++)
            {
                r[i] = burn_quota_read_word(record_address(page, slot) + i * 4U);
                if(r[i] != 0xFFFFFFFFUL) empty = 0;
            }
            if(empty) continue;
            any_data = 1;
            occupied[page] = slot + 1;
            if(!record_valid(r))
            {
                /* 写了一半、尚未提交的记录直接忽略；已提交但数据损坏的记录
                 * 按异常处理（拒绝烧录），而不是把次数补回来。 */
                if(r[7] == QUOTA_COMMIT) quota_fault = 1;
                continue;
            }
            if(!quota_valid || (int32_t)(r[1] - quota_sequence) > 0)
            {
                quota_valid = 1;
                quota_sequence = r[1]; quota_enabled = (uint8_t)r[2];
                quota_remaining = r[3]; quota_page = page;
            }
        }
    quota_next = occupied[quota_page];
    if(any_data && !quota_valid) quota_fault = 1;
}

/* 查询是否启用次数限制。
 * 返回 - 1=已启用，0=不限次数 */
uint8_t burn_quota_enabled(void) { return quota_enabled; }
/* 查询次数记录是否异常；异常时拒绝开始烧录。
 * 返回 - 1=记录异常，0=正常 */
uint8_t burn_quota_fault(void) { return quota_fault; }
/* 查询当前剩余次数，界面以三位数字补零显示。
 * 返回 - 剩余可烧录次数，0~BURN_QUOTA_MAX */
uint32_t burn_quota_remaining(void) { return quota_remaining; }

/* 追加次数记录，先写数据最后提交；页满时轮换，提交前保留旧页，写入异常立即禁止烧录。
 * enabled   - 是否启用次数限制，1=启用，0=不限
 * remaining - 本次要持久化的剩余次数
 * repair    - 1=强制换页重写（用于从异常状态恢复），0=顺序追加
 * 返回      - BURN_QUOTA_OK=写入成功，BURN_QUOTA_STORAGE=Flash读写失败 */
static uint8_t append(uint8_t enabled, uint32_t remaining, uint8_t repair)
{
    uint32_t r[QUOTA_WORDS], address;
    uint8_t i, page = quota_page, slot = quota_next;
    if(repair || slot >= QUOTA_SLOTS)
    {
        page ^= 1U; slot = 0;
        /* 替换记录提交之前，保持当前已提交页原样不动。 */
        if(!burn_quota_erase_page(record_address(page, 0))) goto storage_error;
    }
    address = record_address(page, slot);
    r[0] = QUOTA_MAGIC; r[1] = quota_sequence + 1U; r[2] = enabled;
    r[3] = remaining; r[4] = ~remaining; r[5] = checksum(r);
    r[6] = ~r[1]; r[7] = QUOTA_COMMIT;
    for(i = 0; i < QUOTA_WORDS; i++)
    {
        if(!burn_quota_program_word(address + i * 4U, r[i]) ||
           burn_quota_read_word(address + i * 4U) != r[i]) goto storage_error;
    }
    if(repair && !burn_quota_erase_page(record_address(quota_page, 0))) goto storage_error;
    quota_sequence = r[1]; quota_enabled = enabled; quota_remaining = remaining;
    quota_page = page; quota_next = slot + 1; quota_valid = 1; quota_fault = 0;
    return BURN_QUOTA_OK;
storage_error:
    quota_fault = 1;
    return BURN_QUOTA_STORAGE;
}

/* 保存上位机设置的0～999额度或不限；烧录预扣尚未结束时拒绝重设，相同设置不重复擦写。
 * enabled   - 是否启用次数限制，1=启用，0=不限
 * remaining - 剩余次数，启用时取0~BURN_QUOTA_MAX，不启用时必须为0
 * 返回      - BURN_QUOTA_OK=成功，BURN_QUOTA_BUSY=有预扣未结束，
 *             BURN_QUOTA_STORAGE=参数非法或Flash写入失败 */
uint8_t burn_quota_set(uint8_t enabled, uint32_t remaining)
{
    if(quota_reserved) return BURN_QUOTA_BUSY;
    if(enabled > 1 || remaining > BURN_QUOTA_MAX || (!enabled && remaining))
        return BURN_QUOTA_STORAGE;
    if(!quota_fault && quota_valid && enabled == quota_enabled && remaining == quota_remaining)
        return BURN_QUOTA_OK;
    return append(enabled, remaining, quota_fault);
}

/* 目标芯片上电/烧录前持久化预扣一次；额度为零或存储异常时拒绝开始。
 * 返回 - BURN_QUOTA_OK=可以烧录（不限次数或已扣减成功），
 *        BURN_QUOTA_BUSY=上一次预扣尚未结束，BURN_QUOTA_EMPTY=次数已用尽，
 *        BURN_QUOTA_STORAGE=记录异常或Flash写入失败 */
uint8_t burn_quota_begin(void)
{
    uint8_t result;
    if(quota_reserved) return BURN_QUOTA_BUSY;
    if(quota_fault) return BURN_QUOTA_STORAGE;
    if(!quota_enabled) return BURN_QUOTA_OK;
    if(!quota_remaining) return BURN_QUOTA_EMPTY;
    result = append(1, quota_remaining - 1U, 0);
    if(result == BURN_QUOTA_OK) quota_reserved = 1;
    return result;
}

/* 结束本次预扣：成功不再写Flash，普通失败退回一次；掉电未执行本函数时保留扣减。
 * success - 1=本次烧录成功（保留扣减），0=本次失败（退回一次）
 * 返回    - BURN_QUOTA_OK=处理完成，BURN_QUOTA_STORAGE=退回时Flash写入失败 */
uint8_t burn_quota_finish(uint8_t success)
{
    if(!quota_reserved) return BURN_QUOTA_OK;
    quota_reserved = 0;
    if(success) return BURN_QUOTA_OK;
    return append(1, quota_remaining + 1U, 0);
}
