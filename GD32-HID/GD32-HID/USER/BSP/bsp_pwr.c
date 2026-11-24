#include "bsp_pwr.h"
#include "main.h"



/* 初始化电源相关引脚：充电状态、USB供电使能、自身电源保持、双通道目标供电。
 * 通道A使用，通道B本期不参与流程，只做安全停车（VCC关断、信号高阻）。
 * 返回 - 无 */
void pwr_init(void)
{
	rcu_periph_clock_enable(RCU_GPIOB);
	rcu_periph_clock_enable(RCU_GPIOC);

	gpio_init(BAT_CHARG_GPIO_Port, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, BAT_CHARG_Pin);
	gpio_init(BAT_FULL_GPIO_Port, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, BAT_FULL_Pin);
	gpio_bit_set(USB_EN_GPIO_Port, USB_EN_Pin);
	gpio_init(USB_EN_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, USB_EN_Pin);

	if(!bat_status_get())
	{
		gpio_bit_set(PWR_EN_GPIO_Port, PWR_EN_Pin);
		gpio_init(PWR_EN_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, PWR_EN_Pin);
	}

	/* 两路目标供电 PMOS 先写高再切输出，避免门极悬空导致误上电。
	 * 通道A随后由烧录流程拉低使能；通道B保持关断。 */
	GPIO_BOP(TARGET_PWR_GPIO_Port) = TARGET_PWR_Pin | TGT_B_VCC_EN_Pin;
	gpio_init(TARGET_PWR_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ,
	          TARGET_PWR_Pin | TGT_B_VCC_EN_Pin);

	/* 通道B未实现：RST/SWDIO/SWCLK 与状态线全部高阻，不驱动目标。 */
	gpio_init(TGT_B_RST_GPIO_Port, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ, TGT_B_RST_Pin);
	gpio_init(TGT_B_SWDIO_GPIO_Port, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ, TGT_B_SWDIO_Pin);
	gpio_init(TGT_B_SWCLK_GPIO_Port, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ, TGT_B_SWCLK_Pin);
	gpio_init(TGT_B_START_GPIO_Port, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ,
	          TGT_B_START_Pin | TGT_B_BUSY_Pin | TGT_B_NG_Pin | TGT_B_OK_Pin);
}

/* 读取电池/充电状态，两个检测脚均为低电平有效。
 * 返回 - 0=未接充电器，1=正在充电（BAT_CHARG为低），2=已充满（BAT_FULL为低） */
uint8_t bat_status_get(void)
{
	if(!gpio_input_bit_get(BAT_CHARG_GPIO_Port, BAT_CHARG_Pin))
		return 1;
	if(!gpio_input_bit_get(BAT_FULL_GPIO_Port, BAT_FULL_Pin))
		return 2;
	return 0;
}


