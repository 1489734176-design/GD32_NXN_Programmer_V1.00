#ifndef _BSP_LCD_H__
#define _BSP_LCD_H__

#include "gd32f10x.h"

/* HS20S010B / ST7789V2, hardware SPI0 remapped to PB3/PB5.
 * Flash keeps PA4/PA5/PA6/PA7. bsp_spi0_bus serializes both devices.
 * Define LCD_BAREMETAL for the Bootloader; it does not touch PA9.
 */
#define USE_LCD 1
#define LCD_SCL_Pin GPIO_PIN_3
#define LCD_SDA_Pin GPIO_PIN_5
#define LCD_RES_Pin GPIO_PIN_15
#define LCD_DC_Pin GPIO_PIN_9
#define LCD_CS_Pin GPIO_PIN_8
#define LCD_BLK_Pin GPIO_PIN_10
#define LCD_BLK_GPIO_Port GPIOA
#define LCD_BLK_ACTIVE_HIGH 1

/* 0: portrait, 1: landscape, 2/3: corresponding 180-degree rotation. */
#ifndef LCD_DIRECTION
#define LCD_DIRECTION 1
#endif
#if LCD_DIRECTION == 0 || LCD_DIRECTION == 2
#define LCD_WIDTH 240
#define LCD_HEIGHT 320
#define LCD_UI_SCALE 1
#else
#define LCD_WIDTH 320
#define LCD_HEIGHT 240
#define LCD_UI_SCALE 2
#endif
#define LCD_UI_X ((LCD_WIDTH - 128 * LCD_UI_SCALE) / 2)
#define LCD_UI_Y ((LCD_HEIGHT - 64 * LCD_UI_SCALE) / 2)
#define LCD_BLACK 0x0000
#define LCD_WHITE 0xFFFF
#define LCD_RED 0xF800
#define LCD_GREEN 0x07E0
#define LCD_BLUE 0x001F
#define LCD_YELLOW 0xFFE0
#define LCD_CYAN 0x07FF
#define LCD_MAGENTA 0xF81F
#define LCD_GRAY 0x8410
#define LCD_CN_SIZE 32

void LCD_Init(void);
void LCD_DisplayOn(void);
void LCD_DisplayOff(void);
void LCD_BackLight(uint8_t on);
void LCD_Fill(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color);
void LCD_Clear(uint16_t color);
void LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color);
void LCD_BlitGRAM(const uint8_t gram[8][128]);
void LCD_SetInvert(uint8_t invert);
void LCD_SetFlip(uint8_t flip);
void LCD_ShowChar(uint16_t x, uint16_t y, char ch, uint16_t color, uint16_t back, uint8_t size);
void LCD_ShowString(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t back, uint8_t size);
void LCD_ShowNum(uint16_t x, uint16_t y, uint32_t number, uint8_t length, uint16_t color, uint16_t back, uint8_t size);
#endif
