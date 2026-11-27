#!/usr/bin/env python3
"""Run linked Keil AXF burn timing and dual-channel probe regressions in Unicorn.

Usage: python -B test_burn_time.py DAPlink.axf [--unicorn-path PATH]
"""

import argparse
import struct
import sys
from collections import deque
from pathlib import Path
from typing import NamedTuple

DWT_CYCCNT = 0xE0001004
CYCLES_PER_MS = 72_000
U32_MASK = (1 << 32) - 1
OTHER, CONNECT, ERASE, IO, LOAD, PAGE, VERIFY, OPT, QUOTA, SWD_LOCK, FS_LOCK, QUOTA_LOCK = range(12)
STAGE_COUNT = QUOTA_LOCK + 1
RESULT = struct.Struct("<16QBBB5x")  # AAPCS: 12 stage counters, 3 timestamps, miss_wait, 3 flags


class Result(NamedTuple):
    cycles: tuple[int, ...]
    started: int
    ended: int
    ready: int
    miss_wait: int
    valid: int
    ok: int
    dual: int


def read_axf(path):
    data = path.read_bytes()
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    ident, _, machine, _, _, phoff, shoff, _, _, phsize, phnum, shsize, shnum, _ = header
    if ident[:6] != b"\x7fELF\x01\x01" or machine != 40:
        raise ValueError("Expected a little-endian ARM ELF32 AXF")
    loads = []
    for i in range(phnum):
        kind, offset, address, _, file_size, *_ = struct.unpack_from("<8I", data, phoff + i * phsize)
        if kind == 1 and file_size:
            loads.append((address, data[offset:offset + file_size]))
    sections = [struct.unpack_from("<10I", data, shoff + i * shsize) for i in range(shnum)]
    symbols = {}
    for section in sections:
        if section[1] != 2:  # SHT_SYMTAB
            continue
        _, _, _, _, offset, size, link, _, _, entry_size = section
        strings = sections[link][4]
        for entry in range(offset, offset + size, entry_size):
            name, value, _, info, _, index = struct.unpack_from("<IIIBBH", data, entry)
            if name and index and (info & 0xF) in (1, 2):  # STT_OBJECT / STT_FUNC, including statics
                start = strings + name
                symbol = data[start:data.index(b"\0", start)]
                symbols[symbol.decode("ascii", errors="replace")] = value
    return loads, symbols


class Firmware:
    STOP = 0x10000
    SNAPSHOT = 0x20010000
    STACK_TOP = 0x20012000

    def __init__(self, image):
        import unicorn
        from unicorn import arm_const

        self.regs = arm_const
        self.uc = unicorn.Uc(unicorn.UC_ARCH_ARM, unicorn.UC_MODE_THUMB | unicorn.UC_MODE_MCLASS)
        self.uc.mem_map(self.STOP, 0x1000)
        self.uc.mem_map(0x08000000, 0x40000)
        self.uc.mem_map(0x20000000, 0x12000)  # Firmware SRAM plus private test scratch/stack
        self.uc.mem_map(0xE0000000, 0x10000)  # DWT and Cortex-M registers used by FreeRTOS
        loads, self.symbols = image
        for address, payload in loads:
            self.uc.mem_write(address, payload)
        # Bypass startup: SRAM starts zeroed, the relevant initialized datum is SystemCoreClock.
        self.uc.mem_write(self.symbols["SystemCoreClock"], struct.pack("<I", CYCLES_PER_MS * 1000))

    def call_symbol(self, name, *args, stop=None, budget=20_000):
        r = self.regs
        self.uc.reg_write(r.UC_ARM_REG_SP, self.STACK_TOP)
        self.uc.reg_write(r.UC_ARM_REG_LR, self.STOP | 1)
        for register, value in zip((r.UC_ARM_REG_R0, r.UC_ARM_REG_R1), args):
            self.uc.reg_write(register, value)
        self.uc.emu_start(self.symbols[name] | 1, self.STOP, count=budget)
        pc = self.uc.reg_read(r.UC_ARM_REG_PC)
        assert pc == (self.STOP if stop is None else stop), f"{name}: unexpected stop at 0x{pc:x}"
        return self.uc.reg_read(r.UC_ARM_REG_R0)

    def call(self, name, *args):
        return self.call_symbol(f"burn_time_{name}", *args)

    def at_cycles(self, cycles, name, *args):
        self.uc.mem_write(DWT_CYCCNT, struct.pack("<I", cycles & U32_MASK))
        return self.call(name, *args)

    def at(self, ms, name, *args):
        return self.at_cycles(ms * CYCLES_PER_MS, name, *args)

    def snapshot(self):
        revision = self.call("snapshot", self.SNAPSHOT)
        data = self.uc.mem_read(self.SNAPSHOT, 2 * RESULT.size)
        return revision, tuple(
            Result(row[:STAGE_COUNT], *row[STAGE_COUNT:]) for row in RESULT.iter_unpack(data)
        )

    def pair_ms(self):
        return self.call("pair_ms", self.SNAPSHOT)


def result_ms(start, end, counts):
    return Result(tuple(n * CYCLES_PER_MS for n in counts), start * CYCLES_PER_MS,
                  end * CYCLES_PER_MS, 0, 0, 1, 1, 1)


def test_exclusive_and_rearm(f):
    f.at(5, "rearmed", 0)  # Can't re-arm a result that hasn't been published.
    version, rows = f.snapshot()
    assert version == 0 and rows == (Result((0,) * STAGE_COUNT, 0, 0, 0, 0, 0, 0, 0),) * 2
    f.at(10, "begin", 0, 1)
    f.at(12, "stage", 0, CONNECT)
    f.at(20, "stage", 0, ERASE)
    f.at(23, "begin", 1, 1)
    f.at(25, "stage", 0, IO)
    f.at(25, "stage", 1, IO)
    f.at(28, "stage", 0, LOAD)
    f.at(29, "stage", 1, PAGE)
    previous = f.at(32, "stage", 0, PAGE)
    assert previous == LOAD
    f.at(35, "stage", 0, IO)
    f.at(38, "stage", 0, VERIFY)
    f.at(40, "stage", 1, VERIFY)
    f.at(42, "stage", 0, OPT)
    f.at(45, "stage", 0, QUOTA)
    f.at(47, "stage", 0, SWD_LOCK)
    f.at(49, "stage", 0, OTHER)
    f.at(52, "end", 0, 1)
    version, (a, b) = f.snapshot()
    assert version == 1 and not b.valid and f.pair_ms() == 0
    assert a == result_ms(10, 52, (5, 8, 5, 6, 4, 3, 4, 3, 2, 2, 0, 0)), a
    f.at(60, "end", 1, 1)
    version, rows = f.snapshot()
    assert version == 2 and rows[0] == a
    b = rows[1]
    assert b == result_ms(23, 60, (2, 0, 0, 4, 0, 11, 20, 0, 0, 0, 0, 0)), b
    assert all(sum(r.cycles) == r.ended - r.started for r in rows)
    assert f.pair_ms() == 50  # Union, not overlap or sum of the individual totals.

    f.at(70, "rearmed", 0)
    version, rearmed = f.snapshot()
    assert version == 3 and f.call("revision") == 3
    assert rearmed == (a._replace(ready=70 * CYCLES_PER_MS), b)
    assert rearmed[0].ready - a.ended == 18 * CYCLES_PER_MS and f.pair_ms() == 50
    f.at(90, "rearmed", 0)
    assert f.snapshot() == (3, rearmed)


def test_pair_edges(image):
    cases = (
        ("overlap with B failed", (10, 24, 1), (18, 30, 0), 0),
        ("overlap with A failed", (18, 30, 0), (10, 24, 1), 0),
        ("separated successes", (10, 20, 1), (21, 30, 1), 0),
        ("touching successes", (20, 30, 1), (10, 20, 1), 0),
        ("B starts first", (18, 30, 1), (10, 24, 1), 20),
    )
    for label, a, b, expected in cases:
        f = Firmware(image)
        events = []
        for channel, (start, end, ok) in enumerate((a, b)):
            events.extend(((start, "begin", channel, 1), (end, "end", channel, ok)))
        for event in sorted(events):
            f.at(*event)
        version, rows = f.snapshot()
        assert version == 2 and all(r.valid for r in rows), label
        assert (rows[0].ok, rows[1].ok) == (a[2], b[2]), label
        assert f.pair_ms() == expected, label


def test_sampled_wrap(f):
    wrap = 1 << 32
    f.at_cycles(wrap - 40, "sample")
    f.at_cycles(wrap - 10, "begin", 0, 1)
    f.at_cycles(wrap + 20, "stage", 0, PAGE)
    # No stage transition across the second wrap; samples must keep the epoch current.
    for now in (wrap + wrap // 2, 2 * wrap - 10, 2 * wrap + 50):
        f.at_cycles(now, "sample")
    f.at_cycles(2 * wrap + 75, "end", 0, 1)
    version, (a, _) = f.snapshot()
    counts = [0] * STAGE_COUNT
    counts[OTHER], counts[PAGE] = 30, wrap + 55
    assert version == 1 and a == Result(tuple(counts), wrap - 10, 2 * wrap + 75, 0, 0, 1, 1, 1), a
    delta = a.ended - a.started
    assert sum(a.cycles) == delta
    assert f.call("ms", delta & U32_MASK, delta >> 32) == delta // CYCLES_PER_MS


def test_probe_wait_overlap(f, waiter):
    wrap = 1 << 32
    peer = waiter ^ 1
    f.at_cycles(wrap - 100, "sample")
    f.at_cycles(wrap - 60, "begin", waiter, 1)

    # The probe already owns the mutex when the wait starts: charge only 10 cycles.
    f.at_cycles(wrap - 40, "probe_begin")
    f.at_cycles(wrap - 30, "stage", waiter, SWD_LOCK)
    f.at_cycles(wrap - 20, "probe_end", peer, 0)
    # A second probe in the same wait crosses DWT wrap: charge all 12 cycles.
    f.at_cycles(wrap - 5, "probe_begin")
    f.at_cycles(wrap + 7, "probe_end", peer, 0)

    f.at_cycles(wrap + 11, "probe_begin")
    f.at_cycles(wrap + 14, "probe_end", peer, 1)  # Found: no charge.
    f.at_cycles(wrap + 15, "probe_begin")
    f.at_cycles(wrap + 19, "probe_end", waiter, 0)  # Same channel: no charge.
    for stage, enter, start, end in ((FS_LOCK, 20, 21, 27), (QUOTA_LOCK, 30, 32, 37),
                                     (OTHER, 45, 46, 53)):
        f.at_cycles(wrap + enter, "stage", waiter, stage)
        f.at_cycles(wrap + start, "probe_begin")
        f.at_cycles(wrap + end, "probe_end", peer, 0)  # Not an SWD wait.

    f.at_cycles(wrap + 55, "stage", waiter, SWD_LOCK)
    f.at_cycles(wrap + 60, "end", waiter, 0)
    version, rows = f.snapshot()
    result = rows[waiter]
    counts = [0] * STAGE_COUNT
    counts[OTHER], counts[SWD_LOCK], counts[FS_LOCK], counts[QUOTA_LOCK] = 40, 55, 10, 15
    assert version == 1 and not rows[peer].valid, rows
    assert result == Result(tuple(counts), wrap - 60, wrap + 60, 0, 22, 1, 0, 1), result
    assert sum(result.cycles) == result.ended - result.started
    assert 0 < result.miss_wait < result.cycles[SWD_LOCK]  # Subset, not another stage.

    # An idle worker still has a last result; another probe must not change it.
    f.at_cycles(wrap + 62, "probe_begin")
    f.at_cycles(wrap + 72, "probe_end", peer, 0)
    assert f.snapshot() == (version, rows)


def test_mode_per_burn(f):
    for revision, dual in enumerate((1, 0, 1), 1):
        start = revision * 10
        f.at(start, "begin", 0, dual)
        f.at(start + 3, "end", 0, 1)
        version, (a, _) = f.snapshot()
        assert version == revision and a.valid and a.dual == dual, a


PROBE_NO_REPLY, PROBE_PRESENT, PROBE_DEFERRED = range(3)


class ProbeScript(Firmware):
    """Drive the real worker/probe and verify the ordered boundary calls."""

    TIMING_OBSERVERS = ("burn_time_probe_begin", "burn_time_probe_end", "burn_time_rearmed")
    BOUNDARIES = TIMING_OBSERVERS + (
        "vTaskDelay", "custombulk_is_busy", "xQueueGenericReceive", "xQueueGenericSend",
        "DAP_SelectChannel", "swd_off", "target_power", "swd_init_debug", "delaymS", "channel_burn",
    )
    MUTEX = Firmware.SNAPSHOT  # Opaque handle used only by the mock FreeRTOS calls.
    STOP_WORKER = object()

    def __init__(self, image, ch):
        import unicorn

        super().__init__(image)
        self.ch = ch
        self.steps = deque()
        self.position = 0
        r = self.regs
        self.arg_regs = (r.UC_ARM_REG_R0, r.UC_ARM_REG_R1, r.UC_ARM_REG_R2, r.UC_ARM_REG_R3)
        self.write("g_swd_mutex", struct.pack("<I", self.MUTEX))
        self.write("g_channel_b_enable", b"\x01")
        # app_TypeDef: mode, head, bin_name[32], firmware_name[32].
        self.write("app_data", b"\x02\0" + b"firmware.bin".ljust(32, b"\0") + b"algo.bin\0")
        for name in self.BOUNDARIES:
            addr = self.symbols[name] & ~1
            if name not in self.TIMING_OBSERVERS:
                self.uc.mem_write(addr, b"\x70\x47")  # Thumb BX LR, only in emulator memory.
            self.uc.hook_add(unicorn.UC_HOOK_CODE, self.dispatch, name, begin=addr, end=addr)

    def write(self, symbol, data):
        self.uc.mem_write(self.symbols[symbol], data)

    def expect(self, name, *args, reply=None, action=None):
        self.steps.append((name, args, reply, action))

    def dispatch(self, uc, address, size, name):
        self.position += 1
        assert self.steps, f"ch {self.ch}: unexpected {name} after the script ended"
        expected, args, reply, action = self.steps.popleft()
        actual = tuple(uc.reg_read(r) for r in self.arg_regs[:len(args)])
        assert (name, actual) == (expected, args), (
            f"ch {self.ch}, step {self.position}: expected {expected}{args}, got {name}{actual}"
        )
        if action is self.STOP_WORKER:
            uc.emu_stop()
            return
        if action:
            action()
        if reply is not None:
            uc.reg_write(self.regs.UC_ARM_REG_R0, reply)

    def scan(self, tries, *, reply_at=None, waiter=None, request_at=1):
        def check_waiting_clear():
            # Probes must acquire the mutex without advertising their own burn request.
            assert self.uc.mem_read(self.symbols["g_swd_burn_waiting"], 2) == b"\0\0"

        def request_swd():
            flags = bytearray(2)
            flags[waiter] = 1
            self.write("g_swd_burn_waiting", bytes(flags))

        self.expect("xQueueGenericReceive", self.MUTEX, 0, U32_MASK, 0,
                    reply=1, action=check_waiting_clear)
        self.expect("DAP_SelectChannel", self.ch)
        self.expect("burn_time_probe_begin")
        self.expect("swd_off")
        self.expect("target_power", self.ch, 1)
        for attempt in range(1, tries + 1):
            self.expect("swd_init_debug", reply=int(attempt == reply_at),
                        action=request_swd if waiter is not None and attempt == request_at else None)
            if attempt < tries or (tries == 50 and reply_at != tries):
                self.expect("delaymS", 1)
        self.expect("swd_off")
        self.expect("target_power", self.ch, 0)
        self.expect("burn_time_probe_end", self.ch, int(tries == reply_at))
        self.expect("xQueueGenericSend", self.MUTEX, 0, 0, 0, reply=1,
                    action=lambda: self.write("g_swd_burn_waiting", b"\0\0"))

    def idle_scan(self, tries, **kwargs):
        self.expect("vTaskDelay", 200)
        self.expect("custombulk_is_busy", reply=0)
        self.scan(tries, **kwargs)

    def burn_succeeds(self):
        state = "g_ui_state_b" if self.ch else "g_ui_state"
        self.expect("channel_burn", self.ch,
                    action=lambda: self.write(state, b"\x03"))  # UI_PROGRESS_SUCCESS

    def run_probe(self, may_yield):
        result = self.call_symbol("channel_probe", self.ch, may_yield)
        assert not self.steps, f"probe returned before {self.steps[0][0]}"
        return result

    def run_worker(self, final_delay=200):
        # Stop at a scheduled delay; do not add a test exit to firmware.
        self.expect("vTaskDelay", final_delay, action=self.STOP_WORKER)
        self.call_symbol("channel_worker", self.ch, stop=self.symbols["vTaskDelay"] & ~1,
                         budget=250_000)
        assert not self.steps, f"worker stopped before {self.steps[0][0]}"


def test_probe_policy(image, ch):
    peer = ch ^ 1
    cases = (
        ("uncontended", 1, 50, PROBE_NO_REPLY, {}),
        ("peer queues after several attempts", 1, 7, PROBE_DEFERRED,
         dict(waiter=peer, request_at=7)),
        ("own channel does not count", 1, 50, PROBE_NO_REPLY, dict(waiter=ch)),
        ("peer queues on final miss", 1, 50, PROBE_NO_REPLY, dict(waiter=peer, request_at=50)),
        ("must not yield", 0, 50, PROBE_NO_REPLY, dict(waiter=peer)),
        ("success wins over queued peer", 1, 1, PROBE_PRESENT, dict(waiter=peer, reply_at=1)),
    )
    for label, may_yield, tries, expected, kwargs in cases:
        f = ProbeScript(image, ch)
        f.scan(tries, **kwargs)
        assert f.run_probe(may_yield) == expected, (ch, label)


def test_worker_deferral_limit(image, ch):
    f = ProbeScript(image, ch)
    peer = ch ^ 1
    # Persistent contention: initial full miss, three deferrals, forced full miss, repeat.
    for tries in (50, 1, 1, 1, 50, 1, 1, 1, 50):
        f.idle_scan(tries, waiter=peer)
    f.run_worker()  # No channel_burn or burn_time_rearmed is permitted by this script.


def test_worker_validates_early_reply(image, ch):
    f = ProbeScript(image, ch)
    peer = ch ^ 1
    f.idle_scan(50, waiter=peer)
    f.idle_scan(1, waiter=peer, reply_at=1)
    f.scan(50, waiter=peer)  # The early reply is transient: the full validation misses.
    f.idle_scan(50, waiter=peer)  # A failed validation must not reuse the deferral allowance.
    f.idle_scan(1, waiter=peer, reply_at=1)
    f.scan(2, waiter=peer, reply_at=2)  # A real early responder passes validation despite contention.
    f.burn_succeeds()
    f.run_worker(final_delay=500)


def test_worker_insert_and_remove(image, ch):
    f = ProbeScript(image, ch)
    peer = ch ^ 1
    f.write("g_programming_active", b"\x01")  # The peer is already in a burn session.
    f.idle_scan(50, waiter=peer)
    f.idle_scan(1, waiter=peer)
    # A chip is inserted during the peer burn. It takes the full power-on window to reply;
    # shortened scans power it down before that reply, until the mandatory full scan.
    for tries in (1, 1, 50):
        f.idle_scan(tries, waiter=peer, reply_at=50)
    f.scan(50, waiter=peer, reply_at=50)  # A second full scan must validate the chip.

    f.burn_succeeds()
    f.expect("vTaskDelay", 500)
    # The first miss is not removal. A late reply cancels it; two more full misses are needed.
    for reply_at in (None, 50, None, None):
        f.scan(50, waiter=peer, reply_at=reply_at)
        f.expect("vTaskDelay", 200)
    f.expect("burn_time_rearmed", ch)
    f.idle_scan(50, waiter=peer)  # Re-armed worker starts with a full scan again.
    f.run_worker()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("axf", type=Path, help="linked firmware AXF with symbols")
    parser.add_argument("--unicorn-path", type=Path, help="directory containing the Unicorn package")
    parser.add_argument("--timing-only", action="store_true", help="skip probe regressions on older AXFs")
    args = parser.parse_args()
    if args.unicorn_path:
        sys.path.insert(0, str(args.unicorn_path.resolve()))
    image = read_axf(args.axf)
    test_exclusive_and_rearm(Firmware(image))
    print("PASS exclusive stage totals and one-shot re-arm outside burn time")
    test_pair_edges(image)
    print("PASS successful overlap, failed pair, disjoint and touching intervals")
    test_sampled_wrap(Firmware(image))
    print("PASS sampled DWT wraps and 64-bit totals")
    for waiter in (0, 1):
        test_probe_wait_overlap(Firmware(image), waiter)
    print("PASS probe/SWD wait overlap, exclusions, and exclusive lock totals on both channels")
    test_mode_per_burn(Firmware(image))
    print("PASS mode flag overwritten on each burn")
    if not args.timing_only:
        for ch in (0, 1):
            test_probe_policy(image, ch)
            test_worker_deferral_limit(image, ch)
            test_worker_validates_early_reply(image, ch)
            test_worker_insert_and_remove(image, ch)
        print("PASS compiled probe budget, peer-only deferral and unlock cleanup on both channels")
        print("PASS compiled worker deferral cap, validation, insertion and two-miss re-arm")


if __name__ == "__main__":
    main()
