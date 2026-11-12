#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
FLM to C/BIN Converter (Flash + OPT 扩展版) v1.0
=================================================
将 Keil .FLM 烧录算法文件转换为扩展格式 .c 和 .bin，供离线烧录器使用。

与 flm2c.py 的区别:
  - 标准 14 字段 + OPT 扩展 3 字段 = 17 字段头
  - 一个 BIN 同时包含主 Flash 和 OPT 烧录信息
  - 离线烧录器加载一个算法即可完成 Flash + OPT 一体化烧写

扩展字段:
  [14] opt_program — ProgramOpt 函数地址
  [15] opt_addr    — OPT 基址 (默认 0x1FFFF800)
  [16] opt_size    — OPT 大小 (默认 16)

用法:
  python flm2c_opt.py  MM32G001-OPT.FLM               # Flash+OPT 扩展输出
  python flm2c_opt.py  MM32G001-OPT.FLM  --no-opt     # 退化为标准14字段
  python flm2c_opt.py  MM32G001-OPT.FLM  --dry-run    # 预览不写文件
  python flm2c_opt.py  MM32G001-OPT.FLM  -o ./out     # 指定输出目录
"""

import struct, re, os, sys, argparse

# ============================================================
# 常量
# ============================================================
HEADER_SIZE   = 32          # CMSIS 标准头 32 字节
ALGO_START    = 0x20000000  # 算法加载 RAM 基址
BUF_OFFSET    = 0x400       # 缓冲区偏移 (CMSIS 约定)
OPT_ADDR      = 0x1FFFF800  # MM32G001 OPT 基址
OPT_SIZE      = 16          # MM32G001 OPT 大小 (8个半字)

# CMSIS 标准 Flash 算法头 (8 words = 32 bytes)
FLASH_ALGO_HEADER = [
    0xE00ABE00, 0x062D780D, 0x24084068, 0xD3000040,
    0x1E644058, 0x1C49D1FA, 0x2A001E52, 0x4770D1F2,
]

# 标准字段数
STD_FIELDS = 14
# 扩展字段数 (OPT)
EXT_FIELDS = 3
# 总字段数
TOTAL_FIELDS = STD_FIELDS + EXT_FIELDS  # 17


# ============================================================
# FLM 解析 (与 flm2c.py 相同逻辑, 扩展 ProgramOpt 符号)
# ============================================================

def parse_flm(filepath):
    """解析 FLM (ELF) 文件, 返回全部关键信息字典."""
    with open(filepath, 'rb') as f:
        data = f.read()

    if data[:4] != b'\x7fELF':
        raise ValueError("不是有效的 ELF 文件")

    info = {}

    # ELF header
    e_shoff     = struct.unpack_from('<I', data, 32)[0]
    e_shentsize = struct.unpack_from('<H', data, 46)[0]
    e_shnum     = struct.unpack_from('<H', data, 48)[0]
    e_shstrndx  = struct.unpack_from('<H', data, 50)[0]

    # shstrtab
    base = e_shoff + e_shstrndx * e_shentsize
    shstrtab_off = struct.unpack_from('<I', data, base + 16)[0]

    def sh_name(idx):
        end = data.find(b'\x00', shstrtab_off + idx)
        return data[shstrtab_off + idx:end].decode(errors='replace')

    # Parse sections
    for i in range(e_shnum):
        o = e_shoff + i * e_shentsize
        sn, s_type, s_flags = struct.unpack_from('<III', data, o)
        s_addr, s_off, s_sz = struct.unpack_from('<III', data, o + 12)
        if sn == 0:
            continue
        name = sh_name(sn)

        if name == 'PrgCode':
            info['prgcode_size'] = s_sz
            info['prgcode_data'] = data[s_off:s_off + s_sz]
        elif name == 'PrgData':
            info['prgdata_addr'] = s_addr
            info['prgdata_size'] = s_sz
            info['prgdata_data'] = data[s_off:s_off + s_sz]
            info['prgdata_wr']   = bool(s_flags & 0x1)
        elif name == 'DevDscr':
            _parse_flash_device(info, data, s_off)

    # Parse symbols (包括 ProgramOpt)
    _parse_symbols(info, data, e_shoff, e_shentsize, e_shnum)

    # Validate: 标准5函数 + ProgramOpt + 段信息
    required = ['Init', 'UnInit', 'EraseChip', 'EraseSector', 'ProgramPage',
                'prgcode_size', 'prgdata_addr', 'prgdata_size',
                'flash_addr', 'flash_size', 'flash_page']
    missing = [r for r in required if r not in info]
    if missing:
        raise ValueError(f"FLM 缺少关键信息: {missing}")

    return info


def _parse_flash_device(info, data, off):
    """FlashDevice 结构体 (CMSIS)."""
    info['flash_vers']  = struct.unpack_from('<H', data, off)[0]
    name_bytes = data[off + 2 : off + 130]
    null_pos = name_bytes.find(b'\x00')
    info['dev_name']    = name_bytes[:null_pos].decode(errors='replace')
    info['flash_type']  = struct.unpack_from('<H', data, off + 130)[0]
    info['flash_addr']  = struct.unpack_from('<I', data, off + 132)[0]
    info['flash_size']  = struct.unpack_from('<I', data, off + 136)[0]
    info['flash_page']  = struct.unpack_from('<I', data, off + 140)[0]


def _parse_symbols(info, data, e_shoff, e_shentsize, e_shnum):
    """从 .symtab 提取函数地址 (包括 ProgramOpt)."""
    wanted = {'Init', 'UnInit', 'EraseChip', 'EraseSector', 'ProgramPage', 'ProgramOpt'}

    for i in range(e_shnum):
        o = e_shoff + i * e_shentsize
        if struct.unpack_from('<I', data, o + 4)[0] != 2:  # SYMTAB
            continue
        sym_off, sym_sz = struct.unpack_from('<II', data, o + 16)
        link = struct.unpack_from('<I', data, o + 24)[0]

        # linked strtab
        so = e_shoff + link * e_shentsize
        strtab = struct.unpack_from('<I', data, so + 16)[0]

        for j in range(sym_sz // 16):
            p = sym_off + j * 16
            st_name  = struct.unpack_from('<I', data, p)[0]
            st_value = struct.unpack_from('<I', data, p + 4)[0]
            st_size  = struct.unpack_from('<I', data, p + 8)[0]

            if st_name == 0:
                continue
            end = data.find(b'\x00', strtab + st_name)
            name = data[strtab + st_name:end].decode(errors='replace')

            if name in wanted:
                info[name] = st_value
                info[f'{name}_size'] = st_size


# ============================================================
# C / BIN 生成 (扩展版: 17 字段)
# ============================================================

def generate(info, algo_start=ALGO_START, with_comments=True,
             opt_enabled=True, opt_addr=OPT_ADDR, opt_size=OPT_SIZE):
    """从 FLM 信息生成扩展格式 C 文件内容和 BIN 数据.

    参数:
      info         — parse_flm() 返回的信息字典
      algo_start   — 算法加载 RAM 基址
      with_comments— 是否生成详细注释
      opt_enabled  — 是否包含 OPT 扩展字段
      opt_addr     — OPT 基址
      opt_size     — OPT 大小

    返回 (c_content, bin_bytes)
    """
    I = info

    # ---- 地址计算 ----
    fn_addr = {}
    for fn in ['Init', 'UnInit', 'EraseChip', 'EraseSector', 'ProgramPage']:
        fn_addr[fn] = algo_start + HEADER_SIZE + I[fn]

    # ProgramOpt 地址
    opt_prog_addr = algo_start + HEADER_SIZE + I.get('ProgramOpt', I.get('ProgramPage', 0))

    breakpoint   = algo_start | 1                         # Thumb 模式入口
    static_base  = algo_start + I['prgdata_addr']         # PrgData 段地址
    prog_buf     = algo_start + BUF_OFFSET                # 缓冲区
    stack_ptr    = prog_buf                               # 栈=缓冲区起始,向下到gap
    algo_size    = HEADER_SIZE + I['prgcode_size'] + I['prgdata_size']

    # ---- 最低 RAM 需求 ----
    code_end     = algo_start + algo_size
    buf_end      = prog_buf + I['flash_page']
    min_ram      = max(code_end, buf_end) - algo_start

    # ---- 构建 algo_blob ----
    words = list(FLASH_ALGO_HEADER)                       # 8 words 标准头

    # PrgCode
    cd = I['prgcode_data']
    for i in range(0, len(cd), 4):
        words.append(struct.unpack_from('<I', cd, i)[0])

    # PrgData
    pd = I['prgdata_data']
    for i in range(0, len(pd), 4):
        words.append(struct.unpack_from('<I', pd, i)[0])

    # 对齐到 algo_size
    expect = algo_size // 4
    while len(words) < expect:
        words.append(0)
    words = words[:expect]

    # ---- 格式化 algo_blob 行 ----
    blob_lines = []
    for i in range(0, len(words), 8):
        chunk = words[i:i+8]
        vals = ', '.join(f'0x{w:08X}' for w in chunk)
        if with_comments:
            if i == 0:
                tag = ' // 32B CMSIS算法头 (BKPT序列)'
            elif i == 8:
                tag = ' // PrgCode 开始'
            else:
                tag = ''
        else:
            tag = ''
        blob_lines.append(f'    {vals},{tag}')

    # ---- 生成 C 文件 ----
    flash_kb = I['flash_size'] // 1024
    gap_size = BUF_OFFSET - algo_size

    # 标准字段 (含注释)
    std_fields_c = f'''    0x{fn_addr["Init"]:08X},  // [ 0] Init          - 初始化: 解锁Flash, 清错误标志, 使能外设时钟
    0x{fn_addr["UnInit"]:08X},  // [ 1] UnInit        - 反初始化: 恢复Flash默认状态, 锁定
    0x{fn_addr["EraseChip"]:08X},  // [ 2] EraseChip     - 全片擦除 {flash_kb}KB
    0x{fn_addr["EraseSector"]:08X},  // [ 3] EraseSector   - 扇区擦除
    0x{fn_addr["ProgramPage"]:08X},  // [ 4] ProgramPage   - 主Flash页编程 ({I["flash_page"]}B/页)'''

    syscall_c = f'''    0x{breakpoint:08X},  // [ 5] breakpoint    = algo_start | 1 (Thumb模式入口)
    0x{static_base:08X},  // [ 6] static_base   = algo_start + PrgData.offset(0x{I["prgdata_addr"]:X})  PrgData={I["prgdata_size"]}B
    0x{stack_ptr:08X},  // [ 7] stack_pointer = program_buffer (栈向下增长到gap)'''

    mem_c = f'''    0x{prog_buf:08X},  // [ 8] program_buffer       - 页数据缓冲区
    0x{algo_start:08X},  // [ 9] algo_start           - 算法加载基址
    0x{I["flash_page"]:08X},  // [10] program_buffer_size   - 缓冲区 = Flash 页大小'''

    flash_c = f'''    0x{I["flash_addr"]:08X},  // [11] flash_addr  - 主Flash基址 ({I["dev_name"]})
    0x{I["flash_size"]:08X},  // [12] flash_size  - 主Flash总大小 = {flash_kb}KB
    0x{algo_size:08X},  // [13] algo_size   - 算法大小 = {algo_size}B ({algo_size//4} words)'''

    # OPT 扩展字段
    if opt_enabled and 'ProgramOpt' in I:
        opt_ext_c = f'''
    // ==================== OPT 扩展字段 [14-16] (离线烧录器扩展) ====================
    0x{opt_prog_addr:08X},  // [14] opt_program - ProgramOpt 函数地址 (OPT编程)
    0x{opt_addr:08X},  // [15] opt_addr    - OPT 基址
    0x{opt_size:08X},  // [16] opt_size    - OPT 大小 = {opt_size}B ({opt_size//2}个半字)'''
    elif opt_enabled:
        # FLM 不含 ProgramOpt, 用 ProgramPage 填充并标注
        opt_ext_c = f'''
    // ==================== OPT 扩展字段 [14-16] (⚠ FLM不含ProgramOpt, 用ProgramPage填充) ====================
    0x{fn_addr["ProgramPage"]:08X},  // [14] opt_program - (无ProgramOpt, 回退到ProgramPage)
    0x{opt_addr:08X},  // [15] opt_addr    - OPT 基址
    0x{opt_size:08X},  // [16] opt_size    - OPT 大小 = {opt_size}B'''
    else:
        opt_ext_c = ''

    if with_comments:
        # 详细注释版
        c = f'''// ============================================================
// Flash + OPT 一体化烧录算法 (扩展格式)
// 芯片: {I["dev_name"]}
// 主Flash: {flash_kb}KB @ 0x{I["flash_addr"]:08X}, 页大小 {I["flash_page"]}B
// OPT:     {opt_size}B @ 0x{opt_addr:08X} (ProgramOpt{"=0x%X" % I.get("ProgramOpt", 0) if "ProgramOpt" in I else " 无"})
// 算法加载: RAM 0x{algo_start:08X}, 最低需 {min_ram}B RAM
// 源文件: D:\\电机\\DAPlink\\_Template-opt\\FlashPrg.c
// ============================================================
// 结构定义 (program_target_ext_t):
//   [ 0- 4] 标准函数入口 (Init/UnInit/EraseChip/EraseSector/ProgramPage)
//   [ 5- 7] program_syscall_t (breakpoint/static_base/stack_pointer)
//   [ 8-10] 运行时内存布局 (program_buffer/algo_start/program_buffer_size)
//   [11-13] 主Flash参数 (flash_addr/flash_size/algo_size)
//   [14-16] OPT扩展参数 (opt_program/opt_addr/opt_size)  ← 扩展字段
//   [17+  ] algo_blob 算法机器码 ({algo_size}B = {algo_size//4} words)
// ============================================================
//
//  内存布局:
//  0x{algo_start:08X} +-----------+  <- algo_start
//            | algo code |  {algo_size}B = 32B头 + {I["prgcode_size"]}B PrgCode + {I["prgdata_size"]}B PrgData
//  0x{algo_start + algo_size:08X} +-----------+  <- 代码结束
//            |    gap    |  {gap_size}B 空闲 (栈向下增长)
//  0x{prog_buf:08X} +===========+  <- program_buffer = stack_pointer
//            |  buffer   |  {I["flash_page"]}B  页数据缓冲区
//  0x{buf_end:08X} +-----------+  <- buffer 结束
//
const program_target_ext_t flash_algo = {{
{std_fields_c}
    // ==================== [5-7] program_syscall_t ====================
{syscall_c}
    // ==================== [8-10] 运行时内存布局 (最低需 {min_ram}B RAM) ====================
{mem_c}
    // ==================== [11-13] Flash 参数 ====================
{flash_c}{opt_ext_c}
    // ==================== 算法机器码 ({algo_size}B = {algo_size//4} words) ====================
{chr(10).join(blob_lines)}
}};
'''
    else:
        # 无注释精简版
        std_no_c = f'    0x{fn_addr["Init"]:08X}, 0x{fn_addr["UnInit"]:08X}, 0x{fn_addr["EraseChip"]:08X}, 0x{fn_addr["EraseSector"]:08X}, 0x{fn_addr["ProgramPage"]:08X},'
        syscall_no_c = f'    0x{breakpoint:08X}, 0x{static_base:08X}, 0x{stack_ptr:08X},'
        mem_no_c = f'    0x{prog_buf:08X}, 0x{algo_start:08X}, 0x{I["flash_page"]:08X},'
        flash_no_c = f'    0x{I["flash_addr"]:08X}, 0x{I["flash_size"]:08X}, 0x{algo_size:08X},'

        if opt_enabled:
            opt_no_c = f'    0x{opt_prog_addr:08X}, 0x{opt_addr:08X}, 0x{opt_size:08X},'
        else:
            opt_no_c = ''

        c = f'''// Flash+OPT 一体化烧录算法 (扩展格式) — {I["dev_name"]} 主Flash={flash_kb}KB  OPT={opt_size}B@0x{opt_addr:08X}
const program_target_ext_t flash_algo = {{
{std_no_c}
{syscall_no_c}
{mem_no_c}
{flash_no_c}{opt_no_c}
{chr(10).join(blob_lines)}
}};
'''

    # ---- 生成 BIN ----
    bin_data = _c_to_bin(c, opt_enabled)
    return c, bin_data


def _c_to_bin(c_content, opt_enabled=True):
    """从 C 内容提取数值生成 BIN (扩展格式)."""
    all_vals = []
    for line in c_content.split('\n'):
        code = line.split('//')[0]
        vals = re.findall(r'0x([0-9A-Fa-f]{8})', code)
        all_vals.extend([int(v, 16) for v in vals])

    fields = TOTAL_FIELDS if opt_enabled else STD_FIELDS
    hdr = all_vals[:fields]
    algo = all_vals[fields:fields + hdr[13] // 4]

    out = bytearray()
    for v in hdr:   out.extend(struct.pack('<I', v))
    for v in algo:  out.extend(struct.pack('<I', v))
    return bytes(out)


# ============================================================
# 验证
# ============================================================

def verify(bin_data, info, algo_start=ALGO_START, opt_enabled=True,
           opt_addr=OPT_ADDR, opt_size=OPT_SIZE):
    """验证 BIN 正确性, 返回错误列表."""
    errors = []
    fields = TOTAL_FIELDS if opt_enabled else STD_FIELDS
    hdr = [struct.unpack_from('<I', bin_data, i*4)[0] for i in range(fields)]

    # 大小
    header_bytes = fields * 4
    expected_total = header_bytes + hdr[13]
    if len(bin_data) != expected_total:
        errors.append(f"BIN 大小 {len(bin_data)} != 期望 {expected_total}")

    # 标准函数地址
    for i, fn in enumerate(['Init','UnInit','EraseChip','EraseSector','ProgramPage']):
        exp = algo_start + HEADER_SIZE + info[fn]
        if hdr[i] != exp:
            errors.append(f"[{i}] {fn}: 0x{hdr[i]:08X} != 期望 0x{exp:08X}")

    # static_base
    exp_sb = algo_start + info['prgdata_addr']
    if hdr[6] != exp_sb:
        errors.append(f"[6] static_base: 0x{hdr[6]:08X} != 期望 0x{exp_sb:08X}")

    # algo_size
    exp_as = HEADER_SIZE + info['prgcode_size'] + info['prgdata_size']
    if hdr[13] != exp_as:
        errors.append(f"[13] algo_size: 0x{hdr[13]:X} != 期望 0x{exp_as:X}")

    # OPT 扩展字段
    if opt_enabled:
        if 'ProgramOpt' in info:
            exp_opt = algo_start + HEADER_SIZE + info['ProgramOpt']
            if hdr[14] != exp_opt:
                errors.append(f"[14] opt_program: 0x{hdr[14]:08X} != 期望 0x{exp_opt:08X}")
        if hdr[15] != opt_addr:
            errors.append(f"[15] opt_addr: 0x{hdr[15]:08X} != 期望 0x{opt_addr:08X}")
        if hdr[16] != opt_size:
            errors.append(f"[16] opt_size: 0x{hdr[16]:X} != 期望 0x{opt_size:X}")

    return errors


# ============================================================
# 说明文档生成
# ============================================================

def generate_readme(info, algo_start, opt_enabled, opt_addr, opt_size):
    """生成离线烧录器加载说明."""
    I = info

    fn_addr = {}
    for fn in ['Init', 'UnInit', 'EraseChip', 'EraseSector', 'ProgramPage']:
        fn_addr[fn] = algo_start + HEADER_SIZE + I[fn]

    has_opt = opt_enabled and 'ProgramOpt' in I
    opt_prog_addr = algo_start + HEADER_SIZE + I.get('ProgramOpt', 0) if has_opt else 0

    flash_kb = I['flash_size'] // 1024
    algo_size = HEADER_SIZE + I['prgcode_size'] + I['prgdata_size']
    fields = TOTAL_FIELDS if opt_enabled else STD_FIELDS
    header_bytes = fields * 4

    lines = []
    lines.append("=" * 60)
    lines.append(f"MM32G001 Flash + OPT 一体化烧录算法 - 离线烧录器加载说明")
    lines.append("=" * 60)
    lines.append("")

    lines.append("1. 基本信息")
    lines.append("   FLM 文件:     MM32G001-OPT.FLM")
    lines.append(f"   芯片型号:     {I['dev_name']}")
    lines.append(f"   算法 RAM 基址: 0x{algo_start:08X}")
    lines.append(f"   算法大小:     {algo_size}B ({algo_size//4} words)")
    lines.append(f"   最低 RAM 需求: {max(algo_size, BUF_OFFSET + I['flash_page'])}B")
    lines.append("")

    lines.append("2. 主 Flash 参数")
    lines.append(f"   基址:         0x{I['flash_addr']:08X}")
    lines.append(f"   大小:         0x{I['flash_size']:X} ({flash_kb}KB)")
    lines.append(f"   页大小:       {I['flash_page']}B")
    lines.append(f"   编程函数:     ProgramPage @ 0x{fn_addr['ProgramPage']:08X}")
    lines.append("")

    if opt_enabled:
        lines.append("3. OPT (Option Bytes) 参数")
        lines.append(f"   基址:         0x{opt_addr:08X}")
        lines.append(f"   大小:         {opt_size} 字节 ({opt_size//2} 个半字)")
        if has_opt:
            lines.append(f"   编程函数:     ProgramOpt @ 0x{opt_prog_addr:08X}")
        else:
            lines.append(f"   编程函数:     (FLM 不含 ProgramOpt, 回退到 ProgramPage)")
        lines.append("")

        lines.append("4. OPT 字节布局 (MM32G001)")
        lines.append("   偏移    内容       说明")
        lines.append("   0x00    RDPRT      读保护 (0x5AA5=解除保护)")
        lines.append("   0x02    DATA0      用户数据 0")
        lines.append("   0x04    DATA1      用户数据 1")
        lines.append("   0x06    WRP0       写保护 0")
        lines.append("   0x08    WRP1       写保护 1")
        lines.append("   0x0A    WRP2       写保护 2")
        lines.append("   0x0C    WRP3       写保护 3")
        lines.append("   0x0E    保留       -")
        lines.append("")
    else:
        lines.append("3. OPT 参数: (未启用)")
        lines.append("")

    n = 5 if opt_enabled else 4
    lines.append(f"{n}. BIN 文件格式 (扩展结构 program_target_ext_t)")
    lines.append(f"   头部字段数:   {fields} (标准14 + OPT扩展3)" if opt_enabled else f"   头部字段数:   {fields} (标准14)")
    lines.append(f"   头部大小:     {header_bytes} bytes")
    lines.append(f"   算法码大小:   {algo_size} bytes")
    lines.append(f"   BIN 总大小:   {header_bytes + algo_size} bytes")
    lines.append("")
    lines.append("   字段布局:")
    labels_std = [
        "[ 0] init", "[ 1] uninit", "[ 2] erase_chip", "[ 3] erase_sector",
        "[ 4] program_page (主Flash)",
        "[ 5] breakpoint", "[ 6] static_base", "[ 7] stack_pointer",
        "[ 8] program_buffer", "[ 9] algo_start", "[10] program_buffer_size",
        "[11] flash_addr (主Flash基址)", "[12] flash_size (主Flash大小)",
        "[13] algo_size",
    ]
    for label in labels_std:
        lines.append(f"   {label}")
    if opt_enabled:
        lines.append("   [14] opt_program (OPT编程函数)")
        lines.append(f"   [15] opt_addr (OPT基址 = 0x{opt_addr:08X})")
        lines.append(f"   [16] opt_size (OPT大小 = {opt_size})")
    lines.append("   [17+] algo_blob (算法机器码)")
    lines.append("")

    m = n + 1
    lines.append(f"{m}. 离线烧录器加载步骤")
    lines.append("   a) 将 .bin 文件复制到离线烧录器的算法目录")
    lines.append("   b) 在烧录器配置中选择此算法文件")
    lines.append("   c) 离线烧录器从 BIN 头部读取所有参数, 自动识别:")
    lines.append("      - 标准 14 字段 → 主 Flash 编程 (ProgramPage)")
    if opt_enabled:
        lines.append("      - 扩展 3 字段 → OPT 编程 (ProgramOpt)")
    lines.append("   d) 配置待烧录的主 Flash 数据和 OPT 数据")
    lines.append("   e) 执行烧录 (先主 Flash, 后 OPT)")
    lines.append("")

    m += 1
    lines.append(f"{m}. 源文件位置")
    lines.append("   FLM 文件:     MM32G001-OPT.FLM")
    lines.append("   FLM C 源代码: D:\\电机\\DAPlink\\_Template-opt\\")
    lines.append("     FlashPrg.c  — Flash/OPT 编程实现")
    lines.append("     FlashDev.c  — FlashDevice 设备描述")
    lines.append("     FlashOS.h   — CMSIS 算法接口定义")
    lines.append("     FlashStk.s  — 栈配置 (STACK_SIZE=0x64)")
    lines.append("")

    m += 1
    lines.append(f"{m}. 注意事项")
    lines.append("   - ProgramOpt 函数内部包含 OPT 块擦除步骤, 烧录前自动擦除")
    lines.append("   - 烧录错误的 OPT 值 (尤其是 RDPRT) 可能导致芯片锁定")
    lines.append("   - 解除读保护: RDPRT = 0x5AA5")
    lines.append("   - 算法在 RAM (0x20000000) 中执行, 不掉电")
    if not has_opt and opt_enabled:
        lines.append("   - ⚠ 此 FLM 不含 ProgramOpt 符号, OPT 字段回退到 ProgramPage")
    lines.append("")

    return '\n'.join(lines)


# ============================================================
# CLI
# ============================================================

def main():
    parser = argparse.ArgumentParser(
        description='FLM → C/BIN 转换器 (Flash+OPT 扩展版) — 一体化烧录算法',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""示例:
  python flm2c_opt.py MM32G001-OPT.FLM              # Flash+OPT 扩展输出
  python flm2c_opt.py MM32G001-OPT.FLM --no-opt     # 纯Flash (标准14字段)
  python flm2c_opt.py MM32G001-OPT.FLM --dry-run    # 预览不生成文件
  python flm2c_opt.py MM32G001-OPT.FLM -o ./out     # 指定输出目录""")
    parser.add_argument('flm_file', help='输入 .FLM 文件路径 (建议使用含ProgramOpt的FLM)')
    parser.add_argument('-a', '--algo-start', type=lambda x: int(x, 0),
                        default=ALGO_START,
                        help=f'算法加载 RAM 地址, 默认 0x{ALGO_START:08X}')
    parser.add_argument('-o', '--output-dir', default=None,
                        help='输出目录, 默认与 FLM 同目录')
    parser.add_argument('--opt-addr', type=lambda x: int(x, 0),
                        default=OPT_ADDR,
                        help=f'OPT 基址, 默认 0x{OPT_ADDR:08X}')
    parser.add_argument('--opt-size', type=lambda x: int(x, 0),
                        default=OPT_SIZE,
                        help=f'OPT 大小(字节), 默认 {OPT_SIZE}')
    parser.add_argument('--no-opt', action='store_true',
                        help='不生成 OPT 扩展字段 (退化为标准 14 字段输出)')
    parser.add_argument('--no-comments', action='store_true',
                        help='生成无注释精简版 C 文件')
    parser.add_argument('--no-bin', action='store_true',
                        help='不生成 BIN 文件')
    parser.add_argument('--no-readme', action='store_true',
                        help='不生成说明文件')
    parser.add_argument('--dry-run', action='store_true',
                        help='仅预览, 不写入文件')
    args = parser.parse_args()

    # 读取 FLM
    flm_path = args.flm_file
    if not os.path.exists(flm_path):
        print(f"错误: 找不到 {flm_path}")
        sys.exit(1)

    opt_enabled = not args.no_opt

    print(f'解析: {os.path.basename(flm_path)}')
    info = parse_flm(flm_path)

    # 显示信息
    flash_kb = info['flash_size'] // 1024
    print(f'  芯片:    {info["dev_name"]}')
    print(f'  主Flash: {flash_kb}KB @ 0x{info["flash_addr"]:08X},  页大小: {info["flash_page"]}B')
    print(f'  PrgCode: {info["prgcode_size"]}B,  PrgData: {info["prgdata_size"]}B @ 0x{info["prgdata_addr"]:X}')

    syms = ['Init','UnInit','EraseChip','EraseSector','ProgramPage']
    print(f'  符号:    ' + ', '.join(f'{s}=0x{info[s]:X}' for s in syms))

    has_opt = 'ProgramOpt' in info
    if has_opt:
        print(f'  OPT:     ProgramOpt=0x{info["ProgramOpt"]:X} ({info["ProgramOpt_size"]}B)')
    else:
        print(f'  OPT:     (FLM 不含 ProgramOpt 符号)')

    if opt_enabled:
        print(f'  OPT配置: addr=0x{args.opt_addr:08X}, size={args.opt_size}B')
    else:
        print(f'  模式:    标准14字段 (--no-opt, 不含OPT扩展)')

    # 生成
    c_content, bin_data = generate(info, args.algo_start, not args.no_comments,
                                   opt_enabled, args.opt_addr, args.opt_size)

    # 最低 RAM
    fields = TOTAL_FIELDS if opt_enabled else STD_FIELDS
    hdr = [struct.unpack_from('<I', bin_data, i*4)[0] for i in range(fields)]
    algo_size = hdr[13]
    min_ram = max(algo_size, BUF_OFFSET + info['flash_page'])
    print(f'  最低RAM: {min_ram}B ({min_ram//1024}KB{"+" if min_ram%1024 else ""}{min_ram%1024 if min_ram%1024 else ""}B)')
    print(f'  BIN大小: {len(bin_data)}B (头{fields*4}B + 算法{algo_size}B)')

    # 输出路径
    out_dir = args.output_dir or os.path.dirname(os.path.abspath(flm_path))
    base = os.path.splitext(os.path.basename(flm_path))[0]
    suffix = '_ext' if opt_enabled else '_std'
    c_path = os.path.join(out_dir, base + suffix + '.c')
    bin_path = os.path.join(out_dir, base + suffix + '.bin')
    readme_path = os.path.join(out_dir, base + suffix + '_说明.txt')

    if args.dry_run:
        print(f"\n--- DRY RUN (不写文件) ---")
        print(f'  C      → {c_path}')
        if not args.no_bin:
            print(f'  BIN    → {bin_path} ({len(bin_data)} bytes)')
        if not args.no_readme:
            print(f'  说明   → {readme_path}')
        print(f'\n  关键字段 ({fields}个):')
        names_std = ['Init','UnInit','EraseChip','EraseSector','ProgramPage',
                     'breakpt','static_base','stack_ptr','buf','algo_start',
                     'buf_sz','flash_addr','flash_sz','algo_sz']
        names_opt = ['opt_program','opt_addr','opt_size']
        names = names_std + (names_opt if opt_enabled else [])
        for i, (n, v) in enumerate(zip(names, hdr)):
            print(f'    [{i:2d}] {n:16s} = 0x{v:08X}')
        return

    # 写 C 文件
    with open(c_path, 'w', encoding='utf-8') as f:
        f.write(c_content)
    print(f'\nC      → {c_path}')

    # 写 BIN 文件
    if not args.no_bin:
        with open(bin_path, 'wb') as f:
            f.write(bin_data)
        print(f'BIN    → {bin_path} ({len(bin_data)} bytes)')

        # 验证
        errs = verify(bin_data, info, args.algo_start, opt_enabled,
                      args.opt_addr, args.opt_size)
        if errs:
            print('\n⚠ 验证警告:')
            for e in errs:
                print(f'  - {e}')
        else:
            opt_info = ''
            if opt_enabled:
                opt_info = f'  opt_program=0x{hdr[14]:08X}  opt_addr=0x{hdr[15]:08X}  opt_size={hdr[16]}'
            print(f'  验证通过  static_base=0x{hdr[6]:08X}  algo_size={hdr[13]}B{opt_info}')

    # 写说明文件
    if not args.no_readme:
        readme = generate_readme(info, args.algo_start, opt_enabled,
                                 args.opt_addr, args.opt_size)
        with open(readme_path, 'w', encoding='utf-8') as f:
            f.write(readme)
        print(f'说明   → {readme_path}')


if __name__ == '__main__':
    main()
