# WinuxCmd vs util-linux 2.41 — audit round 2a

Date: 2026-09-21. Companion to `audit-non-coreutils.md` and
`audit-procps-misc.md`. Upstream source:
`tests/differential/.cache/upstream/util-linux-2.41/`. Note: `watch` is NOT
in util-linux; its authority is procps-ng (see round 2b, findings consistent).

## DEVIATES

### col (col.cpp vs text-utils/col.c)
- [BUG] default space→tab compression missing entirely: upstream defaults
  compress_spaces=1 (col.c:648,292-301); `-x` disables it. We never emit
  tabs and treat -x as "expand tabs" — a different semantic (col.cpp:52,128).
- [BUG] half-line model absent: upstream counts half-lines (NL=+2, col.c:427),
  reverse feed \v (col.c:446), ESC-7/8/9 sequences (col.c:410-426); our \v
  only moves under -f and is inverted (col.cpp:138-143).
- [BUG] `-l` parsed but unused; help claims "default 128", upstream default
  is 32 lines (col.c:89,650).
- [BUG] missing long options and -h/--tabs, -V/-H (col.c:528-538).
- [BUG] SO/SI charset switching unhandled (col.c:436-441).
- [PLATFORM-OK] ASCII-only placement approximation.

### namei (namei.cpp vs misc-utils/namei.c)
- [BUG] invented options -a/--access, -d/--noreport; missing upstream
  -x -m -o -l -n -v (namei.c:365-396 vs namei.cpp:44-48).
- [BUG] `-m` declared, never read (namei.cpp:47,60-69).
- [BUG] output format differs: upstream `f: <path>` header + indent/type-char
  lines (namei.c:284-345); we print blank line + bare path (namei.cpp:103-166).
- [BUG] always exit 0; upstream EXIT_FAILURE on stat/missing/loop
  (namei.c:465-482 vs namei.cpp:169).
- [BUG] dead branch: mountpoint `D` marking unreachable (namei.cpp:150-153).

### whereis (whereis.cpp vs misc-utils/whereis.c)
- [BUG] -B/-M/-S consume ONE directory; upstream consumes argv until -f
  (whereis.c:592-614). `whereis -B dir1 dir2 -f ls` misparses dir2 as a name
  (whereis.cpp:129-148).
- [BUG] missing -f must error (whereis.c:663-664); unchecked.
- [BUG] -g is glob-mode upstream, not a no-op (whereis.c:219); -h prints usage
  upstream (whereis.c:657).
- [BUG] -l output format: `bin:/man:/src:` lines upstream (whereis.c:503-523);
  we print custom blocks (whereis.cpp:279-296).
- [BUG-minor] -u want-mask is positional per-name upstream (whereis.c:556-583);
  we apply one global mask.

### logger (logger.cpp vs misc-utils/logger.c)
- [BUG] -f accepted but never reads the file; -e -n -P -T -d --prio-prefix
  --no-act -S --rfc3164/--rfc5424 all silent no-ops (logger.cpp:36-69 vs
  logger.c:1217-1232).
- [BUG] stdin must log each line as its own message (logger.c:1010); we
  concatenate (logger.cpp:104-108,133).
- [BUG] default tag must be the login name (logger.c:962-964); we hardcode
  "logger" (logger.cpp:75).
- [BUG] write failures ignored; upstream exits nonzero (logger.cpp:139-141).
- [BUG-minor] -i/--id no-op (should embed PID).
- [PLATFORM-OK] Windows Event Log sink.

### cal (cal.cpp vs misc-utils/cal.c)
- [BUG] only -m of ~17 options; missing -1 -3 -s -j -n -S -y -Y -w -v -c
  --color --reform --iso (cal.c:308-449).
- [BUG] year layout gutter 3 → must be 2 (week_width 20, 64-col layout;
  cal.c:297,455-465 vs cal.cpp:20-22).
- [BUG] argument forms: month names, timestamps, `day month year` unsupported
  (cal.c:467-499).
- Matches: Sunday default/-m Monday, leap years, centered headers.

### column (column.cpp vs text-utils/column.c)
- [BUG] default mode does not columnate at all — just cats input
  (column.cpp:556-559). This is the tool's core function.
- [BUG] option misbindings: -x bound to invented --output-fields (upstream
  --fillrows, column.cpp:72-74); -e bound to invented --table-empty (upstream
  --table-header-repeat, :95-96); -E bound to invented --table-noescape
  (upstream --table-noextreme, :104-108).
- [BUG] --output-width reads --columns' value (column.cpp:218) — dead option.
- [BUG] -J JSON mode consumes first data row as header keys (column.cpp:471-492);
  upstream does not.
- [BUG] truncates cells by default; upstream requires -T/--table-truncate
  (column.cpp:522-526).

### getopt (getopt.cpp vs misc-utils/getopt.c)
- [BUG] missing -h/-V (getopt.c:376-390).
- [BUG] -T checked before option parsing; upstream processes sequentially so
  `getopt -s bogus -T` dies on the bad shell first, exit 2 (getopt.c:445-449
  vs getopt.cpp:508-511).
- Matches: exit codes 0/1/2/4, empty long opt, POSIXLY_CORRECT, quoting,
  trailing ` --`, GETOPT_COMPATIBLE.

### watch (watch.cpp vs procps src/watch.c — consistent with round 2b)
- [BUG] first frame highlighted as "all new" (watch.cpp:225-241); upstream
  first frame is the reference.
- [BUG] returns child exit code; upstream 0 unless -e/-g/-q (watch.cpp:263).
- [BUG] invented -c/--count collides with upstream --color; missing
  -e -g -q -p -r -w -x -v -c/-C (watch.c:96-113).
- [BUG] -n integer-only; upstream strtod + WATCH_INTERVAL env (watch.c:861).

## MATCHES

- **rev**: option set, record model, codepoint-wise reversal, exit codes
  (UL/rev.c:130-203). Minor: operand glob expansion is a repo-wide convention
  that can alter literal filenames; missing -V/-h.
- **more**: option set, +NUM/+/PAT, non-tty passthrough, exit codes
  (UL/text-utils/more.c:269-279). [COSMETIC] -d prompt wording; "(END)" is an
  extension.

## Top 10 fixes by impact

1. column default columnation (column.cpp:556-559) — core function missing.
2. column option rebindings -x/-e/-E + --output-width fix.
3. namei: upstream option set, output format, exit codes.
4. col: default space→tab compression + half-line/ESC model.
5. cal: -y/-3/-j + 64-col year gutter + argument forms.
6. logger: implement -f, per-line stdin, real default tag, honest exit.
7. watch: first-frame fix, exit code, option completion.
8. whereis: multi-dir -B/-M/-S, missing -f error, -l format.
9. getopt: -h/-V + -T precedence.
10. rev: nothing required.

Fixes to EXISTING commands only — no bloat.
