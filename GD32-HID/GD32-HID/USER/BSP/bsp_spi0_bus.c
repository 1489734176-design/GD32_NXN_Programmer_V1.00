#include "bsp_spi0_bus.h"

/* Flash: PA4/PA5/PA6/PA7. LCD: PB8/PB3/PB5, transmit only.
 * PB4 is the power latch and PA15 is target power: neither is configured
 * as an SPI pin. PA8/PA9/PA10 are never accessed here.
 * Flash is accessed by USB IRQ callbacks, so a task mutex is insufficient.
 * Callers must keep transactions short and never delay while selected.
 */
#define FLASH_CS GPIO_PIN_4
#define FLASH_OUTPUTS (GPIO_PIN_5 | GPIO_PIN_7)
#define LCD_CS GPIO_PIN_8
#define LCD_OUTPUTS (GPIO_PIN_3 | GPIO_PIN_5)
#define SPI_POLL_LIMIT 10000U

static uint8_t bus_ready;
static uint8_t bus_fault;
static uint8_t bus_lcd;
static uint32_t saved_primask;

static uint8_t wait_flag(uint32_t flag, uint8_t set)
{
    uint32_t remaining = SPI_POLL_LIMIT;
    if (bus_fault) return 0;
    while (((SPI_STAT(SPI0) & flag) != 0U) != (set != 0U)) {
        if (--remaining == 0U) {
            bus_fault = 1;
            spi_disable(SPI0);
            return 0;
        }
    }
    return 1;
}

void SPI0_BusWait(void)
{
    if (wait_flag(SPI_STAT_TBE, 1)) wait_flag(SPI_STAT_TRANS, 0);
}

static void configure(uint8_t lcd)
{
    uint32_t afio;
    volatile uint32_t discard;

    /* CS must already be high and the preceding byte complete. */
    spi_disable(SPI0);
    discard = SPI_DATA(SPI0);
    discard = SPI_STAT(SPI0); /* Drain RX and clear any stale overrun. */
    (void)discard;
    GPIO_BC(GPIOA) = FLASH_OUTPUTS;
    GPIO_BC(GPIOB) = LCD_OUTPUTS;
    gpio_init(GPIOA, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, FLASH_OUTPUTS);
    gpio_init(GPIOB, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, LCD_OUTPUTS);

    /* Explicitly preserve SWD-only mode: SWJ_CFG readback is not relied on. */
    afio = AFIO_PCF0 & ~(AFIO_PCF0_SPI0_REMAP | AFIO_PCF0_SWJ_CFG);
    afio |= (2U << 24); /* JTAG disabled; local PA13/PA14 SWD retained. */
    if (lcd) afio |= AFIO_PCF0_SPI0_REMAP;
    AFIO_PCF0 = afio;

    SPI_CTL1(SPI0) = 0;
    SPI_I2SCTL(SPI0) = 0;
    if (lcd) {
        gpio_init(GPIOB, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, LCD_OUTPUTS);
        SPI_CTL0(SPI0) = SPI_MASTER | SPI_NSS_SOFT | SPI_PSC_16 |
                         SPI_TRANSMODE_BDTRANSMIT;
    } else {
        gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, FLASH_OUTPUTS);
        SPI_CTL0(SPI0) = SPI_MASTER | SPI_NSS_SOFT | SPI_PSC_4 |
                         SPI_TRANSMODE_FULLDUPLEX;
    }
    spi_enable(SPI0);
    bus_lcd = lcd;
}

void SPI0_BusInit(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (!bus_ready) {
        rcu_periph_clock_enable(RCU_GPIOA);
        rcu_periph_clock_enable(RCU_GPIOB);
        rcu_periph_clock_enable(RCU_AF);
        rcu_periph_clock_enable(RCU_SPI0);
        GPIO_BOP(GPIOA) = FLASH_CS;
        GPIO_BOP(GPIOB) = LCD_CS;
        gpio_init(GPIOA, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, FLASH_CS);
        gpio_init(GPIOB, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, LCD_CS);
        gpio_init(GPIOA, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, GPIO_PIN_6);
        spi_i2s_deinit(SPI0);
        configure(0);
        bus_ready = 1;
    }
    __set_PRIMASK(primask);
}

static void select_device(uint8_t lcd)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    SPI0_BusInit();
    saved_primask = primask;
    GPIO_BOP(GPIOA) = FLASH_CS;
    GPIO_BOP(GPIOB) = LCD_CS;
    if (bus_fault || bus_lcd != lcd) {
        configure(lcd);
        bus_fault = 0;
    }
    if (lcd) GPIO_BC(GPIOB) = LCD_CS;
    else GPIO_BC(GPIOA) = FLASH_CS;
}

static void release_device(void)
{
    SPI0_BusWait();
    GPIO_BOP(GPIOA) = FLASH_CS;
    GPIO_BOP(GPIOB) = LCD_CS;
    /* Return to Flash before USB or another task can access storage. */
    if (bus_lcd || bus_fault) configure(0);
    __set_PRIMASK(saved_primask);
}

void SPI0_FlashSelect(void) { select_device(0); }
void SPI0_FlashRelease(void) { release_device(); }
void SPI0_LCDSelect(void) { select_device(1); }
void SPI0_LCDRelease(void) { release_device(); }

uint8_t SPI0_FlashExchange(uint8_t data)
{
    if (!wait_flag(SPI_STAT_TBE, 1)) return 0xFF;
    SPI_DATA(SPI0) = data;
    if (!wait_flag(SPI_STAT_RBNE, 1)) return 0xFF;
    return (uint8_t)SPI_DATA(SPI0);
}

void SPI0_LCDWrite(uint8_t data)
{
    if (wait_flag(SPI_STAT_TBE, 1)) SPI_DATA(SPI0) = data;
}

uint8_t SPI0_BusOK(void) { return (uint8_t)!bus_fault; }
