# Changelog

All notable changes to WinuxCmd are documented here. Format follows
[Keep a Changelog](https://keepachangelog.com/); versions follow the
repository release tags.

## [Unreleased]

## [1.1.0] - 2026-09-21

### Fixed — GNU/upstream parity (audit rounds 1-2, 178 commands audited)

- **grep**: conflicting matchers are fatal (exit 2); `-q` keeps error
  diagnostics while suppressing output; `-L` exits 0 iff any line was
  selected anywhere; `-Z` NULs only the filename separator; an empty `-f`
  pattern file selects nothing (exit 1); input opens report
  `grep: FILE: <strerror>`.
- **date**: military zone `J` under `-u` resolves "local" to UTC
  (`date -u -d 1024j` = 10:24, matching GNU).
- **find**: `-exec CMD \;` no longer poisons the exit status; `-exec {} +`
  stays true on child failure; `-execdir` passes `./basename` and batches
  per-directory; findutils-default regex dialect; `-printf` width/precision;
  `-daystart`; `-xdev`.
- **sed**: I/O errors exit 2 (usage stays 1); `\L \U \u \l \E` case
  conversion in replacements; `y///` escapes; `r`/`R` diagnostics;
  `sed -i` with stdin is a hard error (exit 4); `m`/`M` flags;
  `--debug`/`--follow-symlinks` accepted.
- **patch**: engine rework — multi-file patches, blank-line context,
  `\ No newline at end of file`, GNU per-hunk reporting and exit codes
  (0/1/2), default `FILE.rej`, backup-if-mismatch, upstream fuzz/offset
  search.
- **findutils family**: xargs `-r`/`-x` semantics; locate GNU option
  surface (`-l LIMIT`, glob/regex modes, exit 1 on no-match); updatedb
  `--localpaths`/`--prunepaths` and atomic database replace.
- **ls**: built-in default color table (gnulib) when `LS_COLORS` is unset.

### Added

- MSVC **PGO build pipeline** (`scripts/build-pgo.ps1`, CMake
  `WINUXCMD_PGO_INSTRUMENT`/`WINUXCMD_PGO_USE`): training workloads ported
  from uutils `build-pgo.sh` plus the differential corpus. Measured:
  `sort` −18%, `nl` −17%, `fold` −12%, binary size −12% (3.95 MB for 180
  commands).
- Differential audit reports for all 178 commands (coreutils 9.7,
  findutils 4.10.0, grep 3.12, sed 4.9, patch 2.7.6, util-linux 2.41,
  procps-ng 4.0.4 sources; wpm audited against its own specification).

### Removed

- `skills/` agent-skill package (superseded by niubash).
- `scaffold` generator and its DSL docs.
- `BUILD_STANDALONE` broken per-command executable path.
- UPX packing integration (AV false-positive risk; see
  `docs/performance-roadmap.md`).
- Dead FFI library and examples.
