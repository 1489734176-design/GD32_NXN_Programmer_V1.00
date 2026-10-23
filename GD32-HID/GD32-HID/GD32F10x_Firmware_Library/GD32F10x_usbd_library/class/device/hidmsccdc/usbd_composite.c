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
#include "usbd_composite.h"
#include "custom_hid_core.h"
#include "usbd_custombulk.h"
#include "cdc_acm_core.h"
#include "usbd_composite.h"
#include <string.h>
#include "bsp_led.h"

#define USBD_VID                     0x28E9U
#define USBD_PID                     0x128AU

#define WBVAL(x)                          (x & 0xFF),((x >> 8) & 0xFF)
#define USBD_NUM_DEV_CAPABILITIES         1
#define USBD_WINUSB_DESC_LEN              28
#define USBD_WINUSB_PLATFORM_CAP_LEN      (USBD_WINUSB_DESC_LEN + USBD_WINUSB_DESC_SET_LEN)
#define USBD_BOS_WTOTALLENGTH             (USB_BOS_DESC_LEN + USBD_WINUSB_PLATFORM_CAP_LEN)
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


/* Note:it should use the C99 standard when compiling the below codes */
/* USB standard device descriptor */
const usb_desc_dev Composite_dev_desc =
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
const Composite_desc_config Composite_config_desc =
{
    .config =
    {
        .header =
         {
             .bLength         = sizeof(usb_desc_config),
             .bDescriptorType = USB_DESCTYPE_CONFIG
         },
        .wTotalLength         = sizeof(Composite_desc_config),
        .bNumInterfaces       = 0x04U,
        .bConfigurationValue  = 0x01U,
        .iConfiguration       = 0x00U,
        .bmAttributes         = 0x80U,
        .bMaxPower            = 0xFAU
    },
#if DAP_ONLINE_ENABLE
		.hid_iad =
		{
				 .header =
         {
             .bLength         = sizeof(usb_desc_iad),
             .bDescriptorType = USB_DESCTYPE_IAD
         },
				 .bFirstInterface 		=0x00U,
				 .bInterfaceCount 		=0x01U,
				 .bFunctionClass 			=0x03U,
				 .bFunctionSubClass 	=0x00U,
				 .bFunctionProtocol 	=0x00U,
				 .iFunction 					=0x04U
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
        .iInterface           = 0x04U
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
#else
    /* Endpoint-free IF0 preserves existing PC tool and driver interface numbers. */
    .reserved_itf =
    {
        .header = { .bLength = sizeof(usb_desc_itf), .bDescriptorType = USB_DESCTYPE_ITF },
        .bInterfaceNumber = 0x00U,
        .bAlternateSetting = 0x00U,
        .bNumEndpoints = 0x00U,
        .bInterfaceClass = 0xFFU,
        .bInterfaceSubClass = 0x00U,
        .bInterfaceProtocol = 0x00U,
        .iInterface = 0x00U
    },
#endif
		.cb_iad =
		{
				 .header =
         {
             .bLength         = sizeof(usb_desc_iad),
             .bDescriptorType = USB_DESCTYPE_IAD
         },
				 .bFirstInterface 		=0x01U,
				 .bInterfaceCount 		=0x01U,
				 .bFunctionClass 			=0xFFU,
				 .bFunctionSubClass 	=0x00U,
				 .bFunctionProtocol 	=0x00U,
				 .iFunction 					=0x05U
		},
		.cb_itf =
    {
        .header = {
            .bLength         = sizeof(usb_desc_itf),
            .bDescriptorType = USB_DESCTYPE_ITF
        },
        .bInterfaceNumber    = 0x01U,
        .bAlternateSetting   = 0x00U,
        .bNumEndpoints       = 0x02U,
        .bInterfaceClass     = 0xFFU,
        .bInterfaceSubClass  = 0x00U,
        .bInterfaceProtocol  = 0x00U,
        .iInterface          = 0x05U
    },
    .cb_epin =
    {
        .header = {
            .bLength         = sizeof(usb_desc_ep),
            .bDescriptorType = USB_DESCTYPE_EP
        },
        .bEndpointAddress    = CUSTOMBULK_IN_EP,
        .bmAttributes        = USB_EP_ATTR_BULK,
        .wMaxPacketSize      = CUSTOMBULK_IN_PACKET,
        .bInterval           = 0x00U
    },

    .cb_epout =
    {
        .header = {
            .bLength         = sizeof(usb_desc_ep),
            .bDescriptorType = USB_DESCTYPE_EP
        },
        .bEndpointAddress    = CUSTOMBULK_OUT_EP,
        .bmAttributes        = USB_EP_ATTR_BULK,
        .wMaxPacketSize      = CUSTOMBULK_OUT_PACKET,
        .bInterval           = 0x00U
    },
		.cdc_iad =
		{
				 .header =
         {
             .bLength         = sizeof(usb_desc_iad),
             .bDescriptorType = USB_DESCTYPE_IAD
         },
				 .bFirstInterface 		=0x02,
				 .bInterfaceCount 		=0x02,
				 .bFunctionClass 			=0x02,
				 .bFunctionSubClass 	=0x02,
				 .bFunctionProtocol 	=0x01,
				 .iFunction 					=0x00
		},
		.cmd_itf =
    {
        .header =
         {
             .bLength         = sizeof(usb_desc_itf),
             .bDescriptorType = USB_DESCTYPE_ITF
         },
        .bInterfaceNumber     = 0x02U,
        .bAlternateSetting    = 0x00U,
        .bNumEndpoints        = 0x01U,
        .bInterfaceClass      = USB_CLASS_CDC,
        .bInterfaceSubClass   = USB_CDC_SUBCLASS_ACM,
        .bInterfaceProtocol   = USB_CDC_PROTOCOL_AT,
        .iInterface           = 0x00U
    },
    .cdc_header =
    {
        .header =
         {
            .bLength         = sizeof(usb_desc_header_func),
            .bDescriptorType = USB_DESCTYPE_CS_INTERFACE
         },
        .bDescriptorSubtype  = 0x00U,
        .bcdCDC              = 0x0110U
    },
    .cdc_call_managment =
    {
        .header =
         {
            .bLength         = sizeof(usb_desc_call_managment_func),
            .bDescriptorType = USB_DESCTYPE_CS_INTERFACE
         },
        .bDescriptorSubtype  = 0x01U,
        .bmCapabilities      = 0x00U,
        .bDataInterface      = 0x01U
    },
    .cdc_acm =
    {
        .header =
         {
            .bLength         = sizeof(usb_desc_acm_func),
            .bDescriptorType = USB_DESCTYPE_CS_INTERFACE
         },
        .bDescriptorSubtype  = 0x02U,
        .bmCapabilities      = 0x02U,
    },
    .cdc_union =
    {
        .header =
         {
            .bLength         = sizeof(usb_desc_union_func),
            .bDescriptorType = USB_DESCTYPE_CS_INTERFACE
         },
        .bDescriptorSubtype  = 0x06U,
        .bMasterInterface    = 0x00U,
        .bSlaveInterface0    = 0x01U,
    },

    .cdc_cmd_endpoint =
    {
        .header =
         {
            .bLength         = sizeof(usb_desc_ep),
            .bDescriptorType = USB_DESCTYPE_EP,
         },
        .bEndpointAddress    = CDC_CMD_EP,
        .bmAttributes        = USB_EP_ATTR_INT,
        .wMaxPacketSize      = CDC_ACM_CMD_PACKET_SIZE,
        .bInterval           = 0x0AU
    },
    .cdc_data_interface =
    {
        .header =
         {
            .bLength         = sizeof(usb_desc_itf),
            .bDescriptorType = USB_DESCTYPE_ITF,
         },
        .bInterfaceNumber    = 0x03U,
        .bAlternateSetting   = 0x00U,
        .bNumEndpoints       = 0x02U,
        .bInterfaceClass     = USB_CLASS_DATA,
        .bInterfaceSubClass  = 0x00U,
        .bInterfaceProtocol  = USB_CDC_PROTOCOL_NONE,
        .iInterface          = 0x00U
    },
    .cdc_out_endpoint =
    {
        .header =
         {
             .bLength         = sizeof(usb_desc_ep),
             .bDescriptorType = USB_DESCTYPE_EP,
         },
        .bEndpointAddress     = CDC_OUT_EP,
        .bmAttributes         = USB_EP_ATTR_BULK,
        .wMaxPacketSize       = CDC_ACM_OUT_PACKET_SIZE,
        .bInterval            = 0x00U
    },
    .cdc_in_endpoint =
    {
        .header =
         {
             .bLength         = sizeof(usb_desc_ep),
             .bDescriptorType = USB_DESCTYPE_EP
         },
        .bEndpointAddress     = CDC_IN_EP,
        .bmAttributes         = USB_EP_ATTR_BULK,
        .wMaxPacketSize       = CDC_ACM_IN_PACKET_SIZE,
        .bInterval            = 0x00U
    }
};

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
#if DAP_ONLINE_ENABLE
         .bLength         = USB_STRING_LEN(14U),
#else
         .bLength         = USB_STRING_LEN(15U),
#endif
         .bDescriptorType = USB_DESCTYPE_STR,
     },
#if DAP_ONLINE_ENABLE
    .unicode_string = {'C', 'M', 'S', 'I', 'S', '-', 'D', 'A', 'P', ' ','v','2','.','1'}
#else
    .unicode_string = {'D','A','P','L','I','N','K',' ','O','f','f','l','i','n','e'}
#endif
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

const uint8_t os_string[] = {
	0x12, 0x03, 'M', 0, 'S', 0, 'F', 0, 'T', 0, '1', 0, '0', 0, '0', 0, USBD_WINUSB_VENDOR_CODE, 0
};

/* USB string descriptor set */
static uint8_t* usbd_hid_strings[] =
{
    [STR_IDX_LANGID]  = (uint8_t *)&usbd_language_id_desc,
    [STR_IDX_MFC]     = (uint8_t *)&manufacturer_string,
    [STR_IDX_PRODUCT] = (uint8_t *)&product_string,
    [STR_IDX_SERIAL]  = (uint8_t *)&serial_string,
		[STR_IDX_ITF] 		= (uint8_t *)&itf_string,
		[STR_IDX_CONFIG]  = (uint8_t *)&config_string,
		[9]								= (uint8_t *)&os_string
};

/* Shared WinUSB descriptors belong to the composite device, not the optional HID. */
#ifdef USBD_BOS_ENABLE
/* BOS Descriptor: only headers (5 + 28 = 33 bytes) */
const uint8_t binaryobjectstore_desc[] = {
		5,                                        // bLength
    USB_DESCTYPE_BOS,                         // bDescriptorType (0x0F)
    WBVAL(33),                                // wTotalLength (5 BOS + 28 Platform Cap)
    USBD_NUM_DEV_CAPABILITIES,                // bNumDeviceCaps

    /*** MS OS 2.0 Platform Capability Descriptor (header only) ***/
    28,                                       // bLength (0x1C)
    0x10U,                         // bDescriptorType (0x10)
    USB_DEVICE_CAPABILITY_PLATFORM,           // bDevCapabilityType (0x05)
    0x00,                                     // bReserved
    0xDF, 0x60, 0xDD, 0xD8,                   // PlatformCapabilityUUID
    0x89, 0x45, 0xC7, 0x4C,
    0x9C, 0xD2, 0x65, 0x9D,
    0x9E, 0x64, 0x8A, 0x9F,
    0x00, 0x00, 0x03, 0x06,                   // dwWindowsVersion: 0x06030000
    WBVAL(USBD_WINUSB_DESC_SET_LEN),          // wMSOSDescriptorSetTotalLength (170)
    USBD_WINUSB_VENDOR_CODE,                  // bMS_VendorCode (0x20)
    0x00,                                     // bAltEnumCode
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
    1,                                        /* bFirstInterface - CustomBulk IF1 */
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

usb_desc Composite_desc =
{
    .dev_desc    = (uint8_t *)&Composite_dev_desc,
    .config_desc = (uint8_t *)&Composite_config_desc,
#ifdef USBD_BOS_ENABLE
		.bos_desc 	 = (uint8_t *)&binaryobjectstore_desc,
#endif
    .strings     = usbd_hid_strings
};

/* local function prototypes ('static') */
static uint8_t Composite_init         (usb_dev *udev, uint8_t config_index);
static uint8_t Composite_deinit       (usb_dev *udev, uint8_t config_index);
static uint8_t Composite_req_handler  (usb_dev *udev, usb_req *req);
static void Composite_data_in         (usb_dev *udev, uint8_t ep_num);
static void Composite_data_out        (usb_dev *udev, uint8_t ep_num);

usb_class Composite_class = {
    .req_cmd       = 0xFFU,

    .init          		= Composite_init,
    .deinit        		= Composite_deinit,
    .req_process   		= Composite_req_handler,
    .data_in       		= Composite_data_in,
    .data_out      		= Composite_data_out
};



static uint8_t Composite_init (usb_dev *udev, uint8_t config_index)
{
#if DAP_ONLINE_ENABLE
		custom_hid_class.init(udev, config_index);
#endif
		custombulk_class.init(udev, config_index);
	  cdc_class.init(udev, config_index);
    return USBD_OK;
}

/*!
    \brief      de-initialize the HID device
    \param[in]  udev: pointer to USB device instance
    \param[in]  config_index: configuration index
    \param[out] none
    \retval     USB device operation status
*/
static uint8_t Composite_deinit (usb_dev *udev, uint8_t config_index)
{
#if DAP_ONLINE_ENABLE
		custom_hid_class.deinit(udev, config_index);
#endif
		custombulk_class.deinit(udev, config_index);
	  cdc_class.deinit(udev, config_index);
    return USBD_OK;
}

/*!
    \brief      handle the HID class-specific requests
    \param[in]  udev: pointer to USB device instance
    \param[in]  req: device class-specific request
    \param[out] none
    \retval     USB device operation status
*/
static uint8_t Composite_req_handler (usb_dev *udev, usb_req *req)
{
		uint8_t status = USBD_OK;

#ifdef USBD_BOS_ENABLE
		/* Handle WinUSB vendor request: return MS OS 2.0 Descriptor Set */
		if (req->bRequest == USBD_WINUSB_VENDOR_CODE) {
			if (req->wIndex == 0x07) { /* MS_OS_2.0_GET_DESCRIPTOR_SET */
				uint16_t len = USBD_WINUSB_DESC_SET_LEN;
				if (len > req->wLength) len = req->wLength;
				udev->transc_in[0].xfer_buf = (uint8_t *)winusbdescriptorsetdescriptor;
				udev->transc_in[0].xfer_len = len;
				return USBD_OK;
			}
		}
#endif

		switch(req->wIndex & 0xFF)
		{
#if DAP_ONLINE_ENABLE
			case CUSTOM_HID_INTERFACE : status = custom_hid_class.req_process(udev, req); break;
#endif
			case CUSTOMBULK_INTERFACE : status = custombulk_class.req_process(udev, req); break;
			case CDC_COM_INTERFACE : status = cdc_class.req_process(udev, req); break;
		}
		return status;
}



static void Composite_data_in (usb_dev *udev, uint8_t ep_num)
{
		switch(ep_num)
		{
#if DAP_ONLINE_ENABLE
			case (CUSTOMHID_IN_EP & 0x7FU): custom_hid_class.data_in(udev, ep_num); break;
#endif
			case (CUSTOMBULK_IN_EP & 0x7FU): custombulk_class.data_in(udev, ep_num); break;
			case (CDC_IN_EP & 0x7FU): cdc_class.data_in(udev, ep_num); break;
		}
}

/*!
    \brief      handle custom HID data out transaction
    \param[in]  udev: pointer to USB device instance
    \param[in]  ep_num: endpoint number
    \param[out] none
    \retval     none
*/
static void Composite_data_out (usb_dev *udev, uint8_t ep_num)
{
		switch(ep_num)
		{
#if DAP_ONLINE_ENABLE
			case (CUSTOMHID_OUT_EP & 0x7FU): custom_hid_class.data_out(udev, ep_num); break;
#endif
			case (CUSTOMBULK_OUT_EP & 0x7FU): custombulk_class.data_out(udev, ep_num); break;
			case (CDC_OUT_EP & 0x7FU): cdc_class.data_out(udev, ep_num); break;
		}
}


