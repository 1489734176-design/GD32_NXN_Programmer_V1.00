#ifndef _BSP_KEY_H__
#define _BSP_KEY_H__

#include "main.h"
#include "gd32f10x_gpio.h"

#define key_long_th     5*1
#define read_key_left   gpio_input_bit_get(KEY_L_GPIO_Port, KEY_L_Pin)
#define read_key_mid    gpio_input_bit_get(KEY_M_GPIO_Port, KEY_M_Pin)
#define read_key_right  gpio_input_bit_get(KEY_R_GPIO_Port, KEY_R_Pin)

typedef enum {key_null, key_left, key_mid, key_right, key_l_left, key_l_mid, key_l_right, key_d_mid} key_num;

/* 双击检测窗口 (以 key_scan 轮询次数计, 每次约100ms) */
#define key_dbl_th      3

typedef struct
{
	key_num key;
	uint8_t mesg;
	
}key_TypeDef;

extern key_TypeDef key_val;



void key_init(void);
key_num key_scan(void);

#endif

