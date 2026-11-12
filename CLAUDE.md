# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repository is

This is a hand-built **CMSIS-DAP debug probe / standalone (offline) flash programmer** for GD32/STM32/MM32 targets, plus its toolchain. It is a hardware product bundle, not a single software project: it contains multiple Keil MDK-ARM firmware projects, a Python PC host tool, and a flash-algorithm conversion utility. Most subfolders are self-contained, and many exist twice (a working directory plus a same-named `.zip` backup, and often a nested same-named inner folder — e.g. the real files live in `烧录算法转换工具/烧录算法转换工具/` and `GD32-HID/GD32-HID/`).

The product's signature feature is **target chip "encryption" = read protection**: after flashing main Flash, an extended algorithm writes the target's Option Bytes (RDP) so the programmed chip cannot be read back. This is *not* firmware encryption — firmware is stored and transferred in plaintext.

## Repository layout (the parts that matter)

| Path | Role |
|---|---|
| `GD32-HID/GD32-HID/` | **The offline programmer firmware** (GD32F103), OPT-encryption version. This is the main project. |
| `GD32-HID-标准FLM/GD32-HID/` | Same programmer, standard version **without** OPT encryption. Diff against `GD32-HID/` to see what the encryption feature adds. |
| `Bootloader/Bootloader/` | Bootloader for the programmer's GD32; main app runs at Flash offset `0xC000`. |
| `烧录算法转换工具/烧录算法转换工具/` | `.FLM` → C/`.bin` flash-algorithm converter (`flm2c.py`, `flm2c_opt.py`) + generated algorithms. |
| `_Template-opt/`, `_Template/` | Keil CMSIS FLM source projects (`FlashPrg.c`) that compile *target-side* flash algorithms. `-opt` adds `ProgramOpt`. |
| `G0001-FLM/` | Another FLM project (MM32G0001); `APP/main.c` is an empty stub — real code is `flash_loader.c`. |
| `仿真器根目录/仿真器根目录/` | Image of the programmer's on-device SPI-Flash filesystem (`download/`, `update/`, `system/`). |

## Two distinct meanings of "flash algorithm" — do not confuse them

1. **Target-side algorithm** (`_Template-opt/FlashPrg.c`, `G0001-FLM/flash_loader.c`): C code compiled into a CMSIS `.FLM` (an ELF), whose machine code runs *inside the target chip's RAM* to erase/program it. `ProgramOpt()` here is what writes RDP.
2. **Packaged `.bin` header format** (`flm2c_opt.py` + `USER/ALGO/flash_blob.h`): the offline programmer does not consume `.FLM`. `flm2c_opt.py` parses the FLM ELF and emits a private container = a 14-field header (`program_target_t`) or a 17-field header (`program_target_ext_t`, adds `opt_program`/`opt_addr`/`opt_size`) followed by the raw algorithm blob. The programmer reads this `.bin` from its filesystem and drives it over SWD. The struct layout in `flash_blob.h` must stay byte-compatible with the converter's output.

## Offline programmer firmware architecture (`GD32-HID/GD32-HID/USER/`)

`DAPlink/src/main.c` in the Keil tree is a **leftover USB-keyboard demo stub** — ignore it. The real firmware lives under `USER/`:

- `APP/run.c` — FreeRTOS task orchestration and the whole UI/flashing state machine. `mode2()` is the flashing flow; `run_task()` sets up tasks. This is the first file to read.
- `DAPlink/SWD_flash.c` / `SWD_opt.c` / `SWD_host.c` — SWD-driven target flashing. `SWD_opt.c:target_opt_program_ext()` is the OPT-encryption step (writes OPT data into target RAM, then runs the target's `ProgramOpt` via `swd_flash_syscall_exec`).
- `ALGO/flash_blob.h` — the `program_target_t` / `program_target_ext_t` structs + `PROG_TARGET_EXT_HDR_SIZE = 68`. Foundation for all of the above.
- `BSP/` — `bsp_key.c` (3 keys, short/long press), `bsp_oled.c`, `font.h`, `bsp_pwr.c`, `bsp_gd25qxx.c` (SPI Flash).
- `FATFS/` — FatFs over the 8MB GD25QXX SPI Flash, drive `0:`.

### On-device file convention (hardcoded in `run.c` `mode2()`)
- `0:/download/firmware/<name>` — packaged flash algorithm `.bin`
- `0:/download/bin/<name>` — target firmware (plaintext)
- `0:/download/bin/<name>_opt.bin` — OPT/RDP data; only needed for encryption. Name = firmware name with extension replaced by `_opt.bin`.

Extended (encrypting) algorithms are detected at runtime by `fw_size == algo_size + 68`; the blob is then `memmove`'d back 12 bytes so the same code path can treat it as a plain 14-field `program_target_t`.

### USB
The device enumerates as a composite **HID + WinUSB + CDC** device (`hidmsccdc/usbd_composite.c`), VID `0x28E9` PID `0x128A`. Despite the folder name it does **not** expose a USB mass-storage drive, so files cannot be drag-dropped — they go over the WinUSB bulk protocol via the PC tool.

## Common commands

### Firmware (Keil MDK-ARM — no CLI build in this repo)
Open the `.uvprojx` in Keil µVision and build there. Key projects:
- Programmer (encrypting): `GD32-HID/GD32-HID/DAPlink/MDK-ARM/DAPlink.uvprojx` → output `DAPlink.bin`
- Programmer (standard): `GD32-HID-标准FLM/GD32-HID/DAPlink/MDK-ARM/DAPlink.uvprojx`
- Bootloader: `Bootloader/Bootloader/DAPlink/MDK-ARM/DAPlink.uvprojx`
- Target FLM algorithm: `_Template-opt/NewDevice.uvprojx`

To update a physical programmer: rename the built `DAPlink.bin` (or `DAPlink-HID.bin` / `DAPlink-WINUSB.bin`) and place it in the device's `update/` directory, then power-cycle (the bootloader picks it up).

### Flash-algorithm conversion (`烧录算法转换工具/烧录算法转换工具/`)
```bash
python flm2c_opt.py MM32G001-OPT.FLM            # → .c + .bin (17-field, Flash+OPT)
python flm2c_opt.py MM32G001-OPT.FLM --no-opt   # standard 14-field
python flm2c_opt.py MM32G001-OPT.FLM --dry-run  # preview only
python flm2c.py <file>.FLM                       # standard converter (14-field only)
```
Source: `.FLM` files come from Keil chip DFP packs (`C:\Users\<user>\AppData\Local\Arm\Packs\...\Flash`). `readme.txt` in this folder documents the header format and the required manual `sizeof(...)` edits when using the older `flash_algo.exe` GUI path.

### PC host tool (`GD32-HID/GD32-HID/tools/`)
Requires Python **3.12** (3.14 alpha segfaults PyQt5) in a local `.venv`:
```bash
py -3.12 -m venv .venv
.venv/Scripts/python -m pip install intelhex pyusb PyQt5 libusb-package
```
Run the GUI: `run.bat` (Windows) or `run.sh` (Git Bash) — launches `daplink_manager.py`.

CLI file transfer (`file_transfer.py`) pushes files onto the device's SPI-Flash filesystem over WinUSB:
```bash
python file_transfer.py list  /download/bin/
python file_transfer.py push  firmware.bin /download/bin/firmware.bin
python file_transfer.py push  algo.bin      /download/firmware/algo.bin
python file_transfer.py pull  /download/bin/x.bin local.bin
python file_transfer.py rm    /download/bin/x.bin
```
On Windows, first connection needs the WinUSB driver (BOS descriptor, auto on Win8.1+). `enable_bos.py` / `fix_bos.py` / `restore_bos.py` manage that. `opt_config.py` + `chip_config.json` provide per-chip OPT byte definitions to the GUI.

There is no automated test suite; verification is done by flashing real hardware and reading the OLED status codes.

## Editing the device OLED display (frequent request)

- English/digits/status strings: string literals in `USER/APP/run.c` via `OLED_ShowString(...)`. ASCII only.
- Chinese text: NOT literals — 16×16 dot-matrix arrays `string_1..string_4` in `USER/BSP/font.h`, selected by the `index` arg of `OLED_ShowChinese(x, y, index, len, mode)` (`bsp_oled.c`). Regenerate glyphs with a font-extraction tool (16×16, column-major) and replace the array; add a new `case` in `bsp_oled.c`'s `switch(index)` to add entries.
- Boot logo: read at runtime from `0:/system/logo.bin` (128×8 = 1024 bytes) — replace the file, no recompile needed.

## Gotchas

- Chinese-comment source files (`run.c`, `font.h`) are **GBK-encoded** — Keil expects GBK; saving as UTF-8 corrupts existing comments. `tools/gbk_edit.py` does safe GBK find/replace on such files.
- SWD pin mapping is `SWCLK=PB13 SWDIO=PB15 nRESET=PB12` (non-V1); guarded by `#define V1` in `DAP_config.h`.
- The flashing flow races to grab SWD before the target firmware can disable the debug port (retry loop in `run.c` `mode2()`), and `EraseChip` writes RDP key `0x5AA5` to *un*-protect before mass-erasing a previously encrypted chip.
