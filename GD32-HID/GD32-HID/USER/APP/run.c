/* 中文注释补充（2026-10-26）：此前修改说明：保留原烧录/硬件流程，下方以中文说明相关界面与兼容逻辑。 */
#include "run.h"
#include "main.h"
#include "stdio.h"
#include "string.h"
#include "ff.h"
#include "DAP_config.h"
#include "usbd_custombulk.h"
#include "burn_quota.h"
#include "selection_store.h"
#include "image_task.h"
#include "bsp_lcd.h"


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
/* 离线选择页：芯片型号只读，手动选择固件或开始烧录；长按右键切换自动与手动。 */
void mode2(void);
/* 手动固件列表：左右上下移动，中键确认，长中取消。 */
void mode3(void);
/* 统一烧录流程：先检查额度并开启RAM会话，完整成功才扣一次；失败或中断不扣。 */
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
/* 自动待机与结果共用竖屏固定页，固件固定预留两行。 */
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

/* 选择改变仅标记；Flash写入安排在主任务临界区外，失败时暂停烧录。 */
static uint8_t g_selection_dirty;
static uint8_t g_selection_save_failed;

/* 主界面与列表页独立缓存；烧录过程和结果仍在主界面。 */
static uint8_t g_ui_ready, g_ui_percent, g_ui_state;
#define FILE_LIST_ROWS 12U
static char g_file_list[FILE_LIST_ROWS][32];
static uint32_t g_file_count, g_file_index, g_file_first;
static uint8_t g_file_list_ready, g_file_visible;
static const char *g_file_error;
static const char *g_file_footer_drawn;
/* 镜像页使用独立索引/分页状态，行文本复用g_file_list（mode3/mode5互斥）。 */
static uint8_t g_image_list_ready, g_image_index, g_image_first, g_image_visible;
static const char *g_image_footer_drawn;
static char g_image_error[20];
static void draw_main(void);
static void show_counts(void);
static void reset_feedback(void);
static void finish_burn_ui(void);
static uint8_t select_firmware(const char *name);

static void mark_selection_dirty(void)
{
    g_selection_dirty = 1;
    g_selection_save_failed = 0;
}

/* 记录算法名即可反查芯片型号，次数仍由独立次数日志管理。 */
static void capture_program_selection(program_selection_t *selection)
{
    memset(selection, 0, sizeof(*selection));
    memcpy(selection->algorithm_name, app_data.firmware_name, sizeof(selection->algorithm_name));
    memcpy(selection->program_name, app_data.bin_name, sizeof(selection->program_name));
    selection->opt_enabled = app_data.opt_enable;
    selection->checksum = g_program_checksum;
    selection->checksum_valid = g_program_checksum_valid;
}

/* 同一主任务集中保存，避开文件传输和目标烧录；相同记录由日志层免写。 */
static uint8_t save_program_selection(void)
{
    program_selection_t selection;
    /* 次数请求可临时查看别的固件，进入界面/烧录前必须重新绑定实际选择。
     * 在主任务且USB空闲时进行账本I/O，不放入prepare的关中断区。 */
    if(custombulk_is_busy() || g_programming_active) return 0;
    if(burn_quota_select(app_data.bin_name) != BURN_QUOTA_OK) return 0;
    if(!g_selection_dirty) return !g_selection_save_failed;
    if(g_selection_save_failed) return 0;
    capture_program_selection(&selection);
    if(!selection_store_save(&selection))
    {
        g_selection_save_failed = 1;
        return 0;
    }
    g_selection_dirty = 0;
    return 1;
}

/* USB主任务在覆盖/删除/改名/新导入之前调用：先保存不可烧录状态。
 * 保存失败必须拒绝文件变更，避免掉电后复活已被改写的旧固件选择。 */
uint8_t app_selection_invalidate(void)
{
    program_selection_t selection;
    app_data.bin_name[0] = 0;
    app_data.opt_enable = 0;
    g_program_checksum_valid = 0;
    image_store_clear_current_ram();
    reset_feedback();
    oled.Refresh_flag = 1;
    capture_program_selection(&selection);
    if(!selection_store_save(&selection))
    {
        g_selection_dirty = 1;
        g_selection_save_failed = 1;
        return 0;
    }
    g_selection_dirty = 0;
    g_selection_save_failed = 0;
    return 1;
}

/* 上电恢复选择并核对文件。复用已有工作缓冲区，校验不限于首个4KB。 */
static void restore_program_selection(void)
{
    program_selection_t selection;
    char path[64];
    uint32_t size, done, request, read_count, checksum;
    uint8_t ret;
    uint16_t base_len;
    const char *dot;
    if(!selection_store_load(&selection))
    {
        g_selection_save_failed = selection_store_fault();
        return;
    }
    memcpy(app_data.firmware_name, selection.algorithm_name, sizeof(app_data.firmware_name));
    memcpy(app_data.bin_name, selection.program_name, sizeof(app_data.bin_name));
    app_data.opt_enable = selection.opt_enabled;
    g_program_checksum = selection.checksum;
    g_program_checksum_valid = selection.checksum_valid;
    if(!app_data.bin_name[0]) return;
    if(!app_data.firmware_name[0]) goto invalid_selection;
    sprintf(path, "0:/download/firmware/%s", app_data.firmware_name);
    if(f_open(&fd, path, FA_READ) != FR_OK) goto invalid_selection;
    size = f_size(&fd);
    ret = f_close(&fd);
    if(!size || ret != FR_OK) goto invalid_selection;
    sprintf(path, "0:/download/bin/%s", app_data.bin_name);
    if(f_open(&fd, path, FA_READ) != FR_OK) goto invalid_selection;
    size = f_size(&fd); done = 0; checksum = 0; ret = FR_OK;
    if(g_program_checksum_valid)
    {
        while(done < size)
        {
            request = size - done;
            if(request > sizeof(DATA_BUFFER)) request = sizeof(DATA_BUFFER);
            ret = f_read(&fd, DATA_BUFFER, request, &read_count);
            if(ret != FR_OK || read_count != request) break;
            for(base_len = 0; base_len < read_count; base_len++) checksum += DATA_BUFFER[base_len];
            done += read_count;
        }
    }
    if(f_close(&fd) != FR_OK) ret = FR_DISK_ERR;
    if(!size || ret != FR_OK ||
       (g_program_checksum_valid && (done != size || checksum != g_program_checksum))) goto invalid_selection;
    if(app_data.opt_enable)
    {
        /* 与实际烧录相同：按最后一个扩展名生成OPT，无扩展名时用默认文件。 */
        dot = strrchr((const char *)app_data.bin_name, '.');
        if(dot && dot - (const char *)app_data.bin_name < 30)
        {
            base_len = (uint16_t)(dot - (const char *)app_data.bin_name);
            sprintf(path, "0:/download/bin/%.*s_opt.bin", (int)base_len, app_data.bin_name);
        }
        else strcpy(path, "0:/download/bin/default_opt.bin");
        if(f_open(&fd, path, FA_READ) != FR_OK) goto invalid_selection;
        size = f_size(&fd);
        ret = f_close(&fd);
        if(!size || ret != FR_OK) goto invalid_selection;
    }
    return;
invalid_selection:
    app_data.bin_name[0] = 0;
    app_data.opt_enable = 0;
    g_program_checksum_valid = 0;
    mark_selection_dirty();
}

/* 固件被改写、取消或更换后清除旧校验值；下次只刷新Checksum文字行。
 * 返回 - 无 */
static void invalidate_program_checksum(void)
{
    g_program_checksum_valid = 0;
    g_file_list_ready = 0; /* USB更改目录后，下次浏览重新枚举。 */
    mark_selection_dirty();
    reset_feedback();
    oled.Refresh_flag = 1;
}

/* 同行标题蓝色、内容黄色；直接绘制用于已清屏的启动/诊断页。 */


/* 剩余次数独占一行；独立缓冲区不能覆盖正在使用的OPT文件路径D。 */
static void show_quota(uint16_t y)
{
    char text[32];
    if(g_selection_dirty || !app_data.bin_name[0]) strcpy(text, "--");
    else if(burn_quota_fault()) strcpy(text, "\xBC\xC7\xC2\xBC\xD2\xEC\xB3\xA3");
    else if(burn_quota_enabled()) sprintf(text, "%08lu", (unsigned long)burn_quota_remaining());
    else strcpy(text, "\xCE\xDE\xCF\xDE");
    UI_FieldColor(6, 80, y, 152, 16, text, 16, 1, LCD_YELLOW);
}

/* 主固件SUM32同行显示；只更新变化的数字，不影响其他字段。 */
static void show_checksum(uint16_t y)
{
    char text[16];
    if(g_program_checksum_valid && app_data.bin_name[0])
        sprintf(text, "%08lX", (unsigned long)g_program_checksum);
    else strcpy(text, "--------");
    UI_FieldColor(3, 80, y, 152, 16, text, 16, 1, LCD_YELLOW);
}

/* 由已提交的算法文件名反查芯片型号，仅用于显示，不读取或更改目标芯片。
 * 算法由上位机固定配套并通过已有导入事务提交；旧名/未知名不能误标为已知芯片。
 * 返回 - 芯片型号；空选择显示“未选择”，未知旧算法显示“未知”（GBK字节）。 */
static const char *selected_chip_name(void)
{
    if(!app_data.firmware_name[0])
        return "\xCE\xB4\xD1\xA1\xD4\xF1"; /* 显示文字：未选择 */
    if(!strcmp((const char *)app_data.firmware_name, "MM32G001-OPT_ext.bin"))
        return "MM32G0001A";
    if(!strcmp((const char *)app_data.firmware_name, "MM32SPIN0230-OPT_ext.bin"))
        return "MM32SPIN0230";
    return "\xCE\xB4\xD6\xAA"; /* 显示文字：未知 */
}

/* 芯片型号同一行显示，英文冒号后紧接黄色型号（标题宽72像素）。 */
static void show_chip_name(uint16_t y)
{
    UI_FieldColor(0, 80, y, 152, 16, selected_chip_name(), 16, 1, LCD_YELLOW);
}

/* 主界面显示真实文件名：物理名 "001_xxx.bin" 去掉当前镜像号前缀；
 * 手动选择/非当前镜像保持原样。烧录仍用 app_data.bin_name 的物理名。 */
static const char *firmware_display_name(void)
{
    const char *name = (const char *)app_data.bin_name;
    uint8_t id = image_store_current();
    if(id < IMAGE_SLOTS && name[0] == (char)('0' + id / 100U) &&
       name[1] == (char)('0' + (id / 10U) % 10U) &&
       name[2] == (char)('0' + id % 10U) && name[3] == '_')
        return name + 4;
    return name;
}

/* 固件固定占两行，行间隔4像素：首行接标题，次行与内容起点对齐。 */
static void show_firmware_name(uint16_t y)
{
    UI_FieldColor(1, 80, y, 152, 36, firmware_display_name(), 16, 1, LCD_YELLOW);
}

/* 底部按键标题蓝色，切换目标模式黄色；字段9在主界面不用于进度。 */
/* 本次总数、成功已用和余量来自当前固件；旧记录的未知统计用--显示。 */
static void show_counts(void)
{
    char total[16], used[16];
    if(g_selection_dirty || !app_data.bin_name[0] || burn_quota_fault() || !burn_quota_stats_known())
    { strcpy(total, "--"); strcpy(used, "--"); }
    else
    {
        if(burn_quota_enabled()) sprintf(total, "%08lu", (unsigned long)burn_quota_total());
        else strcpy(total, "\xCE\xDE\xCF\xDE");
        sprintf(used, "%08lu", (unsigned long)burn_quota_used());
    }
    UI_FieldColor(4, 80, 188, 152, 16, total, 16, 1, LCD_YELLOW);
    UI_FieldColor(5, 80, 208, 152, 16, used, 16, 1, LCD_YELLOW);
    show_quota(228);
}

/* 主界面镜像号：有效时显示 000-099，加密镜像追加 *；无表/无当前显示 ---。 */
static void show_image_no(void)
{
    char text[8];
    uint8_t id = image_store_current();
    const image_record_t *rec = image_store_current_record();
    if(id < IMAGE_SLOTS && rec)
    {
        if(rec->flags & IMAGE_REC_FLAG_OPT) sprintf(text, "%03u*", (unsigned)id);
        else sprintf(text, "%03u", (unsigned)id);
    }
    else strcpy(text, "---");
    UI_FieldColor(9, 64, 48, 168, 16, text, 16, 1, LCD_YELLOW);
}

/* 静态标题只画一次；状态栏下方与开始烧录上方各留一空行。 */
static void draw_main(void)
{
    if(!g_ui_ready)
    {
        UI_EnterPage(10);
        UI_TextColor(8, 48, "\xBE\xB5\xCF\xF1\xBA\xC5\x3A", 16, 1, LCD_BLUE); /* 镜像号: */
        UI_TextColor(8, 68, "\xCA\xE4\xB3\xF6\xB5\xE7\xD1\xB9\x3A", 16, 1, LCD_BLUE); /* 输出电压: */
        UI_TextColor(8, 88, "\xD0\xBE\xC6\xAC\xD0\xCD\xBA\xC5\x3A", 16, 1, LCD_BLUE); /* 芯片型号: */
        UI_TextColor(8, 108, "\xC9\xD5\xC2\xBC\xC4\xA3\xCA\xBD\x3A", 16, 1, LCD_BLUE); /* 烧录模式: */
        UI_TextColor(8, 128, "\x63\x68\x65\x63\x6B\x73\x75\x6D\x3A", 16, 1, LCD_BLUE); /* checksum: */
        UI_TextColor(8, 148, "\xC9\xD5\xC2\xBC\xB9\xCC\xBC\xFE\x3A", 16, 1, LCD_BLUE); /* 烧录固件: */
        UI_TextColor(8, 188, "\xB1\xBE\xB4\xCE\xC9\xD5\xC2\xBC\x3A", 16, 1, LCD_BLUE); /* 本次烧录: */
        UI_TextColor(8, 208, "\xD2\xD1\xD3\xC3\xB4\xCE\xCA\xFD\x3A", 16, 1, LCD_BLUE); /* 已用次数: */
        UI_TextColor(8, 228, "\xCA\xA3\xD3\xE0\xB4\xCE\xCA\xFD\x3A", 16, 1, LCD_BLUE); /* 剩余次数: */
        UI_TextColor(8, 268, "\xBF\xAA\xCA\xBC\xC9\xD5\xC2\xBC", 16, 1, LCD_BLUE); /* 开始烧录 */
        UI_TextColor(8, 288, "\xD6\xB4\xD0\xD0\xBD\xF8\xB6\xC8\x3A", 16, 1, LCD_BLUE); /* 执行进度: */
        UI_TextColor(80, 68, "3.3V", 16, 1, LCD_YELLOW);
        g_ui_ready = 1;
        show_top(1);
    }
    show_image_no();
    show_chip_name(88);
    show_firmware_name(148);
    UI_FieldColor(2, 80, 108, 152, 16, g_auto_en ? "\xD7\xD4\xB6\xAF" : "\xCA\xD6\xB6\xAF", 16, 1, LCD_YELLOW);
    show_checksum(128);
    show_counts();
    UI_FieldColor(10, 0, 48, 8, 16, !g_auto_en && app_data.head == 3 ? ">" : "", 16, 1, LCD_YELLOW);
    UI_FieldColor(7, 0, 148, 8, 16, !g_auto_en && app_data.head == 1 ? ">" : "", 16, 1, LCD_YELLOW);
    UI_FieldColor(8, 0, 268, 8, 16, g_auto_en || app_data.head == 2 ? ">" : "", 16, 1, LCD_YELLOW);
    show_top(0);
    UI_ProgressStatus(80, 288, 152, g_ui_percent, g_ui_state, g_last_status);
}

static void reset_feedback(void)
{
    g_ui_state = UI_PROGRESS_IDLE;
    g_ui_percent = 0;
    strcpy(g_last_status, !app_data.bin_name[0] ? "\xC7\xEB\xD1\xA1\xD4\xF1\xB9\xCC\xBC\xFE" :
                         (g_auto_en ? "\xB5\xC8\xB4\xFD\xD0\xBE\xC6\xAC" : "\xB5\xC8\xB4\xFD\xB2\xD9\xD7\xF7"));
}

/* 成功色只在整体烧录和次数提交都成功后显示，结果保留在主界面底部进度行。 */
static void finish_burn_ui(void)
{
    if(g_burn_ok)
    {
        g_ui_state = UI_PROGRESS_SUCCESS; g_ui_percent = 100;
        strcpy(g_last_status, "\xC9\xD5\xC2\xBC\xB3\xC9\xB9\xA6");
    }
    else
    {
        g_ui_state = UI_PROGRESS_FAILED;
        if(!g_last_status[0]) strcpy(g_last_status, "\xC9\xD5\xC2\xBC\xCA\xA7\xB0\xDC");
    }
    if(g_auto_en) { g_auto_st = AUTO_ST_RESULT; g_auto_t0 = auto_now_ms(); }
    app_data.mode = 2; key_val.mesg = 0; oled.Refresh_flag = 1;
    draw_main();
}

/* 列表排除目录、OPT数据和无法安全存入选择记录的文件名。 */
static uint8_t selectable_firmware(const char *name, uint8_t attr)
{
    unsigned int i, n = strlen(name);
    char tail[9];
    if((attr & AM_DIR) || !n || n >= sizeof(app_data.bin_name)) return 0;
    for(i = 0; i < n; i++) if((uint8_t)name[i] >= 0x7F || (uint8_t)name[i] < 0x20) return 0;
    if(n >= 8)
    {
        for(i = 0; i < 8; i++) { char ch = name[n - 8 + i]; tail[i] = ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch; }
        tail[8] = 0;
        if(!strcmp(tail, "_opt.bin")) return 0;
    }
    return 1;
}

/* 仅确认候选时读取SUM32/OPT，全部成功才提交；错误不破坏旧有效选择。 */
static uint8_t select_firmware(const char *name)
{
    char path[80];
    const char *dot;
    uint32_t size, done, request, received, checksum, i;
    uint8_t ret, opt_enabled = 0;
    if(custombulk_is_busy()) return 0;
    sprintf(path, "0:/download/bin/%s", name);
    if(f_open(&fd, path, FA_READ) != FR_OK) goto invalid_file;
    size = f_size(&fd); done = checksum = 0; ret = FR_OK;
    while(done < size)
    {
        request = size - done; if(request > sizeof(DATA_BUFFER)) request = sizeof(DATA_BUFFER);
        ret = f_read(&fd, DATA_BUFFER, request, &received);
        if(ret != FR_OK || received != request) break;
        for(i = 0; i < received; i++) checksum += DATA_BUFFER[i];
        done += received;
    }
    if(f_close(&fd) != FR_OK) ret = FR_DISK_ERR;
    if(!size || done != size || ret != FR_OK) goto invalid_file;
    dot = strrchr(name, '.');
    if(dot && dot - name < 30) sprintf(path, "0:/download/bin/%.*s_opt.bin", (int)(dot-name), name);
    else strcpy(path, "0:/download/bin/default_opt.bin");
    ret = f_open(&fd, path, FA_READ);
    if(ret == FR_OK)
    {
        size = f_size(&fd); ret = f_close(&fd);
        if(!size || ret != FR_OK) goto invalid_opt;
        opt_enabled = 1;
    }
    else if(ret != FR_NO_FILE && ret != FR_NO_PATH) goto invalid_opt;
    strcpy((char *)app_data.bin_name, name);
    app_data.opt_enable = opt_enabled;
    g_program_checksum = checksum; g_program_checksum_valid = 1;
    image_store_clear_current_ram();
    mark_selection_dirty(); g_burn_done = 0; reset_feedback(); return 1;
invalid_opt:
    g_file_error = "\x4F\x50\x54\xB6\xC1\xC8\xA1\xCA\xA7\xB0\xDC"; return 0;
invalid_file:
    g_file_error = "\xB9\xCC\xBC\xFE\xB6\xC1\xC8\xA1\xCA\xA7\xB0\xDC"; return 0;
}

/* 接收上位机导入的算法、固件与OPT开关，更新主界面并保持USB后台连接。
 * 上位机在提交前已为此固件重设次数；本函数只复制选择，关中断区外再绑定其额度。
 * algorithm_name - 算法文件名（不含路径），长度不超过app_data.firmware_name容量
 * program_name   - 主固件文件名（不含路径），长度不超过app_data.bin_name容量
 * opt_required   - 上位机是否要求编程OPT，1=需要，0=跳过
 * 返回           - 无 */
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
    image_store_clear_current_ram(); /* USB 导入不是镜像任务，显示 ---。 */
    mark_selection_dirty();
    g_ui_ready = 0; g_file_list_ready = 0; /* 导入可从列表页返回，主界面必须重画。 */
    app_data.mode = 2;
    app_data.head = 2;
    key_val.mesg = 0;
    /* 导入完成后主任务绑定本固件额度并保存选择，再开始芯片检测。 */
    g_auto_st = AUTO_ST_WAIT; g_present_seen = 0; g_absent_seen = 0;
    g_auto_tprobe = auto_now_ms();
    g_burn_done = 0; reset_feedback();
    oled.Refresh_flag = 1;
}

/* 校验镜像记录对应的算法/固件/OPT/SUM，返回 IMG_*。
 * sum_out - 返回实际主固件 SUM32；无 SUM 位的旧迁移记录也会计算并接受。 */
static uint8_t image_validate_record(const image_record_t *rec, uint32_t *sum_out)
{
    char path[80];
    uint32_t size, done, got, sum, request, i, hdr_size, max_algo;
    uint8_t ret;
    program_target_ext_t ext;
    const char *dot;
    uint8_t has_opt;

    if(!rec || !(rec->flags & IMAGE_REC_FLAG_VALID)) return IMG_ERR_FMT;
    if(!rec->algo[0] || !rec->bin[0]) return IMG_ERR_FMT;
    if(!memchr(rec->algo, 0, sizeof(rec->algo)) ||
       !memchr(rec->bin, 0, sizeof(rec->bin))) return IMG_ERR_FMT;

    has_opt = (rec->flags & IMAGE_REC_FLAG_OPT) ? 1 : 0;
    hdr_size = has_opt ? PROG_TARGET_EXT_HDR_SIZE : (PROG_TARGET_EXT_HDR_SIZE - 12U);
    max_algo = has_opt ? 1980U : 1992U;

    sprintf(path, "0:/download/firmware/%s", rec->algo);
    if(f_open(&fd, path, FA_READ) != FR_OK) return IMG_ERR_FILE;
    size = f_size(&fd);
    if(size < PROG_TARGET_EXT_HDR_SIZE) { f_close(&fd); return IMG_ERR_FILE; }
    memset(&ext, 0, sizeof(ext));
    got = 0;
    ret = f_read(&fd, &ext, sizeof(ext), &got);
    if(f_close(&fd) != FR_OK || ret != FR_OK || got != sizeof(ext)) return IMG_ERR_FILE;
    if(ext.algo_size > max_algo || size != ext.algo_size + hdr_size) return IMG_ERR_FILE;
    if(has_opt)
    {
        if(!ext.opt_program || !ext.opt_addr || !ext.opt_size || ext.opt_size > 128U)
            return IMG_ERR_OPT;
        dot = strrchr(rec->bin, '.');
        if(dot && (dot - rec->bin) < 30)
            sprintf(path, "0:/download/bin/%.*s_opt.bin", (int)(dot - rec->bin), rec->bin);
        else
            strcpy(path, "0:/download/bin/default_opt.bin");
        if(f_open(&fd, path, FA_READ) != FR_OK) return IMG_ERR_OPT;
        size = f_size(&fd);
        if(!size || f_close(&fd) != FR_OK) return IMG_ERR_OPT;
    }

    sprintf(path, "0:/download/bin/%s", rec->bin);
    if(f_open(&fd, path, FA_READ) != FR_OK) return IMG_ERR_FILE;
    size = f_size(&fd);
    /* v1 只校验 100KB 上限；算法头 flash_size 语义未验证，避免误拒绝已验证可烧的组合。 */
    if(!size || size > IMAGE_FW_MAX)
    { f_close(&fd); return IMG_ERR_FILE; }
    sum = 0; done = 0; ret = FR_OK;
    while(done < size)
    {
        request = size - done;
        if(request > sizeof(DATA_BUFFER)) request = sizeof(DATA_BUFFER);
        got = 0;
        ret = f_read(&fd, DATA_BUFFER, request, &got);
        if(ret != FR_OK || got != request) break;
        for(i = 0; i < got; i++) sum += DATA_BUFFER[i];
        done += got;
    }
    if(f_close(&fd) != FR_OK || ret != FR_OK || done != size) return IMG_ERR_FILE;
    if((rec->flags & IMAGE_REC_FLAG_SUM) && rec->checksum != sum) return IMG_ERR_SUM;
    if(sum_out) *sum_out = sum;
    return IMG_OK;
}

/* 让主界面镜像号与当前 app_data 选择一致；有效但无匹配显示 ---。 */
static void sync_image_display_to_selection(void)
{
    uint8_t id;
    image_record_t rec;
    uint32_t sum = 0;
    if(app_data.bin_name[0] && app_data.firmware_name[0] &&
       image_store_find_match(app_data.firmware_name, app_data.bin_name,
                              app_data.opt_enable, g_program_checksum,
                              g_program_checksum_valid, &id) &&
       image_store_read_record(id, &rec) &&
       image_validate_record(&rec, &sum) == IMG_OK)
        image_store_select_ram(id);
    else
        image_store_clear_current_ram();
}

/* 回滚到旧选择并尽量持久化；失败置保存错误锁存。 */
static uint8_t restore_selection_object(const program_selection_t *sel)
{
    memcpy(app_data.firmware_name, sel->algorithm_name, sizeof(app_data.firmware_name));
    memcpy(app_data.bin_name, sel->program_name, sizeof(app_data.bin_name));
    app_data.opt_enable = sel->opt_enabled;
    g_program_checksum = sel->checksum;
    g_program_checksum_valid = sel->checksum_valid;
    mark_selection_dirty();
    if(!save_program_selection())
    {
        g_selection_save_failed = 1;
        return 0;
    }
    image_store_init();
    sync_image_display_to_selection();
    return 1;
}

/* 应用镜像：校验 → 写 app_data → 绑定次数 → 保存选择 → 更新 bank current。
 * id - 0..99；IMAGE_ID_NONE 清空当前选择并持久化表 current=0xFF。
 * 返回 - IMG_OK 或 IMG_ERR_*。只在主任务、USB 空闲且未烧录时调用。 */
uint8_t image_apply(uint8_t id)
{
    image_record_t rec;
    program_selection_t old_sel;
    uint32_t sum = 0;
    uint8_t err;

    if(custombulk_is_busy() || g_programming_active) return IMG_ERR_BUSY;
    if(g_selection_save_failed) return IMG_ERR_SAVE;
    if(!image_store_valid()) return IMG_ERR_STORAGE;

    if(id == IMAGE_ID_NONE)
    {
        capture_program_selection(&old_sel);
        app_data.bin_name[0] = 0;
        app_data.firmware_name[0] = 0;
        app_data.opt_enable = 0;
        g_program_checksum = 0;
        g_program_checksum_valid = 0;
        mark_selection_dirty();
        if(!save_program_selection()) { restore_selection_object(&old_sel); return IMG_ERR_SAVE; }
        if(!image_store_set_current(IMAGE_ID_NONE))
        { restore_selection_object(&old_sel); return IMG_ERR_TABLE; }
        reset_feedback(); g_ui_ready = 0; oled.Refresh_flag = 1;
        return IMG_OK;
    }

    if(id >= IMAGE_SLOTS) return IMG_ERR_FMT;
    if(!image_store_read_record(id, &rec)) return IMG_ERR_STORAGE;
    err = image_validate_record(&rec, &sum);
    if(err != IMG_OK) return err;

    capture_program_selection(&old_sel);
    memcpy(app_data.firmware_name, rec.algo, sizeof(app_data.firmware_name));
    memcpy(app_data.bin_name, rec.bin, sizeof(app_data.bin_name));
    app_data.firmware_name[sizeof(app_data.firmware_name) - 1] = 0;
    app_data.bin_name[sizeof(app_data.bin_name) - 1] = 0;
    app_data.opt_enable = (rec.flags & IMAGE_REC_FLAG_OPT) ? 1 : 0;
    g_program_checksum = sum;
    g_program_checksum_valid = 1;
    mark_selection_dirty();

    if(burn_quota_select(app_data.bin_name) != BURN_QUOTA_OK || burn_quota_fault())
    {
        restore_selection_object(&old_sel);
        return IMG_ERR_QUOTA;
    }
    if(!save_program_selection())
    {
        restore_selection_object(&old_sel);
        return IMG_ERR_SAVE;
    }
    if(!image_store_set_current(id))
    {
        restore_selection_object(&old_sel);
        return IMG_ERR_TABLE;
    }
    reset_feedback(); g_ui_ready = 0; oled.Refresh_flag = 1;
    return IMG_OK;
}

/* 关闭所有中断（PRIMASK置1），用于保护不可被打断的短临界区。
 * 返回 - 无（须与INTX_ENABLE成对使用） */
void INTX_DISABLE(void)
{		  
	__ASM volatile("cpsid i");
}

/* 开启所有中断（PRIMASK清0），与INTX_DISABLE配对恢复中断。
 * 返回 - 无 */
void INTX_ENABLE(void)
{
	__ASM volatile("cpsie i");		  
}

/* 启动时恢复选择和次数，直接进入同一个竖屏界面。
 * 返回 - 无 */
/* 开机 logo/caption 放在外部 SPI Flash 的 /system/boot_logo.bin，流式读取，
 * 不再占用内部 Flash。文件缺失/损坏时退回一个红色 NXN 文字，不阻塞启动。
 * 文件格式见 tools/make_boot_logo_bin.py：48B 头 + RLE + 1bpp caption。 */
#define BOOT_LOGO_PATH      "0:/system/boot_logo.bin"
#define BOOT_LOGO_HDR_SIZE  48U

static void show_boot_logo(void)
{
    uint8_t hdr[BOOT_LOGO_HDR_SIZE], rowbuf[24], token;
    uint16_t palette[16];
    uint16_t rle_len, logo_w, logo_h, cap_w, cap_h, cap_stride;
    uint16_t i, col = 0, row = 0, span;
    uint16_t logo_x, logo_y, cap_x, cap_y;
    UINT br = 0;
    FRESULT res = FR_NO_FILE;

    OLED_Clear();
    if(f_open(&fd, BOOT_LOGO_PATH, FA_READ) == FR_OK)
    {
        res = f_read(&fd, hdr, BOOT_LOGO_HDR_SIZE, &br);
        if(res == FR_OK && br == BOOT_LOGO_HDR_SIZE &&
           hdr[0] == 'N' && hdr[1] == 'X' && hdr[2] == 'N' && hdr[3] == '1')
        {
            rle_len = (uint16_t)(hdr[4] | ((uint16_t)hdr[5] << 8));
            logo_w = (uint16_t)(hdr[6] | ((uint16_t)hdr[7] << 8));
            logo_h = (uint16_t)(hdr[8] | ((uint16_t)hdr[9] << 8));
            cap_w = (uint16_t)(hdr[10] | ((uint16_t)hdr[11] << 8));
            cap_h = (uint16_t)(hdr[12] | ((uint16_t)hdr[13] << 8));
            cap_stride = (uint16_t)(hdr[14] | ((uint16_t)hdr[15] << 8));
            for(i = 0; i < 16U; i++)
                palette[i] = (uint16_t)(hdr[16U + i * 2U] | ((uint16_t)hdr[17U + i * 2U] << 8));
            if(logo_w && logo_h && logo_w <= LCD_WIDTH && logo_h <= LCD_HEIGHT &&
               cap_w && cap_h && cap_stride >= (uint16_t)((cap_w + 7U) / 8U) &&
               cap_stride <= sizeof(rowbuf))
            {
                logo_x = (LCD_WIDTH - logo_w) / 2U;
                logo_y = (LCD_HEIGHT - (logo_h + 16U + cap_h)) / 2U;
                cap_x = (LCD_WIDTH - cap_w) / 2U;
                cap_y = logo_y + logo_h + 16U;
                for(i = 0; i < rle_len && row < logo_h; i++)
                {
                    if(f_read(&fd, &token, 1, &br) != FR_OK || br != 1U) break;
                    span = (uint16_t)((token & 15U) + 1U);
                    if(span > (uint16_t)(logo_w - col)) span = (uint16_t)(logo_w - col);
                    if(token >> 4)
                        LCD_Fill((uint16_t)(logo_x + col), (uint16_t)(logo_y + row),
                                 span, 1U, palette[token >> 4]);
                    col = (uint16_t)(col + span);
                    if(col >= logo_w) { col = 0; row++; }
                }
                if(row >= logo_h && f_lseek(&fd, BOOT_LOGO_HDR_SIZE + rle_len) == FR_OK)
                {
                    for(i = 0; i < cap_h; i++)
                    {
                        if(f_read(&fd, rowbuf, cap_stride, &br) != FR_OK || br != cap_stride) break;
                        LCD_BlitMonoColor(cap_x, (uint16_t)(cap_y + i), cap_w, 1U,
                                          cap_stride, rowbuf, 0, 1, 1, LCD_RED);
                    }
                }
            }
        }
        f_close(&fd);
    }
    if(res != FR_OK || row < logo_h)
        UI_TextColor((LCD_WIDTH - 24U) / 2U, (LCD_HEIGHT - 16U) / 2U, "NXN", 16, 1, LCD_RED);
    vTaskDelay(pdMS_TO_TICKS(1000));
}

void start_page(void)
{
    uint8_t ret, legacy_ok;
    program_selection_t legacy_selection;
    program_selection_t current_selection;
    burn_quota_init(); selection_store_init();
    ret = f_mount(&fs, "0:", 1);
    if(ret == FR_OK)
    {
        restore_program_selection();
        legacy_ok = selection_store_load(&legacy_selection);
        if(legacy_ok) burn_quota_migrate_legacy(legacy_selection.program_name);
        else burn_quota_migrate_legacy((const uint8_t *)"");
        burn_quota_select(app_data.bin_name);
        /* 镜像索引在裸扇区；无有效表时把 restore 校验后的当前选择迁移成镜像 000。 */
        image_store_init();
        if(!image_store_valid())
        {
            capture_program_selection(&current_selection);
            if(current_selection.program_name[0] && current_selection.algorithm_name[0])
                image_store_migrate_from_selection(&current_selection);
        }
        /* 表有效时：有效选择只贴镜像号、不覆盖选择；选择为空才用表 current 兜底。 */
        if(image_store_valid())
        {
            if(app_data.bin_name[0] && app_data.firmware_name[0])
            {
                sync_image_display_to_selection();
            }
            else
            {
                image_record_t rec;
                uint32_t sum = 0;
                uint8_t id = image_store_current();
                if(id < IMAGE_SLOTS && image_store_read_record(id, &rec) &&
                   image_validate_record(&rec, &sum) == IMG_OK)
                {
                    memcpy(app_data.firmware_name, rec.algo, sizeof(app_data.firmware_name));
                    memcpy(app_data.bin_name, rec.bin, sizeof(app_data.bin_name));
                    app_data.firmware_name[sizeof(app_data.firmware_name) - 1] = 0;
                    app_data.bin_name[sizeof(app_data.bin_name) - 1] = 0;
                    app_data.opt_enable = (rec.flags & IMAGE_REC_FLAG_OPT) ? 1 : 0;
                    g_program_checksum = sum;
                    g_program_checksum_valid = 1;
                    mark_selection_dirty();
                }
                else image_store_clear_current_ram();
            }
        }
    }
    app_data.mode = 2; app_data.head = 2; g_ui_ready = 0;
    reset_feedback();
    if(ret != FR_OK) { g_ui_state = UI_PROGRESS_FAILED; strcpy(g_last_status, "\xCE\xC4\xBC\xFE\xCF\xB5\xCD\xB3\xD2\xEC\xB3\xA3"); }
    show_boot_logo();
    draw_main();
}

/* 原模式菜单和文件传输页面已移除；文件传输保留为烧录页的USB后台服务。 */
/* 长按中键关机：保留原充电指示，避免移除一级菜单后无法关机。
 * 关屏、Flash掉电、断开USB后进入死循环；插入充电时循环点亮三色灯。
 * 返回 - 不返回（函数内死循环，直到重新上电） */
static void power_down(void)
{
    uint8_t step = 0;
    save_program_selection();
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

/* 离线选择页：芯片型号只读；手动模式仅固件选择和开始烧录参与按键导航。
 * 返回 - 无 */
void mode2(void)
{
    if(g_auto_en) app_data.head = 2;
    else if(app_data.head != 1 && app_data.head != 2 && app_data.head != 3) app_data.head = 1;
    if(key_val.mesg)
    {
        switch(key_val.key)
        {
            case key_l_mid:
                if(!custombulk_is_busy()) power_down();
                key_val.mesg = 0; return;
            case key_d_mid: break;
            /* 手动模式：短按左/右在 1→2→3 之间循环；长按/连发不移动焦点。 */
            case key_left:
                if(!g_auto_en) app_data.head = (app_data.head == 1) ? 3 :
                                              ((app_data.head == 2) ? 1 : 2);
                break;
            case key_right:
                if(!g_auto_en) app_data.head = (app_data.head == 1) ? 2 :
                                              ((app_data.head == 2) ? 3 : 1);
                break;
            case key_l_left: case key_rep_left: case key_rep_right:
                break;
            case key_l_right:
                g_auto_en = !g_auto_en; g_auto_st = AUTO_ST_WAIT;
                g_present_seen = g_absent_seen = 0;
                g_auto_tprobe = auto_now_ms() - AUTO_PROBE_MS;
                app_data.head = g_auto_en ? 2 : 1; g_burn_done = 0; reset_feedback();
                break;
            case key_mid:
                if(custombulk_is_busy()) break;
                if(!g_auto_en && app_data.head == 1)
                {
                    g_ui_ready = 0; g_file_list_ready = 0;
                    app_data.mode = 3; key_val.mesg = 0; return;
                }
                else if(!g_auto_en && app_data.head == 3)
                {
                    g_ui_ready = 0; g_image_list_ready = 0; g_image_error[0] = 0;
                    app_data.mode = 5; key_val.mesg = 0; return;
                }
                else
                {
                    g_burn_done = 0;
                    if(g_auto_en) auto_start_burn();
                    else app_data.mode = 4;
                    key_val.mesg = 0; return;
                }
            default: break;
        }
        key_val.mesg = 0; oled.Refresh_flag = 1;
    }
    if(g_auto_en && !custombulk_is_busy())
    { auto_standby_tick(); if(app_data.mode != 2) return; }
    if(g_auto_en) auto_draw();
    else { if(!g_last_status[0]) reset_feedback(); draw_main(); }
    oled.Refresh_flag = 0;
}

/* 更新主界面底部进度，主固件写入阶段映射为15%到85%。
 * percent - 进度百分比，0~100
 * 返回    - 无 */
static void show_download_progress(uint8_t percent)
{
    uint8_t next = (uint8_t)(15U + 70U * (percent > 100 ? 100 : percent) / 100U);
    if(next > g_ui_percent) g_ui_percent = next;
    UI_ProgressStatus(80, 288, 152, g_ui_percent, g_ui_state, g_last_status);
}

/* 保存反馈文字并叠加在底部进度条内，自动和手动模式共用。
 * status - 要显示的状态文字（GBK字节，最长不超过界面缓冲区）
 * 返回   - 无 */
static void show_download_status(const char *status)
{
    uint8_t next = g_ui_percent;
    strncpy(g_last_status, status, sizeof(g_last_status) - 1);
    g_last_status[sizeof(g_last_status) - 1] = 0;
    if(g_ui_state == UI_PROGRESS_RUNNING)
    {
        if(strstr(status, "\xC1\xAC\xBD\xD3")) next = 2;
        else if(strstr(status, "\xBC\xD3\xD4\xD8")) next = 5;
        else if(strstr(status, "\xB2\xC1\xB3\xFD")) next = 10;
        else if(strstr(status, "\xC9\xD5\xC2\xBC\xB9\xCC\xBC\xFE")) next = 15;
        else if(strstr(status, "\xD0\xA3\xD1\xE9\xB9\xCC\xBC\xFE")) next = 85;
        else if(strstr(status, "\xD0\xB4\xC8\xEB\x4F\x50\x54")) next = 90;
        else if(strstr(status, "\xD0\xA3\xD1\xE9\x4F\x50\x54") || strstr(status, "\xB6\xC1\xC8\xA1\x4F\x50\x54") || strstr(status, "\xBC\xEC\xB2\xE9\x4F\x50\x54")) next = 95;
        if(next > g_ui_percent) g_ui_percent = next;
    }
    UI_ProgressStatus(80, 288, 152, g_ui_percent, g_ui_state, g_last_status);
    show_counts();
}

/* 显示烧录失败原因、步骤、地址和SWD诊断寄存器，底部显示剩余次数。
 * 原生LCD错误页：全页16像素，标题蓝色、诊断值黄色。
 * status - 失败原因文字，形如“ER2a:复位暂停失败”
 * diag   - 诊断值：失败步骤读回的R0或DP/DHCSR快照
 * step   - 失败步骤编号，0xFFFF表示未执行到算法阶段
 * addr   - 失败时的目标地址，为0时不显示地址行
 * 返回   - 无 */
static void show_download_error(const char *status, uint32_t diag, uint32_t step, uint32_t addr)
{
    /* 底层仍保存诊断寄存器；屏幕在底部进度行保留错误码和原因。 */
    (void)diag; (void)step; (void)addr;
    g_ui_state = UI_PROGRESS_FAILED;
    show_download_status(status);
}

/* 尽快连接并暂停内核：最多重试50次，抢在目标程序关闭调试口之前停下CPU。
 * 返回 - 0=DP无响应，1=已暂停，2=DP有响应但CPU停不下来 */
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

/* 将FreeRTOS系统节拍转换为毫秒，用于自动检测和结果停留计时。
 * 返回 - 上电以来的毫秒数（32位，约49天回绕） */
static uint32_t auto_now_ms(void)
{
	return (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
}

/* 检查算法和固件是否都已选择；缺少任一文件时禁止自动开始。
 * 返回 - 1=两个文件都已加载，0=缺文件 */
static uint8_t auto_has_files(void)
{
	return (!g_selection_dirty && !g_selection_save_failed &&
            app_data.bin_name[0] != '\0' && app_data.firmware_name[0] != '\0');
}

/* 短暂上电并尝试SWD连接以检测芯片；上电瞬间立即探测，
 * 以抢在目标程序关闭调试口之前连接。
 * 返回 - 1=SWD有芯片应答，0=座子为空 */
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

/* 进入一次自动烧录：清除旧结果，转到mode4统一进行次数检查和烧录。
 * 返回 - 无 */
static void auto_start_burn(void)
{
	g_burn_done = 0;
	g_burn_ok = 0;
	g_present_seen = 0;
	g_absent_seen = 0;
	g_auto_st = AUTO_ST_BURN;
	app_data.mode = 4; /* 仅切内部执行状态，主界面保持原位。 */
	oled.Refresh_flag = 1;
	key_val.mesg = 0;
}

/* 自动检测芯片：连续检测到芯片后烧录，显示结果后等待芯片取出，
 * 避免同一颗芯片重复烧录。只由mode2()调用，需要开始烧录时把app_data.mode改成4。
 * 返回 - 无 */
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
                    reset_feedback();
					oled.Refresh_flag = 1;
				}
			}
			else g_absent_seen = 0;
			break;
	}
}

/* 绘制240×320自动烧录固定页，结果保持到取出芯片或开始新任务。
 * 返回 - 无 */
static void auto_draw(void)
{
    if(!g_last_status[0]) reset_feedback();
    draw_main();
}

/* 统一烧录流程：先检查额度并开启RAM会话，再烧录和校验主固件/OPT；
 * 完整成功才提交扣次，失败或未完成时断电均不扣。
 * 返回 - 无 */
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

    if(g_burn_done) { app_data.mode = 2; draw_main(); return; }
    g_burn_done = 1; g_burn_ok = 0;
    g_ui_state = UI_PROGRESS_RUNNING; g_ui_percent = 0;
    strcpy(g_last_status, "\xD7\xBC\xB1\xB8\xC9\xD5\xC2\xBC"); draw_main();
    key_val.mesg = 0;
    if(g_selection_dirty || g_selection_save_failed)
    {
        show_download_status("\xD1\xA1\xD4\xF1\xB1\xA3\xB4\xE6\xCA\xA7\xB0\xDC"); finish_burn_ui(); return;
    }
    if(burn_quota_select(app_data.bin_name) != BURN_QUOTA_OK ||
       burn_quota_fault() || (burn_quota_enabled() && !burn_quota_remaining()))
    {
quota_rejected:
        show_download_status(burn_quota_fault() ? "\xB4\xCE\xCA\xFD\xBC\xC7\xC2\xBC\xD2\xEC\xB3\xA3" : "\xCA\xA3\xD3\xE0\xB4\xCE\xCA\xFD\xB2\xBB\xD7\xE3");
        finish_burn_ui(); return;
    }

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
            show_counts();
        swd_off();
        TARGET_PWR_OFF;
        finish_burn_ui();
	}
	else
	{
        show_download_status("\xC7\xEB\xCF\xC8\xD1\xA1\xD4\xF1\xCE\xC4\xBC\xFE");
        finish_burn_ui();
	}
}

/* 目录仅缓存当前页最多十二个名称；短项16px、长项36px，间隔4px。
 * 首次枚举计数并定位旧选择，再按高度找到所在页；跨页重放相同分页规则。
 * 所有目录访问仍在主任务内，浏览不读取文件正文，也不更改有效选择。 */
static uint8_t scan_firmware_page(uint8_t locate)
{
    uint8_t ret;
    uint32_t ordinal = 0;
    uint16_t used = 0, height;
    UI_ClearFields(0, FILE_LIST_ROWS);
    g_file_visible = 0; g_file_first = 0;
    memset(g_file_list, 0, sizeof(g_file_list));
    if(locate)
    {
        g_file_count = 0; g_file_index = 0;
        if(f_opendir(&DirInfo, "0:/download/bin") != FR_OK) goto directory_error;
        while((ret = f_readdir(&DirInfo, &FileInfo)) == FR_OK && FileInfo.fname[0])
        {
            if(!selectable_firmware(FileInfo.fname, FileInfo.fattrib)) continue;
            if(!strcmp((const char *)app_data.bin_name, FileInfo.fname)) g_file_index = g_file_count;
            g_file_count++;
        }
        if(f_closedir(&DirInfo) != FR_OK) ret = FR_DISK_ERR;
        if(ret != FR_OK) goto directory_error;
    }
    if(!g_file_count) { g_file_error = NULL; return 1; }
    if(f_opendir(&DirInfo, "0:/download/bin") != FR_OK) goto directory_error;
    while((ret = f_readdir(&DirInfo, &FileInfo)) == FR_OK && FileInfo.fname[0])
    {
        if(!selectable_firmware(FileInfo.fname, FileInfo.fattrib)) continue;
        height = UI_NumberedItemHeight(ordinal + 1, FileInfo.fname);
        if(used + height > 236)
        {
            if(g_file_index < ordinal) break; /* 选中项所在页已经完整装入。 */
            g_file_first = ordinal; g_file_visible = 0; used = 0;
            memset(g_file_list, 0, sizeof(g_file_list));
        }
        strcpy(g_file_list[g_file_visible++], FileInfo.fname);
        used += height + UI_LINE_GAP;
        ordinal++;
    }
    if(f_closedir(&DirInfo) != FR_OK) ret = FR_DISK_ERR;
    if(ret != FR_OK || g_file_index >= g_file_first + g_file_visible) goto directory_error;
    g_file_error = NULL;
    return 1;
directory_error:
    g_file_count = 0; g_file_visible = 0;
    memset(g_file_list, 0, sizeof(g_file_list));
    g_file_error = "\xC4\xBF\xC2\xBC\xB6\xC1\xC8\xA1\xCA\xA7\xB0\xDC";
    return 0;
}

static void leave_firmware_list(void)
{
    app_data.mode = 2; app_data.head = 1;
    key_val.mesg = 0; g_file_list_ready = 0; g_ui_ready = 0;
    oled.Refresh_flag = 1;
    draw_main();
}

/* 独立手动列表：短按移动一项，左右长按及100ms连发继续移动；长右不切模式。
 * 只有中键确认时读取候选，失败留在列表；长中随时取消，恢复旧有效选择。 */
void mode3(void)
{
    uint8_t row;
    uint16_t y;
    char page[32];
    const char *footer;
    if(key_val.mesg && key_val.key == key_l_mid)
    { leave_firmware_list(); return; }
    if(!g_file_list_ready)
    {
        if(custombulk_is_busy()) { key_val.mesg = 0; return; }
        UI_EnterPage(11);
        scan_firmware_page(1);
        UI_TextColor(8, 28, "\xB9\xCC\xBC\xFE\xD1\xA1\xD4\xF1", 16, 1, LCD_BLUE); /* 固件选择 */
        g_ui_ready = 0; g_file_list_ready = 1;
        g_file_footer_drawn = NULL;
        show_top(1);
    }
    if(key_val.mesg)
    {
        if(!custombulk_is_busy() && g_file_count)
        {
            switch(key_val.key)
            {
                case key_left: case key_l_left: case key_rep_left:
                    g_file_index = g_file_index ? g_file_index - 1 : g_file_count - 1;
                    g_file_error = NULL; break;
                case key_right: case key_l_right: case key_rep_right:
                    g_file_index = g_file_index + 1 < g_file_count ? g_file_index + 1 : 0;
                    g_file_error = NULL; break;
                case key_mid:
                    if(select_firmware(g_file_list[g_file_index - g_file_first]))
                    { leave_firmware_list(); return; }
                    break;
                default: break;
            }
            if(g_file_index < g_file_first || g_file_index >= g_file_first + g_file_visible)
                scan_firmware_page(0);
        }
        key_val.mesg = 0;
    }
    y = 48;
    for(row = 0; row < g_file_visible; row++)
        y += UI_NumberedItem(row, y, g_file_first + row + 1, g_file_list[row],
                            g_file_first + row == g_file_index) + UI_LINE_GAP;
    /* 十二个列表槽0..11、当前项/总数槽12，顶栏独占13..15。 */
    sprintf(page, "%lu/%lu", (unsigned long)(g_file_count ? g_file_index + 1 : 0),
            (unsigned long)g_file_count);
    UI_FieldColor(12, 128, 28, 104, 16, page, 16, 1, LCD_YELLOW);
    footer = g_file_error ? g_file_error : g_file_count ?
             "\xD6\xD0\xBC\xFC\xC8\xB7\xC8\xCF \xB3\xA4\xB0\xB4\xD6\xD0\xBC\xFC\xB7\xB5\xBB\xD8" :
             "\xCE\xDE\xB9\xCC\xBC\xFE \xB3\xA4\xB0\xB4\xD6\xD0\xBC\xFC\xB7\xB5\xBB\xD8";
    if(footer != g_file_footer_drawn)
    {
        UI_Fill(8, 288, 224, 16, 0);
        UI_TextColor(8, 288, footer, 16, 1, g_file_error ? LCD_RED : LCD_BLUE);
        g_file_footer_drawn = footer;
    }
    show_top(0); oled.Refresh_flag = 0;
}

/* 重建镜像页当前 12 行文字；行文本复用g_file_list，进入/换页各读一次。 */
/* 镜像页显示：自定义说明优先；否则物理名去掉 "<id>_" 前缀，
 * 只显示真实文件名。 */
static const char *image_display_name(const image_record_t *rec, uint8_t id)
{
    if(rec->desc[0] && strncmp(rec->desc, "IMAGE 000", sizeof(rec->desc)) != 0)
        return rec->desc;
    if(rec->bin[0] == (char)('0' + id / 100U) &&
       rec->bin[1] == (char)('0' + (id / 10U) % 10U) &&
       rec->bin[2] == (char)('0' + id % 10U) && rec->bin[3] == '_')
        return rec->bin + 4;
    return rec->bin;
}

static void scan_image_page(void)
{
    uint8_t row, id, count = 0;
    image_record_t rec;
    g_image_first = (uint8_t)((g_image_index / FILE_LIST_ROWS) * FILE_LIST_ROWS);
    UI_ClearFields(0, FILE_LIST_ROWS);
    for(row = 0; row < FILE_LIST_ROWS; row++)
    {
        id = (uint8_t)(g_image_first + row);
        if(id >= IMAGE_SLOTS) break;
        if(image_store_read_record(id, &rec) &&
           (rec.flags & IMAGE_REC_FLAG_VALID) && rec.bin[0] &&
           memchr(rec.bin, 0, sizeof(rec.bin)))
        {
            sprintf(g_file_list[row], "%.24s", image_display_name(&rec, id));
        }
        else strcpy(g_file_list[row], "---");
        count++;
    }
    g_image_visible = count;
}

/* 镜像页移动：短按/连发±1，第一次长按±10；跨页时重建。 */
static void image_move(int8_t delta)
{
    int16_t next = (int16_t)g_image_index + delta;
    while(next < 0) next += (int16_t)IMAGE_SLOTS;
    while(next >= (int16_t)IMAGE_SLOTS) next -= (int16_t)IMAGE_SLOTS;
    g_image_index = (uint8_t)next;
    g_image_error[0] = 0;
    if(g_image_index < g_image_first ||
       g_image_index >= (uint8_t)(g_image_first + g_image_visible))
        scan_image_page();
}

static void leave_image_list(void)
{
    app_data.mode = 2; app_data.head = 3;
    key_val.mesg = 0; g_image_list_ready = 0; g_ui_ready = 0;
    oled.Refresh_flag = 1;
    draw_main();
}

/* 镜像选择页：100 槽，12 行/页；中键应用，长中返回。
 * 自动模式不能进入；进入后 run_task 不再调用 mode2，自动探测自然暂停。 */
void mode5(void)
{
    uint8_t row;
    uint16_t y;
    char page[16];
    const char *footer;
    if(key_val.mesg && key_val.key == key_l_mid)
    { leave_image_list(); return; }
    if(!g_image_list_ready)
    {
        if(custombulk_is_busy()) { key_val.mesg = 0; return; }
        UI_EnterPage(12);
        g_image_index = image_store_current();
        if(g_image_index >= IMAGE_SLOTS) g_image_index = 0;
        g_image_error[0] = 0;
        scan_image_page();
        UI_TextColor(8, 28, "\xBE\xB5\xCF\xF1\xD1\xA1\xD4\xF1", 16, 1, LCD_BLUE); /* 镜像选择 */
        g_image_list_ready = 1;
        g_image_footer_drawn = NULL;
        show_top(1);
    }
    if(key_val.mesg)
    {
        if(!custombulk_is_busy())
        {
            switch(key_val.key)
            {
                case key_left: image_move(-1); break;
                case key_l_left: image_move(-10); break;
                case key_rep_left: image_move(-1); break;
                case key_right: image_move(1); break;
                case key_l_right: image_move(10); break;
                case key_rep_right: image_move(1); break;
                case key_mid:
                {
                    uint8_t err = image_apply(g_image_index);
                    if(err == IMG_OK) { leave_image_list(); return; }
                    sprintf(g_image_error, "IMG ERR %u", (unsigned)err);
                    g_image_footer_drawn = NULL;
                    break;
                }
                default: break;
            }
        }
        key_val.mesg = 0;
    }
    y = 48;
    for(row = 0; row < g_image_visible; row++)
        y += UI_ImageItem(row, y, (uint16_t)(g_image_first + row), g_file_list[row],
                          (g_image_first + row) == g_image_index) + UI_LINE_GAP;
    sprintf(page, "%u/9", (unsigned)(g_image_first / FILE_LIST_ROWS + 1));
    UI_FieldColor(12, 128, 28, 104, 16, page, 16, 1, LCD_YELLOW);
    footer = g_image_error[0] ? g_image_error :
             (image_store_valid() ?
              "\xD6\xD0\xBC\xFC\xC8\xB7\xC8\xCF \xB3\xA4\xB0\xB4\xD6\xD0\xBC\xFC\xB7\xB5\xBB\xD8" :
              "\xCE\xDE\xBE\xB5\xCF\xF1 \xB3\xA4\xB0\xB4\xD6\xD0\xBC\xFC\xB7\xB5\xBB\xD8");
    if(footer != g_image_footer_drawn)
    {
        UI_Fill(8, 288, 224, 16, 0);
        UI_TextColor(8, 288, footer, 16, 1, g_image_error[0] ? LCD_RED : LCD_BLUE);
        g_image_footer_drawn = footer;
    }
    show_top(0); oled.Refresh_flag = 0;
}

/* 初始化硬件并创建任务；循环先处理上位机延后请求，再运行当前界面/烧录状态机。
 * pvParameters - FreeRTOS任务入参，本任务不使用（创建时传NULL）
 * 返回         - 不返回（FreeRTOS任务，内部死循环） */
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
            save_program_selection(); /* 已恢复中断且USB空闲时才持久化，先于自动烧录。 */
//			vTaskDelay(1000);
			switch(app_data.mode)
			{
				case 2:mode2();break;
				case 3:mode3();break;
				case 5:mode5();break;
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
/* 在线仿真任务：循环调用DAP_Thread处理CMSIS-DAP主机请求，仅在DAP_ONLINE_ENABLE=1时编译。
 * pvParameters - FreeRTOS任务入参，本任务不使用（创建时传NULL）
 * 返回         - 不返回（FreeRTOS任务，内部死循环） */
void usb_task(void *pvParameters)
{
	while(1)
	{
		DAP_Thread(NULL);
	}
}

#endif /* DAP_ONLINE_ENABLE */

/* LED指示任务：按当前界面与连接状态驱动HID/MSC/CDC三色灯；
 * CDC_LED_SELFTEST=1时先闪3下自检。
 * pvParameters - FreeRTOS任务入参，本任务不使用（创建时传NULL）
 * 返回         - 不返回（FreeRTOS任务，内部死循环） */
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

/* 按固定周期扫描按键；上一事件尚未消费时保留它，避免长按事件被下一次扫描覆盖。
 * pvParameters - FreeRTOS任务入参，本任务不使用（创建时传NULL）
 * 返回         - 不返回（FreeRTOS任务，内部死循环） */
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



/* 顶行连接、温度、电量独立更新；每秒采样一次，恢复连接和电池图标。
 * force - 1=强制立即重新采样（页面切换时用），0=按1秒周期限频
 * 返回  - 无 */
void show_top(uint8_t force)
{
    static uint32_t sampled;
    static uint8_t ready, last_power = 255, last_icon = 255;
    uint32_t now = auto_now_ms();
    uint8_t power, battery, icon;
    char temperature[16], percent[8];
    if(force || !ready || now - sampled >= 1000)
    { sampled = now; ready = 1; adc_get_val(); adc_trigger(); }
    power = bat_status_get();
    if(val.bat_vol <= 3.5f) battery = 0;
    else if(val.bat_vol >= 4.0f) battery = 100;
    else battery = (uint8_t)((val.bat_vol - 3.5f) * 200.0f);
    if(power) battery = 100;
    icon = battery <= 20 ? 0 : battery <= 40 ? 1 : battery <= 60 ? 2 : battery <= 80 ? 3 : 4;
    if(force || last_power != power)
    {
        UI_Logo(8, 8, power ? 6 : 7);
        UI_ChargingBolt(140, 8, power == 1 || power == 2);
        last_power = power;
    }
    if(force || last_icon != icon) { UI_Logo(152, 8, icon); last_icon = icon; }
    sprintf(temperature, "%.1fC", val.chip_temp); sprintf(percent, "%u%%", battery);
    UI_FieldColor(13, 44, 8, 32, 16, power ? "CON" : "DIS", 16, 1, LCD_YELLOW);
    UI_FieldColor(14, 84, 8, 56, 16, temperature, 16, 1, LCD_YELLOW);
    UI_FieldColor(15, 192, 8, 40, 16, percent, 16, 1, LCD_YELLOW);
}
