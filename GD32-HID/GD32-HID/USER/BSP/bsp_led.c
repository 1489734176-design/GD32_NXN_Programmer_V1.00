#include "bsp_led.h"
#include "main.h"

/* 初始化LED1/LED2/LED3（HID/MSC/CDC）为推挽输出，并全部熄灭。
 * 新板三灯分别位于 PA0 / PC4 / PC5。
 * 返回 - 无 */
void led_init(void)
{
	rcu_periph_clock_enable(RCU_GPIOA);
	rcu_periph_clock_enable(RCU_GPIOC);
	gpio_init(HID_LED_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, HID_LED_Pin);
	gpio_init(MSC_LED_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, MSC_LED_Pin);
	gpio_init(CDC_LED_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, CDC_LED_Pin);
	
	HID_LED_OFF;
	MSC_LED_OFF;
	CDC_LED_OFF;
}

