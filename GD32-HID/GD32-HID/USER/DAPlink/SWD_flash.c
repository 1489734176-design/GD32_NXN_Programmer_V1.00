/*
 * @Description: 通过SWD协议对目标MCU的Flash进行编程、擦除与校验
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
#include <string.h>
#include "debug_cm.h"

#define VFY_CSW_VAL  (CSW_RESERVED | CSW_MSTRDBG | CSW_HPROT | CSW_DBGSTAT | CSW_SADDRINC)

uint32_t Flash_Page_Size = 1024;
/* 初始化目标Flash编程：复位并暂停内核、下装算法到SRAM、执行算法init。
 * algo        - Flash算法描述：算法代码块、SRAM地址、各函数入口与系统调用表
 * flash_start - 目标Flash的起始地址，作为init函数的入参（R0）
 * 返回        - ERROR_SUCCESS=成功，ERROR_RESET=复位暂停失败，
 *               ERROR_ALGO_DL=算法下装失败，ERROR_INIT=算法init返回非0 */
error_t target_flash_init(program_target_t *algo, uint32_t flash_start)
{
    uint32_t st = 0;

    if (0 == swd_set_target_state_hw(RESET_PROGRAM))
    {
        /* DHCSR位于内核调试的固定地址；DBG_HCSR需要SWD_host.c内部定义的DBG_Addr，
           这里直接用原始地址读取。 */
        uint32_t h = 0;
        if (swd_read_memory(0xE000EDF0u, (uint8_t *)&h, 4))
        {
            g_diag_hcsr = h;
        }
        return ERROR_RESET;
    }

    /* 复位流程写AIRCR时不检查结果，一旦出错就会留下STICKYERR，
       导致之后所有AP访问失败。因此在下装算法之前先清掉它。 */
    swd_write_dp(DP_ABORT, STKCMPCLR | STKERRCLR | WDERRCLR | ORUNERRCLR);

    // 下载编程算法到目标MCU的SRAM，并初始化
    if (0 == swd_write_memory(algo->algo_start, (uint8_t *)algo->algo_blob, algo->algo_size))
    {
        return ERROR_ALGO_DL;
    }

    /* 被拒绝的AP传输仍然返回ACK=OK，错误只体现在STICKYERR上。
       这里直接按下载失败处理，避免后面系统调用返回误导性的0xDEE1。 */
    if (swd_read_dp(DP_CTRL_STAT, &st) && (st & (STICKYERR | WDATAERR)))
    {
        g_diag_ctrl_stat = st;
        swd_write_dp(DP_ABORT, STKCMPCLR | STKERRCLR | WDERRCLR | ORUNERRCLR);
        return ERROR_ALGO_DL;
    }

    if (0 == swd_flash_syscall_exec(&algo->sys_call_s, algo->init, flash_start, 0, 0, 0))
    {
        return ERROR_INIT;
    }

    return ERROR_SUCCESS;
}

/* 结束Flash编程：执行算法uninit，把目标切回运行状态并释放SWD引脚。
 * algo - Flash算法描述，需提供uninit入口与系统调用表
 * 返回 - 固定返回ERROR_SUCCESS */
error_t target_flash_uninit(program_target_t *algo)
{
    swd_flash_syscall_exec(&algo->sys_call_s, algo->uninit, 0, 0, 0, 0);
    swd_set_target_state_hw(RESET_RUN);

    swd_off();
    return ERROR_SUCCESS;
}

/* 按算法缓冲区大小分块，把数据写入目标Flash。
 * algo - Flash算法描述，需提供program_buffer/program_buffer_size/program_page
 * addr - 目标Flash起始地址
 * buf  - 源数据缓冲区
 * size - 待写入字节数，超过缓冲区大小时自动分多次写入
 * 返回 - ERROR_SUCCESS=全部写入成功，ERROR_ALGO_DATA_SEQ=数据搬到SRAM失败，
 *        ERROR_WRITE=算法program_page返回非0 */
error_t target_flash_program_page(program_target_t *algo, uint32_t addr, const uint8_t *buf, uint32_t size)
{
    while (size > 0)
    {
        uint32_t write_size = size > algo->program_buffer_size ? algo->program_buffer_size : size;

        // 先把本页数据写入目标SRAM缓冲区
        if (!swd_write_memory(algo->program_buffer, (uint8_t *)buf, write_size))
        {
            return ERROR_ALGO_DATA_SEQ;
        }

        // 执行Flash编程
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

/* 擦除目标Flash的一个扇区。
 * algo - Flash算法描述，需提供erase_sector入口与系统调用表
 * addr - 要擦除的扇区地址（任意落在该扇区内的地址）
 * 返回 - ERROR_SUCCESS=成功，ERROR_ERASE_SECTOR=算法返回非0 */
error_t target_flash_erase_sector(program_target_t *algo, uint32_t addr)
{
    if (0 == swd_flash_syscall_exec(&algo->sys_call_s, algo->erase_sector, addr, 0, 0, 0))
    {
        return ERROR_ERASE_SECTOR;
    }

    return ERROR_SUCCESS;
}

uint32_t g_erase_diag;

/* 整片擦除目标Flash；失败时把算法返回的R0存入g_erase_diag供诊断。
 * algo - Flash算法描述，需提供erase_chip入口与系统调用表
 * 返回 - ERROR_SUCCESS=成功，ERROR_ERASE_ALL=算法返回非0 */
error_t target_flash_erase_chip(program_target_t *algo)
{
    error_t status = ERROR_SUCCESS;
    g_last_syscall_r0 = 0;

    if (0 == swd_flash_syscall_exec(&algo->sys_call_s, algo->erase_chip, 0, 0, 0, 0))
    {
        g_erase_diag = g_last_syscall_r0;
        return ERROR_ERASE_ALL;
    }
	
    return status;
}

/* 逐块读回并比对，校验目标Flash内容（不记录差异位置）。
 * algo     - Flash算法描述（本函数未使用其成员，保留以统一接口）
 * addr     - 目标Flash起始地址
 * expected - 期望数据缓冲区
 * size     - 校验字节数，内部按256字节分块
 * 返回     - ERROR_SUCCESS=完全一致，ERROR_VERIFY=读取失败或数据不一致 */
error_t target_flash_verify(program_target_t *algo, uint32_t addr, const uint8_t *expected, uint32_t size)
{
    uint8_t readback[256];
    uint32_t offset = 0;

    /* 清除粘滞错误标志，并强制重写CSW */
    swd_write_dp(DP_ABORT, DAPABORT | STKCMPCLR | STKERRCLR | WDERRCLR | ORUNERRCLR);
    swd_write_ap(AP_CSW, VFY_CSW_VAL | CSW_SIZE32);

    while (offset < size)
    {
        uint32_t chunk = (size - offset) > sizeof(readback) ? sizeof(readback) : (size - offset);

        if (!swd_read_memory(addr + offset, readback, chunk))
            return ERROR_VERIFY;

        if (memcmp(readback, expected + offset, chunk) != 0)
            return ERROR_VERIFY;

        offset += chunk;
    }

    return ERROR_SUCCESS;
}

uint32_t g_vfy_off = 0xFFFFFFFF;   /* 第一个不匹配的字节偏移, 0xFFFFFFFF = 读失败 */
uint32_t g_vfy_exp = 0xFFFFFFFF;   /* 期望值 */
uint32_t g_vfy_act = 0xFFFFFFFF;   /* 读回值 */

/* 逐块读回并比对，并记录第一处不一致的偏移与数据（扩展版校验）。
 * algo     - 扩展算法描述（本函数未使用其成员，保留以统一接口）
 * addr     - 目标Flash起始地址
 * expected - 期望数据缓冲区
 * size     - 校验字节数，内部按256字节分块
 * 返回     - ERROR_SUCCESS=完全一致，ERROR_VERIFY=读取失败或数据不一致
 *            差异写入g_vfy_off/g_vfy_exp/g_vfy_act；读不回来时偏移为0xFFFFFFFF */
error_t target_flash_verify_ext(program_target_ext_t *algo, uint32_t addr, const uint8_t *expected, uint32_t size)
{
    uint8_t readback[256];
    uint32_t offset = 0;

    /* 清除粘滞错误标志，并强制重写CSW */
    swd_write_dp(DP_ABORT, DAPABORT | STKCMPCLR | STKERRCLR | WDERRCLR | ORUNERRCLR);
    swd_write_ap(AP_CSW, VFY_CSW_VAL | CSW_SIZE32);

    while (offset < size)
    {
        uint32_t chunk = (size - offset) > sizeof(readback) ? sizeof(readback) : (size - offset);

        if (!swd_read_memory(addr + offset, readback, chunk))
        {
            /* 读都读不回来(OPT 区不可访问 / 调试口已锁) */
            g_vfy_off = 0xFFFFFFFF;
            g_vfy_exp = 0xEEEEEEEE;
            g_vfy_act = 0xEEEEEEEE;
            return ERROR_VERIFY;
        }

        if (memcmp(readback, expected + offset, chunk) != 0)
        {
            uint32_t k;
            for (k = 0; k < chunk; k++)
            {
                if (readback[k] != expected[offset + k])
                {
                    g_vfy_off = offset + k;
                    g_vfy_exp = expected[offset + k];
                    g_vfy_act = readback[k];
                    break;
                }
            }
            return ERROR_VERIFY;
        }

        offset += chunk;
    }

    return ERROR_SUCCESS;
}
