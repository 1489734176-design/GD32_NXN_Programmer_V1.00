# Independent encoding review

The approved BQT5/BQC3 encoding preserves the 96-byte journal/mirror and 48-byte catalog slot sizes. Total is independently stored in flag-word bits 2..31, remaining and used are separate full-width words. Values 0..100000 fit without overlap or shift loss; used retains all 32 bits.

BQC3 replaces the old record CRC with used. This does not remove authentication: `read_prefix` hashes all 48 bytes unconditionally after structural validation; `scan` compares the complete committed HMAC root before returning any balance or statistics; `save_stats` computes the prospective root, synchronizes the append, then scans/readback-verifies that root and the requested tuple. Domain 2 and UID-derived keys remain unchanged. Journal domain 1 authenticates the 72-byte body containing counters and catalog root. Matching mirror copies alone are insufficient without a valid HMAC.

BQT3/BQT4/BQT5 share the authenticated 96-byte scan tier, so no new migration tier or rollback fallback is needed. Old authenticated records should be read without writes and only replaced through an ordinary successful transaction. Existing BQT1/2 migration must remain restrictive, and BQT1..4/BQC1..2 must retain their original 999 caps and CRC/HMAC validation. Mixed BQC1/2/3 records retain their fixed offsets and append-only prefix history.

Final-source review and actual Thumb execution will be recorded in `quota-review.md`; the independent targeted test plan is `task/review/review_wide.py`. No product files are owned or edited by this reviewer.
