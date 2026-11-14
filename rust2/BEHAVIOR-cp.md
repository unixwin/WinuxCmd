# cp lane — behavior record (WP-J, #1488 rust2)

Oracle: WSL `cp (GNU coreutils) 9.4` — Ubuntu 9.4-3ubuntu6.2. Every value in
`gate/cases/cp.tsv` was fixed from probe scripts
(`rust2/target/probe/p-cp1..4.sh`, lane-local) before the implementation.
GNU source read: coreutils-9.4 tarball, `src/cp.c` / `src/copy.c`.

## GNU owner map (module heads cite the same lines)

| area | owner |
|---|---|
| option table / longopts | cp.c:115-142 |
| sparse / reflink / update value tables | cp.c:84-112 |
| usage text | cp.c:151 |
| `--parents` re-protect | cp.c:302 |
| `make_dir_parents_private` | cp.c:399 |
| `do_copy` (operand grammar, `-t`/`-T`, target validation, dup-source prep) | cp.c:608 |
| `cp_option_init` | cp.c:823 |
| `decode_preserve_arg` (argmatch) | cp.c:876 |
| `main` (getopt switch, last-wins state, defaults) | cp.c:968 |
| `-n` clears `-u` / deref default / backup type | cp.c:1217-1248 |
| `copy_internal` decision ladder | copy.c:2197 |
| `copy_reg` (regular-file copy) | copy.c:1235 |
| `copy_dir` (recursion, ancestors) | copy.c:822 |
| `same_file_ok` | copy.c:1739 |
| `overwrite_ok` (`-i` prompt) | copy.c:1988 |
| `dest_info_init` / `src_info_init` | copy.c:2018 / 2033 |
| `emit_verbose` | copy.c:2082 |
| `create_hard_link` | copy.c:2122 |
| `should_dereference` (`-H` = command-line only) | copy.c:2151 |
| `is_ancestor` (cyclic links) | copy.c:688 |
| `sparse_copy` / `clone_file` / `lseek_copy` | copy.c:318 / 492 / 550 |
| just-created destination refusal | copy.c:2478 |
| dir↔non-dir overwrite refusals | copy.c:2455-2500 |

## Windows value-model rulings (find/winfs precedent)

- **identity** = `(volume serial, file index)` from one handle open; GNU's
  `(st_dev, st_ino)` for same-file, cycle detection and `--preserve=links`.
- **link** = name-surrogate reparse point: `SYMLINK`, `MOUNT_POINT`
  (junction) and the WSL/LX tag shape. Junctions are first-class links
  (1.x #1064), recreated as junctions through `FSCTL_SET_REPARSE_POINT`
  (the unprivileged directory-link form); file symlinks need Developer Mode
  and fail with GNU's `Operation not permitted`.
- **mode** = shared `modemap` bits (0644/0755/0777, exe extensions) carried
  by the read-only attribute; `--preserve=mode` copies write bits only,
  never HIDDEN/SYSTEM.
- **timestamps** = FILETIME ticks (creation/access/write) end to end.
- **devices** = `winux_core::path` DOS table; `NUL`/`/dev/null` open as
  streams; `cp a NUL` is a silent success like GNU.
- **case** = enumeration and identity are case-insensitive (NTFS), matching
  is preserved as typed (display/verbose bytes), the into-itself key is
  ASCII-folded like cp.cpp:1722.
- **`/mnt/X` folding** happens at the operand boundary (`fsutil::resolve` →
  `winux_core::path::normalize_operand`), never in display strings.
- **`--parents` drive rule**: `C:\a\b` grafts as `DEST/a/b` (drop the drive
  designator, like GNU drops the leading `/`); UNC becomes
  `server/share/...`.

## Gate

`rust2/gate/cases/cp.tsv`: **98 rows** — 95 `strict` + 3 `note`.

- Covers the audit issues relevant to cp:
  - **#1647** symlink sources: rows cp81-cp87 (junction rows; GNU side
    `ln -s`, exe sides `cmd /c mklink /J`), cp88/cp89 `note` SKIP for file
    symlinks (Developer Mode bound); cp98 covers a link cycle under `-L`.
  - **#1648** `-i` refusal rc: cp26 (no), cp27 (EOF), cp30/cp31 ordering.
  - **#1649** dir-over-non-dir: cp72 (`cannot overwrite non-directory`).
  - **#1650** empty operand is a path: cp61/cp62.
  - **#1651** `-i`/`-f`/`-n` last-wins: cp30-cp33.
  - #1643-#1646 are `ls`; #1652-#1657 are `stat/du/df`; #1649's `mv` body
    is not shared with cp (cp uses its own copy path) — only the cp face
    above applies.
- SKIP rows: cp88, cp89 (file-symlink fixtures need Developer Mode),
  cp90 (`cp -s` file symlink creation). Junction rows gate the same code
  paths on this host.

Result: `bash gate/run-gate.sh cp` → **98 pass / 0 fail** (95 strict gated
against GNU stdout+rc, 3 record-only notes).

## Triple gate (§4)

1. **GNU oracle**: 98/98 as above.
2. **ctest failure set**: `ctest -R "^cp\."` → **62/62 passed, 0 failed**.
   The product path is unchanged by this lane (no wiring), so the baseline
   is identical by construction.
3. **Old-exe byte comparison**: the gate ledger's `r=c` column compares the
   Rust driver against the 1.x C++ exe on every row. **30 of 98 rows
   differ** (68 identical). Classification:

| rows | C++ 1.x behavior | verdict |
|---|---|---|
| cp05, cp23, cp24, cp25, cp65, cp77, cp78, cp80, cp84 | verbose paths use `\` (`'a' -> 'd\a'`), and recursive copies omit the directory's own line (`cp24` shows only `'s\f' -> 't\f'`) | **1.x bug, fixed** — GNU prints `/` and announces the directory first |
| cp26, cp27, cp30, cp31 | `-i` decline/EOF exits **0** | **#1648 fixed** — GNU and the port exit 1 |
| cp36, cp37 | `-uv` parsed as `--update=v` → rc 1 | **1.x option-parser bug, fixed** (optional-argument short options) |
| cp39 | `-n` not undone by `--update=all` | **fixed** (GNU cp.c:1136-1157) |
| cp42 | backup annotation `(backup: 'b~')` missing | **fixed** (emit_verbose copy.c:2082) |
| cp45 | `--backup=bogus` accepted (rc 0, no backup) | **fixed** — argmatch error rc 1 |
| cp53 | `--preserve=` accepted (rc 0) | **fixed** — ambiguous-argument rc 1 |
| cp69 | `--strip-trailing-slashes a/ b` succeeded (rc 0) | **fixed** — GNU stats the spelling as typed (cp.c:655 strips only in the `-t` form) |
| cp93 | `--sparse=n` (unique-prefix value) rejected | **fixed** — GNU argmatch accepts unique prefixes |
| cp97 | `-t d -t e` accepted (last wins) | **fixed** — `multiple target directories specified` rc 1 (cp.c:1120) |
| cp98 | cyclic junction under `-L`: reports the cycle but exits **0** | **fixed** — `(dev,ino)` ancestor check and rc 1 (copy.c:688) |
| cp75 | `--attributes-only -v` prints `(attributes only)` suffix | **fixed** — GNU prints the plain line |
| cp76 | second source silently overwrites the just-created destination (rc 0, data loss) | **fixed** — `will not overwrite just-created` rc 1 (copy.c:2478) |
| cp81, cp82, cp85, cp86, cp87 | junction sources → `cannot create symbolic link ... A required privilege is not held` rc 1 | **#1647 fixed** — junctions are recreated as junctions, unprivileged |
| cp88-cp90 (note) | rc 1 (privilege) | same platform bound, recorded only |

## Performance (lane-local, Debug vs Debug, median of 5)

| workload | Rust driver | 1.x C++ exe | ratio |
|---|---|---|---|
| 64 MiB single file | 59.5 ms | 140.2 ms | 0.42× |
| 300 × 8 KiB recursive tree | 256.0 ms | 544.1 ms | 0.47× |

(Machine: this worktree host, warm cache, `build-vs` Debug for both. The
Rust side uses a 256 KiB copy buffer; no perf gate is claimed here — WP-O
owns the formal baseline.)

## Intentionally retired 1.x behavior (wiring checklist)

- `--update=none-fail` (1.x accepted it; 9.4 oracle rejects it with the
  `all/none/older` argmatch list, rc 1). Retires with the C++ body.
- The `-Z`/`--context` silent no-op is kept (accepted); the 1.x
  `[DIFFERS]` comment moves into the Rust module head.
- `--debug` is verbose-only (GNU also prints copy-offload/sparse debug
  lines to stdout; not gated, noted as a gap).
- `--preserve=ownership/context/xattr` remain accepted list arithmetic with
  no Windows effect (1.x parity; a real `SetNamedSecurityInfoW` port is out
  of scope and would need privilege tests).

## Wiring checklist (next lane, WP-I recipe step 3-5)

1. `src/commands/cp.cpp`: keep the `REGISTER_COMMAND` metadata byte-identical,
   `import rust2_bridge;`, replace the handler body with
   `return winux::rust2::run_command("cp", ctx.raw_args);`, delete the 1.x
   helpers/includes.
2. `CMakeLists.txt`: append `cp` to `WINUXCMD_RUST2_COMMANDS` (union line, no
   old `set` residue).
3. ctest re-pin: the 62 `cp.*` tests currently pin 1.x behavior. The ones
   that assert now-corrected behavior must be re-pinned to the GNU contract
   with WSL probe evidence: `cp_interactive_1324_*` (rc on decline, #1648),
   `issue1369_reflink_always_failure_leaves_no_destination` (still valid),
   `cp_suffix_value_that_looks_like_abbreviation_is_not_expanded` (still
   valid), `issue1435_*` (still valid), `issue1434_stream_copy_*` (the Rust
   copy loop reports short reads), `issue1366_parents_drive_letter_absolute_source`
   (the Rust graft drops the drive designator — decide whether the test's
   expected path changes).
4. Re-run the gate with the wired exe as the `rust` side and the pre-wiring
   exe as `CPP_BIN` (§4 gate a+c), plus default/full edition builds.

## Remaining gaps

1. `--parents` does not re-protect the intermediate directories from the
   source (GNU `re_protect`, cp.c:302): created dirs get default modes/times
   even under `-a`. Tree content and rc are correct; attribute fidelity is
   the gap.
2. `--reflink=always` is clone-or-fail with ENOTSUP on non-ReFS volumes; no
   `FSCTL_DUPLICATE_EXTENTS_TO_FILE` attempt (same rc/stdout as the ext4
   oracle, not the same optimization on ReFS).
3. `--sparse=always` skips all-zero 64 KiB chunks but does not punch holes
   inside a chunk, and `--sparse=auto` treats every source as non-sparse.
4. `-x` compares volume serials; mount-point traversal across volumes is
   not exercised by the gate.
5. File symlink creation/recreation still needs Developer Mode; cp88-cp90
   stay `note` until a privileged CI runner exists (WP-N fixture).
6. `--debug` stdout detail, `-Z` warning wording, and `--keep-directory-symlink`
   semantics (accepted as a no-op) are recorded, not implemented.
7. `--preserve` attributes are not applied to *recreated links* (GNU
   `lutimes`/`lchmod` on the link itself); the link is recreated correctly,
   its own times/mode are left at creation defaults.
8. After an `--update` skip, the skipped destination is not entered into the
   `--preserve=links` map (GNU's `remember_copied` records it), so a later
   hard-link sibling links to whichever destination was actually copied.
