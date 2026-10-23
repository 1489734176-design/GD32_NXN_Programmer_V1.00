#ifndef __LCD_H
#define __LCD_H
#include <stdint.h>

/*显示方向：0竖屏240x320，1横屏320x240，2竖屏旋转180度，3横屏旋转180度*/
#define LCD_DIRECTION		0

#if ((LCD_DIRECTION == 0) || (LCD_DIRECTION == 2))
#define LCD_WIDTH			240
#define LCD_HEIGHT			320
#else
#define LCD_WIDTH			320
#define LCD_HEIGHT			240
#endif

/*常用颜色，RGB565格式*/
#define LCD_BLACK			0x0000
#define LCD_WHITE			0xFFFF
#define LCD_RED				0xF800
#define LCD_GREEN			0x07E0
#define LCD_BLUE			0x001F
#define LCD_YELLOW			0xFFE0
#define LCD_CYAN			0x07FF
#define LCD_MAGENTA			0xF81F
#define LCD_GRAY			0x8410

/*ASCII之外的字符，可直接拼在字符串里，如"m" LCD_STR_CUBE显示为"m³"*/
#define LCD_STR_CUBE		"\x7F"		//上标3

/*汉字固定为32x32点阵，与Size=2的字符等高，一个汉字占两个字符的宽度
  LCD_ShowString中按GBK编码识别汉字，所以含汉字字符串的.c文件须保存为GBK编码；字模见LCD_Chinese.h*/
#define LCD_CN_SIZE			32

void LCD_Init(void);
void LCD_Fill(uint16_t X, uint16_t Y, uint16_t Width, uint16_t Height, uint16_t Color);
void LCD_Clear(uint16_t Color);
void LCD_DrawPoint(uint16_t X, uint16_t Y, uint16_t Color);
void LCD_ShowChar(uint16_t X, uint16_t Y, char Char, uint16_t Color, uint16_t BackColor, uint8_t Size);
void LCD_ShowChinese(uint16_t X, uint16_t Y, uint16_t Code, uint16_t Color, uint16_t BackColor);
void LCD_ShowString(uint16_t X, uint16_t Y, char *String, uint16_t Color, uint16_t BackColor, uint8_t Size);
void LCD_ShowNum(uint16_t X, uint16_t Y, uint32_t Number, uint8_t Length, uint16_t Color, uint16_t BackColor, uint8_t Size);

#endif
