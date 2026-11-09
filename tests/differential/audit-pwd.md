# pwd — oracle audit (gap-fill round, #1488 / #1545 inventory)

Date: 2026-11-09. Oracle: GNU coreutils 9.4 `/bin/pwd` (WSL
differential gate; probe `build-vs/audit-results/pwd-probe.sh`).
Oracle source reference: coreutils src/pwd.c (POSIXLY_CORRECT
presence test; xgetcwd physical fallback; PWD logical validation).

## Verdict: clean

## Option/behavior matrix

| Face | GNU coreutils 9.4 (observed) | WinuxCmd (observed) | Verdict |
| --- | --- | --- | --- |
| plain / `-P` / `-L` | current dir, exit 0 | same | clean |
| `-L -P` / `-P -L` | last occurrence wins (observed same path here; GNU pwd.c applies flags sequentially) | occurrences loop is last-wins | clean |
| extra operand | warning + path, exit 0 | `pwd: ignoring non-option arguments` + path, exit 0 | clean |
| invalid `-Z` | `<argv0>: invalid option -- 'Z'` + try-help, exit 1 | same shape, exit 1 | clean |
| symlinked cwd, `-L` with matching `PWD` env | logical path through the link | honors a Windows-form `PWD` that names the link (`...\pwd-link` observed) | clean |
| symlinked cwd, `-P` | resolved target | resolved target (junction canonicalized) | clean |
| POSIXLY_CORRECT present-but-empty | selects logical default | presence test (`pwd.cpp`, cites pwd.c:331) | clean |
| bogus `PWD` (nonexistent) | falls back physical, exit 0 | same | clean |
| `PWD` with `.`/`..` components | falls back physical | `has_dot_components` rejects | clean |
| `PWD` valid path but different dir | falls back physical | `equivalent()` check rejects | clean |

## Notes (accepted)

- The diagnostic prefix is the fixed applet name (`pwd`) where GNU
  echoes argv[0] (`/bin/pwd`). Repo-wide convention (fixed program
  name in diagnostics); wording otherwise identical.
- `-L` validates the `PWD` environment value with Win32 path forms: a
  POSIX/msys-style `PWD` (`/d/repo/...`) is rejected as non-absolute
  in the Win32 sense and falls back to physical. This is the repo's
  path-contract boundary (docs on path forms at boundaries); GNU never
  sees such values, so no oracle case is pin-able — recorded only.
- Junction/symlink resolution uses `std::filesystem::canonical` with a
  raw-path fallback; equivalent to xgetcwd's contract for reachable
  directories.

## Unit pins already present

- `pwd_unit_test.cpp` covers `-L`/`-P` selection, POSIXLY_CORRECT
  presence, the extra-operand warning, and unknown-option rejection.
