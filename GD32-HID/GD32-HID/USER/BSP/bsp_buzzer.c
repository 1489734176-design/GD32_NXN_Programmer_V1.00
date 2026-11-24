/* 无源蜂鸣器软件方波驱动：2.7kHz 左右，占空比 50%，阻塞发声。
 * 硬件为 NPN 三极管驱动，高电平导通；两路分别接 PB1(BUZZER1)、PB0(BUZZER2)。 */
#include "bsp_buzzer.h"
#include "main.h"

#define BUZZER_FREQ_HZ   2700U
#define BUZZER_ON_MS     120U
#define BUZZER_GAP_MS    120U

/* 初始化两路蜂鸣器引脚为推挽输出，先输出低电平保持静音。
 * 返回 - 无 */
void buzzer_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOB);
    GPIO_BC(BUZZER1_GPIO_Port) = BUZZER1_Pin | BUZZER2_Pin;
    gpio_init(BUZZER1_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ,
              BUZZER1_Pin | BUZZER2_Pin);
}

/* 以约 2.7kHz 方波驱动指定蜂鸣器一段时间。
 * pin  - 蜂鸣器控制引脚
 * ms   - 发声时长（毫秒）
 * 返回 - 无 */
static void buzzer_tone(uint32_t pin, uint16_t ms)
{
    /* 与 delaymS 相同的空转计数方式：每次循环约 4 个内核周期。 */
    uint32_t half = SystemCoreClock / 4U / (BUZZER_FREQ_HZ * 2U);
    uint32_t periods = (uint32_t)ms * BUZZER_FREQ_HZ / 1000U;

    if(half == 0U) half = 1U;
    while(periods--)
    {
        GPIO_BOP(BUZZER1_GPIO_Port) = pin;
        {
            uint32_t i = half;
            while(i--) __NOP();
        }
        GPIO_BC(BUZZER1_GPIO_Port) = pin;
        {
            uint32_t i = half;
            while(i--) __NOP();
        }
    }
}

/* 按次数鸣叫：每次 120ms，间隔 120ms，结束后保持静音。
 * ch    - 0=BUZZER1，1=BUZZER2
 * times - 次数
 * 返回 - 无 */
void buzzer_beep(uint8_t ch, uint8_t times)
{
    uint32_t pin = (ch == 0U) ? BUZZER1_Pin : BUZZER2_Pin;

    while(times--)
    {
        buzzer_tone(pin, BUZZER_ON_MS);
        GPIO_BC(BUZZER1_GPIO_Port) = pin;
        if(times) delaymS(BUZZER_GAP_MS);
    }
}
