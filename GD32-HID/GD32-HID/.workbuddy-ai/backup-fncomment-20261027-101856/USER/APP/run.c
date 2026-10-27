/* 中文注释补充（2026-10-26）：此前修改说明：保留原烧录/硬件流程，下方以中文说明相关界面与兼容逻辑。 */
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
volatile uint8_t g_programming_active; /* 只在实际烧录调用期间为1，手动结果页仍可USB通信。 */

/* 在线仿真专用部分：宏为0时不编译；离线SWD/OPT及WinUSB传输继续保留。 */
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
/* 按固定周期扫描按键；上一事件尚未消费时保留它，避免长按事件被下一次扫描覆盖。 */
void key_task(void *pvParameters);

/* 在线仿真专用部分：宏为0时不编译；离线SWD/OPT及WinUSB传输继续保留。 */
#if DAP_ONLINE_ENABLE
EventGroupHandle_t  EventGroupHandle;

extern void DAP_Thread (void *argument);
#endif
extern void DAP_Setup(void);
/* 离线选择页：选择算法/固件/开始烧录；长按右键切换自动与手动，实际烧录统一进入mode4。 */
void mode2(void);
/* 文件选择页：每页五项，文件名16像素完整显示，左右键循环选取。 */
void mode3(void);
/* 统一烧录流程：先检查并预扣次数，再烧录和校验主固件/OPT；成功扣一次，正常失败退回一次。 */
void mode4(void);
/* 显示USB连接CON/DIS、电池图标和电量；按用户要求连接状态保留英文缩写。 */
void show_top(uint8_t status);
uint8_t D[64] = {0};

/* ---- 自动烧录（自动模式）----
   WAIT   : 轮询检测座子，直到有芯片响应SWD
   BURN   : 由mode4()执行正常烧录流程
   RESULT : 显示OK / FAIL一段时间
   HOLD   : 结果留在屏幕上，直到芯片被取出
   芯片检测方式是给座子上电并等待SWD应答，与烧录流程完全一致，
   因此能烧录的芯片一定能被检测到。 */
#define AUTO_PROBE_MS       200     /* 两次检测之间的最小间隔 */
#define AUTO_PRESENT_CNT    2       /* 连续检测到几次才判定芯片在位 */
#define AUTO_ABSENT_CNT     2       /* 连续丢失几次才判定芯片已取出 */
#define AUTO_HOLD_MIN_MS    600     /* HOLD状态至少停留这么久 */
#define AUTO_OK_MS          1500    /* 成功时结果停留时间 */
#define AUTO_FAIL_MS        3000    /* 失败时结果停留时间 */

enum { AUTO_ST_WAIT = 0, AUTO_ST_BURN, AUTO_ST_RESULT, AUTO_ST_HOLD };

static uint8_t  g_auto_en      = 1;             /* 0=手动模式，1=自动模式 */
static uint8_t  g_auto_st      = AUTO_ST_WAIT;
static uint8_t  g_absent_seen  = 0;             /* 连续检测不到芯片的次数 */
static uint8_t  g_present_seen = 0;             /* 连续检测到芯片的次数 */
static uint8_t  g_burn_done    = 0;             /* mode4：本颗芯片已处理过 */
static uint8_t  g_burn_ok      = 0;             /* 上一次烧录的结果 */
static uint32_t g_auto_t0      = 0;             /* 进入当前状态时的系统节拍 */
static uint32_t g_auto_tprobe  = 0;             /* 上一次检测时的系统节拍 */

/* 自动检测芯片：连续检测到芯片后烧录，显示结果后等待芯片取出，避免同一颗芯片重复烧录。 */
static void auto_standby_tick(void);
/* 绘制自动烧录待机/烧录/结果页；中文32像素，算法名与固件名16像素，次数独占一行。 */
static void auto_draw(void);
/* 进入一次自动烧录：清除旧结果，转到mode4统一进行次数检查和烧录。 */
static void auto_start_burn(void);
/* 检查算法和固件是否都已选择；缺少任一文件时禁止自动开始。 */
static uint8_t auto_has_files(void);
/* 将FreeRTOS系统节拍转换为毫秒，用于自动检测和结果停留计时。 */
static uint32_t auto_now_ms(void);

/* 最后一次写入OLED状态行的文字；保留它，使mode4返回后待机页
   仍能重复显示失败原因 */
static char g_last_status[64] = {0};

/* 当前成功导入的主固件SUM32；有效标记独立保存，校验值00000000也有效。 */
static uint32_t g_program_checksum;
static uint8_t g_program_checksum_valid;

/* 固件被改写、取消或更换后清除旧校验值；下次只刷新Checksum文字行。 */
static void invalidate_program_checksum(void)
{
    g_program_checksum_valid = 0;
    oled.Refresh_flag = 1;
}

/* 次数占满一整行32像素；按键提示移到单独的16像素底栏。 */
/* 显示剩余次数：三位数字补零或“不限”。独立栈缓冲区保护文件路径D，避免OPT校验打开错误文件。 */
static void show_quota(uint16_t y)
{
    /* 次数文字使用独立栈缓冲区；D还保存OPT路径/诊断文本，刷新界面不能覆盖它。 */
    char quota_text[32];
    if(burn_quota_fault())
        strcpy(quota_text, "\xB4\xCE\xCA\xFD\xBC\xC7\xC2\xBC\xD2\xEC\xB3\xA3"); /* 显示文字：次数记录异常 */
    else if(burn_quota_enabled())
        sprintf(quota_text, "\xCA\xA3\xD3\xE0\xB4\xCE\xCA\xFD\x3A\x25\x30\x33\x6C\x75\xB4\xCE", (unsigned long)burn_quota_remaining()); /* 显示文字：剩余次数:%03lu次（%03lu为三位次数，前面补0） */
    else
        strcpy(quota_text, "\xCA\xA3\xD3\xE0\xB4\xCE\xCA\xFD\x3A\xB2\xBB\xCF\xDE"); /* 显示文字：剩余次数:不限 */
    UI_Field(5, 8, y, 304, 16, quota_text, 16, 1); /* 次数变化只覆盖变化数字。 */
}

/* 显示文字：Checksum: 01234567。SUM32为最终BIN所有字节累加的32位值，
 * 固件未成功导入或已更换时显示Checksum: --------；字号16像素，数字前补0。
 * 独立栈缓冲保护OPT文件路径D；复用空闲字段13，只绘制变化字符。 */
static void show_checksum(uint16_t y)
{
    char checksum_text[24];
    if(g_program_checksum_valid && app_data.bin_name[0])
        sprintf(checksum_text, "Checksum: %08lX", (unsigned long)g_program_checksum);
    else
        strcpy(checksum_text, "Checksum: --------");
    UI_Field(13, 8, y, 304, 16, checksum_text, 16, 1);
}

/* 接收上位机导入的算法、固件与OPT开关，切入烧录页并保持USB后台连接；不重置次数。 */
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
    /* 仅在设备回读校验并确认导入后提交校验值；算法/OPT不计入SUM32。 */
    g_program_checksum = custombulk_prepared_checksum();
    g_program_checksum_valid = 1;
    app_data.mode = 2;
    app_data.head = 2;
    key_val.mesg = 0;
    /* 导入完成只更新文件字段；USB保留连接，次数不重置，重新开始芯片检测。 */
    g_auto_st = AUTO_ST_WAIT; g_present_seen = 0; g_absent_seen = 0;
    g_auto_tprobe = auto_now_ms();
    oled.Refresh_flag = 1;
}

//关闭所有中断
void INTX_DISABLE(void)
{		  
	__ASM volatile("cpsid i");
}
//开启所有中断
void INTX_ENABLE(void)
{
	__ASM volatile("cpsie i");		  
}

/* 启动界面：先恢复掉电保存的次数，再挂载文件系统和加载开机图标。 */
void start_page(void)
{
    uint8_t ret;
    uint32_t ulReadNum;
    const char *status;
    burn_quota_init();
    ret = f_mount(&fs, "0:", 1);
    status = "\xCE\xC4\xBC\xFE\xCF\xB5\xCD\xB3\xD2\xEC\xB3\xA3"; /* 显示文字：文件系统异常 */
    if(ret == FR_OK)
    {
        status = "\xCD\xBC\xB1\xEA\xBC\xD3\xD4\xD8\xCA\xA7\xB0\xDC"; /* 显示文字：图标加载失败 */
        if(f_open(&fd, "0:/system/logo.bin", FA_READ) == FR_OK)
        {
            /* 复用已有的4096字节Flash工作区，不额外分配logo帧缓存。 */
            ret = f_read(&fd, DATA_BUFFER, 1024, &ulReadNum);
            if(ret == FR_OK && ulReadNum == 1024)
            {
                UI_BootLogo(DATA_BUFFER);
                status = "\xCD\xBC\xB1\xEA\xD2\xD1\xBC\xD3\xD4\xD8"; /* 显示文字：图标已加载 */
            }
            f_close(&fd);
        }
    }
    UI_Text(8, 196, status, 32, 1);
    vTaskDelay(1000);
    OLED_Clear();
}
/* 原模式菜单和文件传输页面已移除；文件传输保留为烧录页的USB后台服务。 */
/* 长按中键关机：保留原充电指示，避免移除一级菜单后无法关机。 */
static void power_down(void)
{
    uint8_t step = 0;
    OLED_DisPlay_Off(); GD25QXX_PowerDown(); usbd_disconnect(&usb_hid);
    vTaskDelete(LedTask_Handler); POWER_OFF;
    for(;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if(bat_status_get() == 1)
        {
            HID_LED_OFF; MSC_LED_OFF; CDC_LED_OFF;
            if(step == 0) HID_LED_ON;
            else if(step == 1) MSC_LED_ON;
            else CDC_LED_ON;
            step = (step + 1) % 3;
        }
        else { HID_LED_ON; MSC_LED_ON; CDC_LED_ON; }
    }
}

/* mode2: 离线下载选择页 (算法 / 文件 / 开始烧录), 只负责选择, 烧录在 mode4 */
/* 离线选择页：选择算法/固件/开始烧录；长按右键切换自动与手动，实际烧录统一进入mode4。 */
void mode2(void)
{
	/* 自动模式始终烧录已加载的算法/固件组合；选文件在手动模式下进行
	   （长按右键切换自动/手动） */
	if(g_auto_en)
		app_data.head = 2;

	if(key_val.mesg == 1)
	{
		switch(key_val.key)
		{
            case key_l_mid: /* 一级菜单已删除，在待机页长按中键直接关机。 */
                if(!custombulk_is_busy()) power_down();
                key_val.mesg = 0; return;
            case key_d_mid: /* 双击留在烧录选择页，不再返回已删除的菜单。 */
                key_val.mesg = 0; return;
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
                /* 传输尚未完成时禁止选择/开始，避免使用正在改写的文件。 */
                if(custombulk_is_busy()) { key_val.mesg = 0; return; }
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

	/* 自动模式：监视座子，一检测到芯片就开始烧录 */
	if(g_auto_en && !custombulk_is_busy())
	{
		auto_standby_tick();
		if(app_data.mode != 2)
			return;
	}

	if(oled.Refresh_flag == 1)
	{
        UI_EnterPage(g_auto_en ? 2 : 6); /* 自动/手动布局切换才清屏。 */
		if(g_auto_en)
		{
			auto_draw();
		}
		else
		{
            UI_Field(1, 8, 8, 256, 32, "\xC9\xD5\xC2\xBC\xCB\xE3\xB7\xA8", 32, 1); /* 显示文字：烧录算法 */
            UI_Field(2, 8, 48, 304, 16, (const char *)app_data.firmware_name, 16, 1);
            UI_Field(3, 8, 72, 256, 32, "\xC9\xD5\xC2\xBC\xB9\xCC\xBC\xFE", 32, 1); /* 显示文字：烧录固件 */
            UI_Field(4, 8, 112, 304, 16, (const char *)app_data.bin_name, 16, 1);
            UI_Field(6, 8, 176, 256, 32, "\xBF\xAA\xCA\xBC\xC9\xD5\xC2\xBC", 32, 1); /* 显示文字：开始烧录 */
            /* 焦点变化只更新三处箭头，字段缓存自动擦除旧箭头。 */
            UI_Field(0, 272, 8, 32, 32, app_data.head == 0 ? "<-" : "", 32, 1);
            UI_Field(8, 272, 72, 32, 32, app_data.head == 1 ? "<-" : "", 32, 1);
            UI_Field(14, 272, 176, 32, 32, app_data.head == 2 ? "<-" : "", 32, 1);
            show_quota(136);
            UI_Field(7, 8, 224, 160, 16, "\xB3\xA4\xB0\xB4\xD3\xD2\xBC\xFC\x3A\xD7\xD4\xB6\xAF", 16, 1); /* 显示文字：长按右键:自动 */
		}
		OLED_Refresh();
		oled.Refresh_flag = 0;
	}
    show_quota(g_auto_en ? 148 : 136);
    show_checksum(g_auto_en ? 166 : 156); /* 次数下方独立16像素校验行，保留全屏布局。 */
    show_top(0);
}

/* 显示烧录进度条，percent为0～100，使用320×240原生像素坐标。 */
static void show_download_progress(uint8_t percent)
{
    UI_Progress(112, percent);
}

/* 显示并保存烧录状态；长文字自动换行，下方同步显示剩余次数，供自动结果页复用。 */
static void show_download_status(const char *status)
{
    uint16_t size = UI_TextWidth(status, 32) <= 576 ? 32 : 16;
    UI_Field(0, 8, 144, 304, 64, status, (uint8_t)size, 1); /* 状态只更新变化字及缩短的尾部。 */
    strncpy(g_last_status, status, sizeof(g_last_status) - 1);
    g_last_status[sizeof(g_last_status) - 1] = '\0';
    show_quota(208);
    show_checksum(224); /* 烧录及结果页底行显示主固件校验值，避免与两行状态重叠。 */
}

/* 原生LCD错误页：32像素标题自动换行，16像素完整显示寄存器值。 */
/* 显示烧录失败原因、步骤、地址和SWD诊断寄存器，底部显示剩余次数。 */
static void show_download_error(const char *status, uint32_t diag, uint32_t step, uint32_t addr)
{
    OLED_Clear();
    UI_TextBox(8, 8, 304, 64, status, 32, 1);
    sprintf((char *)D, "\xB2\xBD\xD6\xE8\x3D\x25\x30\x32\x6C\x58\x20\x72\x30\x3D\x25\x6C\x58", (unsigned long)step, (unsigned long)diag); /* 显示文字：步骤=%02lX r0=%lX */
    UI_Text(8, 88, (char *)D, 16, 1);
    strncpy(g_last_status, status, sizeof(g_last_status) - 1);
    g_last_status[sizeof(g_last_status) - 1] = '\0';
    if(addr)
    {
        sprintf((char *)D, "\xB5\xD8\xD6\xB7\x3D\x25\x30\x38\x6C\x58", (unsigned long)addr); /* 显示文字：地址=%08lX */
        UI_Text(8, 116, (char *)D, 16, 1);
    }
    sprintf((char *)D, "CTRL/STAT=%08lX", (unsigned long)g_diag_ctrl_stat);
    UI_Text(8, 144, (char *)D, 16, 1);
    sprintf((char *)D, "DHCSR=%08lX", (unsigned long)g_diag_hcsr);
    UI_Text(8, 172, (char *)D, 16, 1);
    show_quota(208);
    show_checksum(224); /* 诊断页保留主固件Checksum，独立于OPT与诊断寄存器。 */
}

/* 尽快连接并暂停内核。
   0=DP无响应，1=已暂停，2=DP有响应但CPU停不下来 */
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

/* ================= 自动烧录 ================= */

/* 将FreeRTOS系统节拍转换为毫秒，用于自动检测和结果停留计时。 */
static uint32_t auto_now_ms(void)
{
	return (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
}

/* 检查算法和固件是否都已选择；缺少任一文件时禁止自动开始。 */
static uint8_t auto_has_files(void)
{
	return (app_data.bin_name[0] != '\0' && app_data.firmware_name[0] != '\0');
}

/* 短暂上电并尝试SWD连接以检测芯片：1=SWD有芯片应答，0=座子为空；
 * 立即探测以抢在目标程序关闭调试口之前连接。 */
static uint8_t auto_probe(void)
{
	uint8_t ok = 0;
	int i;

	swd_off();
	TARGET_PWR_ON;
	/* 时序与烧录流程一致：上电瞬间就开始探测，
	   因为目标程序可能在几毫秒内关闭SWD */
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

/* 进入一次自动烧录：清除旧结果，转到mode4统一进行次数检查和烧录。 */
static void auto_start_burn(void)
{
	g_burn_done = 0;
	g_burn_ok = 0;
	g_present_seen = 0;
	g_absent_seen = 0;
	g_auto_st = AUTO_ST_BURN;
	app_data.mode = 4; /* 烧录页首次进入时统一清屏，避免转换时重复清屏。 */
	oled.Refresh_flag = 1;
	key_val.mesg = 0;
}

/* 只由mode2()调用；需要开始烧录时返回前把app_data.mode改成4。
 * 自动检测芯片：连续检测到芯片后烧录，显示结果后等待芯片取出，避免同一颗芯片重复烧录。 */
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
			/* mode4未上报结果就返回了（烧录过程中按了按键） */
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

		default:	/* AUTO_ST_HOLD：等待芯片被取出 */
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

/* 绘制整屏320×240的自动烧录页：算法名/固件名各占一行16像素，
 * 其余中文与手动菜单一致使用细32像素字模。 */
static void auto_draw(void)
{
	const char *st, *detail;

	if(!auto_has_files())
	{
		st = "\xD7\xD4\xB6\xAF\x3A\xCE\xB4\xD1\xA1\xCE\xC4\xBC\xFE"; /* 显示文字：自动:未选文件 */
		detail = "";
	}
	else if(g_auto_st == AUTO_ST_WAIT)
	{
		st = "\xD7\xD4\xB6\xAF\x3A\xB5\xC8\xB4\xFD\xD0\xBE\xC6\xAC"; /* 显示文字：自动:等待芯片 */
		detail = "";
	}
	else if(g_auto_st == AUTO_ST_BURN)
	{
		st = "\xD7\xD4\xB6\xAF\x3A\xD5\xFD\xD4\xDA\xC9\xD5\xC2\xBC"; /* 显示文字：自动:正在烧录 */
		detail = "";
	}
	else if(g_burn_ok)
	{
		st = "\xD7\xD4\xB6\xAF\x3A\xC9\xD5\xC2\xBC\xB3\xC9\xB9\xA6"; /* 显示文字：自动:烧录成功 */
		detail = "\xC7\xEB\xC8\xA1\xB3\xF6\xD0\xBE\xC6\xAC"; /* 显示文字：请取出芯片 */
	}
	else
	{
		st = "\xD7\xD4\xB6\xAF\x3A\xC9\xD5\xC2\xBC\xCA\xA7\xB0\xDC"; /* 显示文字：自动:烧录失败 */
		detail = g_last_status[0] ? g_last_status : "\xC7\xEB\xC8\xA1\xB3\xF6\xD0\xBE\xC6\xAC"; /* 显示文字：请取出芯片 */
	}

    UI_Field(0, 8, 8, 304, 32, st, 32, 1);
    UI_Field(1, 8, 44, 304, 32, "\xC9\xD5\xC2\xBC\xCB\xE3\xB7\xA8", 32, 1); /* 显示文字：烧录算法 */
    UI_Field(2, 8, 76, 304, 16, (const char *)app_data.firmware_name, 16, 1);
    UI_Field(3, 8, 96, 304, 32, "\xC9\xD5\xC2\xBC\xB9\xCC\xBC\xFE", 32, 1); /* 显示文字：烧录固件 */
    UI_Field(4, 8, 128, 304, 16, (const char *)app_data.bin_name, 16, 1);
    show_quota(148);
    show_checksum(166); /* 自动待机/结果页：校验行位于次数与失败原因之间。 */
    UI_Field(6, 8, 184, 304, 32, detail, UI_TextWidth(detail, 32) <= 304 ? 32 : 16, 1);
    UI_Field(7, 8, 224, 160, 16, "\xB3\xA4\xB0\xB4\xD3\xD2\xBC\xFC\x3A\xCA\xD6\xB6\xAF", 16, 1); /* 显示文字：长按右键:手动 */
}

/* 统一烧录流程：先检查并预扣次数，再烧录和校验主固件/OPT；成功扣一次，正常失败退回一次。 */
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

	/* 自动模式：mode4()每颗芯片只跑一次，因此从干净状态开始 */
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
						oled.Refresh_flag = 1;
					key_val.mesg = 0;
					return;
				}
				break;
			case key_d_mid:                     /* 双击 -> 返回选择页 */
				app_data.mode = 2;
				g_burn_done = 0;
				oled.Refresh_flag = 1;
				key_val.mesg = 0;
				return;
			case key_l_mid:                     /* 长按 -> 烧录选择页 */
				app_data.mode = 2;
				app_data.head = 0;
				g_burn_done = 0;
				oled.Refresh_flag = 1;
				key_val.mesg = 0;
				return;
			default:
				break;
		}
		key_val.mesg = 0;
	}

    if(g_burn_done) /* 手动结果页允许后台设置次数，只刷新次数行。 */
    { show_quota(208); show_checksum(224); return; } /* 结果页仅局刷次数与校验值。 */
	g_burn_done = 1;
	g_burn_ok = 0;

    if(burn_quota_fault() || (burn_quota_enabled() && !burn_quota_remaining()))
    {
quota_rejected:
        OLED_Clear();
        UI_Text(8, 8, "\xC9\xD5\xC2\xBC\xCA\xA7\xB0\xDC", 32, 1); /* 显示文字：烧录失败 */
        show_download_status(burn_quota_fault() ? "\xB4\xCE\xCA\xFD\xBC\xC7\xC2\xBC\xD2\xEC\xB3\xA3" : "\xCA\xA3\xD3\xE0\xB4\xCE\xCA\xFD\xB2\xBB\xD7\xE3"); /* 显示文字：次数记录异常；或 剩余次数不足 */
        if(g_auto_en)
        {
            g_auto_st = AUTO_ST_RESULT; g_auto_t0 = auto_now_ms();
            app_data.mode = 2; oled.Refresh_flag = 1;
        }
        return;
    }

    OLED_Clear();
    UI_Text(8, 8, "\xBF\xAA\xCA\xBC\xC9\xD5\xC2\xBC", 32, 1); /* 显示文字：开始烧录 */
    sprintf((char *)D, "\xCB\xE3\xB7\xA8\x3A\x25\x73", app_data.firmware_name); /* 显示文字：算法:%s */
    UI_Text(8, 56, (char *)D, 16, 1);
    sprintf((char *)D, "\xB9\xCC\xBC\xFE\x3A\x25\x73", app_data.bin_name); /* 显示文字：固件:%s */
    UI_Text(8, 80, (char *)D, 16, 1);
    show_download_progress(0);

	if(app_data.bin_name[0] != '\0' && app_data.firmware_name[0] != '\0')
	{
        if(burn_quota_begin() != BURN_QUOTA_OK) goto quota_rejected;
		show_download_status("\xD5\xFD\xD4\xDA\xC1\xAC\xBD\xD3\xD0\xBE\xC6\xAC"); /* 显示文字：正在连接芯片 */
		/* 烧录前先给目标重新上电并抢占SWD */
		swd_off();
		TARGET_PWR_OFF;
		delaymS(200);
		TARGET_PWR_ON;
		/* 重试SWD：赶在固件关闭SWD之前抓住CPU */
		{
			uint8_t c = target_connect_halt();
			if (c != 1) {
				show_download_status(c == 2 ? "\xD0\xBE\xC6\xAC\xCE\xDE\xB7\xA8\xD4\xDD\xCD\xA3" : "\x53\x57\x44\xC1\xAC\xBD\xD3\xCA\xA7\xB0\xDC"); /* 显示文字：芯片无法暂停；或 SWD连接失败 */
				swd_off();
				TARGET_PWR_OFF;
				goto download_return;
			}
		}

		err_flag = 0;
		sprintf((char *)D, "0:/download/firmware/%s\0", app_data.firmware_name);
		ret = f_open(&fd, (TCHAR *)D, FA_READ);
		if(ret == FR_OK)								// 算法文件读取成功
		{
			uint32_t fw_size = f_size(&fd);
			ret = f_read(&fd, DATA_BUFFER, 2048, &ulReadNum);
			if(ret == FR_OK && ulReadNum != 0)
			{
				err_flag = 1;
				/* 检测是否为带OPT支持的扩展格式 */
				if(fw_size == ((program_target_t *)DATA_BUFFER)->algo_size + PROG_TARGET_EXT_HDR_SIZE)
				{
					is_ext_algo = 1;
					/* 先保存OPT字段，再移动algo_blob以对齐program_target_t */
					opt_prog_addr = ((program_target_ext_t *)DATA_BUFFER)->opt_program;
					opt_base_addr = ((program_target_ext_t *)DATA_BUFFER)->opt_addr;
					opt_byte_size = ((program_target_ext_t *)DATA_BUFFER)->opt_size;
					/* algo_blob从偏移68移到56，使program_target_t强制转换成立 */
					memmove(DATA_BUFFER + 56, DATA_BUFFER + 68, ((program_target_t *)DATA_BUFFER)->algo_size);
				}
			}
			f_close(&fd);
		}
		else 
		{
			show_download_status("\x45\x52\x30\x3A\xCB\xE3\xB7\xA8\xB4\xF2\xBF\xAA\xCA\xA7\xB0\xDC"); /* 显示文字：ER0:算法打开失败 */
			goto download_return;
		}
		if(err_flag == 1)
		{
			show_download_status("\xD5\xFD\xD4\xDA\xBC\xD3\xD4\xD8\xCB\xE3\xB7\xA8"); /* 显示文字：正在加载算法 */
			OLED_Refresh();
			/* 清空诊断值，避免上次尝试的残留值误导判断 */
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
				/* 在uninit覆盖之前先把诊断值保存下来 */
				init_diag = g_last_syscall_r0;
				init_step = g_diag_step;
				target_flash_uninit((program_target_t *)DATA_BUFFER);
			}
		}
		else
		{			
			show_download_status("\x45\x52\x31\x3A\xCB\xE3\xB7\xA8\xB6\xC1\xC8\xA1\xCA\xA7\xB0\xDC"); /* 显示文字：ER1:算法读取失败 */
			goto download_return;
		}
		if(err_flag == 2)
		{
			show_download_status("\xD5\xFD\xD4\xDA\xB2\xC1\xB3\xFD\xD0\xBE\xC6\xAC"); /* 显示文字：正在擦除芯片 */
			OLED_Refresh();
			if(target_flash_erase_chip((program_target_t *)DATA_BUFFER) == ERROR_SUCCESS)
			{
				err_flag = 3;
				/* 全片擦除同时会解除读保护，但该动作要等目标重新上电才生效。
				   未重新上电就编程会被拒绝（ER6），因此写之前先断电重上电
				   并重新初始化算法。 */
				show_download_status("\xD5\xFD\xD4\xDA\xD6\xD8\xD0\xC2\xB3\xF5\xCA\xBC\xBB\xAF"); /* 显示文字：正在重新初始化 */
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
					show_download_error("\x45\x52\x37\x3A\xD6\xD8\xD0\xC2\xB3\xF5\xCA\xBC\xBB\xAF\xCA\xA7\xB0\xDC", g_last_syscall_r0, g_diag_step, 0); /* 显示文字：ER7:重新初始化失败 */
					goto download_return;
				}
			}
			else
			{
				target_flash_uninit((program_target_t *)DATA_BUFFER);
				sprintf((char *)D, "\xB2\xC1\xB3\xFD\xCA\xA7\xB0\xDC\x3A\x25\x30\x34\x58", (unsigned int)(g_erase_diag & 0xFFFF)); /* 显示文字：擦除失败:%04X */
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
					show_download_error("\x45\x52\x32\x61\x3A\xB8\xB4\xCE\xBB\xD4\xDD\xCD\xA3\xCA\xA7\xB0\xDC", init_diag, init_step, 0); /* 显示文字：ER2a:复位暂停失败 */
					break;
				case ERROR_ALGO_DL:
					show_download_error("\x45\x52\x32\x62\x3A\x53\x52\x41\x4D\xD0\xB4\xC8\xEB\xCA\xA7\xB0\xDC", init_diag, init_step, 0); /* 显示文字：ER2b:SRAM写入失败 */
					break;
				case ERROR_INIT:
					show_download_error("\x45\x52\x32\x63\x3A\xCB\xE3\xB7\xA8\xB3\xF5\xCA\xBC\xBB\xAF\xCA\xA7\xB0\xDC", init_diag, init_step, 0); /* 显示文字：ER2c:算法初始化失败 */
					break;
				default:
				{
					char ebuf[24];
					sprintf(ebuf, "\x45\x52\x32\x3A\xB4\xED\xCE\xF3\x3D\x25\x75", (unsigned)init_err); /* 显示文字：ER2:错误=%u */
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
			show_download_status("\x45\x52\x33\x3A\xB2\xC1\xB3\xFD\xCA\xA7\xB0\xDC"); /* 显示文字：ER3:擦除失败 */
			goto download_return;
		}
		if(err_flag == 4)
		{
			show_download_status("\xD5\xFD\xD4\xDA\xC9\xD5\xC2\xBC\xB9\xCC\xBC\xFE"); /* 显示文字：正在烧录固件 */
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
						show_download_error("\x45\x52\x36\x3A\xC9\xD5\xC2\xBC\xCA\xA7\xB0\xDC", /* 显示文字：ER6:烧录失败 */
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
					show_download_status("\x45\x52\x35\x3A\xB9\xCC\xBC\xFE\xB6\xC1\xC8\xA1\xCA\xA7\xB0\xDC"); /* 显示文字：ER5:固件读取失败 */
					break;
				}
			}

						/* 编程完成后校验固件 */
			if(i == bin_size)
			{
				show_download_status("\xD5\xFD\xD4\xDA\xD0\xA3\xD1\xE9\xB9\xCC\xBC\xFE"); /* 显示文字：正在校验固件 */
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
					show_download_status("\xB9\xCC\xBC\xFE\xD0\xA3\xD1\xE9\xCD\xA8\xB9\xFD"); /* 显示文字：固件校验通过 */
				}
				else
				{
					show_download_status("\xB9\xCC\xBC\xFE\xD0\xA3\xD1\xE9\xCA\xA7\xB0\xDC"); /* 显示文字：固件校验失败 */
				}
				OLED_Refresh();
				f_close(&fd);
			}

/* 扩展算法的OPT编程 */
			if(is_ext_algo && app_data.opt_enable && i == bin_size)
			{
				FIL fd_opt;
				char opt_name[40];
				uint8_t *dot;
				uint32_t opt_read;

				show_download_status("\xD5\xFD\xD4\xDA\xD0\xB4\xC8\xEB\x4F\x50\x54"); /* 显示文字：正在写入OPT */
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
						/* 使用之前保存的OPT字段（algo_blob已被memmove移动过） */
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
							show_download_status("\x4F\x50\x54\xD0\xB4\xC8\xEB\xCD\xEA\xB3\xC9"); /* 显示文字：OPT写入完成 */
							}
							else
							{
								show_download_status("\x4F\x50\x54\xD0\xB4\xC8\xEB\xCA\xA7\xB0\xDC"); /* 显示文字：OPT写入失败 */
								opt_failed = 1;
							}
						} /* algo_ext 作用域结束 */
					}
					else
					{
						show_download_status("\x4F\x50\x54\xB6\xC1\xC8\xA1\xCA\xA7\xB0\xDC"); /* 显示文字：OPT读取失败 */
						opt_failed = 1;
					}
				}
				else
				{
					show_download_status("\xC8\xB1\xC9\xD9\x4F\x50\x54\xCE\xC4\xBC\xFE"); /* 显示文字：缺少OPT文件 */
					opt_failed = 1;
				}
				OLED_Refresh();
			}

			/* 逐段校验OPT字节 */
			if(is_ext_algo && app_data.opt_enable && i == bin_size && opt_prog_addr && !opt_failed)
			{
				show_download_status("\xD5\xFD\xD4\xDA\xD0\xA3\xD1\xE9\x4F\x50\x54"); /* 显示文字：正在校验OPT */
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
				show_download_status("\xD5\xFD\xD4\xDA\xB6\xC1\xC8\xA1\x4F\x50\x54\xCE\xC4\xBC\xFE"); /* 显示文字：正在读取OPT文件 */
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
							show_download_status("\x4F\x50\x54\xCD\xEA\xB3\xC9\xB2\xA2\xD2\xD1\xCB\xF8\xB6\xA8"); /* 显示文字：OPT完成并已锁定 */
						}
						else
						{
							show_download_status("\xD5\xFD\xD4\xDA\xBC\xEC\xB2\xE9\x4F\x50\x54"); /* 显示文字：正在检查OPT */
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
										sprintf((char *)D, "\x4F\x50\x54\xB6\xC1\xC8\xA1\xCA\xA7\xB0\xDC"); /* 显示文字：OPT读取失败 */
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
									show_download_status("\x4F\x50\x54\xD0\xA3\xD1\xE9\xCD\xA8\xB9\xFD"); /* 显示文字：OPT校验通过 */
								}
							}
						}
					}
					else
					{
						show_download_status("\x4F\x50\x54\xD0\xA3\xD1\xE9\xB6\xC1\xC8\xA1\xCA\xA7\xB0\xDC"); /* 显示文字：OPT校验读取失败 */
						opt_failed = 1;
					}
					f_close(&fd_opt2);
				}
				else
				{
					show_download_status("\x4F\x50\x54\xD0\xA3\xD1\xE9\xCE\xC4\xBC\xFE\xD2\xEC\xB3\xA3"); /* 显示文字：OPT校验文件异常 */
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
					show_download_status("\xB9\xCC\xBC\xFE\xCD\xEA\xB3\xC9\x2C\x4F\x50\x54\xCA\xA7\xB0\xDC"); /* 显示文字：固件完成,OPT失败 */
				else if(opt_state == 2)
					show_download_status("\xC9\xD5\xC2\xBC\xCD\xEA\xB3\xC9\x2C\x4F\x50\x54\xD2\xEC\xB3\xA3");		/* OPT 写了但 RDP 不对 */ /* 显示文字：烧录完成,OPT异常 */
				else if(opt_state == 1 || opt_state == 3)
					show_download_status("\xC9\xD5\xC2\xBC\xB3\xC9\xB9\xA6\x2C\xD2\xD1\xBC\xD3\xC3\xDC");	/* 加密已生效 */ /* 显示文字：烧录成功,已加密 */
				else
					show_download_status("\xC9\xD5\xC2\xBC\xB3\xC9\xB9\xA6");	/* 不加密 */ /* 显示文字：烧录成功 */

				/* 只有全程无错才算自动模式烧录成功 */
				if(!opt_failed && opt_state != 2)
					g_burn_ok = 1;

				OLED_Refresh();
				for(volatile uint32_t _d = 0; _d < 500000; _d++);
				swd_set_target_reset(0);
			}
			taskEXIT_CRITICAL();
			f_close(&fd);
		}
		else show_download_status("\x45\x52\x34\x3A\xB9\xCC\xBC\xFE\xB4\xF2\xBF\xAA\xCA\xA7\xB0\xDC"); /* 显示文字：ER4:固件打开失败 */
		
download_return:
        if(burn_quota_finish(g_burn_ok) != BURN_QUOTA_OK)
        {
            g_burn_ok = 0;
            show_download_status("\xB4\xCE\xCA\xFD\xBC\xC7\xC2\xBC\xD2\xEC\xB3\xA3"); /* 显示文字：次数记录异常 */
        }
        else
            show_quota(208);
		swd_off();
		TARGET_PWR_OFF;
		oled.Refresh_flag = 1;
		if(g_auto_en)
		{
			/* 把结果交回待机页；由它判断芯片何时被取出、
			   何时可以烧录下一颗 */
			g_auto_st = AUTO_ST_RESULT;
			g_auto_t0 = auto_now_ms();
			app_data.mode = 2;
		}
	}
	else
	{
		show_download_status("\xC7\xEB\xCF\xC8\xD1\xA1\xD4\xF1\xCE\xC4\xBC\xFE"); /* 显示文字：请先选择文件 */
		OLED_Refresh();
	}
}
/* 文件选择页：每页五项，文件名16像素完整显示，左右键循环选取。 */
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
				oled.Refresh_flag = 1;
				key_val.mesg = 0;
				return;
			case key_l_mid:                 /* 长按 -> 烧录选择页 */
				index = 0;
				app_data.mode = 2;
				app_data.head = 0;
				oled.Refresh_flag = 1;
				key_val.mesg = 0;
				return;
		}

		oled.Refresh_flag = 1;
		key_val.mesg = 0;
	}

	if(oled.Refresh_flag == 1 && !custombulk_is_busy())
	{
        UI_EnterPage(3); /* 文件列表布局首次进入才清屏。 */
        UI_Field(0, 8, 8, 304, 32, app_data.head == 0 ? "\xC9\xD5\xC2\xBC\xCB\xE3\xB7\xA8" : "\xC9\xD5\xC2\xBC\xB9\xCC\xBC\xFE", 32, 1); /* 显示文字：烧录算法；或 烧录固件 */
        sum = 0;
		if(app_data.head == 0)
			ret = f_opendir(&DirInfo, "0:/download/firmware");
		else if(app_data.head == 1)
			ret = f_opendir(&DirInfo, "0:/download/bin");
		
        i = 0;
        if(ret == FR_OK)
        {
            while(f_readdir(&DirInfo, &FileInfo) == FR_OK && FileInfo.fname[0]) sum++;
            if(index >= sum) index = sum ? sum - 1 : 0;
            f_readdir(&DirInfo, NULL); /* 回到目录起点，统一按有效索引绘制。 */
            sum = 0;
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
							/* 手动切换固件名称后旧导入校验值失效，避免显示另一程序的SUM32。 */
							if(strcmp((char *)app_data.bin_name, FileInfo.fname))
								invalidate_program_checksum();
							memset(app_data.bin_name, '\0', 32);
							strncpy((char *)app_data.bin_name, FileInfo.fname, sizeof(app_data.bin_name) - 1);
							/* 设备上手动选文件：OPT文件存在就编程OPT（保持旧行为） */
							app_data.opt_enable = 1;
						}
						/* 完整文件名在下面绘制。 */
                        UI_Field(1 + i, 8, 52 + i * 32, 304, 28, FileInfo.fname, 16, 0); /* 选中行反白显示。 */
					}
					else
					{
						/* 完整文件名在下面绘制。 */
						UI_Field(1 + i, 8, 52 + i * 32, 304, 28, FileInfo.fname, 16, 1); /* 普通行只画变化文件名。 */
					}		
					i++;
				}
				sum ++;
			}
			f_closedir(&DirInfo);
		}
        while(i < 5) { UI_Field(1 + i, 8, 52 + i * 32, 304, 28, "", 16, 1); i++; }
		if(!sum)
		{
			index = 0;
            if(app_data.head == 1) invalidate_program_checksum(); /* 固件目录为空，不能保留旧Checksum。 */
			UI_Field(1, 8, 52, 304, 32, "\xC3\xBB\xD3\xD0\xCE\xC4\xBC\xFE", 32, 1); /* 显示文字：没有文件 */
		}
        sprintf((char *)D, "%lu/%lu", (unsigned long)(sum ? index + 1 : 0), (unsigned long)sum);
        UI_Field(6, 8, 224, 160, 16, (char *)D, 16, 1);
        OLED_Refresh();
        oled.Refresh_flag = 0;
    }
    show_top(0); /* 温度、连接和电量各自刷新，不重画文件列表。 */
}

/* 初始化硬件并创建任务；循环先处理上位机延后请求，再运行当前界面/烧录状态机。 */
void run_task(void *pvParameters)
{
		uint8_t prepare_algorithm[CB_MAX_NAME + 1];
		uint8_t prepare_program[CB_MAX_NAME + 1];
		uint8_t prepare_opt;
        uint32_t prepare_irq_mask; /* 短临界区保护导入确认与界面提交，禁止复位插入二者之间。 */

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
/* 在线仿真专用部分：宏为0时不编译；离线SWD/OPT及WinUSB传输继续保留。 */
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
/* 在线仿真专用部分：宏为0时不编译；离线SWD/OPT及WinUSB传输继续保留。 */
#if DAP_ONLINE_ENABLE
		EventGroupHandle = xEventGroupCreate();
#endif
		taskEXIT_CRITICAL();
		
		start_page();
		oled.Refresh_flag = 1;
        /* USB供电和电池供电均直接进入图2的自动烧录待机页。 */
        app_data.mode = 2;
        app_data.head = 2;

		while(1)
		{
            custombulk_file_service(&usb_hid); /* 所有文件系统访问集中到主任务。 */
            custombulk_quota_service(&usb_hid);
			custombulk_prepare_service(&usb_hid);
            /* 文件读取已结束：下面只消费状态及拷贝名称，不在临界区读文件/刷新LCD。
             * 保存并恢复原PRIMASK，确保高优先级USB复位也无法提交半个旧会话。 */
            prepare_irq_mask = __get_PRIMASK();
            __disable_irq();
            if(custombulk_take_files_changed()) invalidate_program_checksum();
			if(custombulk_take_prepare_complete(prepare_algorithm,
			                                    prepare_program,
			                                    &prepare_opt))
			{
				apply_prepare_download(prepare_algorithm,
				                       prepare_program,
				                       prepare_opt);
			}
            __set_PRIMASK(prepare_irq_mask);
//			vTaskDelay(1000);
			switch(app_data.mode)
			{
				case 2:mode2();break;
				case 3:mode3();break;
				case 4:
                    /* 待处理文件包优先完成；只在真正烧录时拦截USB，结果页解除占用。 */
                    if(!g_burn_done && custombulk_is_busy()) break;
                    g_programming_active = g_burn_done ? 0 : 1;
                    mode4();
                    g_programming_active = 0;
                    break;
                default: app_data.mode = 2; oled.Refresh_flag = 1; break;
			}
			vTaskDelay(pdMS_TO_TICKS(5)); /* 避免待机空转，保证USB及按键任务及时运行。 */
		}
}

/* 在线仿真专用部分：宏为0时不编译；离线SWD/OPT及WinUSB传输继续保留。 */
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
/* 在线仿真专用部分：宏为0时不编译；离线SWD/OPT及WinUSB传输继续保留。 */
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

/* 按固定周期扫描按键；上一事件尚未消费时保留它，避免长按事件被下一次扫描覆盖。 */
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



/* 底部连接、温度、电量独立更新；每秒采样一次，不再触发菜单整体重画。
 * 图2的主要布局保持不变：左侧按键提示，右侧三个16像素信息字段。 */
void show_top(uint8_t force)
{
    static uint32_t sampled;
    static uint8_t ready;
    uint32_t now = auto_now_ms();
    uint8_t power, battery;
    char temperature[16], percent[8];
    /* 采样按时间限频；页面切换后立即恢复三个信息字段。 */
    if(force || !ready || now - sampled >= 1000)
    {
        sampled = now; ready = 1; adc_get_val(); adc_trigger();
    }
    power = bat_status_get();
    if(val.bat_vol <= 3.5f) battery = 0;
    else if(val.bat_vol >= 4.0f) battery = 100;
    else battery = (uint8_t)((val.bat_vol - 3.5f) * 200.0f);
    if(power) battery = 100;
    sprintf(temperature, "%.1fC", val.chip_temp);
    sprintf(percent, "%u%%", battery);
    UI_Field(10, 176, 224, 32, 16, power ? "CON" : "DIS", 16, 1);
    UI_Field(11, 208, 224, 64, 16, temperature, 16, 1);
    UI_Field(12, 280, 224, 32, 16, percent, 16, 1);
}
