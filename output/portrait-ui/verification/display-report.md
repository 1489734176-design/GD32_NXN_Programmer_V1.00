# Task 1 display handoff

Status: complete within assigned BSP scope; parent owns the full firmware build and independent review.

## Changes

- `bsp_lcd.h`: application defaults to `LCD_DIRECTION=0` (240×320); `LCD_BAREMETAL` retains direction 1 (320×240); explicit project overrides remain honored.
- `bsp_lcd.c` / `bsp_lcd.h`: new `LCD_BlitMonoSplit` paints the requested clipped columns directly through existing bounded SPI transactions. Filled side uses black glyphs on the state color; unfilled side uses the state color on black. No framebuffer or temporary pixel row.
- `bsp_oled.c` / `bsp_oled.h`: `UI_ProgressStatus` and four ordered enum values `IDLE=0`, `RUNNING=1`, `FAILED=2`, `SUCCESS=3`; fixed 16px height, running decimal percentage prefix, complete red/green terminal fill, GBK-safe clipping. Only differing glyphs/fill intervals contribute to the dirty range; all glyphs intersecting that range redraw against the correct split background. Identical visible state causes zero pixel writes. `OLED_Clear` invalidates the cache.
- `ui_chinese_font.h`: added 17 SimSun 16 glyphs (作保像准压备存度式执操本模用目行镜), including all GBK escaped characters in the parent's current `run.c`. Existing bitmap bytes preserved and table remains sorted by GBK.
- `UI_Logo`, `UI_BootLogo`, legacy `UI_Progress`, `run.c`, quota sources and Keil project files unchanged by this task. Parent confirmed the application no longer calls its old standalone boot-logo page; no logo scaling change made.

## Tests and evidence

Fixture command (isolated ARMCC + armlink, unchanged copies of actual BSP sources):

```powershell
& 'D:\Program Files\Python314\python.exe' '.planning/2026-10-29-screen-ui/portrait/display/test_progress.py'
```

Result: 7/7 groups PASS, exit 0; fixture build has no compiler/linker warnings. `results.json` and `fixture/compile.log` contain evidence.

- 0/37/100/reset progress compared pixel-by-pixel against the existing text renderer's glyph mask and independently selected expected fill positions (0/56/152/0).
- Same percentage with running/failure/success colors; terminal full fill; repeated state zero writes; long-to-short caption cleanup and idle reset.
- Fill movement beneath unchanged status and percent changes stays inside the dirty interval, matching a clean first draw; percent saturates at 100.
- GBK clipping with only 15px remaining, narrow/right-edge/zero width and incomplete bottom-row rejection.
- The real `LCD_BlitMonoSplit` code emits tested RGB565 high/low SPI bytes for black, white, red, green, blue, yellow, cyan, magenta and gray. Both row-major and column-page bitmaps, absolute clip origin, split boundary and screen edge clipping are covered.
- All current `run.c` Chinese glyphs present, sorted/unique table; mixed ASCII/GBK width transitions, clipped invisible suffix changes and cache invalidation after clear.

RED evidence: `--baseline` uses saved originals and fails because the old progress painter has no fill/overlay at the required row origin (`red-results.json`). Before implementation the six requested groups failed on missing APIs/glyphs (`red-api-results.json`). The final added mixed-text regression passes. During exploratory review a boot-logo resizing check was excluded after the parent's explicit direction that the legacy API is unused; its implementation was never changed. A test setup initially changed terminal state to idle while checking invisible suffix equality; corrected the setup to keep visible state identical.

Glyph regeneration uses the bundled Python with Pillow:

```powershell
& 'C:\Users\mym02\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' '.planning/2026-10-29-screen-ui/portrait/display/add_glyphs.py'
```

## Pixel harness ABI

```c
void UI_ProgressStatus(uint16_t x, uint16_t y, uint16_t width,
                       uint8_t percent, uint8_t state, const char *status);
void LCD_BlitMonoSplit(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                      uint16_t stride, const uint8_t *bitmap, uint8_t column_pages,
                      uint16_t split_x, uint16_t color,
                      uint16_t clip_x, uint16_t clip_width);
```

Split ABI: R0..R3 = x,y,width,height. Stack words = stride,bitmap,column_pages,split_x,color,clip_x,clip_width. Fixed 1× pixels. `x < split_x` is filled. Clip interval is absolute `[clip_x, clip_x+clip_width)`, then clipped to glyph and screen; glyph origin does not shift with clipping. Foreground/background selection also honors the existing `ui_invert` flag. `UI_Logo(x,y,index)` stays 18×8 at 2× scale (36×16).

## Memory and scope audit

- Static RAM +42 bytes for one progress cache; fixture `.bss` grows 0x4e0 → 0x50a. No frame/row pixel buffer.
- Caption local buffer 31 bytes at the default portrait width. ARM `-O0` fixture maximum observed call-chain stack for tested progress paths: 224 bytes, including normal LCD painter call frames; parent must use the optimized application map for the final task-stack judgment.
- Added glyph ROM 578 bytes. Fixture code changes: BSP OLED 0xaa8 → 0xeb6 (+1038), BSP LCD 0x688 → 0x800 (+376), using `-O0`; these are not optimized final image-size claims.
- `display.diff` is relative to saved `portrait/original`; `source-audit.json` lists hashes for the five changed BSP files and confirms strict UTF-8. No Git repository/commit exists.

## Remaining verification / risk

- Full application Keil build, integrated single-page pixel regression, bootloader regression, task-stack/headroom checks and package generation belong to parent and were not run here.
- Physical ST7789 portrait orientation and hardware appearance still require the device; SPI hook tests verify emitted pixel values, not the panel.
- Progress API is intentionally one cached status strip. The main page owns its region; external direct writes over it require `OLED_Clear` (or a state change) before a guaranteed redraw, consistent with other existing cached UI fields.
