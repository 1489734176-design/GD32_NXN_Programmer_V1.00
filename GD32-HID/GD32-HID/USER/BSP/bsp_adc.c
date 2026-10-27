#include "bsp_adc.h"
#include "main.h"

adc_val val;

/* 初始化ADC0注入通道：通道16测内部温度、通道3测内部参考电压，
 * 完成使能、延时稳定与校准后触发一次转换。
 * 返回 - 无 */
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

/* 软件触发一次注入组转换，转换完成后由adc_get_val读取结果。
 * 返回 - 无 */
void adc_trigger(void)
{
		adc_software_trigger_enable(ADC0, ADC_INSERTED_CHANNEL);
}


/* 读取注入组转换结果，换算成芯片温度和电池电压写入全局val。
 * 温度按内部传感器公式换算；电压含2.8V基准、1.5与1.15两级分压系数。
 * 返回 - 无（结果写入val.chip_temp / val.bat_vol） */
void adc_get_val(void)
{
		val.chip_temp = (1.43 - ADC_IDATA0(ADC0)*3.3/4096) * 1000 / 4.3 + 25;
		val.bat_vol = (ADC_IDATA1(ADC0) * 2.8 * 1.5 * 1.15 /4096);
}

/* 粗略微秒级忙等，仅用于ADC上电与校准后的稳定等待。
 * us   - 延时长度，单位微秒（按空转__nop计数，未做精确标定）
 * 返回 - 无 */
void adc_delay(uint16_t us)
{
	while(us)
	{
		us--;
		__nop();
	}
}



