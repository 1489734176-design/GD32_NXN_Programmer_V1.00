/**
 * @file    SWD_opt.c
 * @brief   通过SWD协议对目标MCU的OPT（选项字节）区编程
 */
#include "swd_host.h"
#include "SWD_opt.h"

/* 初始化OPT编程：复位并暂停内核、下装算法到SRAM、执行算法init。
 * 与Flash版不同，init的入参全为0（OPT不按Flash起始地址初始化）。
 * algo - OPT算法描述：算法代码块、SRAM地址、各函数入口与系统调用表
 * 返回 - ERROR_SUCCESS=成功，ERROR_RESET=复位暂停失败，
 *        ERROR_ALGO_DL=算法下装失败，ERROR_INIT=算法init返回非0 */
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

/* 结束OPT编程：执行算法uninit释放目标侧资源。
 * 注意本函数不改变目标运行状态、不释放SWD引脚，由调用方统一收尾。
 * algo - OPT算法描述，需提供uninit入口与系统调用表
 * 返回 - ERROR_SUCCESS=成功，ERROR_INIT=算法uninit返回非0 */
error_t target_opt_uninit(program_target_t *algo)
{
	if (0 == swd_flash_syscall_exec(&algo->sys_call_s, algo->uninit, 0, 0, 0, 0))
	{
		return ERROR_INIT;
	}
	return ERROR_SUCCESS;
}

/* 把一段数据写入目标OPT区（不分块，整段一次下装）。
 * algo - OPT算法描述，需提供program_buffer/program_page入口
 * addr - 目标OPT区地址
 * buf  - 源数据缓冲区
 * size - 待写入字节数，须不超过算法program_buffer的容量
 * 返回 - ERROR_SUCCESS=成功，ERROR_ALGO_DATA_SEQ=数据搬到SRAM失败，
 *        ERROR_WRITE=算法program_page返回非0 */
error_t target_opt_program_page(program_target_t *algo, uint32_t addr, const uint8_t *buf, uint32_t size)
{
	// 先把本页数据写入目标SRAM缓冲区
	if (!swd_write_memory(algo->program_buffer, (uint8_t *)buf, size))
	{
		return ERROR_ALGO_DATA_SEQ;
	}

	// 执行OPT编程
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

/* 擦除OPT区所在扇区。
 * algo - OPT算法描述，需提供erase_sector入口与系统调用表
 * addr - 要擦除的扇区地址
 * 返回 - ERROR_SUCCESS=成功，ERROR_ERASE_SECTOR=算法返回非0 */
error_t target_opt_erase_sector(program_target_t *algo, uint32_t addr)
{
	if (0 == swd_flash_syscall_exec(&algo->sys_call_s, algo->erase_sector, addr, 0, 0, 0))
	{
		return ERROR_ERASE_SECTOR;
	}

	return ERROR_SUCCESS;
}

/* 擦除整片OPT区。
 * algo - OPT算法描述，需提供erase_chip入口与系统调用表
 * 返回 - ERROR_SUCCESS=成功，ERROR_ERASE_ALL=算法返回非0 */
error_t target_opt_erase_chip(program_target_t *algo)
{
	error_t status = ERROR_SUCCESS;

	if (0 == swd_flash_syscall_exec(&algo->sys_call_s, algo->erase_chip, 0, 0, 0, 0))
	{
		return ERROR_ERASE_ALL;
	}

	return status;
}

/* 用扩展算法写入OPT字节：先把数据搬到目标SRAM，再调用算法的opt_program。
 * 与target_opt_program_page的差别是地址与长度来自算法描述表，不由调用方指定。
 * algo     - 扩展算法描述：含opt_addr/opt_size/opt_program与SRAM缓冲区
 * opt_data - OPT数据缓冲区，长度须不小于algo->opt_size
 * 返回     - ERROR_SUCCESS=成功，ERROR_ALGO_DATA_SEQ=数据搬到SRAM失败，
 *            ERROR_WRITE=算法opt_program返回非0 */
error_t target_opt_program_ext(program_target_ext_t *algo, const uint8_t *opt_data)
{
	/* 把OPT数据写入目标SRAM缓冲区 */
	if (!swd_write_memory(algo->program_buffer, (uint8_t *)opt_data, algo->opt_size))
	{
		return ERROR_ALGO_DATA_SEQ;
	}

	/* 在目标上执行ProgramOpt(opt_addr, opt_size, program_buffer) */
	if (!swd_flash_syscall_exec(&algo->sys_call_s,
	                             algo->opt_program,
	                             algo->opt_addr,
	                             algo->opt_size,
	                             algo->program_buffer,
	                             0))
	{
		return ERROR_WRITE;
	}

	return ERROR_SUCCESS;
}
