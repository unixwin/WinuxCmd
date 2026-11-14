# x64/default leg: what actually times out, and why caching cargo is not the answer

Measured 2026-11-14 against `build-winuxcmd.yml` runs
`43569316496` … `43417020857` (the last 30 runs at the time of writing).
Raw job/step data: `jobs30.json`; sampled step logs: `ctest-long*.log`,
`ctest-ok.log` (not committed).

## TL;DR

* `rust2-surface-check` has no matrix and no "x64-default leg". It is a single
  7-second job; in the 30-run window it never failed and never timed out.
  It is **skipped**, not slow, whenever the x64/default build leg fails.
* The slow/timeout reports are the `build-windows (x64, x64, x64, default)`
  leg. The long pole is `Run Unit Tests (ctest)`, not cargo.
* Root cause of the 58–73 minute legs: under runner contention the Windows
  FIFO bridge tests hang until ctest's 120 s per-test ceiling. The
  `ctest-gate.ps1` serial flake retry then re-runs every failed test with the
  same 120 s ceiling, paying the timeouts twice (measured 24–30 min of retry
  for at most 4 recovered tests, and the gate stayed red).
* Cargo is **not** the cause and a `rust2/target` cache is **rejected**:
  at most a couple of minutes per x64/default leg, with stale-object and
  build-authenticity risk. The existing registry cache never populated at all
  (`~/.cargo/registry` does not exist because the workspace has no external
  crates) and has been removed.

## Leg durations (minutes, 30 runs)

Four of the 30 runs had not started the x64/default leg yet (queued),
so that leg has n=26.

| job | n | median | P95 | max | failures |
|---|---|---|---|---|---|
| build-windows (x64, x64, x64, default) | 26 | 35.1 | 72.5 | 72.6 | 15 |
| build-windows (x64, x64, x64, full) | 30 | 3.5 | 3.9 | 4.3 | 0 |
| build-windows (arm64, x64_arm64, arm64, default) | 30 | 3.6 | 4.1 | 4.3 | 0 |
| build-windows (arm64, x64_arm64, arm64, full) | 30 | 3.7 | 4.0 | 4.0 | 0 |
| rust2-surface-check (11 executed, 15 skipped) | 26 | 0.1 | 0.1 | 0.2 | 0 |
| rust-tests | 30 | 0.8 | 0.9 | 1.0 | 2 |
| i18n-drift | 30 | 0.1 | 0.2 | 0.2 | 3 |

x64/default split: green runs n=11, median 30.0 min (24.0–35.4);
red runs n=15, median 67.2 min (27.4–72.6). The red population is bimodal:
four fast failures (27–35 min, of which one ctest step took 1.8 min) and
eleven legs at 58.2–72.6 min where ctest itself consumed 33–40 min.

`rust2-surface-check` executed 11 times: 5–11 s, median 7 s, P95 9 s.
It reported `skipped` 15 times, exactly when the x64/default leg failed.

## Where the 30–40 minutes go

`Run Unit Tests (ctest)` step over 26 x64/default legs: median 1.8 min,
P95 39.5 min, max 39.8 min, 15 failures. On a green run the whole 3864-test
suite finishes in 95.08 s.

Representative failing timelines:

| run | parallel ctest | failures | serial retry | retry result | ctest step | job |
|---|---|---|---|---|---|---|
| 43447059379 | 541.98 s | 22 / 3862 | 1440.92 s | 18 still failed | 33.1 min | 66.2 min |
| 43433809874 | 582.14 s | 22 / 3862 | 1801.11 s | 21 still failed | 39.8 min | 67.2 min |
| 43430200028 | 562.73 s | 21 / 3840 | 1800.80 s | 21 still failed | 39.5 min | 72.6 min |

Timeouts are the Windows FIFO family: `***Timeout` lines in the three sampled
logs were 26 / 31 / 30 events; distinct tests 13 / 16 / 15, dominated by
`mkfifo.*` (9) and `head.head_fifo_operand_*` (6), plus one `seq` timeout in
one run. The same tests pass in 0.1 s on a healthy runner (run 43563419594,
`100% tests passed out of 3864`, 95.08 s), so this is contention-induced
hanging, not a permanent platform exclusion.

All 11 timeout-cluster runs were pushed on 2026-11-13 between 11:26 and
15:59, when the workflow had up to ten runs in flight (four build legs
each). A
parallel run that trips 13 distinct 120 s timeouts at `-j 4` already costs
~9–10 min; the serial retry then re-pays each timeout one at a time, which is
the entire 24–30 min delta.

## Cargo is not the bottleneck

The x64/default leg configures three separate build trees, each with its own
`rust2-target`, so cargo builds the three-crate workspace (std only, no
external crates) three times:
`build-x64`, `build-x64-pgo-train`, `build-x64` again for the PGO rebuild.

Bound from the step data (run 43563419594):

* `PGO instrumented build (x64)`: 1.53 min total for a fresh CMake configure
  **plus** cargo **plus** the full 609-target compile with tests OFF. Cargo is
  a strict subset of that.
* `Build Project`: 8.10 min (tests ON; thousands of C++ test TUs).
* `PGO rebuild with profile (x64)`: 9.27 min (tests ON again).

So caching cargo compile output can save at most a couple of minutes on a
30-minute green leg and nothing on the timeout pathology.

## Cache evaluation

| candidate | measured benefit | risk | decision |
|---|---|---|---|
| `~/.cargo/registry`, `~/.cargo/git` (`%CARGO_HOME%` is `C:\Users\runneradmin\.cargo` on the hosted runner) | none today: workspace has zero external crates, the path does not exist, so `actions/cache` saves nothing and logs a path-validation warning every run | low | removed (re-add only when a dependency lands) |
| pinned rustup toolchain (`~/.rustup/toolchains/1.99.1-*`) | real: cache restores in 0.07 min and carries the aarch64 std lib | low (toolchain is pinned and content-addressed by version) | kept |
| `rust2/target` (or `build-<arch>/rust2-target`) | ≤ ~2–3 min per x64/default leg; zero for the timeout problem | **rejected** — see below | not added |

Why `rust2/target` stays uncached:

1. **Staleness.** Cargo fingerprints embed source mtimes. A fresh checkout
   gives every source file a new mtime, so a restored target dir is either
   ignored (no gain) or, with a coarse restore-keys match, can hand back
   objects compiled from a different tree. The gate is a byte-level
   differential; supplying stale objects to the binary under test is the same
   failure class as the gate01 stale-overwrite incident
   (`DOCS/2.0-HANDOVER.md`, "零、续作检查点").
2. **Authenticity.** The artifact this leg produces is the shipped
   `winuxcmd.exe` (and the PGO profile used to rebuild it). Build inputs
   should be reconstructed from source in the run that publishes them.
3. **PR cache scoping.** `actions/cache` does isolate PR writes: caches saved
   by a `pull_request` run are scoped to that PR's merge ref and are not
   restorable by `main` or by other PRs, and fork PRs get read-only access to
   base-branch caches (entries are immutable, so a fork cannot overwrite a
   base-branch key; it can only restore a poisoned entry into its own run,
   which affects that PR's verdict alone). That removes the classic
   fork-poisoning path, but it does not remove (1) or (2), and same-PR
   re-runs still share the PR-scoped cache. If a target cache is ever
   justified, the key must contain `github.sha` with no `restore-keys`, and
   it must not be used by the release/tag job.

## Changes made with this analysis

* `scripts/ctest-gate.ps1`: the serial flake retry now uses
  `-SerialRetryTimeoutSeconds` (default 30 s) instead of the parallel 120 s
  ceiling. Fail-closed: a real hang still fails, it just fails in 30 s. Worst
  observed retry drops from ~30 min to ~6–7 min for the same failure set.
* `build-winuxcmd.yml`: PR concurrency group with `cancel-in-progress`, so a
  branch push cancels its superseded run instead of queueing a fifth
  four-leg workflow; `timeout-minutes` backstops on `build-windows` (90),
  `rust2-surface-check` (10) and `rust-tests` (30); the dead cargo registry
  cache steps were removed.
* No test selection changed. The FIFO tests are healthy tests that hang only
  under contention; excluding them would hide a real hang and is not needed
  once the retry budget is bounded.
