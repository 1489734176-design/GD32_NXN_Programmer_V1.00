/* 中文注释：配置日志内部Flash适配层，临界区和FMC操作沿用次数日志方式。 */
#include "main.h"
#include "selection_store.h"
#include "gd32f10x_fmc.h"

/* 只允许访问独立预留两页，四字节对齐；不会碰到相邻burn_quota区域。 */
static uint8_t valid_address(uint32_t address)
{
    return address >= SELECTION_STORE_FLASH_BASE &&
           address < SELECTION_STORE_FLASH_BASE + 2U * SELECTION_STORE_PAGE_SIZE &&
           !(address & 3U);
}

/* 地址非法时返回0，不解引用越界地址。 */
uint32_t selection_store_read_word(uint32_t address)
{
    return valid_address(address) ? *(volatile uint32_t *)address : 0;
}

/* 每次操作前清除完成、编程错误和写保护错误标志。 */
static void clear_flags(void)
{
    fmc_flag_clear(FMC_FLAG_BANK0_END);
    fmc_flag_clear(FMC_FLAG_BANK0_PGERR);
    fmc_flag_clear(FMC_FLAG_BANK0_WPERR);
}

/* 在短临界区内解锁、写一个字并锁定；日志层随后读回验证。 */
uint8_t selection_store_program_word(uint32_t address, uint32_t value)
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

/* 必须是预留区内页首，地址检查防止擦除应用、启动程序或次数日志。 */
uint8_t selection_store_erase_page(uint32_t address)
{
    fmc_state_enum result;
    if(!valid_address(address) || address % SELECTION_STORE_PAGE_SIZE) return 0;
    taskENTER_CRITICAL();
    fmc_unlock(); clear_flags();
    result = fmc_page_erase(address);
    fmc_lock();
    taskEXIT_CRITICAL();
    return result == FMC_READY;
}
