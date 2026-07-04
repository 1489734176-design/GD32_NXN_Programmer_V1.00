#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif


#include "gd32f10x.h"

#include "FreeRTOS.h"
#include "event_groups.h"
#include "task.h"
#include "custom_hid_core.h"
#include "usbd_composite.h"
#include "usbd_hw.h"
#include "run.h"
#include "bsp_key.h"
#include "bsp_led.h"
#include "bsp_gd25qxx.h"
#include "bsp_oled.h"
#include "bsp_adc.h"
#include "bsp_uart.h"
#include "bsp_tim.h"
#include "swd_host.h"
#include "SWD_flash.h"
#include "error.h"
#include "bsp_pwr.h"


#define KEY_M_Pin GPIO_PIN_0
#define KEY_M_GPIO_Port GPIOA
#define KEY_L_Pin GPIO_PIN_1
#define KEY_L_GPIO_Port GPIOA
#define KEY_R_Pin GPIO_PIN_2
#define KEY_R_GPIO_Port GPIOA
#define BAT_ADC_Pin GPIO_PIN_3
#define BAT_ADC_GPIO_Port GPIOA
#define SPI_CS_Pin GPIO_PIN_4
#define SPI_CS_GPIO_Port GPIOA
#define SPI_SCK_Pin GPIO_PIN_5
#define SPI_SCK_GPIO_Port GPIOA
#define SPI_MISO_Pin GPIO_PIN_6
#define SPI_MISO_GPIO_Port GPIOA
#define SPI_MOSI_Pin GPIO_PIN_7
#define SPI_MOSI_GPIO_Port GPIOA
#define UART_TX_Pin GPIO_PIN_10
#define UART_TX_GPIO_Port GPIOB
#define UART_RX_Pin GPIO_PIN_11
#define UART_RX_GPIO_Port GPIOB
#define USB_DM_Pin GPIO_PIN_11
#define USB_DM_GPIO_Port GPIOA
#define USB_DP_Pin GPIO_PIN_12
#define USB_DP_GPIO_Port GPIOA
#define SWDIO_Pin GPIO_PIN_13
#define SWDIO_GPIO_Port GPIOA
#define SWCLK_Pin GPIO_PIN_14
#define SWCLK_GPIO_Port GPIOA
#define PWR_CRTL_Pin GPIO_PIN_4
#define PWR_CRTL_GPIO_Port GPIOB
#define CDC_LED_Pin GPIO_PIN_5
#define CDC_LED_GPIO_Port GPIOB
#define MSC_LED_Pin GPIO_PIN_6
#define MSC_LED_GPIO_Port GPIOB
#define HID_LED_Pin GPIO_PIN_7
#define HID_LED_GPIO_Port GPIOB
#define IIC_SCL_Pin GPIO_PIN_8
#define IIC_SCL_GPIO_Port GPIOB
#define IIC_SDA_Pin GPIO_PIN_9
#define IIC_SDA_GPIO_Port GPIOB
#define TGT_NTRST_Pin GPIO_PIN_10
#define TGT_NTRST_GPIO_Port GPIOA
#define TGT_TDI_Pin GPIO_PIN_9
#define TGT_TDI_GPIO_Port GPIOA
#define TGT_TDO_Pin GPIO_PIN_8
#define TGT_TDO_GPIO_Port GPIOA
#define TGT_TMS_Pin GPIO_PIN_15
#define TGT_TMS_GPIO_Port GPIOB
#define TGT_TMS_IN_Pin GPIO_PIN_14
#define TGT_TMS_IN_GPIO_Port GPIOB
#define TGT_TCK_Pin GPIO_PIN_13
#define TGT_TCK_GPIO_Port GPIOB
#define TGT_REREST_Pin GPIO_PIN_12
#define TGT_REREST_GPIO_Port GPIOB
#define USB_EN_Pin GPIO_PIN_3
#define USB_EN_GPIO_Port GPIOB
#define PWR_EN_Pin GPIO_PIN_4
#define PWR_EN_GPIO_Port GPIOB
#define BAT_CHARG_Pin GPIO_PIN_0
#define BAT_CHARG_GPIO_Port GPIOB
#define BAT_FULL_Pin GPIO_PIN_1
#define BAT_FULL_GPIO_Port GPIOB


#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
