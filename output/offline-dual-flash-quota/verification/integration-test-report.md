# Final verification — 2026-10-29

Final application SHA256: 498fec04f012e486b9d48ab2c41bb592c003c3d4d97455628f98d9f1162c5b5d; 64708 bytes.
Keil full rebuild: 0 errors, 0 warnings. BIN and HEX match; code ends before 0x0801F000.

- Actual production ARM Thumb quota/core/catalog/mirror: 16 groups passed, including paired debit/refund, same-CRC catalog replay, missing/stale/tampered media, UID binding, legacy upgrade, page rotation and 52 debit cut/error positions.
- Additional current-source fault matrix: all 5 groups passed. Initial SET, refund, known/unknown firmware switch and BQT1/BQT2 migration cover 435 cut/error cases plus 4 explicit migration recovery cases.
- SHA256/HMAC: 6 groups passed, published SHA256/RFC4231 vectors, streaming boundaries and tag comparisons; source hash checked.
- Independent catalog verification: 10 groups passed, Python HMAC reference and same-length/same-CRC replay included.
- Idle validation: repeated same-name selection avoids media reads; startup and actual burn still detect live mirror/catalog mutations.
- Complete final Keil image: 13 groups passed, covering startup chip/firmware/OPT/SUM32, current selection persistence, named USB reimport, per-firmware counts, manual/automatic zero blocking and missing catalog.
- Existing host 12 tests, scoped USB 9 tests and mutation guard 9 tests passed. Old BQT2/CRC-only quota fixtures are superseded by the production BQT3 suites because record/authentication format and unknown-firmware policy changed.

Only peripheral media/UID/RTOS/display interfaces were simulated. No physical programmer was written or tested. The final call graph estimates 1916 bytes plus unknown indirect/cyclic paths against the 4096-byte main-task stack; this is not a measured hardware high-water mark.
