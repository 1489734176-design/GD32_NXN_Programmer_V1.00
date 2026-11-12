# Independent review and resolution — 2026-10-28

Reviewer: /root/review_quota (fresh context, read-only review of quota core, catalog, host/USB and selection integration). This report records the review messages and the root agent's verified resolutions.

## Findings addressed

- **P1, cross-firmware USB completion race:** a named quota query could load B while app_data still selects A. The main loop skips save/rebind while USB response transfer_active is set; an IN-complete interrupt before mode4 could then allow A to consume B's quota. Root reproduced on actual full application Thumb: A=0 and B=unlimited reached swd_off. mode4 now rebinds app_data.bin_name immediately before the quota gate, while programming is active, and fails before target power on selection/storage errors. test_quota_binding_race.py passed after failing before the fix; included in the final current-image suite.
- **Torn first journal record recovery:** no committed record plus incomplete Flash content must not permanently prevent explicit reimport. Selection now only stages an owner while keeping the storage fault. begin remains blocked, and a successful explicit quota SET is required to repair. Seventeen first-record cut positions pass. Reviewer confirmed the repair continues to fail closed until explicit SET.
- **Catalog ASCII consistency:** core/protocol reject high-bit filename bytes; catalog now applies the same check. Root added a failing nonASCII name case, fixed the check and verified green.

## Reviewed properties

Reviewer reported no further new material findings across completed host/USB/catalog review. Catalog verifies every byte of the internally anchored committed prefix, including records after an early matching name, and reads back the entire updated prefix before returning a new anchor. Host preflights protocol version 2 before mutation, scopes SET to the final uploaded main BIN basename, and does not cancel an import session when preflight failed before a session started.

## Documented limitation

Quota identity uses the complete firmware filename, case-insensitive. Normal GUI uploads and manual FatFs LFN listings use this identity. The API does not canonicalize DOS short-name aliases for the same physical file. Direct callers must use full long filenames; this is documented in the package README. No wider FAT identity redesign was added. Direct quota SET is already an explicit authorized reset operation.

## Final evidence

- Full Keil application build: 0 errors, 0 warnings.
- Eight final regression scripts pass, including production ARM core/catalog/USB, actual application startup/manual selection/USB/depleted burn entry, host import, existing selection journal and USB invalidation guards.
- Final image SHA256: 98fbf11792b38889f258167cf26b37222d29360342883518567a276f95bfcf3e.
- Physical hardware and USB connection have not been tested.
