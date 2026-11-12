#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
FLM to C/BIN Converter (Flash + OPT) v2.0
==========================================
修正版：针对 flm2c_opt.py v1.0 的以下缺陷

  1) [致命] FLM 缺 ProgramOpt 时静默回退到 ProgramPage
     -> v2 默认直接报错退出。ProgramPage 不会解锁 OPT 区(OPTKEYR)、
        也不会擦除 OPT 块，写 0x1FFFF800 必然失败，芯片永远进不了保护态。
        确需回退时显式加 --allow-fallback。

  2) [致命] 非 4 字节对齐时 BIN 长度 != 68 + algo_size
     -> 固件用 `fw_size == algo_size + 68` 判定扩展格式，长度差 1 字节
        就会被当成 14 字段解析，opt_program 读到算法头 0xE00ABE00 -> 跑飞。
        v2 直接把 blob 当字节流拼装并补齐到 4 字节，保证长度严格相等。

  3) [严重] static_base 少加 32 字节 CMSIS 头
     -> PrgData 在 ELF 里的地址是相对 PrgCode 起点的偏移；实际加载布局是
        [32B 头][PrgCode][PrgData]，所以正确值 = algo_start + 32 + offset。
        v1 算成 algo_start + offset，指向 PrgCode 末尾 32 字节处。
        本例 FLM 的 PrgData 是 4 个 0 字节、代码无绝对引用，暂不发作；
        换任何用了静态变量的 FLM 就会静默出错。

  4) [严重] BIN 靠正则回读自己生成的 C 文本
     -> 注释里出现 0x12345678、格式改成 %X、字段改成十进制，都会错位。
        v2 直接从 info 打包字节，C 文本只作为给人看的产物。

  5) [严重] verify() 与 generate() 共用同一个(错误)公式，等于自证清白
     -> v2 校验：BIN 长度、opt_program != program_page、
        opt_program 落在 PrgCode 范围内、opt_addr/opt_size 与芯片一致。

  6) [易错] 说明文档的 OPT 布局是 STM32F1 的(RDP/DATA0/DATA1/WRP0..)
     -> MM32G0001 实际是 RDP/USER/DATA0/DATA1/WRP0..WRP3，
        按旧文档填 _opt.bin 会整体错位 2 字节。v2 已修正。

  7) [缺失] 不生成 _opt.bin
     -> 固件只从 0:/download/bin/<固件名>_opt.bin 读 OPT 数据，
        文件缺失就 "OPT file missing"，最终 "FW OK, OPT failed"。
        v2 用 --emit-opt-bin 生成模板，并明确小端字节序。

用法:
  python flm2c_opt_v2.py MM32G001-OPT.FLM                  # Flash+OPT 扩展输出
  python flm2c_opt_v2.py MM32G001-OPT.FLM --emit-opt-bin    # 同时生成 _opt.bin 模板
  python flm2c_opt_v2.py MM32G001-OPT.FLM --no-opt          # 标准 14 字段
  python flm2c_opt_v2.py MM32G001-OPT.FLM --dry-run
"""

import struct, os, sys, argparse

HEADER_SIZE = 32
ALGO_START  = 0x20000000
BUF_OFFSET  = 0x400
OPT_ADDR    = 0x1FFFF800
OPT_SIZE    = 16

FLASH_ALGO_HEADER = [
    0xE00ABE00, 0x062D780D, 0x24084068, 0xD3000040,
    0x1E644058, 0x1C49D1FA, 0x2A001E52, 0x4770D1F2,
]

STD_FIELDS = 14
EXT_FIELDS = 3
TOTAL_FIELDS = STD_FIELDS + EXT_FIELDS

# 各芯片 OPT 半字布局 (偏移 -> 名称)。用于生成 _opt.bin 模板与文档。
OPT_LAYOUTS = {
    'MM32G0001': [
        (0x00, 'RDP',   '读保护: 0x807F=加密, 0x5AA5=不加密'),
        (0x02, 'USER',  'nRST_STOP / WDG_SW'),
        (0x04, 'DATA0', '用户数据 0'),
        (0x06, 'DATA1', '用户数据 1'),
        (0x08, 'WRP0',  '写保护 0'),
        (0x0A, 'WRP1',  '写保护 1'),
        (0x0C, 'WRP2',  '写保护 2'),
        (0x0E, 'WRP3',  '写保护 3'),
    ],
}
DEFAULT_LAYOUT = OPT_LAYOUTS['MM32G0001']


# ============================================================
# FLM 解析
# ============================================================

def parse_flm(filepath):
    with open(filepath, 'rb') as f:
        data = f.read()

    if data[:4] != b'\x7fELF':
        raise ValueError("不是有效的 ELF 文件")

    info = {}

    e_shoff     = struct.unpack_from('<I', data, 32)[0]
    e_shentsize = struct.unpack_from('<H', data, 46)[0]
    e_shnum     = struct.unpack_from('<H', data, 48)[0]
    e_shstrndx  = struct.unpack_from('<H', data, 50)[0]

    base = e_shoff + e_shstrndx * e_shentsize
    shstrtab_off = struct.unpack_from('<I', data, base + 16)[0]

    def sh_name(idx):
        end = data.find(b'\x00', shstrtab_off + idx)
        return data[shstrtab_off + idx:end].decode(errors='replace')

    for i in range(e_shnum):
        o = e_shoff + i * e_shentsize
        sn, s_type, s_flags = struct.unpack_from('<III', data, o)
        s_addr, s_off, s_sz = struct.unpack_from('<III', data, o + 12)
        if sn == 0:
            continue
        name = sh_name(sn)

        if name == 'PrgCode':
            info['prgcode_addr'] = s_addr
            info['prgcode_size'] = s_sz
            info['prgcode_data'] = data[s_off:s_off + s_sz]
        elif name == 'PrgData':
            info['prgdata_addr'] = s_addr
            info['prgdata_size'] = s_sz
            info['prgdata_data'] = data[s_off:s_off + s_sz]
            info['prgdata_wr']   = bool(s_flags & 0x1)
        elif name == 'DevDscr':
            _parse_flash_device(info, data, s_off)

    _parse_symbols(info, data, e_shoff, e_shentsize, e_shnum)

    required = ['Init', 'UnInit', 'EraseChip', 'EraseSector', 'ProgramPage',
                'prgcode_size', 'prgdata_addr', 'prgdata_size',
                'flash_addr', 'flash_size', 'flash_page']
    missing = [r for r in required if r not in info]
    if missing:
        raise ValueError(f"FLM 缺少关键信息: {missing}")

    return info


def _parse_flash_device(info, data, off):
    info['flash_vers']  = struct.unpack_from('<H', data, off)[0]
    name_bytes = data[off + 2: off + 130]
    null_pos = name_bytes.find(b'\x00')
    info['dev_name']    = name_bytes[:null_pos].decode(errors='replace')
    info['flash_type']  = struct.unpack_from('<H', data, off + 130)[0]
    info['flash_addr']  = struct.unpack_from('<I', data, off + 132)[0]
    info['flash_size']  = struct.unpack_from('<I', data, off + 136)[0]
    info['flash_page']  = struct.unpack_from('<I', data, off + 140)[0]


def _parse_symbols(info, data, e_shoff, e_shentsize, e_shnum):
    wanted = {'Init', 'UnInit', 'EraseChip', 'EraseSector', 'ProgramPage', 'ProgramOpt'}

    for i in range(e_shnum):
        o = e_shoff + i * e_shentsize
        if struct.unpack_from('<I', data, o + 4)[0] != 2:
            continue
        sym_off, sym_sz = struct.unpack_from('<II', data, o + 16)
        link = struct.unpack_from('<I', data, o + 24)[0]

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
# 布局计算 (单一真相源)
# ============================================================

def compute_layout(info, algo_start, opt_enabled, opt_addr, opt_size):
    """一次性算出所有头部字段和算法镜像，generate / verify / readme 共用。"""
    I = info

    # PrgCode 段在 ELF 里的地址(通常是 0)，用来说明 PrgData 偏移是相对量
    prgcode_addr = I.get('prgcode_addr', 0)
    prgdata_off  = I['prgdata_addr'] - prgcode_addr   # 相对 PrgCode 起点的偏移

    # 算法镜像 = 32B CMSIS 头 + PrgCode + PrgData (字节流，避免对齐截断)
    blob = bytearray()
    for w in FLASH_ALGO_HEADER:
        blob += struct.pack('<I', w)
    blob += I['prgcode_data']
    blob += I['prgdata_data']
    while len(blob) % 4:                 # 补齐到字对齐，保持长度可被 4 整除
        blob.append(0)

    algo_size = len(blob)                # 严格等于实际字节数

    # 函数入口: st_value 带 Thumb 位(bit0=1)，保留原样供 BLX 使用
    fn = {}
    for name in ['Init', 'UnInit', 'EraseChip', 'EraseSector', 'ProgramPage']:
        fn[name] = algo_start + HEADER_SIZE + I[name]

    # [修正] PrgData 实际落在 32B 头之后
    static_base = algo_start + HEADER_SIZE + prgdata_off
    breakpoint  = algo_start | 1         # LR 退出点 = 算法头里的 BKPT
    prog_buf    = algo_start + BUF_OFFSET
    stack_ptr   = prog_buf

    has_opt = 'ProgramOpt' in I
    opt_prog = algo_start + HEADER_SIZE + I['ProgramOpt'] if has_opt else 0

    hdr = [
        fn['Init'], fn['UnInit'], fn['EraseChip'], fn['EraseSector'], fn['ProgramPage'],
        breakpoint, static_base, stack_ptr,
        prog_buf, algo_start, I['flash_page'],
        I['flash_addr'], I['flash_size'], algo_size,
    ]
    if opt_enabled:
        hdr += [opt_prog, opt_addr, opt_size]

    return {
        'hdr': hdr,
        'blob': bytes(blob),
        'algo_size': algo_size,
        'fn': fn,
        'opt_prog': opt_prog,
        'has_opt': has_opt,
        'prgdata_off': prgdata_off,
        'static_base': static_base,
        'prog_buf': prog_buf,
        'stack_ptr': stack_ptr,
        'breakpoint': breakpoint,
        'bin': b''.join(struct.pack('<I', v) for v in hdr) + bytes(blob),
    }


# ============================================================
# C 文本生成
# ============================================================

def render_c(info, L, algo_start, opt_enabled, opt_addr, opt_size, with_comments=True):
    I = info
    hdr = L['hdr']
    algo_size = L['algo_size']
    flash_kb = I['flash_size'] // 1024
    gap_size = BUF_OFFSET - algo_size
    buf_end  = L['prog_buf'] + I['flash_page']
    min_ram  = max(algo_start + algo_size, buf_end) - algo_start

    # 算法机器码，按 8 字一行
    words = list(struct.unpack('<%dI' % (algo_size // 4), L['blob']))
    blob_lines = []
    for i in range(0, len(words), 8):
        vals = ', '.join(f'0x{w:08X}' for w in words[i:i + 8])
        tag = ''
        if with_comments:
            if i == 0:
                tag = ' // 32B CMSIS 算法头 (BKPT 序列)'
            elif i == 8:
                tag = ' // PrgCode 开始'
        blob_lines.append(f'    {vals},{tag}')
    blob_text = '\n'.join(blob_lines)

    opt_note = f'0x{L["opt_prog"]:08X}' if L['has_opt'] else '无 (未启用)'

    if not with_comments:
        return (f'// Flash+OPT 算法 — {I["dev_name"]}  Flash={flash_kb}KB  OPT={opt_size}B@0x{opt_addr:08X}\n'
                f'const program_target_ext_t flash_algo = {{\n'
                + ''.join(f'    0x{v:08X},\n' for v in hdr)
                + blob_text + '\n};\n')

    return f'''// ============================================================
// Flash + OPT 一体化烧录算法 (扩展格式, 17 字段)
// 芯片: {I["dev_name"]}
// 主Flash: {flash_kb}KB @ 0x{I["flash_addr"]:08X}, 页大小 {I["flash_page"]}B
// OPT:     {opt_size}B @ 0x{opt_addr:08X}  ProgramOpt = {opt_note}
// 算法加载: RAM 0x{algo_start:08X}, 最低需 {min_ram}B RAM
// ============================================================
// 结构 (program_target_ext_t, 68 字节头):
//   [ 0- 4] Init / UnInit / EraseChip / EraseSector / ProgramPage
//   [ 5- 7] breakpoint / static_base / stack_pointer
//   [ 8-10] program_buffer / algo_start / program_buffer_size
//   [11-13] flash_addr / flash_size / algo_size
//   [14-16] opt_program / opt_addr / opt_size
//   [17+  ] algo_blob 算法机器码 ({algo_size}B = {algo_size // 4} words)
// ============================================================
//  内存布局:
//  0x{algo_start:08X} +-----------+  <- algo_start (32B 头 + PrgCode + PrgData)
//  0x{algo_start + algo_size:08X} +-----------+  <- 算法结束, 共 {algo_size}B
//            |    gap    |  {gap_size}B (栈向下增长)
//  0x{L["prog_buf"]:08X} +===========+  <- program_buffer = stack_pointer
//            |  buffer   |  {I["flash_page"]}B
//  0x{buf_end:08X} +-----------+  <- buffer 结束
// ============================================================
const program_target_ext_t flash_algo = {{
    0x{hdr[0]:08X},  // [ 0] Init          - 解锁 Flash, 清错误标志
    0x{hdr[1]:08X},  // [ 1] UnInit        - 恢复默认状态, 锁定
    0x{hdr[2]:08X},  // [ 2] EraseChip     - 全片擦除 {flash_kb}KB
    0x{hdr[3]:08X},  // [ 3] EraseSector   - 扇区擦除
    0x{hdr[4]:08X},  // [ 4] ProgramPage   - 页编程 ({I["flash_page"]}B/页)
    // ==================== [5-7] program_syscall_t ====================
    0x{hdr[5]:08X},  // [ 5] breakpoint    = algo_start | 1 (算法头 BKPT)
    0x{hdr[6]:08X},  // [ 6] static_base   = algo_start + 32 + PrgData.offset(0x{L["prgdata_off"]:X})
    0x{hdr[7]:08X},  // [ 7] stack_pointer = program_buffer (向下增长)
    // ==================== [8-10] 运行时内存布局 (最低需 {min_ram}B) ====================
    0x{hdr[8]:08X},  // [ 8] program_buffer
    0x{hdr[9]:08X},  // [ 9] algo_start
    0x{hdr[10]:08X},  // [10] program_buffer_size = Flash 页大小
    // ==================== [11-13] Flash 参数 ====================
    0x{hdr[11]:08X},  // [11] flash_addr
    0x{hdr[12]:08X},  // [12] flash_size = {flash_kb}KB
    0x{hdr[13]:08X},  // [13] algo_size = {algo_size}B ({algo_size // 4} words)
    // ==================== [14-16] OPT 扩展 ====================
    0x{hdr[14]:08X},  // [14] opt_program  {"" if L["has_opt"] else "<-- 警告: FLM 无 ProgramOpt"}
    0x{hdr[15]:08X},  // [15] opt_addr
    0x{hdr[16]:08X},  // [16] opt_size = {opt_size}B ({opt_size // 2} 个半字)
    // ==================== 算法机器码 ({algo_size}B) ====================
{blob_text}
}};
'''


# ============================================================
# 校验
# ============================================================

def verify(info, L, algo_start, opt_enabled, opt_addr, opt_size):
    errs, warns = [], []
    hdr = L['hdr']
    algo_size = L['algo_size']
    fields = TOTAL_FIELDS if opt_enabled else STD_FIELDS

    if len(L['bin']) != fields * 4 + algo_size:
        errs.append(f"BIN 长度 {len(L['bin'])} != {fields * 4 + algo_size}")
    if algo_size % 4:
        errs.append(f"algo_size {algo_size} 不是 4 的倍数, 固件长度判定会失败")

    # 固件判定: fw_size == algo_size + 68
    if opt_enabled and len(L['bin']) != hdr[13] + 68:
        errs.append(f"扩展格式长度判定失败: {len(L['bin'])} != {hdr[13]} + 68")

    if hdr[6] != algo_start + HEADER_SIZE + L['prgdata_off']:
        errs.append(f"[6] static_base 0x{hdr[6]:08X} 与 PrgData 实际落点不符")

    if opt_enabled:
        if not L['has_opt']:
            errs.append("[14] opt_program = 0: FLM 不含 ProgramOpt, OPT 无法编程")
        else:
            if hdr[14] == hdr[4]:
                errs.append("[14] opt_program 等于 ProgramPage: OPT 不会被解锁/擦除")
            code_lo = algo_start + HEADER_SIZE
            code_hi = code_lo + info['prgcode_size']
            if not (code_lo <= (hdr[14] & ~1) < code_hi):
                errs.append(f"[14] opt_program 0x{hdr[14]:08X} 不在 PrgCode 范围内")
        if hdr[15] != opt_addr:
            errs.append(f"[15] opt_addr 0x{hdr[15]:08X} != 0x{opt_addr:08X}")
        if hdr[16] != opt_size:
            errs.append(f"[16] opt_size {hdr[16]} != {opt_size}")

    if not L['has_opt']:
        warns.append("该 FLM 没有 ProgramOpt, 只能用 ProgramPage 写 0x1FFFF800, 实际写不进去")
    return errs, warns


# ============================================================
# 说明文件
# ============================================================

def generate_readme(info, L, algo_start, opt_enabled, opt_addr, opt_size, base_name):
    I = info
    flash_kb = I['flash_size'] // 1024
    algo_size = L['algo_size']
    fields = TOTAL_FIELDS if opt_enabled else STD_FIELDS
    L_ = []
    a = L_.append

    a("=" * 66)
    a(f"{I['dev_name']}  Flash + OPT 烧录算法 — 离线烧录器使用说明")
    a("=" * 66)
    a("")
    a("1. 基本信息")
    a(f"   芯片型号:      {I['dev_name']}")
    a(f"   算法 RAM 基址:  0x{algo_start:08X}")
    a(f"   算法大小:      {algo_size}B ({algo_size // 4} words)")
    a(f"   BIN 总大小:    {len(L['bin'])}B = 头 {fields * 4}B + 算法 {algo_size}B")
    a("")
    a("2. 主 Flash")
    a(f"   基址 0x{I['flash_addr']:08X}   大小 {flash_kb}KB   页 {I['flash_page']}B")
    a("")
    if opt_enabled:
        a("3. OPT (选项字)")
        a(f"   基址 0x{opt_addr:08X}   长度 {opt_size}B ({opt_size // 2} 个半字)")
        if L['has_opt']:
            a(f"   ProgramOpt = 0x{L['opt_prog']:08X}  (内部: 解锁->擦 OPT 块->逐半字写入)")
        else:
            a("   ProgramOpt = 无  <-- 该 FLM 无法编程 OPT, 烧录器只会报 OPT failed")
        a("")
        a("   半字布局 (MM32G0001):")
        a("   偏移    名称     说明")
        for off, name, desc in OPT_LAYOUTS.get('MM32G0001', DEFAULT_LAYOUT):
            a(f"   0x{off:02X}    {name:6s}   {desc}")
        a("")
    a(f"{4 if opt_enabled else 3}. 烧录器上的文件约定")
    a(f"   0:/download/firmware/{base_name}.bin      本算法 (17 字段)")
    a(f"   0:/download/bin/<固件名>.bin              待烧固件")
    a(f"   0:/download/bin/<固件名>_opt.bin          OPT 数据 (16B, 小端)")
    a("")
    a("   _opt.bin 必须是原始二进制、小端:")
    a("     加密   -> 7F 80 FF FF FF FF FF FF FF FF FF FF FF FF FF FF")
    a("     不加密 -> A5 5A FF FF FF FF FF FF FF FF FF FF FF FF FF FF")
    a("   注意 RDP=0x807F 存成文件是 7F 80, 不是 80 7F。")
    a("")
    a("5. 烧录顺序")
    a("   初始化算法 -> 全片擦除 -> 写主 Flash -> 校验 -> 写 OPT -> 校验 OPT")
    a("   OPT 必须最后写: 写 RDP 后调试口可能被锁, 再擦再写都会失败。")
    return '\n'.join(L_)


# ============================================================
# CLI
# ============================================================

def main():
    p = argparse.ArgumentParser(description='FLM -> C/BIN 转换器 v2 (Flash+OPT)')
    p.add_argument('flm_file')
    p.add_argument('-a', '--algo-start', type=lambda x: int(x, 0), default=ALGO_START)
    p.add_argument('-o', '--output-dir', default=None)
    p.add_argument('--opt-addr', type=lambda x: int(x, 0), default=OPT_ADDR)
    p.add_argument('--opt-size', type=lambda x: int(x, 0), default=OPT_SIZE)
    p.add_argument('--no-opt', action='store_true', help='标准 14 字段输出')
    p.add_argument('--no-comments', action='store_true')
    p.add_argument('--no-bin', action='store_true')
    p.add_argument('--no-readme', action='store_true')
    p.add_argument('--dry-run', action='store_true')
    p.add_argument('--allow-fallback', action='store_true',
                   help='FLM 无 ProgramOpt 时仍生成(用 ProgramPage 占位), 不推荐')
    p.add_argument('--emit-opt-bin', action='store_true',
                   help='额外生成 _opt.bin 模板(加密态)')
    p.add_argument('--rdp', type=lambda x: int(x, 0), default=0x807F,
                   help='_opt.bin 模板里的 RDP 半字, 默认 0x807F(加密)')
    args = p.parse_args()

    if not os.path.exists(args.flm_file):
        print(f"错误: 找不到 {args.flm_file}")
        sys.exit(1)

    opt_enabled = not args.no_opt
    print(f'解析: {os.path.basename(args.flm_file)}')
    info = parse_flm(args.flm_file)

    flash_kb = info['flash_size'] // 1024
    print(f'  芯片:    {info["dev_name"]}')
    print(f'  主Flash: {flash_kb}KB @ 0x{info["flash_addr"]:08X}, 页 {info["flash_page"]}B')
    print(f'  PrgCode: {info["prgcode_size"]}B, PrgData: {info["prgdata_size"]}B @ 0x{info["prgdata_addr"]:X}')
    print('  符号:    ' + ', '.join(f'{s}=0x{info[s]:X}'
                                    for s in ['Init', 'UnInit', 'EraseChip', 'EraseSector', 'ProgramPage']))

    has_opt = 'ProgramOpt' in info
    if has_opt:
        print(f'  OPT:     ProgramOpt=0x{info["ProgramOpt"]:X} ({info.get("ProgramOpt_size", 0)}B)')
    else:
        print('  OPT:     FLM 不含 ProgramOpt 符号')
        if opt_enabled and not args.allow_fallback:
            print('\n[致命] 这个 FLM 没有 ProgramOpt, 无法生成可加密的算法。')
            print('       ProgramPage 只做普通页编程: 不解锁 OPT 区(OPTKEYR)、不擦 OPT 块,')
            print('       写 0x1FFFF800 会被控制器拒绝, 芯片不会进入读保护状态。')
            print('       请改用带 ProgramOpt 的 FLM (本目录: MM32G001-OPT.FLM)。')
            print('       确实要生成占位版本请加 --allow-fallback。')
            sys.exit(2)

    L = compute_layout(info, args.algo_start, opt_enabled, args.opt_addr, args.opt_size)
    c_text = render_c(info, L, args.algo_start, opt_enabled, args.opt_addr, args.opt_size,
                      with_comments=not args.no_comments)
    bin_data = L['bin']

    errs, warns = verify(info, L, args.algo_start, opt_enabled, args.opt_addr, args.opt_size)

    min_ram = max(L['algo_size'], BUF_OFFSET + info['flash_page'])
    print(f'  最低RAM: {min_ram}B')
    print(f'  BIN大小: {len(bin_data)}B (头{len(L["hdr"]) * 4}B + 算法{L["algo_size"]}B)')
    if opt_enabled:
        print(f'  OPT:     program=0x{L["hdr"][14]:08X}  addr=0x{L["hdr"][15]:08X}  size={L["hdr"][16]}')

    out_dir = args.output_dir or os.path.dirname(os.path.abspath(args.flm_file))
    if not args.dry_run:
        os.makedirs(out_dir, exist_ok=True)
    base = os.path.splitext(os.path.basename(args.flm_file))[0]
    suffix = '_ext' if opt_enabled else '_std'
    c_path   = os.path.join(out_dir, base + suffix + '.c')
    bin_path = os.path.join(out_dir, base + suffix + '.bin')
    rd_path  = os.path.join(out_dir, base + suffix + '_说明.txt')
    opt_path = os.path.join(out_dir, base + suffix + '_opt.bin')

    if args.dry_run:
        print('\n--- DRY RUN ---')
        print(f'  C   -> {c_path}')
        print(f'  BIN -> {bin_path} ({len(bin_data)}B)')
        names = ['Init', 'UnInit', 'EraseChip', 'EraseSector', 'ProgramPage',
                 'breakpoint', 'static_base', 'stack_ptr', 'prog_buf', 'algo_start',
                 'buf_size', 'flash_addr', 'flash_size', 'algo_size',
                 'opt_program', 'opt_addr', 'opt_size']
        for i, v in enumerate(L['hdr']):
            print(f'    [{i:2d}] {names[i]:12s} = 0x{v:08X}')
        for e in errs:
            print(f'  ✗ {e}')
        for w in warns:
            print(f'  ! {w}')
        return

    with open(c_path, 'w', encoding='utf-8') as f:
        f.write(c_text)
    print(f'\nC    -> {c_path}')

    if not args.no_bin:
        with open(bin_path, 'wb') as f:
            f.write(bin_data)
        print(f'BIN  -> {bin_path} ({len(bin_data)}B)')

    if args.emit_opt_bin:
        buf = bytearray([0xFF] * args.opt_size)
        buf[0:2] = struct.pack('<H', args.rdp & 0xFFFF)
        with open(opt_path, 'wb') as f:
            f.write(buf)
        print(f'OPT  -> {opt_path} ({len(buf)}B, RDP=0x{args.rdp:04X} -> {buf[0]:02X} {buf[1]:02X})')

    if not args.no_readme:
        with open(rd_path, 'w', encoding='utf-8') as f:
            f.write(generate_readme(info, L, args.algo_start, opt_enabled,
                                    args.opt_addr, args.opt_size, base))
        print(f'说明 -> {rd_path}')

    if errs:
        print('\n✗ 校验失败:')
        for e in errs:
            print(f'   - {e}')
        sys.exit(3)
    for w in warns:
        print(f'! {w}')
    print(f'✓ 校验通过  static_base=0x{L["hdr"][6]:08X}  algo_size={L["algo_size"]}B')


if __name__ == '__main__':
    main()
