# WinuxCmd vs upstream (non-coreutils) — source-level audit, round 2

Date: 2026-09-21. Companion to `audit-coreutils-9.7.md`. This round covers the
non-coreutils commands against their authoritative upstream sources, fetched to
`tests/differential/.cache/upstream/`:

- findutils 4.10.0 (ftp.gnu.org) — find, xargs, locate, updatedb
- grep 3.12 (ftp.gnu.org) — grep/egrep/fgrep
- sed 4.9 (ftp.gnu.org) — sed
- patch 2.7.6 (ftp.gnu.org) — patch
- xxd, tree, file — documentation-based only (no source audit; see caveats)

Classification is the same as round 1: [BUG] / [PLATFORM-OK] / [COSMETIC] /
[EXT] (documented extension).

## Executive summary

- **xargs: matches** GNU xargs.c almost one-for-one (options, exit codes
  123/124/125/126/127, -I default {}, quote handling). Two small bugs
  (`-r` with whitespace-only input still runs; `-x` wrongly forced with `-d`).
- **grep: high fidelity, 6 real bugs** — `-L` exit status inverted, `-q`
  swallows error exit 2, `-Z` missing NUL in line prefixes, empty `-f`
  pattern file matches nothing, conflicting matchers (-E -F) not rejected,
  BOM-triggered decoding of binary files.
- **find: good, 7 bugs** — `-exec cmd \;` failing child must not set exit 1
  (only `{} +` does), `-execdir {}` must pass `./basename` not the full
  path, `-regextype` default must be findutils-default (EMACS-ish) not ERE,
  `-printf` lacks width/precision flags, `-daystart`/`-xdev` silently
  ignored.
- **sed: script engine matches on the core (addresses, N/D/q/Q/hold space,
  --posix gating), 8 bugs** — I/O errors must exit 2 not 1; missing
  `\L \U \u \l \E` case conversion in replacements; missing `m`/`M`/`e`
  s-flags; `y///` escapes unmapped; unknown `\x` in replacement drops the
  backslash; `r`/`R` failures silent (GNU: "can't read FILE");
  `sed -i` on stdin must be a hard error; `--debug`/`--follow-symlinks`
  hard-fail.
- **patch: FAIL — engine-level rework needed.** Multi-file patches corrupt
  files (single target overwritten); blank context lines inside hunks are
  dropped (very common); no per-hunk `Hunk #N succeeded at LINE (offset …)`
  reporting; no default `.rej` output and wrong reject format; fuzz/offset
  search and accumulation math wrong; `--backup-if-mismatch` default
  inverted; empty target file treated as missing; exit 2 never produced.
- **locate/updatedb: redesigned, not GNU-compatible** — `-l` means the
  opposite thing, no glob/regex matching modes, missing exit-1-on-no-match,
  updatedb option surface incompatible and pruning dead. Decide: either
  align or document as a distinct tool.
- **xxd/tree/file: documentation-based partial pass** — xxd missing
  `-b`/`-e`, `-g 0` wrong; tree missing half its documented options and
  humanizes `-s` sizes wrongly; file(1) is a 9-signature stub with several
  parsed-but-inert options. These need vendored upstream sources
  (vim/tree/file repos) before a source-level audit is possible.

## Recommended fix order (next round)

1. grep: the 5 exit-code/semantic one-liners (#1-#5) — cheap, high value.
2. sed: exit-code split (1 syntax vs 2 I/O), `\L\U\u\l\E`, y/// escapes,
   r/R diagnostics.
3. find: `-exec \;` exit semantics, `-execdir {}` argument form,
   `-regextype` default, `-daystart`/`-xdev`.
4. xargs: `-r` whitespace-only, `-x` with `-d`.
5. patch: full rework of parse/apply/report (multi-file, blank lines,
   default .rej, GNU messages/exit codes) — largest item, schedule
   separately.
6. Decide locate/updatedb positioning (GNU-compat vs documented redesign).

## Per-command detail

### find (vs findutils 4.10.0 find/*.c)

Matches: option surface incl. -newerXY/-samefile/-files0-from, full operator
parser, default -print suppression, -delete+-prune warning, -ok prompt,
exit 0/1.

- [BUG] `-exec cmd \;` failing child sets exit 1; GNU only `{} +` sets exit
  status (find/exec.c:388-392). (find.cpp:3635,3652)
- [BUG] `-exec {} +` predicate must return true even if the child fails
  (exec.c:397-399). (find.cpp:3636)
- [BUG] `-execdir {}` must substitute `./basename`; WinuxCmd passes the full
  path and chdirs instead (exec.c:114-129). (find.cpp:3629-3634)
  `-execdir +` must batch per directory. (find.cpp:3639-3655)
- [BUG] `-regextype` default is ERE; GNU default is findutils-default
  (RE_SYNTAX_EMACS: +?|() literal). (find.cpp:1777)
- [BUG] `-printf` lacks width/precision flags (`%8p`, `%-10s`, `%.5f`)
  (print.c). (find.cpp:3346-3513)
- [BUG] `-daystart` silently ignored (must shift -amin/-atime/-cmin/-ctime/
  -mmin/-mtime to start of day). (find.cpp:375,1668)
- [BUG-minor] `-xdev/-mount` silently ignored (must not descend other
  filesystems). (find.cpp:377-379)
- [BUG-minor] `-type` restricted to f,d,l,p; GNU accepts b,c,s,D.
  (find.cpp:1478-1481)
- [COSMETIC] `-prune` under -depth should warn.
- [PLATFORM-OK] junctions as `-type l`, fifo markers for `-p`.

### xargs (vs xargs/xargs.c)

Matches: full option table one-to-one, -I default {}, -i/-I vs -n/-L
warnings, POSIX exit codes incl. 124-on-255, quote error wording, default
`echo`.

- [BUG] `-r` still runs on whitespace-only non-empty input (gates on raw
  emptiness; GNU gates on zero args). (xargs.cpp:1327-1329,1377,1481)
- [BUG-minor] `-x` forced on with `-d`; GNU only forces with -I/-L
  (xargs.c:773). (xargs.cpp:1291-1294)
- [COSMETIC] `-p` prompt format.
- [PLATFORM-OK] -s cap 32767 (CreateProcess), -o→CONIN$, -P serial slots.

### locate (vs locate/locate.c)

- [BUG] `-l` bound to --ignore-case; GNU -l is --limit N. Extra non-GNU -n.
  (locate.cpp:50,52,96)
- [BUG] no-match must exit 1 (locate.c:1361). (locate.cpp:199)
- [BUG] always substring; GNU uses fnmatch globs when pattern contains
  `*?[]\`, plus -r regex mode. (locate.cpp:167-193)
- [BUG] missing -0 -A -e -E -p -S --regextype -L -P --max-database-age;
  -S/-r declared but unimplemented.
- [PLATFORM-OK] plain-line database instead of LOCATE02 frcode
  (self-consistent with our updatedb; document it).
- [COSMETIC] -w help says "whole filename"; GNU -w/--wholename means whole
  path and is unimplemented.

### updatedb (vs locate/updatedb.sh)

- [BUG] option surface incompatible (GNU is long-only: --localpaths
  --output --prunepaths --prunefs --netpaths …); -d/-e/-f/-g/-u etc. are
  invented.
- [BUG] -e/-x accepted but never read (no pruning).
- [BUG-minor] DB written in place, not temp+rename (crash corruption risk);
  success message printed where GNU is silent.
- [PLATFORM-OK] plain path-lines DB format; default walk root `.` vs `/`.

### grep (vs grep 3.12 src/grep.c)

Matches: exit scheme incl. -q+match-then-error, -l/-L printing rules,
-c/-m semantics, -o rules, multi -e/-f, context group separators, binary
message rules, -r/-R symlink rules, include/exclude sets, -h/-H last-wins,
--label, prefix ordering (filename:line:byte).

- [BUG] empty `-f` file matches nothing; GNU empty pattern list = empty
  pattern = match every line. (grep.cpp:1190-1228,1472-1549)
- [BUG] `-Z` does not put NUL after filename in line prefixes (hardcoded
  ':'). (grep.cpp:1555-1601)
- [BUG] `-L` exit inverted: must be 0 iff at least one line selected
  anywhere (so all-files-listed → 1). (grep.cpp:2533)
- [BUG] `-q` + read error must exit 2 even when a match short-circuits.
  (grep.c:667,3035 vs grep.cpp:2530)
- [BUG] `-E -F` combination must fail "conflicting matchers specified"
  exit 2. (grep.c:2108-2110 vs grep.cpp:968-983)
- [BUG] BOM detection transparently decodes UTF-16/UTF-8 binaries; GNU
  treats NUL bytes as binary. (grep.cpp:2045-2058,2429-2435)
- [COSMETIC] `-d read` directory wording; missing-pattern usage output.
- [PLATFORM-OK] CR stripping unless -U; -P via portable engine.

### sed (vs sed 4.9 sed/*.c)

Matches: option surface incl. --posix gating, -s not default, -i implies
separate files; s/// flags g p N i I w with occurrence checks; full address
grammar incl. `first~step`, `,~N`, `0,/re/`, `!`; all core commands;
N-at-EOF/D/n/q/Q/hold-space/t-T semantics; `#n` first line; exit 1 on
syntax error; `q N` propagation.

- [BUG] I/O errors (input open, w-file failures) exit 1; GNU exits 2
  (utils.h:22-25, execute.c:1710-1712). (sed.cpp:2340-2343,2398)
- [BUG] missing `\L \U \u \l \E` case conversion in s replacements;
  unknown `\x` must keep the backslash. (sed.cpp:1412-1454,
  sed.h:67-77, compile.c:560-565)
- [BUG] `m`/`M` s-flags missing; `e` flag unimplemented but should not
  surface as generic "unknown flag". (sed.cpp:343-347, compile.c:596-609)
- [BUG] `y///` escapes (\n \t \\ \delim) mapped as literal two chars.
  (sed.cpp:561-601, compile.c:1415-1446)
- [BUG] `r`/`R` on unreadable file silently ignored; GNU prints
  "sed: can't read FILE". (sed.cpp:1714-1731, execute.c:1510-1523)
- [BUG] `sed -i` with stdin must be a hard "no input files" error (GNU
  exit 4 panic); here a soft message + exit 1. (sed.cpp:2321-2325)
- [BUG] `--debug`/`--follow-symlinks` hard-fail; GNU supports both.
  (sed.cpp:1289-1296)
- [COSMETIC] w/W/s///w handle sharing; multibyte y///.
- [PLATFORM-OK] -i copy-then-rename loses hardlinks/ACLs (documented);
  file-arg globbing; -b binary no-op.

### patch (vs GNU patch 2.7.6 src/*.c) — FAIL, rework needed

- [CRITICAL] multi-file diffs: single `target_file` overwritten; every
  hunk applied to one file (patch.c loops per file). (patch.cpp:429,441-472)
- [CRITICAL] blank context lines inside hunks are dropped instead of
  counting as context for both sides — breaks any patch touching blank
  lines. (patch.cpp:478-479)
- [BUG] no `\ No newline at end of file` handling; a `\` line terminates
  the hunk and drops remaining lines. (patch.cpp:477-489)
- [BUG] no `Hunk #N succeeded at LINE (offset X lines).` / `with fuzz` /
  `Hunk #N FAILED at LINE.` messages; only a summary line.
  (patch.c:482,491-497 vs patch.cpp:557,626)
- [BUG] no default `file.rej`; reject format lacks headers/context;
  rejects written even under --dry-run. (patch.c:635,649-667 vs
  patch.cpp:588-599)
- [BUG] fuzz search trims symmetrically and sweeps ±N; GNU scans by
  increasing distance with per-side fuzz; offset accumulation double-counts
  and mutates hunk headers. (patch.cpp:218-257,547-552)
- [BUG] `-b` backup-if-mismatch default inverted (GNU: backup only when
  mismatch, unless POSIXLY_CORRECT). (patch.c:142-143 vs patch.cpp:581-583)
- [BUG] empty target file treated as "cannot open"; /dev/null headers
  unsupported. (patch.cpp:527-530)
- [BUG] exit 2 never produced (fatal/usage must be 2); forced application
  exits 0 despite failed hunks. (patch.cpp:630)
- [BUG-minor] `-N/--forward` doesn't skip already-applied hunks; `-E`
  default differs from GNU non-POSIX mode; missing longs --strip/--input;
  `@@` inside hunk body truncates the hunk; CRLF target converted to LF.
- [PLATFORM-OK] `\`/`/` separator handling in -p stripping.

### xxd / tree / file — documentation-based only

- xxd [BUG-doc]: missing `-b` bits dump and `-e` little-endian; `-g 0`
  must mean "no spaces" (here coerced to 2); `-s` lacks `%`/negative.
- tree [BUG-doc]: missing -h -p -u -i -F -r --dirsfirst --du -x; `-s`
  sizes humanized/truncated instead of raw bytes.
- file [BUG-doc]: 9-signature stub + extension table; MZ/PE not
  distinguished; `-z` `-k` `-n` `-N` `-L` parsed but inert; missing files
  abort instead of continue; extensionless binary reported as ASCII text.

These three need vendored sources (vim/tree/file upstream) before
source-level auditing; findings above are doc-based and lower confidence.

## Sources

- tests/differential/.cache/upstream/findutils-4.10.0, grep-3.12, sed-4.9,
  patch-2.7.6 (ftp.gnu.org tarballs, fetched by this audit round)
- xxd/tree/file: upstream documentation (vim 9.x, ice/tree, libmagic)
