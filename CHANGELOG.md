# Changelog

All notable changes to WinuxCmd are documented here. Format follows
[Keep a Changelog](https://keepachangelog.com/); versions follow the
repository release tags.

## [Unreleased]

### Fixed

- **wpm**: `wpm update winuxcmd` swaps the image by unlinking the old name
  and moving the staged binary into place instead of overwriting a
  running/mapped image in place. The in-place write fails with "being used by
  another process" whenever any process holds the file (observed on a live
  install, where the update silently kept the old version), and writing under
  a mapped image is unsafe for the processes already running from it. The
  in-place path remains the fallback when the unlink itself fails.
- **wpm**: installing a package reconciles its own previous install first -
  destinations the new artifact no longer maps are removed and, on a version
  change, files inside still-mapped directories that the new payload dropped
  are pruned. Before, a layout change left inert leftovers behind (the retired
  shim launcher `opt\<pkg>\<cmd>.exe` after a move to `layout: flat`, or a
  stale DLL in a PATH directory from older flat packaging). Only paths
  recorded in the package's own receipt are touched, they must resolve inside
  the root, and the winuxcmd image and the running executable are never
  deleted.
- **wpm**: `outdated` orders versions instead of comparing strings, so a stale
  mirror advertising an older version no longer reports a
  `update available: 2.55.0.5 -> 2.55.0.4` downgrade. The order is the
  dotted-numeric core plus, when present, a Debian-style packaging revision
  (`5.4.1-unixwin2` > `5.4.1-unixwin1` > `5.4.1`) - what a repackaged artifact
  needs, since the upstream version stays and our rebuild gets a revision.
  Vendor spellings (`9.5.0.0p1`, `r42`, ...) keep the conservative "any
  difference is an update" rule.

## [1.1.10] - 2026-10-16

### Added

- **native_path**: normalized API operands now resolve the shell tree — the bare
  `/` and every path below the configured install root, `/tmp`, `/var/tmp`,
  `/home` and the `/mnt/X` WSL drive form — and a shell-namespaced operand's
  canonical result is re-expressed in the POSIX face, so `readlink -f` and
  `realpath` agree with `pwd`. The executable-leaf open order (an existing
  as-spelled path wins, else the `X.exe` sibling) keeps `test -f /usr/bin/seq`
  and `head -1 /usr/bin/seq` consistent while `ls -d` keeps the operand
  spelling. The layer is inert unless a shell root is configured, so standalone
  WinuxCmd keeps its native dialect (unixwin/niubash#346).

## [1.1.9] - 2026-10-16

### Fixed

- **wpm**: installing a payload file over an existing `usr\bin` command name
  now breaks the old WinuxCmd multicall hardlink with POSIX-semantics unlink
  instead of a plain `DeleteFileW`. Every `usr\bin` entry is a link to the
  same on-disk image as the running `wpm.exe`/`winuxcmd.exe`, so the plain
  delete self-locked and failed with `Access denied` ("failed to break
  WinuxCmd hardlink"). Only `layout: flat` packages, whose payload maps the
  real binary onto `usr\bin\<cmd>.exe`, ever reach this path — first hit by
  `wpm install niugit --force` (unixwin/niu-git#8). The same POSIX unlink is
  now used by the stale-link cleanup and alias-replacement paths, which had
  the identical hazard.

## [1.1.8] - 2026-10-15

### Changed

- **wpm**: shim payload resolution checks shallow per-package locations
  (`<pkg>/<cmd>.exe`, plus `bin/`, `libexec/`, `lib/`, `sbin/`, `usr/bin/` at
  depth 1-3, shallowest wins) before falling back to the bounded recursive
  scan, so command-differs-from-package shims (`git` -> `niugit`, `cargo` ->
  `rust`, `clang` -> `llvm`, `nu` -> `nushell`, `nvim` -> `neovim`) no longer
  pay a whole-`opt/` tree walk on every invocation (~15k files). `time git`
  through the shim 212 ms -> 77 ms; a prompt-style git hook batch
  847 ms -> 291 ms; direct-hit shims (`python`, `curl`, `node`) are unchanged
  (unixwin/niu-git#8).

### Fixed

- **ls**: `/` (also spelled `//`, `///`) resolves to the shell root named by
  `__RUBASH_SHELL_ROOT`/`NIU_ROOT`/`WINUXSH_ROOT`/`RUBASH_ROOT` instead of
  enumerating the drive root, independently of the current directory;
  recursive listings keep the logical `/...` headers while lookups and entry
  metadata go through the shell root (unixwin/niubash#346).

## [1.1.7] - 

### Added

- **diff3**: full GNU diffutils 3.10 alignment on the shared diff engine —
  three-way block merge (`make_3way_diff`), default report with GNU's
  odd-one-out section rules, ed scripts (`-A`/`-e`/`-E`/`-3`/`-x`/`-X` with
  `-i` final write), merged output (`-m`, `-L` labels, `-T` initial tab,
  `--strip-trailing-cr`, `--diff-program`), `-` stdin operands and GNU exit
  statuses (0 ok / 1 conflicts / 2 trouble) with byte-verified usage
  diagnostics (issue #1127, P1).
- **sdiff**: rebuilt on `diff --side-by-side` output — GNU's gutter formula,
  `|`/`<`/`>`/`(`/`)` row markers and `/`/`\` separators for rows missing a
  trailing newline; `-l`/`-s`, `-w`/`--tabsize`/`-t`, the ignore-option
  family (`-i`/`-E`/`-Z`/`-b`/`-W`/`-B`/`-I`/`-a`/`--strip-trailing-cr`,
  `-d`/`-H`/`--diff-program`) forwarded into matching only, and `-o` merged
  output that auto-merges non-interactively (overlaps keep the left column)
  per issue #1127, P1.

### Changed

- **winuxcmd**: the bare `winuxcmd` listing groups commands into GNU
  coreutils-style sections (File operations, Text processing, Shell
  utilities, ...) with alphabetical entries and a discovery guide
  (`winuxcmd <command> --help`, `winuxcmd help <command>`,
  `winuxcmd --list-all`, `wpm search`); `winuxcmd --list-all` preserves the
  flat alphabetical listing. When the system locale is non-English and its
  i18n catalog is not installed, one hint line points at the matching WPM
  package (`wpm install winuxcmd-i18n-zh-cn` + `WINUX_LANG`) (#1155).
- **seq**: output is assembled into 64 KiB buffer chunks instead of one
  `WriteFile` per line, and integral first/increment sequences (default
  fixed format, precision 0, no `--format`/`-w`, |operands| <= 2^52) emit
  through a `std::to_chars` fast path like GNU seq.c's integer bypass.
  100k lines, release build: `seq 1 100000` 332 ms -> ~45 ms and
  `seq 1 100000 | wc -l` 903 ms -> ~45 ms (GNU/WSL oracle 7-10 ms; the
  residual is Windows process spawn). The byte stream is unchanged — a
  regression test pins all 588,895 bytes across several flush rounds
  (#1173).
- **diff**: side-by-side output now shares the GNU diffutils `side.c`
  printer with sdiff (identical rows, `(`/`)` remainders, `/`/`\`
  separators, common rows for equal inputs); `-E` expands tabs across the
  whole line for matching, `-B`/`-I` drop ignored hunks and realign their
  lines positionally instead of filtering buffers, and a final line without
  a newline only compares equal to the other side's incomplete last line
  (issue #1127, P1).
- **docs**: the GNU coreutils compatibility records the one deliberate
  Windows deviation around literal-glob operands — NTFS/Win32 forbid `*` in
  file names, so `touch 'x*x'` reports the NTFS error where GNU creates the
  literal file, `touch -c 'x*x'` silently skips like GNU (fixed in #1152),
  and the derived `ls 'x*x'` miss is a consequence of GNU's own creation
  succeeding; the touch test exemption is registered in
  `DOCS/gnu-coreutils/touch.md` and `COMPATIBILITY_MATRIX.md` (#1143, #1167).

### Fixed

- **spawn**: every child-process launch (the xargs/`find -exec`/pipeline
  spawn family) resolves through a shared GNU `execvp`-style resolver —
  bare names search PATH only (no cwd or implicit System32 shadowing, so
  `sort`/`find`/`tree`... pick the GNU-compatible binary the caller put on
  PATH first) and POSIX PATH spellings (`/c/...`, `/cygdrive/c/...`,
  `:`-separated entries) are split and converted before resolution
  (owner-reported, #1169).
- **find**: POSIX drive-form start points (`/c/...`, `/d/...`,
  `/cygdrive/c/...`) now resolve through the same operand boundary as every
  other tool's path operands, matching msys find semantics where the tool's
  own POSIX layer handles the start point. Command-line roots, `-files0-from`
  entries, and the `-files0-from` source operand all go through it (#1170).
- **tests**: the grep junction recursion cases degrade to a SKIP on hosts
  without symlink-creation privilege (error 1314) instead of failing, mirroring
  the `*_or_skip` helper every other unit test file uses (#1168). New tput
  GNU-differential tests pin that every tput path — success, unknown
  capability, missing operand — leaves piped stdin untouched, the contract
  GNU tput is known for (#1166).
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
- **du**: an operand's trailing separator run collapses to a single slash —
  `du src/` prints `src/` and `src/bin` (never `src//bin`), `du src//` prints
  byte-identically to `du src/`, and `du src` keeps the plain form, matching
  GNU coreutils (#1159).
- **du, sed**: POSIX drive-form operands (`/d/...`, `/cygdrive/d/...`)
  resolve through the shared operand boundary — du used to reject them
  with ENOENT while accepting `D:/...`, and sed could not read them as
  input files, `-i` targets, `-f` script files or the `r`/`w`/`W`
  script-embedded paths. Both now route operands through the same
  boundary grep/wc/head/find already use (the L2 self-parse contract,
  uniformly applied); error messages and the `=`/`F` filename display
  keep the operand text as typed (#1174).
- **wpm**: candidate index sources are the built-in registry plus
  `user_sources` only — a `sources` array declared inside the cached index
  file is no longer merged, so one poisoned fetch can no longer persistently
  inject new download locations; `wpm index update` now records the winning
  source (`last_success_source`) and the SHA-256 of the persisted index
  (`last_index_sha256`) and warns when the index content changes while being
  served from a non-preferred source; the `wpm index update --json` payload
  carries `index_sha256` (#1176).
- **wpm**: the loopback proxy fallback (probing `127.0.0.1:7897/7890/7891/
  7892/7893/10808/10809` after direct failures) is off by default; any
  unprivileged local process could otherwise listen there and serve both
  index and artifacts past the index's own sha256 checks. Opt in via
  `allow_loopback_fallback: true` in `.wpm/config.json`, or configure a
  trusted proxy with `--proxy`/environment variables (#1177).

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
