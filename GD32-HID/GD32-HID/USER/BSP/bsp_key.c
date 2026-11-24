/* 中文注释补充（2026-10-26）：此前修改说明：保留原烧录/硬件流程，下方以中文说明相关界面与兼容逻辑。 */
#include "bsp_key.h"
#include "main.h"

key_TypeDef key_val = 
{
	.key = key_null,
	.mesg = 0,
};

/* 初始化三个按键为下拉输入。
 * 返回 - 无 */
void key_init(void)
{
		rcu_periph_clock_enable(RCU_GPIOA);
		rcu_periph_clock_enable(RCU_GPIOC);
		gpio_init(KEY_M_GPIO_Port, GPIO_MODE_IPD, GPIO_OSPEED_50MHZ, KEY_M_Pin);
		gpio_init(KEY_L_GPIO_Port, GPIO_MODE_IPD, GPIO_OSPEED_50MHZ, KEY_L_Pin);
		gpio_init(KEY_R_GPIO_Port, GPIO_MODE_IPD, GPIO_OSPEED_50MHZ, KEY_R_Pin);
}

/* 按键消抖及短按/长按/连发/中键双击识别；长按只发一次，长按松开不再补发短按。
 * 需要在固定周期（KEY_SCAN_MS）下调用，内部按调用次数计时。
 * 返回 - 本次识别到的按键事件；无事件时返回key_null */
key_num key_scan(void)
{
    static key_num active;
    static key_num candidate;
    static uint8_t stable_count;
    static uint8_t hold_count;
    static uint8_t repeat_count;
    static uint8_t repeated;
    static uint8_t mid_wait;
    key_num pressed, previous;

    if(read_key_left) pressed = key_left;
    else if(read_key_mid) pressed = key_mid;
    else if(read_key_right) pressed = key_right;
    else pressed = key_null;

    if(pressed != candidate)
    {
        candidate = pressed;
        stable_count = 1;
    }
    else if(stable_count < 2)
        stable_count++;

    if(stable_count < 2)
        return key_null;

    if(active != pressed)
    {
        previous = active;
        active = pressed;
        if(pressed != key_null)
        {
            hold_count = 0;
            repeat_count = 0;
            repeated = 0;
            if(pressed != key_mid)
                mid_wait = 0;
            return key_null;
        }

        if(previous == key_mid)
        {
            if(!repeated)
            {
                if(mid_wait)
                {
                    mid_wait = 0;
                    return key_d_mid;
                }
                mid_wait = key_dbl_th;
            }
        }
        else if((previous == key_left || previous == key_right) && !repeated)
            return previous;
        return key_null;
    }

    if(active != key_null)
    {
        if(hold_count < key_mid_long_th)
            hold_count++;
        if(active == key_mid)
        {
            if(hold_count >= key_mid_long_th && !repeated)
            {
                repeated = 1;
                mid_wait = 0;
                return key_l_mid;
            }
        }
        else if((active == key_left || active == key_right) && hold_count >= key_long_th)
        {
            if(!repeated)
            {
                repeated = 1;
                repeat_count = 0;
                return active == key_left ? key_l_left : key_l_right;
            }
            if(++repeat_count >= key_repeat_th)
            {
                repeat_count = 0;
                return active == key_left ? key_rep_left : key_rep_right;
            }
        }
    }
    else if(mid_wait && --mid_wait == 0)
        return key_mid;

    return key_null;
}


