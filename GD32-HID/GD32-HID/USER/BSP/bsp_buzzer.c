/* Active buzzer driver: high level turns the NPN on, no PWM needed. */
#include "bsp_buzzer.h"
#include "main.h"

#define BUZZER_ON_MS     120U
#define BUZZER_GAP_MS    120U

void buzzer_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOB);
    GPIO_BC(BUZZER1_GPIO_Port) = BUZZER1_Pin | BUZZER2_Pin;
    gpio_init(BUZZER1_GPIO_Port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ,
              BUZZER1_Pin | BUZZER2_Pin);
}

void buzzer_beep(uint8_t ch, uint8_t times)
{
#if BUZZER_ENABLE
    uint32_t pin = (ch == 0U) ? BUZZER1_Pin : BUZZER2_Pin;
    while(times--)
    {
        GPIO_BOP(BUZZER1_GPIO_Port) = pin;
        delaymS(BUZZER_ON_MS);
        GPIO_BC(BUZZER1_GPIO_Port) = pin;
        if(times) delaymS(BUZZER_GAP_MS);
    }
#else
    (void)ch; (void)times;
#endif
}
