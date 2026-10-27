#include "bsp_adc.h"
#include "main.h"

adc_val val;

void adc_init(void)
{
		rcu_periph_clock_enable(RCU_ADC0);	
		rcu_periph_clock_enable(RCU_GPIOA);
	
		gpio_init(BAT_ADC_GPIO_Port, GPIO_MODE_AIN, GPIO_OSPEED_50MHZ, BAT_ADC_Pin);
		
		    /* 使能ADC扫描模式 */
    adc_special_function_config(ADC0,ADC_SCAN_MODE,ENABLE);  
    /* ADC触发源配置 */
    adc_external_trigger_source_config(ADC0, ADC_INSERTED_CHANNEL, ADC0_1_2_EXTTRIG_INSERTED_NONE);
    /* ADC数据对齐方式配置 */
    adc_data_alignment_config(ADC0,ADC_DATAALIGN_RIGHT);
    /* ADC工作模式配置 */
    adc_mode_config(ADC_MODE_FREE);  
    /* ADC通道数配置 */
    adc_channel_length_config(ADC0,ADC_INSERTED_CHANNEL,2);

    /* 内部温度传感器通道配置 */
    adc_inserted_channel_config(ADC0, 0, ADC_CHANNEL_16, ADC_SAMPLETIME_239POINT5);
    /* 内部参考电压(Vrefint)通道配置 */
    adc_inserted_channel_config(ADC0, 1, ADC_CHANNEL_3, ADC_SAMPLETIME_239POINT5);

    /* 使能ADC外部触发 */
    adc_external_trigger_config(ADC0,ADC_INSERTED_CHANNEL,ENABLE);

    /* 使能温度传感器和内部参考电压 */
    adc_tempsensor_vrefint_enable();
    
    /* 使能ADC接口 */
    adc_enable(ADC0);
    adc_delay(1000);    
    /* ADC校准并复位校准寄存器 */
    adc_calibration_enable(ADC0);
		adc_delay(1000);
		adc_trigger();
}

void adc_trigger(void)
{
		adc_software_trigger_enable(ADC0, ADC_INSERTED_CHANNEL);
}


void adc_get_val(void)
{
		val.chip_temp = (1.43 - ADC_IDATA0(ADC0)*3.3/4096) * 1000 / 4.3 + 25;
		val.bat_vol = (ADC_IDATA1(ADC0) * 2.8 * 1.5 * 1.15 /4096);
}

void adc_delay(uint16_t us)
{
	while(us)
	{
		us--;
		__nop();
	}
}



