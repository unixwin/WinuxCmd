# Changelog

All notable changes to WinuxCmd are documented here. Format follows
[Keep a Changelog](https://keepachangelog.com/); versions follow the
repository release tags.

## [Unreleased]

## [1.1.3] - 2026-09-24

### Fixed — diff rewritten to GNU diffutils 3.10 output parity

- **diff**: every output format byte-compared against GNU diffutils 3.10
  (25/25 golden cases): normal, unified, context, side-by-side (GNU tab
  gutter layout with the exact half-width/gutter formula), ed script (-e),
  forward ed (-f), RCS (-n), --ifdef, --line-format and the
  --GTYPE-group-format family, --from-file/--to-file.
- **diff**: options that were declared in help but rejected at runtime are
  now implemented: -e/-f/-n, -D/--ifdef, -p/--show-c-function,
  -F/--show-function-line, -r/--recursive directory comparison with
  -x/-X/--exclude-dir/-S filters, -l/--paginate (pr-style 66-line pages),
  --color[=WHEN], --left-column, --horizon-lines, --speed-large-files,
  -d/--minimal accepted (the LCS is already minimal).
- **diff**: -Z rebinding per GNU 3.4+: -Z is now --ignore-trailing-space;
  --strip-trailing-cr keeps its long form; -E/--ignore-tab-expansion added.
- **diff**: hunk grouping distance fixed — the old heuristic never split
  hunks; now measures the common-line gap change-to-change (context 0-3
  outputs match GNU for the first time).
- **help**: the i18n catalog's 112 placeholder strings ("新增选项：x")
  replaced with real translations (synced in winuxcmd-i18n 0.6.13);
  diff help text now matches GNU wording.
- **parser**: glued values now accepted on int options (diff -U1, head
  -n5 style), matching GNU; usage errors (missing/extra operands) exit 2
  as GNU does.

### Added

- **PGO in release CI** (x64): the tag build now runs the instrumented
  build, collects profiles via `scripts/pgo-train.sh`, and relinks with
  `/USEPROFILE` — matching the local `scripts/build-pgo.ps1` pipeline
  (~12% smaller, faster hot paths). ARM64 stays non-PGO: profiles must be
  collected on the target architecture.

### Removed

- **link** command (again): its hardlink-style name shadows the MSVC
  toolchain linker `link.exe` on PATH. Added a CMake reserved-command
  blacklist guard (`WINUXCMD_BANNED_COMMANDS`) that fails configuration
  with the command name and rejection reason if the source ever reappears;
  the rule is also recorded in `AGENTS.md`.

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
