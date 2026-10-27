#include "bsp_pwr.h"
#include "main.h"



void pwr_init(void)
{
	gpio_init(BAT_CHARG_GPIO_Port, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, BAT_CHARG_Pin);
	gpio_init(BAT_FULL_GPIO_Port, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, BAT_FULL_Pin);
	gpio_bit_set(USB_EN_GPIO_Port, USB_EN_Pin);
	gpio_init(USB_EN_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, USB_EN_Pin);
	/* 原 USB_EN 所在的 PB3 已飞线到 PA8，这里保持浮空，避免悬空脚误动作 */
	gpio_init(GPIOB, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ, GPIO_PIN_3);
	

	if(!bat_status_get())
	{
		gpio_bit_set(PWR_EN_GPIO_Port, PWR_EN_Pin);
		gpio_init(PWR_EN_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, PWR_EN_Pin);
	}

	/* Target power PMOS control (PA15), default OFF (HIGH) */
	gpio_bit_set(TARGET_PWR_GPIO_Port, TARGET_PWR_Pin);
	gpio_init(TARGET_PWR_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, TARGET_PWR_Pin);

}

uint8_t bat_status_get(void)
{
	if(!gpio_input_bit_get(BAT_CHARG_GPIO_Port, BAT_CHARG_Pin))
		return 1;
	if(!gpio_input_bit_get(BAT_FULL_GPIO_Port, BAT_FULL_Pin))
		return 2;
	return 0;
}


