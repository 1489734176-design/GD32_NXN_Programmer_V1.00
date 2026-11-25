/* 中文注释补充（2026-10-26）：WinUSB文件传输接口固定为1；新增0x0D读次数、0x0E设置次数，仍保留原文件传输协议。 */
#ifndef __USBD_CUSTOMBULK_H
#define __USBD_CUSTOMBULK_H

#include "usbd_core.h"
#include "main.h"

/* Custom Bulk interface index */
#define CUSTOMBULK_INTERFACE        1U

/* Endpoint definitions */
#define CUSTOMBULK_IN_EP            EP_IN(2U)   /* 0x82 */
#define CUSTOMBULK_OUT_EP           EP_OUT(2U)  /* 0x02 */
#define CUSTOMBULK_IN_PACKET        64U
#define CUSTOMBULK_OUT_PACKET       64U

/* Protocol command IDs */
#define CMD_LIST_DIR                0x01U
#define CMD_OPEN_WRITE              0x02U
#define CMD_WRITE_DATA              0x03U
#define CMD_CLOSE_FILE              0x04U
#define CMD_OPEN_READ               0x05U
#define CMD_READ_DATA               0x06U
#define CMD_DELETE_FILE             0x07U
#define CMD_MKDIR                   0x08U
#define CMD_GET_STATUS              0x09U
#define CMD_RENAME                  0x0AU
#define CMD_STAGE_ALGORITHM         0x0BU
#define CMD_PREPARE_DOWNLOAD        0x0CU
#define CMD_GET_BURN_QUOTA          0x0DU
#define CMD_SET_BURN_QUOTA          0x0EU
#define CMD_IMAGE_INFO            0x0FU
#define CMD_IMAGE_COMMIT          0x10U
#define CMD_IMAGE_STATUS          0x11U
#define CMD_IMAGE_ABORT           0x12U
#define CMD_QUOTA_PEEK            0x13U
#define CMD_GET_FREE              0x14U
#define CMD_IMAGE_GET_RECORD      0x15U

/* Response status codes */
#define STATUS_OK                   0x00U
#define STATUS_ERR                  0x01U
#define STATUS_BUSY                 0x02U
#define STATUS_EOF                  0x03U

/* Max path and file-name lengths */
#define CB_MAX_PATH                 60U
#define CB_MAX_NAME                 31U

/* Max data payload per packet (64 - 1 cmd - 1 status - reserved) */
#define CB_MAX_DATA                 56U

/* Custom bulk handler */
typedef struct {
    uint8_t  rx_buf[CUSTOMBULK_OUT_PACKET];
    uint8_t  tx_buf[CUSTOMBULK_IN_PACKET];
    uint8_t  file_opened;           /* 0=closed, 1=read, 2=write */
    uint8_t  transfer_active;
    uint32_t file_size;
    uint32_t bytes_transferred;
} custombulk_handler;

extern usb_class custombulk_class;
/* 文件指令及断线清理由主任务执行；忙标志用于暂停自动烧录。 */
void custombulk_file_service(usb_dev *udev);
/* 总线复位/类卸载只标记延后清理，不在中断执行FatFs。 */
void custombulk_reset(void);
uint8_t custombulk_is_busy(void);
/* Stale USB session recovery (main task only). */
uint32_t custombulk_event_count(void);
void custombulk_recover_session(usb_dev *udev);
void custombulk_service_stale(usb_dev *udev, uint32_t idle_ms);

/* Process deferred prepare requests from the application task. */
/* 在主任务中完成文件检查和导入确认，避免USB中断中执行耗时文件操作。 */
void custombulk_prepare_service(usb_dev *udev);
/* 在主任务中保存/读取次数，返回版本、开关、异常标志和剩余值；避免USB中断中擦写Flash。 */
void custombulk_quota_service(usb_dev *udev);
/* 消费已确认的上位机导入请求，向界面提供算法名、固件名和OPT开关。 */
uint8_t custombulk_take_prepare_complete(uint8_t *algorithm_name,
                                         uint8_t *program_name,
                                         uint8_t *opt_required);

/* 成功准备的主固件SUM32，保持到下一成功prepare、reset或取消；值0也合法。 */
uint32_t custombulk_prepared_checksum(void);
/* 主任务消费文件变更标记使LCD旧值失效；只读和mkdir不产生标记。 */
uint8_t custombulk_take_files_changed(void);

/* Send response to host */
uint8_t custombulk_send_response(usb_dev *udev, uint8_t *data, uint16_t len);

#endif /* __USBD_CUSTOMBULK_H */
