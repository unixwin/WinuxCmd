# stdbuf — oracle audit (gap-fill round, #1488 / #1545 inventory)

Date: 2026-11-09. Oracle: GNU coreutils 9.4 stdbuf (WSL differential
gate; probe `build-vs/audit-results/stdbuf-probe.sh`). Oracle source
reference: coreutils src/stdbuf.c (optstring carries the leading `+`,
so options after COMMAND stay operands; MODE validated while parsing;
`_STDBUF_I/_STDBUF_O/_STDBUF_E` published for the child; xstrtol
suffix rule "no digits + valid suffix → 1").

## Verdict: clean

## Option/behavior matrix

| Face | GNU coreutils 9.4 (observed) | WinuxCmd (observed) | Verdict |
| --- | --- | --- | --- |
| env names for the child | `_STDBUF_I/O/E` published | same names (plus the internal `WINUX_STDBUF_*`) | clean |
| `-o 1M` / `-o 64K` published value | `1048576` / `65536` (resolved bytes) | same | clean |
| `-oL` published value | `L` | `L` | clean |
| `-o 0K` | `0` (any zero spelling unbuffered) | `0` (`00`/`0K` handled) | clean |
| `-iL` | `stdbuf: line buffering stdin is meaningless` + try-help, exit 125 | identical, exit 125 | clean |
| `-eL` | allowed, exit 0 | allowed | clean |
| bare suffix `-o K` | valid (xstrtol: no digits + valid suffix → 1×1024), exit 0 | valid (`parse_buffer_suffix`), exit 0 | clean |
| no mode option + COMMAND | `you must specify a buffering mode option` + try-help, exit 125 | identical, exit 125 | clean |
| missing COMMAND | `missing operand` + try-help, exit 125 | identical, exit 125 | clean |
| invalid mode `-o xyz` | `invalid mode 'xyz'`, exit 125, no try-help | same shape, exit 125 | clean |
| explicitly empty `-i ""` | `invalid mode ''`, exit 125 | same | clean |
| leading whitespace `-o " 0"` | valid, size 0 (stdbuf.c skips leading space) | `strip_leading_whitespace`, published `0` | clean |
| invalid option `-Z` | `invalid option -- 'Z'` + try-help, exit 125 | identical (stdbuf in the 125 group), exit 125 | clean |
| COMMAND not resolvable | `failed to run command 'x': No such file or directory`, exit 127 | identical wording/exit (ERROR_FILE_NOT_FOUND→127) | clean |
| child exit propagation | exit 7 propagated | propagated | clean |
| mode after operand `stdbuf true -o0` | mode-required error (optstring `+`: `-o0` stays an operand), exit 125 | mode-required, exit 125 | clean |

## Notes (accepted)

- GNU uses smart quotes in `invalid mode 'xyz'` / `failed to run
  command 'x'`; ours ASCII. The quotearg residual already recorded
  framework-level in #1462 W3a/W6; not re-fixed here.
- The Windows delivery mechanism differs by platform necessity
  (parent-side setvbuf + inherited env consumed by the child entry
  point) instead of GNU's LD_PRELOAD libstdbuf — mechanism face,
  contract (published env values, exit codes, diagnostics) identical.

## Existing coverage

- Differential corpus `corpus/stdbuf/` (5 cases, tagged audit-w3b) and
  `stdbuf_unit_test.cpp` pin the faces above; all green in this build.
