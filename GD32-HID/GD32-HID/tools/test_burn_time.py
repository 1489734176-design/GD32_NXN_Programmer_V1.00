#!/usr/bin/env python3
"""Run linked Keil AXF burn timing and dual-channel probe regressions in Unicorn.

Usage: python -B test_burn_time.py DAPlink.axf [--unicorn-path PATH]
Use --page-cadence control for a page-profile AXF with fixed one-tick polls.
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
BASE_RESULT = struct.Struct("<16QBBB5x")
COMMIT_RESULT = struct.Struct("<16QBBB5x2Q")
PAGE_RESULT = struct.Struct("<16QBBB5x4Q3I4x")
PAGE_MARK, PAGE_RECORD = "burn_time_page_mark", "burn_time_page_record"
BP_LAUNCH, BP_POLL, BP_RUNNING = range(3)
PAGE_RUNNING, PAGE_LINK_ERROR, PAGE_HALTED, PAGE_SWD_WAIT = range(4)


class Result(NamedTuple):
    cycles: tuple[int, ...]
    started: int
    ended: int
    ready: int
    miss_wait: int
    valid: int
    ok: int
    dual: int
    commit_hold: int = 0
    commit_wait: int = 0
    page_launch: int = 0
    page_poll: int = 0
    launches: int = 0
    polls: int = 0
    running: int = 0

    @property
    def page_gap(self):
        return self.cycles[PAGE] - self.page_launch - self.page_poll


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
        self.arg_regs = (arm_const.UC_ARM_REG_R0, arm_const.UC_ARM_REG_R1,
                         arm_const.UC_ARM_REG_R2, arm_const.UC_ARM_REG_R3)
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
        for register, value in zip(self.arg_regs, args):
            self.uc.reg_write(register, value)
        self.uc.emu_start(self.symbols[name] | 1, self.STOP, count=budget)
        pc = self.uc.reg_read(r.UC_ARM_REG_PC)
        assert pc == (self.STOP if stop is None else stop), f"{name}: unexpected stop at 0x{pc:x}"
        return self.uc.reg_read(r.UC_ARM_REG_R0)

    def call(self, name, *args):
        return self.call_symbol(f"burn_time_{name}", *args)

    def set_cycles(self, cycles):
        self.uc.mem_write(DWT_CYCCNT, struct.pack("<I", cycles & U32_MASK))

    def at_cycles(self, cycles, name, *args):
        self.set_cycles(cycles)
        return self.call(name, *args)

    def at(self, ms, name, *args):
        return self.at_cycles(ms * CYCLES_PER_MS, name, *args)

    def snapshot(self):
        revision = self.call("snapshot", self.SNAPSHOT)
        if PAGE_RECORD in self.symbols:
            layout = PAGE_RESULT
        elif "burn_time_commit_end" in self.symbols:
            layout = COMMIT_RESULT
        else:
            layout = BASE_RESULT
        data = self.uc.mem_read(self.SNAPSHOT, 2 * layout.size)
        return revision, tuple(
            Result(row[:STAGE_COUNT], *row[STAGE_COUNT:]) for row in layout.iter_unpack(data)
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


def test_commit_overlap(image, owner):
    peer = owner ^ 1
    for wait_at, stage, overlap in ((4, FS_LOCK, 20), (17, FS_LOCK, 13), (30, FS_LOCK, 0),
                                    (33, FS_LOCK, 0), (15, SWD_LOCK, 0), (15, QUOTA_LOCK, 0)):
        f = Firmware(image)
        clock = (1 << 32) - 16
        events = [(0, "begin", owner, 1), (1, "begin", peer, 1), (2, "stage", owner, FS_LOCK),
                  (10, "commit_begin"), (10, "stage", owner, QUOTA_LOCK), (12, "stage", owner, QUOTA),
                  (wait_at, "stage", peer, stage), (30, "stage", owner, OTHER), (30, "commit_end", owner),
                  (31, "end", owner, 1), (40, "end", peer, 1)]
        for tick, name, *args in sorted(events, key=lambda event: event[0]):
            f.at_cycles(clock + tick, name, *args)
        _, rows = f.snapshot()
        a, b = rows[owner], rows[peer]
        assert (a.commit_hold, a.commit_wait, b.commit_hold, b.commit_wait) == (20, 0, 0, overlap)
        assert b.commit_wait <= b.cycles[FS_LOCK] and b.commit_wait <= a.commit_hold
        assert all(sum(r.cycles) == r.ended - r.started for r in rows)

        f.at_cycles(clock + 45, "begin", owner, 1)
        f.at_cycles(clock + 47, "commit_begin")
        f.at_cycles(clock + 50, "commit_end", owner)
        f.at_cycles(clock + 51, "end", owner, 0)
        _, after = f.snapshot()
        assert after[owner].commit_hold == 3 and after[peer] == b
        f.at_cycles(clock + 55, "begin", peer, 1)
        f.at_cycles(clock + 60, "end", peer, 0)
        _, after = f.snapshot()
        assert after[peer].commit_wait == after[peer].commit_hold == 0


def test_page_accounting(f):
    start = (1 << 32) - 30

    def at(offset, name, *args):
        return f.at_cycles(start + offset, name, *args)

    def op(ch, kind, mark, record):
        at(mark, "page_mark")
        at(record, "page_record", ch, kind)

    at(0, "begin", 0, 1)
    at(2, "stage", 0, PAGE)
    at(3, "begin", 1, 1)
    at(4, "stage", 1, PAGE)
    at(6, "stage", 0, SWD_LOCK)
    op(1, BP_LAUNCH, 8, 13)  # A is still waiting for B's mutex.
    at(15, "stage", 0, PAGE)
    at(17, "page_mark")
    at(25, "stage", 1, SWD_LOCK)  # B blocks while A owns the shared mark.
    at(33, "page_record", 0, BP_LAUNCH)  # This launch crosses the DWT wrap.
    at(35, "stage", 1, PAGE)
    op(1, BP_RUNNING, 36, 41)
    op(0, BP_RUNNING, 45, 52)
    op(1, BP_POLL, 54, 58)
    at(61, "end", 1, 0)

    version, (unpublished, b) = f.snapshot()
    assert version == 1 and unpublished == Result((0,) * STAGE_COUNT, 0, 0, 0, 0, 0, 0, 0)
    assert (b.cycles[PAGE], b.cycles[SWD_LOCK], b.page_gap) == (47, 10, 33), b
    assert (b.page_launch, b.page_poll, b.launches, b.polls, b.running) == (5, 9, 1, 2, 1), b
    assert b.valid and not b.ok and b.dual, b

    op(0, BP_LAUNCH, 62, 65)
    op(0, BP_RUNNING, 66, 69)
    op(0, BP_POLL, 71, 82)
    assert f.snapshot() == (1, (unpublished, b))
    at(85, "end", 0, 1)
    version, (a, first_b) = f.snapshot()
    assert version == 2 and first_b == b
    assert (a.cycles[PAGE], a.cycles[SWD_LOCK], a.page_gap) == (74, 9, 34), a
    assert (a.page_launch, a.page_poll, a.launches, a.polls, a.running) == (19, 21, 2, 3, 2), a
    assert all(sum(r.cycles) == r.ended - r.started for r in (a, b))

    at(87, "rearmed", 1)
    b = b._replace(ready=start + 87)
    at(90, "begin", 0, 0)
    at(91, "stage", 0, PAGE)
    op(0, BP_LAUNCH, 92, 95)
    op(0, BP_RUNNING, 97, 101)
    op(0, BP_POLL, 102, 108)
    assert f.snapshot() == (3, (a, b))  # Starting a new burn must not clear a published row.
    at(111, "end", 0, 1)
    version, (single, saved_b) = f.snapshot()
    assert version == 4 and saved_b == b
    assert single.valid and single.ok and not single.dual, single
    assert (single.cycles[PAGE], single.cycles[SWD_LOCK], single.page_gap) == (20, 0, 7), single
    assert (single.page_launch, single.page_poll, single.launches, single.polls, single.running) == (3, 10, 1, 2, 1), single
    assert sum(single.cycles) == single.ended - single.started

    at(120, "begin", 1, 1)
    at(121, "end", 1, 0)  # Failed before any page operation: all attribution must reset.
    version, (saved_a, reset_b) = f.snapshot()
    assert version == 5 and saved_a == single
    assert (reset_b.cycles[PAGE], reset_b.page_launch, reset_b.page_poll,
            reset_b.launches, reset_b.polls, reset_b.running, reset_b.page_gap) == (0,) * 7, reset_b


def test_mode_per_burn(f):
    for revision, dual in enumerate((1, 0, 1), 1):
        start = revision * 10
        f.at(start, "begin", 0, dual)
        f.at(start + 3, "end", 0, 1)
        version, (a, _) = f.snapshot()
        assert version == revision and a.valid and a.dual == dual, a


PROBE_NO_REPLY, PROBE_PRESENT, PROBE_DEFERRED = range(3)


class BoundaryScript(Firmware):
    MUTEX = Firmware.SNAPSHOT  # Opaque handle used only by the mock FreeRTOS calls.
    STOP_WORKER = object()

    def __init__(self, image, ch):
        super().__init__(image)
        self.ch = ch
        self.steps = deque()
        self.position = 0
        self.write("g_swd_mutex", struct.pack("<I", self.MUTEX))
        self.write("g_channel_b_enable", b"\x01")

    def hook(self, name, *, observe=False, callback=None):
        import unicorn

        addr = self.symbols[name] & ~1
        if not observe:
            self.uc.mem_write(addr, b"\x70\x47")  # Thumb BX LR, only in emulator memory.
        self.uc.hook_add(unicorn.UC_HOOK_CODE, callback or self.dispatch, name, begin=addr, end=addr)

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


class ProbeScript(BoundaryScript):
    """Drive the real worker/probe and verify the ordered boundary calls."""

    TIMING_OBSERVERS = ("burn_time_probe_begin", "burn_time_probe_end", "burn_time_rearmed")
    BOUNDARIES = TIMING_OBSERVERS + (
        "vTaskDelay", "custombulk_is_busy", "xQueueGenericReceive", "xQueueGenericSend",
        "DAP_SelectChannel", "swd_off", "target_power", "swd_init_debug", "delaymS", "channel_burn",
    )

    def __init__(self, image, ch):
        super().__init__(image, ch)
        # app_TypeDef: mode, head, bin_name[32], firmware_name[32].
        self.write("app_data", b"\x02\0" + b"firmware.bin".ljust(32, b"\0") + b"algo.bin\0")
        for name in self.BOUNDARIES:
            self.hook(name, observe=name in self.TIMING_OBSERVERS)

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


class PollStep(NamedTuple):
    delay: int
    state: int
    lock_ticks: int = 0
    r0: int = 0
    ack: int = 7
    active: int | None = None


class PageWaitScript(BoundaryScript):
    """Execute channel_wait_page and its timing hooks; mock the SWD/RTOS boundary."""

    ADDRESS = 0x0800ABCD
    FAILURE = Firmware.SNAPSHOT + 0x200
    LOCK_CYCLES, SELECT_CYCLES, UNLOCK_CYCLES = 53, 7, 11

    def __init__(self, image, ch, active, tick):
        super().__init__(image, ch)
        self.tick = tick
        self.cycles = 100
        self.ack = 0
        self.write("g_active_burns", bytes([active]))  # Dual enabled does not mean both are burning.
        self.write("g_programming_active", b"\x01")
        self.uc.mem_write(self.FAILURE, b"unchanged\0".ljust(64, b"\0"))
        for name in ("vTaskDelay", "xQueueGenericReceive", "DAP_SelectChannel", PAGE_MARK,
                     "swd_page_poll", PAGE_RECORD, "xQueueGenericSend"):
            self.hook(name, observe=name in (PAGE_MARK, PAGE_RECORD))
        self.hook("xTaskGetTickCount", callback=self.get_tick)
        self.hook("swd_last_ack", callback=self.get_ack)
        self.at_cycles(self.cycles, "begin", ch, 1)
        self.call("stage", ch, PAGE)

    def get_tick(self, uc, address, size, name):
        uc.reg_write(self.arg_regs[0], self.tick & U32_MASK)

    def get_ack(self, uc, address, size, name):
        uc.reg_write(self.arg_regs[0], self.ack)

    def advance(self, *, ticks=0, cycles=0):
        self.tick += ticks
        self.cycles += ticks * CYCLES_PER_MS + cycles
        self.set_cycles(self.cycles)

    def expect_poll(self, step):
        def poll():
            result_ptr = self.uc.reg_read(self.arg_regs[0])
            self.uc.mem_write(result_ptr, struct.pack("<I", step.r0))
            self.ack = step.ack
            self.advance(cycles=self.poll_cycles(step))

        def unlock():
            self.advance(cycles=self.UNLOCK_CYCLES)
            if step.active is not None:
                self.write("g_active_burns", bytes([step.active]))

        self.expect("vTaskDelay", step.delay, action=lambda: self.advance(ticks=step.delay))
        self.expect("xQueueGenericReceive", self.MUTEX, 0, U32_MASK, 0, reply=1,
                    action=lambda: self.advance(ticks=step.lock_ticks, cycles=self.LOCK_CYCLES))
        self.expect("DAP_SelectChannel", self.ch, action=lambda: self.advance(cycles=self.SELECT_CYCLES))
        self.expect(PAGE_MARK)
        self.expect("swd_page_poll", reply=step.state, action=poll)
        self.expect(PAGE_RECORD, self.ch, BP_RUNNING if step.state == PAGE_RUNNING else BP_POLL)
        self.expect("xQueueGenericSend", self.MUTEX, 0, 0, 0, reply=1, action=unlock)

    @staticmethod
    def poll_cycles(step):
        return 23 + step.state  # Give every outcome a distinct measured cost.

    def run_wait(self, steps, failure):
        for step in steps:
            self.expect_poll(step)
        ok = self.call_symbol("channel_wait_page", self.ch, self.ADDRESS, self.FAILURE, budget=100_000)
        assert not self.steps, f"page wait returned before {self.steps[0][0]}"
        assert ok == int(failure is None), ok
        text = self.uc.mem_read(self.FAILURE, 64).partition(b"\0")[0].decode("ascii")
        assert text == (failure if failure is not None else "unchanged"), text
        self.call("end", self.ch, ok)
        version, rows = self.snapshot()
        assert version == 1 and not rows[self.ch ^ 1].valid
        result = rows[self.ch]
        assert (result.page_launch, result.launches) == (0, 0), result
        assert (result.polls, result.running) == (len(steps), sum(s.state == PAGE_RUNNING for s in steps)), result
        assert result.page_poll == sum(map(self.poll_cycles, steps)), result
        assert result.cycles[SWD_LOCK] == sum(s.lock_ticks * CYCLES_PER_MS + self.LOCK_CYCLES for s in steps), result
        assert result.page_gap == sum(s.delay * CYCLES_PER_MS + self.SELECT_CYCLES + self.UNLOCK_CYCLES
                                      for s in steps), result
        assert result.cycles[PAGE] + result.cycles[SWD_LOCK] == sum(result.cycles) == result.ended - result.started


def test_page_cadence(image, ch, cadence):
    def run(label, steps, *, active=2, tick=0, failure=None):
        if cadence == "control":
            steps = [step._replace(delay=1) for step in steps]
        try:
            PageWaitScript(image, ch, active, tick).run_wait(steps, failure)
        except AssertionError as exc:
            raise AssertionError(f"{cadence} page wait ch {ch}, {label}: {exc}") from exc

    running = PollStep(1, PAGE_RUNNING)
    wait = PollStep(1, PAGE_SWD_WAIT)
    link = PollStep(1, PAGE_LINK_ERROR)
    halted = PollStep(1, PAGE_HALTED)
    run("one active burn, even in dual mode", [running] * 4 + [halted], active=1)
    run("backoff, saturation, and independent WAIT/LINK resets", [
        running, running._replace(delay=2), running._replace(delay=4), wait._replace(delay=4),
        running, running._replace(delay=2), link._replace(delay=4), running, halted._replace(delay=2),
    ])
    run("peer burn ends during page wait", [running, running._replace(delay=2, active=1), running, halted])
    run("nonzero R0 fails even when low 16 bits are zero", [halted._replace(r0=0x10000)],
        failure="PAGE 0000 @0800ABCD")
    run("only four consecutive link errors fail; WAIT and RUNNING each clear the streak",
        [link] * 3 + [wait] + [link] * 3 + [running, link._replace(delay=2), link, link, link._replace(ack=4)],
        failure="LINK 04 @0800ABCD")

    # Block on the first mutex take to reach the deadline quickly, crossing the RTOS tick wrap.
    # Candidate: 2990 + 1 + 2 + 4 = 2997, leaving 3 ticks (not the usual 4) before the final poll.
    first = running._replace(lock_ticks=2990)
    if cadence == "candidate":
        before_deadline = [first, running._replace(delay=2), running._replace(delay=4)]
    else:
        before_deadline = [first] + [running] * 8  # Control reaches the same deadline at one-tick cadence.
    for final, failure in ((running, "PAGE TIME @0800ABCD"), (wait, "WAIT TIME @0800ABCD"),
                           (halted, None), (halted._replace(r0=0xABCD0123), "PAGE 0123 @0800ABCD")):
        run("last delay and result checks at 3 s", before_deadline + [final._replace(delay=3)],
            tick=U32_MASK - 2, failure=failure)
    run("the fourth link error takes precedence at 3 s",
        [link._replace(lock_ticks=2996), link, link, link._replace(ack=4)],
        tick=U32_MASK - 2, failure="LINK 04 @0800ABCD")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("axf", type=Path, help="linked firmware AXF with symbols")
    parser.add_argument("--unicorn-path", type=Path, help="directory containing the Unicorn package")
    parser.add_argument("--timing-only", action="store_true", help="skip compiled probe, worker, and page-wait tests")
    parser.add_argument("--page-cadence", choices=("candidate", "control"), default="candidate",
                        help="expected poll cadence for page-profile AXFs (default: candidate)")
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
    if "burn_time_commit_end" in image[1]:
        for owner in (0, 1):
            test_commit_overlap(image, owner)
        print("PASS final commit FS hold and peer wait overlap, both sockets, DWT wrap, reset and exclusions")
    if PAGE_RECORD in image[1]:
        test_page_accounting(Firmware(image))
        print("PASS independent page launch, poll, running and gap attribution, DWT wrap and reset")
        if not args.timing_only:
            for ch in (0, 1):
                test_page_cadence(image, ch, args.page_cadence)
            print(f"PASS compiled {args.page_cadence} page cadence, lock exclusions and deadline checks on both channels")
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
