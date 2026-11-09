# locale — oracle audit (gap-fill round, #1488 / #1545 inventory)

Date: 2026-11-09. Oracle: glibc 2.39 `locale` (Ubuntu GLIBC
2.39-0ubuntu8.9, WSL side of the differential gate; probes preserved in
`build-vs/audit-results/locale-*.sh`). Oracle source reference:
glibc `locale/programs/locale.c` / `locale_special.c` (line-level cites
below are from the live 2.39 binary's observed behavior; the upstream
raw fetch failed in this environment, so source line numbers are not
claimed).

## Verdict: notes — 4 divergences FIXED in this round, platform model faces documented

## Divergences found and FIXED here

1. **Bare `-k` / bare `-c` printed invented output; `-k` also carried a
   broken string-literal "concat"** (`locale.cpp` printed the literal
   `LC_CTYPE= + get_system_locale() + `). GNU writes nothing unless NAME
   operands are given: glibc main() only dumps the environment in the
   no-argument branch; `-c`/`-k` feed the per-NAME loop, so a bare
   `-c`/`-k` is silent, exit 0 (observed both). FIXED: bare `-c`/`-k`
   now answer nothing, exit 0; the broken block is deleted.
2. **Unknown NAME face**: GNU diagnoses `locale: unknown name "X"` exit
   1 (glibc locale_special.c, observed for `codeset`, `month`,
   `default_language`, `nosuchkw123`, and the empty name). We printed
   `locale: unknown keyword 'X'`, and `-k` had NO validation at all
   (`locale -k nosuchkw123` printed a fabricated binding, exit 0).
   FIXED: whitelist admission (the #117-compliant shape) over the
   implemented glibc item names; anything else is `unknown name "X"`,
   exit 1, in both plain and -k modes.
3. **Invented names removed, real name added**: glibc 2.39 rejects
   `codeset` / `collate` / `ctype` / `monetary` / `numeric` / `time` /
   `default_language` / `month`; the LC_TIME full-month item is `mon`
   (glibc categories.def). FIXED: those names now fall to the unknown
   -name error; `mon` answers the ;-joined full-month list (same value
   the old `month` key carried).
4. **`-k` value rendering**: GNU print_item() renders string items as
   `name="value"` (quoted, even empty strings), numeric monetary flags
   unquoted (`frac_digits=-1`), and the era stringlist bare (`era=`)
   (observed: `charmap="ANSI_X3.4-1968"`, `decimal_point="."`,
   `day="Sunday;..."`, `p_cs_precedes=-1`, `era=`). We printed
   `name=value` unquoted everywhere. FIXED: per-type rendering.

## Verified-matching faces (clean)

- `locale charmap` follows the environment the POSIX precedence way:
  LC_ALL=C → `ANSI_X3.4-1968`, C.UTF-8 → `UTF-8` (unit pins exist).
- `decimal_point` `.` / `thousands_sep` empty / `era` empty /
  `positive_sign`/`negative_sign` empty / `yesexpr` `^[yY]` /
  `noexpr` `^[nN]` / `abday` `Sun;...;Sat` / `abmon` `Jan;...;Dec` /
  `day` full list — all byte-equal to the glibc C-locale answers.
- `--usage` exit 0; exit codes for the unknown-name path (1).
- Multiple NAME operands are answered in argv order (GNU loops
  argv; ours now does too).

## Platform faces — documented [DIFFERS], stand as-is

- **Value model of monetary/yesstr keywords**: GNU's C locale answers
  empty strings and -1 flags (`currency_symbol`, `int_curr_symbol`,
  `mon_decimal_point`, `mon_thousands_sep`, `yesstr`, `nostr` are
  empty; `frac_digits`, `int_frac_digits`, `p_cs_precedes` are -1).
  Our simulation answers an en-US-shaped table (`USD `, `$`, `,`, `1`).
  This is the simulation-layer decision (Windows carries no locale
  archive — niu #758 owns the locale database face). Correcting the
  table is a value-model rework, filed separately, not a mechanical
  fix.
- **`-a` / `-m` inventories**: GNU enumerates the locale archive (5
  entries here) and the charmap directory (236 entries); ours answers
  a fixed representative list. Same simulation-layer dependency.
- **Category NAMEs (`locale LC_CTYPE`, `-c LC_CTYPE`)**: GNU dumps the
  compiled category; ours cannot without a locale database (the named
  `-c` form answers the category-name list as an approximation).
- **Plain output**: GNU prints `LANG=` then each quoted category value
  selected by LC_ALL→LANG→POSIX precedence (plus the Ubuntu-added
  `LANGUAGE=` line); ours prints the Windows user locale for every
  field and no `LANGUAGE=` line. Environment-following values are
  computable only where the simulation defines them (charmap/codeset
  already do).

## Follow-up filed

- Value-model + category-dump rework (locale database face) →
  tracked in the WinuxCmd issue tracker as a simulation-layer item
  (niu #758 dependency), not fixable mechanically in this round.
