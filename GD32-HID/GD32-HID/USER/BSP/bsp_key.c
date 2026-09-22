#include "bsp_key.h"
#include "main.h"

key_TypeDef key_val = 
{
	.key = key_null,
	.mesg = 0,
};

void key_init(void)
{
		rcu_periph_clock_enable(RCU_GPIOA);
		gpio_init(KEY_M_GPIO_Port, GPIO_MODE_IPD, GPIO_OSPEED_50MHZ, KEY_M_Pin);
		gpio_init(KEY_L_GPIO_Port, GPIO_MODE_IPD, GPIO_OSPEED_50MHZ, KEY_L_Pin);
		gpio_init(KEY_R_GPIO_Port, GPIO_MODE_IPD, GPIO_OSPEED_50MHZ, KEY_R_Pin);
}

key_num key_scan(void)
{
    static key_num flag;        /* 当前按住的键 */
    static uint8_t count;       /* 按住时长(轮询次数) */
    static uint8_t mid_wait;    /* >0: 中键单击后等待第二次的倒计时窗口 */

    key_num pressed, ev = key_null;

    if(read_key_left) pressed = key_left;
    else if(read_key_mid) pressed = key_mid;
    else if(read_key_right) pressed = key_right;
    else pressed = key_null;

    /* ---- 在松开时产生一次完整按键事件(短按/长按),沿用原逻辑 ---- */
    if(pressed != key_null)
    {
        if(flag == key_null)
            flag = pressed;                 /* 按下沿 */
        else if(pressed == flag && count < 255)
            count++;
    }
    else
    {
        if(flag != key_null)
        {
            if(count > key_long_th)
            {
                switch(flag)
                {
                case key_left:  ev = key_l_left;  break;
                case key_mid:   ev = key_l_mid;   break;
                case key_right: ev = key_l_right; break;
                default: break;
                }
            }
            else
            {
                ev = flag;                  /* 短按 */
            }
            flag = key_null, count = 0;
        }
    }

    /* ---- 中键长按: 立即返回, 取消双击等待 ---- */
    if(ev == key_l_mid)
    {
        mid_wait = 0;
        return key_l_mid;
    }

    /* ---- 中键短按: 叠加双击检测 ---- */
    if(ev == key_mid)
    {
        if(mid_wait > 0)                    /* 窗口内的第二次短按 => 双击 */
        {
            mid_wait = 0;
            return key_d_mid;
        }
        mid_wait = key_dbl_th;              /* 开启窗口, 暂缓这次单击 */
        return key_null;
    }

    /* ---- 非中键事件立即返回 ---- */
    if(ev != key_null)
        return ev;

    /* ---- 窗口在空闲中超时: 补发被暂缓的中键单击 ---- */
    if(mid_wait > 0 && pressed == key_null && flag == key_null)
    {
        mid_wait--;
        if(mid_wait == 0)
            return key_mid;
    }

    return key_null;
}


