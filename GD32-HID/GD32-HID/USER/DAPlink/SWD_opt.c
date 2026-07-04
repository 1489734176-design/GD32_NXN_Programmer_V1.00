/**
 * @file    SWD_opt.c
 * @brief   通过SWD协议对MCU的FLASH编程
 */
#include "swd_host.h"
#include "SWD_opt.h"

error_t target_opt_init(program_target_t *algo)
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

	if (0 == swd_flash_syscall_exec(&algo->sys_call_s, algo->init, 0, 0, 0, 0))
	{
		return ERROR_INIT;
	}

	return ERROR_SUCCESS;
}

error_t target_opt_uninit(program_target_t *algo)
{
	if (0 == swd_flash_syscall_exec(&algo->sys_call_s, algo->uninit, 0, 0, 0, 0))
	{
		return ERROR_INIT;
	}
	return ERROR_SUCCESS;
}

error_t target_opt_program_page(program_target_t *algo, uint32_t addr, const uint8_t *buf, uint32_t size)
{
	// Write page to buffer
	if (!swd_write_memory(algo->program_buffer, (uint8_t *)buf, size))
	{
		return ERROR_ALGO_DATA_SEQ;
	}

	// Run flash programming
	if (!swd_flash_syscall_exec(&algo->sys_call_s,
								algo->program_page,
								addr,
								size,
								algo->program_buffer,
								0))
	{
		return ERROR_WRITE;
	}
	return ERROR_SUCCESS;
}

error_t target_opt_erase_sector(program_target_t *algo, uint32_t addr)
{
	if (0 == swd_flash_syscall_exec(&algo->sys_call_s, algo->erase_sector, addr, 0, 0, 0))
	{
		return ERROR_ERASE_SECTOR;
	}

	return ERROR_SUCCESS;
}

error_t target_opt_erase_chip(program_target_t *algo)
{
	error_t status = ERROR_SUCCESS;

	if (0 == swd_flash_syscall_exec(&algo->sys_call_s, algo->erase_chip, 0, 0, 0, 0))
	{
		return ERROR_ERASE_ALL;
	}

	return status;
}
