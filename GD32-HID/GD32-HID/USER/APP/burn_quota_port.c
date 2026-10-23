#include "main.h"
#include "burn_quota.h"
#include "gd32f10x_fmc.h"

static uint8_t valid_address(uint32_t address)
{
    return address >= BURN_QUOTA_FLASH_BASE &&
           address < BURN_QUOTA_FLASH_BASE + 2U * BURN_QUOTA_PAGE_SIZE && !(address & 3U);
}

uint32_t burn_quota_read_word(uint32_t address)
{
    return valid_address(address) ? *(volatile uint32_t *)address : 0;
}

static void clear_flags(void)
{
    fmc_flag_clear(FMC_FLAG_BANK0_END);
    fmc_flag_clear(FMC_FLAG_BANK0_PGERR);
    fmc_flag_clear(FMC_FLAG_BANK0_WPERR);
}

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
