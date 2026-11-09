# pinky — oracle audit (gap-fill round, #1488 / #1545 inventory)

Date: 2026-11-09. Oracle: GNU coreutils 9.4 pinky (WSL differential
gate; source `build-vs/audit-results/coreutils-pinky-9.4.c`, fetched
from coreutils v9.4). Live probes in
`build-vs/audit-results/pinky-probe.sh`, `pinky-idx.sh`,
`pinky-locale.sh`.

## Verdict: notes — 3 divergences FIXED in this round, W4 platform rulings stand

## Divergences found and FIXED here

1. **`pinky -l` without a username lacked the try-help hint**: GNU
   prints the diagnostic plus `Try 'pinky --help' for more
   information.`, exit 1 (observed). We printed only the first line.
   FIXED (exit code already matched).
2. **`--lookup` was accepted**: coreutils 9.4 pinky declares NO long
   options besides --help/--version (src/pinky.c longopts, fetched;
   observed `pinky --lookup` → `pinky: unrecognized option '--lookup'`
   exit 1). We accepted it silently with an invented description.
   FIXED: option removed from the table; the dispatcher now produces
   the GNU wording, exit 1.
3. **Day zero-padding in the When column**: GNU's short-format time is
   `"%b %e %H:%M"` (src/pinky.c:434; space-padded day, `Nov  9`), we
   formatted `%b %d` (`Nov 09`). FIXED (UCRT strftime %e verified:
   `[Nov  9 09:18] n=12`). The 12-column When field was already
   correct — heading bytes byte-identical to GNU under LC_ALL=C for
   plain/-q/-w/-i (positions Name=10 TTY=31 Idle=40 When=47, lengths
   64/31/44 matched exactly).

## Verified-matching faces (clean)

- Short-format headings byte-identical under LC_ALL=C (see above).
  Note: GNU widens the When field to 16 under a hard LC_TIME locale
  (src/pinky.c:429-430) — the WSL default env LANG=C.UTF-8 selects the
  16-wide form; ours always uses the C-locale 12-wide form. Under
  LC_ALL=C (the comparison convention of this repo's gates) both are
  12 and identical. Hard-locale face recorded, not implemented.
- `-q` heading `Login     TTY      When        ` byte-identical; `-q`
  omits fullname/idle/where exactly as the flags define.
- `-l` unknown user: `Login name: <u>...In real life:  ???` exit 0.
- Unknown user in short mode: no rows, exit 0.
- Invalid option faces: `pinky: invalid option -- 'Z'` + try-help,
  exit 1 — byte-identical.
- `-s` is a no-op (short format is the default), `-h`/`-p` accepted
  no-ops.

## Platform faces — documented [DIFFERS], stand as-is (W4 coordinating ruling 2026-11-01)

- mesg char `?` (no tty-writability bit on Windows; GNU prints `+`/`-`
  from the tty stat) — ruling: "printing `?` where a line cannot be
  resolved states the platform truth".
- Line name `console` (no utmp line), Where column empty, idle
  `?????` when the session table cannot answer.
- Long format answers Directory/Shell from the Windows profile model
  (`C:/Users/<user>`, `/usr/bin/bash`) instead of /etc/passwd GECOS —
  simulation layer (niu #758).
- Rows come from the WTS session table instead of utmp (niu #758).
