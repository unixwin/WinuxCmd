# A1 split artifacts (WP-H, 2026-11-14, re-dispatch on 38b3751d)

Raw hunk evidence from unwinding `rust2/wire-phase-a1` (a58b901e) into
preserve-root closure vs A1 wiring residual, re-applied onto
`origin/main = 38b3751d`.

| file | contents | use |
|---|---|---|
| `preserve-root-closure.patch` | 5 files: `chmod.rs`, `chgrp.rs`, `chown.rs`, `fsutil.rs`, `argopt.rs` — the replayed increment on 38b3751d (3000 lines). Now **includes** the batch-2 port (`usage_error(ctx,…)` signatures, `ambiguous_option`/`unrecognized_option` helpers) and the 34 `command.{chmod,chgrp,chown}.error.*` `tr!` keys — the two units share hunks and are not separable on this base | provenance of the accepted closure as it actually landed; review surface = `git diff origin/main` |
| `a1-wiring-residual.patch` | 31 files: CMake union, 10 `src/commands/*.cpp` body deletions, 15 gate TSVs + `gen-cases.py` help/version `strict→win` downgrades, `cmd-matrix.sh` A1 tables, `date/du/mktemp.rs` pre-wiring fixes (3950 lines, **base 68d2917c**, kept verbatim as the historical record of what was stripped) | **not accepted**; do not apply wholesale. The plan (`DOCS/2.0-a1-wiring-plan.md`) re-derives each piece with its own gate |

Not in the closure patch (reviewed as separate commits on this branch):
the `gen-cases.py` rows and the three regenerated permission TSVs, the
`catalog/` zh-CN patch, and `DOCS/2.0-a1-wiring-plan.md`.
