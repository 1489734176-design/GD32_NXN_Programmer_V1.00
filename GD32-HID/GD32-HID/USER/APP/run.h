/* 中文注释补充（2026-10-26）：应用状态：模式0/1入口已移除；2烧录选择、3文件列表、4烧录；OPT开关仅控制OPT步骤。 */
#ifndef __RUN_H__
#define __RUN_H__

#include "firmware_features.h"
#include "main.h"


typedef struct
{	
	uint8_t mode; //状态2:烧录选择页（USB后台通信），3:文件列表，4:烧录/结果页；0/1不再使用
	uint8_t head;
	
	uint8_t bin_name[32];					//������¼bin�ļ�
	uint8_t firmware_name[32];		//������¼�㷨
	uint8_t opt_enable;						/* 1: OPT file uploaded, program OPT; 0: skip OPT */
	
/* 在线仿真专用部分：宏为0时不编译；离线SWD/OPT及WinUSB传输继续保留。 */
#if DAP_ONLINE_ENABLE
	uint8_t hid_led_blink;
#endif
	uint8_t cb_led_blink;
	uint8_t cdc_led_blink;
	
/* 在线仿真专用部分：宏为0时不编译；离线SWD/OPT及WinUSB传输继续保留。 */
#if DAP_ONLINE_ENABLE
	uint16_t hid_cnt;
#endif
	uint16_t cb_cnt;
	uint16_t cdc_cnt;
/* 在线仿真专用部分：宏为0时不编译；离线SWD/OPT及WinUSB传输继续保留。 */
#if DAP_ONLINE_ENABLE
	uint16_t hid_max;
	uint16_t cb_max;
	uint16_t cdc_max;
	
	uint8_t hid_d[10];
	uint8_t cb_d[10];
	uint8_t cdc_d[10];
#endif
	
}app_TypeDef;

extern usb_dev usb_hid;
extern app_TypeDef app_data;
/* 实际目标烧录占用标志；结果页面不占用USB文件服务。 */
extern volatile uint8_t g_programming_active;
/* 在线仿真专用部分：宏为0时不编译；离线SWD/OPT及WinUSB传输继续保留。 */
#if DAP_ONLINE_ENABLE
extern EventGroupHandle_t  EventGroupHandle;
#endif

/* 初始化硬件并创建任务；循环先处理上位机延后请求，再运行当前界面/烧录状态机。 */
void run_task(void *pvParameters);
/* 启动界面：先恢复掉电保存的次数，再挂载文件系统和加载开机图标。 */
void start_page(void);
/* 启动程序读取应用升级文件并写入应用分区，显示进度，随后跳转应用。 */
void run(void);

#endif

