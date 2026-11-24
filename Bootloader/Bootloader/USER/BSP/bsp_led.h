#ifndef _BSP_LED_H__
#define _BSP_LED_H__

#include "main.h"

#define CDC_LED_ON ((void)0)
#define CDC_LED_OFF ((void)0)
#define CDC_LED_TOG     
#define MSC_LED_ON    	gpio_bit_reset(MSC_LED_GPIO_Port, MSC_LED_Pin)
#define MSC_LED_OFF     gpio_bit_set(MSC_LED_GPIO_Port, MSC_LED_Pin)
#define MSC_LED_TOG     
#define HID_LED_ON    	gpio_bit_reset(HID_LED_GPIO_Port, HID_LED_Pin)
#define HID_LED_OFF     gpio_bit_set(HID_LED_GPIO_Port, HID_LED_Pin)
#define HID_LED_TOG     

void led_init(void);


#endif

