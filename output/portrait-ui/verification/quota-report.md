# Task 2: durable per-firmware quota statistics

Status: complete. Implementation, real-C/USB baseline, legacy compatibility, authenticated catalog and media-fault regressions pass. Product review diff: `task-2.diff`; four-file SHA256 manifest: `quota/source-manifest.json`.

## Changes

- `burn_quota_stats_known()`, `burn_quota_total()`, `burn_quota_used()` expose the selected firmware's imported batch history. Existing begin/finish/select/enabled/remaining/fault signatures stay compatible.
- `burn_quota_reimport(enabled, total)` explicitly starts new history (known=1, imported total, used=0), even if the requested value equals old remaining or old total. Ordinary administrator `burn_quota_set()` preserves an existing batch's known/total/used; only its enabled/remaining changes. A first SET on blank storage starts new history.
- Success increments used only inside `burn_quota_finish(1)` after successful verification of the internal journal, external mirror and catalog. Limited begin keeps the existing durable reservation debit. Failed finish refunds it without incrementing used. Unlimited success persists used too. No reservation recovery computes `used=total-remaining`.
- The GUI import order was verified in `tools/daplink_manager.py:ImportWorker.run`: stage algorithm, upload main BIN, optional OPT, stage again, named quota SET, PREPARE. USB recognizes the first successfully closed main BIN in a staged session, compares its basename case insensitively to an explicitly named SET, and calls reimport once. GET and duplicate SET preserve history. A failed SET retains the marker for retry; failed uploads cannot mint one. The second stage inside the same session preserves the marker.
- Product edits are limited to burn_quota.c/.h, burn_quota_catalog.c and the existing custombulk source. USB's GBK bytes are preserved. No run.c, display or Keil project changes were made by this task.

## Format and compatibility

Both persistent record sizes and paths remain unchanged. HMAC domains, device binding and catalog root coverage remain unchanged.

- BQT4: 96 bytes, same internal two-page journal slots and `burnquota.chk` mirror. Word2 stores enabled bit0 and known bit1. Word3 stores remaining in low16 and imported total in high16. Word4 stores uint32 used (the BQT3 remaining inverse is unnecessary under HMAC). Name, catalog root, 16-byte HMAC, inverse sequence and final commit marker retain their offsets.
- BQC2: 48 bytes, append-only `burnquota.dat` records. Former enabled word is packed enabled bit0, known bit1, remaining bits2..11, total bits12..21. Former remaining word stores used. The record CRC remains and the authenticated journal root covers the whole committed catalog prefix.
- Valid BQT3/BQC1 records are read with known=0/total=0/used=0, preserving exact remaining. BQT1/BQT2 retain their previous conservative migration path. Old and new records may coexist. Only a new import establishes otherwise unknown historical counts.
- The CRC-only BQT1/BQT2 migration path accepts BQC1 records only. It cannot promote an attacker-created BQC2 record into authenticated total/used history. A dedicated real-C test failed before this restriction and passes after it.
- A power cut after reservation may yield total100/used0/remaining99. This is deliberate: remaining remains conservatively spent but success was never confirmed. Administrator SET may also make total differ from used+remaining. That relation is never an integrity predicate.
- A partial external/internal update retains the existing fail-closed policy: unequal mirrors or invalid authentication block burning until explicit repair. Used is never inferred from partial records. Committed success records may survive a cut after commit even if the USB/UI has not yet displayed confirmation; they were written only after confirmed target success.

## Verification commands

Run from repository root with `python` (local Python3.14 interpreter plus existing isolated Unicorn dependency directory). Each fixture compiles the production C with ARMCC Cortex-M3/Thumb and executes it in Unicorn; only internal Flash and FatFs physical I/O are emulated.

1. `python .planning/2026-10-29-screen-ui/portrait/quota/test_stats.py`
2. `python .planning/2026-10-29-screen-ui/portrait/quota/test_usb_import.py`
3. `python .planning/2026-10-29-screen-ui/portrait/quota/test_regression.py`
4. `python .planning/2026-10-29-screen-ui/portrait/quota/test_stats_faults.py`
5. `python .planning/2026-10-29-screen-ui/portrait/quota/test_catalog_regression.py`
6. `python .planning/2026-10-29-screen-ui/portrait/quota/test_usb_guard_regression.py`
7. `python .planning/2026-10-29-screen-ui/portrait/quota/test_migration_faults.py`
8. `GD32-HID/GD32-HID/tools/.venv/Scripts/python.exe .planning/2026-10-28-per-firmware-quota/test_scoped_host.py`

All commands exited zero. Results:

- Six real-C statistical scenarios; exact total100/used1/remaining99 after success, unchanged used after failure, restart, A/B, same-total/equal-remaining reimport, unlimited, legacy unknown and forged legacy-history rejection.
- USB import joint fixture passes actual production handlers plus actual production quota code, covering main+OPT uploads, repeated stage/SET, GET, failed close, failed SET retry and unlimited same-value reset.
- Original quota/mirror regression: 17 grouped checks, including 52 debit interruption cases and rollover cases; a further 435 existing fault-injection cases across initial SET, refunds, file switching and legacy migration, plus four interrupted-migration recovery cases. The old wire-format assertions were updated for BQT4/BQC2, and rollover setup uses one-record reimports because success now writes two records; all security assertions remain.
- New successful-used/reimport fault regression: 408 cases (6 operations × 68 power cuts, write errors, FatFs errors and short transfers), plus equal-copy flags/total/used tampering rejection. A confirmed success can persist used=old or used=old+1 across a cut; begin/failure can only preserve old used.
- Authenticated catalog: 10 groups, including independent Python HMAC root checks, missing/corrupt data, partial tails, 70 firmwares and CRC migration.
- Original USB mutation guard: 9 cases. Real Python host import worker/driver: 12 tests.
- After the final legacy-parser restriction, the six baseline scenarios, all catalog groups and all 408 new statistics fault cases were run against the final source. All BQT1/BQT2 migration faults were specifically rerun: 138 injections plus four interrupted-migration explicit repairs. The earlier full regression source hash differs only by that additional BQC1-only migration predicate; both hashes are preserved in JSON evidence.

RED evidence: test_stats initially failed for missing durable getter/reimport APIs; its later added ordinary SET replay assertion failed before history-preservation fix. test_usb_import failed on actual staged upload SET not resetting used before USB integration. Forged BQC2 legacy history failed before its migration guard. All are now green. ARMCC isolated fixture builds had zero production warnings/errors.

## Limits and decisions

- Unlimited used saturates at UINT32_MAX rather than wrapping.
- Successful limited burns now append two current-state records (reservation and success); unlimited success appends one. This increases internal Flash erase frequency relative to remaining-only storage.
- Statistics unknown in legacy entries remain unknown after burns or ordinary balance adjustments; reimport begins complete history.
- Reimport recognition follows the supported GUI's main-BIN-first staged upload protocol. A raw CLI file push alone carries no imported total and does not establish new statistics.
- Main firmware build and hardware/display validation are owned by the root task. Real power loss, Flash endurance and peripheral USB timing remain physical-device verification items.

Ruling: keep the existing shared directory and isolated fixture folders because this repository is not a Git checkout; the saved originals and generated diff are the review base. Do not apply worktree/commit cleanup instructions that would remove these required evidence files.

Ruling: ordinary administrative SET changes available quota but preserves imported batch history, including when the same original SET is replayed after a successful burn. Root confirmed this interpretation; total can differ from used+remaining.

Ruling: an explicit SET/reimport can repair a transient mirror read failure if all subsequent authenticated root validation and durable paired writes succeed, as the original implementation allowed. Fault tests accept that authorized success only with identical authenticated copies and exactly reset used=0; normal begin/finish never repair an inconsistent pair.
