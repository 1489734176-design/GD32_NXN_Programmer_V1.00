/* 中文注释补充（2026-10-26）：上位机文件/次数协议：USB回调收包，次数保存和导入确认放到主任务；保持WinUSB接口号及原文件命令。 */
#include "usbd_custombulk.h"
#include "usbd_composite.h"
#include "usbd_transc.h"
#include "ff.h"
#include <string.h>
#include "burn_quota.h"
#include "image_task.h"

extern uint8_t image_apply(uint8_t id);

/* External FatFs objects from run.c */
extern FATFS fs;
static FIL cb_fd;  /* Local file object, independent from run.c */
extern FILINFO FileInfo;
extern DIR DirInfo;

static custombulk_handler cb_handler;
/* OUT端点收到一包后等待IN应答才重装，因此只记录长度，无需增加收包缓存。 */
static volatile uint16_t file_request_length;
static volatile uint8_t file_request_pending, file_cleanup_pending;
static volatile uint8_t file_request_busy;
/* Incremented on every USB endpoint event; used to detect a host that stopped reading. */
static volatile uint32_t custombulk_events;

static uint8_t staged_algorithm[CB_MAX_NAME + 1];
static uint8_t prepare_algorithm[CB_MAX_NAME + 1];
static uint8_t prepare_program[CB_MAX_NAME + 1];
static uint8_t prepare_opt_required;
/* 主BIN按字节累加SUM32；已准备的值供LCD提交时读取。 */
static uint8_t prepare_has_checksum;
static uint32_t prepare_expected_checksum;
static uint32_t prepared_checksum;
/* 变更事件由主任务消费；只读及mkdir不使LCD校验值失效。 */
static volatile uint8_t files_changed_pending;
static volatile uint8_t prepare_request_pending;
static volatile uint8_t prepare_response_pending;
static volatile uint8_t prepare_complete_pending;
static volatile uint8_t quota_request_pending;
static uint8_t quota_request_enabled;
static uint32_t quota_request_remaining;
static uint8_t quota_request_program[CB_MAX_NAME + 1];
static uint8_t quota_request_reimport;
/* The official host stages, uploads main BIN then optional OPT, stages again,
 * and sends a named SET. Only the first successfully closed BIN in that
 * session may reset statistics. Consume it once after a successful SET. */
static uint8_t quota_import_program[BURN_QUOTA_NAME_SIZE];
static uint8_t quota_write_program[BURN_QUOTA_NAME_SIZE];
static uint8_t quota_import_consumed;

/* Forward declarations */
static uint8_t custombulk_init(usb_dev *udev, uint8_t config_index);
static uint8_t custombulk_deinit(usb_dev *udev, uint8_t config_index);
static uint8_t custombulk_req_process(usb_dev *udev, usb_req *req);
static void custombulk_data_in(usb_dev *udev, uint8_t ep_num);
static void custombulk_data_out(usb_dev *udev, uint8_t ep_num);

static void process_command(usb_dev *udev, uint8_t *buf, uint16_t len);
static void send_response(usb_dev *udev, uint8_t status, uint8_t *data, uint16_t data_len);

/* strnlen replacement for ARM Compiler V5 */
static uint16_t safe_strnlen(const char *s, uint16_t max)
{
    uint16_t n = 0;
    while (n < max && s[n] != '\0') n++;
    return n;
}

/* Raw index banks have no FatFs path; only 0:/download/stg.dat is writable. */
static uint8_t image_bank_path(const uint8_t *path)
{
    return (uint8_t)(!strncmp((const char *)path, "0:/img/a/", 9) ||
                     !strncmp((const char *)path, "0:/img/b/", 9));
}

usb_class custombulk_class = {
    .req_cmd     = 0xFFU,
    .init        = custombulk_init,
    .deinit      = custombulk_deinit,
    .req_process = custombulk_req_process,
    .data_in     = custombulk_data_in,
    .data_out    = custombulk_data_out
};

static uint8_t custombulk_init(usb_dev *udev, uint8_t config_index)
{
    custombulk_reset(); /* 重新枚举也延后清理旧会话文件，不能在USB中断中关闭。 */
    memset(&cb_handler, 0, sizeof(cb_handler));
    memset(staged_algorithm, 0, sizeof(staged_algorithm));
    memset(prepare_algorithm, 0, sizeof(prepare_algorithm));
    memset(prepare_program, 0, sizeof(prepare_program));
    prepare_opt_required = 0;
    prepare_request_pending = 0;
    prepare_response_pending = 0;
    prepare_complete_pending = 0;
    quota_request_pending = 0;
    memset(quota_request_program, 0, sizeof(quota_request_program));

    udev->class_data[CUSTOMBULK_INTERFACE] = (void *)&cb_handler;

    /* Initialize endpoints using same PMA addresses as MSC */
    usbd_ep_init(udev, EP_BUF_SNG, CUSTOMBULK_TX_ADDR, &(((Composite_desc_config *)udev->desc->config_desc)->cb_epin));
    usbd_ep_init(udev, EP_BUF_SNG, CUSTOMBULK_RX_ADDR, &(((Composite_desc_config *)udev->desc->config_desc)->cb_epout));

    /* Register endpoint callbacks */
    udev->ep_transc[EP_ID(CUSTOMBULK_IN_EP)][TRANSC_IN] = custombulk_class.data_in;
    udev->ep_transc[CUSTOMBULK_OUT_EP][TRANSC_OUT] = custombulk_class.data_out;

    /* Prepare to receive first OUT packet */
    usbd_ep_recev(udev, CUSTOMBULK_OUT_EP, cb_handler.rx_buf, CUSTOMBULK_OUT_PACKET);

    return USBD_OK;
}

static uint8_t custombulk_deinit(usb_dev *udev, uint8_t config_index)
{
    custombulk_reset(); /* 类卸载与总线复位采用同一延后清理。 */

    usbd_ep_deinit(udev, CUSTOMBULK_IN_EP);
    usbd_ep_deinit(udev, CUSTOMBULK_OUT_EP);

    return USBD_OK;
}

static uint8_t custombulk_req_process(usb_dev *udev, usb_req *req)
{
    /* Handle standard requests for this interface if needed */
    return USBD_OK;
}

static void custombulk_data_in(usb_dev *udev, uint8_t ep_num)
{
    custombulk_events++;
    cb_handler.transfer_active = 0; /* 应答结束后才允许启动下一次目标烧录。 */
    /* IN transfer complete - response sent */
    if (prepare_response_pending) {
        prepare_response_pending = 0;
        prepare_complete_pending = 1;
    }
    /* Re-arm OUT endpoint to receive next command */
    usbd_ep_recev(udev, CUSTOMBULK_OUT_EP, cb_handler.rx_buf, CUSTOMBULK_OUT_PACKET);
}

static void custombulk_data_out(usb_dev *udev, uint8_t ep_num)
{
    uint16_t len;

    custombulk_events++;
    /* Get received length */
    len = udev->transc_out[ep_num].xfer_count;
    /* During programming the main task is inside a long critical section and
     * cannot answer any queued command. Reply BUSY from the interrupt for ALL
     * commands, including GET_STATUS; otherwise the host times out, leaves the
     * IN response unread and the OUT endpoint is never re-armed again. */
    if(len && g_programming_active)
    {
        send_response(udev, STATUS_BUSY, NULL, 0);
        return;
    }
    file_request_length = len;
    file_request_busy = g_programming_active;
    file_request_pending = 1;
}

/* Send response back to host */
static void send_response(usb_dev *udev, uint8_t status, uint8_t *data, uint16_t data_len)
{
    uint16_t total;
    if(file_cleanup_pending) return; /* 断线期间丢弃旧会话应答。 */
    cb_handler.transfer_active = 1;

    if (data_len > CUSTOMBULK_IN_PACKET - 1)
        data_len = CUSTOMBULK_IN_PACKET - 1;
    cb_handler.tx_buf[0] = status;
    if (data_len > 0 && data != NULL) {
        memcpy(&cb_handler.tx_buf[1], data, data_len);
    }
    total = 1 + data_len;
    usbd_ep_send(udev, CUSTOMBULK_IN_EP, cb_handler.tx_buf, total);
}

/* USB复位中断仅使会话失效；真实f_close留给主任务，避免中断重入FatFs。
 * 复位中断和类初始化都会调用，覆盖重插后不经过deinit的硬件路径。 */
void custombulk_reset(void)
{
    if(cb_handler.file_opened == 2 || staged_algorithm[0])
    {
        app_data.bin_name[0] = 0;
        files_changed_pending = 1; /* 中断导入时清选择，普通完成会话重连保留LCD值。 */
    }
    prepared_checksum = 0;
    prepare_expected_checksum = 0; prepare_has_checksum = 0; /* 未完成上传不可继续自动烧录旧选择所指向的残缺文件。 */
    file_cleanup_pending = 1; file_request_pending = 0;
    cb_handler.transfer_active = 0;
    prepare_request_pending = 0; prepare_response_pending = 0;
    prepare_complete_pending = 0; quota_request_pending = 0;
    quota_request_reimport = quota_import_consumed = 0;
    quota_import_program[0] = quota_write_program[0] = 0;
    memset(quota_request_program, 0, sizeof(quota_request_program));
    staged_algorithm[0] = 0; prepare_algorithm[0] = 0; prepare_program[0] = 0;
    oled.Refresh_flag = 1;
}

/* 在主任务依次执行文件命令和断线清理，与界面/烧录共享同一FatFs调用线程。 */
void custombulk_file_service(usb_dev *udev)
{
    uint16_t length;
    if(file_cleanup_pending)
    {
        if(cb_fd.obj.fs) f_close(&cb_fd);
        cb_handler.file_opened = 0;
        file_cleanup_pending = 0;
        oled.Refresh_flag = 1;
    }
    if(!file_request_pending) return;
    length = file_request_length;
    file_request_pending = 0;
    if(file_request_busy && length && cb_handler.rx_buf[0] != CMD_GET_STATUS)
        send_response(udev, STATUS_BUSY, NULL, 0);
    else process_command(udev, cb_handler.rx_buf, length);
}
/* 打开文件、待处理包、导入会话或未发完应答都暂停自动探测/按键烧录。 */
uint8_t custombulk_is_busy(void)
{
    return cb_handler.file_opened || cb_handler.transfer_active || file_request_pending ||
           file_cleanup_pending || staged_algorithm[0] || prepare_request_pending ||
           prepare_response_pending || prepare_complete_pending || quota_request_pending;
}

/* Number of USB endpoint events. The main task compares this counter to detect
 * a host that stopped consuming responses. */
uint32_t custombulk_event_count(void)
{
    return custombulk_events;
}

/* Drop a wedged host session and re-arm the OUT endpoint so the device can
 * receive commands again. Only called from the main task. */
void custombulk_recover_session(usb_dev *udev)
{
    if(cb_fd.obj.fs) f_close(&cb_fd);
    cb_handler.file_opened = 0;
    cb_handler.transfer_active = 0;
    file_request_pending = 0;
    file_cleanup_pending = 0;
    usbd_ep_recev(udev, CUSTOMBULK_OUT_EP, cb_handler.rx_buf, CUSTOMBULK_OUT_PACKET);
    oled.Refresh_flag = 1;
}

/* Recover a session the host has abandoned. idle_ms is the time since the last
 * USB endpoint event. An unread IN response wedges the bulk pair, so it is
 * treated as stale sooner than a half-finished file upload. */
void custombulk_service_stale(usb_dev *udev, uint32_t idle_ms)
{
    if(cb_handler.transfer_active && idle_ms > 6000U)
    {
        custombulk_recover_session(udev);
    }
    else if(cb_handler.file_opened && idle_ms > 15000U)
    {
        if(cb_handler.file_opened == 2)
        {
            /* An abandoned upload may be a truncated file: never burn it. */
            staged_algorithm[0] = 0;
            prepare_algorithm[0] = 0;
            prepare_program[0] = 0;
            prepare_request_pending = 0;
            prepare_response_pending = 0;
            prepare_complete_pending = 0;
            quota_request_pending = 0;
            app_data.bin_name[0] = 0;
            files_changed_pending = 1;
        }
        custombulk_recover_session(udev);
    }
}

/* LIST_DIR: [CMD][path] -> [STATUS][entry1\nentry2\n...] */
static void cmd_list_dir(usb_dev *udev, uint8_t *path, uint16_t path_len)
{
    FRESULT res;
    DIR dir;
    FILINFO fno;
    uint8_t resp[CUSTOMBULK_IN_PACKET - 1];
    uint16_t pos = 0;

    /* Null-terminate and fix path */
    if (path_len >= CB_MAX_PATH) path_len = CB_MAX_PATH - 1;
    path[path_len] = '\0';


    res = f_opendir(&dir, (const TCHAR *)path);
    if (res != FR_OK) {
        send_response(udev, STATUS_ERR, NULL, 0);
        return;
    }

    while (1) {
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == 0) break;

        uint16_t name_len = strlen(fno.fname);
        if (pos + name_len + 2 > sizeof(resp)) break;  /* +2 for separator and possible dir marker */

        memcpy(&resp[pos], fno.fname, name_len);
        pos += name_len;
        if (fno.fattrib & AM_DIR) {
            resp[pos++] = '/';  /* trailing slash for directories */
        }
        resp[pos++] = '\n';
    }
    f_closedir(&dir);

    send_response(udev, STATUS_OK, resp, pos);
}

/* OPEN_WRITE: [CMD][4-byte size][path] -> [STATUS] */
static void cmd_open_write(usb_dev *udev, uint8_t *buf, uint16_t len)
{
    FRESULT res;

    if (len < 5) {  /* 1 cmd + 4 size + at least 0 path */
        send_response(udev, STATUS_ERR, NULL, 0);
        return;
    }

    uint32_t file_size = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);
    uint8_t *path = &buf[5];
    uint16_t path_len = len - 5;

    if (path_len == 0 || path_len >= CB_MAX_PATH) {
        send_response(udev, STATUS_ERR, NULL, 0);
        return;
    }
    path[path_len] = '\0';
    if (image_bank_path(path)) { send_response(udev, STATUS_ERR, NULL, 0); return; }


    /* 先保存选择失效记录；失败或USB复位时不得改动原文件。 */
    if (!app_selection_invalidate() || file_cleanup_pending) {
        send_response(udev, STATUS_ERR, NULL, 0);
        return;
    }

    /* Close any previously open file */
    if (cb_handler.file_opened) {
        f_close(&cb_fd);
    }

    res = f_open(&cb_fd, (const TCHAR *)path, FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK) {
        send_response(udev, STATUS_ERR, NULL, 0);
        return;
    }

    quota_write_program[0] = 0;
    if(staged_algorithm[0] && !strncmp((const char *)path, "0:/download/bin/", 16) &&
       path_len > 16 && path_len - 16 < BURN_QUOTA_NAME_SIZE) {
        memcpy(quota_write_program, path + 16, path_len - 16);
        quota_write_program[path_len - 16] = 0;
    }
    files_changed_pending = 1; /* CREATE_ALWAYS已创建/截断，旧LCD校验值失效。 */
    cb_handler.file_opened = 2;  /* write mode */
    cb_handler.file_size = file_size;
    cb_handler.bytes_transferred = 0;

    send_response(udev, STATUS_OK, NULL, 0);
}

/* WRITE_DATA: [CMD][data...] -> [STATUS] */
static void cmd_write_data(usb_dev *udev, uint8_t *buf, uint16_t len)
{
    UINT bw;
    FRESULT res;

    if (cb_handler.file_opened != 2) {
        send_response(udev, STATUS_ERR, NULL, 0);
        return;
    }

    if (len <= 1) {
        /* No data, just close */
        send_response(udev, STATUS_OK, NULL, 0);
        return;
    }

    res = f_write(&cb_fd, &buf[1], len - 1, &bw);
    if (res != FR_OK || bw != len - 1) {
        send_response(udev, STATUS_ERR, NULL, 0);
        return;
    }

    cb_handler.bytes_transferred += bw;

    send_response(udev, STATUS_OK, NULL, 0);
}

/* CLOSE_FILE: [CMD] -> [STATUS] */
static void cmd_close_file(usb_dev *udev)
{
    FRESULT res;
    uint8_t writing = (cb_handler.file_opened == 2); /* 只对写上传错误撤销导入，读取关闭不影响已有Checksum。 */

    if (cb_handler.file_opened == 0) {
        send_response(udev, STATUS_OK, NULL, 0);
        return;
    }
    if (cb_handler.file_opened == 2 &&
        cb_handler.bytes_transferred != cb_handler.file_size) {
        if (cb_handler.file_opened)
            f_close(&cb_fd);
        if(writing) {
            /* 未传够声明长度的文件不可prepare，即使随后已能正常读回也必须重新导入。 */
            staged_algorithm[0] = 0; app_data.bin_name[0] = 0;
            files_changed_pending = 1; oled.Refresh_flag = 1;
        }
        cb_handler.file_opened = 0;
        send_response(udev, STATUS_ERR, NULL, 0);
        return;
    }

    res = f_close(&cb_fd);
    if(writing && res == FR_OK && staged_algorithm[0] && quota_write_program[0] &&
       !quota_import_program[0] && !quota_import_consumed)
        memcpy(quota_import_program, quota_write_program, sizeof(quota_import_program));
    quota_write_program[0] = 0;
    if(writing && res != FR_OK) {
        /* CLOSE失败说明上传未可靠落盘；清会话与旧校验值，禁止故障恢复后误提交。 */
        staged_algorithm[0] = 0; app_data.bin_name[0] = 0;
        files_changed_pending = 1; oled.Refresh_flag = 1;
    }
    cb_handler.file_opened = 0;
    oled.Refresh_flag = 1; /* 文件完成后重新比较列表/文件字段。 */
    send_response(udev, (res == FR_OK) ? STATUS_OK : STATUS_ERR, NULL, 0);
}

/* OPEN_READ: [CMD][path] -> [STATUS][4-byte size] */
static void cmd_open_read(usb_dev *udev, uint8_t *path, uint16_t path_len)
{
    FRESULT res;
    uint8_t resp[4];

    if (path_len >= CB_MAX_PATH) path_len = CB_MAX_PATH - 1;
    path[path_len] = '\0';


    if (cb_handler.file_opened) {
        f_close(&cb_fd);
    }

    res = f_open(&cb_fd, (const TCHAR *)path, FA_READ);
    if (res != FR_OK) {
        send_response(udev, STATUS_ERR, NULL, 0);
        return;
    }

    cb_handler.file_opened = 1;  /* read mode */
    cb_handler.file_size = f_size(&cb_fd);
    cb_handler.bytes_transferred = 0;

    resp[0] = (uint8_t)(cb_handler.file_size & 0xFF);
    resp[1] = (uint8_t)((cb_handler.file_size >> 8) & 0xFF);
    resp[2] = (uint8_t)((cb_handler.file_size >> 16) & 0xFF);
    resp[3] = (uint8_t)((cb_handler.file_size >> 24) & 0xFF);

    send_response(udev, STATUS_OK, resp, 4);
}

/* READ_DATA: [CMD][1-byte requested_len] -> [STATUS][data...] or [STATUS_EOF] */
static void cmd_read_data(usb_dev *udev, uint8_t *buf, uint16_t len)
{
    UINT br;
    FRESULT res;
    uint8_t req_len;

    if (cb_handler.file_opened != 1) {
        send_response(udev, STATUS_ERR, NULL, 0);
        return;
    }

    req_len = (len > 1) ? buf[1] : CB_MAX_DATA;
    if (req_len > CB_MAX_DATA) req_len = CB_MAX_DATA;

    if (cb_handler.bytes_transferred >= cb_handler.file_size) {
        f_close(&cb_fd);
        cb_handler.file_opened = 0;
        send_response(udev, STATUS_EOF, NULL, 0);
        return;
    }

    uint8_t data_buf[CB_MAX_DATA];
    res = f_read(&cb_fd, data_buf, req_len, &br);
    if (res != FR_OK) {
        send_response(udev, STATUS_ERR, NULL, 0);
        return;
    }

    cb_handler.bytes_transferred += br;

    if (br == 0 || cb_handler.bytes_transferred >= cb_handler.file_size) {
        f_close(&cb_fd);
        cb_handler.file_opened = 0;
        send_response(udev, STATUS_EOF, data_buf, br);
    } else {
        send_response(udev, STATUS_OK, data_buf, br);
    }
}

/* DELETE_FILE: [CMD][path] -> [STATUS] */
static void cmd_delete_file(usb_dev *udev, uint8_t *path, uint16_t path_len)
{
    FRESULT res;

    if (path_len >= CB_MAX_PATH) path_len = CB_MAX_PATH - 1;
    path[path_len] = '\0';
    if (image_bank_path(path)) { send_response(udev, STATUS_ERR, NULL, 0); return; }


    /* 先保存选择失效记录；失败或USB复位时不得改动原文件。 */
    if (!app_selection_invalidate() || file_cleanup_pending) {
        send_response(udev, STATUS_ERR, NULL, 0);
        return;
    }

    res = f_unlink((const TCHAR *)path);
    if(res == FR_OK) { files_changed_pending = 1; oled.Refresh_flag = 1; } /* 上位机目录变更后只比较列表字段。 */
    send_response(udev, (res == FR_OK) ? STATUS_OK : STATUS_ERR, NULL, 0);
}

/* MKDIR: [CMD][path] -> [STATUS] */
static void cmd_mkdir(usb_dev *udev, uint8_t *path, uint16_t path_len)
{
    FRESULT res;

    if (path_len >= CB_MAX_PATH) path_len = CB_MAX_PATH - 1;
    path[path_len] = '\0';


    res = f_mkdir((const TCHAR *)path);
    if(res == FR_OK) oled.Refresh_flag = 1; /* 上位机目录变更后只比较列表字段。 */
    send_response(udev, (res == FR_OK) ? STATUS_OK : STATUS_ERR, NULL, 0);
}

/* GET_STATUS: [CMD] -> [STATUS][file_opened][4-byte bytes_transferred][4-byte file_size] */
static void cmd_get_status(usb_dev *udev)
{
    uint8_t resp[10];
    resp[0] = cb_handler.file_opened;
    resp[1] = (uint8_t)(cb_handler.bytes_transferred & 0xFF);
    resp[2] = (uint8_t)((cb_handler.bytes_transferred >> 8) & 0xFF);
    resp[3] = (uint8_t)((cb_handler.bytes_transferred >> 16) & 0xFF);
    resp[4] = (uint8_t)((cb_handler.bytes_transferred >> 24) & 0xFF);
    resp[5] = (uint8_t)(cb_handler.file_size & 0xFF);
    resp[6] = (uint8_t)((cb_handler.file_size >> 8) & 0xFF);
    resp[7] = (uint8_t)((cb_handler.file_size >> 16) & 0xFF);
    resp[8] = (uint8_t)((cb_handler.file_size >> 24) & 0xFF);

    send_response(udev, STATUS_OK, resp, 9);
}

/* RENAME: [CMD][old_path\0new_path] -> [STATUS] */
static void cmd_rename(usb_dev *udev, uint8_t *buf, uint16_t len)
{
    FRESULT res;
    uint8_t *old_path = &buf[1];
    uint16_t old_len = safe_strnlen((const char *)old_path, len - 1);

    if (old_len >= len - 2) {
        send_response(udev, STATUS_ERR, NULL, 0);
        return;
    }

    uint8_t *new_path = &buf[1 + old_len + 1];
    if (image_bank_path(old_path) || image_bank_path(new_path))
    { send_response(udev, STATUS_ERR, NULL, 0); return; }

    /* 先保存选择失效记录；失败或USB复位时不得改动原文件。 */
    if (!app_selection_invalidate() || file_cleanup_pending) {
        send_response(udev, STATUS_ERR, NULL, 0);
        return;
    }

    res = f_rename((const TCHAR *)old_path, (const TCHAR *)new_path);
    if(res == FR_OK) { files_changed_pending = 1; oled.Refresh_flag = 1; } /* 上位机目录变更后只比较列表字段。 */
    send_response(udev, (res == FR_OK) ? STATUS_OK : STATUS_ERR, NULL, 0);
}

static uint8_t valid_file_name(uint8_t *name, uint16_t len)
{
    uint16_t i;

    if (len == 0 || len > CB_MAX_NAME)
        return 0;
    if ((len == 1 && name[0] == '.') ||
        (len == 2 && name[0] == '.' && name[1] == '.'))
        return 0;
    for (i = 0; i < len; i++) {
        if (name[i] < 0x20 || name[i] == '/' || name[i] == '\\' || name[i] == ':')
            return 0;
    }
    return 1;
}

static uint8_t file_available(const uint8_t *path)
{
    FIL check_fd;
    FRESULT res;

    res = f_open(&check_fd, (const TCHAR *)path, FA_READ);
    if (res != FR_OK)
        return 0;
    res = f_close(&check_fd);
    return (res == FR_OK);
}

static uint8_t make_opt_name(const uint8_t *program, uint8_t *opt_name)
{
    uint16_t len = strlen((const char *)program);
    uint16_t base_len = len;
    uint16_t i;

    for (i = 0; i < len; i++) {
        if (program[i] == '.') {
            base_len = i;
            break;
        }
    }
    if (base_len + 8 >= 64)
        return 0;
    memcpy(opt_name, program, base_len);
    memcpy(&opt_name[base_len], "_opt.bin", 8);
    opt_name[base_len + 8] = '\0';
    return 1;
}

/* STAGE_ALGORITHM: [CMD][algorithm file name] -> [STATUS] */
static void cmd_stage_algorithm(usb_dev *udev, uint8_t *buf, uint16_t len)
{
    uint16_t name_len = len - 1;
    /* 空文件名取消未完成导入，失败后不让待机页一直暂停检测。 */
    if(len == 1)
    {
        /* 先持久化选择失效；失败或USB复位时保留原导入会话。 */
        if (!app_selection_invalidate() || file_cleanup_pending) {
            send_response(udev, STATUS_ERR, NULL, 0);
            return;
        }

        if(cb_handler.file_opened) f_close(&cb_fd); /* 取消写入失败的导入，关闭尚未完成的文件。 */
        cb_handler.file_opened = 0; staged_algorithm[0] = 0;
        prepare_request_pending = 0; prepare_response_pending = 0; prepare_complete_pending = 0;
        prepare_has_checksum = 0; prepare_expected_checksum = 0; prepared_checksum = 0;
        files_changed_pending = 1;
        app_data.bin_name[0] = 0; oled.Refresh_flag = 1; /* 残缺上传必须重新导入/选择后才可烧录。 */
        send_response(udev, STATUS_OK, NULL, 0); return;
    }

    if (len < 2 || !valid_file_name(&buf[1], name_len) ||
        prepare_request_pending || prepare_response_pending || prepare_complete_pending) {
        send_response(udev, STATUS_ERR, NULL, 0);
        return;
    }
    /* 先持久化选择失效；失败或USB复位时保留原导入会话。 */
    if (!app_selection_invalidate() || file_cleanup_pending) {
        send_response(udev, STATUS_ERR, NULL, 0);
        return;
    }

    if(!staged_algorithm[0]) {
        quota_import_program[0] = quota_write_program[0] = 0;
        quota_import_consumed = 0;
    }
    memcpy(staged_algorithm, &buf[1], name_len);
    staged_algorithm[name_len] = '\0';
    files_changed_pending = 1; /* 新导入使旧LCD值失效，成功准备后才重新显示。 */
    send_response(udev, STATUS_OK, NULL, 0);
}

/* PREPARE_DOWNLOAD: flags bit0=OPT，bit1=携带期望SUM32。
 * bit1置位时[2..5]为LE32，文件名从6开始；否则旧格式文件名从2开始。
 * 成功应答为[STATUS_OK][SUM32 LE32]，旧上位机可忽略追加的四字节。 */
static void cmd_prepare_download(usb_dev *udev, uint8_t *buf, uint16_t len)
{
    uint16_t name_offset, name_len;
    if (len < 3 || (buf[1] & (uint8_t)~3U) != 0) {
        send_response(udev, STATUS_ERR, NULL, 0);
        return;
    }
    name_offset = (buf[1] & 2U) ? 6U : 2U;
    if (len <= name_offset || !valid_file_name(&buf[name_offset], len - name_offset) ||
        staged_algorithm[0] == '\0' || prepare_request_pending ||
        prepare_response_pending || prepare_complete_pending) {
        send_response(udev, STATUS_ERR, NULL, 0);
        return;
    }
    /* 必须等待成功CLOSE，不能对仍在上传的文件重复打开并误提交。 */
    if (g_programming_active || cb_handler.file_opened) {
        send_response(udev, STATUS_BUSY, NULL, 0);
        return;
    }
    name_len = len - name_offset;
    memcpy(prepare_algorithm, staged_algorithm, sizeof(prepare_algorithm));
    memcpy(prepare_program, &buf[name_offset], name_len);
    prepare_program[name_len] = '\0';
    prepare_opt_required = buf[1] & 1U;
    prepare_has_checksum = (buf[1] & 2U) ? 1U : 0U;
    prepare_expected_checksum = prepare_has_checksum ?
        (uint32_t)buf[2] | ((uint32_t)buf[3] << 8) |
        ((uint32_t)buf[4] << 16) | ((uint32_t)buf[5] << 24) : 0U;
    prepare_request_pending = 1;
}

/* 局部FIL和64字节栈缓冲回读主BIN；不复用烧录/OPT工作区或USB文件对象。
 * USB复位只标记清理，所有读/关闭仍由当前主任务完成。 */
static uint8_t file_sum32(const uint8_t *path, uint32_t *checksum)
{
    FIL sum_fd;
    uint8_t buffer[64];
    UINT br, request, i;
    FRESULT res;
    uint32_t size, done = 0, sum = 0;
    if (file_cleanup_pending || f_open(&sum_fd, (const TCHAR *)path, FA_READ) != FR_OK)
        return 0;
    size = f_size(&sum_fd);
    while (!file_cleanup_pending && done < size) {
        request = (size - done > sizeof(buffer)) ? sizeof(buffer) : (UINT)(size - done);
        res = f_read(&sum_fd, buffer, request, &br);
        if (file_cleanup_pending || res != FR_OK || br != request)
            break; /* FR_OK也可能短读，残缺固件不能接受。 */
        for (i = 0; i < br; i++) sum += buffer[i]; /* uint32按模2^32溢出。 */
        done += br;
    }
    res = f_close(&sum_fd); /* 成功open后的每条出口恰好close一次。 */
    if (file_cleanup_pending || res != FR_OK || !size || done != size)
        return 0;
    *checksum = sum;
    return 1;
}

/* 主任务检查算法/OPT存在并回读主固件，成功后才排队确认应答。 */
void custombulk_prepare_service(usb_dev *udev)
{
    uint8_t algorithm[CB_MAX_NAME + 1];
    uint8_t program[CB_MAX_NAME + 1];
    uint8_t opt_name[64];
    uint8_t path[80], response[4];
    uint8_t status = STATUS_ERR;
    uint32_t checksum, irq_mask;
    if (!prepare_request_pending)
        return;
    prepare_request_pending = 0;
    memcpy(algorithm, prepare_algorithm, sizeof(algorithm));
    memcpy(program, prepare_program, sizeof(program));
    if (g_programming_active || cb_handler.file_opened) {
        status = STATUS_BUSY;
        goto prepare_error;
    }
    strcpy((char *)path, "0:/download/firmware/");
    strcat((char *)path, (char *)algorithm);
    if (file_cleanup_pending || !file_available(path))
        goto prepare_error;
    strcpy((char *)path, "0:/download/bin/");
    strcat((char *)path, (char *)program);
    if (!file_sum32(path, &checksum))
        goto prepare_error;
    if (prepare_has_checksum && checksum != prepare_expected_checksum)
        goto prepare_error;
    if (prepare_opt_required) {
        if (!make_opt_name(program, opt_name))
            goto prepare_error;
        strcpy((char *)path, "0:/download/bin/");
        strcat((char *)path, (char *)opt_name);
        if (file_cleanup_pending || !file_available(path))
            goto prepare_error;
    }
    response[0] = (uint8_t)checksum; response[1] = (uint8_t)(checksum >> 8);
    response[2] = (uint8_t)(checksum >> 16); response[3] = (uint8_t)(checksum >> 24);
    /* 最后有效性判断、置pending和排队应答不可被reset插入；临界区没有FatFs。 */
    irq_mask = __get_PRIMASK(); __disable_irq();
    if (!file_cleanup_pending && staged_algorithm[0]) {
        prepared_checksum = checksum;
        prepare_response_pending = 1;
        send_response(udev, STATUS_OK, response, sizeof(response));
    }
    __set_PRIMASK(irq_mask);
    return;
prepare_error:
    irq_mask = __get_PRIMASK(); __disable_irq();
    if (!file_cleanup_pending) {
        staged_algorithm[0] = '\0'; prepare_algorithm[0] = '\0'; prepare_program[0] = '\0';
        prepare_response_pending = 0; prepare_complete_pending = 0;
        prepare_has_checksum = 0; prepare_expected_checksum = 0;
        app_data.bin_name[0] = 0; files_changed_pending = 1; oled.Refresh_flag = 1;
        send_response(udev, status, NULL, 0); /* 复位后不发旧应答或重新置busy。 */
    }
    __set_PRIMASK(irq_mask);
}

/* 消费已确认的上位机导入请求，向界面提供算法名、固件名和OPT开关。 */
uint8_t custombulk_take_prepare_complete(uint8_t *algorithm_name,
                                         uint8_t *program_name,
                                         uint8_t *opt_required)
{
    if (!prepare_complete_pending)
        return 0;

    prepare_complete_pending = 0;
    memcpy(algorithm_name, prepare_algorithm, sizeof(prepare_algorithm));
    memcpy(program_name, prepare_program, sizeof(prepare_program));
    *opt_required = prepare_opt_required;
    staged_algorithm[0] = '\0';
    prepare_algorithm[0] = '\0';
    prepare_program[0] = '\0';
    prepare_opt_required = 0;
    return 1;
}

/* take_prepare_complete保留原三参数；run.c提交文件名时读取确认的SUM32。 */
uint32_t custombulk_prepared_checksum(void)
{
    return prepared_checksum;
}

/* 消费一次文件变更事件，兼容调用者已处于短临界区。 */
uint8_t custombulk_take_files_changed(void)
{
    uint8_t changed;
    uint32_t irq_mask = __get_PRIMASK();
    __disable_irq();
    changed = files_changed_pending; files_changed_pending = 0;
    __set_PRIMASK(irq_mask);
    return changed;
}

/* GET: [0D][optional program basename]; SET: [0E][enabled][remaining LE32]
 * [optional program basename]. Short requests use app_data.bin_name.
 * Response: [status][version=2][enabled][fault][remaining LE32]. */
static uint8_t valid_quota_name(uint8_t *name, uint16_t len)
{
    uint16_t i;
    if(!valid_file_name(name, len) || name[len - 1] == '.' || name[len - 1] == ' ')
        return 0;
    for(i = 0; i < len; i++) {
        if(name[i] > 0x7EU || strchr("*?\"<>|", name[i]) != NULL)
            return 0;
    }
    return 1;
}

static uint8_t quota_import_match(const uint8_t *program)
{
    uint8_t i, a, b;
    if(!staged_algorithm[0] || !quota_import_program[0] || quota_import_consumed) return 0;
    for(i = 0; i < BURN_QUOTA_NAME_SIZE; i++) {
        a = program[i]; b = quota_import_program[i];
        if(a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if(b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if(a != b) return 0;
        if(!a) return 1;
    }
    return 0;
}

static void cmd_burn_quota(usb_dev *udev, uint8_t *buf, uint16_t len)
{
    uint32_t remaining = 0;
    uint16_t name_offset, name_len;
    uint8_t *program;
    if(g_programming_active || cb_handler.file_opened || prepare_request_pending ||
       prepare_response_pending || prepare_complete_pending || quota_request_pending) {
        send_response(udev, STATUS_BUSY, NULL, 0);
        return;
    }
    name_offset = buf[0] == CMD_GET_BURN_QUOTA ? 1U : 6U;
    if(len < name_offset || (buf[0] == CMD_SET_BURN_QUOTA && buf[1] > 1)) {
        send_response(udev, STATUS_ERR, NULL, 0);
        return;
    }
    name_len = len - name_offset;
    program = &buf[name_offset];
    if(!name_len) {
        program = app_data.bin_name;
        while(name_len < sizeof(app_data.bin_name) && program[name_len]) name_len++;
    }
    if((name_len && !valid_quota_name(program, name_len)) ||
       (!name_len && buf[0] == CMD_SET_BURN_QUOTA)) {
        send_response(udev, STATUS_ERR, NULL, 0);
        return;
    }
    if(buf[0] == CMD_SET_BURN_QUOTA) {
        remaining = (uint32_t)buf[2] | ((uint32_t)buf[3] << 8) |
                    ((uint32_t)buf[4] << 16) | ((uint32_t)buf[5] << 24);
        if(remaining > BURN_QUOTA_MAX || (!buf[1] && remaining)) {
            send_response(udev, STATUS_ERR, NULL, 0);
            return;
        }
        quota_request_enabled = buf[1];
        quota_request_remaining = remaining;
    }
    memcpy(quota_request_program, program, name_len);
    quota_request_program[name_len] = '\0';
    quota_request_reimport = buf[0] == CMD_SET_BURN_QUOTA && len > name_offset &&
                             quota_import_match(quota_request_program);
    quota_request_pending = buf[0];
}

/* Filesystem/checkpoint and internal Flash work stays in the main task. */
void custombulk_quota_service(usb_dev *udev)
{
    uint8_t cmd, result = BURN_QUOTA_OK, enabled = 0, fault = 0;
    uint8_t response[7], program[CB_MAX_NAME + 1], request_enabled, request_reimport;
    uint32_t remaining = 0, request_remaining, irq_mask;
    if(!quota_request_pending) return;
    /* USB reset can clear pending/name: snapshot only, never hold IRQs over I/O. */
    irq_mask = __get_PRIMASK(); __disable_irq();
    cmd = quota_request_pending;
    memcpy(program, quota_request_program, sizeof(program));
    request_enabled = quota_request_enabled;
    request_reimport = quota_request_reimport;
    request_remaining = quota_request_remaining;
    __set_PRIMASK(irq_mask);
    if(!cmd) return;
    /* OUT is rearmed only after this response completes. */
    if(g_programming_active || cb_handler.file_opened) {
        quota_request_pending = 0;
        send_response(udev, STATUS_BUSY, NULL, 0);
        return;
    }
    if(program[0]) {
        result = burn_quota_select(program);
        if(result != BURN_QUOTA_OK) {
            quota_request_pending = 0;
            send_response(udev, result == BURN_QUOTA_BUSY ? STATUS_BUSY : STATUS_ERR, NULL, 0);
            return;
        }
        if(file_cleanup_pending) {
            quota_request_pending = 0;
            return;
        }
        if(cmd == CMD_SET_BURN_QUOTA) {
            result = request_reimport ? burn_quota_reimport(request_enabled, request_remaining) :
                                        burn_quota_set(request_enabled, request_remaining);
            if(result == BURN_QUOTA_OK && request_reimport) {
                quota_import_consumed = 1;
                quota_import_program[0] = 0;
            }
            oled.Refresh_flag = 1;
        }
        enabled = burn_quota_enabled(); fault = burn_quota_fault();
        remaining = burn_quota_remaining();
    } else if(cmd == CMD_SET_BURN_QUOTA) {
        result = BURN_QUOTA_STORAGE; /* Defend against a lost/empty filename. */
    }
    response[0] = 2; response[1] = enabled; response[2] = fault;
    response[3] = (uint8_t)remaining; response[4] = (uint8_t)(remaining >> 8);
    response[5] = (uint8_t)(remaining >> 16); response[6] = (uint8_t)(remaining >> 24);
    quota_request_pending = 0;
    send_response(udev, result == BURN_QUOTA_OK ? STATUS_OK :
                  (result == BURN_QUOTA_BUSY ? STATUS_BUSY : STATUS_ERR), response, sizeof(response));
}

/* --- Image task commands 0x0F-0x15 ---------------------------------------
 * These run in the main task (custombulk_file_service), so FatFs and raw
 * SPI flash access are allowed. Responses fit the 63-byte data limit. */
static void image_clear_import_session(void)
{
    staged_algorithm[0] = 0;
    prepare_algorithm[0] = 0;
    prepare_program[0] = 0;
    prepare_opt_required = 0;
    prepare_request_pending = 0;
    prepare_response_pending = 0;
    prepare_complete_pending = 0;
    prepare_has_checksum = 0;
    prepare_expected_checksum = 0;
    quota_import_program[0] = 0;
    quota_write_program[0] = 0;
    quota_import_consumed = 0;
}

/* INFO -> [ver][slots][effective][flags][seq:4] */
static void cmd_image_info(usb_dev *udev)
{
    uint8_t resp[8], flags = 0;
    uint32_t seq = image_store_seq();
    if(image_store_valid()) flags |= 1U;
    if(image_store_fault()) flags |= 2U;
    resp[0] = (uint8_t)IMAGE_BANK_VERSION;
    resp[1] = (uint8_t)IMAGE_SLOTS;
    resp[2] = image_store_current();
    resp[3] = flags;
    resp[4] = (uint8_t)seq; resp[5] = (uint8_t)(seq >> 8);
    resp[6] = (uint8_t)(seq >> 16); resp[7] = (uint8_t)(seq >> 24);
    send_response(udev, STATUS_OK, resp, sizeof(resp));
}

/* COMMIT [expected_seq:4][stage_crc:4] -> [detail][new_seq:4][effective][bank_crc:4] */
static void cmd_image_commit(usb_dev *udev, uint8_t *buf, uint16_t len)
{
    uint8_t resp[10], detail = 0, err;
    uint32_t seq, crc, new_seq, bank_crc;
    if(len < 9) { send_response(udev, STATUS_ERR, NULL, 0); return; }
    seq = (uint32_t)buf[1] | ((uint32_t)buf[2] << 8) |
          ((uint32_t)buf[3] << 16) | ((uint32_t)buf[4] << 24);
    crc = (uint32_t)buf[5] | ((uint32_t)buf[6] << 8) |
          ((uint32_t)buf[7] << 16) | ((uint32_t)buf[8] << 24);
    if(g_programming_active || cb_handler.file_opened) {
        send_response(udev, STATUS_BUSY, NULL, 0); return;
    }
    image_clear_import_session();
    if(f_open(&cb_fd, "0:/download/stg.dat", FA_READ) != FR_OK) {
        detail = 6;
        send_response(udev, STATUS_ERR, &detail, 1); return;
    }
    if(!image_store_commit_from_file(&cb_fd, seq, crc, &detail)) {
        f_close(&cb_fd);
        image_store_init();
        (void)image_apply(image_store_current());
        send_response(udev, STATUS_ERR, &detail, 1); return;
    }
    f_close(&cb_fd);
    err = image_apply(image_store_current());
    if(err != IMG_OK) detail = (uint8_t)(0x80U | err);
    new_seq = image_store_seq();
    bank_crc = image_store_crc();
    resp[0] = detail;
    resp[1] = (uint8_t)new_seq; resp[2] = (uint8_t)(new_seq >> 8);
    resp[3] = (uint8_t)(new_seq >> 16); resp[4] = (uint8_t)(new_seq >> 24);
    resp[5] = image_store_current();
    resp[6] = (uint8_t)bank_crc; resp[7] = (uint8_t)(bank_crc >> 8);
    resp[8] = (uint8_t)(bank_crc >> 16); resp[9] = (uint8_t)(bank_crc >> 24);
    send_response(udev, STATUS_OK, resp, sizeof(resp));
}

/* ABORT -> delete stage file and re-apply the active current image. */
static void cmd_image_abort(usb_dev *udev)
{
    uint8_t detail = 0, err, resp[2];
    image_clear_import_session();
    if(cb_handler.file_opened) { f_close(&cb_fd); cb_handler.file_opened = 0; }
    (void)f_unlink("0:/download/stg.dat");
    image_store_init();
    err = image_apply(image_store_current());
    if(err != IMG_OK) detail = err;
    resp[0] = detail; resp[1] = image_store_current();
    send_response(udev, STATUS_OK, resp, sizeof(resp));
}

/* QUOTA_PEEK [name] -> [enabled][known][remaining:4] (read-only, no switch). */
static void cmd_quota_peek(usb_dev *udev, uint8_t *buf, uint16_t len)
{
    uint8_t name[CB_MAX_NAME + 1], enabled = 0, resp[6];
    uint32_t remaining = 0;
    burn_quota_stats_t stats;
    uint16_t n = (len > 1) ? (uint16_t)(len - 1) : 0;
    if(n == 0 || n > CB_MAX_NAME) { send_response(udev, STATUS_ERR, NULL, 0); return; }
    memcpy(name, &buf[1], n); name[n] = 0;
    if(!valid_quota_name(name, n) ||
       !burn_quota_peek(name, &enabled, &remaining, &stats)) {
        send_response(udev, STATUS_ERR, NULL, 0); return;
    }
    resp[0] = enabled; resp[1] = stats.known;
    resp[2] = (uint8_t)remaining; resp[3] = (uint8_t)(remaining >> 8);
    resp[4] = (uint8_t)(remaining >> 16); resp[5] = (uint8_t)(remaining >> 24);
    send_response(udev, STATUS_OK, resp, sizeof(resp));
}

/* GET_FREE -> [free_kb:4][total_kb:4] */
static void cmd_get_free(usb_dev *udev)
{
    FATFS *pfs = 0;
    DWORD nclst = 0;
    uint8_t resp[8];
    uint32_t free_kb, total_kb;
    if(f_getfree("0:", &nclst, &pfs) != FR_OK || !pfs) {
        send_response(udev, STATUS_ERR, NULL, 0); return;
    }
    free_kb = (uint32_t)nclst * pfs->csize / 2U;
    total_kb = (pfs->n_fatent > 2U) ?
               (uint32_t)(pfs->n_fatent - 2U) * pfs->csize / 2U : 0U;
    resp[0] = (uint8_t)free_kb; resp[1] = (uint8_t)(free_kb >> 8);
    resp[2] = (uint8_t)(free_kb >> 16); resp[3] = (uint8_t)(free_kb >> 24);
    resp[4] = (uint8_t)total_kb; resp[5] = (uint8_t)(total_kb >> 8);
    resp[6] = (uint8_t)(total_kb >> 16); resp[7] = (uint8_t)(total_kb >> 24);
    send_response(udev, STATUS_OK, resp, sizeof(resp));
}

/* GET_RECORD [index][offset 0/56] -> [len][data <=56] */
static void cmd_image_get_record(usb_dev *udev, uint8_t *buf, uint16_t len)
{
    image_record_t rec;
    uint8_t resp[1 + CB_MAX_DATA];
    uint16_t off, n;
    if(len < 3) { send_response(udev, STATUS_ERR, NULL, 0); return; }
    if(buf[1] >= IMAGE_SLOTS || buf[2] > IMAGE_RECORD_SIZE) {
        send_response(udev, STATUS_ERR, NULL, 0); return;
    }
    if(!image_store_read_record(buf[1], &rec)) {
        send_response(udev, STATUS_ERR, NULL, 0); return;
    }
    off = buf[2];
    n = (uint16_t)(IMAGE_RECORD_SIZE - off);
    if(n > CB_MAX_DATA) n = CB_MAX_DATA;
    resp[0] = (uint8_t)n;
    memcpy(&resp[1], (const uint8_t *)&rec + off, n);
    send_response(udev, STATUS_OK, resp, (uint16_t)(1U + n));
}

/* Command dispatcher */
static void process_command(usb_dev *udev, uint8_t *buf, uint16_t len)
{
    if (len == 0) return;

    uint8_t cmd = buf[0];
    /* 两层拦截：即使请求在状态切换前已收到，也不允许烧录时访问SPI文件系统。 */
    if(g_programming_active && cmd != CMD_GET_STATUS) {
        send_response(udev, STATUS_BUSY, NULL, 0); return;
    }

    switch (cmd) {
    case CMD_LIST_DIR:
        cmd_list_dir(udev, &buf[1], len - 1);
        break;
    case CMD_OPEN_WRITE:
        cmd_open_write(udev, buf, len);
        break;
    case CMD_WRITE_DATA:
        cmd_write_data(udev, buf, len);
        break;
    case CMD_CLOSE_FILE:
        cmd_close_file(udev);
        break;
    case CMD_OPEN_READ:
        cmd_open_read(udev, &buf[1], len - 1);
        break;
    case CMD_READ_DATA:
        cmd_read_data(udev, buf, len);
        break;
    case CMD_DELETE_FILE:
        cmd_delete_file(udev, &buf[1], len - 1);
        break;
    case CMD_MKDIR:
        cmd_mkdir(udev, &buf[1], len - 1);
        break;
    case CMD_GET_STATUS:
        cmd_get_status(udev);
        break;
    case CMD_RENAME:
        cmd_rename(udev, buf, len);
        break;
    case CMD_STAGE_ALGORITHM:
        cmd_stage_algorithm(udev, buf, len);
        break;
    case CMD_PREPARE_DOWNLOAD:
        cmd_prepare_download(udev, buf, len);
        break;
    case CMD_GET_BURN_QUOTA:
    case CMD_SET_BURN_QUOTA:
        cmd_burn_quota(udev, buf, len);
        break;
    case CMD_IMAGE_INFO:
        cmd_image_info(udev);
        break;
    case CMD_IMAGE_COMMIT:
        cmd_image_commit(udev, buf, len);
        break;
    case CMD_IMAGE_ABORT:
        cmd_image_abort(udev);
        break;
    case CMD_QUOTA_PEEK:
        cmd_quota_peek(udev, buf, len);
        break;
    case CMD_GET_FREE:
        cmd_get_free(udev);
        break;
    case CMD_IMAGE_GET_RECORD:
        cmd_image_get_record(udev, buf, len);
        break;
    default:
        send_response(udev, STATUS_ERR, NULL, 0);
        break;
    }
}

uint8_t custombulk_send_response(usb_dev *udev, uint8_t *data, uint16_t len)
{
    usbd_ep_send(udev, CUSTOMBULK_IN_EP, data, len);
    return USBD_OK;
}
