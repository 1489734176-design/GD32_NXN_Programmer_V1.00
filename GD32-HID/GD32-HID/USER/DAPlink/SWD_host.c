/**
 * @file    SWD_host.c
 * @brief   SWD主机驱动：通过DAP访问目标芯片的调试口（DP/AP寄存器、内存读写、内核控制、Flash算法调用）
 */

#include "swd_host.h"
#include "DAP_config.h"
#include "DAP.h"
#include "debug_cm.h"

extern uint32_t Flash_Page_Size;

#define NVIC_Addr (0xe000e000)
#define DBG_Addr (0xe000edf0)

/* 记录swd_write_debug_state()失败在哪一步，用于屏幕上的故障诊断 */
uint32_t g_diag_step = 0xFFFF;

// AP的CSW寄存器基值：保留位+主机调试+高保护+调试状态+地址自增
#define CSW_VALUE (CSW_RESERVED | CSW_MSTRDBG | CSW_HPROT | CSW_DBGSTAT | CSW_SADDRINC)

// SWD寄存器访问标志位
#define SWD_REG_AP (1)          // 1=访问AP，0=访问DP
#define SWD_REG_DP (0)
#define SWD_REG_R (1 << 1)      // 1=读，0=写
#define SWD_REG_W (0 << 1)
#define SWD_REG_ADR(a) (a & 0x0c)   // 取地址低2位作为寄存器选择

#define DCRDR 0xE000EDF8
#define DCRSR 0xE000EDF4
#define DHCSR 0xE000EDF0
#define REGWnR (1 << 16)

#define MAX_SWD_RETRY 10            // SWD传输返回WAIT时的最大重试次数
#define MAX_TIMEOUT 1000000         // 目标端系统调用的超时轮询次数

/* DAP缓存状态：记住当前SELECT和CSW的值，相同则跳过重复写入 */
typedef struct
{
	uint32_t select;
	uint32_t csw;
} DAP_STATE;

/* 调试状态：目标内核的寄存器现场，用于执行Flash算法系统调用 */
typedef struct
{
	uint32_t r[16];     // R0~R15
	uint32_t xpsr;      // 程序状态寄存器
} DEBUG_STATE;

static DAP_STATE dap_states[2] = {{0xffffffffU, 0xffffffffU}, {0xffffffffU, 0xffffffffU}};
#define dap_state (dap_states[dap_channel])

static uint8_t swd_read_core_register(uint32_t n, uint32_t *val);
static uint8_t swd_write_core_register(uint32_t n, uint32_t val);

/* 毫秒级忙等延时：按SystemCoreClock空转__NOP，不占用定时器。
 * ms   - 延时长度，单位毫秒
 * 返回 - 无 */
void delaymS(uint32_t ms)
{
	uint32_t cnt = SystemCoreClock / 4 / 1000 * ms;

	for (uint32_t i = 0; i < cnt; i++)
		__NOP();
}

/* 把32位数据按小端顺序拆成字节数组，供SWD传输使用。
 * res  - 输出，字节数组，长度至少len
 * data - 待拆分的32位数据
 * len  - 拆出的字节数，1~4
 * 返回 - 无 */
static void int2array(uint8_t *res, uint32_t data, uint8_t len)
{
	uint8_t i = 0;

	for (i = 0; i < len; i++)
	{
		res[i] = (data >> 8 * i) & 0xFF;
	}
}

/* 带重试的SWD传输：目标返回WAIT时重试，最多MAX_SWD_RETRY次。
 * req  - 请求字节（AP/DP选择、读写方向、寄存器地址）
 * data - 输入输出，写入时为待发数据，读取时为读回数据；可为NULL表示只发不收
 * 返回 - DAP传输应答码：0x01=OK，0x02=WAIT，0x04=FAULT，0x07=NO_ACK */
static uint8_t last_ack[2];
uint8_t swd_last_ack(void) { return last_ack[dap_channel]; }

static uint8_t swd_transfer_retry(uint32_t req, uint32_t *data)
{
	uint8_t i, ack;

	for (i = 0; i < MAX_SWD_RETRY; i++)
	{
		ack = SWD_Transfer(req, data);

		if (ack != DAP_TRANSFER_WAIT) break;
	}

	last_ack[dap_channel] = ack;
	return ack;
}

/* Save the bus setup overhead on a block's data words, not while polling a
 * control register or retrying after WAIT. The first attempt counts as one. */
static uint8_t swd_block_data_retry(uint32_t req, uint32_t *data)
{
    uint8_t i, ack = SWD_TransferBlockData(req, data);
    for(i = 1; ack == DAP_TRANSFER_WAIT && i < MAX_SWD_RETRY; i++)
        ack = SWD_Transfer(req, data);
    last_ack[dap_channel] = ack;
    return ack;
}

/* 初始化SWD主机：复位DAP内部状态并把SWDIO/SWCLK引脚切到调试功能。
 * 返回 - 固定返回1 */
uint8_t swd_init(void)
{
	DAP_Setup();
	PORT_SWD_SETUP();

	return 1;
}

/* 释放SWD引脚：把SWDIO/SWCLK交还给目标板，避免影响目标运行。
 * 返回 - 固定返回1 */
uint8_t swd_off(void)
{
	PORT_OFF();

	return 1;
}

/* 读DP（调试端口）寄存器。
 * adr  - DP寄存器地址：DP_IDCODE / DP_ABORT / DP_CTRL_STAT / DP_SELECT / DP_RDBUFF
 * val  - 输出，读回的32位寄存器值
 * 返回 - 1=成功，0=传输失败 */
uint8_t swd_read_dp(uint8_t adr, uint32_t *val)
{
	uint32_t tmp_in;
	uint8_t tmp_out[4];
	uint8_t ack;
	uint32_t tmp;

	tmp_in = SWD_REG_DP | SWD_REG_R | SWD_REG_ADR(adr);
	ack = swd_transfer_retry(tmp_in, (uint32_t *)tmp_out);

	*val = 0;
	tmp = tmp_out[3];
	*val |= (tmp << 24);
	tmp = tmp_out[2];
	*val |= (tmp << 16);
	tmp = tmp_out[1];
	*val |= (tmp << 8);
	tmp = tmp_out[0];
	*val |= (tmp << 0);

	return (ack == 0x01);
}

/* 写DP（调试端口）寄存器。写DP_SELECT时会与缓存比较，值没变则直接返回。
 * adr  - DP寄存器地址：DP_ABORT / DP_CTRL_STAT / DP_SELECT
 * val  - 待写入的32位值
 * 返回 - 1=成功，0=传输失败 */
uint8_t swd_write_dp(uint8_t adr, uint32_t val)
{
	uint32_t req;
	uint8_t data[4];
	uint8_t ack;

	switch (adr)
	{
	case DP_SELECT:
		if (dap_state.select == val)
		{
			return 1;
		}

		break;

	default:
		break;
	}

	req = SWD_REG_DP | SWD_REG_W | SWD_REG_ADR(adr);
	int2array(data, val, 4);
	ack = swd_transfer_retry(req, (uint32_t *)data);
	if (adr == DP_SELECT) dap_state.select = (ack == 0x01) ? val : 0xffffffffU;

	return (ack == 0x01);
}

/* 读AP（访问端口）寄存器。先写DP_SELECT选中对应的AP和寄存器组，
 * 再发一次空读丢掉流水线里的旧值，第二次读才是目标寄存器的内容。
 * adr  - AP寄存器地址，高8位为AP编号(APSEL)，低8位含寄存器组(BANKSEL)与偏移
 * val  - 输出，读回的32位寄存器值
 * 返回 - 1=成功，0=传输失败 */
uint8_t swd_read_ap(uint32_t adr, uint32_t *val)
{
	uint8_t tmp_in, ack;
	uint8_t tmp_out[4];
	uint32_t tmp;
	uint32_t apsel = adr & 0xff000000;
	uint32_t bank_sel = adr & APBANKSEL;

	if (!swd_write_dp(DP_SELECT, apsel | bank_sel))
	{
		return 0;
	}

	tmp_in = SWD_REG_AP | SWD_REG_R | SWD_REG_ADR(adr);
	// 第一次是空读，读回的是上一次的残留值
	swd_transfer_retry(tmp_in, (uint32_t *)tmp_out);
	ack = swd_transfer_retry(tmp_in, (uint32_t *)tmp_out);

	*val = 0;
	tmp = tmp_out[3];
	*val |= (tmp << 24);
	tmp = tmp_out[2];
	*val |= (tmp << 16);
	tmp = tmp_out[1];
	*val |= (tmp << 8);
	tmp = tmp_out[0];
	*val |= (tmp << 0);

	return (ack == 0x01);
}

/* 写AP（访问端口）寄存器。写CSW时会与缓存比较，值没变则直接返回；
 * 写完后读一次DP_RDBUFF，把写操作彻底冲刷到目标端。
 * adr  - AP寄存器地址，高8位为AP编号(APSEL)，低8位含寄存器组(BANKSEL)与偏移
 * val  - 待写入的32位值
 * 返回 - 1=成功，0=传输失败 */
uint8_t swd_write_ap(uint32_t adr, uint32_t val)
{
	uint8_t data[4];
	uint8_t req, ack;
	uint32_t apsel = adr & 0xff000000;
	uint32_t bank_sel = adr & APBANKSEL;

	if (!swd_write_dp(DP_SELECT, apsel | bank_sel))
	{
		return 0;
	}

	switch (adr)
	{
	case AP_CSW:
		if (dap_state.csw == val)
		{
			return 1;
		}

		dap_state.csw = 0xffffffffU; /* Unknown until the posted write completes. */
		break;

	default:
		break;
	}

	req = SWD_REG_AP | SWD_REG_W | SWD_REG_ADR(adr);
	int2array(data, val, 4);

	if (swd_transfer_retry(req, (uint32_t *)data) != 0x01)
	{
		return 0;
	}

	req = SWD_REG_DP | SWD_REG_R | SWD_REG_ADR(DP_RDBUFF);
	ack = swd_transfer_retry(req, NULL);
	if (ack == 0x01 && adr == AP_CSW) dap_state.csw = val;

	return (ack == 0x01);
}

/* 以32位字为单位、靠TAR地址自增连续写目标内存（一次调用最多写到页边界）。
 * address - 目标起始地址，必须4字节对齐
 * data    - 源数据缓冲区
 * size    - 写入字节数，必须是4的倍数，为0时直接返回0
 * 返回    - 1=成功，0=失败 */
static uint8_t swd_write_block(uint32_t address, uint8_t *data, uint32_t size)
{
	uint8_t tmp_in[4], req;
	uint32_t size_in_words;
	uint32_t i, ack;

	if (size == 0)
	{
		return 0;
	}

	size_in_words = size / 4;

	// 设置CSW：32位宽、地址自增
	if (!swd_write_ap(AP_CSW, CSW_VALUE | CSW_SIZE32))
	{
		return 0;
	}

	// 写TAR，设定起始地址
	req = SWD_REG_AP | SWD_REG_W | (1 << 2);
	int2array(tmp_in, address, 4);

	if (swd_transfer_retry(req, (uint32_t *)tmp_in) != 0x01)
	{
		return 0;
	}

	// 写DRW，TAR随每次写入自动累加
	req = SWD_REG_AP | SWD_REG_W | (3 << 2);

	for (i = 0; i < size_in_words; i++)
	{
		if (swd_block_data_retry(req, (uint32_t *)data) != 0x01)
		{
			return 0;
		}

		data += 4;
	}

	// 空读一次，冲刷写缓冲，确认目标端确实接收完毕
	req = SWD_REG_DP | SWD_REG_R | SWD_REG_ADR(DP_RDBUFF);
	ack = swd_transfer_retry(req, NULL);
	return (ack == 0x01);
}

/* 以32位字为单位、靠TAR地址自增连续读目标内存（一次调用最多读到页边界）。
 * address - 目标起始地址，必须4字节对齐
 * data    - 输出，接收数据缓冲区
 * size    - 读取字节数，必须是4的倍数，为0时直接返回0
 * 返回    - 1=成功，0=失败 */
static uint8_t swd_read_block(uint32_t address, uint8_t *data, uint32_t size)
{
	uint8_t tmp_in[4], req, ack;
	uint32_t size_in_words;
	uint32_t i;

	if (size == 0)
	{
		return 0;
	}

	size_in_words = size / 4;

	if (!swd_write_ap(AP_CSW, CSW_VALUE | CSW_SIZE32))
	{
		return 0;
	}

	// 写TAR，设定起始地址
	req = SWD_REG_AP | SWD_REG_W | AP_TAR;
	int2array(tmp_in, address, 4);

	if (swd_transfer_retry(req, (uint32_t *)tmp_in) != DAP_TRANSFER_OK)
	{
		return 0;
	}

	// 读DRW
	req = SWD_REG_AP | SWD_REG_R | AP_DRW;

	// 先发起第一次读，数据要到下一次读才回来
	if (swd_transfer_retry(req, NULL) != 0x01)
	{
		return 0;
	}

	for (i = 0; i < (size_in_words - 1); i++)
	{
		if (swd_block_data_retry(req, (uint32_t *)data) != DAP_TRANSFER_OK)
		{
			return 0;
		}

		data += 4;
	}

	// 读最后一个字
	req = SWD_REG_DP | SWD_REG_R | SWD_REG_ADR(DP_RDBUFF);
	ack = swd_transfer_retry(req, (uint32_t *)data);
	return (ack == 0x01);
}

/* 读目标内存的一个32位字（底层实现，要求CSW已配置为32位宽）。
 * addr - 目标地址
 * val  - 输出，读回的32位数据
 * 返回 - 1=成功，0=失败 */
static uint8_t swd_read_data(uint32_t addr, uint32_t *val)
{
	uint8_t tmp_in[4];
	uint8_t tmp_out[4];
	uint8_t req, ack;
	uint32_t tmp;
	// 把地址写进TAR寄存器
	int2array(tmp_in, addr, 4);
	req = SWD_REG_AP | SWD_REG_W | (1 << 2);

	if (swd_transfer_retry(req, (uint32_t *)tmp_in) != 0x01)
	{
		return 0;
	}

	// 读数据
	req = SWD_REG_AP | SWD_REG_R | (3 << 2);

	if (swd_transfer_retry(req, (uint32_t *)tmp_out) != 0x01)
	{
		return 0;
	}

	// 空读一次，把流水线里的数据取回来
	req = SWD_REG_DP | SWD_REG_R | SWD_REG_ADR(DP_RDBUFF);
	ack = swd_transfer_retry(req, (uint32_t *)tmp_out);
	*val = 0;
	tmp = tmp_out[3];
	*val |= (tmp << 24);
	tmp = tmp_out[2];
	*val |= (tmp << 16);
	tmp = tmp_out[1];
	*val |= (tmp << 8);
	tmp = tmp_out[0];
	*val |= (tmp << 0);
	return (ack == 0x01);
}

/* 写目标内存的一个32位字（底层实现，要求CSW已配置为32位宽）。
 * address - 目标地址
 * data    - 待写入的32位数据
 * 返回    - 1=成功，0=失败 */
static uint8_t swd_write_data(uint32_t address, uint32_t data)
{
	uint8_t tmp_in[4];
	uint8_t req, ack;
	// 把地址写进TAR寄存器
	int2array(tmp_in, address, 4);
	req = SWD_REG_AP | SWD_REG_W | (1 << 2);

	if (swd_transfer_retry(req, (uint32_t *)tmp_in) != 0x01)
	{
		return 0;
	}

	// 写数据
	int2array(tmp_in, data, 4);
	req = SWD_REG_AP | SWD_REG_W | (3 << 2);

	if (swd_transfer_retry(req, (uint32_t *)tmp_in) != 0x01)
	{
		return 0;
	}

	// 空读一次，冲刷写缓冲
	req = SWD_REG_DP | SWD_REG_R | SWD_REG_ADR(DP_RDBUFF);
	ack = swd_transfer_retry(req, NULL);
	return (ack == 0x01) ? 1 : 0;
}

/* 从目标内存读一个32位字：先把CSW设成32位宽，再走底层读流程。
 * addr - 目标地址
 * val  - 输出，读回的32位数据
 * 返回 - 1=成功，0=失败 */
static uint8_t swd_read_word(uint32_t addr, uint32_t *val)
{
	if (!swd_write_ap(AP_CSW, CSW_VALUE | CSW_SIZE32))
	{
		return 0;
	}

	if (!swd_read_data(addr, val))
	{
		return 0;
	}

	return 1;
}

/* 往目标内存写一个32位字：先把CSW设成32位宽，再走底层写流程。
 * addr - 目标地址
 * val  - 待写入的32位数据
 * 返回 - 1=成功，0=失败 */
static uint8_t swd_write_word(uint32_t addr, uint32_t val)
{
	if (!swd_write_ap(AP_CSW, CSW_VALUE | CSW_SIZE32))
	{
		return 0;
	}

	if (!swd_write_data(addr, val))
	{
		return 0;
	}

	return 1;
}

/* 从目标内存读一个8位字节：CSW设成8位宽，从读回的32位字里按地址偏移取出对应字节。
 * addr - 目标地址，无需对齐
 * val  - 输出，读回的字节
 * 返回 - 1=成功，0=失败 */
static uint8_t swd_read_byte(uint32_t addr, uint8_t *val)
{
	uint32_t tmp;

	if (!swd_write_ap(AP_CSW, CSW_VALUE | CSW_SIZE8))
	{
		return 0;
	}

	if (!swd_read_data(addr, &tmp))
	{
		return 0;
	}

	*val = (uint8_t)(tmp >> ((addr & 0x03) << 3));
	return 1;
}

/* 往目标内存写一个8位字节：先按地址偏移把数据挪到32位字里的正确位置，
 * 再把CSW设成8位宽写入（硬件只取有效字节，不会破坏同字的其它字节）。
 * addr - 目标地址，无需对齐
 * val  - 待写入的字节
 * 返回 - 1=成功，0=失败 */
static uint8_t swd_write_byte(uint32_t addr, uint8_t val)
{
	uint32_t tmp;

	if (!swd_write_ap(AP_CSW, CSW_VALUE | CSW_SIZE8))
	{
		return 0;
	}

	tmp = val << ((addr & 0x03) << 3);

	if (!swd_write_data(addr, tmp))
	{
		return 0;
	}

	return 1;
}

/* 读任意起始地址、任意长度的目标内存：未对齐的头部逐字节读，
 * 中间按页内剩余长度成块读，尾部再逐字节补齐。
 * address - 目标起始地址，可以不对齐
 * data    - 输出，接收数据缓冲区
 * size    - 读取字节数
 * 返回    - 1=成功，0=失败 */
uint8_t swd_read_memory(uint32_t address, uint8_t *data, uint32_t size)
{
	uint32_t n;

	// 先逐字节读到4字节对齐
	while ((size > 0) && (address & 0x3))
	{
		if (!swd_read_byte(address, data))
		{
			return 0;
		}

		address++;
		data++;
		size--;
	}

	// 按字对齐成块读
	while (size > 3)
	{
		// 一次最多读到当前页结束，避免TAR自增跨页失效
		n = Flash_Page_Size - (address & (Flash_Page_Size - 1));

		if (size < n)
		{
			n = size & 0xFFFFFFFC; // 只算够一个完整字的长度
		}

		if (!swd_read_block(address, data, n))
		{
			return 0;
		}

		address += n;
		data += n;
		size -= n;
	}

	// 剩余不足一个字的字节逐个读
	while (size > 0)
	{
		if (!swd_read_byte(address, data))
		{
			return 0;
		}

		address++;
		data++;
		size--;
	}

	return 1;
}

/* 写任意起始地址、任意长度的目标内存：未对齐的头部逐字节写，
 * 中间按页内剩余长度成块写，尾部再逐字节补齐。
 * address - 目标起始地址，可以不对齐
 * data    - 源数据缓冲区
 * size    - 写入字节数
 * 返回    - 1=成功，0=失败 */
uint8_t swd_write_memory(uint32_t address, uint8_t *data, uint32_t size)
{
	uint32_t n = 0;

	// 先逐字节写到4字节对齐
	while ((size > 0) && (address & 0x3))
	{
		if (!swd_write_byte(address, *data))
		{
			return 0;
		}

		address++;
		data++;
		size--;
	}

	// 按字对齐成块写
	while (size > 3)
	{
		// 一次最多写到当前页结束，避免TAR自增跨页失效
		n = Flash_Page_Size - (address & (Flash_Page_Size - 1));

		if (size < n)
		{
			n = size & 0xFFFFFFFC; // 只算够一个完整字的长度
		}

		if (!swd_write_block(address, data, n))
		{
			return 0;
		}

		address += n;
		data += n;
		size -= n;
	}

	// 剩余不足一个字的字节逐个写
	while (size > 0)
	{
		if (!swd_write_byte(address, *data))
		{
			return 0;
		}

		address++;
		data++;
		size--;
	}

	return 1;
}

/* 把寄存器现场写进目标内核，随后跳转到算法入口执行（Flash算法系统调用的第一步）。
 * state - 待写入的调试状态：r[0..3]为参数，r[9]为静态基址，r[13]为栈顶，
 *         r[14]为返回地址，r[15]为入口地址，xpsr的T位必须为1
 * 返回  - 1=成功，0=失败（失败步骤记录在g_diag_step） */
static uint8_t swd_write_debug_state(DEBUG_STATE *state)
{
	uint32_t i, status;

	g_diag_step = 0;
	if (!swd_write_dp(DP_SELECT, 0))
	{
		return 0;
	}

	// R0、R1、R2、R3：算法入参
	for (i = 0; i < 4; i++)
	{
		g_diag_step = 1 + i;
		if (!swd_write_core_register(i, state->r[i]))
		{
			return 0;
		}
	}

	// R9：静态基址SB
	g_diag_step = 5;
	if (!swd_write_core_register(9, state->r[9]))
	{
		return 0;
	}

	// R13栈指针、R14返回地址、R15入口地址
	for (i = 13; i < 16; i++)
	{
		g_diag_step = 6;
		if (!swd_write_core_register(i, state->r[i]))
		{
			return 0;
		}
	}

	// xPSR：置T位，保证以Thumb状态启动
	g_diag_step = 7;
	if (!swd_write_core_register(16, state->xpsr))
	{
		return 0;
	}

	g_diag_step = 8;
	if (!swd_write_word(DBG_HCSR, DBGKEY | C_DEBUGEN))
	{
		return 0;
	}

	// 检查CTRL/STAT，确认没有粘滞错误
	g_diag_step = 9;
	if (!swd_read_dp(DP_CTRL_STAT, &status))
	{
		return 0;
	}

	g_diag_step = 10;
	if (status & (STICKYERR | WDATAERR))
	{
		return 0;
	}

	return 1;
}

/* 连接后尽快暂停内核：若目标程序重映射了PA13/PA14或进入低功耗模式，
 * SWD链路会被切断，所以要在任何20ms复位延时之前先把CPU停下来。
 * 返回 - 1=已暂停，0=失败 */
uint8_t swd_halt_core(void)
{
	uint32_t val = 0;

	if (!swd_write_word(DBG_HCSR, DBGKEY | C_DEBUGEN | C_HALT))
	{
		return 0;
	}

	if (!swd_read_word(DBG_HCSR, &val))
	{
		return 0;
	}

	return (val & S_HALT) ? 1 : 0;
}

/* 读目标内核寄存器：先写DCRSR指定寄存器号，等S_REGRDY置位后从DCRDR取数据。
 * n    - 内核寄存器编号：0~15为R0~R15，16为xPSR
 * val  - 输出，读回的寄存器值
 * 返回 - 1=成功，0=失败或超时 */
static uint8_t swd_read_core_register(uint32_t n, uint32_t *val)
{
	int i = 0, timeout = 100;

	if (!swd_write_word(DCRSR, n))
	{
		return 0;
	}

	// 等待S_REGRDY（寄存器读写完成）
	for (i = 0; i < timeout; i++)
	{
		if (!swd_read_word(DHCSR, val))
		{
			return 0;
		}

		if (*val & S_REGRDY)
		{
			break;
		}
	}

	if (i == timeout)
	{
		return 0;
	}

	if (!swd_read_word(DCRDR, val))
	{
		return 0;
	}

	return 1;
}

/* 写目标内核寄存器：先把数据写进DCRDR，再写DCRSR（带REGWnR位）触发传输，
 * 最后等S_REGRDY置位确认写完。
 * n    - 内核寄存器编号：0~15为R0~R15，16为xPSR
 * val  - 待写入的寄存器值
 * 返回 - 1=成功，0=失败或超时 */
static uint8_t swd_write_core_register(uint32_t n, uint32_t val)
{
	int i = 0, timeout = 100;

	if (!swd_write_word(DCRDR, val))
	{
		return 0;
	}

	if (!swd_write_word(DCRSR, n | REGWnR))
	{
		return 0;
	}

	// 等待S_REGRDY（寄存器读写完成）
	for (i = 0; i < timeout; i++)
	{
		if (!swd_read_word(DHCSR, &val))
		{
			return 0;
		}

		if (val & S_REGRDY)
		{
			return 1;
		}
	}

	return 0;
}

/* 轮询DHCSR，等待目标内核进入暂停状态。
 * 返回 - 1=已暂停，0=超时未暂停 */
static uint8_t swd_wait_until_halted(void)
{
	// 等待目标停机
	uint32_t val, i, timeout = MAX_TIMEOUT;

	for (i = 0; i < timeout; i++)
	{
		#if 1
		if (!swd_read_word(DBG_HCSR, &val))
		{
			#if 0
			return 0;
			#endif
		}

		 else if (val & S_HALT)
		{
			return 1;
		}
			#endif
	}

	return 0;
}

uint32_t g_last_syscall_r0;
uint32_t g_diag_ctrl_stat = 0xFFFFFFFF;
uint32_t g_diag_hcsr = 0xFFFFFFFF;

/* 调试状态写入失败后，立刻抓取DP的CTRL/STAT和DHCSR快照，
 * 让调用方能区分是“AP被拒绝”还是“内核没停下来”。
 * 先重新初始化：链路还活着就拿到真实值，否则用哨兵值0xDDDDDDDD表示链路已断。
 * 返回 - 无（结果写入g_diag_ctrl_stat / g_diag_hcsr） */
static void swd_diag_capture(void)
{
	uint32_t v;

	swd_init();
	if (!swd_init_debug())
	{
		g_diag_ctrl_stat = 0xDDDDDDDD;
		g_diag_hcsr = 0xDDDDDDDD;
		return;
	}

	v = 0xFFFFFFFF;
	if (swd_read_dp(DP_CTRL_STAT, &v))
		g_diag_ctrl_stat = v;
	else
		g_diag_ctrl_stat = 0xEEEEEEEE;

	v = 0xFFFFFFFF;
	if (swd_read_word(DBG_HCSR, &v))
		g_diag_hcsr = v;
	else
		g_diag_hcsr = 0xEEEEEEEE;
}

/* 在目标上执行一次Flash算法系统调用：布置好寄存器现场后跳转到算法入口，
 * 等算法跑完读回R0作为返回值（Flash算法约定R0=0表示成功）。
 * sysCallParam - 算法描述表：提供静态基址、栈指针、返回断点和各函数入口
 * entry        - 本次要调用的算法函数入口地址（init/uninit/erase/program/verify）
 * arg1         - 传给算法的第1个参数，对应R0（通常是目标地址）
 * arg2         - 传给算法的第2个参数，对应R1（通常是数据缓冲区地址或长度）
 * arg3         - 传给算法的第3个参数，对应R2（通常是长度）
 * arg4         - 传给算法的第4个参数，对应R3
 * 返回         - 1=算法返回0（成功），0=失败（原因记录在g_last_syscall_r0） */
uint8_t swd_flash_syscall_exec(const program_syscall_t *sysCallParam, uint32_t entry, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
	DEBUG_STATE state = {{0}, 0};
	// 在目标上调用Flash算法函数并等待结果
	state.r[0] = arg1;						   // R0：参数1
	state.r[1] = arg2;						   // R1：参数2
	state.r[2] = arg3;						   // R2：参数3
	state.r[3] = arg4;						   // R3：参数4
	state.r[9] = sysCallParam->static_base;	   // SB：静态基址
	state.r[13] = sysCallParam->stack_pointer; // SP：栈指针
	state.r[14] = sysCallParam->breakpoint;	   // LR：返回断点（算法执行完回到这里）
	state.r[15] = entry;					   // PC：入口地址
	state.xpsr = 0x01000000;				   // xPSR：T=1（Thumb态），ISR=0

	if (!swd_write_debug_state(&state))
	{
		swd_diag_capture();
		g_last_syscall_r0 = 0xDEE1;
		return 0;
	}
#if 1
	if (!swd_wait_until_halted())
	{
		g_last_syscall_r0 = 0xDEE2;
	//	return 0;
	}
#endif
	if (!swd_read_core_register(0, &state.r[0]))
	{
		g_last_syscall_r0 = 0xDEE3;
	//	return 0;
	}

	// Flash算法约定：返回0表示成功
	if (state.r[0] != 0)
	{
		g_last_syscall_r0 = state.r[0];
		return 0;
	}

	return 1;
}
/* Start a flash algorithm syscall without waiting for completion. */
uint8_t swd_flash_syscall_start(const program_syscall_t *sysCallParam, uint32_t entry,
                                uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4)
{
    DEBUG_STATE state = {{0}, 0};
    state.r[0] = arg1;
    state.r[1] = arg2;
    state.r[2] = arg3;
    state.r[3] = arg4;
    state.r[9] = sysCallParam->static_base;
    state.r[13] = sysCallParam->stack_pointer;
    state.r[14] = sysCallParam->breakpoint;
    state.r[15] = entry;
    state.xpsr = 0x01000000;
    if(!swd_write_debug_state(&state))
    {
        swd_diag_capture();
        g_last_syscall_r0 = 0xDEE1;
        return 0;
    }
    return 1;
}

/* Retry only idempotent RAM staging on the same halted core, never a flash command. */
uint8_t swd_can_retry_ram_write(uint8_t ack)
{
    uint32_t h;
    if(ack != DAP_TRANSFER_WAIT && ack != DAP_TRANSFER_FAULT) return 0U;
    if(ack == DAP_TRANSFER_FAULT &&
       !swd_write_dp(DP_ABORT, STKCMPCLR | STKERRCLR | WDERRCLR | ORUNERRCLR)) return 0U;
    dap_state.select = dap_state.csw = 0xffffffffU;
    return swd_read_word(DHCSR, &h) && (h & (C_DEBUGEN | S_HALT | S_RESET_ST)) == (C_DEBUGEN | S_HALT);
}

/* One sample only: another powered socket can use SWD between samples. */
swd_page_state_t swd_page_poll(uint32_t *r0)
{
    uint32_t status;
    if(swd_read_word(DHCSR, &status))
    {
        if(!(status & C_DEBUGEN)) return PAGE_LINK_ERROR;
        if(!(status & S_HALT)) return PAGE_RUNNING;
        if(swd_read_core_register(0, r0)) return PAGE_HALTED;
    }
    return swd_last_ack() == DAP_TRANSFER_WAIT ? PAGE_SWD_WAIT : PAGE_LINK_ERROR;
}

/* Wait for the syscall breakpoint and read R0 (0 = success). */
uint8_t swd_flash_syscall_wait(uint32_t *r0)
{
    uint32_t value = 0;
    if(!swd_wait_until_halted())
    {
        g_last_syscall_r0 = 0xDEE2;
        return 0;
    }
    if(!swd_read_core_register(0, &value))
    {
        g_last_syscall_r0 = 0xDEE3;
        return 0;
    }
    if(value != 0)
    {
        g_last_syscall_r0 = value;
        return 0;
    }
    if(r0) *r0 = value;
    return 1;
}


/* 发送SWD复位序列：先输出至少50个时钟的高电平，让目标端SWD状态机复位。
 * 返回 - 固定返回1 */
static uint8_t swd_reset(void)
{
	uint8_t tmp_in[8];
	uint8_t i = 0;

	for (i = 0; i < 8; i++)
	{
		tmp_in[i] = 0xff;
	}

	SWJ_Sequence(51, tmp_in);
	return 1;
}

/* 发送16位切换序列（JTAG-to-SWD的0xE79E魔数），把目标从JTAG切到SWD。
 * val  - 16位切换码，通常为0xE79E
 * 返回 - 固定返回1 */
static uint8_t swd_switch(uint16_t val)
{
	uint8_t tmp_in[2];
	tmp_in[0] = val & 0xff;
	tmp_in[1] = (val >> 8) & 0xff;
	SWJ_Sequence(16, tmp_in);
	return 1;
}

/* 读目标的IDCODE，用来确认SWD链路已经建立。
 * id   - 输出，读回的32位IDCODE
 * 返回 - 1=成功，0=失败 */
static uint8_t swd_read_idcode(uint32_t *id)
{
	uint8_t tmp_in[1];
	uint8_t tmp_out[4];
	tmp_in[0] = 0x00;
	SWJ_Sequence(8, tmp_in);

	if (swd_read_dp(0, (uint32_t *)tmp_out) != 0x01)
	{
		return 0;
	}

	*id = (tmp_out[3] << 24) | (tmp_out[2] << 16) | (tmp_out[1] << 8) | tmp_out[0];
	return 1;
}
#if SWD_SPI
/* 用SPI时序直发JTAG-to-SWD切换序列（走SWD_SPI硬件通路时使用）。
 * 返回 - 1=切换成功且读到IDCODE，0=失败 */
static uint8_t JTAG2SWD()
{
	uint32_t tmp = 0;
	uint8_t LineRst[7] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	uint8_t swd_switch1[2] = {0x9E, 0xE7};
	uint8_t swd_switch2[2] = {0x6D, 0xB7};

	SPI_TX7(LineRst);
	SPI_TX2(swd_switch1);
	SPI_TX7(LineRst);
	SPI_TX2(swd_switch2);
	SPI_TX7(LineRst);

	if (!swd_read_idcode(&tmp))
	{
		//printf("Read IDCODE Fault 0x%.4X\r\n",tmp);
		return 0;
	}
	//		printf("IDCODE : 0x%.4X\r\n",tmp);
	return 1;
}

#else

/* 用位操作时序把目标从JTAG切换到SWD：复位序列→切换序列→复位序列→读IDCODE。
 * 返回 - 1=切换成功且读到IDCODE，0=失败 */
static uint8_t JTAG2SWD()
{
	uint32_t tmp = 0;

	if (!swd_reset())
	{
		return 0;
	}

	if (!swd_switch(0xE79E))
	{
		return 0;
	}

	if (!swd_reset())
	{
		return 0;
	}

	if (!swd_read_idcode(&tmp))
	{
		return 0;
	}

	return 1;
}
#endif

/* 清DP的粘滞错误标志（选通比较错、写数据错、溢出等），让后续传输能正常进行。
 * 返回 - 1=成功，0=失败 */
uint8_t swd_clear_errors(void)
{
    if (!swd_write_dp(DP_ABORT, STKCMPCLR | STKERRCLR | WDERRCLR | ORUNERRCLR)) {
        return 0;
    }
    return 1;
}

/* 建立调试连接：切到SWD、清错误、给调试端口上电并确认电源应答。
 * 返回 - 1=连接成功，0=失败 */
uint8_t swd_init_debug(void)
{
	uint32_t tmp = 0;
	int i = 0;
	int timeout = 100;
	// 用假值初始化DAP缓存，强制第一次写入真正发到目标
	dap_state.select = 0xffffffff;
	dap_state.csw = 0xffffffff;
	swd_init();
	swd_write_dp(DP_ABORT, DAPABORT);
	// 目标相关的预处理钩子（本工程未使用）
	// 这里可以放真正初始化调试口之前需要做的事情
	//target_before_init_debug();

	if (!JTAG2SWD())
	{
		return 0;
	}

	if (!swd_write_dp(DP_ABORT, STKCMPCLR | STKERRCLR | WDERRCLR | ORUNERRCLR))
	{
		return 0;
	}
	if (!swd_clear_errors())
	{
		return 0;
	}

	// 确保DPBANKSEL选中CTRL/STAT寄存器
	if (!swd_write_dp(DP_SELECT, 0))
	{
		return 0;
	}

	// 请求给调试域和系统域上电
	if (!swd_write_dp(DP_CTRL_STAT, CSYSPWRUPREQ | CDBGPWRUPREQ))
	{
		return 0;
	}

	for (i = 0; i < timeout; i++)
	{
		if (!swd_read_dp(DP_CTRL_STAT, &tmp))
		{
			return 0;
		}

		if ((tmp & (CDBGPWRUPACK | CSYSPWRUPACK)) == (CDBGPWRUPACK | CSYSPWRUPACK))
		{
			// 上电应答齐全就跳出循环
			break;
		}
	}

	if (i == timeout)
	{
		// 调试端口上电失败
		return 0;
	}

	if (!swd_write_dp(DP_CTRL_STAT, CSYSPWRUPREQ | CDBGPWRUPREQ | TRNNORMAL | MASKLANE))
	{
		return 0;
	}

	// 目标相关的解锁钩子：
	// 有些目标会进入锁定状态，可以在这里做解锁序列
	//target_unlock_sequence();

	if (!swd_write_dp(DP_SELECT, 0))
	{
		return 0;
	}

	return 1;
}

/*
__attribute__((weak)) void swd_set_target_reset(uint8_t asserted)
{
    (asserted) ? PIN_nRESET_OUT(0) : PIN_nRESET_OUT(1);
}
*/
/* 控制目标的nRESET引脚；释放复位时额外补发一次软件复位请求。
 * 本文件调用此函数时都是先以 asserted=1 拉低 nRESET，延时后再以 asserted=0 释放，
 * 所以这里只在第二次调用（释放）时补发软件复位。
 * asserted - 1=拉低nRESET（目标保持复位），0=释放nRESET并触发SYSRESETREQ
 * 返回     - 无 */
void swd_set_target_reset(uint8_t asserted)
{
	(asserted) ? PIN_nRESET_OUT(0) : PIN_nRESET_OUT(1);
	if (asserted == 0)
	{
		swd_write_word((uint32_t)&SCB->AIRCR, ((0x5FA << SCB_AIRCR_VECTKEY_Pos) | (SCB->AIRCR & SCB_AIRCR_PRIGROUP_Msk) | SCB_AIRCR_SYSRESETREQ_Msk));
	}
}

/* 用硬件nRESET引脚把目标切到指定状态（复位保持/复位后运行/复位后编程/调试/暂停/运行）。
 * 关键点：进入RESET_PROGRAM时先使能调试并立刻暂停内核，否则目标程序会在
 * 下面的20ms复位延时里重映射PA13/PA14或进入低功耗，把SWD链路切断。
 * state - 目标状态：RESET_HOLD / RESET_PROGRAM / RESET_RUN / NO_DEBUG / DEBUG / HALT / RUN
 * 返回  - 1=成功，0=失败（失败步骤记录在g_diag_step） */
uint8_t swd_set_target_state_hw(TARGET_RESET_STATE state)
{
	uint32_t val;
	int8_t ap_retries = 2;

	/* 进入RUN状态之前不能调用swd_init，否则后续操作会失败。 */
	if (state != RUN)
	{
		swd_init();
	}

	switch (state)
	{
	case RESET_HOLD:
		swd_set_target_reset(1);
		break;

	case RESET_RUN:
		swd_set_target_reset(1);
		delaymS(20);
		swd_set_target_reset(0);
		delaymS(20);
		swd_off();
		break;

	case RESET_PROGRAM:
		g_diag_step = 0x20;
		if (!swd_init_debug())
		{
			return 0;
		}

		// 立刻使能调试并暂停内核：如果只使能调试，目标程序会在下面20ms的
		// 复位延时期间继续运行，可能重映射PA13/PA14或进入低功耗，切断SWD链路。
		g_diag_step = 0x21;
		while (swd_write_word(DBG_HCSR, DBGKEY | C_DEBUGEN | C_HALT) == 0)
		{
			if (--ap_retries <= 0)
				return 0;

			// 目标可能处于异常状态，硬复位再来一次
			swd_set_target_reset(1);
			delaymS(20);
			swd_set_target_reset(0);
			delaymS(20);
		}

		// 使能复位时暂停（VC_CORERESET）
		g_diag_step = 0x22;
		if (!swd_write_word(DBG_EMCR, VC_CORERESET))
		{
			return 0;
		}

		// 再次复位，让内核停在上电起点
		swd_set_target_reset(1);
		delaymS(20);
		swd_set_target_reset(0);
		delaymS(20);

		// 等待内核暂停，但不能死等：链路已断或目标反复复位时必须能退出去报错
		g_diag_step = 0x23;
		{
			int halt_wait = 200;
			for (;;)
			{
				if (!swd_read_word(DBG_HCSR, &val))
				{
					return 0;
				}
				if (val & S_HALT)
				{
					break;
				}
				if (--halt_wait <= 0)
				{
					g_diag_step = 0x25;
					return 0;
				}
				delaymS(1);
			}
		}

		// 关闭复位时暂停
		g_diag_step = 0x24;
		if (!swd_write_word(DBG_EMCR, 0))
		{
			return 0;
		}

		break;

	case NO_DEBUG:
		if (!swd_write_word(DBG_HCSR, DBGKEY))
		{
			return 0;
		}

		break;

	case DEBUG:
		if (!JTAG2SWD())
		{
			return 0;
		}

		if (!swd_write_dp(DP_ABORT, STKCMPCLR | STKERRCLR | WDERRCLR | ORUNERRCLR))
		{
			return 0;
		}

		// 确保DPBANKSEL选中CTRL/STAT寄存器
		if (!swd_write_dp(DP_SELECT, 0))
		{
			return 0;
		}

		// 请求给调试域和系统域上电
		if (!swd_write_dp(DP_CTRL_STAT, CSYSPWRUPREQ | CDBGPWRUPREQ))
		{
			return 0;
		}

		// 使能调试
		if (!swd_write_word(DBG_HCSR, DBGKEY | C_DEBUGEN))
		{
			return 0;
		}

		break;

	case HALT:
		if (!swd_init_debug())
		{
			return 0;
		}

		// 使能调试并暂停内核（DHCSR <- 0xA05F0003）
		if (!swd_write_word(DBG_HCSR, DBGKEY | C_DEBUGEN | C_HALT))
		{
			return 0;
		}

		// 等待内核暂停
		do
		{
			if (!swd_read_word(DBG_HCSR, &val))
			{
				return 0;
			}
		} while ((val & S_HALT) == 0);

		break;

	case RUN:
		if (!swd_write_word(DBG_HCSR, DBGKEY))
		{
			return 0;
		}

		swd_off();

	default:
		return 0;
	}

	return 1;
}

/* 用软件方式（写AIRCR的SYSRESETREQ）把目标切到指定状态，不需要nRESET引脚。
 * 与硬件版本的差别：复位由内核自己发起，省掉外部复位线的时序依赖。
 * state - 目标状态：RESET_HOLD / RESET_PROGRAM / RESET_RUN / NO_DEBUG / DEBUG / HALT / RUN
 * 返回  - 1=成功，0=失败 */
uint8_t swd_set_target_state_sw(TARGET_RESET_STATE state)
{
	uint32_t val;

	/* 进入RUN状态之前不能调用swd_init，否则后续操作会失败。 */
	if (state != RUN)
	{
		swd_init(); //不重新初始化引脚的话，无法正常进入复位流程
	}

	switch (state)
	{
	case RESET_HOLD:
		swd_set_target_reset(1);
		break;

	case RESET_RUN:
		swd_set_target_reset(1);
		delaymS(20);
		swd_set_target_reset(0);
		delaymS(20);
		swd_off();
		break;

	case RESET_PROGRAM:
		if (!swd_init_debug())
		{
			return 0;
		}

		// 使能调试并暂停内核（DHCSR <- 0xA05F0003）
		if (!swd_write_word(DBG_HCSR, DBGKEY | C_DEBUGEN | C_HALT))
		{
			return 0;
		}

		// 等待内核暂停
		do
		{
			if (!swd_read_word(DBG_HCSR, &val))
			{
				return 0;
			}
		} while ((val & S_HALT) == 0);

		// 使能复位时暂停（VC_CORERESET）
		if (!swd_write_word(DBG_EMCR, VC_CORERESET))
		{
			return 0;
		}

		// 发起一次软复位
		if (!swd_read_word(NVIC_AIRCR, &val))
		{
			return 0;
		}

		if (!swd_write_word(NVIC_AIRCR, VECTKEY | (val & SCB_AIRCR_PRIGROUP_Msk) | SYSRESETREQ))
		{
			return 0;
		}

		delaymS(20);

		do
		{
			if (!swd_read_word(DBG_HCSR, &val))
			{
				return 0;
			}
		} while ((val & S_HALT) == 0);

		// 关闭复位时暂停
		if (!swd_write_word(DBG_EMCR, 0))
		{
			return 0;
		}

		break;

	case NO_DEBUG:
		if (!swd_write_word(DBG_HCSR, DBGKEY))
		{
			return 0;
		}

		break;

	case DEBUG:
		if (!JTAG2SWD())
		{
			return 0;
		}

		if (!swd_write_dp(DP_ABORT, STKCMPCLR | STKERRCLR | WDERRCLR | ORUNERRCLR))
		{
			return 0;
		}

		// 确保DPBANKSEL选中CTRL/STAT寄存器
		if (!swd_write_dp(DP_SELECT, 0))
		{
			return 0;
		}

		// 请求给调试域和系统域上电
		if (!swd_write_dp(DP_CTRL_STAT, CSYSPWRUPREQ | CDBGPWRUPREQ))
		{
			return 0;
		}

		// 使能调试
		if (!swd_write_word(DBG_HCSR, DBGKEY | C_DEBUGEN))
		{
			return 0;
		}

		break;

	case HALT:
		if (!swd_init_debug())
		{
			return 0;
		}

		// 使能调试并暂停内核（DHCSR <- 0xA05F0003）
		if (!swd_write_word(DBG_HCSR, DBGKEY | C_DEBUGEN | C_HALT))
		{
			return 0;
		}

		// 等待内核暂停
		do
		{
			if (!swd_read_word(DBG_HCSR, &val))
			{
				return 0;
			}
		} while ((val & S_HALT) == 0);

		break;

	case RUN:
		if (!swd_write_word(DBG_HCSR, DBGKEY))
		{
			return 0;
		}

		swd_off();

	default:
		return 0;
	}

	return 1;
}
