# Portrait Single-Screen Programmer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Implement the user's twelve-row portrait programmer with one live page, restored icons, overlaid progress and durable per-firmware counters.

**Architecture:** Keep target programming and USB operations serialized in the existing main task. Independently implement the low-memory progress painter and the authenticated quota statistics, then integrate them with a fixed main page and inline file selection.

**Tech Stack:** GD32F103 C, Keil ARMCC/MDK, ST7789 SPI, FatFs, Python/Unicorn execution of real Thumb code.

**Spec:** `.planning/2026-10-29-screen-ui/portrait/design.md` (records the user's explicit layout, reference video and subsequent counter clarification).

## Global Constraints

- 240x320 portrait; text16; line gap4; twelve row origins8 through228 at20-pixel pitch.
- Blue static labels, yellow values; real existing connection/battery icons; no invented output-voltage measurement; image number000.
- One visible page during file selection, programming and all outcomes; automatic cursor at start, manual cursor at firmware/start.
- Firmware reserves exactly two aligned152-pixel value lines atx80, y108/128; clear unused continuation.
- Running progress yellow, failure red, success green; percentage/status drawn over the fill without black rectangles or residual glyphs.
- Imported total and successful-used count belong to the selected firmware; preserve over power-off, increment used only on confirmed success, reset on reimport even with an unchanged limit.
- Old entries lacking historical totals must not fabricate history or grant extra remaining quota. Unlimited entries continue to count successful uses.
- Preserve existing authorization, internal/external authenticated consistency, failed-burn refunds, USB background service and SWD/OPT ordering.
- Actual sources are UTF-8 with GBK byte escapes. No Git checkout exists; use saved originals and generated diffs. Main-project build is owned by root; fixture builds must be isolated.

## Review Focus

- Power loss after reservation or while committing success must not turn an unconfirmed burn into success or add quota (Task2).
- Reimporting the same name and total must reset used even when the old remaining equals the requested value (Task2).
- A firmware switch must update checksum, OPT derivation, quota owner and persistence together while USB is idle (Task3).
- Fill movement beneath an unchanged caption and same-percentage color changes must repaint the correct pixels (Task1).
- Long status/filename, empty files, missing algorithm, storage faults, automatic removal/reinsertion and repeat manual starts must remain on the same page (Task3).

### Task 1: Portrait display and unified progress renderer

**Files:** USER/BSP/bsp_lcd.[ch], bsp_oled.[ch], ui_chinese_font.h; tests/reports under `.planning/2026-10-29-screen-ui/portrait/display/`.

**Interfaces:** produce `UI_ProgressStatus(uint16_t x, uint16_t y, uint16_t width, uint8_t percent, uint8_t state, const char *status)` and enum `UI_PROGRESS_IDLE`, `UI_PROGRESS_RUNNING`, `UI_PROGRESS_FAILED`, `UI_PROGRESS_SUCCESS`. Fixed16-pixel height, cache last values, no framebuffer. Existing `UI_Logo` stays available to root.

- [ ] Pin overlay, clipping, color change and glyph-background behavior with real rendering tests; establish the old code cannot provide the requested overlay.
- [ ] Default app orientation0 while preserving LCD_BAREMETAL Bootloader direction1. Add a low-memory split-background glyph painter as needed; the public progress function prefixes running percentage, fills terminal states completely and clips at GBK character boundaries.
- [ ] Add missing Chinese glyphs required by the spec with the existing simsun16 extraction style. Preserve sorted GBK table.
- [ ] Test running0/37/100, reset0, terminal red/green at the same percentage, unchanged redraw, long-to-short status, clipping and all RGB565 colors. Report lower-level APIs for the root pixel harness.
- [ ] Review scope/diff; return the report to root for independent review and integration.

### Task 2: Durable per-firmware imported total and successful used count

**Files:** USER/APP/burn_quota*.[ch], quota_auth only if required, USB quota/reimport integration at its actual source; tests/reports under `portrait/quota/`. Do not modify run.c or display files.

**Interfaces:** produce `uint8_t burn_quota_stats_known(void)`, `uint32_t burn_quota_total(void)`, `uint32_t burn_quota_used(void)`. Existing enabled/remaining/fault and begin/finish APIs remain source compatible. Reimport resets stats through the correct existing host command boundary.

- [ ] Real-C tests: import100→success total100/used1/remaining99; failure leaves1/99; reboot retains; firmwareB independent; same-total reimport resets; unlimited success increments used; legacy remaining preserved with unknown total/used.
- [ ] Integrate total/used into authenticated durable transactions, choosing a versioned compatible representation. Never infer used from a pending decrement after crash. Ensure legacy and tamper behavior remains conservative.
- [ ] Run quota/mirror/catalog regressions and power-loss fault injection including SET failures, unchanged-value reimport, partial writes, missing/corrupt records and file switching.
- [ ] Report API/format changes and exact commands/results for review. No shared main-project build.

### Task 3: Single-page application integration and delivery

**Files:** USER/APP/run.[ch], test harness/build/package scripts under `portrait/`; root owns these files.

**Interfaces:** consume Tasks1/2 APIs; static labels drawn once, field IDs allocated only for dynamic values, icons restored in top row.

- [ ] Adapt existing Thumb pixel and application harnesses to240x320 and pin new navigation/single-page behavior before implementation.
- [ ] Implement twelve rows, inline next-file selection on middle key, fixed automatic cursor, mode toggle and power-off. Update selected checksum/OPT/quota and save selection correctly.
- [ ] Route every running/status/error/result path through the same page and progress area; no clear/re-entry during a burn. Map progress stages monotonically and only report success after the overall burn and quota commit succeed.
- [ ] Compile in Keil after both components finish; execute screen/application/quota regressions, inspect rendered previews and review changes independently.
- [ ] Package verified `output/portrait-ui/DAPlink.bin`, HEX, preview and matching-hash reports; preserve the previous `output/screen-ui` release.
