#include "bsp_led.h"
#include "main.h"

/* 初始化HID/MSC/CDC三个指示灯的GPIO为推挽输出，并全部熄灭。
 * CDC_LED已飞线到PA9，因此额外使能GPIOA时钟。
 * 返回 - 无 */
void led_init(void)
{
	rcu_periph_clock_enable(RCU_GPIOB);
	rcu_periph_clock_enable(RCU_GPIOA);   /* CDC_LED 已改到 PA9 */
	gpio_init(HID_LED_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, HID_LED_Pin);
	gpio_init(MSC_LED_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, MSC_LED_Pin);
	gpio_init(CDC_LED_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, CDC_LED_Pin);
	
	HID_LED_OFF;
	MSC_LED_OFF;
	CDC_LED_OFF;
}

