#ifndef __USBD_COMPOSITE_H_
#define __USBD_COMPOSITE_H_

#include "firmware_features.h"
#include "usb_ch9_std.h"
#include "custom_hid_core.h"
#include "usbd_custombulk.h"
#include "cdc_acm_core.h"


typedef struct
{
    usb_desc_config         				 config;
#if DAP_ONLINE_ENABLE
		usb_desc_iad										 hid_iad;
		usb_desc_itf            				 hid_itf;
    usb_desc_hid            				 hid_vendor;
    usb_desc_ep             				 hid_epout;
    usb_desc_ep             				 hid_epin;
#else
    usb_desc_itf reserved_itf; /* Preserve WinUSB IF1 and CDC IF2/3. */
#endif
		usb_desc_iad										 cb_iad;
		usb_desc_itf            				 cb_itf;
    usb_desc_ep             				 cb_epin;
    usb_desc_ep             				 cb_epout;
		usb_desc_iad										 cdc_iad;
		usb_desc_itf                     cmd_itf;
    usb_desc_header_func             cdc_header;
    usb_desc_call_managment_func     cdc_call_managment;
    usb_desc_acm_func                cdc_acm;
    usb_desc_union_func              cdc_union;
    usb_desc_ep                      cdc_cmd_endpoint;
    usb_desc_itf                     cdc_data_interface;
    usb_desc_ep                      cdc_out_endpoint;
    usb_desc_ep                      cdc_in_endpoint;
} Composite_desc_config;

extern usb_desc Composite_desc;
extern usb_class Composite_class;


#endif



