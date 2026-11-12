# Compact numbered UI — worker report

Implementation and targeted real-Thumb validation complete.

## Result

- Connection and battery icons now yellow; external charging bolt remains for charging/full and clears unplugged.
- Main coordinates match design: mirror28, voltage48, chip68, mode88, checksum108, firmware128/148, total168, used188, remaining208, blank228, start248, progress268. Voltage remains 3.3V. All three main/status/progress rendering calls use y268.
- Chooser numbers valid files in existing directory order using global ordinals plus a period. No spacer follows the period. Filename continuation starts at the filename's first-line x coordinate.
- Items use 16px for short names or 36px for wrapped names, then 4px spacing. One page fits 12 short items, 6 long items, or complete mixed items such as 1 long + 10 short. Highlight exactly matches actual item height.
- The indicator displays selected ordinal/filtered total, as permitted by design and confirmed by root.
- Short/long/repeat navigation, wrap, current-name location, confirmation, cancel, full-checksum/OPT validation, directory errors and USB return remain intact. No body reads while browsing; failed candidates preserve the prior valid selection.

## Implementation boundaries

Only run.c, bsp_oled.c and bsp_oled.h changed. run.h is included in the source hash manifest but unchanged. No quota, host, SWD, SPI, Bootloader, aligned buffer, Chinese glyph table or algorithm changes.

The 12×32byte name cache adds 192 bytes over the previous six-name cache. Numbered items reuse the existing 16 ui_fields. Slots 0..11 hold items, 12 selected/total, 13..15 the top strip. Footer stores only a pointer to its constant text. Cross-page/USB re-enumeration clears old list rectangles before drawing new geometry, preventing old-item clearing from erasing a new long item's continuation. Same-page navigation redraws changed selected items; idle draws do not write LCD pixels. No framebuffer.

run.o static RAM increases 196 bytes (RW +4, ZI +192); bsp_oled.o RAM is unchanged. The map's actual MCU SRAM region `RW_IRAM1` is `0x4DD8` = 19928 / 20480 bytes, leaving 552 bytes. The aggregate RW 844 + ZI 19228 = 20072 bytes also includes 144 bytes in the fixed USB peripheral region at `0x40006000`, so that aggregate must not be used as MCU SRAM consumption. The task stack remains 4KB. Root reports the retained 7 main-screen groups, 14 full-application groups and compiled alignment check have passed.

## Evidence

- RED: 5 new groups failed against preserved baseline; 1 combined prior-safety group passed. Stored in red.json.
- Root rebuilt main Keil with 0 errors/0 warnings.
- GREEN: 6/6 groups pass. The combined retained group runs old charger/full/unplug tests, empty/open/read/close-directory failure cancellation and rejected main/OPT file/read/close preservation tests. Additional new tests cover confirmed checksum 294 / OPT, filter numbering, cancel and USB main/list restoration.
- Actual image tested: offline.bin SHA256 `a3aa023c89159243105ce522b1289da5f091255cc3fc66e50f8f18dbbd30d28a`.
- Pixel preview was generated through the actual Thumb/LCD path and visually inspected: chooser-preview.png.
- Source evidence: ui.diff and source-hashes.json. Results: green.json. Prior task files were read only.
- Physical hardware was not exercised. Independent review, full-app/main retained tests, alignment and release packaging remain root-owned.

Run from repository root:

```powershell
& 'D:\Program Files\Python314\python.exe' '.planning\2026-10-29-compact-numbered-ui\task\ui\test_compact_ui.py' --image '.planning\2026-10-29-compact-numbered-ui'
& 'C:\Users\mym02\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' '.planning\2026-10-29-compact-numbered-ui\task\ui\preview_compact.py' --image '.planning\2026-10-29-compact-numbered-ui'
```
