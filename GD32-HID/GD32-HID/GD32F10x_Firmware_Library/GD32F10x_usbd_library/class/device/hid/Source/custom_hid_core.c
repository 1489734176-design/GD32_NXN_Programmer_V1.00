/*!
    \file    custom_hid_core.c
    \brief   custom HID class driver

    \version 2020-07-17, V3.0.0, firmware for GD32F10x
    \version 2021-06-22, V3.0.1, firmware for GD32F10x
    \version 2021-11-09, V3.0.2, firmware for GD32F10x
    \version 2022-06-30, V3.1.0, firmware for GD32F10x
*/

/*
    Copyright (c) 2022, GigaDevice Semiconductor Inc.

    Redistribution and use in source and binary forms, with or without modification, 
are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this 
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice, 
       this list of conditions and the following disclaimer in the documentation 
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors 
       may be used to endorse or promote products derived from this software without 
       specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT 
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR 
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
OF SUCH DAMAGE.
*/

#include "usbd_transc.h"
#include "custom_hid_core.h"
#include <string.h>
#include "bsp_led.h"

#define USBD_VID                     0x28E9U
#define USBD_PID                     0x128AU

#define WBVAL(x)                          (x & 0xFF),((x >> 8) & 0xFF)
#define USBD_NUM_DEV_CAPABILITIES         1
#define USBD_WINUSB_DESC_LEN              28
#define USBD_BOS_WTOTALLENGTH             (USB_BOS_DESC_LEN + USBD_WINUSB_DESC_LEN)
/* WinUSB Microsoft OS 2.0 Descriptor Types */
#define WINUSB_SET_HEADER_DESCRIPTOR_TYPE           0x00
#define WINUSB_SUBSET_HEADER_CONFIGURATION_TYPE     0x01
#define WINUSB_SUBSET_HEADER_FUNCTION_TYPE          0x02
#define WINUSB_FEATURE_COMPATIBLE_ID_TYPE           0x03
#define WINUSB_FEATURE_REG_PROPERTY_TYPE            0x04
#define WINUSB_FEATURE_MIN_RESUME_TIME_TYPE         0x05
#define WINUSB_FEATURE_MODEL_ID_TYPE                0x06
#define WINUSB_FEATURE_CCGP_DEVICE_TYPE             0x07
#define WINUSB_PROP_DATA_TYPE_REG_SZ                0x01
#define WINUSB_PROP_DATA_TYPE_REG_MULTI_SZ          0x07
/* WinUSB Microsoft OS 2.0 descriptor request codes */
#define WINUSB_REQUEST_GET_DESCRIPTOR_SET          	0x07
#define WINUSB_REQUEST_SET_ALT_ENUM                	0x08
/* WinUSB Microsoft OS 2.0 descriptor sizes */
#define WINUSB_DESCRIPTOR_SET_HEADER_SIZE          	10
#define WINUSB_FUNCTION_SUBSET_HEADER_SIZE         	8
#define WINUSB_FEATURE_COMPATIBLE_ID_SIZE          	20

#define FUNCTION_SUBSET_LEN                 				160
#define DEVICE_INTERFACE_GUIDS_FEATURE_LEN  				132

#define USBD_WINUSB_DESC_SET_LEN    							  (WINUSB_DESCRIPTOR_SET_HEADER_SIZE + FUNCTION_SUBSET_LEN)
#define USB_DEVICE_CAPABILITY_PLATFORM              5
#define USBD_WINUSB_VENDOR_CODE     								0x20


extern void USBD_HID0_SetReport (usb_dev *udev, uint8_t ep_num);
/* Note:it should use the C99 standard when compiling the below codes */
/* USB standard device descriptor */
const usb_desc_dev custom_hid_dev_desc =
{
    .header = 
     {
         .bLength          = USB_DEV_DESC_LEN, 
         .bDescriptorType  = USB_DESCTYPE_DEV,
     },
#ifdef USBD_BOS_ENABLE
		.bcdUSB                = 0x0210U,
#else
    .bcdUSB                = 0x0200U,
#endif
    .bDeviceClass          = 0x00U,
    .bDeviceSubClass       = 0x00U,
    .bDeviceProtocol       = 0x00U,
    .bMaxPacketSize0       = USBD_EP0_MAX_SIZE,
    .idVendor              = USBD_VID,
    .idProduct             = USBD_PID,
    .bcdDevice             = 0x0100U,
    .iManufacturer         = STR_IDX_MFC,
    .iProduct              = STR_IDX_PRODUCT,
    .iSerialNumber         = STR_IDX_SERIAL,
    .bNumberConfigurations = USBD_CFG_MAX_NUM,
};

/* USB device configure descriptor */
usb_hid_desc_config_set custom_hid_config_desc = 
{
    .config = 
    {
        .header = 
         {
             .bLength         = sizeof(usb_desc_config), 
             .bDescriptorType = USB_DESCTYPE_CONFIG 
         },
        .wTotalLength         = sizeof(usb_hid_desc_config_set),
        .bNumInterfaces       = 0x01U,
        .bConfigurationValue  = 0x01U,
        .iConfiguration       = 0x00U,
        .bmAttributes         = 0x80U,
        .bMaxPower            = 0x32U
    },

    .hid_itf = 
    {
        .header = 
         {
             .bLength         = sizeof(usb_desc_itf), 
             .bDescriptorType = USB_DESCTYPE_ITF 
         },
        .bInterfaceNumber     = 0x00U,
        .bAlternateSetting    = 0x00U,
        .bNumEndpoints        = 0x02U,
        .bInterfaceClass      = USB_HID_CLASS,
        .bInterfaceSubClass   = 0x00U,
        .bInterfaceProtocol   = 0x00U,
        .iInterface           = 0x00U
    },

    .hid_vendor = 
    {
        .header = 
         {
             .bLength         = sizeof(usb_desc_hid),
             .bDescriptorType = USB_DESCTYPE_HID 
         },
        .bcdHID               = 0x0111U,
        .bCountryCode         = 0x00U,
        .bNumDescriptors      = 0x01U,
        .bDescriptorType      = USB_DESCTYPE_REPORT,
        .wDescriptorLength    = DESC_LEN_REPORT,
    },

    .hid_epin = 
    {
        .header = 
         {
             .bLength         = sizeof(usb_desc_ep), 
             .bDescriptorType = USB_DESCTYPE_EP 
         },
        .bEndpointAddress     = CUSTOMHID_IN_EP,
        .bmAttributes         = USB_EP_ATTR_INT,
        .wMaxPacketSize       = CUSTOMHID_IN_PACKET,
        .bInterval            = 0x01U
    },

    .hid_epout = 
    {
        .header = 
         {
             .bLength         = sizeof(usb_desc_ep), 
             .bDescriptorType = USB_DESCTYPE_EP
         },
        .bEndpointAddress     = CUSTOMHID_OUT_EP,
        .bmAttributes         = USB_EP_ATTR_INT,
        .wMaxPacketSize       = CUSTOMHID_OUT_PACKET,
        .bInterval            = 0x01U
    }
};

#ifdef USBD_BOS_ENABLE
const uint8_t binaryobjectstore_desc[] = {
		USB_BOS_DESC_LEN, 													// bLength
    USB_DESCTYPE_BOS, 		// bDescriptorType
    WBVAL(USBD_BOS_WTOTALLENGTH), 							// wTotalLength
    USBD_NUM_DEV_CAPABILITIES,                  // bNumDeviceCaps

    /*** MS OS 2.0 descriptor platform capability descriptor ***/
    USBD_WINUSB_DESC_LEN, 											// bLength
    USB_DESCTYPE_CDT, 													// bDescriptorType
    USB_DEVICE_CAPABILITY_PLATFORM, 						// bDevCapabilityType: PLATFORM (05H)
    0x00, // bReserved
    0xDF, 0x60, 0xDD, 0xD8,     								// PlatformCapabilityUUID
    0x89, 0x45, 0xC7, 0x4C,
    0x9C, 0xD2, 0x65, 0x9D,
    0x9E, 0x64, 0x8A, 0x9F,

    0x00, 0x00, 0x03, 0x06,        							// dwWindowsVersion: 0x06030000 for Windows 8.1
    WBVAL(USBD_WINUSB_DESC_SET_LEN), 						// wDescriptorSetTotalLength: size of MS OS 2.0 descriptor set
    USBD_WINUSB_VENDOR_CODE,         						// bMS_VendorCode
    0x00,                       								// bAltEnumCmd
};

const uint8_t winusbdescriptorsetdescriptor[] = {
    /*** Microsoft OS 2.0 Descriptor Set Header ***/
    WBVAL(WINUSB_DESCRIPTOR_SET_HEADER_SIZE), // wLength
    WBVAL(WINUSB_SET_HEADER_DESCRIPTOR_TYPE),   //wDescriptorType

    0x00, 0x00, 0x03, 0x06,     // dwWindowsVersion: 0x06030000 for Windows 8.1

    WBVAL(USBD_WINUSB_DESC_SET_LEN),          // wDescriptorSetTotalLength

    /*** Microsoft OS 2.0 function subset header ***/
    WBVAL(WINUSB_FUNCTION_SUBSET_HEADER_SIZE),/* wLength */
    WBVAL(WINUSB_SUBSET_HEADER_FUNCTION_TYPE),/* wDescriptorType */
    0,                                        /* bFirstInterface USBD_BULK_IF_NUM*/
    0,                                        /* bReserved */
    WBVAL(FUNCTION_SUBSET_LEN),               /* wSubsetLength */

    /*** Microsoft OS 2.0 compatible ID descriptor ***/
    WBVAL(WINUSB_FEATURE_COMPATIBLE_ID_SIZE), /* wLength */
    WBVAL(WINUSB_FEATURE_COMPATIBLE_ID_TYPE), /* wDescriptorType */
    'W', 'I', 'N', 'U', 'S', 'B', 0, 0,       /* CompatibleId*/
    0, 0, 0, 0, 0, 0, 0, 0,                   /* SubCompatibleId*/

    /*** MS OS 2.0 registry property descriptor ***/
    WBVAL(DEVICE_INTERFACE_GUIDS_FEATURE_LEN),/* wLength */
    WBVAL(WINUSB_FEATURE_REG_PROPERTY_TYPE),  /* wDescriptorType */
    WBVAL(WINUSB_PROP_DATA_TYPE_REG_MULTI_SZ), /* wPropertyDataType */

    // PropertyName: "DeviceInterfaceGUID"
    WBVAL(42), /* wPropertyNameLength */
    'D',0,'e',0,'v',0,'i',0,'c',0,'e',0,
    'I',0,'n',0,'t',0,'e',0,'r',0,'f',0,'a',0,'c',0,'e',0,
    'G',0,'U',0,'I',0,'D',0,'s',0,0,0,

    // PropertyData: "{CDB3B5AD-293B-4663-AA36-1AAE46463776}"
    WBVAL(80), /* wPropertyDataLength */
    '{',0,
    'C',0,'D',0,'B',0,'3',0,'B',0,'5',0,'A',0,'D',0,'-',0,
    '2',0,'9',0,'3',0,'B',0,'-',0,
    '4',0,'6',0,'6',0,'3',0,'-',0,
    'A',0,'A',0,'3',0,'6',0,'-',
    0,'1',0,'A',0,'A',0,'E',0,'4',0,'6',0,'4',0,'6',0,'3',0,'7',0,'7',0,'6',0,
    '}',0,0,0,0,0,
};

#endif
/* USB language ID descriptor */
const static usb_desc_LANGID usbd_language_id_desc = 
{
    .header = 
     {
         .bLength = sizeof(usb_desc_LANGID), 
         .bDescriptorType = USB_DESCTYPE_STR
     },
    .wLANGID = ENG_LANGID
};

/* USB manufacture string */
const static usb_desc_str manufacturer_string = 
{
    .header = 
     {
         .bLength         = USB_STRING_LEN(20U), 
         .bDescriptorType = USB_DESCTYPE_STR,
     },
    .unicode_string = {'C', 'M', 'S', 'I', 'S', '-', 'D', 'A', 'P','L','I','N','K', ' ','b','y',' ', 'A','R','M'}
};

/* USB product string */
const static usb_desc_str product_string = 
{
    .header = 
     {
         .bLength         = USB_STRING_LEN(14U), 
         .bDescriptorType = USB_DESCTYPE_STR,
     },
    .unicode_string = {'C', 'M', 'S', 'I', 'S', '-', 'D', 'A', 'P', ' ','v','2','.','1'}
};

/* USB serial string */
static usb_desc_str serial_string = 
{
    .header = 
     {
         .bLength         = USB_STRING_LEN(12U), 
         .bDescriptorType = USB_DESCTYPE_STR,
     },
		 .unicode_string = {'9','7','8','9','A','0','0','0','0','0','0','1'}
};

const static usb_desc_str itf_string = {
    .header = {
         .bLength         = USB_STRING_LEN(17U),
         .bDescriptorType = USB_DESCTYPE_STR,
     },
    .unicode_string = {'C', 'M', 'S', 'I', 'S', '-', 'D', 'A', 'P',' ','v','2','.','1','-', 'G','D'}
};

const static usb_desc_str config_string = {
    .header = {
         .bLength         = USB_STRING_LEN(17U),
         .bDescriptorType = USB_DESCTYPE_STR,
     },
    .unicode_string = {'C', 'M', 'S', 'I', 'S', '-', 'D', 'A', 'P',' ','v','2','.','1','-', 'G','D'}
};



/* USB string descriptor set */
static uint8_t* usbd_hid_strings[] = 
{
    [STR_IDX_LANGID]  = (uint8_t *)&usbd_language_id_desc,
    [STR_IDX_MFC]     = (uint8_t *)&manufacturer_string,
    [STR_IDX_PRODUCT] = (uint8_t *)&product_string,
    [STR_IDX_SERIAL]  = (uint8_t *)&serial_string,
		[STR_IDX_ITF] 		= (uint8_t *)&itf_string,
		[STR_IDX_CONFIG]  = (uint8_t *)&config_string
};

usb_desc custom_hid_desc =
{
    .dev_desc    = (uint8_t *)&custom_hid_dev_desc,
    .config_desc = (uint8_t *)&custom_hid_config_desc,
#ifdef USBD_BOS_ENABLE
		.bos_desc 	 = (uint8_t *)&binaryobjectstore_desc,
#endif
    .strings     = usbd_hid_strings
};

/* local function prototypes ('static') */
static uint8_t custom_hid_init         (usb_dev *udev, uint8_t config_index);
static uint8_t custom_hid_deinit       (usb_dev *udev, uint8_t config_index);
static uint8_t custom_hid_req_handler  (usb_dev *udev, usb_req *req);
//static uint8_t custom_hid_vendor_handler  (usb_dev *udev, usb_req *req);
static void custom_hid_data_in         (usb_dev *udev, uint8_t ep_num);
static void custom_hid_data_out        (usb_dev *udev, uint8_t ep_num);

usb_class custom_hid_class = {
    .req_cmd       = 0xFFU,

    .init          		= custom_hid_init,
    .deinit        		= custom_hid_deinit,
    .req_process   		= custom_hid_req_handler,
    .data_in       		= custom_hid_data_in,
    .data_out      		= custom_hid_data_out
};

const uint8_t customhid_report_descriptor[DESC_LEN_REPORT] =
{
	0X06,0X00,0XFF,  
	0X09 ,0X01,  
	0XA1 ,0X01,  
	0X15 ,0X00,  
	0X26 ,0XFF ,0X00,  
	0X75 ,0X08,  
	0X95 ,0X40,  
	0X09 ,0X01,  
	0X81 ,0X02,  
	0X95 ,0X40,  
	0X09 ,0X01,  
	0X91 ,0X02,  
	0X95 ,0X01,  
	0X09 ,0X01, 
	0XB1 ,0X02,  
	0XC0, 
};

/*!
    \brief      register HID interface operation functions
    \param[in]  udev: pointer to USB device instance
    \param[in]  hid_fop: HID operation functions structure
    \param[out] none
    \retval     USB device operation status
*/
uint8_t custom_hid_itfop_register (usb_dev *udev, hid_fop_handler *hid_fop)
{
    if (NULL != hid_fop) {
        udev->user_data = hid_fop;

        return USBD_OK;
    }

    return USBD_FAIL;
}

/*!
    \brief      send custom HID report
    \param[in]  udev: pointer to USB device instance
    \param[in]  report: pointer to HID report
    \param[in]  len: data length
    \param[out] none
    \retval     USB device operation status
*/
uint8_t custom_hid_report_send (uint8_t *report, uint16_t len)
{
    usbd_ep_send (&usb_hid, CUSTOMHID_IN_EP, report, len);

    return USBD_OK;
}

/*!
    \brief      initialize the HID device
    \param[in]  udev: pointer to USB device instance
    \param[in]  config_index: configuration index
    \param[out] none
    \retval     USB device operation status
*/
static uint8_t custom_hid_init (usb_dev *udev, uint8_t config_index)
{
    static custom_hid_handler hid_handler;

    memset((void *)&hid_handler, 0, sizeof(custom_hid_handler));

    /* initialize the data endpoints */
    usbd_ep_init(udev, EP_BUF_SNG, HID_TX_ADDR, &(custom_hid_config_desc.hid_epin));
    usbd_ep_init(udev, EP_BUF_SNG, HID_RX_ADDR, &(custom_hid_config_desc.hid_epout));

    usbd_ep_recev (udev, CUSTOMHID_OUT_EP, hid_handler.data_out, CUSTOMHID_OUT_PACKET);

    udev->ep_transc[EP_ID(CUSTOMHID_IN_EP)][TRANSC_IN] = custom_hid_class.data_in;
    udev->ep_transc[EP_ID(CUSTOMHID_OUT_EP)][TRANSC_OUT] = custom_hid_class.data_out;

    udev->class_data[CUSTOM_HID_INTERFACE] = (void *)&hid_handler;

    if (udev->user_data != NULL) {
        for (uint8_t i = 0U; i < MAX_PERIPH_NUM; i++) {
            if (((hid_fop_handler *)udev->user_data)->periph_config[i] != NULL) {
                ((hid_fop_handler *)udev->user_data)->periph_config[i]();
            }
        }
    }

    return USBD_OK;
}

/*!
    \brief      de-initialize the HID device
    \param[in]  udev: pointer to USB device instance
    \param[in]  config_index: configuration index
    \param[out] none
    \retval     USB device operation status
*/
static uint8_t custom_hid_deinit (usb_dev *udev, uint8_t config_index)
{
    /* deinitialize HID endpoints */
    usbd_ep_deinit(udev, CUSTOMHID_IN_EP);
    usbd_ep_deinit(udev, CUSTOMHID_OUT_EP);
    return USBD_OK;
}

/*!
    \brief      handle the HID class-specific requests
    \param[in]  udev: pointer to USB device instance
    \param[in]  req: device class-specific request
    \param[out] none
    \retval     USB device operation status
*/
static uint8_t custom_hid_req_handler (usb_dev *udev, usb_req *req)
{
    uint8_t status = REQ_NOTSUPP;

    custom_hid_handler *hid = (custom_hid_handler *)udev->class_data[CUSTOM_HID_INTERFACE];

    switch (req->bRequest) {
    case USB_GET_DESCRIPTOR:
        if (USB_DESCTYPE_REPORT == (req->wValue >> 8U)) {
            usb_transc_config(&udev->transc_in[0], 
                              (uint8_t *)customhid_report_descriptor, 
                              USB_MIN(DESC_LEN_REPORT, req->wLength), 
                              0U);

            status = REQ_SUPP;
        } else if (USB_DESCTYPE_HID == (req->wValue >> 8U)) {
            usb_transc_config(&udev->transc_in[0U], 
                              (uint8_t *)(&(custom_hid_config_desc.hid_vendor)), 
                              USB_MIN(9U, req->wLength), 
                              0U);
        }
        break;

    case GET_REPORT:
        if (2U == req->wLength) {
            usb_transc_config(&udev->transc_in[0], hid->data_out, 2U, 0U);

            status = REQ_SUPP;
        }
        break;

    case GET_IDLE:
        usb_transc_config(&udev->transc_in[0], (uint8_t *)&hid->idlestate, 1U, 0U);

        status = REQ_SUPP;
        break;

    case GET_PROTOCOL:
        usb_transc_config(&udev->transc_in[0], (uint8_t *)&hid->protocol, 1U, 0U);

        status = REQ_SUPP;
        break;

    case SET_REPORT:
        hid->reportID = (uint8_t)(req->wValue);

        usb_transc_config(&udev->transc_out[0], hid->data_out, req->wLength, 0U);

        status = REQ_SUPP;
        break;

    case SET_IDLE:
        hid->idlestate = (uint8_t)(req->wValue >> 8);

        status = REQ_SUPP;
        break;

    case SET_PROTOCOL:
        hid->protocol = (uint8_t)(req->wValue);

        status = REQ_SUPP;
        break;

    default:
        break;
    }

    return status;
}

uint8_t custom_hid_vendor_handler  (usb_dev *udev, usb_req *req)
{
#ifdef USBD_BOS_ENABLE
//    uint16_t packet, len;
	
    if(req->bmRequestType & 0x80) {
        // Device to host
        if(req->bRequest == USBD_WINUSB_VENDOR_CODE) {
						if(req->wIndex == 7) {
//                packet = USBD_WINUSB_DESC_SET_LEN;
//                len = (req->wLength < packet) ? req->wLength : packet;

//                udev->transc_in[0].xfer_buf = (uint8_t *)winusbdescriptorsetdescriptor;
//                udev->transc_in[0].xfer_len = len;
								usb_transc_config(&udev->transc_in[0], 
                                 (uint8_t *)winusbdescriptorsetdescriptor, 
																 USB_MIN(USBD_WINUSB_DESC_SET_LEN, req->wLength), 
                                 0U);
								return REQ_SUPP;
            }
        } 
    }
#endif
		return REQ_NOTSUPP;
}

/*!
    \brief      handle custom HID data in transaction
    \param[in]  udev: pointer to USB device instance
    \param[in]  ep_num: endpoint number
    \param[out] none
    \retval     none
*/
static void custom_hid_data_in (usb_dev *udev, uint8_t ep_num)
{
		app_data.hid_led_blink = 1;
		app_data.hid_cnt++;
}

/*!
    \brief      handle custom HID data out transaction
    \param[in]  udev: pointer to USB device instance
    \param[in]  ep_num: endpoint number
    \param[out] none
    \retval     none
*/

static void custom_hid_data_out (usb_dev *udev, uint8_t ep_num)
{
		app_data.hid_led_blink = 1;
		app_data.hid_cnt++;
		USBD_HID0_SetReport(udev, ep_num);
}
