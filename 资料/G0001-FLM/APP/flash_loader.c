/***********************************************************************************************************************
    @file    flash_loader.c
    @author  FAE Team
    @date    06-Jul-2026
    @brief   Flash Loader Module (FLM) for MM32G0001
             Based on reverse engineering of MM32G0001_16.FLM
  **********************************************************************************************************************
    @attention

    <h2><center>&copy; Copyright(c) <2023> <MindMotion></center></h2>

      Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
    following conditions are met:
    1. Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and
       the following disclaimer in the documentation and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or
       promote products derived from this software without specific prior written permission.

      THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
    INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
    WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  *********************************************************************************************************************/

/* Files include */
#include <string.h>
#include "flash_loader.h"

/** @addtogroup MM32G0001_FLM
  * @{
  */

/** @defgroup FLASH_Private_Defines
  * @{
  */

/* FLASH Base Address */
#define FLASH_BASE                  ((uint32_t)0x40022000)

/* IWDG Base Address */
#define IWDG_BASE                   ((uint32_t)0x40003000)

/* FLASH Register Structure */
typedef struct
{
    volatile uint32_t ACR;          /* offset: 0x00 - Flash access control register */
    volatile uint32_t KEYR;         /* offset: 0x04 - FPEC key register */
    volatile uint32_t OPTKEYR;      /* offset: 0x08 - Flash OPTKEY register */
    volatile uint32_t SR;           /* offset: 0x0C - Flash status register */
    volatile uint32_t CR;           /* offset: 0x10 - Flash control register */
    volatile uint32_t AR;           /* offset: 0x14 - Flash address register */
    volatile uint32_t RESERVED0x18; /* offset: 0x18 */
    volatile uint32_t OBR;          /* offset: 0x1C - Option byte register */
    volatile uint32_t WRPR;         /* offset: 0x20 - Write protection register */
} FLASH_TypeDef;

/* IWDG Register Structure */
typedef struct
{
    volatile uint32_t KR;           /* offset: 0x00 - Key register */
    volatile uint32_t PR;           /* offset: 0x04 - Prescaler register */
    volatile uint32_t RLR;          /* offset: 0x08 - Reload register */
    volatile uint32_t SR;           /* offset: 0x0C - Status register */
    volatile uint32_t CR;           /* offset: 0x10 - Control register */
    volatile uint32_t IGEN;         /* offset: 0x14 - Interrupt generate register */
    volatile uint32_t CNT;          /* offset: 0x18 - Counter register */
} IWDG_TypeDef;

#define FLASH                       ((FLASH_TypeDef *)FLASH_BASE)
#define IWDG                        ((IWDG_TypeDef *)IWDG_BASE)

/* FLASH Keys */
#define FLASH_KEY1                  ((uint32_t)0x45670123)
#define FLASH_KEY2                  ((uint32_t)0xCDEF89AB)

/* IWDG Key */
#define IWDG_KEY_ENABLE             ((uint32_t)0x00005555)
#define IWDG_KEY_REFRESH            ((uint32_t)0x0000AAAA)

/* FLASH_CR Register Bit Definition */
#define FLASH_CR_PG                 ((uint32_t)0x00000001)  /* Programming */
#define FLASH_CR_PER                ((uint32_t)0x00000002)  /* Page Erase */
#define FLASH_CR_MER                ((uint32_t)0x00000004)  /* Mass Erase */
#define FLASH_CR_OPTPG              ((uint32_t)0x00000010)  /* Option byte programming */
#define FLASH_CR_OPTER              ((uint32_t)0x00000020)  /* Option byte erase */
#define FLASH_CR_STRT               ((uint32_t)0x00000040)  /* Start */
#define FLASH_CR_LOCK               ((uint32_t)0x00000080)  /* Lock */

/* FLASH_SR Register Bit Definition */
#define FLASH_SR_BSY                ((uint32_t)0x00000001)  /* Busy */
#define FLASH_SR_PGERR              ((uint32_t)0x00000004)  /* Programming error */
#define FLASH_SR_WRPRTERR           ((uint32_t)0x00000010)  /* Write protection error */
#define FLASH_SR_EOP                ((uint32_t)0x00000020)  /* End of operation */

/* Error flags */
#define FLASH_SR_ERROR_FLAGS        (FLASH_SR_EOP | FLASH_SR_PGERR)  /* 0x14 */

/* Flash Configuration */
#define FLASH_START_ADDR            ((uint32_t)0x08000000)
#define FLASH_PAGE_SIZE             ((uint32_t)1024)        /* 1KB per page */
#define FLASH_SIZE                  ((uint32_t)16384)       /* 16KB total */

/** @}
  */

/** @defgroup FLASH_Private_Functions
  * @{
  */

/**
  * @brief  Unlock flash for programming
  */
static void FLASH_Unlock(void)
{
    FLASH->KEYR = FLASH_KEY1;
    FLASH->KEYR = FLASH_KEY2;
}

/**
  * @brief  Unlock option byte for programming
  */
static void FLASH_OPTB_Unlock(void)
{
    FLASH->OPTKEYR = FLASH_KEY1;
    FLASH->OPTKEYR = FLASH_KEY2;
}

/**
  * @brief  Wait for flash operation to complete with IWDG refresh
  * @retval 1 if OK, 0 if timeout
  */
static uint32_t FLASH_WaitBusy(void)
{
    while (FLASH->SR & FLASH_SR_BSY)
    {
        IWDG->KR = IWDG_KEY_REFRESH;  /* Refresh watchdog */
    }
    return 1;
}

/**
  * @brief  Initialize Flash
  * @param  adr: Device base address
  * @param  clk: System clock frequency in Hz
  * @param  fnc: Function code
  * @retval 0 on success
  */
uint32_t Init(uint32_t adr, uint32_t clk, uint32_t fnc)
{
    /* Unlock Flash */
    FLASH->KEYR = FLASH_KEY1;
    FLASH->KEYR = FLASH_KEY2;

    /* Unlock Option Byte */
    FLASH->OPTKEYR = FLASH_KEY1;
    FLASH->OPTKEYR = FLASH_KEY2;

    /* Configure ACR: 0 wait states, no prefetch */
    FLASH->ACR = 0;

    /* Clear SR flags (EOP | PGERR) */
    FLASH->SR |= FLASH_SR_ERROR_FLAGS;

    /* Check OBR bit 30, if NOT set, configure IWDG */
    if (!(FLASH->OBR & (1 << 30)))
    {
        IWDG->KR  = IWDG_KEY_ENABLE;  /* Enable register access */
        IWDG->PR  = 6;                 /* Prescaler */
        IWDG->RLR = 0xFFF;             /* Reload value */
    }

    return 0;
}

/**
  * @brief  Uninitialize Flash
  * @param  adr: Device base address
  * @param  clk: System clock frequency in Hz
  * @param  fnc: Function code
  * @retval 0 on success
  */
uint32_t UnInit(uint32_t adr, uint32_t clk, uint32_t fnc)
{
    /* Lock Flash */
    FLASH->CR |= FLASH_CR_LOCK;

    /* Clear PER and MER bits */
    FLASH->CR &= ~(FLASH_CR_PER | FLASH_CR_MER);

    return 0;
}

/**
  * @brief  Erase all Flash pages
  * @param  adr: Flash start address
  * @retval 0 on success, 1 on error
  */
static uint32_t FLASH_EraseAllPages(uint32_t adr)
{
    /* Set address */
    FLASH->AR = FLASH_START_ADDR;

    /* Clear SR flags */
    FLASH->SR |= FLASH_SR_ERROR_FLAGS;

    /* Enable mass erase */
    FLASH->CR |= FLASH_CR_MER;

    /* Start erase */
    FLASH->CR |= FLASH_CR_STRT;

    /* Wait with IWDG refresh */
    FLASH_WaitBusy();

    /* Clear MER bit */
    FLASH->CR &= ~FLASH_CR_MER;

    /* Check for errors */
    if (FLASH->SR & FLASH_SR_ERROR_FLAGS)
    {
        FLASH->SR |= FLASH_SR_ERROR_FLAGS;
        return 1;
    }

    return 0;
}

/**
  * @brief  Erase option block
  * @param  adr: Option block address
  * @retval 0 on success, 1 on error
  */
static uint32_t FLASH_EraseOptionBlock(uint32_t adr)
{
    /* Clear SR flags */
    FLASH->SR |= FLASH_SR_ERROR_FLAGS;

    /* Unlock Option Byte */
    FLASH->OPTKEYR = FLASH_KEY1;
    FLASH->OPTKEYR = FLASH_KEY2;

    /* Set address */
    FLASH->AR = adr;

    /* Enable option byte erase */
    FLASH->CR |= FLASH_CR_OPTER;

    /* Start erase */
    FLASH->CR |= FLASH_CR_STRT;

    /* Wait with IWDG refresh */
    FLASH_WaitBusy();

    /* Clear OPTER bit */
    FLASH->CR &= ~FLASH_CR_OPTER;

    /* Check for errors */
    if (FLASH->SR & FLASH_SR_ERROR_FLAGS)
    {
        FLASH->SR |= FLASH_SR_ERROR_FLAGS;
        return 1;
    }

    return 0;
}

/**
  * @brief  Erase option block and write 0x5AA5
  * @retval 0 on success, 1 on error
  */
static uint32_t EraseBlock1ffff800write5AA5(void)
{
    /* Erase option block at 0x1FFFF800 */
    FLASH_EraseOptionBlock(0x1FFFF800);

    /* Unlock Option Byte */
    FLASH->OPTKEYR = FLASH_KEY1;
    FLASH->OPTKEYR = FLASH_KEY2;

    /* Enable option byte programming */
    FLASH->CR |= FLASH_CR_OPTPG;

    /* Write 0x5AA5 to option block */
    *(volatile uint16_t *)0x1FFFF800 = 0x5AA5;

    /* Wait with IWDG refresh */
    FLASH_WaitBusy();

    /* Clear OPTPG bit */
    FLASH->CR &= ~FLASH_CR_OPTPG;

    /* Check for errors */
    if (FLASH->SR & FLASH_SR_ERROR_FLAGS)
    {
        FLASH->SR |= FLASH_SR_ERROR_FLAGS;
        return 1;
    }

    return 0;
}

/**
  * @brief  Erase complete Flash
  * @param  adr: Device base address
  * @param  clk: System clock frequency in Hz
  * @param  fnc: Function code
  * @retval 0 on success
  */
uint32_t EraseChip(uint32_t adr, uint32_t clk, uint32_t fnc)
{
    /* Unlock Flash */
    FLASH_Unlock();

    /* Erase all pages */
    FLASH_EraseAllPages(adr);

    return 0;
}

/**
  * @brief  Erase Sector in Flash
  * @param  adr: Sector address
  * @param  sz: Sector size
  * @param  clk: System clock frequency in Hz
  * @param  fnc: Function code
  * @retval 0 on success, 1 on error
  */
uint32_t EraseSector(uint32_t adr, uint32_t sz, uint32_t clk, uint32_t fnc)
{
    /* Clear SR flags */
    FLASH->SR |= FLASH_SR_ERROR_FLAGS;

    /* Enable page erase */
    FLASH->CR |= FLASH_CR_PER;

    /* Set page address */
    FLASH->AR = adr;

    /* Start erase */
    FLASH->CR |= FLASH_CR_STRT;

    /* Wait with IWDG refresh */
    FLASH_WaitBusy();

    /* Clear PER bit */
    FLASH->CR &= ~FLASH_CR_PER;

    /* Check for errors */
    if (FLASH->SR & FLASH_SR_ERROR_FLAGS)
    {
        FLASH->SR |= FLASH_SR_ERROR_FLAGS;
        return 1;
    }

    return 0;
}

/**
  * @brief  Blank check (always returns 1 = not blank)
  * @param  adr: Start address
  * @param  sz: Size in bytes
  * @param  clk: System clock frequency in Hz
  * @retval 1 (not blank)
  */
uint32_t BlankCheck(uint32_t adr, uint32_t sz, uint32_t clk)
{
    return 1;
}

/**
  * @brief  Program Page in Flash
  * @param  adr: Page start address
  * @param  sz: Page size in bytes (must be multiple of 2)
  * @param  buf: Pointer to data buffer
  * @param  clk: System clock frequency in Hz
  * @param  fnc: Function code
  * @retval 0 on success, 1 on error
  */
uint32_t ProgramPage(uint32_t adr, uint32_t sz, uint8_t *buf, uint32_t clk, uint32_t fnc)
{
    uint32_t i;
    uint16_t data;

    /* Clear SR flags */
    FLASH->SR |= FLASH_SR_ERROR_FLAGS;

    /* Program half-words (16-bit) */
    for (i = 0; i < sz; i += 2)
    {
        /* Enable programming */
        FLASH->CR |= FLASH_CR_PG;

        /* Get data from buffer (little-endian) */
        data = (uint16_t)(buf[i] | (buf[i + 1] << 8));

        /* Program half-word */
        *(volatile uint16_t *)(adr + i) = data;

        /* Wait with IWDG refresh */
        FLASH_WaitBusy();

        /* Clear programming bit */
        FLASH->CR &= ~FLASH_CR_PG;

        /* Check for errors */
        if (FLASH->SR & FLASH_SR_ERROR_FLAGS)
        {
            FLASH->SR |= FLASH_SR_ERROR_FLAGS;
            return 1;
        }
    }

    return 0;
}

/** @}
  */

/** @}
  */

/********************************************** (C) Copyright MindMotion **********************************************/
