# WinuxCmd vs procps-ng 4.0.4 / misc upstreams — audit round 2b

Date: 2026-09-21. Companion to `audit-non-coreutils.md`. Upstream source:
procps-ng 4.0.4 (`tests/differential/.cache/upstream/procps-v4.0.4/`).
Commands without procps coverage (time, strings, envsubst, cpio, d2u/u2d,
killall, man) are doc-based against GNU time / binutils / gettext / cpio /
dos2unix / psmisc documented behavior.

## FAIL-level findings

### pgrep (pgrep.cpp)
- [BUG] name match not anchored: `regex_search` anywhere in the name
  (pgrep.cpp:49-55); upstream anchors `^(pattern)$` (src/pgrep.c:601,1051).
  `pgrep explor` must not match explorer.
- [BUG] missing most selection options: -d -g -G -n -o -O -P -s -t -u -U -v
  -w -r -q -F -L -A -H --ns --nslist (src/pgrep.c:858-889); we ship only
  -f -i -l -a -x -c (pgrep.cpp:31-43).
- [COSMETIC] usage error wording; regex grammar ECMAScript vs POSIX ERE
  [PLATFORM-OK].

### pkill (pkill.cpp)
- [BUG] same unanchored match (pkill.cpp:56-62).
- [BUG] generic `-<signal>`/`--signal` not accepted (only fixed spellings,
  pkill.cpp:34-43); missing -P -s -t -u -U -g -G -n -o -v -w -x.
- Exit codes match (1 no match / 2 usage).

### pidof (pidof.cpp)
- [BUG] `-x` implemented as "exact match" (pidof.cpp:34); upstream -x means
  "also match shells running the named scripts" (src/pidof.c:69-88).
- [BUG] default match is substring over the whole command line; upstream
  matches the program basename exactly.
- [BUG] missing -s -o -q -S (src/pidof.c:310).

### free (free.cpp)
- [BUG] default unit MiB (free.cpp:100); upstream default KiB.
- [BUG] header/columns: must be `total used free shared buff/cache available`
  (wide adds `buffers cache` — src/free.c:395-397); we print
  `total used free available` and a wrong wide variant with hardcoded zeros
  (free.cpp:246,262-266).
- [BUG] `-g` prints %.1f; upstream integer GiB (free.cpp:153).
- [BUG] `--si` folded into 1024-based human (free.cpp:126-128); upstream
  --si is 1000-based.
- [BUG] `-v/--committed` accepted, no effect.
- [PLATFORM-OK] buffers/cache zeros, swap from pagefile, -l semantics.

### vmstat (vmstat.cpp)
- [BUG] report-mode header/fields wrong: must be
  `procs ---memory--- ---swap-- ---io---- -system-- -cpu----` with row
  `r b swpd free buff cache si so bi bo in cs us sy id wa st`
  (src/vmstat.c:253-309); we print a 13-field custom layout
  (vmstat.cpp:181-199).
- [BUG] no positional `delay [count]` mode.
- [BUG] `-d` documented as memory stats (upstream: disk stats, unimplemented);
  `-p` documented as per-PID (upstream: partition stats, unimplemented).
- [BUG] `-f` output format wrong (`<N> forks` upstream); `-a -m -w -t`
  parsed but inert.

### w (w.cpp)
- [BUG] header clock is the literal string `" 14:23:45 up "` (w.cpp:203).
- [BUG] `-h/--heading` inverted: we print a header; upstream -h SUPPRESSES it
  (src/w.c -h, --no-header). (w.cpp:101,112)
- [BUG] missing load average in header; column header differs from
  `USER TTY FROM LOGIN@ IDLE JCPU PCPU WHAT` (src/w.c:799-803).
- [BUG] invented flags -b -d -u -t -r -w -a -c; -i declared but unread
  (w.cpp:114-121). Upstream surface is only -h -u -s -f -o -i -V.

### watch (watch.cpp)
- [BUG] `-c` collides: upstream -c is --chgexit (watch.cpp:36-38); missing
  -e -g -p -x -w -q.
- [BUG] header format `Every 2s: cmd HH:MM:SS`; upstream
  `Every 2.0s: cmd  hostname: time` padded to width (watch.cpp:222-229).
- [BUG] always returns child exit code; upstream returns 0 unless
  -e/-g/-q (watch.cpp:277).

### time (time.cpp, doc-based GNU time)
- [BUG] default format `real 0.123s`; upstream `real\t0m0.123s`
  (time.cpp:186-192).
- [BUG] `-p` uses 3 decimals; POSIX `%2.2f` → 2.
- [BUG] missing -v -f FORMAT -o FILE -a.

### cpio (cpio.cpp, doc-based GNU cpio)
- [BUG] `-H/--format` ignored — always writes newc `070701` magic
  (cpio.cpp:204).
- [BUG] `-ov` writes the file list to stdout, corrupting piped archives;
  upstream prints it to stderr (cpio.cpp:290-292).
- [BUG] `-p` unimplemented; `-d` creates one level only; no pattern args,
  -F, -B, --no-absolute-filenames.

## PARTIAL

- ps: matches on the deterministic surface; [BUG] `-u` is a format flag here
  but upstream `-u NAME` selects users (ps.cpp:51-77).
- top: platform-adapted, acceptable; `-d` integer-only (upstream fractional).
- killall: substring default match; psmisc killall is exact-name by default
  (doc-based); missing -i -l -r -s -u -w -y -t.
- envsubst: `${VAR:-default}` / `${VAR:+alt}` unsupported — parser stops at
  `:` (envsubst.cpp:108-117,141-149); SHELL-FORMAT `-` (stdin) unsupported.
- d2u/u2d/dos2unix/unix2dos: conversion core correct; missing -k -n old new
  -o -c -f -q; in-place write not atomic (truncate in place).
- strings: matches binutils on the main surface; `-e S` accepts invalid
  UTF-8 (strings.cpp:312-319); exit-status help text wrong (binutils
  returns 0 regardless).

## PASS

nice, renice, kill (spot), hostname, less (spot), ps (minus -u), top,
strings (main surface).

## Top 10 fixes by impact

1. vmstat report mode: upstream header + field order + `delay [count]`
   (vmstat.cpp:181-199).
2. free: default KiB, `shared buff/cache` columns, `--si` 1000-based
   (free.cpp:100,246).
3. w: real clock, -h suppresses header, load-average line, correct columns,
   drop invented flags (w.cpp:101-227).
4. pgrep/pkill anchored name matching (pgrep.cpp:49, pkill.cpp:56).
5. time default/`-p` formats (time.cpp:186-192).
6. cpio: -ov list to stderr, honor or reject -H, hard-fail -p (cpio.cpp).
7. pidof: exact basename default, -x redefine, -s/-o (pidof.cpp).
8. envsubst `${VAR:-}`/`${VAR:+}` (envsubst.cpp).
9. watch: -c collision + upstream header format (watch.cpp).
10. d2u/u2d: `-k` keep-date default behavior decision + `-n old new`
    (d2u.cpp).

Note on bloat: these are fixes to EXISTING commands, no new commands.
