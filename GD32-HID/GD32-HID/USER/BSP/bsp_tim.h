#ifndef __BSP_TIM_H__
#define __BSP_TIM_H__

#include "main.h"
#include "gd32f10x_timer.h"

typedef struct
{
	uint8_t time_start_flag;
}time_TypeDef;

extern time_TypeDef time;

/* 配置TIMER0为1ms更新中断，用于CDC串口发送节流（初始化后定时器保持关闭）。
 * 返回 - 无 */
void time_init(void);


#endif


