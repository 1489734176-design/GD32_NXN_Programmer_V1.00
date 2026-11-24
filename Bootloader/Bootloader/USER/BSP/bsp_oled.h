#ifndef _BSP_OLED_H__
#define _BSP_OLED_H__

#include "main.h"
#include "gd32f10x_i2c.h"

#define OLED_CMD  0	//写命令
#define OLED_DATA 1	//写数据

extern uint8_t OLED_GRAM[8][128];

typedef struct
{
    uint8_t oled_Read_addr;
    uint8_t oled_Write_addr;
		uint8_t Refresh_flag;

}oled_TypeDef;



extern oled_TypeDef oled;

void OLED_Init(void);
void OLED_ClearPoint(uint8_t x,uint8_t y);
void OLED_WR_Byte(uint8_t dat,uint8_t mode);
void OLED_DisplayTurn(uint8_t i);
void OLED_DisPlay_On(void);
void OLED_DisPlay_Off(void);
void OLED_Clear(void);
void OLED_Refresh(void);
void OLED_DrawPoint(uint8_t x,uint8_t y,uint8_t t);
void OLED_DrawLine(uint8_t x1,uint8_t y1,uint8_t x2,uint8_t y2,uint8_t mode);
void OLED_DrawCircle(uint8_t x,uint8_t y,uint8_t r);
void OLED_ScrollDisplay(uint8_t num,uint8_t space,uint8_t mode);
void OLED_ShowPicture(uint8_t x,uint8_t y,uint8_t sizex,uint8_t sizey,uint8_t BMP[],uint8_t mode);
void OLED_ShowChar(uint8_t x,uint8_t y,uint8_t chr,uint8_t size1,uint8_t mode);
void OLED_ShowString(uint8_t x,uint8_t y,uint8_t *chr,uint8_t size1,uint8_t mode);
void OLED_ShowNum(uint8_t x,uint8_t y,uint32_t num,uint8_t len,uint8_t size1,uint8_t mode);

void OLED_ShowChinese(uint8_t x, uint8_t y, uint8_t index, uint8_t len, uint8_t mode);


#endif

