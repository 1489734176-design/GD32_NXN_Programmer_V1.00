#ifndef _BSP_OLED_H__
#define _BSP_OLED_H__
#include "main.h"
typedef struct { uint8_t oled_Read_addr, oled_Write_addr, Refresh_flag; } oled_TypeDef;
extern oled_TypeDef oled;
void OLED_Init(void);
void OLED_WR_Byte(uint8_t dat, uint8_t mode);
void OLED_DisPlay_On(void);
void OLED_DisPlay_Off(void);
void OLED_Clear(void);
void OLED_Refresh(void);
void OLED_ColorTurn(uint8_t invert);
void OLED_DisplayTurn(uint8_t normal);
/* All UI_* coordinates are physical pixels: x = column, y = row. */
uint16_t UI_TextWidth(const char *s, uint8_t size);
const char *UI_TextLine(uint16_t x, uint16_t y, uint16_t width, const char *s, uint8_t size, uint8_t mode);
void UI_Text(uint16_t x, uint16_t y, const char *s, uint8_t size, uint8_t mode);
void UI_TextBox(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const char *s, uint8_t size, uint8_t mode);
void UI_Fill(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t on);
void UI_Logo(uint16_t x, uint16_t y, uint8_t index);
void UI_BootLogo(const uint8_t *data);
void UI_Progress(uint16_t y, uint8_t percent);
/* Optional online graph compatibility only. */
void OLED_DrawPoint(uint8_t row, uint8_t col, uint8_t on);
void OLED_DrawLine(uint8_t r1, uint8_t c1, uint8_t r2, uint8_t c2, uint8_t on);
void OLED_ShowString(uint8_t row, uint8_t col, uint8_t *s, uint8_t size, uint8_t mode);
#endif
