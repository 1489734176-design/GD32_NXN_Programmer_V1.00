// ============================================================
// Flash + OPT 一体化烧录算法 (扩展格式)
// 芯片: MM32SPIN0230 32kB Flash
// 主Flash: 32KB @ 0x08000000, 页大小 1024B
// OPT:     16B @ 0x1FFFF800 (ProgramOpt=0x213)
// 算法加载: RAM 0x20000000, 最低需 2048B RAM
// 源文件: D:\电机\DAPlink\_Template-opt\FlashPrg.c
// ============================================================
// 结构定义 (program_target_ext_t):
//   [ 0- 4] 标准函数入口 (Init/UnInit/EraseChip/EraseSector/ProgramPage)
//   [ 5- 7] program_syscall_t (breakpoint/static_base/stack_pointer)
//   [ 8-10] 运行时内存布局 (program_buffer/algo_start/program_buffer_size)
//   [11-13] 主Flash参数 (flash_addr/flash_size/algo_size)
//   [14-16] OPT扩展参数 (opt_program/opt_addr/opt_size)  ← 扩展字段
//   [17+  ] algo_blob 算法机器码 (700B = 175 words)
// ============================================================
//
//  内存布局:
//  0x20000000 +-----------+  <- algo_start
//            | algo code |  700B = 32B头 + 664B PrgCode + 4B PrgData
//  0x200002BC +-----------+  <- 代码结束
//            |    gap    |  324B 空闲 (栈向下增长)
//  0x20000400 +===========+  <- program_buffer = stack_pointer
//            |  buffer   |  1024B  页数据缓冲区
//  0x20000800 +-----------+  <- buffer 结束
//
const program_target_ext_t flash_algo = {
    0x200000EF,  // [ 0] Init          - 初始化: 解锁Flash, 清错误标志, 使能外设时钟
    0x20000127,  // [ 1] UnInit        - 反初始化: 恢复Flash默认状态, 锁定
    0x20000145,  // [ 2] EraseChip     - 全片擦除 32KB
    0x20000193,  // [ 3] EraseSector   - 扇区擦除
    0x200001DD,  // [ 4] ProgramPage   - 主Flash页编程 (1024B/页)
    // ==================== [5-7] program_syscall_t ====================
    0x20000001,  // [ 5] breakpoint    = algo_start | 1 (Thumb模式入口)
    0x20000298,  // [ 6] static_base   = algo_start + PrgData.offset(0x298)  PrgData=4B
    0x20000400,  // [ 7] stack_pointer = program_buffer (栈向下增长到gap)
    // ==================== [8-10] 运行时内存布局 (最低需 2048B RAM) ====================
    0x20000400,  // [ 8] program_buffer       - 页数据缓冲区
    0x20000000,  // [ 9] algo_start           - 算法加载基址
    0x00000400,  // [10] program_buffer_size   - 缓冲区 = Flash 页大小
    // ==================== [11-13] Flash 参数 ====================
    0x08000000,  // [11] flash_addr  - 主Flash基址 (MM32SPIN0230 32kB Flash)
    0x00008000,  // [12] flash_size  - 主Flash总大小 = 32KB
    0x000002BC,  // [13] algo_size   - 算法大小 = 700B (175 words)
    // ==================== OPT 扩展字段 [14-16] (离线烧录器扩展) ====================
    0x20000233,  // [14] opt_program - ProgramOpt 函数地址 (OPT编程)
    0x1FFFF800,  // [15] opt_addr    - OPT 基址
    0x00000010,  // [16] opt_size    - OPT 大小 = 16B (8个半字)
    // ==================== 算法机器码 (700B = 175 words) ====================
    0xE00ABE00, 0x062D780D, 0x24084068, 0xD3000040, 0x1E644058, 0x1C49D1FA, 0x2A001E52, 0x4770D1F2, // 32B CMSIS算法头 (BKPT序列)
    0x499D489C, 0x489D6048, 0x47706048, 0x499A4899, 0x489A6088, 0x47706088, 0x4899E002, 0x60084999, // PrgCode 开始
    0x68C04895, 0x0FC007C0, 0xD1F62800, 0x48924770, 0x211468C0, 0x28004008, 0x2014D004, 0x60C8498E,
    0x47702001, 0xE7FC2000, 0x4604B510, 0x498A2014, 0xF7FF60C8, 0x4888FFDB, 0x69006144, 0x43082120,
    0x61084985, 0x69004608, 0x43082140, 0x61084982, 0xFFD2F7FF, 0x69004880, 0x43882120, 0x6108497E,
    0xFFD5F7FF, 0xD0012800, 0xBD102001, 0xE7FC2000, 0xF7FFB510, 0x487CFFB5, 0xFFD6F7FF, 0x69004876,
    0x43082110, 0x61084974, 0x49774878, 0xF7FF8008, 0x4871FFB3, 0x21106900, 0x496F4388, 0xF7FF6108,
    0x2800FFB6, 0x2001D001, 0x2000BD10, 0xB570E7FC, 0x460D4604, 0xF7FF4616, 0xF7FFFF93, 0x2000FF97,
    0x60084965, 0x60C82014, 0x69C04608, 0x40080449, 0xD1062800, 0x49634866, 0x20066008, 0x48656048,
    0x20006088, 0x4601BD70, 0x6900485B, 0x43102280, 0x61104A59, 0x69004610, 0x43902206, 0x61104A56,
    0x47702000, 0xF7FFB510, 0xF7FFFFB3, 0x2001FF69, 0x495106C0, 0x20146148, 0x460860C8, 0x21046900,
    0x494D4308, 0x46086108, 0x21406900, 0x494A4308, 0xF7FF6108, 0x4848FF61, 0x21046900, 0x49464388,
    0xF7FF6108, 0x2800FF64, 0x2001D001, 0x2000BD10, 0xB510E7FC, 0x20144604, 0x60C8493F, 0x69004608,
    0x43082102, 0x6108493C, 0x61444608, 0x21406900, 0x49394308, 0xF7FF6108, 0x4837FF3F, 0x21026900,
    0x49354388, 0xF7FF6108, 0x2800FF42, 0x2001D001, 0x2000BD10, 0x4603E7FC, 0x47702001, 0x460DB5F7,
    0x1C684616, 0x006D0845, 0x492B2004, 0x240060C8, 0x4829E01B, 0x21016900, 0x49274308, 0x5D376108,
    0x5C711C61, 0x430F0209, 0x53079800, 0xFF14F7FF, 0x69004821, 0x00400840, 0x6108491F, 0xFF17F7FF,
    0xD0012800, 0xBDFE2001, 0x42AC1CA4, 0x2000D3E1, 0xB5F7E7F9, 0x4616460D, 0x08451C68, 0xF7FF006D,
    0x9800FEEF, 0xFF10F7FF, 0xD0012800, 0xBDFE2001, 0xE01B2400, 0x69004810, 0x43082110, 0x6108490E,
    0x1C615D37, 0x02095C71, 0x9800430F, 0xF7FF5307, 0x4809FEE3, 0x21106900, 0x49074388, 0xF7FF6108,
    0x2800FEE6, 0x2001D001, 0x1CA4E7E1, 0xD3E142AC, 0xE7DC2000, 0x45670123, 0x40022000, 0xCDEF89AB,
    0x0000AAAA, 0x40003000, 0x1FFFF800, 0x00005AA5, 0x00005555, 0x00000FFF, 0x00000000,
};
