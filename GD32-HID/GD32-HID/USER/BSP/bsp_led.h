#ifndef _BSP_LED_H__
#define _BSP_LED_H__

#include "main.h"

#define CDC_LED_ON    	gpio_bit_reset(CDC_LED_GPIO_Port, CDC_LED_Pin)
#define CDC_LED_OFF     gpio_bit_set(CDC_LED_GPIO_Port, CDC_LED_Pin)
#define CDC_LED_TOG     

/* 1 = 上电时让 CDC_LED(PC5/LED3) 闪 3 下，用作上电自检；
   不需要时把这里改成 0（或注释掉）即可 */
#ifndef CDC_LED_SELFTEST
#define CDC_LED_SELFTEST   1
#endif
#define MSC_LED_ON    	gpio_bit_reset(MSC_LED_GPIO_Port, MSC_LED_Pin)
#define MSC_LED_OFF     gpio_bit_set(MSC_LED_GPIO_Port, MSC_LED_Pin)
#define MSC_LED_TOG     
#define HID_LED_ON    	gpio_bit_reset(HID_LED_GPIO_Port, HID_LED_Pin)
#define HID_LED_OFF     gpio_bit_set(HID_LED_GPIO_Port, HID_LED_Pin)
#define HID_LED_TOG     

/* 初始化HID/MSC/CDC三个指示灯的GPIO为推挽输出并全部熄灭。
 * 返回 - 无 */
void led_init(void);


#endif

