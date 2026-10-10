# Changelog

All notable changes to WinuxCmd are documented here. Format follows
[Keep a Changelog](https://keepachangelog.com/); versions follow the
repository release tags.

## [Unreleased]

### Fixed

- **i18n**: the WPM message catalog resolves at the WinuxCmd install root, so
  localized messages work when `winuxcmd.exe` is invoked through a shim or
  PATH entry outside the install directory (#1151).
- **touch**: GNU-parity handling of unmatched literal-glob operands — names
  that glob to nothing are treated as missing under `-c` (skipped) instead of
  being created verbatim, matching GNU coreutils (#1152).
- **wpm**: interaction-audit fixes (#1156). `wpm outdated` now anchors
  installed versions on install receipts, so it keeps reporting a pending
  update after `wpm index update` instead of flipping to "all up to date"
  while old files are still on disk. `wpm install --json` prints one
  machine-readable payload (statuses `installed`, `already_installed`,
  `would_install`, `not_found`, `not_installable`, `preflight_failed`,
  `download_failed`, `error`) with all human progress routed to stderr, and
  `wpm index update --json` prints a matching payload. `wpm --help` now
  documents the `install` `--force` reinstall/overwrite semantics. Verified
  by measurement: piped `y`/`n` confirmation on `wpm uninstall` stdin (and
  `-y` in any argument position) already behaved correctly on this branch,
  and locked destination files already fail loudly with exit 1 on both
  install and uninstall; regression tests pin all of these behaviors.

## [1.1.5] - 2026-10-03

### Fixed — upstream lane wt50 (2026-10-03)

- **yes**: a broken-pipe write death now reports exit status 141 (the
  SIGPIPE emulation other streaming tools use) instead of exit 1, so
  `yes | head -n 3` shows 141 on the producer side exactly like GNU
  coreutils (#1142). Non-pipe write errors keep the fatal exit-1 path.

### Fixed — upstream lane wt48 (2026-10-03)

- **cp**: every stat/open route now goes through the shared API-path
  boundary (`native_path::make_api_path_operand`, the mv/install
  invariant), so POSIX-drive-form operands (`/d/x`, `/cygdrive/d/x`) work
  in any combination with Windows-form operands instead of failing
  `cannot stat ... No such file or directory` at the source-existence,
  destination-directory, `-t`, same-file, symlink, and recursive-child
  gates (#1145, unixwin/niubash#124 Option B). `cp -s` stores the
  dialect-resolved source spelling as link text (a typed `/d/x` link text
  can never resolve on native Windows).
- **ls**: file operands and `-d` directory operands resolve POSIX drive
  forms — `std::filesystem::absolute` used to fold `/d/x` against the
  current drive root (`D:\d\x`) before the boundary could convert it
  (#1145). A bare root operand (`ls /`, `ls -d /`, `ls //`) lists the
  current drive root (the platform's `/` mapping, consistent with
  `cygpath -w /`): the `\\?\` spelling of a drive root is rejected by
  attribute probes, and the root/`\*` enumeration pattern is now built
  with a path-aware join (`GetFullPathNameW` mis-parses the concatenated
  `/\*` form).
- **native_path (shared boundary)**: `normalize_api_operand_w` folds the
  Cygwin drive prefix (`/cygdrive/d/x`, both separators) into the MSYS
  spelling before drive-letter conversion, giving every command that
  already routes through the boundary (cat, cp, install, mv, ...) cygdrive
  support uniformly; `to_extended_path` keeps bare drive roots un-prefixed
  and folds separators before `GetFullPathNameW` (#1145).

### Fixed — upstream lane wt41 (2026-10-02)

- **mv**: operands in MSYS drive form (`/d/x`) are routed through the
  shared API-path boundary like cp/install, so mixing Windows-form and
  POSIX-form operands in one invocation works in any combination (#1140,
  unixwin/niubash#124); GNU coreutils under MSYS accepts either dialect.
  The copy fallback also no longer mislabels regular files as directories,
  actually moves directories across volumes (they used to report success
  while doing nothing), refuses to move a directory into itself
  (`cannot copy a directory, X, into itself, Y`, GNU copy.c:2091), and
  honors a trailing separator on DEST (`failed to access 'DEST': Not a
  directory` instead of silently creating/overwriting the stripped name).
- **mktemp**: the printed name keeps the `-p`/`--tmpdir` operand's dialect
  (`-p /d/...` echoes `/d/.../x.XXXXXX`, `-p /cygdrive/d/...` likewise),
  so shell round-trips that capture the output stay self-consistent with
  POSIX-form variables (#1141); Windows-form operands and the TMPDIR/
  default-temp paths keep the native display form.
- **xargs**: option parsing stops at the utility name; every argument
  after it is passed to the utility verbatim (POSIX xargs operand
  semantics, GNU findutils) — `xargs cat -n` no longer errors on `-n`,
  `xargs wc -l` no longer silently consumes `-l` as `--max-lines`, and
  `xargs echo -- help` keeps the `--` (#1139).

## [1.1.4] - 2026-09-30

### Fixed — issue triage round (2026-09-29)

- **grep -r**: recursive walk rewritten (#1135). pnpm-style crossed NTFS
  junctions are not symlinks to std::filesystem, so
  `recursive_directory_iterator` descended them until the stack died with
  0xC0000409 and zero output — indistinguishable from "no matches". The
  hand-rolled walk skips reparse directories under -r, dedups canonical
  real paths under -R, and warns
  `grep: warning: <dir>: cyclic directory junction; skipped` on loops.
- **hexdump -b/-c**: rows are the GNU-fixed 71 columns — 16 slots of 4
  with only the final trailing space trimmed (#1133); short lines carried
  one extra space.
- **getconf ARG_MAX**: prints the Windows per-process limit (32767,
  CreateProcess command-line cap) instead of falling through to the
  unknown-variable error (#1134).
- **build**: /bigobj for the commands unity TU (176-file merge exceeded
  the default COFF section limit).

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
