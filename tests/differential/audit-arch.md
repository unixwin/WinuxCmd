# arch — oracle audit (gap-fill round, #1488 / #1545 inventory)

Date: 2026-11-09. Oracle: GNU coreutils 9.4 (`arch (GNU coreutils) 9.4`,
WSL GNU side of the differential gate). The oracle value is pinned by
coreutils src/uname.c `arch` mode (print machine hardware name); live
evidence from the WSL gate in this worktree
(`build-vs/audit-results/arch-probe.sh`).

## Verdict: clean

## Option/behavior matrix

| Face | GNU coreutils 9.4 (observed) | WinuxCmd (observed) | Verdict |
| --- | --- | --- | --- |
| no operands | `x86_64`, exit 0 | `x86_64`, exit 0 | clean |
| value = `uname -m` | `x86_64` == `uname -m` | `x86_64` == `uname -m` (unit pin `arch_matches_uname_m`) | clean |
| extra operand | `arch: extra operand 'foo'` + try-help, exit 1 | same shape, exit 1 (WinuxCmd #1215 pin) | clean |
| `-` operand | extra operand error, exit 1 | same | clean |
| invalid short `-Z` | `arch: invalid option -- 'Z'` + try-help, exit 1 | byte-identical first line, exit 1 | clean |
| invalid long `--bogus` | `arch: unrecognized option '--bogus'` + try-help, exit 1 | identical shape, exit 1 | clean |
| `--help` | exit 0, coreutils usage block | exit 0, repo help block | notes |
| `--version` | exit 0, coreutils banner | exit 0, repo banner | notes |

## Notes (accepted, repo-wide conventions)

- GNU prints smart quotes (U+2018/U+2019) in diagnostics; WinuxCmd uses
  ASCII quotes. This is the quotearg residual already recorded as a
  framework-level 待裁 item in the #1462 W3a/W6 rounds.
- `--help`/`--version` answer the repo's dispatcher-intercepted blocks
  (same exit codes; wording follows the repo convention, per the W7
  round's tzset ruling "dispatcher-intercepted, no bug").

## Platform faces

- The machine value is mapped from `GetNativeSystemInfo` (not the WOW64
  `GetSystemInfo`), so `arch` and `uname -m` agree for a 32-bit process;
  oracle behavior is "report the native machine" (coreutils `uname -m`).
  amd64→`x86_64`, ARM64→`aarch64` match GNU spelling; i386/arm/ia64 are
  legacy mappings GNU cannot observe on this host — spelling-only risk,
  recorded, no case pinned.
