/* 中文注释补充（2026-10-26）：原生屏幕布局：默认横屏320×240；LCD_BlitMono按小块直接画字，不分配整屏显存。 */
#ifndef _BSP_LCD_H__
#define _BSP_LCD_H__

#include "gd32f10x.h"

/* HS20S010B / ST7789V2 液晶屏移植接口。
 * LCD 使用硬件 SPI0 重映射引脚 PB3(SCK)、PB5(MOSI)，仅发送数据。
 * 外部 Flash 使用 PA4/PA5/PA6/PA7，由 bsp_spi0_bus 统一切换与保护总线。
 * 应用和 Bootloader 共用本驱动；Bootloader 定义 LCD_BAREMETAL，采用裸机延时，
 * 不编译直接字符显示函数。本驱动不操作 PA9。
 */
/* LCD 控制引脚：SCL/SDA/RES/DC/CS 位于 GPIOB，BLK 位于 GPIOA。 */
#define USE_LCD 1
#define LCD_SCL_Pin GPIO_PIN_3 /* PB3：SPI0 时钟 */
#define LCD_SDA_Pin GPIO_PIN_5 /* PB5：SPI0 数据输出 */
#define LCD_RES_Pin GPIO_PIN_15 /* PB15：硬件复位，低电平有效 */
#define LCD_DC_Pin GPIO_PIN_9 /* PB9：0 为命令，1 为数据 */
#define LCD_CS_Pin GPIO_PIN_8 /* PB8：片选，低电平有效 */
#define LCD_BLK_Pin GPIO_PIN_10 /* PA10：背光控制 */
#define LCD_BLK_GPIO_Port GPIOA
#define LCD_BLK_ACTIVE_HIGH 1 /* 1：高电平点亮背光；0：低电平点亮 */

/* 显示方向：0 为竖屏，1 为横屏，2/3 分别为竖屏/横屏旋转 180 度。
 * 可在工程预定义 LCD_DIRECTION；默认横屏 320x240。
 */
#ifndef LCD_DIRECTION
#define LCD_DIRECTION 1
#endif
/* Native screen geometry; the UI uses 320x240 landscape pixel coordinates. */
#if LCD_DIRECTION == 0 || LCD_DIRECTION == 2
#define LCD_WIDTH 240
#define LCD_HEIGHT 320
#else
#define LCD_WIDTH 320
#define LCD_HEIGHT 240
#endif
/* 常用颜色：RGB565 格式，红 5 位、绿 6 位、蓝 5 位。 */
#define LCD_BLACK 0x0000
#define LCD_WHITE 0xFFFF
#define LCD_RED 0xF800
#define LCD_GREEN 0x07E0
#define LCD_BLUE 0x001F
#define LCD_YELLOW 0xFFE0
#define LCD_CYAN 0x07FF
#define LCD_MAGENTA 0xF81F
#define LCD_GRAY 0x8410
#define LCD_CN_SIZE 32 /* 直接显示汉字时使用 32x32 点阵 */

/* 初始化共用 SPI0、控制引脚与 ST7789；清为黑屏后开启显示和背光。 */
void LCD_Init(void);
/* Native monochrome drawing, no framebuffer; stride is measured in bytes. */
/* 将单色点阵按整数倍率分块写入LCD；支持汉字行式点阵与ASCII/图标列式点阵，限制SPI占用长度。 */
void LCD_BlitMono(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                  uint16_t stride, const uint8_t *bitmap, uint8_t column_pages,
                  uint8_t scale, uint8_t mode);
/* 开显示同时点亮背光；关显示先关闭背光。 */
void LCD_DisplayOn(void);
void LCD_DisplayOff(void);
/* on 非零点亮背光，为零关闭；电平极性由 LCD_BLK_ACTIVE_HIGH 指定。 */
void LCD_BackLight(uint8_t on);
/* 矩形左上角为 (x,y)，宽高单位为像素；越界部分裁剪，color 为 RGB565。 */
void LCD_Fill(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color);
/* 全屏填充与单像素绘制。 */
void LCD_Clear(uint16_t color);
void LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color);
/* 单色字模反显和硬件 180 度翻转设置。 */
void LCD_SetInvert(uint8_t invert);
void LCD_SetFlip(uint8_t flip);
/* 以下直接字符接口仅在未定义 LCD_BAREMETAL 的应用中实现。
 * 坐标为字符左上角，color/back 分别为前景/背景 RGB565 颜色。
 * ASCII 使用 8x16 字模，size 为 1~4 倍，0 按 1 倍处理；0x7F 为上标 3。
 */
void LCD_ShowChar(uint16_t x, uint16_t y, char ch, uint16_t color, uint16_t back, uint8_t size);
/* 支持 ASCII 与 GBK 双字节汉字混排；汉字固定 32x32，不随 size 缩放。
 * 含汉字的字符串须使用 GBK，且对应字模须存在于 LCD_Chinese.h。
 */
void LCD_ShowString(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t back, uint8_t size);
/* 十进制数字显示：length 最多 10 位，不足补前导零，超出位数保留低位。 */
void LCD_ShowNum(uint16_t x, uint16_t y, uint32_t number, uint8_t length, uint16_t color, uint16_t back, uint8_t size);
#endif
