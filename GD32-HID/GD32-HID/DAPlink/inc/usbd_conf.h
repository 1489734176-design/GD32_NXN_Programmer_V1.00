/*!
    \file    usbd_conf.h
    \brief   USB设备驱动基本配置

    \version 2021-04-30, V3.0.0, demo for GD32F10x
*/

/*
    Copyright (c) 2021, GigaDevice Semiconductor Inc.

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

#ifndef __USBD_CONF_H
#define __USBD_CONF_H

#include "gd32f10x.h"


#define USBD_CFG_MAX_NUM                   1U
#define USBD_ITF_MAX_NUM                   3U

#define CUSTOM_HID_INTERFACE               0U
#define CDC_COM_INTERFACE               	 2U

/* 使能后允许设备在USB挂起事件之后进入DEEP_SLEEP模式，
   并在收到USB唤醒事件后唤醒。 */
//#define USB_DEVICE_LOW_PWR_MODE_SUPPORT

/* USB特性：自供电 */
/* #define USBD_SELF_POWERED */

/* HID设备使用的端点 */
#define CUSTOMHID_IN_EP                    EP_IN(1U)
#define CUSTOMHID_OUT_EP                   EP_OUT(1U)

#define CUSTOMHID_IN_PACKET                64U
#define CUSTOMHID_OUT_PACKET               64U

/* CDC端点定义 */
#define CDC_IN_EP                    			 EP_IN(3U)
#define CDC_CMD_EP                    		 EP_IN(4U)
#define CDC_OUT_EP                   			 EP_OUT(3U)

#define CDC_ACM_IN_PACKET_SIZE             16U
#define CDC_ACM_CMD_PACKET_SIZE            8U
#define CDC_ACM_OUT_PACKET_SIZE            16U

/* 端点0收发缓冲区地址偏移 */

#define EP0_RX_ADDR                        (0X48)
#define EP0_TX_ADDR                        (0X88)

/* HID收发缓冲区地址偏移 */
#define HID_TX_ADDR                        (0XC8)
#define HID_RX_ADDR                        (0X0108)

/* 自定义批量端点收发缓冲区地址偏移（替代原来的MSC） */
#define CUSTOMBULK_TX_ADDR                 (0X0148)
#define CUSTOMBULK_RX_ADDR                 (0X0188)

/* CDC收发缓冲区地址偏移 */
#define CDC_TX_ADDR                        (0X01C8)
#define CDC_RX_ADDR                        (0X01D8)
#define CDC_CMD_ADDR                       (0X01E8)

/* 设备使用的端点总数 */
#define EP_COUNT                           (9U)

/* 分配缓冲区基地址偏移，用于缓冲描述符表和包存储区 */
#define BTABLE_OFFSET                      (0x0000U)


#endif /* __USBD_CONF_H */
