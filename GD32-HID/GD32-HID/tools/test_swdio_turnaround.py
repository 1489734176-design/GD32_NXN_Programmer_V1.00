"""Compare the ARMCC5 SWD_Transfer GPIO trace to the vendor gpio_init reference.

Example: python test_swdio_turnaround.py --arm-bin <ARMCC/bin> --cmsis <CMSIS/Core/Include>
Requires Python's unicorn package. Uses only simulated GPIO; no programmer is opened.
"""
import argparse
import re
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
PORTS = (0x40010800, 0x40010C00, 0x40011000)  # GPIO A, B, C
RETURN = 0x0800F000
WORD = 0x2000E000
WRAPPER = '''#include "DAP_config.h"
#include "DAP.h"
unsigned test_transfer(unsigned ch, unsigned request, unsigned *data) {
    DAP_SelectChannel(ch);
    DAP_Data.fast_clock = (request >> 8) & 1U;
    DAP_Data.swd_conf.data_phase = (request >> 9) & 1U;
    DAP_Data.clock_delay = (CPU_CLOCK / 2U) / DAP_DEFAULT_SWJ_CLOCK - IO_PORT_WRITE_CYCLES;
    DAP_Data.swd_conf.turnaround = 1U;
    return (request & 1024U) ? SWD_TransferBlockData(request & 15U, data) : SWD_Transfer(request & 15U, data);
}
'''


def build(path, opt):
    mdk = PROJECT / 'DAPlink/MDK-ARM'
    cc = ET.parse(mdk / 'DAPlink.uvprojx').find('.//Cads/VariousControls')
    inc = [(mdk / d).resolve() for d in re.split(r'[;]', cc.findtext('IncludePath'))]
    inc.extend([mdk / 'RTE/_DAPlink', opt.cmsis])
    compiler = [opt.arm_bin / 'armcc.exe', '--cpu=Cortex-M3', '--c99', '-O2', '--split_sections',
                '--library_type=microlib', '-D_RTE_', *['-D'+d for d in cc.findtext('Define').split(',')],
                *['-I'+str(d) for d in inc]]
    (path / 'test.c').write_text(WRAPPER, encoding='ascii')
    def run(cmd):
        subprocess.run(cmd, cwd=path, check=True, stdout=subprocess.PIPE)
    for name, src in [('test', path/'test.c'),
                      ('swd', PROJECT/'USER/DAPlink/SW_DP.c'), ('dap', PROJECT/'USER/DAPlink/DAP.c'),
                      *[(driver, PROJECT/f'GD32F10x_Firmware_Library/GD32F10x_standard_peripheral/Source/gd32f10x_{driver}.c')
                        for driver in ('gpio', 'rcu')]]:
        run([*compiler, '-c', src, '-o', name+'.o'])
    run([opt.arm_bin / 'armlink.exe', '--cpu=Cortex-M3', '--library_type=microlib', '--entry=test_transfer',
         '--ro-base=0x08000000', '--rw-base=0x20000000', 'test.o', 'dap.o', 'gpio.o', 'rcu.o', 'swd.o', '-o', 'test.axf'])
    return (path/'test.axf').read_bytes()


def simulate(image, ch, request, ack, bad_parity):
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_READ, UC_MEM_WRITE
    from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_PC
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    for addr in (0x08000000, 0x20000000, 0x40010000):
        uc.mem_map(addr, 0x10000)
    entry = struct.unpack_from('<I', image, 24)[0]
    sh = struct.unpack_from('<I', image, 32)[0]
    stride, n = struct.unpack_from('<HH', image, 46)
    for i in range(n):
        _, kind, flags, addr, off, length = struct.unpack_from('<6I', image, sh + i*stride)
        if kind == 1 and flags & 2 and length:  # Copy initialized sections to execution (not load) addresses.
            uc.mem_write(addr, image[off:off+length])
    pin = 10 if ch == 0 else 6
    word = 0x9F0482AD
    bits = [(ack >> i) & 1 for i in range(3)]
    if ack == 1 and request & 2:
        bits += [(word >> i) & 1 for i in range(32)] + [(word.bit_count() & 1) ^ bad_parity]
    pack = lambda v: struct.pack('<I', v)
    uc.mem_write(WORD, pack(0x56ABC781))
    uc.mem_write(PORTS[2], struct.pack('<II', 0x76543210, 0xFEDCBA98))
    for reg, value in [(UC_ARM_REG_SP, WORD-256), (UC_ARM_REG_LR, RETURN | 1),
                       (UC_ARM_REG_R0, ch), (UC_ARM_REG_R1, request), (UC_ARM_REG_R2, WORD)]:
        uc.reg_write(reg, value)
    trace = []
    count = 0
    def instruction(*args):
        nonlocal count
        count += 1
    def gpio(uc, access, addr, size, value, _):
        if access != UC_MEM_WRITE and addr == PORTS[2] + 8:
            uc.mem_write(addr, pack(bits.pop(0) << pin))
        trace.append((access, addr, size, value if access == UC_MEM_WRITE else None))
    uc.hook_add(UC_HOOK_CODE, instruction)
    uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, gpio, begin=0x40010000, end=0x4001ffff)
    uc.emu_start(entry | 1, RETURN, count=30000)
    assert uc.reg_read(UC_ARM_REG_PC) == RETURN and not bits
    return (uc.reg_read(UC_ARM_REG_R0), bytes(uc.mem_read(WORD, 4)), trace), count


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--arm-bin', required=True, type=Path)
    p.add_argument('--cmsis', required=True, type=Path)
    p.add_argument('--unicorn-dir', type=Path, help='optional directory of an installed unicorn package')
    opt = p.parse_args()
    opt.arm_bin, opt.cmsis = opt.arm_bin.resolve(), opt.cmsis.resolve()
    if opt.unicorn_dir: sys.path.append(str(opt.unicorn_dir.resolve()))
    with tempfile.TemporaryDirectory(prefix='nx-swdio-trace-') as tmp:
        image = build(Path(tmp), opt)
    checks = 0
    for ch in (0, 1):
        for fast in (0, 1):
            for data_phase in (0, 1):
                for request in (13, 15):  # word write and read
                    for ack, parity in [(1, 0), (2, 0), (4, 0), (7, 0)] + ([(1, 1)] if request == 15 else []):
                        params = (ch, request | (fast << 8) | (data_phase << 9), ack, parity)
                        before, old_count = simulate(image, *params)
                        after, count = simulate(image, ch, params[1] | 1024, ack, parity)
                        assert before == after, params  # all GPIO reads/writes, data and ACK
                        if not fast:  # The programmer uses the slow-clock routine; fast mode is unmodified.
                            assert count < old_count, (params, old_count, count)
                        if ack == 1 and not (parity or fast or data_phase):
                            print(f'Channel {"AB"[ch]}, {"read" if request == 15 else "write"}: {old_count} -> {count} Thumb instructions')
                        checks += 1
    print(f'{checks} transfer traces match the vendor reference (counts are instructions, not board timings)')


if __name__ == '__main__':
    main()
