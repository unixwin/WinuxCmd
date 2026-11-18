# WinuxCmd 2.0 — Agent Handover Brief (2026-11-18)

> Written for an agent on a **different machine**. Everything here is reproducible from
> `git clone https://github.com/unixwin/WinuxCmd.git` plus the remote refs named below.
> No local paths, worktrees, or unpublished state are required.

---

## 1. What this project is

WinuxCmd is a Windows implementation of GNU coreutils / util-linux. Version 1.x is C++23
(modules, `src/`). Version 2.0 rewrites each applet in Rust (`rust2/`) and then replaces the
C++ body with a thin dispatcher call into the Rust ABI. The end state is 176 applets, all
Rust-backed.

Work is organised as one PR per command or per small command group, each chasing **GNU parity**
(byte-exact stdout/stderr and exit status against a real GNU coreutils oracle).

---

## 2. Where things stand right now

### Remote facts (verify these first — they move)

```bash
git clone https://github.com/unixwin/WinuxCmd.git
cd WinuxCmd
git fetch origin --prune
git log --oneline -5 origin/main
gh pr list --state open --json number,title,mergeable,headRefName
```

- `origin/main` is the integration target. At handover time it was `72d4baf3`.
- An **aggregation PR #1832** (`integrate/parity-wave`) folds eight branches into one merge.
  It is the fastest way to see the current state of the parity work.
- Two PRs merged during this session: **#1819** (docs triage) and **#1829** (tty wiring).
- The i18n catalogue repository `unixwin/winuxcmd-i18n` was synced to **v0.6.53**
  (commit `8bd1169`): seven keys that no branch emits any more were dropped.
  `VERSION` and the release are already published.

### PR inventory

| PR | Branch | Content | Status at handover |
|---|---|---|---|
| #1832 | integrate/parity-wave | Aggregates the eight branches listed as "Contained" below | **Lands with known red ctest tests — see §3** |
| #1828 | fix/parity-shuf | shuf WP-I parity, Keccak RC[12] fix, reservoir sampling | Contained in #1832 |
| #1827 | fix/parity-cksum | cksum parity, untagged `-c`, `Try --help` after argmatch | Contained in #1832 |
| #1826 | fix/parity-head | head parity, `/dev/stdin` reads fd 0, ASCII quotes | Contained in #1832 |
| #1825 | fix/parity-tail | tail parity, `/dev/stdin` reads fd 0, ASCII quotes | Contained in #1832 |
| #1824 | fix/parity-sort | sort parity, `plain_err` diagnostics, full `--debug` | Contained in #1832 |
| #1823 | fix/parity-csplit-factor | csplit/factor parity, BigUint >u64, `-e` alias | Contained in #1832 |
| #1818 | def11 | 11 triaged library defects (chmod/chown/df/csplit/tty) | Contained in #1832 |
| #1817 | test/1x-repin-clean | GNU 9.4 re-pin of stale 1.x assertions | Contained in #1832 — **source of the red tests** |
| #1831 | fix/parity-date | `date --uct/--rfc-822` aliases + rust2 wiring | **NOT in #1832 — reverted, see §3f** |
| #1821 | wire-lsrm | Routes `ls` and `rm` through rust2 | **NOT in #1832 — reverted, see §3g** |
| #1819 | docs/cpp-only-triage | Triage of 94 C++-only applets into A/B/C/D | **MERGED** |
| #1829 | fix/parity-tty | tty write-error status 3 | **MERGED** |

Close the "Contained" PRs once #1832 lands — their commits are then on `main` and merging them
again would only conflict. **Do not close #1831 or #1821**: their content was deliberately
reverted out of the integration branch, and both need real fixes first (§3f, §3g).

Note: a PR whose branch is "contained in #1832" should be closed once #1832 lands; its commits
are then already on `main` and merging it separately would conflict.

---

## 3. Known failing tests (33 on CI) — **the actual remaining work**

`build-windows (x64, x64, x64, default)` runs the full ctest suite. It is currently **red**.
Every other check on #1832 passes: `oracle`, `baseline` (the GNU differential gate),
`baseline-growth`, `i18n-drift`, `rust-tests`, and both arm64 legs.

All 33 failures come from the **#1817 test re-pin**: that PR rewrote assertions to match real
GNU 9.4 behaviour, and in several cases the Rust/C++ implementation has not caught up. So each
failure is either "the test is now right and the code is behind" or "the assertion itself needs
one more correction". Decide per test; do not blanket-revert.

### 3a. chgrp — 9 failures

```
chgrp.chgrp_from_option_is_rejected_like_gnu
chgrp.chgrp_missing_reference_file_reports_gnu_style_error
chgrp.chgrp_nonexistent_group
chgrp.chgrp_preserve_root_all_separator_spellings_are_rejected
chgrp.chgrp_recursive_descendant_failure_reaches_exit_code
chgrp.chgrp_recursive_reference_drive_dialect_walks_operand_drive
chgrp.chgrp_reference_drive_dialect_cwd_on_other_drive_resolves_operand
chgrp.chgrp_reference_drive_dialect_resolves_operand_file
chgrp.chgrp_reference_verbose_reports_retained_group
```

### 3b. chown — 10 failures

```
chown.chown_dot_separator_owner_form_warns
chown.chown_dot_separator_owner_group_form_warns
chown.chown_double_separator_forms_fail_with_invalid_group
chown.chown_invalid_from_group_fails
chown.chown_invalid_from_user_fails
chown.chown_invalid_group_fails
chown.chown_invalid_user_fails
chown.chown_missing_reference_file_reports_gnu_style_error
chown.chown_numeric_owner_with_empty_group_fails_with_invalid_spec
chown.chown_preserve_root_all_separator_spellings_are_rejected
```

**The shared cause here is a single missing diagnostic suffix.** The implementation emits

```
chown: invalid group: ':nonexistent_group_xyz'
Try 'chown --help' for more information.
```

while the assertion expects only the first line. GNU 9.4 does print the `Try ...` line after
these usage-shaped errors, so the **implementation is right and the assertions are stale**.
Fixing this is mostly mechanical: add the `Try '<cmd> --help' for more information.` line to
the expected stderr of the nine `chgrp`/`chown` cases that fail this way. Verify each against a
real GNU oracle before editing — a couple of the `preserve_root` and `drive_dialect` cases may
have a different first line.

### 3c. chmod — 1 failure

```
chmod.chmod_wildcard_multiple_files
```

Implemented behaviour returns **exit 0**, the test expects **1**. GNU `chmod` exits non-zero in
this shape. This one looks like a **real implementation bug**, not a stale assertion — check what
the rust2 `chmod` does with a wildcard matching several files.

### 3d. xargs — 13 failures

```
xargs.xargs_command_template_args_stay_literal
xargs.xargs_command_template_char_class_stays_literal
xargs.xargs_command_template_parent_segment_wildcards_stay_literal
xargs.xargs_conflict_max_args_then_max_lines_last_wins
xargs.xargs_conflict_replace_then_max_args_last_wins
xargs.xargs_conflict_replace_then_max_lines_last_wins
xargs.xargs_default_echo_replacement_uses_one_input_line_per_command
xargs.xargs_incomplete_hex_delimiter_escape_is_nul
xargs.xargs_max_chars_above_windows_limit_is_clamped_with_warning
xargs.xargs_rejects_empty_long_delimiter
xargs.xargs_rejects_empty_short_delimiter
xargs.xargs_rejects_unknown_named_delimiter_escape
xargs.xargs_replace_still_implies_exit_if_exceeded
```

Two distinct causes are visible in the samples:

- **Wording drift.** The implementation emits
  `options --max-args and --max-lines/-L/-l are mutually exclusive`, the assertion expects
  `options --max-args and -L are mutually exclusive`. The longer spelling is the findutils 4.9
  one; check which GNU build the oracle in `tests/differential/runner.sh` actually pins, then
  align whichever side is wrong.
- **Exit status.** `xargs_incomplete_hex_delimiter_escape_is_nul` gets **1** where **0** is
  expected, and several `command_template_*` cases look like the same class. Run each case
  against the oracle before changing anything.

Note the `/dev/stdin` work from #1826/#1825 is *already* in; other applets wired earlier
(sed, grep, rev, tac, paste, od) still open `/dev/stdin` through the Windows console device and
will hang under WSL. No differential case covers them yet. Worth a separate issue.

### 3e. kill — 2 cases, only fail locally

```
kill.invalid_signal_name
kill.invalid_signal_number
```

These failed in a local `build-vs` run but **pass in CI**, so treat them as environment-specific
until reproduced on a clean runner. Not covered by any PR in this wave; the re-pin exposed them.

---

### 3f. `date` — reverted out of #1832, must be fixed before it can land

PR **#1831** (`fix/parity-date`) was merged into the integration branch and then **reverted**,
because it broke 18 differential cases that are green on `main`:

```
corpus/date/01-12hour-ampm.case        EXIT_DIFF  (subject rc=1, oracle rc=0)
corpus/date/02-12hour-nospace.case     EXIT_DIFF
corpus/date/06-slash-date.case         EXIT_DIFF
corpus/date/08-rel-month-sum.case      OUT_DIFF
corpus/date/10-rel-year-rollover.case  OUT_DIFF
corpus/date/11-military-j.case         EXIT_DIFF
corpus/date/12-military-J.case         EXIT_DIFF
corpus/date/15-word-order.case         EXIT_DIFF
corpus/date/16-word-order-ampm.case    EXIT_DIFF
corpus/date/20-ampm-midnight.case      EXIT_DIFF
corpus/date/21-ampm-noon.case          EXIT_DIFF
corpus/date/22-ampm-dotted.case        EXIT_DIFF
corpus/date/28-zone-colons.case        OUT_DIFF
corpus/date/30-format-conflict.case    EXIT_DIFF  (subject rc=0, oracle rc=1)
corpus/date/31-resolution-format.case  OUT_DIFF
corpus/date/32-iso-prefix.case         OUT_DIFF
corpus/date/33-iso-nanoseconds-rejected.case  EXIT_DIFF
corpus/date/34-reference-missing.case  OUT_DIFF
```

`main` passes all 34 `corpus/date/*` cases, so this is a **regression introduced by the wiring**,
not a pre-existing gap. The pattern — many 12-hour/AMPm/word-order/military-zone forms accepted
by the subject but rejected by GNU, plus two cases where the subject is too permissive — points at
the JSON/datetime parse subset accepting forms GNU `date` rejects. Start by diffing #1831's
`rust2/crates/winux-cmds/src/date.rs` parse path against `main`'s C++ `date`, then run
`rust2/gate/run-gate.sh date` before proposing it again.

Separately, `date.date_case_flags_match_gnu` fails **locally** with `%#a`/`%^a`/`%#Z` emitted
literally instead of cased. That did not reproduce in CI, so confirm on a clean runner before
treating it as a bug.

### 3g. `ls` — reverted out of #1832, one quoting regression

PR **#1821** (`wire-lsrm`) routes `ls` and `rm` through rust2. Merging it broke:

```
regressions/59-ls-quoting-locale-sort-abbrev.case   OUT_DIFF
subject: ‘a'b’  / oracle: 'a\'b'
```

The rust2 port renders `quote()` with typographic quotes (`‘’`) and does not escape the embedded
apostrophe, while GNU under `LC_ALL=C` (which `tests/differential/runner.sh` pins via
`ORACLE_LOCALE=C`) emits ASCII quotes with backslash escaping. #1826/#1825 fixed exactly this
class of bug in head/tail. Fix `ls`'s quotearg path the same way, run
`rust2/gate/run-gate.sh ls`, then re-propose. `rm` may be fine on its own — consider splitting
the PR so `rm` is not held hostage by `ls`.

Also note that `ls` matching a wildcard returns exit **2** with
`ls: cannot access '[a-b].txt': No such file or directory` — char-class globs are not expanded.
That is a separate gap from the quoting one.

---

## 4. How to work on this repo

### Build and test

```powershell
# Windows + Visual Studio 2026 toolchain, from the repo root
powershell -ExecutionPolicy Bypass -File scripts/build-with-vs.ps1

# tests (the binary lands in build-vs/tests/)
./build-vs/tests/winuxcmd-tests.exe --gtest_filter='chown.*'
cd build-vs && ctest -R 'chown|chgrp' --output-on-failure
```

Formatting: `scripts/format.py`. Do not run it repo-wide — the local clang-format disagrees
with the tree's committed style on ~171 files and CI does not gate on it.

### The differential gate

`rust2/gate/` holds the generator (`gen-cases.py`), the runner (`run-gate.sh`), the per-command
case tables (`cases/*.tsv`), and `COVERAGE.md`. `tests/differential/runner.sh` pins
`ORACLE_LOCALE=C`, which is why several diagnostics must be ASCII-quoted.

`rust2/REPLACEMENT-RECIPE.md` is the six-step recipe for converting one more applet.

### i18n

Any user-visible string needs a stable key plus an English fallback. After changing source text:

```bash
python scripts/i18n_batch.py extract      # regenerate the English catalogue
python scripts/i18n_batch.py validate     # check a locale catalogue
python scripts/check_i18n_drift.py --i18n-repo <clone-of-winuxcmd-i18n>
```

When the drift gate reports *extra* keys, the tempting move — delete them from the zh-CN
catalogue — is usually wrong: if `main` still emits that string, deleting it just makes `main`
red on the next run. Confirm the key is dead on every branch first. If it is live, record the
growth in the PR branch instead:

```bash
python scripts/check_i18n_drift.py --i18n-repo <clone> --update-baseline
```

The catalogue lives in the **separate** repo `unixwin/winuxcmd-i18n`. Every change under
`catalogs/` must bump that repo's `VERSION` in the same PR or CI blocks it.

---

## 5. Hard rules

1. **Never add a `link` command.** Its name shadows the MSVC linker `link.exe` and breaks the
   build. It was removed once (`10b2364`) and re-added by a bot PR (#1065); do not accept it.
2. **Agents do not merge their own PRs** unless the maintainer explicitly authorises it for that
   specific wave.
3. **Never run `--preserve-root` recursion or an unisolated full CTest against a real host
   filesystem.** A previous incident traversed a real drive. Exercise those paths only in an
   isolated or injected-IO harness.
4. Any user-visible string change needs the i18n key + fallback + catalogue sync in the same task.

---

## 6. Environment traps (each one cost real debugging time)

- **`niubash` rewrites POSIX argv.** The agent shell on the original machine rewrote command
  arguments; `MSYS_NO_PATHCONV` and `MSYS2_ARG_CONV_EXCL` did **not** help. Use PowerShell for
  verbatim product probes, a script *file* for WSL GNU oracles (`wsl -c` folds backslashes), and
  environment variables for path operands.
- **`/dev/stdin` on Windows.** Ports that resolve it through the pseudo-device layer land on
  `CONIN$` and block. Under WSL this shows up as a hang (rc 124) rather than an error, which no
  local niubash run reproduces. #1826/#1825 fixed head and tail; other applets are still exposed.
- **WSL installation step times out at ~19.5 min** on GitHub runners, which cancels the whole
  gate-shard job before it runs anything. Symptom: a shard "fails" with every later step skipped.
  Compare against `main`'s run for the same shard; if `main` is green it is infrastructure —
  `gh run rerun --failed`.
- **GitHub's `mergeable` field returns `UNKNOWN`** while it recomputes after a merge. Wait ~20 s
  and re-query instead of assuming.
- **All these PRs append to the same line** of `WINUXCMD_RUST2_COMMANDS` in `CMakeLists.txt`, so
  merging them one by one re-conflicts the next every time. Resolve by **set union** of the
  command names. The integration branch did exactly that.
- **Logs are huge.** Always `gh run view --job <id> --log > file` and then grep the file.

---

## 7. Suggested next actions, in order

1. Land **#1832** (or rebase and land its constituent PRs in sequence — the union-merge route is
   far cheaper). Then close the now-contained PRs: #1817, #1818, #1821, #1823, #1824, #1825,
   #1826, #1827, #1828, #1831.
2. Work through §3 until `build-windows (x64, x64, x64, default)` is green. §3b is the cheapest
   chunk; §3c is the only clear implementation bug.
3. Rebase and land **#1831** (`date`), which is the one parity branch not folded into #1832.
4. Fix the `date` format directives the local suite also flags (`%#a`, `%^a`, `%#Z` and friends —
   see `date.date_case_flags_match_gnu`). Those are outside the 35 CI failures but reproduce
   locally; they may be environment-specific, so confirm on a clean runner first.
5. Repo hygiene, once the tree is calm: the repository has accumulated a large number of merged
   branches and stale worktrees. `git branch --merged origin/main` and
   `git branch -r --merged origin/main` are the safe lists; anything with an open PR must be
   excluded.

---

## 8. Reference files

| Path | What it is |
|---|---|
| `DOCS/2.0-HANDOVER.md` | Roadmap, technical debt, probe methodology |
| `DOCS/2.0-inventory.tsv` | All 176 applets and their state |
| `DOCS/2.0-MIGRATION.md` | Migration notes |
| `rust2/README.md` | Shape contract, streaming v2 |
| `rust2/REPLACEMENT-RECIPE.md` | Six-step recipe per applet |
| `rust2/gate/gen-cases.py` | Differential case generator (single source of truth) |
| `rust2/gate/run-gate.sh` | Differential runner |
| `rust2/gate/COVERAGE.md` | Coverage board |
| `rust2/gate/PROBE-METHODOLOGY.md` | How to probe correctly on Windows |
| `scripts/check_i18n_drift.py` | i18n drift gate |
