# GNU differential regression tests

This directory contains independent cases for comparing WinuxCmd with a GNU
coreutils oracle. Set `WINUXCMD_BIN` and `GNU_BIN` when they are not on PATH.

The runner is a gate, not a smoke test. It fails for an empty corpus, missing
oracle, unexplained skip, exit-code difference, or output difference. Use
`--results FILE` to retain machine-readable TSV output for baseline comparison.

Each case supports `cmd`, `args`, `stdin`, `setup`, `timeout`, `source`,
`tags`, `oracle`, and `subject`. `stdin` and `setup` use a following block
indented by two spaces.

`oracle` names the GNU oracle runtime that a case requires (`linux` or `msys`).
Some GNU-visible behavior is produced by the POSIX runtime rather than by
coreutils itself: when stdin is closed, an MSYS2 oracle dies inside its read()
shim with `failed to set file descriptor text/binary mode: Bad file descriptor`
— identically for `cat`, `wc`, `od`, `head` and `tee` — and never reaches
coreutils' own error path. Bumping the coreutils version does not help, because
8.32 and 9.4 are byte-identical there. Cases carrying a mismatching `oracle`
are reported as `SKIP`, not compared, so `KNOWN_DIFF` keeps meaning "the case
ran and the two sides differed". Omit `oracle` when a case is comparable
against any oracle.

`subject` names the subject launch shape that a case requires (`native` or
`interop`). Some behavior depends on what the launch delivers to the subject
rather than on either program: a genuinely closed stdin exists when a Windows
shell creates the subject directly, but a subject reached through the WSL
interop chain — the shape the baseline is recorded in — receives a substitute
handle instead, so a case pinning coreutils' closed-stdin diagnostics would
measure the launch chain instead of the product. Cases carrying a mismatching
`subject` are reported as `SKIP`, not compared, for the same reason as
`oracle`. Omit `subject` when a case is comparable under any launch shape.

The whitelist of known differential gaps is split by oracle platform.
`whitelist.txt` holds entries that hold against any oracle; the runner
additionally loads `whitelist-linux.txt` (the WSL-oracle shape) or
`whitelist-msys.txt` (an MSYS2/Git Bash oracle) for the detected oracle
platform, and a platform entry overrides a common entry for the same case.
An excuse recorded against one oracle's runtime — newlib printf rounding,
the WSL CI image's ncal, 9p-share behavior — must not excuse the case under
the other oracle, where it either passes outright or surfaces a real
difference. Because `baseline.json` is recorded in the WSL shape, an entry
may move into a platform file only when the recorded shape does not rely on
it (every case recorded non-PASS keeps its excuse under Linux).

Outputs that differ only in genuinely run-varying text are compared through
the normalization layer in `normalize.sed` (dd's timing tail, timestamp
times-of-day) before a case counts as different; the file documents per rule
what is masked and what is deliberately not masked. Locale sensitivity is
handled by pinning, not masking: the runner runs both sides under
`ORACLE_LOCALE` (`C` by default), so a verdict never depends on the host
locale.
