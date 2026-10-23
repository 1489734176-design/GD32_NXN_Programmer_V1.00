#ifndef BSP_SPI0_BUS_H
#define BSP_SPI0_BUS_H

#include "gd32f10x.h"

/* Both devices use SPI0 mode 0, MSB first, 8 bits. */
void SPI0_BusInit(void);
void SPI0_FlashSelect(void);
void SPI0_FlashRelease(void);
uint8_t SPI0_FlashExchange(uint8_t data);
void SPI0_LCDSelect(void);
void SPI0_LCDRelease(void);
void SPI0_LCDWrite(uint8_t data);
void SPI0_BusWait(void);
uint8_t SPI0_BusOK(void);

#endif
