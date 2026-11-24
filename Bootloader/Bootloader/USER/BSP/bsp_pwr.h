#ifndef __BSP_PWR_H__
#define __BSP_PWR_H__

#include "gd32f10x.h"

#define	POWER_OFF	gpio_bit_reset(PWR_EN_GPIO_Port, PWR_EN_Pin)
#define	POWER_ON	gpio_bit_set(PWR_EN_GPIO_Port, PWR_EN_Pin)

void pwr_init(void);
uint8_t bat_status_get(void);


#endif



