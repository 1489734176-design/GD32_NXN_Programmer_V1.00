# Task 1 — charging icon and manual firmware chooser

Implementation and targeted Thumb verification completed. Files changed only:

- `GD32-HID/GD32-HID/USER/APP/run.c`, `run.h`
- `GD32-HID/GD32-HID/USER/BSP/bsp_oled.c`, `bsp_oled.h`, `ui_chinese_font.h`

Charging and full states display an independent yellow 8×16 vertical bolt at x140, y8. Battery stays a normal charge-level icon at x152; unplugging clears the bolt rectangle. Temperature field is 56px wide. Existing plugged-in 100% behavior and 3.3V label remain.

Manual firmware-row middle key enters mode3. Six 224×36px rows at y48..248 wrap long names over two lines; highlighted row and page counter follow the current filename. Left/right short, long and repeat events move up/down with wrap. Long right switches mode only on main. Middle confirms, long middle cancels. Browsing stores only six 32-byte names and never opens firmware/OPT bodies. Invalid names, folders and OPT files are filtered. USB import returns to main and invalidates both page states.

Confirmation validates full SUM32 and OPT presence/size/close results before updating app_data and dirty-save state. Failed candidates remain in the list, with an error, and cannot silently disable OPT. Prior valid selection survives cancellation. This is the explicit root-approved ruling, replacing the old inline selector's invalid-selection behavior.

Existing glyph bytes were retained; four SimSun 16px glyphs were added for confirmation/back instructions. UTF-8 source/CRLF, 3.3V and DATA_BUFFER alignment remain. No SWD, SPI, quota or main build-script changes were made. run.c's old predebit/refund comments now reflect successful-burn debit.

## Verification

- Seven new groups first failed against baseline Thumb (missing bolt / list).
- Root's fresh Keil build passed with 0 errors and 0 warnings after removal of a now-unreachable case-ending break.
- All seven groups now pass, including charger/full/unplug, current-name location, cross-page/wrap/long/repeat navigation and main long-right, no browsing reads, checksum+OPT confirmation, empty and failed directories, invalid/long names, file/read/close/OPT failures preserving old protection, cancel, USB import and cache restoration.
- Pixel bounds are asserted at the LCD port. Idle main/list draws produce no writes.
- Actual-Thumb preview was rendered and visually inspected: `chooser-preview.png`.
- `offline.bin` tested SHA256: `d6097d058f062597e0b5d660f8c8e88b702943136d21504ae6e003c148e13ca4`.
- Targeted results: `red.json`, `green.json`; source diff and hashes: `ui.diff`, `source-hashes.json`.
- run.o static RAM rises by 212 bytes (RW +20, ZI +192); bsp_oled.o static RAM unchanged. No framebuffer allocation. Root owns whole-app RAM/alignment checks and independent review.
- Physical hardware was not exercised.

Run GREEN from repository root:

```powershell
& 'D:\Program Files\Python314\python.exe' '.planning\2026-10-29-ui-optimizations\task\ui\test_ui_chooser.py' --image '.planning\2026-10-29-ui-optimizations'
```

Generate preview (Pillow is present in bundled Python):

```powershell
& 'C:\Users\mym02\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' '.planning\2026-10-29-ui-optimizations\task\ui\preview_chooser.py' --image '.planning\2026-10-29-ui-optimizations'
```

No further product edits pending from this implementer; awaiting root review/integration feedback.
