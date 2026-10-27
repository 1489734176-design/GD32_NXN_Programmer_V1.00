/*!
    \file    main.c
    \brief   离线烧录器主入口（原USB键盘例程）

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

#include "main.h"

#define RUN_TASK_PRIO		1	
#define RUN_STK_SIZE 		1024  
TaskHandle_t RunTask_Handler;

int main(void)
{
		SCB->VTOR = FLASH_BASE | 0xC000;
    rcu_config();
		nvic_config();
	
		xTaskCreate((TaskFunction_t )run_task,            	//任务函数
                (const char*    )"run_task",          	//任务名称
                (uint16_t       )RUN_STK_SIZE,        	//任务堆栈大小
                (void*          )NULL,                  //传递给任务函数的参数
                (UBaseType_t    )RUN_TASK_PRIO,       	//任务优先级
                (TaskHandle_t*  )&RunTask_Handler);   	//任务句柄              
    vTaskStartScheduler();          										//开启任务调度

//    while(usb_hid.cur_status != USBD_CONFIGURED)
//		{
//			
//    }
    while (1) 
	{

    }
}
