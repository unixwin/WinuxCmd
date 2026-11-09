# who — oracle audit (gap-fill round, #1488 / #1545 inventory)

Date: 2026-11-09. Oracle: GNU coreutils 9.4 who (WSL differential
gate; source `build-vs/audit-results/coreutils-who-9.4.c` fetched from
coreutils v9.4; probes in `build-vs/audit-results/who-probe.sh`,
`who-headers.sh`). This audit re-opens the who faces first visited by
the #1462 W4 round, whose report lived outside the repo; where the W4
coordinating ruling (2026-11-01) fixed platform residuals, those
standings are cited, not re-litigated.

## Verdict: notes — 3 divergences FIXED in this round, W4 platform rulings stand

## Divergences found and FIXED here

1. **Missing FILE operand was an error**: we returned
   `who: <file>: No such file or directory`, exit 1 (pinned by an old
   unit test "the GNU error"). The live oracle shows the opposite:
   coreutils who.c has no FILE existence validation — utmpxname()/
   getutent() fail lazily, so `who /no/such/utmp` prints NOTHING and
   exits 0 (observed on 9.4; who.c scan_entries prints the -H heading
   before the record loop, so `-H FILE` shows the header only).
   FIXED: unreadable FILE → zero records, silent, exit 0; `-H` still
   prints the heading. Old pin flipped.
2. **Doubled diagnostic prefix**: the unsupported-option refusal went
   through report_error(), which prepends `who: `, while the i18n
   default string embedded its own `who: ` → stderr read
   `who: who: option is not supported on Windows: --boot`. FIXED: the
   prefix now appears once (`who: option is not supported on Windows:
   --boot`).
3. **Time column used the hard-locale face**: GNU who.c:803-811 selects
   `"%b %e %H:%M"` with time_format_width 12 in the C/POSIX branch and
   only the hard-locale branch uses `"%Y-%m-%d %H:%M"` width 16. We
   always printed the 16-column ISO face, so the header and data were
   4 columns wider than GNU under LC_ALL=C. FIXED: `%b %e %H:%M` +
   width 12. Header faces now byte-identical to GNU under LC_ALL=C:
   `-H` → `NAME     LINE         TIME         COMMENT`, `-H -T` →
   `NAME       LINE ...`, `-H -u` → `... TIME         IDLE
   PID COMMENT` (observed equal on both sides).

## Verified-matching faces (clean)

- `-q`: space-joined names line + `# users=N` (GNU's empty-utmp output
  is an empty first line + `# users=0`; ours renders the same shape
  when the session table is empty).
- `-q` suppresses `-H` (GNU: short_list branch prints before any
  heading).
- Three operands → `who: extra operand 'c'` + try-help, exit 1.
- Invalid option → `who: invalid option -- 'Z'` + try-help, exit 1.
- "am i" two-operand form selects the caller's line.
- mesg column rendering `%-8s` + `" x"` + ` %-12s` line (data row
  `cmx      + Console` matches GNU print_line's field order).

## Platform faces — documented [DIFFERS], stand as-is (W4 coordinating ruling 2026-11-01)

- mesg state always `+` (no tty-writability bit; ruling 1: "printing
  `+` ... states the platform truth; inventing a mesg state would be
  worse").
- FILE operand accepted-but-unparsed: rows come from the live WTS
  session table, not utmp records (ruling 2; ownership niubash #758).
- `-b/-d/-l/-p/-r/-t/-a/--lookup/--writable` refused as "not supported
  on Windows" (utmp record classes do not exist; the GNU faces answer
  from boot time / runlevel / init records Windows does not carry).
- Hard-locale time face (width 16 + ISO format) not implemented — ours
  always answers the C/POSIX branch, which is the shape this repo's
  gates compare under (LC_ALL=C).
