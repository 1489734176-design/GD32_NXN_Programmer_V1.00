/*!
    \file    system_gd32f10x.c
    \brief   CMSIS Cortex-M3 器件外设访问层源文件（GD32F10x系列）
*/

/*
    Copyright (c) 2012 ARM LIMITED

    All rights reserved.

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

/* 本文件遵循CMSIS标准，并针对兆易创新芯片做了少量调整 */

#include "gd32f10x.h"

/* 系统频率定义 */
#define __IRC8M           (IRC8M_VALUE)            /* 内部8MHz RC振荡器频率 */
#define __HXTAL           (HXTAL_VALUE)            /* 高速外部晶振频率 */
#define __SYS_OSC_CLK     (__IRC8M)                /* 主振荡器频率 */

/* 取消下面某一行的注释即可选择系统时钟 */
/* 使用内部IRC8M */
//#define __SYSTEM_CLOCK_48M_PLL_IRC8M            (uint32_t)(48000000)
//#define __SYSTEM_CLOCK_72M_PLL_IRC8M            (uint32_t)(72000000)
//#define __SYSTEM_CLOCK_108M_PLL_IRC8M           (uint32_t)(108000000)

/* 使用外部晶振HXTAL（XD系列8M，CL系列25M） */
//#define __SYSTEM_CLOCK_HXTAL                    (uint32_t)(__HXTAL)
//#define __SYSTEM_CLOCK_24M_PLL_HXTAL            (uint32_t)(24000000)
//#define __SYSTEM_CLOCK_36M_PLL_HXTAL            (uint32_t)(36000000)
//#define __SYSTEM_CLOCK_48M_PLL_HXTAL            (uint32_t)(48000000)
//#define __SYSTEM_CLOCK_56M_PLL_HXTAL            (uint32_t)(56000000)
//#define __SYSTEM_CLOCK_72M_PLL_HXTAL            (uint32_t)(72000000)
#define __SYSTEM_CLOCK_96M_PLL_HXTAL            (uint32_t)(96000000)
//#define __SYSTEM_CLOCK_108M_PLL_HXTAL           (uint32_t)(108000000)

#define SEL_IRC8M       0x00U
#define SEL_HXTAL       0x01U
#define SEL_PLL         0x02U

/* 设置系统时钟频率并声明系统时钟配置函数 */
#ifdef __SYSTEM_CLOCK_48M_PLL_IRC8M
uint32_t SystemCoreClock = __SYSTEM_CLOCK_48M_PLL_IRC8M;
static void system_clock_48m_irc8m(void);
#elif defined (__SYSTEM_CLOCK_72M_PLL_IRC8M)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_72M_PLL_IRC8M;
static void system_clock_72m_irc8m(void);
#elif defined (__SYSTEM_CLOCK_108M_PLL_IRC8M)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_108M_PLL_IRC8M;
static void system_clock_108m_irc8m(void);

#elif defined (__SYSTEM_CLOCK_HXTAL)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_HXTAL;
static void system_clock_hxtal(void);
#elif defined (__SYSTEM_CLOCK_24M_PLL_HXTAL)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_24M_PLL_HXTAL;
static void system_clock_24m_hxtal(void);
#elif defined (__SYSTEM_CLOCK_36M_PLL_HXTAL)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_36M_PLL_HXTAL;
static void system_clock_36m_hxtal(void);
#elif defined (__SYSTEM_CLOCK_48M_PLL_HXTAL)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_48M_PLL_HXTAL;
static void system_clock_48m_hxtal(void);
#elif defined (__SYSTEM_CLOCK_56M_PLL_HXTAL)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_56M_PLL_HXTAL;
static void system_clock_56m_hxtal(void);
#elif defined (__SYSTEM_CLOCK_72M_PLL_HXTAL)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_72M_PLL_HXTAL;
static void system_clock_72m_hxtal(void);
#elif defined (__SYSTEM_CLOCK_96M_PLL_HXTAL)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_96M_PLL_HXTAL;
static void system_clock_96m_hxtal(void);
#elif defined (__SYSTEM_CLOCK_108M_PLL_HXTAL)
uint32_t SystemCoreClock = __SYSTEM_CLOCK_108M_PLL_HXTAL;
static void system_clock_108m_hxtal(void);
#endif /* __SYSTEM_CLOCK_48M_PLL_IRC8M */

/* 配置系统时钟 */
static void system_clock_config(void);

/*!
    \brief      configure the system clock
    \param[in]  无
    \param[out] 无
    \retval     无
*/
static void system_clock_config(void)
{
#ifdef __SYSTEM_CLOCK_HXTAL
    system_clock_hxtal();
#elif defined (__SYSTEM_CLOCK_24M_PLL_HXTAL)
    system_clock_24m_hxtal();
#elif defined (__SYSTEM_CLOCK_36M_PLL_HXTAL)
    system_clock_36m_hxtal();
#elif defined (__SYSTEM_CLOCK_48M_PLL_HXTAL)
    system_clock_48m_hxtal();
#elif defined (__SYSTEM_CLOCK_56M_PLL_HXTAL)
    system_clock_56m_hxtal();
#elif defined (__SYSTEM_CLOCK_72M_PLL_HXTAL)
    system_clock_72m_hxtal();
#elif defined (__SYSTEM_CLOCK_96M_PLL_HXTAL)
    system_clock_96m_hxtal();
#elif defined (__SYSTEM_CLOCK_108M_PLL_HXTAL)
    system_clock_108m_hxtal();

#elif defined (__SYSTEM_CLOCK_48M_PLL_IRC8M)
    system_clock_48m_irc8m();
#elif defined (__SYSTEM_CLOCK_72M_PLL_IRC8M)
    system_clock_72m_irc8m();
#elif defined (__SYSTEM_CLOCK_108M_PLL_IRC8M)
    system_clock_108m_irc8m();
#endif /* __SYSTEM_CLOCK_HXTAL */
}

/*!
    \brief      setup the microcontroller system, initialize the system
    \param[in]  无
    \param[out] 无
    \retval     无
*/
void SystemInit(void)
{
    /* 把RCC时钟配置复位到默认状态 */
    /* 使能IRC8M */
    RCU_CTL |= RCU_CTL_IRC8MEN;
    
    /* 复位SCS、AHBPSC、APB1PSC、APB2PSC、ADCPSC、CKOUT0SEL位 */
    RCU_CFG0 &= ~(RCU_CFG0_SCS | RCU_CFG0_AHBPSC | RCU_CFG0_APB1PSC | RCU_CFG0_APB2PSC |
                  RCU_CFG0_ADCPSC | RCU_CFG0_ADCPSC_2 | RCU_CFG0_CKOUT0SEL);

    /* 复位HXTALEN、CKMEN、PLLEN位 */
    RCU_CTL &= ~(RCU_CTL_HXTALEN | RCU_CTL_CKMEN | RCU_CTL_PLLEN);

    /* 复位HXTALBPS位 */
    RCU_CTL &= ~(RCU_CTL_HXTALBPS);

    /* 复位PLLSEL、PREDV0_LSB、PLLMF、USBFSPSC位 */
    
#ifdef GD32F10X_CL
    RCU_CFG0 &= ~(RCU_CFG0_PLLSEL | RCU_CFG0_PREDV0_LSB | RCU_CFG0_PLLMF |
                  RCU_CFG0_USBFSPSC | RCU_CFG0_PLLMF_4);

    RCU_CFG1 = 0x00000000U;
#else
    RCU_CFG0 &= ~(RCU_CFG0_PLLSEL | RCU_CFG0_PREDV0 | RCU_CFG0_PLLMF |
                  RCU_CFG0_USBDPSC | RCU_CFG0_PLLMF_4);
#endif /* GD32F10X_CL */

#if (defined(GD32F10X_MD) || defined(GD32F10X_HD) || defined(GD32F10X_XD))
    /* 复位HXTALEN、CKMEN、PLLEN位 */
    RCU_CTL &= ~(RCU_CTL_PLLEN | RCU_CTL_CKMEN | RCU_CTL_HXTALEN);
    /* 关闭所有中断 */
    RCU_INT = 0x009F0000U;
#elif defined(GD32F10X_CL)
    /* 复位HXTALEN、CKMEN、PLLEN、PLL1EN、PLL2EN位 */
    RCU_CTL &= ~(RCU_CTL_PLLEN | RCU_CTL_PLL1EN | RCU_CTL_PLL2EN | RCU_CTL_CKMEN | RCU_CTL_HXTALEN);
    /* 关闭所有中断 */
    RCU_INT = 0x00FF0000U;
#endif

    /* 配置系统时钟源、PLL倍频、AHB/APBx分频以及Flash等待周期 */
    system_clock_config();
}

/*!
    \brief      update the SystemCoreClock with current core clock retrieved from cpu registers
    \param[in]  无
    \param[out] 无
    \retval     无
*/
void SystemCoreClockUpdate(void)
{
    uint32_t scss;
    uint32_t pllsel, predv0sel, pllmf, ck_src;
#ifdef  GD32F10X_CL
    uint32_t predv0, predv1, pll1mf;
#endif /* GD32F10X_CL */

    scss = GET_BITS(RCU_CFG0, 2, 3);

    switch (scss)
    {
        /* 选择IRC8M作为CK_SYS */
        case SEL_IRC8M:
            SystemCoreClock = IRC8M_VALUE;
            break;
            
        /* 选择HXTAL作为CK_SYS */
        case SEL_HXTAL:
            SystemCoreClock = HXTAL_VALUE;
            break;
            
        /* 选择PLL作为CK_SYS */
        case SEL_PLL:
            /* PLL时钟源选择：HXTAL或IRC8M/2 */
            pllsel = (RCU_CFG0 & RCU_CFG0_PLLSEL);


            if(RCU_PLLSRC_IRC8M_DIV2 == pllsel){
                /* PLL时钟源为IRC8M/2 */
                ck_src = IRC8M_VALUE / 2U;
            }else{
                /* PLL时钟源为HXTAL */
                ck_src = HXTAL_VALUE;
            
#if (defined(GD32F10X_MD) || defined(GD32F10X_HD) || defined(GD32F10X_XD))
                predv0sel = (RCU_CFG0 & RCU_CFG0_PREDV0);

                /* PREDV0输入时钟2分频 */
                if(RCU_CFG0_PREDV0 == predv0sel){
                    ck_src = HXTAL_VALUE / 2U;
                }
#elif defined(GD32F10X_CL)
                predv0sel = (RCU_CFG1 & RCU_CFG1_PREDV0SEL);

                /* 时钟源使用PLL1 */
                if(RCU_PREDV0SRC_CKPLL1 == predv0sel){
                    predv1 = ((RCU_CFG1 & RCU_CFG1_PREDV1) >> 4) + 1U;
                    pll1mf = ((RCU_CFG1 & RCU_CFG1_PLL1MF) >> 8) + 2U;
                    if(17U == pll1mf){
                        pll1mf = 20U;
                    }
                    ck_src = (ck_src / predv1) * pll1mf;
                }
                predv0 = (RCU_CFG1 & RCU_CFG1_PREDV0) + 1U;
                ck_src /= predv0;
#endif /* 以下仅适用于GD32F10X_MD/HD/XD */
            }

            /* PLL倍频系数 */
            pllmf = GET_BITS(RCU_CFG0, 18, 21);

            if((RCU_CFG0 & RCU_CFG0_PLLMF_4)){
                pllmf |= 0x10U;
            }

            if(pllmf >= 15U){
                pllmf += 1U;
            }else{
                pllmf += 2U;
            }

            SystemCoreClock = ck_src * pllmf;

#ifdef GD32F10X_CL
            if(15U == pllmf){
                /* PLL源时钟6.5倍频 */
                SystemCoreClock = ck_src * 6U + ck_src / 2U;
            }
#endif /* GD32F10X_CL */

            break;

        /* 选择IRC8M作为CK_SYS */
        default:
            SystemCoreClock = IRC8M_VALUE;
            break;
    }
}

#ifdef __SYSTEM_CLOCK_HXTAL
/*!
    \brief      configure the system clock to HXTAL
    \param[in]  无
    \param[out] 无
    \retval     无
*/
static void system_clock_hxtal(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;
    
    /* 使能HXTAL */
    RCU_CTL |= RCU_CTL_HXTALEN;
    
    /* 等待HXTAL稳定，或超过HXTAL_STARTUP_TIMEOUT启动超时 */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_HXTALSTB);
    }while((0U == stab_flag) && (HXTAL_STARTUP_TIMEOUT != timeout));
    
    /* 启动失败 */
    if(0U == (RCU_CTL & RCU_CTL_HXTALSTB)){
        while(1){
        }
    }
    
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/1 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV1;
    /* APB1 = AHB/2 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV2;
    
    /* 选择HXTAL作为系统时钟 */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_HXTAL;
    
    /* 等待HXTAL被选为系统时钟 */
    while(0 == (RCU_CFG0 & RCU_SCSS_HXTAL)){
    }
}

#elif defined (__SYSTEM_CLOCK_24M_PLL_HXTAL)
/*!
    \brief      把系统时钟配置为24M：PLL以HXTAL为源（MD/HD/XD:8M；CL:25M）
    \param[in]  无
    \param[out] 无
    \retval     无
*/
static void system_clock_24m_hxtal(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;

    /* 使能HXTAL */
    RCU_CTL |= RCU_CTL_HXTALEN;

    /* 等待HXTAL稳定，或超过HXTAL_STARTUP_TIMEOUT启动超时 */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_HXTALSTB);
    }while((0U == stab_flag) && (HXTAL_STARTUP_TIMEOUT != timeout));

    /* 启动失败 */
    if(0U == (RCU_CTL & RCU_CTL_HXTALSTB)){
        while(1){
        }
    }

    /* HXTAL已稳定 */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/1 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV1;
    /* APB1 = AHB/2 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV2;

#if (defined(GD32F10X_MD) || defined(GD32F10X_HD) || defined(GD32F10X_XD))
    /* 选择HXTAL/2作为PLL时钟源 */
    RCU_CFG0 &= ~(RCU_CFG0_PLLSEL | RCU_CFG0_PREDV0);
    RCU_CFG0 |= (RCU_PLLSRC_HXTAL | RCU_CFG0_PREDV0);

    /* CK_PLL = (CK_HXTAL/2) * 6 = 24 MHz */
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4);
    RCU_CFG0 |= RCU_PLL_MUL6;

#elif defined(GD32F10X_CL)
    /* CK_PLL = (CK_PREDIV0) * 6 = 24 MHz */
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4);
    RCU_CFG0 |= (RCU_PLLSRC_HXTAL | RCU_PLL_MUL6);

    /* CK_PREDIV0 = (CK_HXTAL)/5 *8 /10 = 4 MHz */
    RCU_CFG1 &= ~(RCU_CFG1_PREDV0SEL | RCU_CFG1_PLL1MF | RCU_CFG1_PREDV1 | RCU_CFG1_PREDV0);
    RCU_CFG1 |= (RCU_PREDV0SRC_CKPLL1 | RCU_PLL1_MUL8 | RCU_PREDV1_DIV5 | RCU_PREDV0_DIV10);

    /* 使能PLL1 */
    RCU_CTL |= RCU_CTL_PLL1EN;
    /* 等待PLL1就绪 */
    while((RCU_CTL & RCU_CTL_PLL1STB) == 0){
    }
#endif /* 以下仅适用于GD32F10X_MD/HD/XD */

    /* 使能PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* 等待PLL稳定 */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }

    /* 选择PLL作为系统时钟 */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLL;

    /* 等待PLL被选为系统时钟 */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLL)){
    }
}

#elif defined (__SYSTEM_CLOCK_36M_PLL_HXTAL)
/*!
    \brief      把系统时钟配置为36M：PLL以HXTAL为源（MD/HD/XD:8M；CL:25M）
    \param[in]  无
    \param[out] 无
    \retval     无
*/
static void system_clock_36m_hxtal(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;

    /* 使能HXTAL */
    RCU_CTL |= RCU_CTL_HXTALEN;

    /* 等待HXTAL稳定，或超过HXTAL_STARTUP_TIMEOUT启动超时 */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_HXTALSTB);
    }while((0U == stab_flag) && (HXTAL_STARTUP_TIMEOUT != timeout));

    /* 启动失败 */
    if(0U == (RCU_CTL & RCU_CTL_HXTALSTB)){
        while(1){
        }
    }

    /* HXTAL已稳定 */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/1 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV1;
    /* APB1 = AHB/2 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV2;

#if (defined(GD32F10X_MD) || defined(GD32F10X_HD) || defined(GD32F10X_XD))
    /* 选择HXTAL/2作为PLL时钟源 */
    RCU_CFG0 &= ~(RCU_CFG0_PLLSEL | RCU_CFG0_PREDV0);
    RCU_CFG0 |= (RCU_PLLSRC_HXTAL | RCU_CFG0_PREDV0);

    /* CK_PLL = (CK_HXTAL/2) * 9 = 36 MHz */
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4);
    RCU_CFG0 |= RCU_PLL_MUL9;

#elif defined(GD32F10X_CL)
    /* CK_PLL = (CK_PREDIV0) * 9 = 36 MHz */
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4);
    RCU_CFG0 |= (RCU_PLLSRC_HXTAL | RCU_PLL_MUL9);

    /* CK_PREDIV0 = (CK_HXTAL)/5 *8 /10 = 4 MHz */
    RCU_CFG1 &= ~(RCU_CFG1_PREDV0SEL | RCU_CFG1_PLL1MF | RCU_CFG1_PREDV1 | RCU_CFG1_PREDV0);
    RCU_CFG1 |= (RCU_PREDV0SRC_CKPLL1 | RCU_PLL1_MUL8 | RCU_PREDV1_DIV5 | RCU_PREDV0_DIV10);

    /* 使能PLL1 */
    RCU_CTL |= RCU_CTL_PLL1EN;
    /* 等待PLL1就绪 */
    while((RCU_CTL & RCU_CTL_PLL1STB) == 0){
    }
#endif /* 以下仅适用于GD32F10X_MD/HD/XD */

    /* 使能PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* 等待PLL稳定 */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }

    /* 选择PLL作为系统时钟 */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLL;

    /* 等待PLL被选为系统时钟 */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLL)){
    }
}

#elif defined (__SYSTEM_CLOCK_48M_PLL_HXTAL)
/*!
    \brief      把系统时钟配置为48M：PLL以HXTAL为源（MD/HD/XD:8M；CL:25M）
    \param[in]  无
    \param[out] 无
    \retval     无
*/
static void system_clock_48m_hxtal(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;

    /* 使能HXTAL */
    RCU_CTL |= RCU_CTL_HXTALEN;

    /* 等待HXTAL稳定，或超过HXTAL_STARTUP_TIMEOUT启动超时 */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_HXTALSTB);
    }while((0U == stab_flag) && (HXTAL_STARTUP_TIMEOUT != timeout));

    /* 启动失败 */
    if(0U == (RCU_CTL & RCU_CTL_HXTALSTB)){
        while(1){
        }
    }

    /* HXTAL已稳定 */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/1 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV1;
    /* APB1 = AHB/2 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV2;

#if (defined(GD32F10X_MD) || defined(GD32F10X_HD) || defined(GD32F10X_XD))
    /* 选择HXTAL/2作为PLL时钟源 */
    RCU_CFG0 &= ~(RCU_CFG0_PLLSEL | RCU_CFG0_PREDV0);
    RCU_CFG0 |= (RCU_PLLSRC_HXTAL | RCU_CFG0_PREDV0);

    /* CK_PLL = (CK_HXTAL/2) * 12 = 48 MHz */
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4);
    RCU_CFG0 |= RCU_PLL_MUL12;

#elif defined(GD32F10X_CL)
    /* CK_PLL = (CK_PREDIV0) * 12 = 48 MHz */
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4);
    RCU_CFG0 |= (RCU_PLLSRC_HXTAL | RCU_PLL_MUL12);

    /* CK_PREDIV0 = (CK_HXTAL)/5 *8 /10 = 4 MHz */
    RCU_CFG1 &= ~(RCU_CFG1_PREDV0SEL | RCU_CFG1_PLL1MF | RCU_CFG1_PREDV1 | RCU_CFG1_PREDV0);
    RCU_CFG1 |= (RCU_PREDV0SRC_CKPLL1 | RCU_PLL1_MUL8 | RCU_PREDV1_DIV5 | RCU_PREDV0_DIV10);

    /* 使能PLL1 */
    RCU_CTL |= RCU_CTL_PLL1EN;
    /* 等待PLL1就绪 */
    while((RCU_CTL & RCU_CTL_PLL1STB) == 0){
    }
#endif /* 以下仅适用于GD32F10X_MD/HD/XD */

    /* 使能PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* 等待PLL稳定 */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }

    /* 选择PLL作为系统时钟 */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLL;

    /* 等待PLL被选为系统时钟 */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLL)){
    }
}

#elif defined (__SYSTEM_CLOCK_56M_PLL_HXTAL)
/*!
    \brief      把系统时钟配置为56M：PLL以HXTAL为源（MD/HD/XD:8M；CL:25M）
    \param[in]  无
    \param[out] 无
    \retval     无
*/
static void system_clock_56m_hxtal(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;

    /* 使能HXTAL */
    RCU_CTL |= RCU_CTL_HXTALEN;

    /* 等待HXTAL稳定，或超过HXTAL_STARTUP_TIMEOUT启动超时 */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_HXTALSTB);
    }while((0U == stab_flag) && (HXTAL_STARTUP_TIMEOUT != timeout));

    /* 启动失败 */
    if(0U == (RCU_CTL & RCU_CTL_HXTALSTB)){
        while(1){
        }
    }

    /* HXTAL已稳定 */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/1 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV1;
    /* APB1 = AHB/2 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV2;

#if (defined(GD32F10X_MD) || defined(GD32F10X_HD) || defined(GD32F10X_XD))
    /* 选择HXTAL/2作为PLL时钟源 */
    RCU_CFG0 &= ~(RCU_CFG0_PLLSEL | RCU_CFG0_PREDV0);
    RCU_CFG0 |= (RCU_PLLSRC_HXTAL | RCU_CFG0_PREDV0);

    /* CK_PLL = (CK_HXTAL/2) * 14 = 56 MHz */
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4);
    RCU_CFG0 |= RCU_PLL_MUL14;

#elif defined(GD32F10X_CL)
    /* CK_PLL = (CK_PREDIV0) * 14 = 56 MHz */
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4);
    RCU_CFG0 |= (RCU_PLLSRC_HXTAL | RCU_PLL_MUL14);

    /* CK_PREDIV0 = (CK_HXTAL)/5 *8 /10 = 4 MHz */
    RCU_CFG1 &= ~(RCU_CFG1_PREDV0SEL | RCU_CFG1_PLL1MF | RCU_CFG1_PREDV1 | RCU_CFG1_PREDV0);
    RCU_CFG1 |= (RCU_PREDV0SRC_CKPLL1 | RCU_PLL1_MUL8 | RCU_PREDV1_DIV5 | RCU_PREDV0_DIV10);

    /* 使能PLL1 */
    RCU_CTL |= RCU_CTL_PLL1EN;
    /* 等待PLL1就绪 */
    while((RCU_CTL & RCU_CTL_PLL1STB) == 0){
    }
#endif /* 以下仅适用于GD32F10X_MD/HD/XD */

    /* 使能PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* 等待PLL稳定 */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }

    /* 选择PLL作为系统时钟 */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLL;

    /* 等待PLL被选为系统时钟 */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLL)){
    }
}

#elif defined (__SYSTEM_CLOCK_72M_PLL_HXTAL)
/*!
    \brief      把系统时钟配置为72M：PLL以HXTAL为源（MD/HD/XD:8M；CL:25M）
    \param[in]  无
    \param[out] 无
    \retval     无
*/
static void system_clock_72m_hxtal(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;

    /* 使能HXTAL */
    RCU_CTL |= RCU_CTL_HXTALEN;

    /* 等待HXTAL稳定，或超过HXTAL_STARTUP_TIMEOUT启动超时 */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_HXTALSTB);
    }while((0U == stab_flag) && (HXTAL_STARTUP_TIMEOUT != timeout));

    /* 启动失败 */
    if(0U == (RCU_CTL & RCU_CTL_HXTALSTB)){
        while(1){
        }
    }

    /* HXTAL已稳定 */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/1 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV1;
    /* APB1 = AHB/2 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV2;

#if (defined(GD32F10X_MD) || defined(GD32F10X_HD) || defined(GD32F10X_XD))
    /* 选择HXTAL/2作为PLL时钟源 */
    RCU_CFG0 &= ~(RCU_CFG0_PLLSEL | RCU_CFG0_PREDV0);
    RCU_CFG0 |= (RCU_PLLSRC_HXTAL | RCU_CFG0_PREDV0);

    /* CK_PLL = (CK_HXTAL/2) * 18 = 72 MHz */
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4);
    RCU_CFG0 |= RCU_PLL_MUL18;

#elif defined(GD32F10X_CL)
    /* CK_PLL = (CK_PREDIV0) * 18 = 72 MHz */ 
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4);
    RCU_CFG0 |= (RCU_PLLSRC_HXTAL | RCU_PLL_MUL18);

    /* CK_PREDIV0 = (CK_HXTAL)/5 *8 /10 = 4 MHz */ 
    RCU_CFG1 &= ~(RCU_CFG1_PREDV0SEL | RCU_CFG1_PLL1MF | RCU_CFG1_PREDV1 | RCU_CFG1_PREDV0);
    RCU_CFG1 |= (RCU_PREDV0SRC_CKPLL1 | RCU_PLL1_MUL8 | RCU_PREDV1_DIV5 | RCU_PREDV0_DIV10);

    /* 使能PLL1 */
    RCU_CTL |= RCU_CTL_PLL1EN;
    /* 等待PLL1就绪 */
    while((RCU_CTL & RCU_CTL_PLL1STB) == 0){
    }
#endif /* 以下仅适用于GD32F10X_MD/HD/XD */

    /* 使能PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* 等待PLL稳定 */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }

    /* 选择PLL作为系统时钟 */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLL;

    /* 等待PLL被选为系统时钟 */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLL)){
    }
}

#elif defined (__SYSTEM_CLOCK_96M_PLL_HXTAL)
/*!
    \brief      把系统时钟配置为96M：PLL以HXTAL为源（MD/HD/XD:8M；CL:25M）
    \param[in]  无
    \param[out] 无
    \retval     无
*/
static void system_clock_96m_hxtal(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;

    /* 使能HXTAL */
    RCU_CTL |= RCU_CTL_HXTALEN;

    /* 等待HXTAL稳定，或超过HXTAL_STARTUP_TIMEOUT启动超时 */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_HXTALSTB);
    }while((0U == stab_flag) && (HXTAL_STARTUP_TIMEOUT != timeout));

    /* 启动失败 */
    if(0U == (RCU_CTL & RCU_CTL_HXTALSTB)){
        while(1){
        }
    }

    /* HXTAL已稳定 */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/1 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV1;
    /* APB1 = AHB/2 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV2;

#if (defined(GD32F10X_MD) || defined(GD32F10X_HD) || defined(GD32F10X_XD))
    /* 选择HXTAL/2作为PLL时钟源 */
    RCU_CFG0 &= ~(RCU_CFG0_PLLSEL | RCU_CFG0_PREDV0);
    RCU_CFG0 |= (RCU_PLLSRC_HXTAL | RCU_CFG0_PREDV0);

    /* CK_PLL = (CK_HXTAL/2) * 24 = 96 MHz */
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4);
    RCU_CFG0 |= RCU_PLL_MUL24;

#elif defined(GD32F10X_CL)
    /* CK_PLL = (CK_PREDIV0) * 24 = 96 MHz */
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4);
    RCU_CFG0 |= (RCU_PLLSRC_HXTAL | RCU_PLL_MUL24);

    /* CK_PREDIV0 = (CK_HXTAL)/5 *8 /10 = 4 MHz */
    RCU_CFG1 &= ~(RCU_CFG1_PREDV0SEL | RCU_CFG1_PLL1MF | RCU_CFG1_PREDV1 | RCU_CFG1_PREDV0);
    RCU_CFG1 |= (RCU_PREDV0SRC_CKPLL1 | RCU_PLL1_MUL8 | RCU_PREDV1_DIV5 | RCU_PREDV0_DIV10);

    /* 使能PLL1 */
    RCU_CTL |= RCU_CTL_PLL1EN;
    /* 等待PLL1就绪 */
    while((RCU_CTL & RCU_CTL_PLL1STB) == 0){
    }
#endif /* 以下仅适用于GD32F10X_MD/HD/XD */

    /* 使能PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* 等待PLL稳定 */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }

    /* 选择PLL作为系统时钟 */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLL;

    /* 等待PLL被选为系统时钟 */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLL)){
    }
}

#elif defined (__SYSTEM_CLOCK_108M_PLL_HXTAL)
/*!
    \brief      把系统时钟配置为108M：PLL以HXTAL为源（MD/HD/XD:8M；CL:25M）
    \param[in]  无
    \param[out] 无
    \retval     无
*/
static void system_clock_108m_hxtal(void)
{
    uint32_t timeout   = 0U;
    uint32_t stab_flag = 0U;

    /* 使能HXTAL */
    RCU_CTL |= RCU_CTL_HXTALEN;

    /* 等待HXTAL稳定，或超过HXTAL_STARTUP_TIMEOUT启动超时 */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_HXTALSTB);
    }while((0U == stab_flag) && (HXTAL_STARTUP_TIMEOUT != timeout));

    /* 启动失败 */
    if(0U == (RCU_CTL & RCU_CTL_HXTALSTB)){
        while(1){
        }
    }

    /* HXTAL已稳定 */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/1 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV1;
    /* APB1 = AHB/2 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV2;

#if (defined(GD32F10X_MD) || defined(GD32F10X_HD) || defined(GD32F10X_XD))
    /* 选择HXTAL/2作为PLL时钟源 */
    RCU_CFG0 &= ~(RCU_CFG0_PLLSEL | RCU_CFG0_PREDV0);
    RCU_CFG0 |= (RCU_PLLSRC_HXTAL | RCU_CFG0_PREDV0);

    /* CK_PLL = (CK_HXTAL/2) * 27 = 108 MHz */
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4);
    RCU_CFG0 |= RCU_PLL_MUL27;

#elif defined(GD32F10X_CL)
    /* CK_PLL = (CK_PREDIV0) * 27 = 108 MHz */ 
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4);
    RCU_CFG0 |= (RCU_PLLSRC_HXTAL | RCU_PLL_MUL27);

    /* CK_PREDIV0 = (CK_HXTAL)/5 *8 /10 = 4 MHz */ 
    RCU_CFG1 &= ~(RCU_CFG1_PREDV0SEL | RCU_CFG1_PLL1MF | RCU_CFG1_PREDV1 | RCU_CFG1_PREDV0);
    RCU_CFG1 |= (RCU_PREDV0SRC_CKPLL1 | RCU_PLL1_MUL8 | RCU_PREDV1_DIV5 | RCU_PREDV0_DIV10);

    /* 使能PLL1 */
    RCU_CTL |= RCU_CTL_PLL1EN;
    /* 等待PLL1就绪 */
    while((RCU_CTL & RCU_CTL_PLL1STB) == 0){
    }
#endif /* 以下仅适用于GD32F10X_MD/HD/XD */

    /* 使能PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* 等待PLL稳定 */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }

    /* 选择PLL作为系统时钟 */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLL;

    /* 等待PLL被选为系统时钟 */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLL)){
    }
}

#elif defined (__SYSTEM_CLOCK_48M_PLL_IRC8M)
/*!
    \brief      configure the system clock to 48M by PLL which selects IRC8M as its clock source
    \param[in]  无
    \param[out] 无
    \retval     无
*/
static void system_clock_48m_irc8m(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;
    
    /* 使能IRC8M */
    RCU_CTL |= RCU_CTL_IRC8MEN;

    /* wait until IRC8M is stable or the startup time is longer than IRC8M_STARTUP_TIMEOUT */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_IRC8MSTB);
    }
    while((0U == stab_flag) && (IRC8M_STARTUP_TIMEOUT != timeout));

    /* 启动失败 */
    if(0U == (RCU_CTL & RCU_CTL_IRC8MSTB)){
      while(1){
      }
    }

    /* IRC8M is stable */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/1 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV1;
    /* APB1 = AHB/2 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV2;

    /* CK_PLL = (CK_IRC8M/2) * 12 = 48 MHz */
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4);
    RCU_CFG0 |= RCU_PLL_MUL12;

    /* 使能PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* 等待PLL稳定 */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }

    /* 选择PLL作为系统时钟 */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLL;

    /* 等待PLL被选为系统时钟 */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLL)){
    }
}

#elif defined (__SYSTEM_CLOCK_72M_PLL_IRC8M)
/*!
    \brief      configure the system clock to 72M by PLL which selects IRC8M as its clock source
    \param[in]  无
    \param[out] 无
    \retval     无
*/
static void system_clock_72m_irc8m(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;
    
    /* 使能IRC8M */
    RCU_CTL |= RCU_CTL_IRC8MEN;

    /* wait until IRC8M is stable or the startup time is longer than IRC8M_STARTUP_TIMEOUT */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_IRC8MSTB);
    }
    while((0U == stab_flag) && (IRC8M_STARTUP_TIMEOUT != timeout));

    /* 启动失败 */
    if(0U == (RCU_CTL & RCU_CTL_IRC8MSTB)){
      while(1){
      }
    }

    /* IRC8M is stable */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/1 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV1;
    /* APB1 = AHB/2 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV2;

    /* CK_PLL = (CK_IRC8M/2) * 18 = 72 MHz */
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4);
    RCU_CFG0 |= RCU_PLL_MUL18;

    /* 使能PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* 等待PLL稳定 */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }

    /* 选择PLL作为系统时钟 */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLL;

    /* 等待PLL被选为系统时钟 */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLL)){
    }
}

#elif defined (__SYSTEM_CLOCK_108M_PLL_IRC8M)
/*!
    \brief      configure the system clock to 108M by PLL which selects IRC8M as its clock source
    \param[in]  无
    \param[out] 无
    \retval     无
*/
static void system_clock_108m_irc8m(void)
{
    uint32_t timeout = 0U;
    uint32_t stab_flag = 0U;
    
    /* 使能IRC8M */
    RCU_CTL |= RCU_CTL_IRC8MEN;

    /* wait until IRC8M is stable or the startup time is longer than IRC8M_STARTUP_TIMEOUT */
    do{
        timeout++;
        stab_flag = (RCU_CTL & RCU_CTL_IRC8MSTB);
    }
    while((0U == stab_flag) && (IRC8M_STARTUP_TIMEOUT != timeout));

    /* 启动失败 */
    if(0U == (RCU_CTL & RCU_CTL_IRC8MSTB)){
      while(1){
      }
    }

    /* IRC8M is stable */
    /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;
    /* APB2 = AHB/1 */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV1;
    /* APB1 = AHB/2 */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV2;

    /* CK_PLL = (CK_IRC8M/2) * 27 = 108 MHz */
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4);
    RCU_CFG0 |= RCU_PLL_MUL27;

    /* 使能PLL */
    RCU_CTL |= RCU_CTL_PLLEN;

    /* 等待PLL稳定 */
    while(0U == (RCU_CTL & RCU_CTL_PLLSTB)){
    }

    /* 选择PLL作为系统时钟 */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLL;

    /* 等待PLL被选为系统时钟 */
    while(0U == (RCU_CFG0 & RCU_SCSS_PLL)){
    }
}

#endif
