#ifndef __BSP_ADC_H__
#define __BSP_ADC_H__

#include "main.h"

typedef struct 
{
	float chip_temp;
	float bat_vol;
}adc_val;

extern adc_val val;

/* 初始化ADC0注入通道：通道16测内部温度、通道3测内部参考电压。
 * 返回 - 无 */
void adc_init(void);
/* 软件触发一次注入组转换。
 * 返回 - 无 */
void adc_trigger(void);
/* 粗略微秒级忙等，仅用于ADC上电与校准后的稳定等待。
 * us   - 延时长度，单位微秒（未做精确标定）
 * 返回 - 无 */
void adc_delay(uint16_t us);
/* 读取注入组结果并换算成芯片温度与电池电压。
 * 返回 - 无（结果写入全局val.chip_temp / val.bat_vol） */
void adc_get_val(void);


#endif

