# Quota 100000 verification

Implemented the approved encoding in three UTF-8 source files only. New max is
100000; fixed BQT5 magic 0x35545142 uses the existing 96-byte authenticated slot,
state=enabled|(known<<1)|(total<<2), full remaining and full used words. Fixed BQC3
magic 0x33435142 uses the existing 48-byte slot with state at36, remaining at40,
used at44. HMAC authenticates all48 bytes; write/readback verification precedes
publishing its root. New writes use BQC3 even for unknown history. BQT1..4 and
BQC1..2 retain their original decoding and 999 limits, old CRC checks and HMAC.

Authentication domain/key/UID derivation, mirror format/size, port and buffers
are byte-identical to the baseline. BQT3/4/5 share the existing authenticated
scanner tier and do not auto-upgrade at boot. No inferred total, no legacy stats
reset. SET preserves history; reimport resets only that firmware; only confirmed
success decrements, and used saturates at UINT32_MAX.

All tests execute ARMCC Cortex-M3 Thumb production quota/catalog/mirror/auth code
in Unicorn. Only physical internal Flash, FatFs media I/O and hardware UID reads
are emulated. Main Keil build and hardware deployment are owned by the root task;
this worker performed no physical flashing/reset and makes no hardware claim.

## Evidence

- RED: baseline rejects valid1000 with BURN_QUOTA_STORAGE (2), exactly as expected.
- Wide behavior: 11 groups PASS. Range0/999/1000/65535/65536/99999/100000; reject
  100001 before mutation; begin/failure/reboot unchanged; 100000->99999 with used1;
  A/B switch/reboot, independent SET history and same-name reimport.
- Compatibility/auth: 6 groups PASS. Actual prechange ARMCC firmware generates
  BQT4/BQC2 active/inactive history; new firmware retains it. Prior1/2/3 unknown
  history and mixed catalogs pass; six authenticated-version sequence orderings
  select the newest without upgrade writes; old caps remain999. Full32-bit used,
  0xfffffffe and saturation pass. Signed invalid new schemas and old limits reject.
  Independent Python HMAC confirms unchanged domains/UID prefix and catalog root.
- Security regression: 10 catalog +8 journal groups PASS. Historical sameCRC
  replay remains blocked; missing/short/changed/replayed media fail; explicit SET
  repair preserves history; malformed roots/names reject; CRC-only roots cannot
  promote BQC2/3; 70 catalog names survive. New used/remaining word corruption
  immediately after write prevents publishing a root/internal record.
- Fault injection: 509 cases PASS in 7 scenarios. Every media mutation
  and FatFs occurrence is covered for wide success, rollover, first old->new,
  catalog switch and reimport. Restart permits only a complete old/new tuple or
  a fault-locked state; begin and duplicate finish never repair/change storage.
  Authorized reimport can repair an initial mirror read failure only by fully
  committing and re-verifying the requested new tuple; persistent/read-after-write
  errors and ordinary-success/switch failures remain blocked.
- All isolated ARMCC compiles complete with zero errors and warnings. Quota-only
  ELF region sizes (bytes): before {'ER_RO': 6040, 'ER_RW': 32, 'ER_ZI': 1608}; after {'ER_RO': 6160, 'ER_RW': 32, 'ER_ZI': 1608}. RW/ZI remain unchanged.

| Fault scenario | Cases | Injection breakdown |
|---|---:|---|
| 100000 successful commit | 69 | power_cut=26, write_error=26, fatfs_error=14, short_io=2, readback_corruption=1 |
| 100000 success across journal page rollover | 71 | power_cut=27, write_error=27, fatfs_error=14, short_io=2, readback_corruption=1 |
| First BQT4 success becomes BQT5 preserving old history/catalog | 75 | power_cut=26, write_error=26, fatfs_error=20, short_io=2, readback_corruption=1 |
| Wide catalog append and A/B root commit | 115 | power_cut=27, write_error=27, fatfs_error=42, short_io=18, readback_corruption=1 |
| First BQC2/BQT4 switch appends BQC3 and commits BQT5 | 105 | power_cut=27, write_error=27, fatfs_error=37, short_io=13, readback_corruption=1 |
| Reimport wide total commits reset tuple atomically | 71 | power_cut=26, write_error=26, fatfs_error=14, short_io=4, readback_corruption=1 |
| Begin read errors cannot create success session | 3 | fatfs_error=3 |

## Exact commands

Run from D:/mym/NXN/6.Programmer/DAPlink. RED intentionally exits1. The first full
fault run completed five scenarios, then stopped on an overstrict fixture
assertion for authorized initial-read reimport repair. After independent review,
only that fixture expectation was corrected; production source did not change.
test_reimport_tail.py re-ran the corrected reimport scenario and pending begin
cases successfully. Completed first-five PASS tool-output receipts were retained
with explicit provenance and source/binary hashes rather than rerun. The final
merged result has all seven scenarios passing. The corrected full fault script
is available to reproduce all scenarios from scratch; this completion used the
recorded first five plus fresh tail. All other verification commands exit0.

```powershell
& 'D:/Program Files/Python314/python.exe' -B '.planning/2026-10-30-quota-100k-header-gap/quota/test_wide.py' --before
& 'D:/Program Files/Python314/python.exe' -B '.planning/2026-10-30-quota-100k-header-gap/quota/test_wide.py'
& 'D:/Program Files/Python314/python.exe' -B '.planning/2026-10-30-quota-100k-header-gap/quota/test_compat_auth.py'
& 'D:/Program Files/Python314/python.exe' -B '.planning/2026-10-30-quota-100k-header-gap/quota/test_wide_faults.py'
& 'D:/Program Files/Python314/python.exe' -B '.planning/2026-10-30-quota-100k-header-gap/quota/test_security_regression.py'
& 'D:/Program Files/Python314/python.exe' -B '.planning/2026-10-30-quota-100k-header-gap/quota/test_reimport_tail.py'
& 'D:/Program Files/Python314/python.exe' -B '.planning/2026-10-30-quota-100k-header-gap/quota/merge_fault_results.py'
& 'D:/Program Files/Python314/python.exe' -B '.planning/2026-10-30-quota-100k-header-gap/quota/make_report.py'
```

Result files: red-results.json, wide-results.json, compat-auth-results.json,
security-regression-results.json, wide-fault-results.json. source-hashes.json
binds each result to the actual source; ../quota.diff compares exact backup
sources. Historical fixtures/tests are loaded read-only; generated fixture
sources, objects, maps and expanded catalog wrapper stay in this quota folder.
Fault provenance: fault-first-run-output-receipts.txt,
wide-fault-initial-results.json, reimport-tail-results.json and the merged
wide-fault-results.json; reviewer proof is ../task/review/reimport-repair-results.json.
