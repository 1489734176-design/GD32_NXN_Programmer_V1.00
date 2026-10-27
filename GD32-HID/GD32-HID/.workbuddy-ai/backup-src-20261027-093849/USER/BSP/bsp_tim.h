#ifndef __BSP_TIM_H__
#define __BSP_TIM_H__

#include "main.h"
#include "gd32f10x_timer.h"

typedef struct
{
	uint8_t time_start_flag;
}time_TypeDef;

extern time_TypeDef time;

void time_init(void);


#endif


