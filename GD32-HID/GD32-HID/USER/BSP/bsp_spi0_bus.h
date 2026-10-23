#ifndef BSP_SPI0_BUS_H
#define BSP_SPI0_BUS_H

#include "gd32f10x.h"

/* LCD 与外部 Flash 共用硬件 SPI0：模式 0、8 位、高位在前。
 * Select/Release 必须成对使用且不可嵌套；片选期间屏蔽中断，禁止延时。
 * LCD 使用重映射单线发送，Flash 使用默认映射全双工，释放后恢复 Flash。
 */
/* 仅首次调用配置时钟和引脚；保留调用前的中断状态。 */
void SPI0_BusInit(void);
/* 开始/结束 Flash 事务：PA4 为片选，PA5/PA6/PA7 为 SCK/MISO/MOSI。 */
void SPI0_FlashSelect(void);
void SPI0_FlashRelease(void);
/* 在 Flash 事务中收发一个字节；等待超时返回 0xFF 并记录故障。 */
uint8_t SPI0_FlashExchange(uint8_t data);
/* 开始/结束 LCD 事务：PB8 为片选，PB3/PB5 为 SCK/MOSI。 */
void SPI0_LCDSelect(void);
void SPI0_LCDRelease(void);
/* 在 LCD 事务中发送一个字节，不等该字节在线上完全发送结束。 */
void SPI0_LCDWrite(uint8_t data);
/* 有限轮询等待发送缓冲区空且传输结束，用于 DC/片选切换前。 */
void SPI0_BusWait(void);
/* 非零表示尚未记录总线超时；下一次 Select 可重新配置并清除故障。 */
uint8_t SPI0_BusOK(void);

#endif
