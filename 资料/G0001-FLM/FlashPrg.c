/***********************************************************************************************************************
 * @file    FlashPrg.c
 * @brief   Flash Programming Functions for MM32G0001
 *          Based on reverse engineering of MM32G0001_16.FLM
 **********************************************************************************************************************/

/* Hardware Register Definitions */
#define FLASH_BASE              ((volatile unsigned long *)0x40022000)
#define IWDG_BASE               ((volatile unsigned long *)0x40003000)

/* FLASH Register Offsets */
#define FLASH_ACR               0x00
#define FLASH_KEYR              0x01
#define FLASH_OPTKEYR           0x02
#define FLASH_SR                0x03
#define FLASH_CR                0x04
#define FLASH_AR                0x05
#define FLASH_OBR               0x07

/* IWDG Register Offsets */
#define IWDG_KR                 0x00
#define IWDG_PR                 0x01
#define IWDG_RLR                0x02

/* FLASH Keys */
#define FLASH_KEY1              0x45670123
#define FLASH_KEY2              0xCDEF89AB

/* IWDG Keys */
#define IWDG_KEY_ENABLE         0x5555
#define IWDG_KEY_REFRESH        0xAAAA

/* FLASH_CR Bits */
#define FLASH_CR_PG             0x00000001
#define FLASH_CR_PER            0x00000002
#define FLASH_CR_MER            0x00000004
#define FLASH_CR_OPTPG          0x00000010
#define FLASH_CR_OPTER          0x00000020
#define FLASH_CR_STRT           0x00000040
#define FLASH_CR_LOCK           0x00000080

/* FLASH_SR Bits */
#define FLASH_SR_BSY            0x00000001
#define FLASH_SR_PGERR          0x00000004
#define FLASH_SR_WRPRTERR       0x00000010
#define FLASH_SR_EOP            0x00000020

/* Error flags */
#define FLASH_SR_ERROR          (FLASH_SR_EOP | FLASH_SR_PGERR)

/* Flash Configuration */
#define FLASH_START_ADDR        0x08000000

/*
 * Helper Functions
 */

static void FLASH_Unlock(void)
{
    FLASH_BASE[FLASH_KEYR] = FLASH_KEY1;
    FLASH_BASE[FLASH_KEYR] = FLASH_KEY2;
}

static void FLASH_OPTB_Unlock(void)
{
    FLASH_BASE[FLASH_OPTKEYR] = FLASH_KEY1;
    FLASH_BASE[FLASH_OPTKEYR] = FLASH_KEY2;
}

static void FLASH_WaitBusy(void)
{
    while (FLASH_BASE[FLASH_SR] & FLASH_SR_BSY)
    {
        IWDG_BASE[IWDG_KR] = IWDG_KEY_REFRESH;
    }
}

static void FLASH_ClearFlags(void)
{
    FLASH_BASE[FLASH_SR] = FLASH_SR_ERROR;
}

static int FLASH_CheckError(void)
{
    if (FLASH_BASE[FLASH_SR] & FLASH_SR_ERROR)
    {
        FLASH_BASE[FLASH_SR] = FLASH_SR_ERROR;
        return 1;  /* Error */
    }
    return 0;  /* OK */
}

/*
 * Flash Programming Functions (Keil Standard Interface)
 */

int Init(unsigned long adr, unsigned long clk, unsigned long fnc)
{
    /* Unlock Flash */
    FLASH_Unlock();

    /* Unlock Option Byte */
    FLASH_OPTB_Unlock();

    /* Configure ACR: 0 wait states, no prefetch */
    FLASH_BASE[FLASH_ACR] = 0;

    /* Clear SR flags */
    FLASH_ClearFlags();

    /* Check OBR bit 30, if NOT set, configure IWDG */
    if (!(FLASH_BASE[FLASH_OBR] & (1 << 30)))
    {
        IWDG_BASE[IWDG_KR]  = IWDG_KEY_ENABLE;
        IWDG_BASE[IWDG_PR]  = 6;
        IWDG_BASE[IWDG_RLR] = 0xFFF;
    }

    return (0);  /* OK */
}

int UnInit(unsigned long adr, unsigned long clk, unsigned long fnc)
{
    /* Lock Flash */
    FLASH_BASE[FLASH_CR] |= FLASH_CR_LOCK;

    /* Clear PER and MER bits */
    FLASH_BASE[FLASH_CR] &= ~(FLASH_CR_PER | FLASH_CR_MER);

    return (0);  /* OK */
}

int EraseChip(void)
{
    /* Unlock Flash */
    FLASH_Unlock();

    /* Set address */
    FLASH_BASE[FLASH_AR] = FLASH_START_ADDR;

    /* Clear SR flags */
    FLASH_ClearFlags();

    /* Enable mass erase */
    FLASH_BASE[FLASH_CR] |= FLASH_CR_MER;

    /* Start erase */
    FLASH_BASE[FLASH_CR] |= FLASH_CR_STRT;

    /* Wait with IWDG refresh */
    FLASH_WaitBusy();

    /* Clear MER bit */
    FLASH_BASE[FLASH_CR] &= ~FLASH_CR_MER;

    /* Check for errors */
    if (FLASH_CheckError())
        return 1;

    return (0);  /* OK */
}

int EraseSector(unsigned long adr)
{
    /* Clear SR flags */
    FLASH_ClearFlags();

    /* Enable page erase */
    FLASH_BASE[FLASH_CR] |= FLASH_CR_PER;

    /* Set page address */
    FLASH_BASE[FLASH_AR] = adr;

    /* Start erase */
    FLASH_BASE[FLASH_CR] |= FLASH_CR_STRT;

    /* Wait with IWDG refresh */
    FLASH_WaitBusy();

    /* Clear PER bit */
    FLASH_BASE[FLASH_CR] &= ~FLASH_CR_PER;

    /* Check for errors */
    if (FLASH_CheckError())
        return 1;

    return (0);  /* OK */
}

int ProgramPage(unsigned long adr, unsigned long sz, unsigned char *buf)
{
    unsigned long i;
    unsigned short data;

    /* Clear SR flags */
    FLASH_ClearFlags();

    /* Program half-words (16-bit) */
    for (i = 0; i < sz; i += 2)
    {
        /* Enable programming */
        FLASH_BASE[FLASH_CR] |= FLASH_CR_PG;

        /* Get data from buffer (little-endian) */
        data = (unsigned short)(buf[i] | (buf[i + 1] << 8));

        /* Program half-word */
        *((volatile unsigned short *)(adr + i)) = data;

        /* Wait with IWDG refresh */
        FLASH_WaitBusy();

        /* Clear programming bit */
        FLASH_BASE[FLASH_CR] &= ~FLASH_CR_PG;

        /* Check for errors */
        if (FLASH_CheckError())
            return 1;
    }

    return (0);  /* OK */
}
