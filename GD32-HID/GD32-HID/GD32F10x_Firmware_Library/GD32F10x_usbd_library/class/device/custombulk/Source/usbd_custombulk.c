#include "usbd_custombulk.h"
#include "usbd_composite.h"
#include "usbd_transc.h"
#include "ff.h"
#include <string.h>

/* External FatFs objects from run.c */
extern FATFS fs;
static FIL cb_fd;  /* Local file object, independent from run.c */
extern FILINFO FileInfo;
extern DIR DirInfo;

static custombulk_handler cb_handler;
static uint8_t staged_algorithm[CB_MAX_NAME + 1];
static uint8_t prepare_algorithm[CB_MAX_NAME + 1];
static uint8_t prepare_program[CB_MAX_NAME + 1];
static uint8_t prepare_opt_required;
static volatile uint8_t prepare_request_pending;
static volatile uint8_t prepare_response_pending;
static volatile uint8_t prepare_complete_pending;

/* Forward declarations */
static uint8_t custombulk_init(usb_dev *udev, uint8_t config_index);
static uint8_t custombulk_deinit(usb_dev *udev, uint8_t config_index);
static uint8_t custombulk_req_process(usb_dev *udev, usb_req *req);
static void custombulk_data_in(usb_dev *udev, uint8_t ep_num);
static void custombulk_data_out(usb_dev *udev, uint8_t ep_num);

static void process_command(usb_dev *udev, uint8_t *buf, uint16_t len);

/* strnlen replacement for ARM Compiler V5 */
static uint16_t safe_strnlen(const char *s, uint16_t max)
{
    uint16_t n = 0;
    while (n < max && s[n] != '\0') n++;
    return n;
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
    memset(&cb_handler, 0, sizeof(cb_handler));
    memset(staged_algorithm, 0, sizeof(staged_algorithm));
    memset(prepare_algorithm, 0, sizeof(prepare_algorithm));
    memset(prepare_program, 0, sizeof(prepare_program));
    prepare_opt_required = 0;
    prepare_request_pending = 0;
    prepare_response_pending = 0;
    prepare_complete_pending = 0;

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
    /* Close any open file */
    if (cb_handler.file_opened) {
        f_close(&cb_fd);
        cb_handler.file_opened = 0;
    }
    prepare_request_pending = 0;
    prepare_response_pending = 0;
    prepare_complete_pending = 0;
    memset(staged_algorithm, 0, sizeof(staged_algorithm));
    memset(prepare_algorithm, 0, sizeof(prepare_algorithm));
    memset(prepare_program, 0, sizeof(prepare_program));

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

    /* Get received length */
    len = udev->transc_out[ep_num].xfer_count;

    /* Process the received command */
    process_command(udev, cb_handler.rx_buf, len);
}

/* Send response back to host */
static void send_response(usb_dev *udev, uint8_t status, uint8_t *data, uint16_t data_len)
{
    uint16_t total;

    if (data_len > CUSTOMBULK_IN_PACKET - 1)
        data_len = CUSTOMBULK_IN_PACKET - 1;
    cb_handler.tx_buf[0] = status;
    if (data_len > 0 && data != NULL) {
        memcpy(&cb_handler.tx_buf[1], data, data_len);
    }
    total = 1 + data_len;
    usbd_ep_send(udev, CUSTOMBULK_IN_EP, cb_handler.tx_buf, total);
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


    /* Close any previously open file */
    if (cb_handler.file_opened) {
        f_close(&cb_fd);
    }

    res = f_open(&cb_fd, (const TCHAR *)path, FA_WRITE | FA_CREATE_ALWAYS);
    if (res != FR_OK) {
        send_response(udev, STATUS_ERR, NULL, 0);
        return;
    }

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

    if (cb_handler.file_opened == 0 ||
        (cb_handler.file_opened == 2 &&
         cb_handler.bytes_transferred != cb_handler.file_size)) {
        if (cb_handler.file_opened)
            f_close(&cb_fd);
        cb_handler.file_opened = 0;
        send_response(udev, STATUS_ERR, NULL, 0);
        return;
    }

    res = f_close(&cb_fd);
    cb_handler.file_opened = 0;
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


    res = f_unlink((const TCHAR *)path);
    send_response(udev, (res == FR_OK) ? STATUS_OK : STATUS_ERR, NULL, 0);
}

/* MKDIR: [CMD][path] -> [STATUS] */
static void cmd_mkdir(usb_dev *udev, uint8_t *path, uint16_t path_len)
{
    FRESULT res;

    if (path_len >= CB_MAX_PATH) path_len = CB_MAX_PATH - 1;
    path[path_len] = '\0';


    res = f_mkdir((const TCHAR *)path);
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

    res = f_rename((const TCHAR *)old_path, (const TCHAR *)new_path);
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

    if (len < 2 || !valid_file_name(&buf[1], name_len) ||
        prepare_request_pending || prepare_response_pending) {
        send_response(udev, STATUS_ERR, NULL, 0);
        return;
    }
    memcpy(staged_algorithm, &buf[1], name_len);
    staged_algorithm[name_len] = '\0';
    send_response(udev, STATUS_OK, NULL, 0);
}

/* PREPARE_DOWNLOAD: [CMD][flags][program file name] */
static void cmd_prepare_download(usb_dev *udev, uint8_t *buf, uint16_t len)
{
    uint16_t name_len;

    if (len < 3 || (buf[1] & (uint8_t)~1U) != 0 ||
        !valid_file_name(&buf[2], len - 2) ||
        staged_algorithm[0] == '\0' || prepare_request_pending ||
        prepare_response_pending) {
        send_response(udev, STATUS_ERR, NULL, 0);
        return;
    }
    if (app_data.mode == 4) {
        send_response(udev, STATUS_BUSY, NULL, 0);
        return;
    }

    name_len = len - 2;
    memcpy(prepare_algorithm, staged_algorithm, sizeof(prepare_algorithm));
    memcpy(prepare_program, &buf[2], name_len);
    prepare_program[name_len] = '\0';
    prepare_opt_required = buf[1] & 1U;
    prepare_request_pending = 1;
}

void custombulk_prepare_service(usb_dev *udev)
{
    uint8_t algorithm[CB_MAX_NAME + 1];
    uint8_t program[CB_MAX_NAME + 1];
    uint8_t opt_name[64];
    uint8_t path[80];

    if (!prepare_request_pending)
        return;

    prepare_request_pending = 0;
    memcpy(algorithm, prepare_algorithm, sizeof(algorithm));
    memcpy(program, prepare_program, sizeof(program));

    if (app_data.mode == 4) {
        staged_algorithm[0] = '\0';
        send_response(udev, STATUS_BUSY, NULL, 0);
        return;
    }

    strcpy((char *)path, "0:/download/firmware/");
    strcat((char *)path, (char *)algorithm);
    if (!file_available(path))
        goto prepare_error;

    strcpy((char *)path, "0:/download/bin/");
    strcat((char *)path, (char *)program);
    if (!file_available(path))
        goto prepare_error;

    if (prepare_opt_required) {
        if (!make_opt_name(program, opt_name))
            goto prepare_error;
        strcpy((char *)path, "0:/download/bin/");
        strcat((char *)path, (char *)opt_name);
        if (!file_available(path))
            goto prepare_error;
    }

    prepare_response_pending = 1;
    send_response(udev, STATUS_OK, NULL, 0);
    return;

prepare_error:
    staged_algorithm[0] = '\0';
    send_response(udev, STATUS_ERR, NULL, 0);
}

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

/* Command dispatcher */
static void process_command(usb_dev *udev, uint8_t *buf, uint16_t len)
{
    if (len == 0) return;

    uint8_t cmd = buf[0];

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
