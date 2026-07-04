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
    static key_num flag;
    static uint8_t count;
    key_num key = key_null;

    if(read_key_left) key = key_left;
    else if(read_key_mid) key = key_mid;
    else if(read_key_right) key = key_right;
    else key = key_null;

    if(key != key_null)
    {
        if(flag == key_null)
            flag = key;
        else if(key == flag)
            count++;
    }
    else
    {
        if(count > key_long_th)
        {
            switch (flag)
            {
            case key_left:
                key = key_l_left;
                break;
            case key_mid:
                key = key_l_mid;
                break;
            case key_right:
                key = key_l_right;
                break;
            default:
                break;
            }
					flag = key_null, count = 0;
					return key;
        }
				if(flag != key_null)
					key = flag;
        flag = key_null, count = 0;
        return key;
    }
    return key_null; 

}


