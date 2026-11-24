/* 板载蜂鸣器驱动：BUZZER1 对应通道 A，BUZZER2 对应通道 B。
 * 蜂鸣器为无源器件（约 2.7kHz），用软件方波驱动，不占用定时器。 */
#ifndef __BSP_BUZZER_H__
#define __BSP_BUZZER_H__

#include "gd32f10x.h"

/* 初始化两路蜂鸣器控制脚为推挽输出并保持静音。
 * 返回 - 无 */
void buzzer_init(void);
/* 鸣叫提示：阻塞执行，建议只在烧录结果等低频事件中调用。
 * ch    - 0=BUZZER1（通道A），1=BUZZER2（通道B）
 * times - 鸣叫次数；0 表示不响
 * 返回  - 无 */
void buzzer_beep(uint8_t ch, uint8_t times);

#endif
