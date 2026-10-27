#ifndef __BSP_PWR_H__
#define __BSP_PWR_H__

#include "gd32f10x.h"

#define	POWER_OFF	gpio_bit_reset(PWR_EN_GPIO_Port, PWR_EN_Pin)
#define	POWER_ON	gpio_bit_set(PWR_EN_GPIO_Port, PWR_EN_Pin)

/* 初始化电源相关引脚：充电/充满检测、USB供电使能、目标板供电控制。
 * 返回 - 无 */
void pwr_init(void);
/* 读取电池/充电状态，两个检测脚均为低电平有效。
 * 返回 - 0=未接充电器，1=正在充电，2=已充满 */
uint8_t bat_status_get(void);


#endif



