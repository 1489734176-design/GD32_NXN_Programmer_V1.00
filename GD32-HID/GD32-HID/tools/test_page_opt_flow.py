"""Compare linked programmer builds with real burn/verify/OPT drivers and simulated target responses."""
import argparse
import runpy
import struct
import sys
from pathlib import Path


def burn(harness, image, algorithm, payload, option, ch, case):
    from unicorn import UC_HOOK_CODE
    from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_PC, UC_ARM_REG_LR, UC_ARM_REG_SP

    board = harness['Firmware'](image)
    u, symbols = board.uc, board.symbols
    regs = UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3
    header = struct.unpack_from('<14I', algorithm)
    flash_base, flash_size = header[11:13]
    extended = len(algorithm) == 68 + header[13]
    opt = struct.unpack_from('<3I', algorithm, 56) if extended else None
    enabled = case != 'plain' and extended
    wanted = option.ljust(opt[2], b'\xff') if opt else None
    flash = bytearray(b'\xff' * flash_size)
    ram = bytearray(8192)
    files = {b'0:/download/firmware/loader.bin': algorithm, b'0:/download/bin/test.bin': payload}
    if case != 'missing':
        files[b'0:/download/bin/test_opt.bin'] = option
    opened, verified, trace, opt_calls = {}, bytearray(), [], []
    tick = launched = 0
    debits = 0

    def hook(uc, address, size, name):
        nonlocal tick, launched, debits
        a, b, c, d = (uc.reg_read(r) for r in regs)
        ret = 0
        if name == 'f_open':
            path = bytes(uc.mem_read(b, 64)).partition(b'\0')[0]
            trace.append(('open', path))
            if path not in files:
                ret = 4
            else:
                opened[a] = files[path], 0
                uc.mem_write(a + 12, struct.pack('<I', len(files[path])))
        elif name == 'f_read':
            data, pos = opened[a]
            chunk = data[pos:pos+c]
            if chunk:
                uc.mem_write(b, chunk)
            uc.mem_write(d, struct.pack('<I', len(chunk)))
            opened[a] = data, pos + len(chunk)
        elif name == 'swd_write_memory':
            offset = a - header[9]
            assert 0 <= offset <= len(ram) - c
            data = bytes(uc.mem_read(b, c))
            ram[offset:offset+c] = data
            trace.append(('stage', a, data))
            ret = 1
        elif name == 'swd_read_memory':
            pos = a - flash_base
            trace.append(('read', a, c))
            if 0 <= pos <= flash_size-c:
                data = bytearray(flash[pos:pos+c])
                if case == 'mismatch' and pos+c == len(payload):
                    data[-1] ^= 1
                uc.mem_write(b, bytes(data))
                verified.extend(data)
                ret = 1
            else:
                assert opt and a == opt[1] and opt_calls
        elif name in ('swd_flash_syscall_exec', 'swd_flash_syscall_start'):
            buf, last = struct.unpack('<II', uc.mem_read(uc.reg_read(UC_ARM_REG_SP), 8))
            trace.append(('call', name, b, c, d, buf, last))
            assert struct.unpack('<3I', uc.mem_read(a, 12)) == header[5:8]
            ret = 1
            if b == header[4]:
                launched = tick
                start, pos = buf - header[9], c - flash_base
                assert buf == header[8] and 0 <= pos <= flash_size-d
                flash[pos:pos+d] = ram[start:start+d]
                if case == 'start_fail' or (ch is None and case == 'page_error'):
                    ret = 0
            elif opt and b == opt[0]:
                assert enabled and c == opt[1] and d == opt[2] and buf == header[8] and last == 0
                assert verified == payload and not opt_calls
                data = bytes(ram[buf-header[9]:buf-header[9]+d])
                assert data == wanted
                opt_calls.append(data)
                ret = int(case != 'opt_fail')
            else:
                assert b in header[:3]
        elif name == 'swd_page_poll':
            if tick-launched < 9:
                ret = harness['PAGE_RUNNING']
            else:
                uc.mem_write(a, struct.pack('<I', int(case == 'page_error')))
                ret = harness['PAGE_HALTED']
        elif name == 'xTaskGetTickCount':
            ret = tick
        elif name == 'vTaskDelay':
            tick += a
            if a > 4:
                trace.append(('power delay', a))
        elif name == 'swd_read_dp':
            uc.mem_write(b, b'\0'*4)
            ret = 1
        elif name in ('swd_write_dp', 'swd_write_ap', 'swd_last_ack', 'target_connect_halt', 'swd_off', 'swd_set_target_state_hw'):
            ret = 1
            if name in ('target_connect_halt', 'swd_set_target_state_hw'):
                trace.append((name, a if name == 'swd_set_target_state_hw' else 0))
        elif name in ('delaymS', 'target_power'):
            trace.append((name, a, b if name == 'target_power' else 0))
        elif name == 'burn_quota_finish':
            trace.append(('finish', a))
            debits += a
        uc.reg_write(UC_ARM_REG_R0, ret)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    boundaries = ('f_open f_read f_close swd_write_memory swd_read_memory swd_flash_syscall_exec '
                  'swd_flash_syscall_start swd_page_poll swd_last_ack xTaskGetTickCount vTaskDelay delaymS '
                  'target_power target_connect_halt swd_off swd_set_target_state_hw swd_set_target_reset '
                  'swd_read_dp swd_write_dp swd_write_ap burn_quota_select burn_quota_fault burn_quota_enabled '
                  'burn_quota_begin burn_quota_finish draw_main show_counts show_download_status '
                  'show_download_error UI_ProgressStatus OLED_Refresh buzzer_beep')
    for name in boundaries.split():
        addr = symbols[name] & ~1
        u.hook_add(UC_HOOK_CODE, hook, name, begin=addr, end=addr)
    u.mem_map(0x40010000, 0x10000)
    u.mem_write(symbols['app_data'], b'\x02\x02' + b'test.bin'.ljust(32,b'\0') + b'loader.bin'.ljust(32,b'\0') + bytes([enabled]))
    if ch is None:
        board.call_symbol('mode4', budget=8_000_000)
    else:
        u.mem_write(symbols['g_active_burns'], b'\1')
        board.call_symbol('channel_burn', ch, budget=250_000)
    ok = case in ('protected', 'plain', 'standard')
    assert debits == int(ok) and len(opt_calls) == int(case in ('protected', 'opt_fail')), (ch, case, debits, opt_calls)
    state = u.mem_read(symbols['g_ui_state_b' if ch == 1 else 'g_ui_state'], 1)[0]
    assert state == (3 if ok else 2), (ch, case, state)
    if ok:
        assert verified == payload
    return trace, opt_calls, debits, state


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('axfs', type=Path, nargs='+')
    parser.add_argument('--unicorn-path', type=Path)
    args = parser.parse_args()
    if args.unicorn_path:
        sys.path.insert(0, str(args.unicorn_path.resolve()))
    harness = runpy.run_path(str(Path(__file__).with_name('test_burn_time.py')))
    sample = Path(__file__).resolve().parents[3] / 'FLASH文件/RCT6/download'
    extended = (sample / 'firmware/MM32SPIN0230-OPT_ext.bin').read_bytes()
    standard = (sample / 'firmware/MM32SPIN0230_32.bin').read_bytes()
    data = (sample / 'bin/SPIN0230_DEMO.bin').read_bytes()[:1026]
    option = (sample / 'bin/SPIN0230_DEMO_opt.bin').read_bytes()
    images = [harness['read_axf'](p) for p in args.axfs]
    for ch in (None, 0, 1):
        for case in ('protected','plain','standard','missing','mismatch','start_fail','page_error','opt_fail'):
            traces = [burn(harness, image, standard if case == 'standard' else extended, data, option, ch, case) for image in images]
            assert all(trace == traces[0] for trace in traces[1:]), (ch, case)
        print('PASS', 'single A' if ch is None else 'dual '+'AB'[ch], 'verified tail, OPT order/arguments/padding, disabled OPT and failure gates')


if __name__ == '__main__':
    main()
