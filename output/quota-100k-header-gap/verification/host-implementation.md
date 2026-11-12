# Host 0–100000 verification

Changed only the product source `GD32-HID/GD32-HID/tools/daplink_manager.py`.
`BURN_QUOTA_MAX = 100000` is shared by response validation, SET input validation
and QSpinBox. Error messages and the spinbox tooltip state 0–100000. The default
remains 100; unchecked limits normalize to zero and display 不限. No packet shape,
v2 version, named scope, checksum or import completion behavior changed.

Verification used Python 3.12.14 from tools/.venv, actual PyQt5
widgets, ImportWorker, QuotaWorker and DAPLinkDevice; only the physical USB boundary
and modal dialog interaction are replaced. usb.core.find is forbidden. A fake
handle supports PyUSB `_ctx.dispose`; closeEvent exercises real disconnect.

- RED: 28 tests; 24 expected assertions failed;
  0 test errors. Existing 17 host cases passed. Failures prove
  the old 999 validators, input clamp and failed 100000 import/refresh path.
- GREEN: 28 tests passed; 0 failures,
  0 errors. The prior 17 cases remain; their superseded 1000
  invalid-response fixture is now 100001.
- Driver GET/SET boundaries: 0, 999, 1000, 65535, 65536, 99999, 100000 accepted;
  SET -1, 100001, uint32 maximum and nonintegers rejected before USB mutation.
  Decoder rejects 100001, uint32 maximum, malformed lengths/status/version/flags,
  and unlimited responses with nonzero remaining.
- Actual standalone worker saves 100000; actual import worker/callback confirms
  100000, including same-name reimport and saved quota after prepare failure.
- Six digits plus 次 fit at 1000×760, 900×600 and 1200×900. Qt recalculates the
  spinbox size hint from the maximum, so the existing 100px minimum requires no
  manual width change. Measured widths: [{'window': [1000, 760], 'spin_width': 150, 'usable_text_width': 130, 'text_width': 112}, {'window': [900, 600], 'spin_width': 150, 'usable_text_width': 130, 'text_width': 112}, {'window': [1200, 900], 'spin_width': 150, 'usable_text_width': 130, 'text_width': 112}].
- UTF-8 decode/compile succeeded; CRLF source preserved (0
  lone LF). Exact original/final hashes are in source-hashes.json and test JSON.

The repository documents no general automated suite. This runs all 17 existing
offline host regression cases plus the 11 new range/worker/Qt cases. No physical
USB enumeration, upload, flashing, reset or Keil build occurred in this worker.
This is host-side verification; root handles firmware integration and builds.

Artifacts: red-results.json, green-results.json, source-hashes.json, host.diff,
qt-widths.json, and three actual Qt render PNGs. Windows Qt offscreen omits all
text in those PNGs, as previously recorded in the 2026-10-27 host review; these
images show control geometry only and do not prove native textual appearance.
The real edit text and font metrics are checked directly by the fitting test.
Test setup writes only this
task's host fixture directory; earlier test files are read-only inputs.
