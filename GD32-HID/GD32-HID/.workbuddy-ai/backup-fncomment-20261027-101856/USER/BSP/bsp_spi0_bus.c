/* 中文注释补充（2026-10-26）：共享SPI0总线：LCD使用PB3/PB5重映射单向发送，Flash使用PA4～PA7双向收发；切换片选和映射时保护临界区。 */
#include "bsp_spi0_bus.h"

/* SPI0 共享总线移植：Flash 使用 PA4/PA5/PA6/PA7，
 * LCD 使用 PB8/PB3/PB5，采用单线只发送模式，无需占用 PB4(MISO)。
 * PB4 为电源保持引脚，PA15 为目标供电引脚，均不配置为 SPI 引脚；
 * 本文件也不操作 PA8/PA9/PA10。
 * USB 中断回调也会访问 Flash，仅用任务互斥锁无法保护共享外设，
 * 因此每次事务保存并屏蔽中断，结束后恢复；调用方必须分块且不得延时。
 */
#define FLASH_CS GPIO_PIN_4 /* PA4：Flash 片选，低电平有效 */
#define FLASH_OUTPUTS (GPIO_PIN_5 | GPIO_PIN_7) /* PA5 时钟、PA7 数据输出 */
#define LCD_CS GPIO_PIN_8 /* PB8：LCD 片选，低电平有效 */
#define LCD_OUTPUTS (GPIO_PIN_3 | GPIO_PIN_5) /* PB3 时钟、PB5 数据输出 */
#define SPI_POLL_LIMIT 10000U /* 最大轮询次数，防止外设异常时永久等待 */

static uint8_t bus_ready; /* 总线是否已完成首次初始化 */
static uint8_t bus_fault; /* 轮询超时标志，故障时停止继续发送 */
static uint8_t bus_lcd; /* 当前 SPI0 配置：0 为 Flash，1 为 LCD */
static uint32_t saved_primask; /* 本次事务开始前的全局中断屏蔽状态 */

/* 等待指定状态位达到 set：1 为置位，0 为清零。
 * 轮询超时则记录故障并关闭 SPI0；后续等待直接失败，由下次片选恢复。
 */
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

/* 先等发送缓冲区空，再等 SPI0 不忙，确保最后一字节已发送完毕。 */
void SPI0_BusWait(void)
{
    if (wait_flag(SPI_STAT_TBE, 1)) wait_flag(SPI_STAT_TRANS, 0);
}

/* 切换 SPI0 引脚映射与传输模式，lcd 非零选择 LCD，否则选择 Flash。
 * 调用时两路片选均须为高，正常传输已结束；故障时用于重新配置外设。
 */
static void configure(uint8_t lcd)
{
    uint32_t afio;
    volatile uint32_t discard;

    /* 先停用 SPI0，再清理接收状态并切换引脚。 */
    spi_disable(SPI0);
    discard = SPI_DATA(SPI0);
    discard = SPI_STAT(SPI0); /* 依次读数据和状态，清理接收数据及旧溢出标志。 */
    (void)discard;
    /* 两组输出先置低并退回普通 GPIO，避免非选中设备出现额外时钟。 */
    GPIO_BC(GPIOA) = FLASH_OUTPUTS;
    GPIO_BC(GPIOB) = LCD_OUTPUTS;
    gpio_init(GPIOA, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, FLASH_OUTPUTS);
    gpio_init(GPIOB, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, LCD_OUTPUTS);

    /* 显式保持仅 SWD 模式，不依赖 SWJ_CFG 位的读回值；保留其他重映射位。 */
    afio = AFIO_PCF0 & ~(AFIO_PCF0_SPI0_REMAP | AFIO_PCF0_SWJ_CFG);
    afio |= (2U << 24); /* 关闭 JTAG，释放 PB3；保留本机 PA13/PA14 的 SWD。 */
    if (lcd) afio |= AFIO_PCF0_SPI0_REMAP;
    AFIO_PCF0 = afio;

    /* 关闭额外 SPI 功能及 I2S，按当前设备重新设置主机传输参数。 */
    SPI_CTL1(SPI0) = 0;
    SPI_I2SCTL(SPI0) = 0;
    if (lcd) {
        /* LCD：PB3/PB5 复用输出，16 分频、单线发送，不配置 PB4。 */
        gpio_init(GPIOB, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, LCD_OUTPUTS);
        SPI_CTL0(SPI0) = SPI_MASTER | SPI_NSS_SOFT | SPI_PSC_16 |
                         SPI_TRANSMODE_BDTRANSMIT;
    } else {
        /* Flash：PA5/PA7 复用输出，4 分频、全双工；PA6 保持输入。 */
        gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, FLASH_OUTPUTS);
        SPI_CTL0(SPI0) = SPI_MASTER | SPI_NSS_SOFT | SPI_PSC_4 |
                         SPI_TRANSMODE_FULLDUPLEX;
    }
    spi_enable(SPI0);
    bus_lcd = lcd;
}

/* 幂等初始化：首次启用 GPIO/AFIO/SPI0 时钟并默认选用 Flash 配置。
 * 临界区只覆盖硬件配置，返回时恢复进入函数前的中断状态。
 */
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

/* 开始事务：保存中断状态，两路片选先拉高，必要时切换映射或恢复故障。
 * 随后只拉低目标设备片选；须由 release_device 结束，不允许嵌套调用。
 */
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

/* 结束事务：等最后一字节发完，撤销片选，恢复 Flash 配置，再恢复中断。 */
static void release_device(void)
{
    SPI0_BusWait();
    GPIO_BOP(GPIOA) = FLASH_CS;
    GPIO_BOP(GPIOB) = LCD_CS;
    /* 在 USB 中断或其他任务访问存储前，将 SPI0 恢复为 Flash 默认映射。 */
    if (bus_lcd || bus_fault) configure(0);
    __set_PRIMASK(saved_primask);
}

/* 公共片选接口统一复用临界区管理，保持 Flash 与 LCD 的访问规则一致。 */
void SPI0_FlashSelect(void) { select_device(0); }
void SPI0_FlashRelease(void) { release_device(); }
void SPI0_LCDSelect(void) { select_device(1); }
void SPI0_LCDRelease(void) { release_device(); }

/* Flash 全双工收发一个字节；发送或接收等待失败时返回 0xFF。 */
uint8_t SPI0_FlashExchange(uint8_t data)
{
    if (!wait_flag(SPI_STAT_TBE, 1)) return 0xFF;
    SPI_DATA(SPI0) = data;
    if (!wait_flag(SPI_STAT_RBNE, 1)) return 0xFF;
    return (uint8_t)SPI_DATA(SPI0);
}

/* LCD 单线发送一个字节，只等待发送缓冲区可写；
 * 命令/数据切换及事务结束时由 SPI0_BusWait 确认线上发送完成。
 */
void SPI0_LCDWrite(uint8_t data)
{
    if (wait_flag(SPI_STAT_TBE, 1)) SPI_DATA(SPI0) = data;
}

/* 查询当前故障标志，供上层分块绘制及时停止输出。 */
uint8_t SPI0_BusOK(void) { return (uint8_t)!bus_fault; }
