# Independent final quota review

**Result: no P1/P2 findings.** Reviewed the final diff of `burn_quota.c`, `burn_quota.h`, and `burn_quota_catalog.c` against this task's untouched originals. Authentication/mirror source and headers are byte-identical to the originals. No product files were edited by this reviewer.

The implementation matches the approved fixed-size BQT5/BQC3 encoding. BQT5 retains a 96-byte authenticated slot, stores total above the two flags, and stores full remaining/used words. BQC3 retains a 48-byte record, with full remaining at byte 40 and full used at byte 44. The HMAC path authenticates the full catalog prefix before exposing state and verifies the newly written prefix before publishing its root. The two-page scanner continues to treat BQT3/4/5 as one authenticated tier, and older CRC migrations cannot authenticate BQC2/3 history.

No static buffers or API field widths changed. New encoding shifts are bounded by 100000; legacy decoders retain 999 limits. Commit ordering, paired-mirror authentication, sequence handling, success-only debit, and explicit-SET repair logic are unchanged. Main firmware RAM/build and USB/UI integration are checked separately by the root implementer.

Fresh independent verification used ARMCC Cortex-M3 Thumb compilation of the production quota/catalog/mirror/authentication code, executed with Unicorn. Only physical Flash, UID reads, and FatFs media calls were emulated. Private compilation completed with zero warnings; all seven targeted test groups passed:

- Exact 0/999/1000/65535/65536/99999/100000 persistence, 100001 rejection without storage changes, and success-only debit.
- Wide A/B inactive balances and history through reboot, administrator SET preserving history, and same-name reimport resetting it.
- BQT3/BQT4 read without automatic writes; mixed BQC1/BQC2/BQC3 append-only history preserving an inactive `used=0x12345678` and unknown legacy history.
- BQT1/BQT2 migration, unchanged old 999 limits across BQT1..4/BQC1..2, and old CRC enforcement even beneath an authenticated root.
- Validly authenticated but malformed new fields rejected; `used=0xFFFFFFFF` saturates while limited remaining still decrements and survives the catalog round trip.
- Catalog name/state/remaining/used tampering, matching forged journal/mirror copies, and cross-UID copies rejected.
- Twelve representative first BQT4-to-BQT5 power-cut/write-error boundaries retain a complete old/new `(remaining,total,used)` tuple or remain locked; inactive catalog history remains intact.

Commands:

```powershell
python -B .planning\2026-10-30-quota-100k-header-gap\task\review\review_wide.py
python -B .planning\2026-10-30-quota-100k-header-gap\task\review\source_review.py
```

Artifacts: `task/review/review-results.json`, `task/review/source.diff`, and `task/review/fixture/dual-fixture/current/compile.log`. `source_review.py` confirms that the reviewed/compiled bytes still match current product files. No hardware was flashed or reset. Targeted fault checks complement the implementation worker's broader fault enumeration; they do not model electrical brownout behavior.

Reviewed SHA-256:

| File | SHA-256 |
|---|---|
| burn_quota.c | `7f785934d630db23e5895eb0e46da55bdb6fa4d7e2f2e527360e313d96907c34` |
| burn_quota.h | `0e8eb55385d52305e0c12b6702699720d96214e77cf8e72f5820bbcb519c0c1c` |
| burn_quota_catalog.c | `8ee732b8ae53ad2aeb2abce6856ee32a9635a107522e2981771126cfee5a45a6` |
| quota_auth.c | `9e5f1506ed4804dabaebdc3239fee50687ad6aaf352d324c5fcc41d61ea67ea9` |
| burn_quota_mirror.c | `e84466344f2a1d01b81a99828977e925bcd84911c52371971cecafce2940e0ad` |

## Narrow reimport fault assertion follow-up

The implementation worker's initial fault test required STORAGE for every injected FatFs error during explicit reimport. The specific failure, first mirror `f_open`, instead returned OK after repairing the current quota pair. This is intended existing behavior: `set_quota` first authenticates the committed catalog, then allows an explicit administrator SET/reimport to append a repaired pair after `verify_pair` marks the current pair faulty. This branch is unchanged from the baseline; normal begin/success paths do not gain this repair permission.

Independently reused the already compiled, hash-matched Thumb binary for six narrow checks, without another build or broad test run. One-shot prewrite mirror `f_open`, `f_read`, `f_close`, and short `f_read` each produced the complete requested `(remaining=100000,total=100000,used=0)` tuple, identical journal/mirror bytes, 28 repair mutations, and the same valid state after restart with authenticated begin. Persistent read failure returned STORAGE and remained locked after restart. A failure opening a nonempty committed catalog returned STORAGE with zero writes.

Conclusion: correct only the reimport-specific prewrite error expectations, requiring the exact complete new tuple, paired records, and reboot authentication whenever OK is returned. Keep other failure cases strict. No product fix is indicated. Evidence: `task/review/reimport_repair_probe.py` and `task/review/reimport-repair-results.json`; all reviewed product hashes remain unchanged.
