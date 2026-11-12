# Task 2 — debit only on confirmed success

Completed production changes: `GD32-HID/GD32-HID/USER/APP/burn_quota.c` and `.h` only.

- `begin()` authenticates internal Flash, external mirror and committed catalog, checks zero/fault state, then opens a RAM session. It makes no persistent writes and does not change remaining/used.
- `finish(0)` closes the session with no media calls or refund. Reset also discards the RAM session.
- `finish(1)` authenticates again, then commits remaining-1 and used+1 in one existing BQT4 transaction. Unlimited preserves remaining=0 and advances known successful-used. Legacy unknown stays unknown while limited balance still decreases. Saturated used never wraps.
- Existing active-session guards continue blocking firmware selection, SET, reimport and another begin, including unlimited sessions. Duplicate finish cannot add another debit or successful-used, including after a storage error.
- BQT4/BQC2 byte layouts, HMAC/device binding, per-firmware accounting and same-limit reimport are unchanged.

## Verification evidence

All tests execute production ARMCC5 Cortex-M3 Thumb through Unicorn. Only physical internal Flash, FatFs and device ID ports are emulated. Independent fixtures compile without warnings/errors; main Keil build and hardware validation belong to root integration.

Run from the repository root with Python `D:/Program Files/Python314/python.exe`:

| Command suffix | Result | Evidence |
| --- | --- | --- |
| `quota/test_success_commit.py --before` | Expected RED: begin reduced 100 instead of preserving it | quota/red.log, red-results.json |
| `quota/test_success_commit.py` | PASS 4 scenario groups | quota/success.log, success-results.json |
| `quota/test_commit_faults.py` | PASS 215 injected cases | quota/commit-faults.log, commit-fault-results.json |
| `quota/test_regression.py` | PASS BQT3 + 6 history + 13 authentication groups; 373 prior transaction fault cases | quota/regression.log, regression-results.json |
| `quota/test_catalog_regression.py` | PASS 10 catalog groups | quota/catalog.log, catalog-results.json |
| `quota/test_reimport_faults.py` | PASS 136 reset transaction fault cases | quota/reimport.log, reimport-results.json |

Every command suffix above is relative to `.planning/2026-10-29-ui-optimizations/` and invoked with `python`.

Success commit enumeration covers all 26 normal media mutations and all 27 rollover mutations, each as power cut and write failure, every FatFs call occurrence, short read/write and post-write corruption. Restarted records must expose exactly the complete old tuple or complete new tuple; a mismatched/unreadable pair must block begin without repair. Faulted begin does not create a finishable session. Tests compare both internal and external media byte-for-byte before successful confirmation.

Ruling: previous historical expectations that interrupted begin retains a predebit and failed finish persists a refund are explicitly superseded by the user's success-only/interrupt-no-charge requirement. New local wrappers adapt these assertions; the original historical files are untouched. Expanded adapted source is saved as `quota/expanded_*.py.txt`. Old predebit/refund fault tests are replaced by stronger joint remaining/used success-commit checks, not used to preserve old behavior.

Ruling: this repository is not Git; original source backups, `quota.diff` and `quota-source-manifest.json` provide the review boundary. No main Keil build, host/device operation or extra reviewer was dispatched by this implementer.

Independent review is coordinated by root. Physical power loss on target hardware remains outside this emulated verification.
