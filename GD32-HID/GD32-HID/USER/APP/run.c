#include "run.h"
#include "main.h"
#include "stdio.h"
#include "string.h"
#include "ff.h"
#include "DAP_config.h"


usb_dev usb_hid;
FATFS fs;
FIL fd;
FIL fd_opt2;
FILINFO FileInfo;
DIR DirInfo;
app_TypeDef app_data;

#define USB_TASK_PRIO		5	
#define USB_STK_SIZE 		256  
TaskHandle_t UsbTask_Handler;
void usb_task(void *pvParameters);
	
#define LED_TASK_PRIO		3	
#define LED_STK_SIZE 		64  
TaskHandle_t LedTask_Handler;
void led_task(void *pvParameters);

#define KEY_TASK_PRIO		4	
#define KEY_STK_SIZE 		64  
TaskHandle_t KeyTask_Handler;
void key_task(void *pvParameters);

EventGroupHandle_t  EventGroupHandle;

extern void DAP_Thread (void *argument);
extern void DAP_Setup(void);
void page(void);
void USB_UNPLUG(void);
void mode1(void);
void mode2(void);
void mode3(void);
void mode4(void);
void show_top(uint8_t status);
uint8_t D[64] = {0};

//�ر������ж�
void INTX_DISABLE(void)
{		  
	__ASM volatile("cpsid i");
}
//���������ж�
void INTX_ENABLE(void)
{
	__ASM volatile("cpsie i");		  
}

void start_page(void)
{
	uint8_t ret;
	uint32_t ulReadNum;

	ret = f_mount(&fs, "0:", 1);
	if(ret != FR_OK)
	{
		OLED_ShowString(56, 0, (uint8_t *)"FATFS FAIL", 8, 1);
	}
	else
	{
		ret = f_open(&fd, "0:/system/logo.bin", FA_READ);
		if(ret == FR_OK)
		{
			ret = f_read(&fd, OLED_GRAM, 128*8, &ulReadNum);
			if(ret == FR_OK && ulReadNum == 128*8)
				OLED_ShowString(56, 0, (uint8_t *)"LOGO OK", 8, 1);
				
			f_close(&fd);
		}
		else
			OLED_ShowString(56, 0, (uint8_t *)"LOGO FAIL", 8, 1);
	}
	OLED_Refresh();
	vTaskDelay(1000);
	OLED_Clear();
	
}
void data_move(uint8_t *d, uint8_t m, uint8_t max)
{
	uint8_t i;
	for(i = 0; i < max - m; i++)
	{
		d[max-(i+1)] = d[max-(m+i+1)];
	}
}
void page(void)
{
	uint8_t i,j;
	if(key_val.mesg == 1)
	{
		switch(key_val.key)
		{
			case key_left:
				app_data.head = (app_data.head + 1) % 2;   /* 上一个(循环, 共2项) */
				break;
			case key_right:
				app_data.head = (app_data.head + 1) % 2;   /* 下一个(循环, 共2项) */
				break;
			case key_mid:
				if(app_data.mode == 0)
				{
					if(app_data.head == 0) 
					{
						app_data.mode = 1;
						OLED_Clear();
						show_top(1);
						oled.Refresh_flag = 1;
						key_val.mesg = 0;
						return;
					}
					if(app_data.head == 1)
					{
						app_data.mode = 2;
						app_data.head = 0;
						OLED_Clear();
						oled.Refresh_flag = 1;
						key_val.mesg = 0;
						usbd_disconnect(&usb_hid);
						rcu_periph_clock_disable(RCU_USBD);
						return;
					}
				}
				break;
			case key_l_mid:						//�����м��ػ�
				OLED_DisPlay_Off();
				GD25QXX_PowerDown();
				usbd_disconnect(&usb_hid);
				vTaskDelete(LedTask_Handler);
				POWER_OFF;
				while(1)
				{
					vTaskDelay(1000);
					i = bat_status_get();
					if(i == 1)
					{
						switch(j)
						{
							case 0: HID_LED_ON, MSC_LED_OFF, CDC_LED_OFF;break;
							case 1: HID_LED_OFF, MSC_LED_ON, CDC_LED_OFF;break;
							case 2: HID_LED_OFF, MSC_LED_OFF, CDC_LED_ON;break;
						}
					}
					else
							HID_LED_ON, MSC_LED_ON, CDC_LED_ON;
					j++;
					if(j >= 3) j = 0;
				}
//				pmu_wakeup_pin_enable();
//				pmu_to_standbymode(WFI_CMD);
		}
		show_top(1);
		oled.Refresh_flag = 1;
		key_val.mesg = 0;
	}

	show_top(0);
	
	if(oled.Refresh_flag == 1)
	{
		OLED_ShowChinese(16, 32, 1, 4, 1);
		OLED_ShowChinese(32, 32, 2, 4, 1);
		if(app_data.head == 0) 			OLED_ShowString(16, 96, "<-", 16, 1),OLED_ShowString(32, 96, "  ", 16, 1);
		else if(app_data.head == 1) OLED_ShowString(16, 96, "  ", 16, 1),OLED_ShowString(32, 96, "<-", 16, 1);
		
		OLED_Refresh();
		OLED_Clear();
		oled.Refresh_flag = 0;
	}
}


void mode1(void)
{
	uint8_t i;
	static uint32_t cnt = 0;
	
	if(key_val.mesg == 1)
	{
		switch(key_val.key)
		{
			case key_l_mid:                 /* 长按 -> 模式选择页 */
			case key_d_mid:                 /* 双击 -> 返回上一界面(=模式选择) */
				app_data.mode = 0;
				app_data.head = 0;
				OLED_Clear();
				show_top(1);
				oled.Refresh_flag = 1;
				key_val.mesg = 0;
				return;
		}
		key_val.mesg = 0;
	}

	if(oled.Refresh_flag == 1)
	{

		sprintf((char *)D, "HID:%3d%% \0",app_data.hid_d[0]);
		OLED_ShowString(16, 0, D, 8, 1);
		for(i = 0; i < 9; i++)
		{
			OLED_DrawLine(28 - app_data.hid_d[i]*0.15, 50+i*8, 28 - app_data.hid_d[i+1]*0.15, 50+(i+1)*8, 1);
		}
		OLED_DrawLine(29,50, 29, 122, 1);
		sprintf((char *)D, "File:%2d%% \0",app_data.cb_d[0]);
		OLED_ShowString(32, 0, D, 8, 1);
		for(i = 0; i < 9; i++)
		{
			OLED_DrawLine(44 - app_data.cb_d[i]*0.15, 50+i*8, 44 - app_data.cb_d[i+1]*0.15, 50+(i+1)*8, 1);
		}
		OLED_DrawLine(45,50, 45, 122, 1);
		sprintf((char *)D, "CDC:%3d%% \0",app_data.cdc_d[0]);
		OLED_ShowString(48, 0, D, 8, 1);
		for(i = 0; i < 9; i++)
		{
			OLED_DrawLine(60 - app_data.cdc_d[i]*0.15, 50+i*8, 60 - app_data.cdc_d[i+1]*0.15, 50+(i+1)*8, 1);
		}
		OLED_DrawLine(61,50, 61, 122, 1);
		OLED_Refresh();
		OLED_Clear();
		oled.Refresh_flag = 0;
	}
	
	if(cnt < 500000)
		cnt++;
	else
	{
		cnt = 0;
		
		if(app_data.hid_max < app_data.hid_cnt)
			app_data.hid_max = app_data.hid_cnt;
		if(app_data.cb_max < app_data.cb_cnt)
			app_data.cb_max = app_data.cb_cnt;
		if(app_data.cdc_max < app_data.cdc_cnt)
			app_data.cdc_max = app_data.cdc_cnt;
			
		data_move(app_data.hid_d, 1, 10);
		data_move(app_data.cb_d, 1, 10);
		data_move(app_data.cdc_d, 1, 10);
			
		app_data.hid_d[0] = (uint8_t)(app_data.hid_cnt * 100.0 / app_data.hid_max);
		app_data.cb_d[0] = (uint8_t)(app_data.cb_cnt * 100.0 / app_data.cb_max);
		app_data.cdc_d[0] = (uint8_t)(app_data.cdc_cnt * 100.0 / app_data.cdc_max);
			
		app_data.hid_cnt = 0;
		app_data.cb_cnt = 0;
		app_data.cdc_cnt = 0;
		
		show_top(1);
		oled.Refresh_flag = 1;
	}
			
}
/* mode2: 离线下载选择页 (算法 / 文件 / 开始烧录), 只负责选择, 烧录在 mode4 */
void mode2(void)
{
	if(key_val.mesg == 1)
	{
		switch(key_val.key)
		{
			case key_l_mid:                     /* 长按 -> 模式选择页 */
			case key_d_mid:                     /* 双击 -> 返回上一界面(=模式选择) */
				app_data.mode = 0;
				app_data.head = 0;
				OLED_Clear();
				show_top(1);
				oled.Refresh_flag = 1;
				key_val.mesg = 0;
				rcu_periph_clock_enable(RCU_USBD);
				usbd_connect(&usb_hid);
				return;
			case key_left:                      /* 上一个(循环) */
				app_data.head = (app_data.head + 2) % 3;
				break;
			case key_right:                     /* 下一个(循环) */
				app_data.head = (app_data.head + 1) % 3;
				break;
			case key_mid:
				if(app_data.head != 2)          /* 选算法/文件 */
				{
					app_data.mode = 3;
					oled.Refresh_flag = 1;
					key_val.mesg = 0;
					return;
				}
				else                            /* 开始烧录 -> 进度界面 */
				{
					app_data.mode = 4;
					OLED_Clear();
					oled.Refresh_flag = 1;
					key_val.mesg = 0;
					return;
				}
			default:
				break;
		}

		oled.Refresh_flag = 1;
		key_val.mesg = 0;
	}

	if(oled.Refresh_flag == 1)
	{
		OLED_Clear();
		OLED_ShowChinese(0, 0, 3, 4, 1);
		sprintf((char *)D, "%.21s", app_data.firmware_name);
		OLED_ShowString(16, 0, D, 8, 1);
		OLED_ShowChinese(24, 0, 4, 4, 1);
		sprintf((char *)D, "%.21s", app_data.bin_name);
		OLED_ShowString(40, 0, D, 8, 1);
		OLED_ShowChinese(48, 0, 5, 4, 1);
		OLED_ShowString(app_data.head * 24, 100, "<-", 16, 1);
		OLED_Refresh();
		oled.Refresh_flag = 0;
	}
}

static void show_download_progress(uint8_t percent)
{
	uint8_t text[17];
	memset(OLED_GRAM[3], 0, sizeof(OLED_GRAM[3]));
	sprintf((char *)text, "[          ]%3u%%", (unsigned int)percent);
	OLED_ShowString(24, 16, text, 8, 1);
	text[percent / 10 + 1] = '\0';
	OLED_ShowString(24, 22, text + 1, 8, 0);
	OLED_Refresh();
}

static void show_download_status(const char *status)
{
	memset(OLED_GRAM[5], 0, sizeof(OLED_GRAM[5]));
	OLED_ShowString(40, 0, (uint8_t *)status, 8, 1);
	OLED_Refresh();
}

void mode4(void)
{
	uint8_t err_flag = 0, is_ext_algo = 0;
	uint32_t ret, i, j;
	uint32_t ulReadNum, bin_size;
	uint32_t opt_prog_addr = 0, opt_base_addr = 0, opt_byte_size = 0;
	uint8_t opt_failed = 0;
	static uint8_t done = 0;

	if(key_val.mesg == 1)
	{
		switch(key_val.key)
		{
			case key_d_mid:                     /* 双击 -> 返回选择页 */
				app_data.mode = 2;
				done = 0;
				OLED_Clear();
				oled.Refresh_flag = 1;
				key_val.mesg = 0;
				return;
			case key_l_mid:                     /* 长按 -> 模式选择页 */
				app_data.mode = 0;
				app_data.head = 0;
				done = 0;
				OLED_Clear();
				show_top(1);
				oled.Refresh_flag = 1;
				key_val.mesg = 0;
				rcu_periph_clock_enable(RCU_USBD);
				usbd_connect(&usb_hid);
				return;
			default:
				break;
		}
		key_val.mesg = 0;
	}

	if(done)                                    /* 已烧录完, 停留在结果页等待按键 */
		return;
	done = 1;

	OLED_Clear();
	show_download_progress(0);

	if(app_data.bin_name[0] != '\0' && app_data.firmware_name[0] != '\0')
	{
		show_download_status("Connecting...");
		/* Power-cycle target and grab SWD before programming */
		swd_off();
		TARGET_PWR_OFF;
		delaymS(200);
		TARGET_PWR_ON;
		/* Retry SWD: grab CPU before firmware disables SWD */
		{
			uint8_t swd_ok = 0;
			for (int _i = 0; _i < 50; _i++) {
				if (swd_init_debug()) { swd_ok = 1; break; }
				delaymS(1);
			}
			if (!swd_ok) {
				show_download_status("DIS: SWD failed");
				swd_off();
				TARGET_PWR_OFF;
				goto download_return;
			}
		}

		err_flag = 0;
		sprintf((char *)D, "0:/download/firmware/%s\0", app_data.firmware_name);
		ret = f_open(&fd, (TCHAR *)D, FA_READ);
		if(ret == FR_OK)								// read firmware algo OK
		{
			uint32_t fw_size = f_size(&fd);
			ret = f_read(&fd, DATA_BUFFER, 2048, &ulReadNum);
			if(ret == FR_OK && ulReadNum != 0)
			{
				err_flag = 1;
				/* Detect extended format with OPT support */
				if(fw_size == ((program_target_t *)DATA_BUFFER)->algo_size + PROG_TARGET_EXT_HDR_SIZE)
				{
					is_ext_algo = 1;
					/* Save OPT fields before shifting algo_blob to align with program_target_t */
					opt_prog_addr = ((program_target_ext_t *)DATA_BUFFER)->opt_program;
					opt_base_addr = ((program_target_ext_t *)DATA_BUFFER)->opt_addr;
					opt_byte_size = ((program_target_ext_t *)DATA_BUFFER)->opt_size;
					/* Shift algo_blob from offset 68 to 56 so program_target_t cast works */
					memmove(DATA_BUFFER + 56, DATA_BUFFER + 68, ((program_target_t *)DATA_BUFFER)->algo_size);
				}
			}
			f_close(&fd);
		}
		else 
		{
			show_download_status("ER0: Algo open");
			goto download_return;
		}
		if(err_flag == 1)
		{
			show_download_status("Loading algorithm...");
			OLED_Refresh();
			if(target_flash_init((program_target_t *)DATA_BUFFER, ((program_target_t *)DATA_BUFFER)->flash_addr) == ERROR_SUCCESS)
				err_flag = 2;								//������¼�㷨��Ŀ��оƬ
			else
				target_flash_uninit((program_target_t *)DATA_BUFFER);
		}
		else
		{			
			show_download_status("ER1: Algo read");
			goto download_return;
		}
		if(err_flag == 2)
		{
			show_download_status("Erasing...");
			OLED_Refresh();
			if(target_flash_erase_chip((program_target_t *)DATA_BUFFER) == ERROR_SUCCESS)
				err_flag = 3;
			else
			{
				target_flash_uninit((program_target_t *)DATA_BUFFER);
				sprintf((char *)D, "Erase error: %04X", (unsigned int)(g_erase_diag & 0xFFFF));
				show_download_status((char *)D);
				OLED_Refresh();
				swd_off();
				TARGET_PWR_OFF;
				{ volatile uint32_t _d; for(_d=0; _d<3000000; _d++); }
				goto download_return;
			}								//����Ŀ��оƬ
		}
		else 
		{
			target_flash_uninit((program_target_t *)DATA_BUFFER);
			show_download_status("ER2: Algo init");
			goto download_return;
		}
		if(err_flag == 3)
		{
			sprintf((char *)D, "0:/download/bin/%s\0", app_data.bin_name);
			ret = f_open(&fd, (TCHAR *)D, FA_READ);
			if(ret == FR_OK)
				err_flag = 4;
		}
		else 
		{
			target_flash_uninit((program_target_t *)DATA_BUFFER);
			show_download_status("ER3: Erase failed");
			goto download_return;
		}
		if(err_flag == 4)
		{
			show_download_status("Programming...");
			OLED_Refresh();
			
			bin_size = f_size(&fd);
			taskENTER_CRITICAL();
			for(i = 0; i < bin_size;)
			{
				ret = f_read(&fd, (DATA_BUFFER + 2048), 2048, &ulReadNum);
				if(ret == FR_OK && ulReadNum != 0)
				{
					if(target_flash_program_page((program_target_t *)DATA_BUFFER, 							
																			((program_target_t *)DATA_BUFFER)->flash_addr + i,
																			(const uint8_t *)(DATA_BUFFER + 2048), ulReadNum) != ERROR_SUCCESS)  //��¼����Ŀ��оƬ
					{
						show_download_status("ER6: Program failed");
						break;
					}
					i += ulReadNum;
					j = i;
					j = (uint32_t)(j * 100.0/ bin_size);
								
					show_download_progress((uint8_t)j);
				}
				else 
				{
					show_download_status("ER5: Firmware read");
					break;
				}
			}

						/* Verify firmware after programming */
			if(i == bin_size)
			{
				show_download_status("Verifying...");
				OLED_Refresh();
				f_close(&fd);
				sprintf((char *)D, "0:/download/bin/%s", app_data.bin_name);
				if(f_open(&fd, (TCHAR *)D, FA_READ) == FR_OK)
				{
					for(j = 0; j < bin_size; )
					{
						ret = f_read(&fd, (DATA_BUFFER + 2048), 2048, &ulReadNum);
						if(ret != FR_OK || ulReadNum == 0) { i = 0; break; }
						if(target_flash_verify((program_target_t *)DATA_BUFFER,
						                        ((program_target_t *)DATA_BUFFER)->flash_addr + j,
						                        (const uint8_t *)(DATA_BUFFER + 2048),
						                        ulReadNum) != ERROR_SUCCESS) { i = 0; break; }
						j += ulReadNum;
					}
				}
				else { i = 0; }
				if(i == bin_size)
				{
					show_download_status("Verify OK");
				}
				else
				{
					show_download_status("Verify failed");
				}
				OLED_Refresh();
				f_close(&fd);
			}

/* OPT programming for extended algorithm */
			if(is_ext_algo && i == bin_size)
			{
				FIL fd_opt;
				char opt_name[40];
				uint8_t *dot;
				uint32_t opt_read;

				show_download_status("Programming OPT...");
				OLED_Refresh();

				dot = (uint8_t *)strrchr((const char *)app_data.bin_name, '.');
				if(dot && (dot - app_data.bin_name) < 30)
				{
					uint32_t prefix_len = dot - app_data.bin_name;
					memcpy(opt_name, app_data.bin_name, prefix_len);
					opt_name[prefix_len] = '\0';
					strcat(opt_name, "_opt.bin");
				}
				else
				{
					strcpy(opt_name, "default_opt.bin");
				}

				sprintf((char *)D, "0:/download/bin/%s", opt_name);
				if(f_open(&fd_opt, (TCHAR *)D, FA_READ) == FR_OK)
				{
					ret = f_read(&fd_opt, (DATA_BUFFER + 2048), 128, &opt_read);
					f_close(&fd_opt);
					if(ret == FR_OK && opt_read > 0)
					{
						/* Use saved OPT fields (algo_blob was shifted by memmove) */
						{
							program_target_ext_t algo_ext;
							algo_ext.sys_call_s = ((program_target_t *)DATA_BUFFER)->sys_call_s;
							algo_ext.program_buffer = ((program_target_t *)DATA_BUFFER)->program_buffer;
							algo_ext.opt_program = opt_prog_addr;
							algo_ext.opt_addr = opt_base_addr;
							algo_ext.opt_size = opt_byte_size;
							if(target_opt_program_ext(&algo_ext,
							                           (const uint8_t *)(DATA_BUFFER + 2048)) == ERROR_SUCCESS)
						{
							show_download_status("OPT OK");
							}
							else
							{
								show_download_status("OPT program failed");
								opt_failed = 1;
							}
						} /* end of algo_ext scope */
					}
					else
					{
						show_download_status("OPT read failed");
						opt_failed = 1;
					}
				}
				else
				{
					show_download_status("OPT file missing");
					opt_failed = 1;
				}
				OLED_Refresh();
			}

			/* Verify OPT bytes - stage by stage */
			if(is_ext_algo && i == bin_size && opt_prog_addr)
			{
				show_download_status("Verifying OPT...");
				OLED_Refresh();
				{
					char opt_name2[40];
					uint8_t *dot2 = (uint8_t *)strrchr((const char *)app_data.bin_name, '.');
					if(dot2 && (dot2 - app_data.bin_name) < 30)
					{
						uint32_t prefix_len = dot2 - app_data.bin_name;
						memcpy(opt_name2, app_data.bin_name, prefix_len);
						opt_name2[prefix_len] = 0;
						strcat(opt_name2, "_opt.bin");
					}
					else strcpy(opt_name2, "default_opt.bin");
					sprintf((char *)D, "0:/download/bin/%s", opt_name2);
				}
				show_download_status("Reading OPT file...");
				OLED_Refresh();
				if(f_open(&fd_opt2, (TCHAR *)D, FA_READ) == FR_OK)
				{
					ret = f_read(&fd_opt2, (DATA_BUFFER + 2048), 128, &ulReadNum);
					if(ret == FR_OK && ulReadNum > 0)
					{
						show_download_status("Checking OPT...");
						OLED_Refresh();
						{
							program_target_ext_t algo_ext;
							algo_ext.sys_call_s = ((program_target_t *)DATA_BUFFER)->sys_call_s;
							algo_ext.program_buffer = ((program_target_t *)DATA_BUFFER)->program_buffer;
							algo_ext.opt_program = opt_prog_addr;
							algo_ext.opt_addr = opt_base_addr;
							algo_ext.opt_size = opt_byte_size;
							if(target_flash_verify_ext(&algo_ext,
							                              opt_base_addr,
							                              (const uint8_t *)(DATA_BUFFER + 2048),
							                              ulReadNum) != ERROR_SUCCESS)
							{
								show_download_status("Verify failed");
								i = 0;
							}
							else
							{
								show_download_status("OPT verify OK");
							}
						}
					}
					else
					{
						show_download_status("OPT verify read error");
						opt_failed = 1;
					}
					f_close(&fd_opt2);
				}
				else
				{
					show_download_status("OPT verify file error");
					opt_failed = 1;
				}
				OLED_Refresh();
			}


			swd_set_target_state_hw(RESET_RUN);
			swd_off();
			TARGET_PWR_OFF;
			if(i == bin_size)
			{
				show_download_status(opt_failed ? "FW OK, OPT failed" : "SUC: Program OK");
				OLED_Refresh();
				for(volatile uint32_t _d = 0; _d < 500000; _d++);
				swd_set_target_reset(0);
			}
			taskEXIT_CRITICAL();
			f_close(&fd);
		}
		else show_download_status("ER4: Firmware open");
		
download_return:		
		swd_off();
		TARGET_PWR_OFF;
		oled.Refresh_flag = 1;
	}
	else
	{
		show_download_status("No file selected");
		OLED_Refresh();
	}
}
void mode3(void)
{
	static uint32_t index = 0;
	static uint32_t sum = 0;
	uint32_t j;
	uint8_t i, ret;
	
	if(key_val.mesg == 1)
	{
		switch(key_val.key)
		{
			case key_left:
				if(sum)
					index = index == 0 ? sum - 1 : index - 1;
				break;
			case key_right:
				if(sum)
					index = (index + 1) % sum;
				break;
			case key_mid:                   /* 选中文件 -> 返回选择页 */
			case key_d_mid:                 /* 双击 -> 返回上一界面(选择页) */
				index = 0;
				app_data.mode = 2;
				OLED_Clear();
				oled.Refresh_flag = 1;
				key_val.mesg = 0;
				return;
			case key_l_mid:                 /* 长按 -> 模式选择页 */
				index = 0;
				app_data.mode = 0;
				app_data.head = 0;
				OLED_Clear();
				show_top(1);
				oled.Refresh_flag = 1;
				key_val.mesg = 0;
				rcu_periph_clock_enable(RCU_USBD);
				usbd_connect(&usb_hid);
				return;
		}

		oled.Refresh_flag = 1;
		key_val.mesg = 0;
	}

	if(oled.Refresh_flag == 1)
	{
		OLED_Clear();
		sum = 0;
		if(app_data.head == 0)
			ret = f_opendir(&DirInfo, "0:/download/firmware");
		else if(app_data.head == 1)
			ret = f_opendir(&DirInfo, "0:/download/bin");
		
		if(ret == FR_OK)
		{
			sum = 0;
			i = 0;
			while(f_readdir(&DirInfo, &FileInfo) == FR_OK)
			{
				if(!FileInfo.fname[0])
					break;
				j = index / 8;
				if(sum >= j*8 && sum < (j + 1)*8)
				{	
					if(i == index % 8)
					{
//						OLED_ShowString(i*8, 110, "<-", 8, 1);
						if(app_data.head == 0)
						{
							memset(app_data.firmware_name, '\0', 32);
							strncpy((char *)app_data.firmware_name, FileInfo.fname, sizeof(app_data.firmware_name) - 1);
						}	
						else if(app_data.head == 1)
						{
							memset(app_data.bin_name, '\0', 32);
							strncpy((char *)app_data.bin_name, FileInfo.fname, sizeof(app_data.bin_name) - 1);
						}
						FileInfo.fname[21] = '\0';   					//������ʾ����			
						OLED_ShowString(i*8, 0, (uint8_t *)FileInfo.fname, 8, 0);  //���Դ����ͷ�����ļ�����ʾ����?
					}
					else
					{
						FileInfo.fname[21] = '\0';   					//������ʾ����	
						OLED_ShowString(i*8, 0, (uint8_t *)FileInfo.fname, 8, 1);
					}		
					i++;
				}
				sum ++;
			}
			f_closedir(&DirInfo);
		}
		if(!sum)
		{
			index = 0;
			OLED_ShowString(0, 0, "No files", 8, 1);
		}
		OLED_Refresh();
		oled.Refresh_flag = 0;
	}
}

void run_task(void *pvParameters)
{
		pwr_init();
		led_init();
		key_init();
		OLED_Init();
		GD25QXX_Init();
		adc_init();
		uart_init();
		time_init();
	
		usbd_init(&usb_hid, &Composite_desc, &Composite_class);
		usbd_connect(&usb_hid);
	
		CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
		DWT -> CYCCNT = 0;
		DWT -> CTRL |= DWT_CTRL_CYCCNTENA_Msk;	
		DAP_Setup();
	
		taskENTER_CRITICAL();
		xTaskCreate((TaskFunction_t )usb_task,     	
                (const char*    )"usb_task",   	
                (uint16_t       )USB_STK_SIZE, 
                (void*          )NULL,				
                (UBaseType_t    )USB_TASK_PRIO,	
                (TaskHandle_t*  )&UsbTask_Handler);
		xTaskCreate((TaskFunction_t )led_task,     	
                (const char*    )"led_task",   	
                (uint16_t       )LED_STK_SIZE, 
                (void*          )NULL,				
                (UBaseType_t    )LED_TASK_PRIO,	
                (TaskHandle_t*  )&LedTask_Handler);
		xTaskCreate((TaskFunction_t )key_task,     	
                (const char*    )"key_task",   	
                (uint16_t       )KEY_STK_SIZE, 
                (void*          )NULL,				
                (UBaseType_t    )KEY_TASK_PRIO,	
                (TaskHandle_t*  )&KeyTask_Handler);								
		EventGroupHandle = xEventGroupCreate();
		taskEXIT_CRITICAL();
		
		start_page();
		oled.Refresh_flag = 1;
#ifdef V1
		app_data.mode = 1;
#else
		if(bat_status_get() == 0)
			app_data.mode = 2;
		else
			app_data.mode = 1;
#endif

		while(1)
		{
//			vTaskDelay(1000);
			switch(app_data.mode)
			{
				case 0:page();break;
				case 1:mode1();break;
				case 2:mode2();break;
				case 3:mode3();break;
				case 4:mode4();break;
			}
			
		}
}

void usb_task(void *pvParameters)
{
	while(1)
	{
		DAP_Thread(NULL);
	}
}

void led_task(void *pvParameters)
{
	while(1)
	{
		if(app_data.hid_led_blink)
			app_data.hid_led_blink = 0, HID_LED_ON;
		if(app_data.cb_led_blink)
			app_data.cb_led_blink = 0, MSC_LED_ON;
		if(app_data.cdc_led_blink)
			app_data.cdc_led_blink = 0, CDC_LED_ON;
		vTaskDelay(100);
		HID_LED_OFF;
		MSC_LED_OFF;
		CDC_LED_OFF;
		vTaskDelay(100);
	}
}

void key_task(void *pvParameters)
{
	while(1)
	{
		key_num event = key_scan();
		if(!key_val.mesg && event != key_null)
		{
			key_val.key = event;
			key_val.mesg = 1;
		}
		vTaskDelay(pdMS_TO_TICKS(KEY_SCAN_MS));
	}
}



void show_top(uint8_t status)
{
	static uint32_t cnt;
	uint8_t i, bat;
	
	if(cnt > 500000 || status)
	{
		cnt = 0;
		adc_get_val();
		i = bat_status_get();
		if(i == 0)
		{
			OLED_ShowLogo(0, 0, 7, 1);    				//USB
			OLED_ShowString(0, 20, "DIS", 8, 1);
			
			bat = (uint8_t)((val.bat_vol - 3.5)*100/0.5);
			sprintf((char *)D, "%3d%%\0", bat);
			OLED_ShowString(0, 93, D, 8, 1);
			
			if(bat < 20) OLED_ShowLogo(0, 74, 0, 1);
			else if(bat < 40) OLED_ShowLogo(0, 74, 1, 1);
			else if(bat < 60) OLED_ShowLogo(0, 74, 2, 1);
			else if(bat < 80) OLED_ShowLogo(0, 74, 3, 1);
			else OLED_ShowLogo(0, 74, 4, 1);
		}
		else
		{
			OLED_ShowLogo(0, 0, 6, 1);    								//USB
			OLED_ShowString(0, 20, "CON", 8, 1);
			if(i == 1) 
			{
				OLED_ShowLogo(0, 74, 5, 1);
			}
			else
			{
				OLED_ShowLogo(0, 74, 4, 1);
				sprintf((char *)D, "%3d%%\0", 100);
				OLED_ShowString(0, 93, D, 8, 1);
			}
		}
		
		sprintf((char *)D, "%4.1fC\0", val.chip_temp);
//		sprintf((char *)D, "%4.1fC\0", val.bat_vol);
		OLED_ShowString(0, 40, D, 8, 1);
		adc_trigger();
		if(!status)
			oled.Refresh_flag = 1;
	}
	else
		cnt++;
}


