---
name: winuxcmd
description: Repository-local WinuxCmd workflow for AI agents: download a released WinuxCmd build, integrate it into .winuxcmd/bin, activate the workspace without touching global PATH, use man.exe on Windows, run MSVC builds/tests, and package the skill for release. Use when working in this repository or preparing WinuxCmd release assets and the skill bundle.
---

# WinuxCmd

Use this skill when working inside the WinuxCmd repository or when preparing a
WinuxCmd release bundle.

## Start Here

1. If you are already in the repo, activate the workspace-local command
   directory for the current shell:

```powershell
.\scripts\activate-workspace.ps1
```

2. If you are in `cmd`, use:

```cmd
scripts\activate-workspace.cmd
```

3. If you are starting from a downloaded WinuxCmd release, register the release
   binary in the workspace first:

```powershell
.\scripts\setup-workspace-bin.ps1 -WinuxCmdPath "C:\path\to\winuxcmd.exe"
.\scripts\activate-workspace.ps1
```

4. When you need command help on Windows, call `man.exe`, not `man`.

## Why `man.exe`

- PowerShell resolves aliases before native executables.
- `man` is often an alias for PowerShell help, so `man.exe` is the unambiguous
  WinuxCmd command.
- Use explicit `.exe` names whenever an alias could collide with a WinuxCmd
  tool.

## Workspace Activation

- `.winuxcmd/bin` is the repo-local command directory.
- `scripts/setup-workspace-bin.ps1` creates the command links there.
- `scripts/activate-workspace.ps1` prepends `.winuxcmd/bin` to the current
  PowerShell session only and clears common alias collisions for that session.
- `scripts/activate-workspace.cmd` does the same for CMD sessions.
- `scripts/install-workspace-profile-hook.ps1` adds a per-user PowerShell 7 /
  Windows PowerShell 5.1 hook so new shells auto-activate in this repository.
- If you are already inside an extracted release directory, `scripts/create_links.ps1`
  can generate the release-side command links there.

Typical commands after activation:

```powershell
man.exe ls
grep.exe -n TODO README.md
winuxcmd.exe help sort
```

## Rules

- Do not modify user PATH or machine PATH.
- Do not rely on PowerShell aliases such as `man` or `ls`.
- Use the repository-local `.winuxcmd\bin` directory for command execution.
- If a command exists both as a PowerShell alias and as a WinuxCmd executable,
  always call the executable with `.exe`.

## Build and Test

- Use `scripts/build-with-vs.ps1` when you need an MSVC build/test run.
- Default target: `winuxcmd-tests`.
- Default build directory: `build-vs`.
- Default environment script: `vcvars64.bat`.

## Command Guidance

- After activation, bare `ls`, `cp`, `mv`, `rm`, and similar common commands
  should resolve to WinuxCmd in the current session.
- Use the repo's GNU-parity flags when they reduce AI mistakes:
  - `ls`: `-d`, `-b`, `-f`, `-I`, `-U`, `-X`, `-v`, `-i`, `-s`, `-h/--human-readable`, `--block-size=SIZE`, `-Q`, `-b`, `-q`, `-N`, `--quoting-style=WORD`; directory long listings include `total`, hard-link counts come from Windows file metadata, `-h` humanizes long sizes and `-s`/`total`, sort/time/quoting-affecting options use last-occurrence precedence, and wildcard matches that are directories list contents unless `-d` is active
  - `cp`: `-a/--archive`, `-p`, `-t`, `-T`, `-n`, `-u`, `-b/--backup`, `-S/--suffix`; `-t` requires an existing directory, `-t` and `-T` conflict, self-copy is refused except `--force --backup`, and plain file copies do not create missing parents
  - `mv`: `-t`, `-T`, `-n`, `-u`, `-b`; `-t` requires an existing directory, `-t` and `-T` conflict, and ordinary multi-source moves require an existing target directory after wildcard expansion
  - `rm`: `-f`, `-d/--dir`, `-r/-R/--recursive`, `-I`, `-i`, `--interactive[=never|once|always]`, `--one-file-system`; `rm -f` with no operands succeeds, `-d` removes only empty directories, and `-f`/`-i`/`-I`/`--interactive=WHEN` use last-option precedence for prompting/force behavior
  - `install`: `-D`, `-d`, `-t`, `-T`; `-t` and `-T` conflict, multi-source installs require an existing target directory after wildcard expansion, `-D -t` creates the target directory, and `-d` creates parent directories
  - `sort`: repeatable `-k/--key` with `F[.C][OPTS][,F[.C][OPTS]]`, `--sort=WORD`, `-M`, `-R`, `--random-source`, `-S/--buffer-size`, `-s`, `-t \0`, `-u`, `-V`; `-t \0` uses NUL as the field separator, global ordering modes use last-occurrence precedence, `-n` does not accept leading `+` or exponent notation, `-h` compares sign then SI suffix then value, `-u` uses sort-key equality and disables last-resort whole-line ordering, `-c/-C -u` rejects adjacent equal keys, `-b` affects `-k F.C` character counts, and end position `.0` means field end
  - `grep`: `-E`, `-F`, `-G`, repeatable `-e/--regexp`, repeatable `-f/--file`, `-i`, `-v`, `-n`, `-c`, `-l`, `-L`, `-H/-h`, `--label`, `--line-buffered`, repeatable `--include/--exclude/--exclude-from/--exclude-dir`, `-A/-B/-C` context output, `-o`, `-D/--devices`, `--color[=WHEN]`; bare `--color` defaults to `auto`, invalid color modes fail, zero-byte `-f` pattern files match nothing, include/exclude file rules keep command-line order and the last matching rule wins, command-line file operands match include/exclude globs by name suffix, `--exclude-dir` skips matching command-line directories and ignores trailing slashes, context ranges merge, match prefixes use `:`, context prefixes use `-`, `-o` warns while ignoring context options, and wildcard operands that expand to directories recurse when `-r/-R` is active
  - `sed`: ordered repeatable `-e/--expression` and `-f/--file`, `-i[SUFFIX]` / `--in-place[=SUFFIX]`, `-n/--quiet/--silent`, `-E`, `-r`, `-s`, `-z/--null-data/--zero-terminated`, `-u/--unbuffered`, `-b/--binary`; in-place editing creates no backup when suffix is omitted, appends suffix when it has no `*`, and replaces each `*` with the current filename otherwise; `s///` supports `&`, `\1`-`\9`, numeric occurrence flags, `g`, `p`, and `I/i`; address negation with `!`, `first~step`, `0~step`, and `/regex/I` are supported
  - `head`: `-c`, `-n`, `-q`, `-v`, `-z`; GNU count suffixes are accepted (`KB`/`MB`/.../`QB` decimal, `K`/`M`/.../`Q` binary, and `KiB`/.../`QiB` IEC) with overflow rejected; first-argument obsolete `-[num][bkm][cqv]` is accepted for compatibility, but prefer `-c NUM` or `-n NUM`
  - `tail`: `-c`, `-n`, `-q`, `-v`, `-z`, `-f`, `-F`, `-s`, `--max-unchanged-stats`, repeatable `--pid`, `--debug`, `--retry`; GNU count suffixes are accepted (`KB`/`MB`/.../`QB` decimal, `K`/`M`/.../`Q` binary, and `KiB`/.../`QiB` IEC) with overflow rejected; obsolete `-[num][bcl][f]` is accepted when it does not conflict with normal parsing, but prefer `-c NUM`, `-n NUM`, and `-f`
  - `find`: `-L`, `-name`, `-iname`, `-path`, `-ipath`, `-regex`, `-iregex`, `-type`, `-size`, `-empty`, `-mtime`, `-mmin`, `-newer`, `-mindepth`, `-maxdepth`, `-depth`, `-true`, `-false`, `!`, `-not`, implicit AND, `-a/-and`, `-o/-or`, parentheses, `-print`, `-print0`, `-printf`, `-delete`, `-prune`, `-quit`, `-exec ... {} ;`, `-exec ... {} +`, `-ok ... {} ;`; predicate operators and these common actions follow GNU expression precedence and short-circuit behavior; remaining gaps are comma expressions, `-ok ... {} +`, full `-printf`, full GNU regex dialect details, and `-delete` plus `-prune` interaction edge cases
  - `cut`: `-b/--bytes`, `-c/--characters`, `-f/--fields`, `--complement`, `--output-delimiter`, `-z`
  - `df`: `-h`, `-H`, `-k`, `-T/--print-type`, `-i/--inodes`
  - `du`: `-a`, `-A/--apparent-size`, `-B/--block-size`, `-b/--bytes`, `-c/--total`, `-d/--max-depth`, `-h`, `--si`, `-H/--dereference-args`, `-k`, `-s`, `-t/--threshold`, `--exclude=PATTERN`; default output uses 1024-byte blocks, later size-display options override earlier ones, `--block-size=human-readable|si` is accepted, `--max-depth=0` prints only the root total, `--exclude` patterns stay literal option patterns, Windows size reporting uses file length as the allocated/apparent-size approximation, and `-H` is not SI output
  - `env`: `-i/--ignore-environment`, repeatable `-u/--unset=NAME`, `-0/--null`, `-C/--chdir=DIR`, `NAME=VALUE COMMAND [ARG]...`
  - `realpath`: `-e`, `-m`, `-q`, `-s`, `-z`
  - `xargs`: `-n/--max-args`, `-L/--max-lines`, `-l`, `-I`, `-i/--replace`, `-P/--max-procs`, `-p/--interactive`, `-0/--null`, `-d/--delimiter`, `-a/--arg-file`, `-E`, `-e/--eof`, `--show-limits`, `-t/--verbose`, `-o/--open-tty`, `-r/--no-run-if-empty`, `-s/--max-chars`, `-x/--exit`, `--process-slot-var`; `-p` prompts before each command and runs only for `y`/`Y`, default echo output has no trailing space; mutually exclusive `-I/-i`, `-L/-l`, and `-n` families use the last conflicting family and warn, except `-I ... -n1`; child failures map GNU-style for common cases (`1..125 -> 123`, `255 -> 124`, not found -> `127`)
  - `dd`: `if=`, `of=`, `bs=`, `ibs=`, `obs=`, `count=`, `skip=`, `seek=`, `conv=sync`, `conv=notrunc`, `status=none|noxfer|progress`
- Treat wildcard characters as syntax unless the operand is a file input:
  `grep` patterns, `sed` scripts, `jq` filters, `xargs` input items, and
  destination operands such as `tee OUT` or `install SRC DEST` stay literal.
  File input operands such as `grep PATTERN *.txt` and `sed SCRIPT *.txt`
  expand inside WinuxCmd.
- For fixed-arity file commands such as `diff`, `diff3`, `split`, and
  `csplit`, let wildcard expansion resolve to the exact number of operands the
  command expects; do not guess a first match.
- Keep the implementation matrix and GNU parity ledger synchronized with code
  changes in the same batch.
- If a command is already available as a WinuxCmd executable, prefer it over a
  PowerShell alias or external fallback.

## Release Packaging

- When packaging the skill, load `references/release-packaging.md` first.
- Confirm the bundle layout, then package the standalone bundle with the
  Windows binaries.
- Keep the skill bundle rooted at `winuxcmd/` with `SKILL.md` at the top level
  of the archive.
- Keep the skill bundle small and release-ready; long-form details belong in
  `references/`.

## References

- `references/workspace-integration.md`
- `references/download-and-integrate.md`
- `references/command-guidance.md`
- `references/release-packaging.md`
