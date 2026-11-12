#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Update MM32G0001_16.c with detailed Chinese comments."""

import sys
import os

# Use utf-8-sig or gbk depending on what the C2B tool expects
# The file currently has ASCII only, so both work
ENCODING = 'utf-8'

filepath = r'D:\电机\烧录算法转换工具\烧录算法转换工具\MM32G0001_16.c'

with open(filepath, 'rb') as f:
    content = f.read()

# The exact old header text to find and replace
old_header = (
    '\tconst program_target_t flash_algo = {\n'
    '\t    0x2000002D,  // Init\n'
    '\t    0x2000005F,  // UnInit\n'
    '\t    0x20000153,  // EraseChip\n'
    '\t    0x20000161,  // EraseSector\n'
    '\t    0x200001A7,  // ProgramPage\n'
    '\t    0x20000001,\n'
    '\t    0x20000224,\n'
    '\t    0x20000400,\n'
    '\t    0x20000400,  // mem buffer location\n'
    '\t    0x20000000,  // location to write prog_blob in target RAM\n'
    '\t    0x00000400,  // ram_to_flash_bytes_to_be_written\n'
    '\t    0x08000000,  // chip flash addr\n'
    '\t    0x00004000,  // chip flash size\n'
    '\t    0x00000224,  // prog_blob size'
)

new_header = (
    '\tconst program_target_t flash_algo = {\n'
    '\t    // ==================== 烧录算法函数入口 (FLM符号表提取, 地址 = algo_start + 0x20 + 原始符号偏移) ====================\n'
    '\t    0x2000002D,  // [ 0] Init          - 初始化Flash控制器: 解锁Flash, 清除错误标志, 使能外设时钟(RCC@0x40003000)\n'
    '\t    0x2000005F,  // [ 1] UnInit        - 反初始化: 恢复Flash控制器到上电默认状态(禁用PG/PER, 锁定Flash)\n'
    '\t    0x20000153,  // [ 2] EraseChip     - 全片擦除: 先擦除主Flash所有页(EraseAllPages), 再擦除OptionBlock\n'
    '\t    0x20000161,  // [ 3] EraseSector   - 扇区擦除: 按指定地址擦除单个扇区(1KB), 参数: r0=flash_addr\n'
    '\t    0x200001A7,  // [ 4] ProgramPage   - 页编程: 半字(16bit)循环写入, 参数: r0=flash_addr r1=byte_count r2=buf_ptr\n'
    '\t    // ==================== 系统调用参数 (program_syscall_t, 离线烧录器使用) ====================\n'
    '\t    0x20000001,  // [ 5] breakpoint    - 断点地址 = algo_start | 1 (Thumb模式入口, 烧录完成后PC跳转目标)\n'
    '\t    0x20000224,  // [ 6] static_base   - 静态RW/ZI数据基址 (位于algo代码和buffer之间gap区, 本算法无RW数据)\n'
    '\t    0x20000400,  // [ 7] stack_pointer - 栈指针MSP (必须指向有效RAM! 设为buffer起始处, 栈向下增长到gap区, 不与buffer重叠)\n'
    '\t    // ==================== 运行时内存布局 (MM32G0001: 2KB SRAM @ 0x20000000-0x200007FF) ====================\n'
    '\t    //\n'
    '\t    //  0x20000000 +-----------+  <- algo_start     (算法代码加载地址)\n'
    '\t    //            | algo code |  548 bytes (0x224)  {32B标准头 + 512B PrgCode + 4B PrgData}\n'
    '\t    //  0x20000224 +-----------+  <- static_base    (gap区起点)\n'
    '\t    //            |  空闲gap  |  476 bytes          栈向下增长至此区域\n'
    '\t    //  0x200003EC | -栈底-   |  <- ProgramPage push{r4-r7,lr} 最深位置 (SP-20)\n'
    '\t    //  0x20000400 +===========+  <- stack_pointer = program_buffer\n'
    '\t    //            |  buffer   |  1024 bytes         烧录器在此填充1KB页数据\n'
    '\t    //  0x20000800 +-----------+  <- 2KB SRAM顶部 (超出即HardFault!)\n'
    '\t    //\n'
    '\t    0x20000400,  // [ 8] program_buffer       - 编程数据缓冲区 (烧录器将待写入Flash的页数据复制到此)\n'
    '\t    0x20000000,  // [ 9] algo_start           - 算法加载基址 (烧录器将algo_blob[]复制到以此地址开始的RAM)\n'
    '\t    0x00000400,  // [10] program_buffer_size   - 缓冲区大小 = 1024字节 (必须 >= Flash页大小szPage)\n'
    '\t    // ==================== 目标芯片Flash参数 (手动修改, 需与FLM文件FlashDevice结构体一致) ====================\n'
    '\t    0x08000000,  // [11] flash_addr  - Flash起始地址 (MM32G0001主Flash映射地址, Cortex-M0固定)\n'
    '\t    0x00004000,  // [12] flash_size  - Flash总大小 = 16384 = 16KB (MM32G0001A1T规格)\n'
    '\t    0x00000224,  // [13] algo_size   - 算法代码字节数 = 548 (32B标准头+512B PrgCode段+4B PrgData段)'
)

old_bytes = old_header.encode(ENCODING)
if old_bytes not in content:
    print(f"ERROR: old_header not found! Checking with different encoding...")
    # Try to find what's actually in the file
    # Search for key strings
    for marker in [b'Init', b'UnInit', b'0x2000002D']:
        idx = content.find(marker)
        if idx >= 0:
            print(f"  Found '{marker.decode()}' at byte {idx}")
            print(f"  Context: {repr(content[max(0,idx-30):idx+60])}")
    sys.exit(1)

new_bytes = new_header.encode(ENCODING)
content = content.replace(old_bytes, new_bytes, 1)

with open(filepath, 'wb') as f:
    f.write(content)
print("OK - Header comments updated successfully!")
