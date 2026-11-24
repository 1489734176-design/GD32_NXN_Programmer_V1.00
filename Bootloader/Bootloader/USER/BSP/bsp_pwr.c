#include "bsp_pwr.h"
#include "main.h"


uint8_t power;
void pwr_init(void)
{
	rcu_periph_clock_enable(RCU_GPIOC);
	gpio_init(BAT_CHARG_GPIO_Port, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, BAT_CHARG_Pin);
	gpio_init(BAT_FULL_GPIO_Port, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, BAT_FULL_Pin);
	
	/* 先把 ODR 置高再切成输出：否则 gpio_init 那一瞬间输出低电平，
	   会误把 D+ 的 1.5k 上拉接上，主机可能认到一次假插入 */
	gpio_bit_set(USB_EN_GPIO_Port, USB_EN_Pin);
	gpio_init(USB_EN_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, USB_EN_Pin);
	
	power = bat_status_get();
	if(!bat_status_get())
	{
		gpio_init(PWR_EN_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, PWR_EN_Pin);
		gpio_bit_set(PWR_EN_GPIO_Port, PWR_EN_Pin);
	}

	/* Ŀ�� VCC ��д�����������˫ͨ�����ֹض� */
	GPIO_BOP(GPIOC) = TGT_A_VCC_EN_Pin | TGT_B_VCC_EN_Pin;
	gpio_init(GPIOC, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, TGT_A_VCC_EN_Pin | TGT_B_VCC_EN_Pin);
}

uint8_t bat_status_get(void)
{
	if(!gpio_input_bit_get(BAT_CHARG_GPIO_Port, BAT_CHARG_Pin))
		return 1;
	if(!gpio_input_bit_get(BAT_FULL_GPIO_Port, BAT_FULL_Pin))
		return 2;
	return 0;
}


