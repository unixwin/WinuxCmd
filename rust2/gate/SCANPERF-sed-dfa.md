# sed `-n /re/p` scan lane — lazy line DFA filter (research, #1488)

Status: PROTOTYPE on `rust2/sed-scanperf` (based on `origin/rust2/sed-impl`
`09df368d`). Not committed; no command/help/version text changes, so no I18N
catalog impact.

## Why

On the 64 MiB `gate/sed-bench.py` corpus the only case still behind GNU sed
4.9 was `-n /lorem.*amet/p` (0.72 s vs 0.30 s). GNU splits selection from
matching (`dfa.c` picks the line, `regexec` only runs for spans/captures).
`winux-regex` had the same split available from the grep lane
(`origin/rust2/grep-impl` `91cee800`, `dfa.rs`) but sed's address path called
the interpreter (`Regex::is_match_with`) for every line.

## Hot-path analysis (manual counting)

Instrumented counters (temporarily patched into `pike.rs`, reverted) over the
64 MiB corpus, pattern `lorem.*amet`, 1 622 665 lines / 41.4 B average:

| counter | total | per line |
| --- | --- | --- |
| `nfa_search` calls (one per line, address miss and hit alike) | 1 622 665 | 1.0 |
| outer-loop iterations (positions walked; pre-filter jumps via memchr) | 15 436 493 | 9.5 (0.23/byte) |
| seed attempts that passed the literal pre-filter (`lorem` candidates) | 648 496 | 0.40 |

So the cost is not "attempt the interpreter at every byte": the prefix
pre-filter already skips. It is **one engine invocation plus scratch setup per
line** and **648 k seed attempts, 543 k of them futile, each running the
thread set to end of line** (only 104 759 lines actually match). Measured
selection cost alone (`sed-line-probe` example, NFA scratch reused, output
excluded): 0.60–0.67 s (~100–107 MB/s, 369–416 ns/line). The same loop through the
line DFA: 0.135–0.163 s (392–474 MB/s, 84–100 ns/line) with identical hit
counts.

## What the prototype does

* `winux-regex/src/dfa.rs` — the grep-lane lazy line DFA, adapted to the sed
  AST: `AnchorStart`/`BufferStart` are line start, `AnchorEnd`/`BufferEnd` are
  line end, `DotRestricted { exclude_newline: false }` is `Dot`, and the
  multiline anchors `LineStart(_)`/`LineEnd(_)` (sed `M`) decline the DFA.
* `Regex::has_nfa()` — new accessor. Patterns without the Thompson NFA
  (back-references, >20 k interval expansion) are answered by the budgeted
  backtracker, whose exhaustion is a *conservative* no-match; the DFA is
  exact, so building it there would change verdicts. sed builds the filter
  only when `has_nfa()`.
* `sed/compile.rs` — `SedRegex::matches_line`: the DFA answers the boolean for
  an address when it is exact on the input (`InputRequirement` Any/ValidUtf8/
  Ascii) and the pattern space contains no record delimiter (`N`-joined text,
  `-z` NUL); otherwise, and for every capture/span query, the interpreter
  runs unchanged. `dfa` is built only for address regexes (`line_filter`), so
  `s///` never pays for one.
* `sed/exec.rs` — address `Regex` matching calls `matches_line`.

## Results (best of 3, wall clock, output to file)

Machine: Intel Xeon w5-3433, 16C/32T, 63.7 GiB, Windows 10.0.19045,
rustc 1.99.0; GNU sed 4.9 in WSL2 (kernel 6.18.33.2), input on ext4.
`python gate/sed-scanperf.py before|after` (identical corpus/cases, GNU side
measured in the same run):

| case | before | after | GNU (range over runs) |
| --- | --- | --- | --- |
| `s/foo/bar/g` | 0.35 | 0.35 | 0.42–0.52 |
| `-n /re/p` (`lorem.*amet`) | **0.73** | **0.29** | 0.30–0.44 |
| `y/abc/xyz/` | 0.19 | 0.20 | 1.77–3.54 |
| `s/\(a\)\(b\)/\2\1/` | 0.52 | 0.54 | 0.66–0.79 |
| `-n p` (I/O floor) | 0.18 | 0.18 | 0.16–0.18 |

`gate/sed-bench.py` (the lane's official table) agrees: `-n /re/p` 0.72 → 0.29–0.32
against GNU 0.30–0.32; every other case unchanged within noise. GNU numbers
drift 0.30–0.44 run to run on this shared box, so parity — not "faster than
GNU" — is the defensible claim.

## Correctness

* gate `bash gate/run-gate.sh sed`: **GNU-gated 378 pass / 0 fail** (the
  table has 379 rows; the one win-only row `sd056-version` fails only because
  this worktree has no C++ `build-vs/usr/bin/winuxcmd.exe`, `c_rc=127` — a
  pre-existing environment gap, not a sed difference).
* `cargo test -p winux-cmds --lib sed::` 18 pass (one new test covering dot /
  anchor / word / backref / `-i` / `-z` / `N`-joined / `M` address shapes).
* `crates/winux-regex/tests/dfa_sed_equiv.rs` (new): differential DFA vs
  interpreter on 18 hand-picked sed shapes + 250 generated BRE patterns over
  311 lines (empty, UTF-8, ill-formed bytes, embedded `\n`), plus the `-z`
  NUL record model and the decline cases (`M` anchors, backrefs, oversized
  interval); it asserts the DFA actually engaged (≥50 patterns) so the suite
  cannot go vacuous.
* End-to-end A/B: the `09df368d` binary vs the prototype binary produced
  byte-identical stdout for 44 address patterns and 11 shape cases (`-i`,
  ranges, `-z`, `N`, `s///`) on a 4000-line mixed ASCII/UTF-8 corpus.

## Recommendation

Fold this into the sed work **before** (or independently of) the `sed` wire
commit: it touches only the address-selection path, is inert for every
`sed` case that is not an address, and the gate row set is already green.
Two review points for the wire owner:

1. `dfa.rs` is shared with the grep lane — land it from one place (this
   branch's version extends the AST arms for the sed dialect and adds
   `state_count()`), not twice.
2. Keep the `has_nfa()` + record-delimiter + `InputRequirement` gate exactly
   as written; each clause exists for a measured or budget-defined
   divergence, and `tests/dfa_sed_equiv.rs` pins them.

Follow-up not done here: buffer-at-a-time selection (`LineDfa::find_line`
over big blocks) would remove the remaining per-line `read_until`/scratch
cost (the 0.18 s I/O floor), but it needs a sed main-loop restructure and
belongs to the wire lane, not the scan lane.
