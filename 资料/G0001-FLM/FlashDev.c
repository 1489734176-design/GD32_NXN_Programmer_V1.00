/***********************************************************************************************************************
 * @file    FlashDev.c
 * @brief   Flash Device Description for MM32G0001
 *          Used by Keil MDK Flash Programming
 **********************************************************************************************************************/

/* Flash Device Structure Definition */
#define UNKNOWN    0
#define ONCHIP     1
#define EXT8BIT    2
#define EXT16BIT   3
#define EXT32BIT   4
#define EXTSPI     5

#define SECTOR_END 0xFFFFFFFF

#define FLASH_DRV_VERS    0x0100    /* Driver Version 1.0 */

struct FlashSectors {
    unsigned long   szSector;       /* Sector Size in Bytes */
    unsigned long   AddrSector;     /* Address of Sector */
};

struct FlashDevice {
    unsigned long   Vers;           /* Version Number and Architecture */
    char            DevName[128];   /* Device Name and Description */
    unsigned long   DevType;        /* Device Type: ONCHIP, EXT8BIT, ... */
    unsigned long   DevAdr;         /* Default Device Start Address */
    unsigned long   szDev;          /* Total Size of Device */
    unsigned long   szPage;         /* Programming Page Size */
    unsigned long   Res;            /* Reserved for future Extension */
    unsigned char   valEmpty;       /* Content of Erased Memory */
    unsigned long   toProg;         /* Time Out of Program Page Function */
    unsigned long   toErase;        /* Time Out of Erase Sector Function */
    struct FlashSectors sectors[32];/* Sector Information (16 pages + 1 end) */
};

/* Flash Device Description */
struct FlashDevice const FlashDevice = {
    FLASH_DRV_VERS,             /* Driver Version, do not modify! */
    "MM32G0001 16KB Flash",     /* Device Name */
    ONCHIP,                     /* Device Type */
    0x08000000,                 /* Device Start Address */
    0x00004000,                 /* Device Size (16KB) */
    1024,                       /* Programming Page Size */
    0,                          /* Reserved, must be 0 */
    0xFF,                       /* Initial Content of Erased Memory */
    1000,                       /* Program Page Timeout 1000 mSec */
    60000,                      /* Erase Sector Timeout 60000 mSec */

    /* Sector Information */
    {
        {0x000400, 0x000000},    /* Sector Size 1KB (1024) */
        {SECTOR_END}             /* End of sector list */
    }
};
