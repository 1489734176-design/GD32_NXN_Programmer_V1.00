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

/* Process deferred prepare requests from the application task. */
void custombulk_prepare_service(usb_dev *udev);
void custombulk_quota_service(usb_dev *udev);
uint8_t custombulk_take_prepare_complete(uint8_t *algorithm_name,
                                         uint8_t *program_name,
                                         uint8_t *opt_required);

/* Send response to host */
uint8_t custombulk_send_response(usb_dev *udev, uint8_t *data, uint16_t len);

#endif /* __USBD_CUSTOMBULK_H */
