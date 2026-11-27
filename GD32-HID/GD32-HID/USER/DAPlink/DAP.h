/*
 * Copyright (c) 2013-2017 ARM Limited. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * ----------------------------------------------------------------------
 *
 * $Date:        1. December 2017
 * $Revision:    V2.0.0
 *
 * Project:      CMSIS-DAP Include
 * Title:        DAP.h Definitions
 *
 *---------------------------------------------------------------------------*/

#ifndef __DAP_H__
#define __DAP_H__


// DAP固件版本
#ifdef  DAP_FW_V1
#define DAP_FW_VER                      "1.2.0"
#else
#define DAP_FW_VER                      "2.0.0"
#endif

// DAP命令ID
#define ID_DAP_Info                     0x00U
#define ID_DAP_HostStatus               0x01U
#define ID_DAP_Connect                  0x02U
#define ID_DAP_Disconnect               0x03U
#define ID_DAP_TransferConfigure        0x04U
#define ID_DAP_Transfer                 0x05U
#define ID_DAP_TransferBlock            0x06U
#define ID_DAP_TransferAbort            0x07U
#define ID_DAP_WriteABORT               0x08U
#define ID_DAP_Delay                    0x09U
#define ID_DAP_ResetTarget              0x0AU
#define ID_DAP_SWJ_Pins                 0x10U
#define ID_DAP_SWJ_Clock                0x11U
#define ID_DAP_SWJ_Sequence             0x12U
#define ID_DAP_SWD_Configure            0x13U
#define ID_DAP_SWD_Sequence             0x1DU
#define ID_DAP_JTAG_Sequence            0x14U
#define ID_DAP_JTAG_Configure           0x15U
#define ID_DAP_JTAG_IDCODE              0x16U
#define ID_DAP_SWO_Transport            0x17U
#define ID_DAP_SWO_Mode                 0x18U
#define ID_DAP_SWO_Baudrate             0x19U
#define ID_DAP_SWO_Control              0x1AU
#define ID_DAP_SWO_Status               0x1BU
#define ID_DAP_SWO_ExtendedStatus       0x1EU
#define ID_DAP_SWO_Data                 0x1CU

#define ID_DAP_QueueCommands            0x7EU
#define ID_DAP_ExecuteCommands          0x7FU

// DAP厂商自定义命令ID
#define ID_DAP_Vendor0                  0x80U
#define ID_DAP_Vendor1                  0x81U
#define ID_DAP_Vendor2                  0x82U
#define ID_DAP_Vendor3                  0x83U
#define ID_DAP_Vendor4                  0x84U
#define ID_DAP_Vendor5                  0x85U
#define ID_DAP_Vendor6                  0x86U
#define ID_DAP_Vendor7                  0x87U
#define ID_DAP_Vendor8                  0x88U
#define ID_DAP_Vendor9                  0x89U
#define ID_DAP_Vendor10                 0x8AU
#define ID_DAP_Vendor11                 0x8BU
#define ID_DAP_Vendor12                 0x8CU
#define ID_DAP_Vendor13                 0x8DU
#define ID_DAP_Vendor14                 0x8EU
#define ID_DAP_Vendor15                 0x8FU
#define ID_DAP_Vendor16                 0x90U
#define ID_DAP_Vendor17                 0x91U
#define ID_DAP_Vendor18                 0x92U
#define ID_DAP_Vendor19                 0x93U
#define ID_DAP_Vendor20                 0x94U
#define ID_DAP_Vendor21                 0x95U
#define ID_DAP_Vendor22                 0x96U
#define ID_DAP_Vendor23                 0x97U
#define ID_DAP_Vendor24                 0x98U
#define ID_DAP_Vendor25                 0x99U
#define ID_DAP_Vendor26                 0x9AU
#define ID_DAP_Vendor27                 0x9BU
#define ID_DAP_Vendor28                 0x9CU
#define ID_DAP_Vendor29                 0x9DU
#define ID_DAP_Vendor30                 0x9EU
#define ID_DAP_Vendor31                 0x9FU

#define ID_DAP_Invalid                  0xFFU

// DAP状态码
#define DAP_OK                          0U
#define DAP_ERROR                       0xFFU

// DAP信息项ID（用于ID_DAP_Info命令）
#define DAP_ID_VENDOR                   1U
#define DAP_ID_PRODUCT                  2U
#define DAP_ID_SER_NUM                  3U
#define DAP_ID_FW_VER                   4U
#define DAP_ID_DEVICE_VENDOR            5U
#define DAP_ID_DEVICE_NAME              6U
#define DAP_ID_CAPABILITIES             0xF0U
#define DAP_ID_TIMESTAMP_CLOCK          0xF1U
#define DAP_ID_SWO_BUFFER_SIZE          0xFDU
#define DAP_ID_PACKET_COUNT             0xFEU
#define DAP_ID_PACKET_SIZE              0xFFU

// DAP主机状态
#define DAP_DEBUGGER_CONNECTED          0U
#define DAP_TARGET_RUNNING              1U

// DAP端口类型
#define DAP_PORT_AUTODETECT             0U      // 自动检测端口
#define DAP_PORT_DISABLED               0U      // 端口关闭（I/O引脚高阻）
#define DAP_PORT_SWD                    1U      // SWD端口（SWCLK、SWDIO）+ nRESET
#define DAP_PORT_JTAG                   2U      // JTAG端口（TCK、TMS、TDI、TDO、nTRST）+ nRESET

// SWJ引脚编号
#define DAP_SWJ_SWCLK_TCK               0       // SWCLK/TCK时钟
#define DAP_SWJ_SWDIO_TMS               1       // SWDIO/TMS数据
#define DAP_SWJ_TDI                     2       // TDI数据输入
#define DAP_SWJ_TDO                     3       // TDO数据输出
#define DAP_SWJ_nTRST                   5       // nTRST测试复位
#define DAP_SWJ_nRESET                  7       // nRESET目标复位

// DAP传输请求标志位
#define DAP_TRANSFER_APnDP              (1U<<0)
#define DAP_TRANSFER_RnW                (1U<<1)
#define DAP_TRANSFER_A2                 (1U<<2)
#define DAP_TRANSFER_A3                 (1U<<3)
#define DAP_TRANSFER_MATCH_VALUE        (1U<<4)
#define DAP_TRANSFER_MATCH_MASK         (1U<<5)
#define DAP_TRANSFER_TIMESTAMP          (1U<<7)

// DAP传输应答标志位
#define DAP_TRANSFER_OK                 (1U<<0)
#define DAP_TRANSFER_WAIT               (1U<<1)
#define DAP_TRANSFER_FAULT              (1U<<2)
#define DAP_TRANSFER_ERROR              (1U<<3)
#define DAP_TRANSFER_MISMATCH           (1U<<4)

// SWO跟踪模式
#define DAP_SWO_OFF                     0U
#define DAP_SWO_UART                    1U
#define DAP_SWO_MANCHESTER              2U

// SWO跟踪状态标志位
#define DAP_SWO_CAPTURE_ACTIVE          (1U<<0)
#define DAP_SWO_CAPTURE_PAUSED          (1U<<1)
#define DAP_SWO_STREAM_ERROR            (1U<<6)
#define DAP_SWO_BUFFER_OVERRUN          (1U<<7)


// 调试端口（DP）寄存器地址
#define DP_IDCODE                       0x00U   // IDCODE寄存器（SW只读）
#define DP_ABORT                        0x00U   // ABORT寄存器（SW只写）
#define DP_CTRL_STAT                    0x04U   // 控制与状态寄存器
#define DP_WCR                          0x04U   // 线控寄存器（仅SW）
#define DP_SELECT                       0x08U   // SELECT寄存器（JTAG可读写，SW只写）
#define DP_RESEND                       0x08U   // RESEND重发（SW只读）
#define DP_RDBUFF                       0x0CU   // RDBUFF读缓冲（只读）

// JTAG指令寄存器（IR）编码
#define JTAG_ABORT                      0x08U
#define JTAG_DPACC                      0x0AU
#define JTAG_APACC                      0x0BU
#define JTAG_IDCODE                     0x0EU
#define JTAG_BYPASS                     0x0FU

// JTAG时序序列信息字节
#define JTAG_SEQUENCE_TCK               0x3FU   // TCK时钟个数
#define JTAG_SEQUENCE_TMS               0x40U   // TMS电平值
#define JTAG_SEQUENCE_TDO               0x80U   // TDO采样使能

// SWD时序序列信息字节
#define SWD_SEQUENCE_CLK                0x3FU   // SWCLK时钟个数
#define SWD_SEQUENCE_DIN                0x80U   // SWDIO采样使能


#include <stddef.h>
#include <stdint.h>
#include "cmsis_compiler.h"

// DAP运行时数据结构
typedef struct {
  uint8_t     debug_port;                       // 调试端口类型
  uint8_t     fast_clock;                       // 快速时钟标志
  uint8_t     padding[2];
  uint32_t   clock_delay;                       // 时钟延时
  uint32_t     timestamp;                       // 最近一次捕获的时间戳
  struct {                                      // 传输配置
    uint8_t   idle_cycles;                      // 传输后插入的空闲周期数
    uint8_t    padding[3];
    uint16_t  retry_count;                      // 收到WAIT应答后的重试次数
    uint16_t  match_retry;                      // 读回值不匹配时的重试次数
    uint32_t  match_mask;                       // 匹配掩码
  } transfer;
#if (DAP_SWD != 0)
  struct {                                      // SWD配置
    uint8_t    turnaround;                      // 总线换向周期
    uint8_t    data_phase;                      // 始终产生数据相位
  } swd_conf;
#endif
#if (DAP_JTAG != 0)
  struct {                                      // JTAG器件链
    uint8_t   count;                            // 链上器件个数
    uint8_t   index;                            // 器件索引（TDO端器件索引为0）
#if (DAP_JTAG_DEV_CNT != 0)
    uint8_t   ir_length[DAP_JTAG_DEV_CNT];      // IR长度（位）
    uint16_t  ir_before[DAP_JTAG_DEV_CNT];      // IR之前的位数
    uint16_t  ir_after [DAP_JTAG_DEV_CNT];      // IR之后的位数
#endif
  } jtag_dev;
#endif
} DAP_Data_t;

extern          DAP_Data_t DAP_Data;            // DAP运行时数据
extern volatile uint8_t    DAP_TransferAbort;   // 传输中止标志


// 函数声明

/* 输出一段任意长度的SWJ时序（用于SWD/JTAG切换序列等）。
 * count - 要输出的位数
 * data  - 位数据缓冲区，按LSB优先逐位输出
 * 返回  - 无 */
extern void     SWJ_Sequence    (uint32_t count, const uint8_t *data);

/* 按SWD时序序列执行一段收发（对应ID_DAP_SWD_Sequence）。
 * info  - 序列信息字节：低6位为SWCLK个数，bit7为是否采样SWDIO
 * swdo  - 要输出的SWDIO位数据；不输出时可为NULL
 * swdi  - 输出参数，采样到的SWDIO位数据；不采样时可为NULL
 * 返回  - 无 */
extern void     SWD_Sequence    (uint32_t info,  const uint8_t *swdo, uint8_t *swdi);

/* 按JTAG时序序列执行一段收发（对应ID_DAP_JTAG_Sequence）。
 * info  - 序列信息字节：低6位为TCK个数，bit6为TMS电平，bit7为是否采样TDO
 * tdi   - 要输出的TDI位数据；不输出时可为NULL
 * tdo   - 输出参数，采样到的TDO位数据；不采样时可为NULL
 * 返回  - 无 */
extern void     JTAG_Sequence   (uint32_t info,  const uint8_t *tdi,  uint8_t *tdo);

/* 把目标JTAG链切换到IR（指令寄存器）状态并写入指令。
 * ir   - 要写入的IR值
 * 返回 - 无 */
extern void     JTAG_IR         (uint32_t ir);

/* 读取目标JTAG链的IDCODE（32位）。
 * 返回 - 读到的IDCODE；链上无器件时返回0或0xFFFFFFFF */
extern uint32_t JTAG_ReadIDCode (void);

/* 通过JTAG写目标的ABORT寄存器。
 * data - 要写入的32位值（各错误清除位）
 * 返回 - 无 */
extern void     JTAG_WriteAbort (uint32_t data);

/* 通过JTAG执行一次DP/AP寄存器传输。
 * request - 传输请求字节（APnDP、RnW、A2/A3等标志）
 * data    - 输入输出，写入时为待写数据，读取时为读回数据
 * 返回    - DAP_TRANSFER_OK / WAIT / FAULT / ERROR / MISMATCH */
extern uint8_t  JTAG_Transfer   (uint32_t request, uint32_t *data);

/* 通过SWD执行一次DP/AP寄存器传输（带重试与应答解析）。
 * request - 传输请求字节（APnDP、RnW、A2/A3等标志）
 * data    - 输入输出，写入时为待写数据，读取时为读回数据
 * 返回    - DAP_TRANSFER_OK / WAIT / FAULT / ERROR / MISMATCH */
extern uint8_t  SWD_Transfer    (uint32_t request, uint32_t *data);
extern uint8_t  SWD_TransferBlockData(uint32_t request, uint32_t *data);

/* 毫秒级延时。
 * delay - 延时长度，单位毫秒
 * 返回  - 无 */
extern void     Delayms         (uint32_t delay);

/* 设置SWO传输方式（对应ID_DAP_SWO_Transport）。
 * request  - 请求字节：request[1]为传输方式（OFF/UART/MANCHESTER）
 * response - 输出，response[0]为DAP_OK或DAP_ERROR
 * 返回     - 已写入response的字节数 */
extern uint32_t SWO_Transport      (const uint8_t *request, uint8_t *response);

/* 设置SWO跟踪模式（对应ID_DAP_SWO_Mode）。
 * request  - 请求字节：request[1]为模式（OFF/UART/MANCHESTER）
 * response - 输出，response[0]为DAP_OK或DAP_ERROR
 * 返回     - 已写入response的字节数 */
extern uint32_t SWO_Mode           (const uint8_t *request, uint8_t *response);

/* 设置SWO波特率（对应ID_DAP_SWO_Baudrate）。
 * request  - 请求字节：request[1..4]为小端32位波特率
 * response - 输出，response[0]为DAP_OK或DAP_ERROR
 * 返回     - 已写入response的字节数 */
extern uint32_t SWO_Baudrate       (const uint8_t *request, uint8_t *response);

/* 启动/停止SWO数据捕获（对应ID_DAP_SWO_Control）。
 * request  - 请求字节：request[1]=1启动捕获，0停止
 * response - 输出，response[0]为DAP_OK或DAP_ERROR
 * 返回     - 已写入response的字节数 */
extern uint32_t SWO_Control        (const uint8_t *request, uint8_t *response);

/* 查询SWO跟踪状态（对应ID_DAP_SWO_Status）。
 * response - 输出：response[0]=DAP_OK，response[1]为跟踪状态位，
 *            response[2..5]为小端32位已捕获字节数
 * 返回     - 已写入response的字节数 */
extern uint32_t SWO_Status                                 (uint8_t *response);

/* 查询SWO扩展状态（对应ID_DAP_SWO_ExtendedStatus）。
 * request  - 请求字节：request[1]为要查询的控制位
 * response - 输出：response[0]=DAP_OK，response[1]为跟踪状态位，
 *            response[2..5]为小端32位已捕获字节数
 * 返回     - 已写入response的字节数 */
extern uint32_t SWO_ExtendedStatus (const uint8_t *request, uint8_t *response);

/* 读取已捕获的SWO数据（对应ID_DAP_SWO_Data）。
 * request  - 请求字节：request[1..2]为小端16位请求字节数
 * response - 输出：response[0]=DAP_OK，response[1]为跟踪状态位，
 *            response[2..3]为实际返回字节数，其后为数据
 * 返回     - 已写入response的字节数 */
extern uint32_t SWO_Data           (const uint8_t *request, uint8_t *response);

/* 把一段SWO数据放入待发送队列。
 * buf  - 数据缓冲区
 * num  - 字节数
 * 返回 - 无 */
extern void     SWO_QueueTransfer    (uint8_t *buf, uint32_t num);

/* 中止当前SWO传输并复位队列。
 * 返回 - 无 */
extern void     SWO_AbortTransfer    (void);

/* 通知SWO传输已完成（USB端点发送结束回调）。
 * 返回 - 无 */
extern void     SWO_TransferComplete (void);

/* UART方式的SWO：设置/查询使能状态。
 * enable - 1=使能，0=关闭
 * 返回   - 当前使能状态 */
extern uint32_t UART_SWO_Mode     (uint32_t enable);

/* UART方式的SWO：设置波特率。
 * baudrate - 目标波特率
 * 返回     - 实际生效的波特率 */
extern uint32_t UART_SWO_Baudrate (uint32_t baudrate);

/* UART方式的SWO：启动/停止捕获。
 * active - 1=启动，0=停止
 * 返回   - 当前捕获状态 */
extern uint32_t UART_SWO_Control  (uint32_t active);

/* UART方式的SWO：把接收到的数据写入缓冲区。
 * buf  - 数据缓冲区
 * num  - 字节数
 * 返回 - 无 */
extern void     UART_SWO_Capture  (uint8_t *buf, uint32_t num);

/* UART方式的SWO：查询已捕获字节数。
 * 返回 - 已捕获的字节数 */
extern uint32_t UART_SWO_GetCount (void);

/* Manchester方式的SWO：设置/查询使能状态。
 * enable - 1=使能，0=关闭
 * 返回   - 当前使能状态 */
extern uint32_t Manchester_SWO_Mode     (uint32_t enable);

/* Manchester方式的SWO：设置波特率。
 * baudrate - 目标波特率
 * 返回     - 实际生效的波特率 */
extern uint32_t Manchester_SWO_Baudrate (uint32_t baudrate);

/* Manchester方式的SWO：启动/停止捕获。
 * active - 1=启动，0=停止
 * 返回   - 当前捕获状态 */
extern uint32_t Manchester_SWO_Control  (uint32_t active);

/* Manchester方式的SWO：把接收到的数据写入缓冲区。
 * buf  - 数据缓冲区
 * num  - 字节数
 * 返回 - 无 */
extern void     Manchester_SWO_Capture  (uint8_t *buf, uint32_t num);

/* Manchester方式的SWO：查询已捕获字节数。
 * 返回 - 已捕获的字节数 */
extern uint32_t Manchester_SWO_GetCount (void);

/* 处理厂商自定义命令（ID_DAP_Vendor0~31）。
 * request  - 请求包：request[0]为命令ID，其后为参数
 * response - 输出，response[0]为状态码
 * 返回     - 已写入response的字节数 */
extern uint32_t DAP_ProcessVendorCommand (const uint8_t *request, uint8_t *response);

/* 处理一条标准DAP命令。
 * request  - 请求包：request[0]为命令ID，其后为参数
 * response - 输出，response[0]为状态码，其后为返回数据
 * 返回     - 已写入response的字节数 */
extern uint32_t DAP_ProcessCommand       (const uint8_t *request, uint8_t *response);

/* 执行一个命令块（可含多条命令，对应ID_DAP_ExecuteCommands）。
 * request  - 请求包：request[0]为命令数，其后为各命令
 * response - 输出，依次写入各命令的返回数据
 * 返回     - 已写入response的字节数 */
extern uint32_t DAP_ExecuteCommand       (const uint8_t *request, uint8_t *response);

/* 初始化DAP运行时数据（端口、时钟延时、传输配置等）为默认值。
 * 返回 - 无 */
extern void     DAP_Setup (void);

// 时钟生成用的可配置延时
#ifndef DELAY_SLOW_CYCLES
#define DELAY_SLOW_CYCLES       3U      // 单次循环的周期数
#endif

/* 可配置的引脚延时循环，用于慢速时钟下的建立/保持时间。
 * delay - 循环次数，0或1时直接返回
 * 返回  - 无 */
__STATIC_FORCEINLINE void PIN_DELAY_SLOW (uint32_t delay) {
  uint32_t count;

  count = delay;
  while (--count) {
    /**
      * GCC会把空循环整个优化掉，这里插一条空汇编阻止优化。
      * 该做法不可移植，仅在本工程工具链下验证过。
      */
    __ASM("");
  }
}

// 快速时钟生成用的固定延时
#ifndef DELAY_FAST_CYCLES
#define DELAY_FAST_CYCLES       0U      // 周期数，取值0~3
#endif

/* 固定周期的引脚延时，用空操作凑出DELAY_FAST_CYCLES个周期。
 * 返回 - 无 */
__STATIC_FORCEINLINE void PIN_DELAY_FAST (void) {
#if (DELAY_FAST_CYCLES >= 1U)
  __nop();
#endif
#if (DELAY_FAST_CYCLES >= 2U)
  __nop();
#endif
#if (DELAY_FAST_CYCLES >= 3U)
  __nop();
#endif
}


#endif  /* __DAP_H__ */
