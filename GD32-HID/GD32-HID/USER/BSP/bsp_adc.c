#include "bsp_adc.h"
#include "main.h"

adc_val val;

void adc_init(void)
{
		rcu_periph_clock_enable(RCU_ADC0);	
		rcu_periph_clock_enable(RCU_GPIOA);
	
		gpio_init(BAT_ADC_GPIO_Port, GPIO_MODE_AIN, GPIO_OSPEED_50MHZ, BAT_ADC_Pin);
		
		    /* ADC SCAN function enable */
    adc_special_function_config(ADC0,ADC_SCAN_MODE,ENABLE);  
    /* ADC trigger config */
    adc_external_trigger_source_config(ADC0, ADC_INSERTED_CHANNEL, ADC0_1_2_EXTTRIG_INSERTED_NONE);
    /* ADC data alignment config */
    adc_data_alignment_config(ADC0,ADC_DATAALIGN_RIGHT);
    /* ADC mode config */
    adc_mode_config(ADC_MODE_FREE);  
    /* ADC channel length config */
    adc_channel_length_config(ADC0,ADC_INSERTED_CHANNEL,2);

    /* ADC temperature sensor channel config */
    adc_inserted_channel_config(ADC0, 0, ADC_CHANNEL_16, ADC_SAMPLETIME_239POINT5);
    /* ADC internal voltage channel config */
    adc_inserted_channel_config(ADC0, 1, ADC_CHANNEL_3, ADC_SAMPLETIME_239POINT5);

    /* ADC external trigger enable */
    adc_external_trigger_config(ADC0,ADC_INSERTED_CHANNEL,ENABLE);

    /* ADC temperature and Vrefint enable */
    adc_tempsensor_vrefint_enable();
    
    /* enable ADC interface */
    adc_enable(ADC0);
    adc_delay(1000);    
    /* ADC calibration and reset calibration */
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



