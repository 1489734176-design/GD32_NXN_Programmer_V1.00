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
#include "bsp_buzzer.h"
#include "bsp_gd25qxx.h"
#include "bsp_oled.h"
#include "bsp_adc.h"
#include "bsp_uart.h"
#include "bsp_tim.h"
#include "swd_host.h"
#include "SWD_flash.h"
#include "SWD_opt.h"
#include "error.h"
#include "bsp_pwr.h"


/* ================= GD32F103RCT6 双通道板引脚 ================= */

/* 按键：高电平有效，外部 10k 下拉 */
#define KEY_M_Pin GPIO_PIN_1
#define KEY_M_GPIO_Port GPIOC
#define KEY_L_Pin GPIO_PIN_1
#define KEY_L_GPIO_Port GPIOA
#define KEY_R_Pin GPIO_PIN_2
#define KEY_R_GPIO_Port GPIOA

/* 电池检测与外部 SPI Flash */
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

/* 串口与 USB、本机 SWD（PA13/PA14） */
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

/* 指示灯：低电平点亮；编号与旧板 1:1 对应（LED1=HID, LED2=MSC, LED3=CDC） */
#define HID_LED_Pin GPIO_PIN_0
#define HID_LED_GPIO_Port GPIOA
#define MSC_LED_Pin GPIO_PIN_4
#define MSC_LED_GPIO_Port GPIOC
#define CDC_LED_Pin GPIO_PIN_5
#define CDC_LED_GPIO_Port GPIOC

/* 电源与充电状态：CHRG/STDBY 开漏低有效 */
#define BAT_CHARG_Pin GPIO_PIN_2
#define BAT_CHARG_GPIO_Port GPIOC
#define BAT_FULL_Pin GPIO_PIN_3
#define BAT_FULL_GPIO_Port GPIOC
#define USB_EN_Pin GPIO_PIN_14
#define USB_EN_GPIO_Port GPIOC
#define PWR_EN_Pin GPIO_PIN_0
#define PWR_EN_GPIO_Port GPIOC

/* 烧录通道 A：VCC_EN 经 PMOS 控制，低电平给目标上电 */
#define TARGET_PWR_Pin        GPIO_PIN_11
#define TARGET_PWR_GPIO_Port  GPIOC
#define TARGET_PWR_ON         gpio_bit_reset(TARGET_PWR_GPIO_Port, TARGET_PWR_Pin)
#define TARGET_PWR_OFF        gpio_bit_set(TARGET_PWR_GPIO_Port, TARGET_PWR_Pin)

#define TGT_A_VCC_EN_Pin GPIO_PIN_11
#define TGT_A_VCC_EN_GPIO_Port GPIOC
#define TGT_A_SWCLK_Pin GPIO_PIN_15
#define TGT_A_SWCLK_GPIO_Port GPIOA
#define TGT_A_SWDIO_Pin GPIO_PIN_10
#define TGT_A_SWDIO_GPIO_Port GPIOC
#define TGT_A_RST_Pin GPIO_PIN_12
#define TGT_A_RST_GPIO_Port GPIOC
#define TGT_A_START_Pin GPIO_PIN_9
#define TGT_A_START_GPIO_Port GPIOC
#define TGT_A_BUSY_Pin GPIO_PIN_10
#define TGT_A_BUSY_GPIO_Port GPIOA
#define TGT_A_NG_Pin GPIO_PIN_9
#define TGT_A_NG_GPIO_Port GPIOA
#define TGT_A_OK_Pin GPIO_PIN_8
#define TGT_A_OK_GPIO_Port GPIOA

/* 烧录通道 B：本期只定义，初始化时安全停车，不参与烧录流程 */
#define TGT_B_VCC_EN_Pin GPIO_PIN_7
#define TGT_B_VCC_EN_GPIO_Port GPIOC
#define TGT_B_SWCLK_Pin GPIO_PIN_15
#define TGT_B_SWCLK_GPIO_Port GPIOB
#define TGT_B_SWDIO_Pin GPIO_PIN_6
#define TGT_B_SWDIO_GPIO_Port GPIOC
#define TGT_B_RST_Pin GPIO_PIN_8
#define TGT_B_RST_GPIO_Port GPIOC
#define TGT_B_START_Pin GPIO_PIN_2
#define TGT_B_START_GPIO_Port GPIOB
#define TGT_B_BUSY_Pin GPIO_PIN_14
#define TGT_B_BUSY_GPIO_Port GPIOB
#define TGT_B_NG_Pin GPIO_PIN_13
#define TGT_B_NG_GPIO_Port GPIOB
#define TGT_B_OK_Pin GPIO_PIN_12
#define TGT_B_OK_GPIO_Port GPIOB

/* 蜂鸣器：高电平经 NPN 驱动；BUZZER1 对应通道 A */
#define BUZZER1_Pin GPIO_PIN_1
#define BUZZER1_GPIO_Port GPIOB
#define BUZZER2_Pin GPIO_PIN_0
#define BUZZER2_GPIO_Port GPIOB

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
