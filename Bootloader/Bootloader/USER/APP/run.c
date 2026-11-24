#include "run.h"
#include "main.h"
#include "stdio.h"
#include "string.h"
#include "ff.h"

FATFS fs;
FIL fd;
FILINFO  	bin_log;
FILINFO	 	*old_log;
uint8_t 	update_flag = 0; //0不需要更新1需要更新2没有bin文件
uint32_t 	update_size;
uint8_t 	data[128];
uint8_t 	iap_buff[4*1024]; 

void start_up(void)
{
	uint8_t ret;
	uint32_t ulReadNum;
	pwr_init();
	GD25QXX_Init();
	OLED_Init();
	led_init();
	key_init();
	
	CDC_LED_OFF;
	MSC_LED_OFF;
	HID_LED_OFF;
	
	ret = f_mount(&fs, "0:", 1);
	if(ret != FR_OK)
	{
		OLED_ShowString(56, 0, "FATFS FAIL", 8, 1);
	}
	else
	{
		ret = f_open(&fd, "0:/update/DAPlink.bin", FA_READ);
		if(ret == FR_OK)
		{
			update_size = f_size(&fd);
			/* 新增: 更新文件大小硬校验, 防止超过应用区 0x30000 覆盖选择/次数页 */
			if(update_size < 8U || update_size > 0x30000U)
			{
				f_close(&fd);
				OLED_ShowString(0, 0, "BIN SIZE ERR", 8, 1);
				{
					uint32_t sp = *(vu32*)FLASH_APP_ADDR;
					uint32_t reset = *(vu32*)(FLASH_APP_ADDR + 4);
					update_flag = (sp > 0x20000000U && sp <= 0x2000C000U &&
					               (sp & 7U) == 0U && (reset & 1U) != 0U &&
					               reset >= FLASH_APP_ADDR && reset < 0x0803C000U) ? 0 : 2;
				}
				if(read_key_left) update_flag = 2;
				OLED_Refresh();
				return;
			}
			ret = f_stat("0:/update/DAPlink.bin",&bin_log);
			if(ret != FR_OK)
				OLED_ShowString(56, 0, "BIN LOG FAIL", 8, 1);
			f_close(&fd);
		}
		else
		{
			OLED_ShowString(56, 0, "BIN FAIL", 8, 1);
            /* No update file: boot a valid installed app, otherwise use
             * the existing USB recovery mode. Never hang before refresh. */
            {
                uint32_t sp = *(vu32*)FLASH_APP_ADDR;
                uint32_t reset = *(vu32*)(FLASH_APP_ADDR + 4);
                update_flag = (sp > 0x20000000U && sp <= 0x2000C000U &&
                               (sp & 7U) == 0U && (reset & 1U) != 0U &&
                               reset >= FLASH_APP_ADDR && reset < 0x0803C000U) ? 0 : 2;
            }
            if(read_key_left) update_flag = 2;
            OLED_Refresh();
            return;
		}
		
		ret = f_open(&fd, "0:/update/log.txt", FA_OPEN_ALWAYS|FA_WRITE|FA_READ);
		if(ret == FR_OK)
		{
			ret = f_read(&fd, data, sizeof(FILINFO), &ulReadNum);
			if(ret == FR_OK && ulReadNum == sizeof(FILINFO))
			{
				old_log = (FILINFO *)data;
				if(update_flag != 2)
				{
					if((old_log->fdate != bin_log.fdate) || (old_log->ftime != bin_log.ftime))
					{
						f_lseek(&fd,0);
						ret = f_write(&fd, &bin_log, sizeof(FILINFO), &ulReadNum);
						OLED_ShowString(0, 0, "APP UPDATE", 8, 1);
						update_flag = 1;

					}
				}
			}
			else
			{
				OLED_ShowString(56, 0, "LOG INIT", 8, 1);
				f_lseek(&fd,0);
				f_write(&fd, &bin_log, sizeof(FILINFO), &ulReadNum);
				update_flag = 1; /* 新日志首次建立时也执行本次更新, 避免格式化后不升级 */
			}
					
			f_close(&fd);
		}
		else
			OLED_ShowString(56, 0, "LOG FAIL", 8, 1);
		
	}
	
	if(((*(vu32*)(FLASH_APP_ADDR+4))&0xFF000000)!=0x08000000) //判断是否有应用程序
	{
		OLED_ShowString(0, 0, "APP UPDATE", 8, 1);
		if(update_flag != 2)
			update_flag = 1;
	}
	memcpy(&bin_log, (char*)data, sizeof(FILINFO));
	if(update_flag == 1)
	{
		sprintf((char*)data, "%d/%d/%d\0", ((bin_log.fdate&0xFE00)>>9)+1980, (bin_log.fdate&0x1E0)>>5, bin_log.fdate&0x1F );
		OLED_ShowString(8, 0, data, 8, 1);
		sprintf((char*)data, "%d:%d:%d\0", (bin_log.ftime&0xF800)>>11, (bin_log.ftime&0x7E0)>>5, (bin_log.ftime&0x1F)*2 );
		OLED_ShowString(16, 0, data, 8, 1);
	}
	
	if(read_key_left)
		update_flag = 2;
	
	OLED_Refresh();

}


void run(void)
{
	uint8_t ret;
	uint8_t d;
	uint32_t i;
	uint32_t ulReadNum;
	while(1)
	{
		if(update_flag == 1)
		{
			ret = f_open(&fd, "0:/update/DAPlink.bin", FA_READ);
			if(ret == FR_OK)
			{
				for(i = 0; i < update_size;)
				{
					ret = f_read(&fd, iap_buff, 4*1024, &ulReadNum);
					if(ret == FR_OK)
					{
						iap_write_appbin(FLASH_APP_ADDR + i, iap_buff, ulReadNum);//更新APP程序
						i += ulReadNum;
					}
					else
					{
						OLED_ShowString(48, 0, "FLASH UPDATE FAIL", 8, 1);
						OLED_Refresh();
						while(1)
						{
							//add flash update fail show
						};
					}
						
					
					d = (uint8_t)(i*100.0/update_size);
					d = d < 100 ? d : 100;
					
					sprintf((char*)data, "[%6d/%6d]", i, update_size);
					OLED_ShowString(48, 19, data, 8, 1);
					sprintf((char*)data, "           %3d%% ", d);
					OLED_ShowString(56, 19, data, 8, 1);
					data[(uint8_t)(d/10.0+1)] = '\0';
					OLED_ShowString(56, 19, data, 8, 0);
					OLED_Refresh();
				}
			}
			else
				OLED_ShowString(56, 0, "BIN FAIL", 8, 1);
		}
		
		iap_load_app(FLASH_APP_ADDR); //跳转APP程序
		while(1);
	}
}




