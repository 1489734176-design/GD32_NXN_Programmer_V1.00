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

/* 初始化LCD：配置DC/CS/RES控制引脚与SPI1，复位后下发ST7789初始化命令。
 * 返回 - 无 */
void LCD_Init(void);
/* 填充矩形区域，超出屏幕的部分自动裁掉。
 * X      - 左上角列坐标
 * Y      - 左上角行坐标
 * Width  - 宽度（像素）
 * Height - 高度（像素）
 * Color  - RGB565颜色值
 * 返回   - 无 */
void LCD_Fill(uint16_t X, uint16_t Y, uint16_t Width, uint16_t Height, uint16_t Color);
/* 全屏填充一种颜色。
 * Color - RGB565颜色值
 * 返回  - 无 */
void LCD_Clear(uint16_t Color);
/* 画一个像素点。
 * X     - 列坐标
 * Y     - 行坐标
 * Color - RGB565颜色值
 * 返回  - 无 */
void LCD_DrawPoint(uint16_t X, uint16_t Y, uint16_t Color);
/* 显示8×16 ASCII字符，按Size整数倍放大。
 * X         - 左上角列坐标
 * Y         - 左上角行坐标
 * Char      - 字符，0x20~0x7F；0x7F为上标3
 * Color     - 字的颜色，RGB565
 * BackColor - 背景色，RGB565
 * Size      - 放大倍数，0按1处理
 * 返回      - 无 */
void LCD_ShowChar(uint16_t X, uint16_t Y, char Char, uint16_t Color, uint16_t BackColor, uint8_t Size);
/* 显示一个32×32点阵汉字，字库中没有的汉字填背景色。
 * X         - 左上角列坐标
 * Y         - 左上角行坐标
 * Code      - 汉字的GBK内码，如“长”为0xB3A4
 * Color     - 字的颜色，RGB565
 * BackColor - 背景色，RGB565
 * 返回      - 无 */
void LCD_ShowChinese(uint16_t X, uint16_t Y, uint16_t Code, uint16_t Color, uint16_t BackColor);
/* 显示字符串，超出屏幕右边的部分不显示。
 * X         - 起始列坐标
 * Y         - 起始行坐标
 * String    - 以空字符结尾的GBK字符串
 * Color     - 字的颜色，RGB565
 * BackColor - 背景色，RGB565
 * Size      - ASCII放大倍数，0按1处理
 * 返回      - 无 */
void LCD_ShowString(uint16_t X, uint16_t Y, char *String, uint16_t Color, uint16_t BackColor, uint8_t Size);
/* 显示十进制正整数，位数不足时前面补0。
 * X         - 起始列坐标
 * Y         - 起始行坐标
 * Number    - 要显示的数字
 * Length    - 显示位数1~10，超过10按10处理
 * Color     - 字的颜色，RGB565
 * BackColor - 背景色，RGB565
 * Size      - ASCII放大倍数，0按1处理
 * 返回      - 无 */
void LCD_ShowNum(uint16_t X, uint16_t Y, uint32_t Number, uint8_t Length, uint16_t Color, uint16_t BackColor, uint8_t Size);

#endif
