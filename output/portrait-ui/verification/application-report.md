# Application integration result

The application uses one fixed portrait screen from startup through selection, burning and terminal feedback. Original SWD/OPT programming order is retained; selection, quota and USB operations remain serialized by the existing main task.

- Pixel RED against preserved baseline: horizontal text writes exceed portrait bounds; unified progress API absent. Tests written in test_portrait_ui.py.
- One pageID10; static labels once, dynamic IDs0..8 and10..12. Fixed y8..228 at20-pixel pitch, 152px value width.
- Manual middle on firmware cycles eligible files inline, excludes directories and *_opt.bin, computes actual SUM32 and OPT presence, then defers durable selection to existing main-task save. Slots show -- while selection owner is pending.
- Automatic cursor/start stays fixed, existing detection/removal state machine preserved. Terminal results appear only after burn_quota_finish and return internally to mode2.
- No application logo/results/error/chooser screen. Long middle shutdown and long right mode toggle preserved.
- Progress15..85 covers main write; later validation/OPT phases reserve85..95; confirmed completion sets100/green. Error result remains red with reason.
- Startup and file errors must remain visible, so feedback resets at explicit new import/selection/mode switch/removal, not every auto idle redraw.

Review P1 fixed: only FR_NO_FILE/FR_NO_PATH mean optional OPT is absent. Empty OPT, I/O and close failures block saving and burning. Review P2 fixed: a bad file retains its traversal position so the next file remains reachable. Both closed by independent scoped review.

## Final verification

Final BIN SHA256: `443afb261cdf303659c21b3fd7faa5767b903d758ed7c216816a76acaa6d2a0c`.

- Keil rebuild: 0 errors, 0 warnings; Code53972, RO13124, RW820, ZI18844.
- `test_portrait_ui.py --preview`: 10/10 groups pass in the final Thumb image. Covers layout, filename clearing, navigation, overlay updates, error placement, invalid files, OPT errors, actual automatic RESULT/HOLD/removal/reinsert state machine, and repeated manual starts with SWD-failure refund.
- `run_application_regression.py`: 13/13 groups pass in the same image. Covers restored selection/checksum, USB import, per-firmware quotas, zero-limit blocking, failed persistence, and shutdown.
- The automatic lifecycle test executes the real application state machine and final result handler, substituting time and physical presence. The manual failure test executes mode2 and mode4 with failed physical SWD connection and observes begin/finish(0). Component tests independently verify the real quota storage transaction.
- Actual-image preview visually inspected. Pixel tests emulate LCD transport; storage tests emulate physical Flash/FatFs I/O. Hardware direction, colors, SWD and real power interruption have not been tested on a device.

Memory checks: image starts0x0800C000, 67428 bytes ends0x0801C764, below selection storage0x0801F000; RAM19664/20480. Keil maximum static call path1956 bytes plus unknown indirect/cyclic paths, below4096-byte main task stack; this is not a physical stack high-water measurement.

Diff: application.diff; baseline: original/GD32-HID/GD32-HID/USER/APP/run.c. Final build/test hashes are recorded in screen-verification.json and power-restore-verification.json.
