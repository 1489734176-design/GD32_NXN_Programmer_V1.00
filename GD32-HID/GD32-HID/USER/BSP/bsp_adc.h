#ifndef __BSP_ADC_H__
#define __BSP_ADC_H__

#include "main.h"

typedef struct 
{
	float chip_temp;
	float bat_vol;
}adc_val;

extern adc_val val;

void adc_init(void);
void adc_trigger(void);
void adc_delay(uint16_t us);
void adc_get_val(void);


#endif

