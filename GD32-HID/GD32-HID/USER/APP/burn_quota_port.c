/* 中文注释补充（2026-10-26）：此前修改说明：保留原烧录/硬件流程，下方以中文说明相关界面与兼容逻辑。 */
#include "main.h"
#include "burn_quota.h"
#include "gd32f10x_fmc.h"
#include "quota_auth.h"

/* GD32唯一ID只参与设备绑定；不把UID当作秘密密钥。 */
void burn_quota_device_id(uint32_t out[3])
{
    out[0] = *(volatile uint32_t *)0x1FFFF7E8UL;
    out[1] = *(volatile uint32_t *)0x1FFFF7ECUL;
    out[2] = *(volatile uint32_t *)0x1FFFF7F0UL;
}

/* 只允许访问预留的最后两个1KB页，且必须四字节对齐。
 * address - 待检查的内部Flash地址
 * 返回    - 1=地址在预留区内且4字节对齐，0=越界或未对齐 */
static uint8_t valid_address(uint32_t address)
{
    return address >= BURN_QUOTA_FLASH_BASE &&
           address < BURN_QUOTA_FLASH_BASE + 2U * BURN_QUOTA_PAGE_SIZE && !(address & 3U);
}

/* 读取次数日志中的32位数据；越界地址不读。
 * address - 要读取的内部Flash地址，须落在预留次数页内
 * 返回    - 读到的32位数据；地址非法时返回0 */
uint32_t burn_quota_read_word(uint32_t address)
{
    return valid_address(address) ? *(volatile uint32_t *)address : 0;
}

/* 清除FMC完成/编程错误/写保护错误标志，再执行下一次存储操作。
 * 返回 - 无 */
static void clear_flags(void)
{
    fmc_flag_clear(FMC_FLAG_BANK0_END);
    fmc_flag_clear(FMC_FLAG_BANK0_PGERR);
    fmc_flag_clear(FMC_FLAG_BANK0_WPERR);
}

/* 在短临界区内解锁FMC、写入一个日志字并重新锁定；由日志层读回验证。
 * address - 目标内部Flash地址，须4字节对齐且落在预留次数页内
 * value   - 要写入的32位数据
 * 返回    - 1=写入成功，0=地址非法或FMC编程失败 */
uint8_t burn_quota_program_word(uint32_t address, uint32_t value)
{
    fmc_state_enum result;
    if(!valid_address(address)) return 0;
    taskENTER_CRITICAL();
    fmc_unlock(); clear_flags();
    result = fmc_word_program(address, value);
    fmc_lock();
    taskEXIT_CRITICAL();
    return result == FMC_READY;
}

/* 擦除预留次数页；地址检查保证不擦应用或启动程序。
 * address - 要擦除的页首地址，须页对齐且落在预留次数页内
 * 返回    - 1=擦除成功，0=地址非法或FMC擦除失败 */
uint8_t burn_quota_erase_page(uint32_t address)
{
    fmc_state_enum result;
    if(!valid_address(address) || address % BURN_QUOTA_PAGE_SIZE) return 0;
    taskENTER_CRITICAL();
    fmc_unlock(); clear_flags();
    result = fmc_page_erase(address);
    fmc_lock();
    taskEXIT_CRITICAL();
    return result == FMC_READY;
}
