#include "bsp_led.h"
#include "main.h"

void led_init(void)
{
	rcu_periph_clock_enable(RCU_GPIOB);
	gpio_init(HID_LED_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, HID_LED_Pin);
	gpio_init(MSC_LED_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, MSC_LED_Pin);
	gpio_init(CDC_LED_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, CDC_LED_Pin);
	
	HID_LED_OFF;
	MSC_LED_OFF;
	CDC_LED_OFF;
}

