# A1 split artifacts (WP-H, 2026-11-14)

Raw hunk evidence from unwinding `rust2/wire-phase-a1` (a58b901e) into
preserve-root closure vs A1 wiring residual. Both patches are against the
pre-WIP base `68d2917c`.

| file | contents | use |
|---|---|---|
| `preserve-root-closure.patch` | 5 files: `chmod.rs`, `chgrp.rs`, `chown.rs`, `fsutil.rs`, `argopt.rs` (2427 lines) | provenance of the accepted closure; re-apply on the then-current `origin/main` with `git apply --3way` |
| `a1-wiring-residual.patch` | 31 files: CMake union, 10 `src/commands/*.cpp` body deletions, 15 gate TSVs + `gen-cases.py` help/version `strict→win` downgrades, `cmd-matrix.sh` A1 tables, `date/du/mktemp.rs` pre-wiring fixes (3950 lines) | **not accepted**; do not apply wholesale. The plan (`DOCS/2.0-a1-wiring-plan.md`) re-derives each piece with its own gate |

The closure patch does NOT contain this lane's follow-ups: the `Ctx::new`
migration of the closure tests (forced by #1636's host seam), the `tr!`
migration of the permission-family messages, the fsutil refusal-line
signature change, and the gate rows added in `gen-cases.py`. Those live in
the working tree and are reviewed as `git diff origin/main`.
