#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
FLM to C/BIN Converter v2.1
============================
将 Keil .FLM 烧录算法文件转换为 .c 和 .bin，供离线烧录器使用。
所有参数全自动从 FLM 提取，无需手动指定。

修复了 flash_algo.exe 的问题:
  1. static_base   — 旧:硬编码+0xC00 → 新:从 PrgData 段提取
  2. stack_pointer — 旧:硬编码+0x1000 → 新:恒为 buffer 地址,栈向 gap 增长,通用安全
  3. algo_size     — 旧:手动填写 → 新:自动计算 32+PrgCode+PrgData
  4. flash_addr/size — 旧:手动填写 → 新:从 FlashDevice 提取

用法:
  python flm2c.py  input.FLM               # 生成 .c 和 .bin
  python flm2c.py  input.FLM  --no-comments # 精简版无注释
  python flm2c.py  input.FLM  -o ./out      # 指定输出目录
  python flm2c.py  input.FLM  --dry-run     # 预览不写文件
"""

import struct, re, os, sys, argparse

# ============================================================
# 常量
# ============================================================
HEADER_SIZE   = 32          # CMSIS 标准头 32 字节
ALGO_START    = 0x20000000  # 算法加载 RAM 基址
BUF_OFFSET    = 0x400       # 缓冲区偏移 (CMSIS 约定)

# CMSIS 标准 Flash 算法头 (8 words = 32 bytes)
FLASH_ALGO_HEADER = [
    0xE00ABE00, 0x062D780D, 0x24084068, 0xD3000040,
    0x1E644058, 0x1C49D1FA, 0x2A001E52, 0x4770D1F2,
]


# ============================================================
# FLM 解析
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

    # Parse symbols
    _parse_symbols(info, data, e_shoff, e_shentsize, e_shnum)

    # Validate
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
    """从 .symtab 提取函数地址."""
    wanted = {'Init', 'UnInit', 'EraseChip', 'EraseSector', 'ProgramPage'}

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
# C / BIN 生成
# ============================================================

def generate(info, algo_start=ALGO_START, with_comments=True):
    """从 FLM 信息生成 C 文件内容和 BIN 数据.

    返回 (c_content, bin_bytes)
    """
    I = info

    # ---- 地址计算 ----
    fn_addr = {}
    for fn in ['Init', 'UnInit', 'EraseChip', 'EraseSector', 'ProgramPage']:
        fn_addr[fn] = algo_start + HEADER_SIZE + I[fn]

    breakpoint   = algo_start | 1                         # Thumb 模式入口
    static_base  = algo_start + I['prgdata_addr']         # PrgData 段地址
    prog_buf     = algo_start + BUF_OFFSET                # 缓冲区
    stack_ptr    = prog_buf                               # ★ 栈=缓冲区起始,向下到gap
    algo_size    = HEADER_SIZE + I['prgcode_size'] + I['prgdata_size']

    # ---- 最低 RAM 需求 ----
    code_end     = algo_start + algo_size
    buf_end      = prog_buf + I['flash_page']
    min_ram      = max(code_end, buf_end) - algo_start   # 最少需要的 RAM 字节数

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
            tag = ' // 32B CMSIS算法头 (BKPT序列)' if i == 0 else ''
        else:
            tag = ''
        blob_lines.append(f'    {vals},{tag}')

    # ---- 生成 C 文件 ----
    flash_kb = I['flash_size'] // 1024
    gap_size = BUF_OFFSET - algo_size  # 代码结束到 buffer 的间隙

    if with_comments:
        c = f'''const program_target_t flash_algo = {{
    // ==================== [0-4] 函数入口 (algo_start + 0x20头 + FLM符号偏移) ====================
    0x{fn_addr["Init"]:08X},  // [ 0] Init          - 初始化: 解锁Flash, 清错误标志, 使能外设时钟
    0x{fn_addr["UnInit"]:08X},  // [ 1] UnInit        - 反初始化: 恢复Flash默认状态, 锁定
    0x{fn_addr["EraseChip"]:08X},  // [ 2] EraseChip     - 全片擦除 {flash_kb}KB
    0x{fn_addr["EraseSector"]:08X},  // [ 3] EraseSector   - 扇区擦除
    0x{fn_addr["ProgramPage"]:08X},  // [ 4] ProgramPage   - 页编程 ({I["flash_page"]}B/页)
    // ==================== [5-7] program_syscall_t (系统调用参数) ====================
    0x{breakpoint:08X},  // [ 5] breakpoint    = algo_start | 1 (Thumb模式入口)
    0x{static_base:08X},  // [ 6] static_base   = algo_start + PrgData.offset(0x{I["prgdata_addr"]:X})  PrgData={I["prgdata_size"]}B
    0x{stack_ptr:08X},  // [ 7] stack_pointer = program_buffer (栈向下增长到gap, 通用安全, 不覆盖buffer)
    // ==================== [8-10] 运行时内存布局 (自动计算, 最低需 {min_ram}B = {min_ram//1024}KB+{min_ram%1024}B RAM) ====================
    //
    //  0x{algo_start:08X} +-----------+  <- algo_start
    //            | algo code |  {algo_size}B = 32B头 + {I["prgcode_size"]}B PrgCode + {I["prgdata_size"]}B PrgData
    //  0x{algo_start + algo_size:08X} +-----------+  <- 代码结束
    //            |    gap    |  {gap_size}B 空闲  ★栈向下增长至此 (最深~28B, gap={gap_size}B 充足)
    //  0x{prog_buf:08X} +===========+  <- program_buffer = stack_pointer
    //            |  buffer   |  {I["flash_page"]}B  页数据缓冲区
    //  0x{buf_end:08X} +-----------+  <- buffer 结束 (最低RAM要求)
    //
    0x{prog_buf:08X},  // [ 8] program_buffer       - 页数据缓冲区 (烧录器在此填入待写数据)
    0x{algo_start:08X},  // [ 9] algo_start           - 算法加载基址
    0x{I["flash_page"]:08X},  // [10] program_buffer_size   - 缓冲区 = Flash 页大小
    // ==================== [11-13] Flash 参数 (从 FLM FlashDevice 结构体提取) ====================
    0x{I["flash_addr"]:08X},  // [11] flash_addr  - Flash 基址 ({I["dev_name"]})
    0x{I["flash_size"]:08X},  // [12] flash_size  - Flash 总大小 = {flash_kb}KB
    0x{algo_size:08X},  // [13] algo_size   - 算法大小 = {algo_size}B ({algo_size//4} words)
    // ==================== 算法机器码 ({algo_size}B = {algo_size//4} words) ====================
{chr(10).join(blob_lines)}
}};
'''
    else:
        c = f'''const program_target_t flash_algo = {{
    0x{fn_addr["Init"]:08X}, 0x{fn_addr["UnInit"]:08X}, 0x{fn_addr["EraseChip"]:08X}, 0x{fn_addr["EraseSector"]:08X}, 0x{fn_addr["ProgramPage"]:08X},
    0x{breakpoint:08X}, 0x{static_base:08X}, 0x{stack_ptr:08X},
    0x{prog_buf:08X}, 0x{algo_start:08X}, 0x{I["flash_page"]:08X},
    0x{I["flash_addr"]:08X}, 0x{I["flash_size"]:08X}, 0x{algo_size:08X},
{chr(10).join(blob_lines)}
}};
'''

    # ---- 生成 BIN ----
    bin_data = _c_to_bin(c)
    return c, bin_data


def _c_to_bin(c_content):
    """从 C 内容提取数值生成 BIN."""
    all_vals = []
    for line in c_content.split('\n'):
        code = line.split('//')[0]
        vals = re.findall(r'0x([0-9A-Fa-f]{8})', code)
        all_vals.extend([int(v, 16) for v in vals])

    hdr = all_vals[:14]
    algo = all_vals[14:14 + hdr[13] // 4]

    out = bytearray()
    for v in hdr:   out.extend(struct.pack('<I', v))
    for v in algo:  out.extend(struct.pack('<I', v))
    return bytes(out)


# ============================================================
# 验证
# ============================================================

def verify(bin_data, info, algo_start=ALGO_START):
    """验证 BIN 正确性, 返回错误列表."""
    errors = []
    hdr = [struct.unpack_from('<I', bin_data, i*4)[0] for i in range(14)]

    # 大小
    expected_total = 56 + hdr[13]
    if len(bin_data) != expected_total:
        errors.append(f"BIN 大小 {len(bin_data)} != 期望 {expected_total}")

    # 函数地址
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

    return errors


# ============================================================
# CLI
# ============================================================

def main():
    parser = argparse.ArgumentParser(
        description='FLM → C/BIN 转换器 — 全自动, 无需手动填写任何参数',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""示例:
  python flm2c.py MM32G0001_16.FLM           # 全自动转换
  python flm2c.py GD32F10x_CL.FLM -o ./out   # 指定输出目录
  python flm2c.py input.FLM --dry-run        # 预览不生成文件
  python flm2c.py input.FLM --no-comments    # 精简版(无注释)""")
    parser.add_argument('flm_file', help='输入 .FLM 文件路径')
    parser.add_argument('-a', '--algo-start', type=lambda x: int(x, 0),
                        default=ALGO_START,
                        help=f'算法加载 RAM 地址, 默认 0x{ALGO_START:08X}')
    parser.add_argument('-o', '--output-dir', default=None,
                        help='输出目录, 默认与 FLM 同目录')
    parser.add_argument('--no-comments', action='store_true',
                        help='生成无注释精简版 C 文件')
    parser.add_argument('--no-bin', action='store_true',
                        help='不生成 BIN 文件')
    parser.add_argument('--dry-run', action='store_true',
                        help='仅预览, 不写入文件')
    args = parser.parse_args()

    # 读取 FLM
    flm_path = args.flm_file
    if not os.path.exists(flm_path):
        print(f"错误: 找不到 {flm_path}")
        sys.exit(1)

    print(f'解析: {os.path.basename(flm_path)}')
    info = parse_flm(flm_path)

    # 显示信息
    flash_kb = info['flash_size'] // 1024
    print(f'  芯片:    {info["dev_name"]}')
    print(f'  Flash:   {flash_kb}KB @ 0x{info["flash_addr"]:08X},  页大小: {info["flash_page"]}B')
    print(f'  PrgCode: {info["prgcode_size"]}B,  PrgData: {info["prgdata_size"]}B @ 0x{info["prgdata_addr"]:X}')
    syms = ['Init','UnInit','EraseChip','EraseSector','ProgramPage']
    print(f'  符号:    ' + ', '.join(f'{s}=0x{info[s]:X}' for s in syms))

    # 生成
    c_content, bin_data = generate(info, args.algo_start, not args.no_comments)

    # 最低 RAM
    hdr = [struct.unpack_from('<I', bin_data, i*4)[0] for i in range(14)]
    algo_size = hdr[13]
    min_ram = max(algo_size, BUF_OFFSET + info['flash_page'])
    print(f'  最低RAM: {min_ram}B ({min_ram//1024}KB{"+" if min_ram%1024 else ""}{min_ram%1024 if min_ram%1024 else ""}B)')

    # 输出路径
    out_dir = args.output_dir or os.path.dirname(os.path.abspath(flm_path))
    base = os.path.splitext(os.path.basename(flm_path))[0]
    c_path = os.path.join(out_dir, base + '.c')
    bin_path = os.path.join(out_dir, base + '.bin')

    if args.dry_run:
        print(f"\n--- DRY RUN (不写文件) ---")
        print(f'  C   → {c_path}')
        if not args.no_bin:
            print(f'  BIN → {bin_path} ({len(bin_data)} bytes)')
        print(f'\n  关键字段:')
        names = ['Init','UnInit','EraseChip','EraseSector','ProgramPage',
                 'breakpt','static_base','stack_ptr','buf','algo_start',
                 'buf_sz','flash_addr','flash_sz','algo_sz']
        for i, (n, v) in enumerate(zip(names, hdr)):
            print(f'    [{i:2d}] {n:12s} = 0x{v:08X}')
        return

    # 写 C 文件
    with open(c_path, 'w', encoding='utf-8') as f:
        f.write(c_content)
    print(f'\nC   → {c_path}')

    # 写 BIN 文件
    if not args.no_bin:
        with open(bin_path, 'wb') as f:
            f.write(bin_data)
        print(f'BIN → {bin_path} ({len(bin_data)} bytes)')

        # 验证
        errs = verify(bin_data, info, args.algo_start)
        if errs:
            print('\n⚠ 验证警告:')
            for e in errs:
                print(f'  - {e}')
        else:
            print(f'  验证通过  static_base=0x{hdr[6]:08X}  stack_ptr=0x{hdr[7]:08X}  algo_size={hdr[13]}B')


if __name__ == '__main__':
    main()
