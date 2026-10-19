#ifndef __RUN_H__
#define __RUN_H__

#include "main.h"


typedef struct
{	
	uint8_t mode; //mode 0:模式选择页 1:在线仿真页 2:离线下载选择页 3:文件选择页 4:烧录进度页
	uint8_t head;
	
	uint8_t bin_name[32];					//������¼bin�ļ�
	uint8_t firmware_name[32];		//������¼�㷨
	uint8_t opt_enable;						/* 1: OPT file uploaded, program OPT; 0: skip OPT */
	
	uint8_t hid_led_blink;
	uint8_t cb_led_blink;
	uint8_t cdc_led_blink;
	
	uint16_t hid_cnt;
	uint16_t cb_cnt;
	uint16_t cdc_cnt;
	uint16_t hid_max;
	uint16_t cb_max;
	uint16_t cdc_max;
	
	uint8_t hid_d[10];
	uint8_t cb_d[10];
	uint8_t cdc_d[10];
	
}app_TypeDef;

extern usb_dev usb_hid;
extern app_TypeDef app_data;
extern EventGroupHandle_t  EventGroupHandle;

void run_task(void *pvParameters);
void start_page(void);
void run(void);

#endif

