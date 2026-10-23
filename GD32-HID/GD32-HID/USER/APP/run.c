#include "run.h"
#include "main.h"
#include "stdio.h"
#include "string.h"
#include "ff.h"
#include "DAP_config.h"
#include "usbd_custombulk.h"
#include "burn_quota.h"


usb_dev usb_hid;
FATFS fs;
FIL fd;
FIL fd_opt2;
FILINFO FileInfo;
DIR DirInfo;
app_TypeDef app_data;

#if DAP_ONLINE_ENABLE
#define USB_TASK_PRIO		5	
#define USB_STK_SIZE 		256  
TaskHandle_t UsbTask_Handler;
void usb_task(void *pvParameters);
#endif
	
#define LED_TASK_PRIO		3	
#define LED_STK_SIZE 		64  
TaskHandle_t LedTask_Handler;
void led_task(void *pvParameters);

#define KEY_TASK_PRIO		4	
#define KEY_STK_SIZE 		64  
TaskHandle_t KeyTask_Handler;
void key_task(void *pvParameters);

#if DAP_ONLINE_ENABLE
EventGroupHandle_t  EventGroupHandle;

extern void DAP_Thread (void *argument);
#endif
extern void DAP_Setup(void);
void page(void);
void USB_UNPLUG(void);
void mode1(void);
void mode2(void);
void mode3(void);
void mode4(void);
void show_top(uint8_t status);
uint8_t D[64] = {0};

/* ---- automatic burning (auto mode) ----
   WAIT   : probe the socket until a chip answers SWD
   BURN   : mode4() runs the normal burn sequence
   RESULT : show OK / FAIL for a while
   HOLD   : keep the result on screen until the chip is taken out
   The chip is detected by powering the socket and looking for an SWD
   answer - the same thing the burn path does, so a chip that can be
   burned can also be detected. */
#define AUTO_PROBE_MS       200     /* min interval between two probes */
#define AUTO_PRESENT_CNT    2       /* consecutive hits before we burn */
#define AUTO_ABSENT_CNT     2       /* consecutive misses before "removed" */
#define AUTO_HOLD_MIN_MS    600     /* stay in HOLD at least this long */
#define AUTO_OK_MS          1500    /* result dwell on success */
#define AUTO_FAIL_MS        3000    /* result dwell on failure */

enum { AUTO_ST_WAIT = 0, AUTO_ST_BURN, AUTO_ST_RESULT, AUTO_ST_HOLD };

static uint8_t  g_auto_en      = 1;             /* 0 = manual, 1 = automatic */
static uint8_t  g_auto_st      = AUTO_ST_WAIT;
static uint8_t  g_absent_seen  = 0;             /* consecutive probes without a chip */
static uint8_t  g_present_seen = 0;             /* consecutive probes with a chip */
static uint8_t  g_burn_done    = 0;             /* mode4: this chip was already handled */
static uint8_t  g_burn_ok      = 0;             /* result of the last burn */
static uint32_t g_auto_t0      = 0;             /* tick when the state was entered */
static uint32_t g_auto_tprobe  = 0;             /* tick of the last probe */

static void auto_standby_tick(void);
static void auto_draw(void);
static void auto_start_burn(void);
static uint8_t auto_has_files(void);
static uint32_t auto_now_ms(void);

/* last text pushed to the OLED status row, kept so the standby page can
   repeat the failure reason after mode4 has returned */
static char g_last_status[64] = {0};

/* Quota gets a full-width 32px row. Key hints move to a separate 16px footer. */
static void show_quota(uint16_t y)
{
    if(burn_quota_fault())
        strcpy((char *)D, "\xB4\xCE\xCA\xFD\xBC\xC7\xC2\xBC\xD2\xEC\xB3\xA3");
    else if(burn_quota_enabled())
        sprintf((char *)D, "\xCA\xA3\xD3\xE0\xB4\xCE\xCA\xFD\x3A\x25\x30\x33\x6C\x75\xB4\xCE", (unsigned long)burn_quota_remaining());
    else
        strcpy((char *)D, "\xCA\xA3\xD3\xE0\xB4\xCE\xCA\xFD\x3A\xB2\xBB\xCF\xDE");
    UI_Fill(0, y, 320, 32, 0);
    UI_Text(8, y, (char *)D, 32, 1);
}

static void apply_prepare_download(const uint8_t *algorithm_name,
                                   const uint8_t *program_name,
                                   uint8_t opt_required)
{
    app_data.opt_enable = opt_required ? 1 : 0;
    strncpy((char *)app_data.firmware_name, (const char *)algorithm_name,
            sizeof(app_data.firmware_name) - 1);
    app_data.firmware_name[sizeof(app_data.firmware_name) - 1] = '\0';
    strncpy((char *)app_data.bin_name, (const char *)program_name,
            sizeof(app_data.bin_name) - 1);
    app_data.bin_name[sizeof(app_data.bin_name) - 1] = '\0';
    app_data.mode = 2;
    app_data.head = 2;
    key_val.mesg = 0;
    OLED_Clear();
    oled.Refresh_flag = 1;
    usbd_disconnect(&usb_hid);
    rcu_periph_clock_disable(RCU_USBD);
}

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
    const char *status;
    burn_quota_init();
    ret = f_mount(&fs, "0:", 1);
    status = "\xCE\xC4\xBC\xFE\xCF\xB5\xCD\xB3\xD2\xEC\xB3\xA3";
    if(ret == FR_OK)
    {
        status = "\xCD\xBC\xB1\xEA\xBC\xD3\xD4\xD8\xCA\xA7\xB0\xDC";
        if(f_open(&fd, "0:/system/logo.bin", FA_READ) == FR_OK)
        {
            /* Reuse the existing 4096-byte flash workspace, no logo framebuffer. */
            ret = f_read(&fd, DATA_BUFFER, 1024, &ulReadNum);
            if(ret == FR_OK && ulReadNum == 1024)
            {
                UI_BootLogo(DATA_BUFFER);
                status = "\xCD\xBC\xB1\xEA\xD2\xD1\xBC\xD3\xD4\xD8";
            }
            f_close(&fd);
        }
    }
    UI_Text(8, 196, status, 32, 1);
    vTaskDelay(1000);
    OLED_Clear();
}
#if DAP_ONLINE_ENABLE
void data_move(uint8_t *d, uint8_t m, uint8_t max)
{
	uint8_t i;
	for(i = 0; i < max - m; i++)
	{
		d[max-(i+1)] = d[max-(m+i+1)];
	}
}
#endif /* DAP_ONLINE_ENABLE */
void page(void)
{
	uint8_t i,j;
	if(key_val.mesg == 1)
	{
		switch(key_val.key)
		{
			case key_left:
			case key_l_left:
			case key_rep_left:
				app_data.head = (app_data.head + 1) % 2;   /* 上一个(循环, 共2项) */
				break;
			case key_right:
			case key_l_right:
			case key_rep_right:
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
        UI_Fill(0, 32, 320, 208, 0);
#if DAP_ONLINE_ENABLE
        UI_Text(8, 64, "\xD4\xDA\xCF\xDF\xB7\xC2\xD5\xE6", 32, 1);
#else
        UI_Text(8, 64, "\xCE\xC4\xBC\xFE\xB4\xAB\xCA\xE4", 32, 1);
#endif
        UI_Text(8, 136, "\xC0\xEB\xCF\xDF\xCF\xC2\xD4\xD8", 32, 1);
        UI_Text(272, app_data.head == 0 ? 64 : 136, "<-", 32, 1);
        OLED_Refresh();
		oled.Refresh_flag = 0;
	}
}


#if DAP_ONLINE_ENABLE
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

        UI_Fill(0, 32, 320, 208, 0);
		sprintf((char *)D, "HID:%3d%% \0",app_data.hid_d[0]);
		OLED_ShowString(16, 0, D, 8, 1);
		for(i = 0; i < 9; i++)
		{
			OLED_DrawLine(28 - app_data.hid_d[i]*0.15, 50+i*8, 28 - app_data.hid_d[i+1]*0.15, 50+(i+1)*8, 1);
		}
		OLED_DrawLine(29,50, 29, 122, 1);
		sprintf((char *)D, "\xCE\xC4\xBC\xFE\x3A\x25\x32\x64\x25\x25",app_data.cb_d[0]);
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
#else
/* Mode 1 keeps USB available for the existing PC file manager. */
void mode1(void)
{
    if(key_val.mesg == 1)
    {
        switch(key_val.key)
        {
            case key_mid:
                app_data.mode = 2;
                app_data.head = 2;
                usbd_disconnect(&usb_hid);
                rcu_periph_clock_disable(RCU_USBD);
                break;
            case key_l_mid:
            case key_d_mid:
                app_data.mode = 0;
                app_data.head = 0;
                break;
            default:
                break;
        }
        OLED_Clear();
        show_top(1);
        oled.Refresh_flag = 1;
        key_val.mesg = 0;
        if(app_data.mode != 1) return;
    }
    show_top(0);
    if(oled.Refresh_flag == 1)
    {
        UI_Fill(0, 32, 320, 208, 0);
        UI_Text(8, 56, "\xCE\xC4\xBC\xFE\xB4\xAB\xCA\xE4", 32, 1);
        UI_Text(8, 112, "\xB5\xE7\xC4\xD4\xB4\xAB\xCA\xE4\xCE\xC4\xBC\xFE", 32, 1);
        show_quota(148);
        UI_Text(8, 184, "\xD6\xD0\xBC\xFC\xBD\xF8\xC8\xEB\xC9\xD5\xC2\xBC", 32, 1);
        OLED_Refresh();
        oled.Refresh_flag = 0;
    }
}
#endif /* DAP_ONLINE_ENABLE */

/* mode2: 离线下载选择页 (算法 / 文件 / 开始烧录), 只负责选择, 烧录在 mode4 */
void mode2(void)
{
	/* auto mode always burns the pair that is already loaded; file selection
	   is done in manual mode (long press right to switch) */
	if(g_auto_en)
		app_data.head = 2;

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
			case key_l_left:
			case key_rep_left:
				if(g_auto_en)                   /* 自动模式下不切换焦点 */
					break;
				app_data.head = (app_data.head + 2) % 3;
				break;
			case key_right:                     /* 下一个(循环) */
				if(g_auto_en)
					break;
				app_data.head = (app_data.head + 1) % 3;
				break;
			case key_l_right:                   /* 长按右键 -> 自动/手动 切换 */
				g_auto_en = g_auto_en ? 0 : 1;
				g_auto_st = AUTO_ST_WAIT;
				g_present_seen = 0;
				g_absent_seen = 0;
				g_auto_tprobe = auto_now_ms() - AUTO_PROBE_MS;
				oled.Refresh_flag = 1;
				key_val.mesg = 0;
				return;
			case key_mid:
				if(app_data.head != 2)          /* 选算法/文件 */
				{
					app_data.mode = 3;
					oled.Refresh_flag = 1;
					key_val.mesg = 0;
					return;
				}
				else if(g_auto_en)              /* 自动模式: 中键 = 立刻烧录 */
				{
					if(auto_has_files())
						auto_start_burn();
					key_val.mesg = 0;
					return;
				}
				else                            /* 开始烧录 -> 进度界面 */
				{
					g_burn_done = 0;        /* 重新开始, 不受上一次结果页影响 */
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

	/* auto mode: watch the socket, start a burn as soon as a chip shows up */
	if(g_auto_en)
	{
		auto_standby_tick();
		if(app_data.mode != 2)
			return;
	}

	if(oled.Refresh_flag == 1)
	{
		OLED_Clear();
		if(g_auto_en)
		{
			auto_draw();
		}
		else
		{
            UI_Text(8, 8, "\xC9\xD5\xC2\xBC\xCB\xE3\xB7\xA8", 32, 1);
            UI_Text(8, 48, (const char *)app_data.firmware_name, 16, 1);
            UI_Text(8, 72, "\xC9\xD5\xC2\xBC\xB9\xCC\xBC\xFE", 32, 1);
            UI_Text(8, 112, (const char *)app_data.bin_name, 16, 1);
            UI_Text(8, 176, "\xBF\xAA\xCA\xBC\xC9\xD5\xC2\xBC", 32, 1);
            UI_Text(272, app_data.head == 2 ? 176 : 8 + app_data.head * 64, "<-", 32, 1);
            show_quota(136);
            UI_Text(160, 220, "\xB3\xA4\xB0\xB4\xD3\xD2\xBC\xFC\x3A\xD7\xD4\xB6\xAF", 16, 1);
		}
		OLED_Refresh();
		oled.Refresh_flag = 0;
	}
}

static void show_download_progress(uint8_t percent)
{
    UI_Progress(112, percent);
}

static void show_download_status(const char *status)
{
    uint16_t size = UI_TextWidth(status, 32) <= 576 ? 32 : 16;
    UI_Fill(0, 144, 320, 64, 0);
    UI_TextBox(8, 144, 304, 64, status, (uint8_t)size, 1);
    strncpy(g_last_status, status, sizeof(g_last_status) - 1);
    g_last_status[sizeof(g_last_status) - 1] = '\0';
    show_quota(208);
}

/* Native LCD error page: wrapped 32px title, complete 16px register values. */
static void show_download_error(const char *status, uint32_t diag, uint32_t step, uint32_t addr)
{
    OLED_Clear();
    UI_TextBox(8, 8, 304, 64, status, 32, 1);
    sprintf((char *)D, "\xB2\xBD\xD6\xE8\x3D\x25\x30\x32\x6C\x58\x20\x72\x30\x3D\x25\x6C\x58", (unsigned long)step, (unsigned long)diag);
    UI_Text(8, 88, (char *)D, 16, 1);
    strncpy(g_last_status, status, sizeof(g_last_status) - 1);
    g_last_status[sizeof(g_last_status) - 1] = '\0';
    if(addr)
    {
        sprintf((char *)D, "\xB5\xD8\xD6\xB7\x3D\x25\x30\x38\x6C\x58", (unsigned long)addr);
        UI_Text(8, 116, (char *)D, 16, 1);
    }
    sprintf((char *)D, "CTRL/STAT=%08lX", (unsigned long)g_diag_ctrl_stat);
    UI_Text(8, 144, (char *)D, 16, 1);
    sprintf((char *)D, "DHCSR=%08lX", (unsigned long)g_diag_hcsr);
    UI_Text(8, 172, (char *)D, 16, 1);
    show_quota(208);
}

/* Connect and halt the core as fast as possible.
   0 = no DP response, 1 = halted, 2 = DP answered but CPU would not stop */
static uint8_t target_connect_halt(void)
{
	uint8_t seen = 0;
	int i;

	for (i = 0; i < 50; i++)
	{
		if (swd_init_debug())
		{
			seen = 1;
			if (swd_halt_core())
			{
				return 1;
			}
		}
		delaymS(1);
	}
	return seen ? 2 : 0;
}

/* ================= automatic burning ================= */

static uint32_t auto_now_ms(void)
{
	return (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
}

static uint8_t auto_has_files(void)
{
	return (app_data.bin_name[0] != '\0' && app_data.firmware_name[0] != '\0');
}

/* 1 = a chip answered on SWD, 0 = nothing in the socket */
static uint8_t auto_probe(void)
{
	uint8_t ok = 0;
	int i;

	swd_off();
	TARGET_PWR_ON;
	/* same timing as the burn path: start looking right at power-on,
	   because the target application may disable SWD within a few ms */
	for(i = 0; i < 50; i++)
	{
		if(swd_init_debug())
		{
			ok = 1;
			break;
		}
		delaymS(1);
	}
	swd_off();
	TARGET_PWR_OFF;
	return ok;
}

static void auto_start_burn(void)
{
	g_burn_done = 0;
	g_burn_ok = 0;
	g_present_seen = 0;
	g_absent_seen = 0;
	g_auto_st = AUTO_ST_BURN;
	app_data.mode = 4;
	OLED_Clear();
	oled.Refresh_flag = 1;
	key_val.mesg = 0;
}

/* Called from mode2() only. Returns with app_data.mode changed to 4 when a
   burn has to start. */
static void auto_standby_tick(void)
{
	uint32_t now = auto_now_ms();

	if(!auto_has_files())
	{
		g_auto_st = AUTO_ST_WAIT;
		g_present_seen = 0;
		return;
	}

	switch(g_auto_st)
	{
		case AUTO_ST_WAIT:
			if(now - g_auto_tprobe < AUTO_PROBE_MS)
				return;
			g_auto_tprobe = now;
			if(auto_probe())
			{
				if(++g_present_seen >= AUTO_PRESENT_CNT)
					auto_start_burn();
			}
			else g_present_seen = 0;
			break;

		case AUTO_ST_BURN:
			/* mode4 returned without reporting (key pressed during the burn) */
			g_burn_ok = 0;
			g_auto_st = AUTO_ST_RESULT;
			g_auto_t0 = now;
			oled.Refresh_flag = 1;
			break;

		case AUTO_ST_RESULT:
			if(now - g_auto_t0 >= (g_burn_ok ? AUTO_OK_MS : AUTO_FAIL_MS))
			{
				g_auto_st = AUTO_ST_HOLD;
				g_absent_seen = 0;
				g_auto_tprobe = now;
				oled.Refresh_flag = 1;
			}
			break;

		default:	/* AUTO_ST_HOLD: wait for the chip to be taken out */
			if(now - g_auto_t0 < AUTO_HOLD_MIN_MS)
				return;
			if(now - g_auto_tprobe < AUTO_PROBE_MS)
				return;
			g_auto_tprobe = now;
			if(!auto_probe())
			{
				if(++g_absent_seen >= AUTO_ABSENT_CNT)
				{
					g_absent_seen = 0;
					g_present_seen = 0;
					g_auto_st = AUTO_ST_WAIT;
					oled.Refresh_flag = 1;
				}
			}
			else g_absent_seen = 0;
			break;
	}
}

/* Full 320x240 auto screen. File names have separate 16px lines; normal
   Chinese text uses the same thin 32px glyphs as the manual menu. */
static void auto_draw(void)
{
	const char *st, *detail;

	if(!auto_has_files())
	{
		st = "\xD7\xD4\xB6\xAF\x3A\xCE\xB4\xD1\xA1\xCE\xC4\xBC\xFE";
		detail = "";
	}
	else if(g_auto_st == AUTO_ST_WAIT)
	{
		st = "\xD7\xD4\xB6\xAF\x3A\xB5\xC8\xB4\xFD\xD0\xBE\xC6\xAC";
		detail = "";
	}
	else if(g_auto_st == AUTO_ST_BURN)
	{
		st = "\xD7\xD4\xB6\xAF\x3A\xD5\xFD\xD4\xDA\xC9\xD5\xC2\xBC";
		detail = "";
	}
	else if(g_burn_ok)
	{
		st = "\xD7\xD4\xB6\xAF\x3A\xC9\xD5\xC2\xBC\xB3\xC9\xB9\xA6";
		detail = "\xC7\xEB\xC8\xA1\xB3\xF6\xD0\xBE\xC6\xAC";
	}
	else
	{
		st = "\xD7\xD4\xB6\xAF\x3A\xC9\xD5\xC2\xBC\xCA\xA7\xB0\xDC";
		detail = g_last_status[0] ? g_last_status : "\xC7\xEB\xC8\xA1\xB3\xF6\xD0\xBE\xC6\xAC";
	}

    UI_Text(8, 8, st, 32, 1);
    UI_Text(8, 44, "\xC9\xD5\xC2\xBC\xCB\xE3\xB7\xA8", 32, 1);
    UI_Text(8, 76, (const char *)app_data.firmware_name, 16, 1);
    UI_Text(8, 96, "\xC9\xD5\xC2\xBC\xB9\xCC\xBC\xFE", 32, 1);
    UI_Text(8, 128, (const char *)app_data.bin_name, 16, 1);
    show_quota(148);
    if(UI_TextWidth(detail, 32) <= 304)
        UI_Text(8, 184, detail, 32, 1);
    else
        UI_TextBox(8, 184, 304, 32, detail, 16, 1);
    UI_Text(8, 224, "\xB3\xA4\xB0\xB4\xD3\xD2\xBC\xFC\x3A\xCA\xD6\xB6\xAF", 16, 1);
}

void mode4(void)
{
	uint8_t err_flag = 0, is_ext_algo = 0;
	uint32_t ret, i, j;
	uint32_t ulReadNum, bin_size;
	uint32_t opt_prog_addr = 0, opt_base_addr = 0, opt_byte_size = 0;
	uint8_t opt_failed = 0;
	uint8_t opt_state = 0;		/* 0=未加密 1=已锁(读不回) 2=读回值不对 3=读到加密值 */
	error_t init_err = ERROR_SUCCESS;
	uint32_t init_diag = 0;
	uint32_t init_step = 0xFFFF;

	/* auto mode: mode4() runs once per chip, so start from a clean slate */
	if(g_auto_en)
		g_burn_done = 0;

	if(key_val.mesg == 1)
	{
		switch(key_val.key)
		{
			case key_left:
			case key_l_left:
			case key_rep_left:
			case key_right:
			case key_l_right:
			case key_rep_right:
			case key_mid:
				if(g_burn_done)             /* 结果页: 单击任一键返回选择页 */
				{
					app_data.mode = 2;
					g_burn_done = 0;
					OLED_Clear();
					oled.Refresh_flag = 1;
					key_val.mesg = 0;
					return;
				}
				break;
			case key_d_mid:                     /* 双击 -> 返回选择页 */
				app_data.mode = 2;
				g_burn_done = 0;
				OLED_Clear();
				oled.Refresh_flag = 1;
				key_val.mesg = 0;
				return;
			case key_l_mid:                     /* 长按 -> 模式选择页 */
				app_data.mode = 0;
				app_data.head = 0;
				g_burn_done = 0;
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

	if(g_burn_done)                             /* 已烧录完, 停留在结果页等待按键 */
		return;
	g_burn_done = 1;
	g_burn_ok = 0;

    if(burn_quota_fault() || (burn_quota_enabled() && !burn_quota_remaining()))
    {
quota_rejected:
        OLED_Clear();
        UI_Text(8, 8, "\xC9\xD5\xC2\xBC\xCA\xA7\xB0\xDC", 32, 1);
        show_download_status(burn_quota_fault() ? "\xB4\xCE\xCA\xFD\xBC\xC7\xC2\xBC\xD2\xEC\xB3\xA3" : "\xCA\xA3\xD3\xE0\xB4\xCE\xCA\xFD\xB2\xBB\xD7\xE3");
        if(g_auto_en)
        {
            g_auto_st = AUTO_ST_RESULT; g_auto_t0 = auto_now_ms();
            app_data.mode = 2; oled.Refresh_flag = 1;
        }
        return;
    }

    OLED_Clear();
    UI_Text(8, 8, "\xBF\xAA\xCA\xBC\xC9\xD5\xC2\xBC", 32, 1);
    sprintf((char *)D, "\xCB\xE3\xB7\xA8\x3A\x25\x73", app_data.firmware_name);
    UI_Text(8, 56, (char *)D, 16, 1);
    sprintf((char *)D, "\xB9\xCC\xBC\xFE\x3A\x25\x73", app_data.bin_name);
    UI_Text(8, 80, (char *)D, 16, 1);
    show_download_progress(0);

	if(app_data.bin_name[0] != '\0' && app_data.firmware_name[0] != '\0')
	{
        if(burn_quota_begin() != BURN_QUOTA_OK) goto quota_rejected;
		show_download_status("\xD5\xFD\xD4\xDA\xC1\xAC\xBD\xD3\xD0\xBE\xC6\xAC");
		/* Power-cycle target and grab SWD before programming */
		swd_off();
		TARGET_PWR_OFF;
		delaymS(200);
		TARGET_PWR_ON;
		/* Retry SWD: grab CPU before firmware disables SWD */
		{
			uint8_t c = target_connect_halt();
			if (c != 1) {
				show_download_status(c == 2 ? "\xD0\xBE\xC6\xAC\xCE\xDE\xB7\xA8\xD4\xDD\xCD\xA3" : "\x53\x57\x44\xC1\xAC\xBD\xD3\xCA\xA7\xB0\xDC");
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
			show_download_status("\x45\x52\x30\x3A\xCB\xE3\xB7\xA8\xB4\xF2\xBF\xAA\xCA\xA7\xB0\xDC");
			goto download_return;
		}
		if(err_flag == 1)
		{
			show_download_status("\xD5\xFD\xD4\xDA\xBC\xD3\xD4\xD8\xCB\xE3\xB7\xA8");
			OLED_Refresh();
			/* reset diag so a stale value from a previous attempt can't mislead */
			g_last_syscall_r0 = 0;
			g_diag_ctrl_stat = 0xFFFFFFFF;
			g_diag_hcsr = 0xFFFFFFFF;
			g_diag_step = 0xFFFF;
			init_err = target_flash_init((program_target_t *)DATA_BUFFER,
			                             ((program_target_t *)DATA_BUFFER)->flash_addr);
			if(init_err == ERROR_SUCCESS)
				err_flag = 2;
			else
			{
				/* keep the diag before uninit overwrites it */
				init_diag = g_last_syscall_r0;
				init_step = g_diag_step;
				target_flash_uninit((program_target_t *)DATA_BUFFER);
			}
		}
		else
		{			
			show_download_status("\x45\x52\x31\x3A\xCB\xE3\xB7\xA8\xB6\xC1\xC8\xA1\xCA\xA7\xB0\xDC");
			goto download_return;
		}
		if(err_flag == 2)
		{
			show_download_status("\xD5\xFD\xD4\xDA\xB2\xC1\xB3\xFD\xD0\xBE\xC6\xAC");
			OLED_Refresh();
			if(target_flash_erase_chip((program_target_t *)DATA_BUFFER) == ERROR_SUCCESS)
			{
				err_flag = 3;
				/* Mass erase also drops the read protection, but that only
				   takes effect after the target restarts. Programming before
				   the restart is rejected (ER6), so power-cycle and re-init
				   the algorithm before writing. */
				show_download_status("\xD5\xFD\xD4\xDA\xD6\xD8\xD0\xC2\xB3\xF5\xCA\xBC\xBB\xAF");
				OLED_Refresh();
				swd_off();
				TARGET_PWR_OFF;
				delaymS(100);
				TARGET_PWR_ON;
				delaymS(20);
				if(target_connect_halt() != 1 ||
				   target_flash_init((program_target_t *)DATA_BUFFER,
				                     ((program_target_t *)DATA_BUFFER)->flash_addr) != ERROR_SUCCESS)
				{
					show_download_error("\x45\x52\x37\x3A\xD6\xD8\xD0\xC2\xB3\xF5\xCA\xBC\xBB\xAF\xCA\xA7\xB0\xDC", g_last_syscall_r0, g_diag_step, 0);
					goto download_return;
				}
			}
			else
			{
				target_flash_uninit((program_target_t *)DATA_BUFFER);
				sprintf((char *)D, "\xB2\xC1\xB3\xFD\xCA\xA7\xB0\xDC\x3A\x25\x30\x34\x58", (unsigned int)(g_erase_diag & 0xFFFF));
				show_download_status((char *)D);
				OLED_Refresh();
				swd_off();
				TARGET_PWR_OFF;
				{ volatile uint32_t _d; for(_d=0; _d<3000000; _d++); }
				goto download_return;
			}
		}
		else 
		{
			switch(init_err)
			{
				case ERROR_RESET:
					show_download_error("\x45\x52\x32\x61\x3A\xB8\xB4\xCE\xBB\xD4\xDD\xCD\xA3\xCA\xA7\xB0\xDC", init_diag, init_step, 0);
					break;
				case ERROR_ALGO_DL:
					show_download_error("\x45\x52\x32\x62\x3A\x53\x52\x41\x4D\xD0\xB4\xC8\xEB\xCA\xA7\xB0\xDC", init_diag, init_step, 0);
					break;
				case ERROR_INIT:
					show_download_error("\x45\x52\x32\x63\x3A\xCB\xE3\xB7\xA8\xB3\xF5\xCA\xBC\xBB\xAF\xCA\xA7\xB0\xDC", init_diag, init_step, 0);
					break;
				default:
				{
					char ebuf[24];
					sprintf(ebuf, "\x45\x52\x32\x3A\xB4\xED\xCE\xF3\x3D\x25\x75", (unsigned)init_err);
					show_download_error(ebuf, init_diag, init_step, 0);
					break;
				}
			}
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
			show_download_status("\x45\x52\x33\x3A\xB2\xC1\xB3\xFD\xCA\xA7\xB0\xDC");
			goto download_return;
		}
		if(err_flag == 4)
		{
			show_download_status("\xD5\xFD\xD4\xDA\xC9\xD5\xC2\xBC\xB9\xCC\xBC\xFE");
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
																			(const uint8_t *)(DATA_BUFFER + 2048), ulReadNum) != ERROR_SUCCESS)
					{
						show_download_error("\x45\x52\x36\x3A\xC9\xD5\xC2\xBC\xCA\xA7\xB0\xDC",
						                    g_last_syscall_r0,
						                    g_diag_step,
						                    ((program_target_t *)DATA_BUFFER)->flash_addr + i);
						break;
					}
					i += ulReadNum;
					j = i;
					j = (uint32_t)(j * 100.0/ bin_size);
								
					show_download_progress((uint8_t)j);
				}
				else 
				{
					show_download_status("\x45\x52\x35\x3A\xB9\xCC\xBC\xFE\xB6\xC1\xC8\xA1\xCA\xA7\xB0\xDC");
					break;
				}
			}

						/* Verify firmware after programming */
			if(i == bin_size)
			{
				show_download_status("\xD5\xFD\xD4\xDA\xD0\xA3\xD1\xE9\xB9\xCC\xBC\xFE");
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
					show_download_status("\xB9\xCC\xBC\xFE\xD0\xA3\xD1\xE9\xCD\xA8\xB9\xFD");
				}
				else
				{
					show_download_status("\xB9\xCC\xBC\xFE\xD0\xA3\xD1\xE9\xCA\xA7\xB0\xDC");
				}
				OLED_Refresh();
				f_close(&fd);
			}

/* OPT programming for extended algorithm */
			if(is_ext_algo && app_data.opt_enable && i == bin_size)
			{
				FIL fd_opt;
				char opt_name[40];
				uint8_t *dot;
				uint32_t opt_read;

				show_download_status("\xD5\xFD\xD4\xDA\xD0\xB4\xC8\xEB\x4F\x50\x54");
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
					/* 缓冲区先填 0xFF: 算法按 opt_size(16B) 写, 文件可能不足,
					   不预填会把上一页固件的残留字节写进 OPT 区(WRP) */
					memset(DATA_BUFFER + 2048, 0xFF, 128);
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
							/* 立刻回读 RDP 判断加密是否真的写进去:
							   读失败(调试口已锁) 或 读到 0x807F 都算成功;
							   读到别的值说明 ProgramOpt 没写进 flash。 */
							uint16_t rdp_w = (uint16_t)(DATA_BUFFER[2048] | ((uint16_t)DATA_BUFFER[2049] << 8));
							if(rdp_w != 0x5AA5)
							{
								uint8_t rb[2];
								if(swd_read_memory(opt_base_addr, rb, 2))
								{
									uint16_t v = (uint16_t)(rb[0] | ((uint16_t)rb[1] << 8));
									opt_state = (v == 0x807F) ? 3 : 2;
								}
								else opt_state = 1;
							}
							show_download_status("\x4F\x50\x54\xD0\xB4\xC8\xEB\xCD\xEA\xB3\xC9");
							}
							else
							{
								show_download_status("\x4F\x50\x54\xD0\xB4\xC8\xEB\xCA\xA7\xB0\xDC");
								opt_failed = 1;
							}
						} /* end of algo_ext scope */
					}
					else
					{
						show_download_status("\x4F\x50\x54\xB6\xC1\xC8\xA1\xCA\xA7\xB0\xDC");
						opt_failed = 1;
					}
				}
				else
				{
					show_download_status("\xC8\xB1\xC9\xD9\x4F\x50\x54\xCE\xC4\xBC\xFE");
					opt_failed = 1;
				}
				OLED_Refresh();
			}

			/* Verify OPT bytes - stage by stage */
			if(is_ext_algo && app_data.opt_enable && i == bin_size && opt_prog_addr && !opt_failed)
			{
				show_download_status("\xD5\xFD\xD4\xDA\xD0\xA3\xD1\xE9\x4F\x50\x54");
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
				show_download_status("\xD5\xFD\xD4\xDA\xB6\xC1\xC8\xA1\x4F\x50\x54\xCE\xC4\xBC\xFE");
				OLED_Refresh();
				if(f_open(&fd_opt2, (TCHAR *)D, FA_READ) == FR_OK)
				{
					memset(DATA_BUFFER + 2048, 0xFF, 128);
					ret = f_read(&fd_opt2, (DATA_BUFFER + 2048), 128, &ulReadNum);
					if(ret == FR_OK && ulReadNum > 0)
					{
						uint16_t rdp_word = (uint16_t)(DATA_BUFFER[2048] | ((uint16_t)DATA_BUFFER[2049] << 8));
						if(ulReadNum >= 2 && rdp_word != 0x5AA5)
						{
							/* RDP 写成加密值后调试口立即被锁, 紧接着读 OPT 必然失败。
							   这不是烧录错误: 读不回来本身就证明加密已经生效。 */
							show_download_status("\x4F\x50\x54\xCD\xEA\xB3\xC9\xB2\xA2\xD2\xD1\xCB\xF8\xB6\xA8");
						}
						else
						{
							show_download_status("\xD5\xFD\xD4\xDA\xBC\xEC\xB2\xE9\x4F\x50\x54");
							OLED_Refresh();
							{
								program_target_ext_t algo_ext;
								/* 只严格校验 RDP 这个半字(offset 0~1)。
								   其余 OPT 字节(USER/DATA/WRP)的读回值会被芯片按
								   "值+反码"规则规范化, 强行逐字节比对只会误报;
								   实测写 0xFFFF 读回 0x00FF 就是这种情况。 */
								uint32_t vlen = (ulReadNum >= 2) ? 2 : ulReadNum;
								algo_ext.sys_call_s = ((program_target_t *)DATA_BUFFER)->sys_call_s;
								algo_ext.program_buffer = ((program_target_t *)DATA_BUFFER)->program_buffer;
								algo_ext.opt_program = opt_prog_addr;
								algo_ext.opt_addr = opt_base_addr;
								algo_ext.opt_size = opt_byte_size;
								if(target_flash_verify_ext(&algo_ext,
								                              opt_base_addr,
								                              (const uint8_t *)(DATA_BUFFER + 2048),
								                              vlen) != ERROR_SUCCESS)
								{
									/* 显示第一个不一致的字节: 偏移 / 期望 / 读回 */
									if(g_vfy_off == 0xFFFFFFFF)
										sprintf((char *)D, "\x4F\x50\x54\xB6\xC1\xC8\xA1\xCA\xA7\xB0\xDC");
									else
										sprintf((char *)D, "OPT +%02lX %02lX>%02lX",
										        (unsigned long)g_vfy_off,
										        (unsigned long)g_vfy_exp,
										        (unsigned long)g_vfy_act);
									show_download_status((char *)D);
									i = 0;
								}
								else
								{
									show_download_status("\x4F\x50\x54\xD0\xA3\xD1\xE9\xCD\xA8\xB9\xFD");
								}
							}
						}
					}
					else
					{
						show_download_status("\x4F\x50\x54\xD0\xA3\xD1\xE9\xB6\xC1\xC8\xA1\xCA\xA7\xB0\xDC");
						opt_failed = 1;
					}
					f_close(&fd_opt2);
				}
				else
				{
					show_download_status("\x4F\x50\x54\xD0\xA3\xD1\xE9\xCE\xC4\xBC\xFE\xD2\xEC\xB3\xA3");
					opt_failed = 1;
				}
				OLED_Refresh();
			}


			swd_set_target_state_hw(RESET_RUN);
			swd_off();
			TARGET_PWR_OFF;
			if(i == bin_size)
			{
				if(opt_failed)
					show_download_status("\xB9\xCC\xBC\xFE\xCD\xEA\xB3\xC9\x2C\x4F\x50\x54\xCA\xA7\xB0\xDC");
				else if(opt_state == 2)
					show_download_status("\xC9\xD5\xC2\xBC\xCD\xEA\xB3\xC9\x2C\x4F\x50\x54\xD2\xEC\xB3\xA3");		/* OPT 写了但 RDP 不对 */
				else if(opt_state == 1 || opt_state == 3)
					show_download_status("\xC9\xD5\xC2\xBC\xB3\xC9\xB9\xA6\x2C\xD2\xD1\xBC\xD3\xC3\xDC");	/* 加密已生效 */
				else
					show_download_status("\xC9\xD5\xC2\xBC\xB3\xC9\xB9\xA6");	/* 不加密 */

				/* only a clean run counts as a success for auto mode */
				if(!opt_failed && opt_state != 2)
					g_burn_ok = 1;

				OLED_Refresh();
				for(volatile uint32_t _d = 0; _d < 500000; _d++);
				swd_set_target_reset(0);
			}
			taskEXIT_CRITICAL();
			f_close(&fd);
		}
		else show_download_status("\x45\x52\x34\x3A\xB9\xCC\xBC\xFE\xB4\xF2\xBF\xAA\xCA\xA7\xB0\xDC");
		
download_return:
        if(burn_quota_finish(g_burn_ok) != BURN_QUOTA_OK)
        {
            g_burn_ok = 0;
            show_download_status("\xB4\xCE\xCA\xFD\xBC\xC7\xC2\xBC\xD2\xEC\xB3\xA3");
        }
        else
            show_quota(208);
		swd_off();
		TARGET_PWR_OFF;
		oled.Refresh_flag = 1;
		if(g_auto_en)
		{
			/* hand the result back to the standby page; it decides when the
			   chip has been taken out and the next one can be burned */
			g_auto_st = AUTO_ST_RESULT;
			g_auto_t0 = auto_now_ms();
			app_data.mode = 2;
			OLED_Clear();
		}
	}
	else
	{
		show_download_status("\xC7\xEB\xCF\xC8\xD1\xA1\xD4\xF1\xCE\xC4\xBC\xFE");
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
			case key_l_left:
			case key_rep_left:
				if(sum)
					index = index == 0 ? sum - 1 : index - 1;
				break;
			case key_right:
			case key_l_right:
			case key_rep_right:
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
        UI_Text(8, 8, app_data.head == 0 ? "\xC9\xD5\xC2\xBC\xCB\xE3\xB7\xA8" : "\xC9\xD5\xC2\xBC\xB9\xCC\xBC\xFE", 32, 1);
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
				j = index / 5;
				if(sum >= j*5 && sum < (j + 1)*5)
				{	
					if(i == index % 5)
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
							/* manual pick on device: program OPT if the file exists (old behaviour) */
							app_data.opt_enable = 1;
						}
						/* Full filename drawn below. */   					//������ʾ����			
						UI_Fill(8, 52 + i * 32, 304, 28, 1);
                        UI_Text(16, 58 + i * 32, FileInfo.fname, 16, 0);  //���Դ����ͷ�����ļ�����ʾ����?
					}
					else
					{
						/* Full filename drawn below. */   					//������ʾ����	
						UI_Text(16, 58 + i * 32, FileInfo.fname, 16, 1);
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
			UI_Text(8, 64, "\xC3\xBB\xD3\xD0\xCE\xC4\xBC\xFE", 32, 1);
		}
        sprintf((char *)D, "%lu/%lu", (unsigned long)(sum ? index + 1 : 0), (unsigned long)sum);
        UI_Text(8, 220, (char *)D, 16, 1);
        OLED_Refresh();
        oled.Refresh_flag = 0;
    }
}

void run_task(void *pvParameters)
{
		uint8_t prepare_algorithm[CB_MAX_NAME + 1];
		uint8_t prepare_program[CB_MAX_NAME + 1];
		uint8_t prepare_opt;

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
#if DAP_ONLINE_ENABLE
		xTaskCreate((TaskFunction_t )usb_task,     	
                (const char*    )"usb_task",   	
                (uint16_t       )USB_STK_SIZE, 
                (void*          )NULL,				
                (UBaseType_t    )USB_TASK_PRIO,	
                (TaskHandle_t*  )&UsbTask_Handler);
#endif
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
#if DAP_ONLINE_ENABLE
		EventGroupHandle = xEventGroupCreate();
#endif
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
            custombulk_quota_service(&usb_hid);
			custombulk_prepare_service(&usb_hid);
			if(custombulk_take_prepare_complete(prepare_algorithm,
			                                    prepare_program,
			                                    &prepare_opt))
			{
				apply_prepare_download(prepare_algorithm,
				                       prepare_program,
				                       prepare_opt);
			}
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

#if DAP_ONLINE_ENABLE
void usb_task(void *pvParameters)
{
	while(1)
	{
		DAP_Thread(NULL);
	}
}

#endif /* DAP_ONLINE_ENABLE */
void led_task(void *pvParameters)
{
#if CDC_LED_SELFTEST
	/* 上电自检：CDC_LED 已飞线到 PA9，这里闪 3 下确认接线正确。
	   正常运行时 CDC_LED 不参与指示，只有关机充电时才会轮到它 */
	{
		uint8_t n;
		for(n = 0; n < 3; n++)
		{
			CDC_LED_ON;  vTaskDelay(120);
			CDC_LED_OFF; vTaskDelay(120);
		}
	}
#endif
	while(1)
	{
#if DAP_ONLINE_ENABLE
		if(app_data.hid_led_blink)
			app_data.hid_led_blink = 0, HID_LED_ON;
#endif
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
        UI_Fill(0, 0, 320, 32, 0);
		adc_get_val();
		i = bat_status_get();
		if(i == 0)
		{
			UI_Logo(8, 8, 7);    				//USB
			UI_Text(52, 8, "DIS", 16, 1);
			
			bat = (uint8_t)((val.bat_vol - 3.5)*100/0.5);
			sprintf((char *)D, "%3d%%\0", bat);
			UI_Text(272, 8, (char *)D, 16, 1);
			
			if(bat < 20) UI_Logo(228, 8, 0);
			else if(bat < 40) UI_Logo(228, 8, 1);
			else if(bat < 60) UI_Logo(228, 8, 2);
			else if(bat < 80) UI_Logo(228, 8, 3);
			else UI_Logo(228, 8, 4);
		}
		else
		{
			UI_Logo(8, 8, 6);    								//USB
			UI_Text(52, 8, "CON", 16, 1);
			if(i == 1) 
			{
				UI_Logo(228, 8, 5);
			}
			else
			{
				UI_Logo(228, 8, 4);
				sprintf((char *)D, "%3d%%\0", 100);
				UI_Text(272, 8, (char *)D, 16, 1);
			}
		}
		
		sprintf((char *)D, "%3.1fC", val.chip_temp);
//		sprintf((char *)D, "%4.1fC\0", val.bat_vol);
		UI_Text(104, 8, (char *)D, 16, 1);
		adc_trigger();
		if(!status)
			oled.Refresh_flag = 1;
	}
	else
		cnt++;
}


