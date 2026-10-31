# Speed-matrix benchmark baseline

`scripts/benchmark-speed-matrix.py` compares the built WinuxCmd
dispatcher against GNU coreutils (MSYS/Git toolchain) and uutils
coreutils on the hot-path workloads of `DOCS/performance-roadmap.md`
(large cat / cp / wc / hashes / sort / uniq). Every workload's output
is hash-verified across implementations before its timings are
trusted; a workload whose outputs disagree is reported as a mismatch,
never averaged in.

## Seeding the baseline

`baseline.json` in this directory is the checked-in baseline. It ships
unseeded (empty `entries`): the first run on a Windows machine with a
Release build records real numbers:

```powershell
python scripts/benchmark-speed-matrix.py `
  --build-dir build-x64 `
  --gnu-root "C:\Program Files\Git\usr\bin" `
  --uutils-root <dir with uu-*.exe or coreutils.exe> `
  --record-baseline tests\benchmark\baseline.json
```

Commit the regenerated `baseline.json` (its `environment` block
records host, OS and CPU count so later comparisons can judge whether
the two runs are even comparable).

## Comparing

```powershell
python scripts/benchmark-speed-matrix.py --build-dir build-x64 `
  --baseline tests\benchmark\baseline.json
```

The comparison is **report-only** by default: it prints per-workload
deltas and flags regressions beyond 15%, but exits 0.

## Why there is no hard CI gate yet

Wall-clock medians on shared CI runners swing by more than the
regressions worth catching (this repository's own CI history shows
double-digit percentage run-to-run variance on identical code), and
there is no dedicated performance runner. A hard gate today would
train everyone to ignore or bypass it. The adopted path, mirroring
the report-first pattern of `tests/differential`:

1. The `benchmark-speed-matrix` workflow runs on demand and weekly,
   uploads the JSON/Markdown matrix as artifacts, and appends the
   baseline comparison to the job summary — regressions are visible
   in every run without blocking merges.
2. Once the baseline is seeded and ~10 scheduled runs have
   characterized the variance on the windows-2025 pool, revisit a
   hard gate scoped to the large-file workloads (where signal/noise
   is best), e.g. `--fail-on-regression 25` on a dedicated label.
   CodSpeed-style instrumentation (the pattern uutils uses) is the
   alternative if runner variance stays too high.
