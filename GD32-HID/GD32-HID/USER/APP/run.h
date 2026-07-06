#ifndef __RUN_H__
#define __RUN_H__

#include "main.h"


typedef struct
{	
	uint8_t mode; //mode 0顶层页面，1在线仿真页面，2离线下载页面, 3固件选择界面
	uint8_t head;
	
	uint8_t bin_name[32];					//离线烧录bin文件
	uint8_t firmware_name[32];		//离线烧录算法
	
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

