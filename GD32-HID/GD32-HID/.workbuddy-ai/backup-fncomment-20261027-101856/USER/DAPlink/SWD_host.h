/*
 * @Descripttion : SWD主机驱动对外接口
 * @version      : 
 * @Author       : Kevincoooool
 * @Date         : 2020-08-22 17:55:57
 * @LastEditors  : Kevincoooool
 * @LastEditTime : 2020-11-07 10:21:30
 * @FilePath     : \Simple_TeenyUSB_TX\CMSIS-DAP\Include\SWD_host.h
 */
#ifndef SWDHOST_CM_H
#define SWDHOST_CM_H

#include <stdint.h>

#include "../algo/flash_blob.h"


/* 目标状态机：交给swd_set_target_state_hw/ sw决定目标处于什么状态 */
typedef enum
{
    RESET_HOLD,              // 拉住复位，目标停在复位态
    RESET_PROGRAM,           // 复位后停在起点，准备烧录
    RESET_RUN,               // 复位后正常跑起来
    NO_DEBUG,                // 关掉调试，目标继续运行
    DEBUG,                   // 目标运行中打开调试
    HALT,                    // 不复位，直接暂停目标
    RUN                      // 不复位，让目标继续运行
} TARGET_RESET_STATE;


/* 初始化SWD主机引脚与DAP内部状态。返回1 */
uint8_t swd_init(void);
/* 释放SWD引脚，把SWDIO/SWCLK交还目标。返回1 */
uint8_t swd_off(void);
/* 建立调试连接：切SWD、清错误、调试端口上电。返回1=成功 */
uint8_t swd_init_debug(void);
/* 使能调试并暂停内核。返回1=已暂停 */
uint8_t swd_halt_core(void);
/* 读DP寄存器：adr=寄存器地址，val=输出值。返回1=成功 */
uint8_t swd_read_dp(uint8_t adr, uint32_t *val);
/* 写DP寄存器：adr=寄存器地址，val=待写值。返回1=成功 */
uint8_t swd_write_dp(uint8_t adr, uint32_t val);
/* 读AP寄存器：adr=AP地址（含APSEL/BANKSEL），val=输出值。返回1=成功 */
uint8_t swd_read_ap(uint32_t adr, uint32_t *val);
/* 写AP寄存器：adr=AP地址（含APSEL/BANKSEL），val=待写值。返回1=成功 */
uint8_t swd_write_ap(uint32_t adr, uint32_t val);
/* 读目标内存：address=起始地址（可不对齐），data=接收缓冲区，size=字节数。返回1=成功 */
uint8_t swd_read_memory(uint32_t address, uint8_t *data, uint32_t size);
/* 写目标内存：address=起始地址（可不对齐），data=源缓冲区，size=字节数。返回1=成功 */
uint8_t swd_write_memory(uint32_t address, uint8_t *data, uint32_t size);
extern uint32_t g_last_syscall_r0;      // 最近一次算法系统调用的返回码/错误码
extern uint32_t g_diag_ctrl_stat;       // 失败时抓到的DP CTRL/STAT快照
extern uint32_t g_diag_hcsr;            // 失败时抓到的DHCSR快照
extern uint32_t g_diag_step;            // 失败发生在哪个子步骤
/* 在目标上执行Flash算法系统调用。
 * sysCallParam=算法描述表，entry=算法函数入口，arg1~arg4=对应R0~R3的入参。返回1=算法返回0 */
uint8_t swd_flash_syscall_exec(const program_syscall_t *sysCallParam, uint32_t entry, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4);
/* 毫秒级忙等延时：ms=延时长度（毫秒） */
void delaymS(uint32_t ms);
/* 控制nRESET：asserted=1拉低（保持复位），0释放并补发软复位 */
void swd_set_target_reset(uint8_t asserted);
/* 用硬件复位把目标切到state指定的状态。返回1=成功 */
uint8_t swd_set_target_state_hw(TARGET_RESET_STATE state);
/* 用软件复位（AIRCR.SYSRESETREQ）把目标切到state指定的状态。返回1=成功 */
uint8_t swd_set_target_state_sw(TARGET_RESET_STATE state);
void vResetTarget(uint8_t bit);

#endif
