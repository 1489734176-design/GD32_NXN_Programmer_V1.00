/* 中文注释补充（2026-10-26）：显示接口：新UI使用320×240屏幕像素，中文16/32点，ASCII半宽；直接分块绘制，不维护整屏缓存。 */
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
/* 页面改变才清屏；字段缓存逐字更新，不使用大帧缓冲。 */
void UI_EnterPage(uint8_t page);
void UI_Field(uint8_t id, uint16_t x, uint16_t y, uint16_t width, uint16_t height,
              const char *text, uint8_t size, uint8_t mode);
/* 原生LCD直接绘图，函数返回时绘制已完成，保留旧接口兼容调用。 */
void OLED_Refresh(void);
void OLED_ColorTurn(uint8_t invert);
void OLED_DisplayTurn(uint8_t normal);
/* UI_*系列全部使用物理像素坐标：x为列、y为行。
 * 混合汉字/ASCII文本宽度：汉字占size像素，ASCII占size/2像素。 */
uint16_t UI_TextWidth(const char *s, uint8_t size);
/* 在一行限定宽度内显示文本，返回第一个未显示字符；16点字模可整数放大为32点。 */
const char *UI_TextLine(uint16_t x, uint16_t y, uint16_t width, const char *s, uint8_t size, uint8_t mode);
/* 显示单行文本：x为横坐标、y为纵坐标，size为16或32像素，mode=1白字/0黑字。 */
void UI_Text(uint16_t x, uint16_t y, const char *s, uint8_t size, uint8_t mode);
/* 按指定矩形换行显示文字；逐个完整字符换行，超出高度后停止，先清理旧文字背景。 */
void UI_TextBox(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const char *s, uint8_t size, uint8_t mode);
/* 按屏幕像素填充矩形；on=1白色、on=0黑色，不申请整屏显存。 */
void UI_Fill(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t on);
/* 显示状态小图标；启动程序精简编译时不使用应用图标表。 */
void UI_Logo(uint16_t x, uint16_t y, uint8_t index);
/* 将128×64单色开机图标放大两倍显示在320×240屏幕。 */
void UI_BootLogo(const uint8_t *data);
/* 绘制百分比进度条和数字；限制0～100%，使用局部绘图避免全屏刷新。 */
void UI_Progress(uint16_t y, uint8_t percent);
/* 仅为兼容旧的在线图表接口。 */
void OLED_DrawPoint(uint8_t row, uint8_t col, uint8_t on);
void OLED_DrawLine(uint8_t r1, uint8_t c1, uint8_t r2, uint8_t c2, uint8_t on);
/* 兼容旧在线图表的行列坐标接口；新的离线界面使用UI_Text原生像素接口。 */
void OLED_ShowString(uint8_t row, uint8_t col, uint8_t *s, uint8_t size, uint8_t mode);
#endif
