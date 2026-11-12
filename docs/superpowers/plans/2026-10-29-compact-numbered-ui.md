# Compact Numbered UI Implementation Plan

> Use subagent-driven-development; local backups replace Git-only operations.

**Goal:** Exact user layout/colors and variable-height numbered chooser.
**Architecture:** Existing direct LCD, cached fields and serial run task; no framebuffer.
**Spec:** design.md

## Tasks
- [x] UI worker: baseline-failing layout/list checks; implement yellow icons, revised main coordinates, compact numbered variable-height list. Preserve selection/OPT and count semantics; handoff source hashes/diff/report.
- [x] Independent reviewer: numbered navigation/packing boundary,9→10 digits, mixed long/short, last page/wrap, stale highlight/cache/empty/error and safety review.
- [x] Root: build main Keil0 errors/warnings; adapt only superseded display-coordinate/list assertions, run retained application/main tests, compiled buffer alignment and RAM check; inspect previews.
- [x] Package new BIN/HEX/preview/README/source manifest, preserve old releases and report hardware status honestly.

## Review focus
12short vs6long mixed fit; numbered prefix continuation indent; previous tall highlight erasure; file count changes/USB/error; no new stack/RAM overflow or regression to success-only quota.

## Next Step
Complete: release files generated and verified in output/compact-numbered-ui; physical hardware verification remains with the user.
