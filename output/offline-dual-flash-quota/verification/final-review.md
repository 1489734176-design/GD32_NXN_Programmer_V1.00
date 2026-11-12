# Final review — dual Flash quota

This file records the independent `/root/final_review` findings delivered to the root agent and the root's verified dispositions. The reviewer inspected the changed quota core, catalog, mirror, authentication, port and integration paths against the saved baseline. No Git checkout exists; the baseline is `original/` under this plan.

## Outcome

No remaining identified authorization bypass, rollback acceptance within the stated single-media threat model, cross-firmware balance mix-up, or stack overflow issue. Final core source SHA256: `ce2f3134eca4d597f178b302cfceaa5e1cfe0fce0393db9d5c9502ff8539e44b`.

## Findings addressed

1. **Important: repeated idle authentication traversed the entire historical catalog.** `save_program_selection` binds the selected filename every main-loop iteration before checking whether the selection is dirty. Initially the same-name branch called `verify_pair` every time. Independent production Thumb measurements for 0/64/512 catalog records were 102,925/506,471/3,244,197 instructions and 1/65/513 FatFs reads per idle bind. Root reproduced this with `test_idle_validation.py`, then added `quota_checked` only to the same-name idle binding branch. First selection after startup still verifies; begin, SET, actual firmware switch and refund still physically reread/authenticate. RED and GREEN observed; current-source idle verification passes.
2. **Important: interrupted legacy migration could not be explicitly repaired.** A power cut after creating/writing the external mirror but before the first internal word correctly blocked burns, but `legacy_pending` also blocked administrative SET. Root and test agent reproduced it, then allowed explicit selection/SET while retaining the legacy burn prohibition. SET validates the old catalog, upgrades its root and commits both copies. Valid inactive firmware balances are retained in the tested interrupted-mirror cases. Recovery RED and GREEN plus the final fault matrix pass.

## Deferred minor / known limitation

Historical catalog validation remains linear in historical length. With 512 records (24 KiB), the reviewer measured 6,527,827 instructions/1,027 FatFs reads for begin and 3,243,916 instructions/513 reads for successful finish in the peripheral-stubbed Thumb fixture. These are instruction counts, not hardware timings. Idle full scans have been removed; catalog compaction is not part of this request. Long-running hardware performance/endurance testing remains outstanding and is documented in the delivered README.

## Bounds

The final Keil image has zero compiler/linker errors or warnings. Its call graph estimates 1,916 bytes plus unknown indirect/cyclic paths for the deepest chain, against a 4,096-byte main-task stack. New media calls are outside outer IRQ-disabled sections; only individual FMC operations enter critical sections. The application ends before `0x0801F000`, preserving selection and quota pages.

HMAC protects record integrity and binds the device UID. Restoring both internal and external Flash to the same old snapshot, source/firmware-key extraction, the existing administrative SET interface and unsigned bootloader replacement are not stronger protections delivered by this two-copy scheme. No changes to hardware read protection or target algorithm format were requested or made.

The complete final application image subsequently passed all 13 integration groups; source-matched core, crypto, catalog, idle and fault suites passed. Physical hardware tests were not performed.
