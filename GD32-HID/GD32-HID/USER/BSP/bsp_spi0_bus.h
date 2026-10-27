/* 中文注释补充（2026-10-26）：LCD与外部Flash共享SPI0的事务接口；禁止两者同时选中，LCD分块发送后释放总线。 */
#ifndef BSP_SPI0_BUS_H
#define BSP_SPI0_BUS_H

#include "gd32f10x.h"

/* LCD 与外部 Flash 共用硬件 SPI0：模式 0、8 位、高位在前。
 * Select/Release 必须成对使用且不可嵌套；片选期间屏蔽中断，禁止延时。
 * LCD 使用重映射单线发送，Flash 使用默认映射全双工，释放后恢复 Flash。
 */
/* 幂等初始化：首次启用GPIO/AFIO/SPI0时钟并默认选用Flash配置。
 * 返回 - 无（返回时恢复调用前的中断状态） */
void SPI0_BusInit(void);
/* 开始Flash事务：进入临界区并拉低PA4片选，须与SPI0_FlashRelease配对。
 * 返回 - 无 */
void SPI0_FlashSelect(void);
/* 结束Flash事务：等最后一字节发完、拉高片选并恢复中断状态。
 * 返回 - 无 */
void SPI0_FlashRelease(void);
/* 在Flash事务中全双工收发一个字节。
 * data - 要发送的字节
 * 返回 - 同时收到的字节；等待超时返回0xFF并记录故障 */
uint8_t SPI0_FlashExchange(uint8_t data);
/* 开始LCD事务：进入临界区、切换到LCD映射并拉低PB8片选。
 * 返回 - 无（须与SPI0_LCDRelease配对） */
void SPI0_LCDSelect(void);
/* 结束LCD事务：等发送完成、拉高片选、恢复Flash配置与中断状态。
 * 返回 - 无 */
void SPI0_LCDRelease(void);
/* 在LCD事务中发送一个字节，不等该字节在线上完全发送结束。
 * data - 要发送的字节
 * 返回 - 无 */
void SPI0_LCDWrite(uint8_t data);
/* 有限轮询等待发送缓冲区空且传输结束，用于DC/片选切换前。
 * 返回 - 无（超时则记录故障并关闭SPI0） */
void SPI0_BusWait(void);
/* 查询总线故障标志，供上层分块绘制及时停止输出。
 * 返回 - 1=总线正常，0=曾发生轮询超时 */
uint8_t SPI0_BusOK(void);

#endif
