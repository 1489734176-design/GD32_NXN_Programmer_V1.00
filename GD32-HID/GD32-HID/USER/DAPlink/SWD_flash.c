/*
 * @Descripttion: 
 * @version: 
 * @Author: Kevincoooool
 * @Date: 2020-07-22 21:35:49
 * @LastEditors  : Kevincoooool
 * @LastEditTime : 2020-10-15 19:21:23
 * @FilePath     : \Simple_TeenyUSB_TX\CMSIS-DAP\Source\SWD_flash.c
 */
/**
 * @file    SWD_flash.c
 * @brief   通过SWD协议对MCU的FLASH编程
 */
#include "swd_host.h"
#include "SWD_flash.h"

uint32_t Flash_Page_Size = 1024;
error_t target_flash_init(program_target_t *algo, uint32_t flash_start)
{
    if (0 == swd_set_target_state_hw(RESET_PROGRAM))
    {
        return ERROR_RESET;
    }

    // 下载编程算法到目标MCU的SRAM，并初始化
    if (0 == swd_write_memory(algo->algo_start, (uint8_t *)algo->algo_blob, algo->algo_size))
    {
        return ERROR_ALGO_DL;
    }

    if (0 == swd_flash_syscall_exec(&algo->sys_call_s, algo->init, flash_start, 0, 0, 0))
    {
        return ERROR_INIT;
    }

    return ERROR_SUCCESS;
}

error_t target_flash_uninit(void)
{
    swd_set_target_state_hw(RESET_RUN);

    swd_off();
    return ERROR_SUCCESS;
}

error_t target_flash_program_page(program_target_t *algo, uint32_t addr, const uint8_t *buf, uint32_t size)
{
    while (size > 0)
    {
        uint32_t write_size = size > algo->program_buffer_size ? algo->program_buffer_size : size;

        // Write page to buffer
        if (!swd_write_memory(algo->program_buffer, (uint8_t *)buf, write_size))
        {
            return ERROR_ALGO_DATA_SEQ;
        }

        // Run flash programming
        if (!swd_flash_syscall_exec(&algo->sys_call_s,
                                    algo->program_page,
                                    addr,
                                    algo->program_buffer_size,
                                    algo->program_buffer,
                                    0))
        {
            return ERROR_WRITE;
        }

        addr += write_size;
        buf += write_size;
        size -= write_size;
    }

    return ERROR_SUCCESS;
}

error_t target_flash_erase_sector(program_target_t *algo, uint32_t addr)
{
    if (0 == swd_flash_syscall_exec(&algo->sys_call_s, algo->erase_sector, addr, 0, 0, 0))
    {
        return ERROR_ERASE_SECTOR;
    }

    return ERROR_SUCCESS;
}

error_t target_flash_erase_chip(program_target_t *algo)
{
    error_t status = ERROR_SUCCESS;

    if (0 == swd_flash_syscall_exec(&algo->sys_call_s, algo->erase_chip, 0, 0, 0, 0))
    {
        return ERROR_ERASE_ALL;
    }
	
    return status;
}
