# Device USB import quota verification

PASS. No product or historical test files were edited.

Command, from repository root:

```powershell
python .planning/2026-10-29-ui-optimizations/host/test_usb_import.py
```

Evidence: `usb-import.log`, `usb-import-results.json`; source hashes are recorded in the JSON. ARMCC5 fixture compile/link logs are in `usb/dual-fixture/current/compile.log` (no warnings/errors).

The test compiles current production quota/catalog/mirror/auth modules and the unchanged current USB handlers (`cmd_open_write`, `cmd_write_data`, `cmd_close_file`, `cmd_stage_algorithm`, `cmd_burn_quota`, `custombulk_quota_service`, and validation helpers) as Cortex-M3 Thumb, then executes them through Unicorn. Only physical media, selection invalidation and USB response ports are emulated; the import marker routing and quota state logic execute real C.

Verified:

- Same-name, same-limit upload resets `(total, used, remaining)` from `(100, 1, 99)` to `(100, 0, 100)`, including repeated uploads, filename case aliases and restart.
- Unlimited same-name upload resets `(0, 1, 0)` to `(0, 0, 0)` and retains unlimited mode after restart.
- Upload with requested limit equal to current remaining still resets history, e.g. `(100, 1, 99)` becomes `(99, 0, 99)`.
- GET preserves state; same-value ordinary/repeated SET is idempotent. A normal SET that changes balance preserves total/used. Staging an algorithm without a completed upload does not reset history.
- The uploaded program marker is firmware-scoped and consumed only after successful quota persistence. Failed file close cannot mint an import; failed SET retains the marker for retry. Optional OPT upload and repeated stage do not replace the main BIN marker.
- Existing wire protocol remains GET `0x0D + optional name`, SET `0x0E + enabled + remaining LE32 + name`, successful response `[status, version=2, enabled, fault, remaining LE32]` (8 bytes). Tests assert exact reply bytes.

Production path: successful staged main BIN close records `quota_import_program`; named SET matches this marker and `custombulk_quota_service()` calls `burn_quota_reimport()`. GET and later SETs use ordinary selection/SET semantics. The device already supports the requested refresh; root owns the GUI `_import_done` presentation update.

This is a host-side execution fixture, not a physical USB/hardware upload or the main Keil firmware build. The prior 724 quota fault cases were not repeated.
