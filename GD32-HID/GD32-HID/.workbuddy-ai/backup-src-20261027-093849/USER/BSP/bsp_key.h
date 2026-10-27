/* 中文注释补充（2026-10-26）：按键时序：20ms扫描，左右长按500ms，中键长按1500ms，连发100ms、双击300ms；长按与连发事件分开。 */
#ifndef _BSP_KEY_H__
#define _BSP_KEY_H__

#include "main.h"
#include "gd32f10x_gpio.h"

#define KEY_SCAN_MS     20
#define key_long_th     (500 / KEY_SCAN_MS)
#define key_mid_long_th (1500 / KEY_SCAN_MS)
#define key_repeat_th   (100 / KEY_SCAN_MS)
#define read_key_left   gpio_input_bit_get(KEY_L_GPIO_Port, KEY_L_Pin)
#define read_key_mid    gpio_input_bit_get(KEY_M_GPIO_Port, KEY_M_Pin)
#define read_key_right  gpio_input_bit_get(KEY_R_GPIO_Port, KEY_R_Pin)

/* Long events fire once; repeat events are separate for menu-specific handling. */
typedef enum {key_null, key_left, key_mid, key_right, key_l_left, key_l_mid, key_l_right, key_d_mid, key_rep_left, key_rep_right} key_num;

#define key_dbl_th      (300 / KEY_SCAN_MS)

typedef struct
{
	key_num key;
	uint8_t mesg;
	
}key_TypeDef;

extern key_TypeDef key_val;



/* 初始化三个按键为下拉输入。 */
void key_init(void);
/* 按键消抖及短按/长按/连发/中键双击识别；长按只发一次，长按松开不再补发短按。 */
key_num key_scan(void);

#endif

