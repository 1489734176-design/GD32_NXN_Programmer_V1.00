#include "bsp_pwr.h"
#include "main.h"



void pwr_init(void)
{
	gpio_init(BAT_CHARG_GPIO_Port, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, BAT_CHARG_Pin);
	gpio_init(BAT_FULL_GPIO_Port, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, BAT_FULL_Pin);
	gpio_bit_set(USB_EN_GPIO_Port, USB_EN_Pin);
	gpio_init(USB_EN_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, USB_EN_Pin);
	

	if(!bat_status_get())
	{
		gpio_bit_set(PWR_EN_GPIO_Port, PWR_EN_Pin);
		gpio_init(PWR_EN_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, PWR_EN_Pin);
	}
	
}

uint8_t bat_status_get(void)
{
	if(!gpio_input_bit_get(BAT_CHARG_GPIO_Port, BAT_CHARG_Pin))
		return 1;
	if(!gpio_input_bit_get(BAT_FULL_GPIO_Port, BAT_FULL_Pin))
		return 2;
	return 0;
}


