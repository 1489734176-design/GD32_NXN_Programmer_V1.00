/* 中文注释补充（2026-10-26）：在线HID报告与专用缓冲区受总开关控制，默认不编译；离线与WinUSB功能不依赖它。 */
/*------------------------------------------------------------------------------
 * MDK Middleware - Component ::USB:Device
 * Copyright (c) 2004-2017 ARM Germany GmbH. All rights reserved.
 *------------------------------------------------------------------------------
 * Name:    USBD_User_HID_0.c
 * Purpose: USB Device Human Interface Device class (HID) User module
 * Rev.:    V6.2.3
 *----------------------------------------------------------------------------*/
/**
 * \addtogroup usbd_hidFunctions
 *
 * USBD_User_HID_0.c implements the application specific functionality of the
 * HID class and is used to receive and send data reports to the USB Host.
 *
 * The implementation must match the configuration file USBD_Config_HID_0.h.
 * The following values in USBD_Config_HID_0.h affect the user code:
 *
 *  - 'Endpoint polling Interval' specifies the frequency of requests
 *    initiated by USB Host for \ref USBD_HIDn_GetReport.
 *
 *  - 'Number of Output Reports' configures the values for \em rid of
 *    \ref USBD_HIDn_SetReport.
 *
 *  - 'Number of Input Reports' configures the values for \em rid of
 *    \ref USBD_HIDn_GetReport and \ref USBD_HID_GetReportTrigger.
 *
 *  - 'Maximum Input Report Size' specifies the maximum value for:
 *       - return of \ref USBD_HIDn_GetReport
 *       - len of \ref USBD_HID_GetReportTrigger.
 *
 *  - 'Maximum Output Report Size' specifies the maximum value for \em len
 *    in \ref USBD_HIDn_SetReport for rtype=HID_REPORT_OUTPUT
 *
 *  - 'Maximum Feature Report Size' specifies the maximum value for \em len
 *    in \ref USBD_HIDn_SetReport for rtype=HID_REPORT_FEATURE
 *
 */


//! [code_USBD_User_HID]

#include <stdint.h>
#include <string.h>
#include "DAP_config.h"
#include "DAP.h"
#include "main.h"

/* 在线仿真专用部分：宏为0时不编译；离线SWD/OPT及WinUSB传输继续保留。 */
#if DAP_ONLINE_ENABLE

uint8_t  	USB_Request[DAP_PACKET_SIZE];
uint8_t 	USB_Request_len;
uint8_t  	USB_Response[DAP_PACKET_SIZE]; 
uint8_t  	USB_Response_len;

void USBD_HID0_SetReport (usb_dev *udev, uint8_t ep_num) {
	BaseType_t Result, xHigherPriorityTaskWoken = pdFALSE;
	
	USB_Request_len= udev->transc_out[ep_num].xfer_count;
	if(USB_Request_len != 0)
	{
		Result = xEventGroupSetBitsFromISR(EventGroupHandle, 0x80U, &xHigherPriorityTaskWoken);
		if(Result != pdFAIL) portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
	}
}


void DAP_Thread (void *argument) {
  (void)   argument;
	
  for (;;) {
		xEventGroupWaitBits(EventGroupHandle, 0x81U, pdTRUE, pdFALSE, portMAX_DELAY);
		
		USB_Response_len = DAP_ExecuteCommand(USB_Request, USB_Response);
		custom_hid_report_send(USB_Response, CUSTOMHID_IN_PACKET);
		USB_Request_len = 0;
		usbd_ep_recev(&usb_hid, CUSTOMHID_OUT_EP, USB_Request, CUSTOMHID_OUT_PACKET);
	}
}














#endif /* DAP_ONLINE_ENABLE */
