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
void HardFault_Handler(void)
{
    /* 发生HardFault异常时进入死循环 */
    while(1){
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
    /* 发生MemManage异常时进入死循环 */
    while(1){
    }
}

/*!
    \brief      BusFault异常处理函数
    \param[in]  无
    \param[out] 无
    \retval     无
*/
void BusFault_Handler(void)
{
    /* 发生BusFault异常时进入死循环 */
    while(1){
    }
}

/*!
    \brief      UsageFault异常处理函数
    \param[in]  无
    \param[out] 无
    \retval     无
*/
void UsageFault_Handler(void)
{
    /* 发生UsageFault异常时进入死循环 */
    while(1){
    }
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
