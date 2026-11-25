/*!
    \file    gd32f10x_it.c
    \brief   主要中断服务程序

    \version 2021-04-30, V3.0.0, demo for GD32F10x
*/

/*
    Copyright (c) 2021, GigaDevice Semiconductor Inc.

    Redistribution and use in source and binary forms, with or without modification, 
are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this 
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice, 
       this list of conditions and the following disclaimer in the documentation 
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors 
       may be used to endorse or promote products derived from this software without 
       specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT 
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR 
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
OF SUCH DAMAGE.
*/

#include "gd32f10x_it.h"
#include "usbd_lld_int.h"
#include "usbd_lld_core.h"
#include "FreeRTOS.h"
#include "task.h"
#include "bsp_lcd.h"
#include <stdio.h>

/*!
    \brief      NMI异常处理函数
    \param[in]  无
    \param[out] 无
    \retval     无
*/
void NMI_Handler(void)
{
}

/*!
    \brief      HardFault异常处理函数
    \param[in]  无
    \param[out] 无
    \retval     无
*/
/* 故障现场：把异常压栈的 R0~R3/PC/LR 和 SCB 故障寄存器显示到 LCD，
 * 然后保持 LED3(PC5) 快闪。frame 由下面的汇编入口按 EXC_RETURN 选择
 * MSP/PSP 后传入；所有值先拷贝到局部变量，避免后续绘图覆盖故障栈。 */
void fault_report(uint32_t *frame)
{
    uint32_t r0, r1, r2, r3, lr, pc, cfsr, hfsr, mmfar, bfar;
    char line[32];
    volatile uint32_t i;

    r0 = frame[0]; r1 = frame[1]; r2 = frame[2]; r3 = frame[3];
    lr = frame[5]; pc = frame[6];
    cfsr = SCB->CFSR; hfsr = SCB->HFSR;
    mmfar = SCB->MMFAR; bfar = SCB->BFAR;

    /* 屏幕已经初始化过；这里不能再调 LCD_Init()，它会 vTaskDelay。 */
    LCD_Clear(LCD_BLACK);
    LCD_ShowString(8, 8,   "*** FAULT ***", LCD_RED, LCD_BLACK, 2);
    sprintf(line, "PC %08lX", (unsigned long)pc);
    LCD_ShowString(8, 36, line, LCD_WHITE, LCD_BLACK, 2);
    sprintf(line, "LR %08lX", (unsigned long)lr);
    LCD_ShowString(8, 56, line, LCD_WHITE, LCD_BLACK, 2);
    sprintf(line, "CFSR %08lX", (unsigned long)cfsr);
    LCD_ShowString(8, 80, line, LCD_YELLOW, LCD_BLACK, 2);
    sprintf(line, "HFSR %08lX", (unsigned long)hfsr);
    LCD_ShowString(8, 100, line, LCD_YELLOW, LCD_BLACK, 2);
    sprintf(line, "R0 %08lX", (unsigned long)r0);
    LCD_ShowString(8, 124, line, LCD_WHITE, LCD_BLACK, 2);
    sprintf(line, "R1 %08lX", (unsigned long)r1);
    LCD_ShowString(8, 144, line, LCD_WHITE, LCD_BLACK, 2);
    sprintf(line, "R2 %08lX", (unsigned long)r2);
    LCD_ShowString(8, 164, line, LCD_WHITE, LCD_BLACK, 2);
    sprintf(line, "R3 %08lX", (unsigned long)r3);
    LCD_ShowString(8, 184, line, LCD_WHITE, LCD_BLACK, 2);
    sprintf(line, "MMF %08lX", (unsigned long)mmfar);
    LCD_ShowString(8, 208, line, LCD_GRAY, LCD_BLACK, 2);
    sprintf(line, "BFA %08lX", (unsigned long)bfar);
    LCD_ShowString(8, 228, line, LCD_GRAY, LCD_BLACK, 2);

    rcu_periph_clock_enable(RCU_GPIOC);
    gpio_init(GPIOC, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_5);
    while(1)
    {
        GPIO_BOP(GPIOC) = GPIO_PIN_5;
        for(i = 0; i < 200000U; i++) { }
        GPIO_BC(GPIOC) = GPIO_PIN_5;
        for(i = 0; i < 200000U; i++) { }
    }
}

/* 汇编入口：按 EXC_RETURN 的 bit2 选择异常压栈的 MSP/PSP，再跳到 C 处理函数。
 * 不能直接在 C 入口里读 LR，因为 BL 调用会覆盖它。 */
__asm void HardFault_Handler(void)
{
    IMPORT fault_report
    TST     LR, #4
    ITE     EQ
    MRSEQ   R0, MSP
    MRSNE   R0, PSP
    B       fault_report
}

/* MemManage/BusFault/UsageFault 默认未使能，异常都会升级到 HardFault；
 * 打开时至少保留 LED3 快闪指示，避免静默死循环。 */
static void fault_indicator(void)
{
    volatile uint32_t i;
    rcu_periph_clock_enable(RCU_GPIOC);
    gpio_init(GPIOC, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_5);
    while(1)
    {
        GPIO_BOP(GPIOC) = GPIO_PIN_5;
        for(i = 0; i < 200000U; i++) { }
        GPIO_BC(GPIOC) = GPIO_PIN_5;
        for(i = 0; i < 200000U; i++) { }
    }
}

/*!
    \brief      MemManage异常处理函数
    \param[in]  无
    \param[out] 无
    \retval     无
*/
void MemManage_Handler(void)
{
    fault_indicator();
}

/*!
    \brief      BusFault异常处理函数
    \param[in]  无
    \param[out] 无
    \retval     无
*/
void BusFault_Handler(void)
{
    fault_indicator();
}

/*!
    \brief      UsageFault异常处理函数
    \param[in]  无
    \param[out] 无
    \retval     无
*/
void UsageFault_Handler(void)
{
    fault_indicator();
}

/*!
    \brief      SVC异常处理函数
    \param[in]  无
    \param[out] 无
    \retval     无
*/
//void SVC_Handler(void)
//{
//}

extern void xPortSysTickHandler(void);

void SysTick_Handler(void)
{
    if(xTaskGetSchedulerState()!=taskSCHEDULER_NOT_STARTED)//系统已经运行
    {
        xPortSysTickHandler();	
    }	
}

/*!
    \brief      DebugMon异常处理函数
    \param[in]  无
    \param[out] 无
    \retval     无
*/
void DebugMon_Handler(void)
{
}

/*!
    \brief      PendSV异常处理函数
    \param[in]  无
    \param[out] 无
    \retval     无
*/
//void PendSV_Handler(void)
//{
//}

/*!
    \brief      USBD中断处理函数
    \param[in]  无
    \param[out] 无
    \retval     无
*/
void USBD_LP_CAN0_RX0_IRQHandler (void)
{
    usbd_isr();
}

/*!
    \brief      EXTI10_15外部中断处理函数
    \param[in]  无
    \param[out] 无
    \retval     无
*/
//void EXTI10_15_IRQHandler (void)
//{
//    if (RESET != exti_interrupt_flag_get(TAMPER_KEY_EXTI_LINE)) {
//        /* 检查是否已使能远程唤醒 */
//        if (usbd_core.dev->pm.remote_wakeup) {
//            /* 退出低功耗模式并重新配置时钟 */
//            usbd_remote_wakeup_active(usbd_core.dev);

//            usbd_core.dev->cur_status = usbd_core.dev->backup_status;

//            /* 关闭远程唤醒功能 */
//            usbd_core.dev->pm.remote_wakeup = 0U;
//        }

//        /* 清除EXTI线挂起标志 */
//        exti_interrupt_flag_clear(TAMPER_KEY_EXTI_LINE);
//    }
//}

#ifdef USBD_LOWPWR_MODE_ENABLE

/*!
    \brief      USBD唤醒中断处理函数
    \param[in]  无
    \param[out] 无
    \retval     无
*/
void USBD_WKUP_IRQHandler (void)
{
    exti_interrupt_flag_clear(EXTI_18);
}

#endif /* USBD_LOWPWR_MODE_ENABLE */
