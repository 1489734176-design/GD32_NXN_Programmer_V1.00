# UI and Successful-Burn Accounting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans task-by-task.

**Goal:** External charging bolt, manual file chooser, debit only successful burns.
**Architecture:** Existing C state machine, shared UI caches and quota transaction APIs; disjoint worker ownership.
**Tech Stack:** ARMCC5, FreeRTOS, FatFs, direct LCD, Thumb-image tests.
**Spec:** design.md

## Constraints and review focus
-240x320,16px text,4px gaps,no framebuffer; preserve current3.3V and __align(4) DATA_BUFFER.
- No SWD timing/algorithm-container/Bootloader changes. Only root builds main Keil.
- Cover charging/full/unplugged clearing; empty/many/invalid/long-named files; chooser cancel/confirm and long-right scope; begin/fail/reboot no mutation and exactly-once success; RAM bounds/alignment.

### Task1 — UI
Files: USER/APP/run.c/.h and USER/BSP/bsp_oled.c/.h, ui_chinese_font.h only if needed.
Consumes unchanged burn_quota API and existing short/long/repeat key events.
- [x] Write baseline-failing icon and chooser tests.
- [x] Implement bolt and mode3 chooser, keeping checksum/OPT validation and dirty-save gating.
- [x] Verify, hand off diff/source hashes, receive independent review.

### Task2 — Quota
Files: USER/APP/burn_quota.c/.h; isolated quota tests.
Interfaces unchanged: begin checks/locks without debit; finish(0) ends; finish(1) commits once.
- [x] RED:100→begin remains100/media unchanged;fail/reboot100;success99 used1;duplicate finish99.
- [x] Implement RAM-only active session including unlimited, keeping concurrent switch/admin/import blocked.
- [x] Verify actual C limited/unlimited,A-B/reimport/legacy,tampering,and success-commit failure boundaries; independent review.

### Task3 — Root integration
- [x] Adapt full-app regression only for user-superseded chooser/predebit behavior.
- [x] Build; UI/application/alignment tests; inspect preview/RAM and source diffs.
- [x] Final interface review; scoped correction of confirmed findings.
- [x] New BIN/HEX/preview/README/manifest package, preserving previous releases.

Ruling: user supplied specific changes and explicitly confirmed no debit on interrupted burn; execute without another approval gate. Existing directory is nonGit; backups replace Git-only tooling. Quota/UI work may run in parallel under developer collaboration instructions.


### Final integration — complete
- User clarified "上传固件" means the host ⚡ 导入程序 action. Existing ImportWorker named SET/reimport and _import_done refresh already implement the required behavior. Kept the protocol/order and verified it; changed only outdated host tooltip (interrupted burns are no longer charged).
- Main Keil: 0 errors / 0 warnings. BIN SHA256 d6097d058f062597e0b5d660f8c8e88b702943136d21504ae6e003c148e13ca4.
- Main screen7 + chooser7 + full application14 + Qt/host17 passed; quota724 injected fault cases, USB reimport and independent UI/quota reviews passed.
- Header LDRD reads aligned address0x20002704 from DATA_BUFFER0x200026CC. RW_IRAM1=19736/20480 bytes; existing6KB FreeRTOS heap and4KB run_task stack unchanged. Main/list previews inspected.
- Release: output/ui-optimizations-success-count. BIN/HEX identical payload at0x0800C000..0x0801C9FF; previous releases preserved. No hardware flashing/reset or physical-device verification.
