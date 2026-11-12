// ============================================================
// Flash + OPT 一体化烧录算法 (扩展格式)
// 芯片: MM32G0001 16KB Flash
// 主Flash: 16KB @ 0x08000000, 页大小 1024B
// OPT:     16B @ 0x1FFFF800 (ProgramOpt 无)
// 算法加载: RAM 0x20000000, 最低需 2048B RAM
// 源文件: D:\电机\DAPlink\_Template-opt\FlashPrg.c
// ============================================================
// 结构定义 (program_target_ext_t):
//   [ 0- 4] 标准函数入口 (Init/UnInit/EraseChip/EraseSector/ProgramPage)
//   [ 5- 7] program_syscall_t (breakpoint/static_base/stack_pointer)
//   [ 8-10] 运行时内存布局 (program_buffer/algo_start/program_buffer_size)
//   [11-13] 主Flash参数 (flash_addr/flash_size/algo_size)
//   [14-16] OPT扩展参数 (opt_program/opt_addr/opt_size)  ← 扩展字段
//   [17+  ] algo_blob 算法机器码 (548B = 137 words)
// ============================================================
//
//  内存布局:
//  0x20000000 +-----------+  <- algo_start
//            | algo code |  548B = 32B头 + 512B PrgCode + 4B PrgData
//  0x20000224 +-----------+  <- 代码结束
//            |    gap    |  476B 空闲 (栈向下增长)
//  0x20000400 +===========+  <- program_buffer = stack_pointer
//            |  buffer   |  1024B  页数据缓冲区
//  0x20000800 +-----------+  <- buffer 结束
//
const program_target_ext_t flash_algo = {
    0x2000002D,  // [ 0] Init          - 初始化: 解锁Flash, 清错误标志, 使能外设时钟
    0x2000005F,  // [ 1] UnInit        - 反初始化: 恢复Flash默认状态, 锁定
    0x20000153,  // [ 2] EraseChip     - 全片擦除 16KB
    0x20000161,  // [ 3] EraseSector   - 扇区擦除
    0x200001A7,  // [ 4] ProgramPage   - 主Flash页编程 (1024B/页)
    // ==================== [5-7] program_syscall_t ====================
    0x20000001,  // [ 5] breakpoint    = algo_start | 1 (Thumb模式入口)
    0x20000200,  // [ 6] static_base   = algo_start + PrgData.offset(0x200)  PrgData=4B
    0x20000400,  // [ 7] stack_pointer = program_buffer (栈向下增长到gap)
    // ==================== [8-10] 运行时内存布局 (最低需 2048B RAM) ====================
    0x20000400,  // [ 8] program_buffer       - 页数据缓冲区
    0x20000000,  // [ 9] algo_start           - 算法加载基址
    0x00000400,  // [10] program_buffer_size   - 缓冲区 = Flash 页大小
    // ==================== [11-13] Flash 参数 ====================
    0x08000000,  // [11] flash_addr  - 主Flash基址 (MM32G0001 16KB Flash)
    0x00004000,  // [12] flash_size  - 主Flash总大小 = 16KB
    0x00000224,  // [13] algo_size   - 算法大小 = 548B (137 words)
    // ==================== OPT 扩展字段 [14-16] (⚠ FLM不含ProgramOpt, 用ProgramPage填充) ====================
    0x200001A7,  // [14] opt_program - (无ProgramOpt, 回退到ProgramPage)
    0x1FFFF800,  // [15] opt_addr    - OPT 基址
    0x00000010,  // [16] opt_size    - OPT 大小 = 16B
    // ==================== 算法机器码 (548B = 137 words) ====================
    0xE00ABE00, 0x062D780D, 0x24084068, 0xD3000040, 0x1E644058, 0x1C49D1FA, 0x2A001E52, 0x4770D1F2, // 32B CMSIS算法头 (BKPT序列)
    0x49764877, 0x49776041, 0x47706041, 0x4A734874, 0x49746042, 0x60826041, 0x21006081, 0x68C16001, // PrgCode 开始
    0x43112214, 0x69C060C1, 0xD4060740, 0x496E486F, 0x21066001, 0x496E6041, 0x20006081, 0x48684770,
    0x22806901, 0x61014311, 0x15826901, 0x61014391, 0x47702000, 0x2101B530, 0x06C94861, 0x68C16141,
    0x43212414, 0x690160C1, 0x43292504, 0x69016101, 0x43112240, 0x495F6101, 0xE0004A5C, 0x68C36011,
    0xD1FB07DB, 0x43A96901, 0x68C16101, 0xD0044221, 0x432168C1, 0x200160C1, 0x2000BD30, 0xB530BD30,
    0x68CA494F, 0x431A2314, 0x4A4C60CA, 0x4A4D608A, 0x6148608A, 0x24206908, 0x61084320, 0x22406908,
    0x61084310, 0x4A49484B, 0x6010E000, 0x07ED68CD, 0x6908D1FB, 0x610843A0, 0x401868C8, 0x68C8D003,
    0x60C84318, 0xBD302001, 0xF7FFB530, 0x4D42FF89, 0xF7FF4628, 0x493AFFD4, 0x158A6908, 0x61084310,
    0x24106908, 0x61084320, 0x8028483C, 0x4A374839, 0x6010E000, 0x07DB68CB, 0x6908D1FB, 0x610843A0,
    0x221468C8, 0xD0034010, 0x431068C8, 0x200160C8, 0xB500BD30, 0xFFD8F7FF, 0xFF8CF7FF, 0xBD002000,
    0x4927B530, 0x231468CA, 0x60CA431A, 0x2402690A, 0x610A4322, 0x69086148, 0x43102240, 0x48256108,
    0xE0004A22, 0x68CD6010, 0xD1FB07ED, 0x43A06908, 0x68C86108, 0xD0034018, 0x431868C8, 0x200160C8,
    0x2001BD30, 0xB5F04770, 0x1C494D15, 0x68EB0849, 0x24040049, 0x60EB4323, 0x4C162714, 0x692BE01A,
    0x43332601, 0x8813612B, 0x4B108003, 0x601CE000, 0x07F668EE, 0x692BD1FB, 0x005B085B, 0x68EB612B,
    0xD004423B, 0x433868E8, 0x200160E8, 0x1C80BDF0, 0x1C921E89, 0xD1E22900, 0xBDF02000, 0x45670123,
    0x40022000, 0xCDEF89AB, 0x00005555, 0x40003000, 0x00000FFF, 0x0000AAAA, 0x1FFFF800, 0x00005AA5,
    0x00000000,
};
