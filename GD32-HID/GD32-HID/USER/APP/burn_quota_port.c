/* 中文注释补充（2026-10-26）：此前修改说明：保留原烧录/硬件流程，下方以中文说明相关界面与兼容逻辑。 */
#include "main.h"
#include "burn_quota.h"
#include "gd32f10x_fmc.h"

/* 只允许访问预留的最后两个1KB页，且必须四字节对齐。 */
static uint8_t valid_address(uint32_t address)
{
    return address >= BURN_QUOTA_FLASH_BASE &&
           address < BURN_QUOTA_FLASH_BASE + 2U * BURN_QUOTA_PAGE_SIZE && !(address & 3U);
}

/* 读取次数日志中的32位数据；越界地址不读。 */
uint32_t burn_quota_read_word(uint32_t address)
{
    return valid_address(address) ? *(volatile uint32_t *)address : 0;
}

/* 清除FMC完成/编程错误/写保护错误标志，再执行下一次存储操作。 */
static void clear_flags(void)
{
    fmc_flag_clear(FMC_FLAG_BANK0_END);
    fmc_flag_clear(FMC_FLAG_BANK0_PGERR);
    fmc_flag_clear(FMC_FLAG_BANK0_WPERR);
}

/* 在短临界区内解锁FMC、写入一个日志字并重新锁定；由日志层读回验证。 */
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

/* 擦除预留次数页；地址检查保证不擦应用或启动程序。 */
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
