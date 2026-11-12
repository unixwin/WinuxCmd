# gate/PROBE-METHODOLOGY.md — probe channels for POSIX-spelling probes (#1488)

The gate's posix round probes the shell-namespace contract: rubash hands its
own applets POSIX spellings **verbatim** (niubash#346 boundary; the C++
mapper is `src/utils/native_path.cppm:shell_tree_map_w`) and the applets must
resolve them. Probing that contract means getting a POSIX spelling from the
probe harness into the product's argv **unmodified**. On this host that is
harder than it sounds: there are three candidate channels, two of them
silently rewrite the spelling. Every claim below was measured live on
2026-11-12 in the `wc-20inv` worktree; reproduce with the snippets shown.

## Channel matrix

| channel | argv fidelity | use for | never use for |
| --- | --- | --- | --- |
| agent bash (niubash-family) | REWRITTEN, non-attributable | probing niubash itself | product-contract probes |
| PowerShell → native exe | verbatim | product-contract probes (Rust driver, C++ dispatcher) | — |
| WSL GNU bash, script file | native POSIX | GNU oracle side | — |

## Channel 1 — the agent's bash is niubash-family: POSIX args do NOT survive

`bash` on the agent PATH resolves to
`C:/Users/cmx/AppData/Local/Programs/Niubash/winuxcmd/usr/bin/bash.exe`
(`which -a bash`; the Bash tool itself runs under `Niubash/niu.exe`), an
MSYS-family build ("GNU bash, version 5.3.20(1)-release (x86_64-pc-msys)").
Three distinct rewriting layers were measured:

1. **Spawn-layer conversion for native children.** Handing a native exe a
   POSIX spelling through niubash bash rewrites it to niubash-root/Temp
   native forms:

   ```sh
   # probe-conv.sh (script FILE, not -c):
   /d/repo/wc-20inv/rust2/target/debug/winuxcmd-driver.exe realpath /usr/bin/echo
   # -> C:\Users\cmx\AppData\Local\Programs\Niubash\winuxcmd\usr\bin\echo.exe  (rc 0)
   env <driver> realpath /tmp/probe-spelling
   # -> C:\Users\cmx\AppData\Local\Temp\probe-spelling
   ```

   The driver prints the path it was GIVEN (PowerShell control, below,
   resolves the same spellings to errors) — so the conversion happened in the
   niubash spawn path, before argv reached the exe. MSYS-side children
   (e.g. `mkdir.exe` from the same tree) keep argv bytes but their runtime
   **mounts** still remap `/tmp` → `%TEMP%`
   (`bash -c 'mkdir -p /tmp/x'` creates `%TEMP%\x`; `cygpath -w /tmp` →
   `C:\Users\cmx\AppData\Local\Temp`), so the observable landing is decided
   by niubash config, not by the product under test.

2. **The knobs do not work.** Both `MSYS_NO_PATHCONV=1` (a Git-for-Windows
   knob) and `MSYS2_ARG_CONV_EXCL='*'` (the MSYS2 knob) were exported around
   the same spawns: identical rewritten landings, every time. Neither knob
   reaches niubash's conversion.

3. **`bash -c` mangles the payload itself.** The same driver probe handed
   through `bash -c '... realpath /usr/bin/echo'` returned
   `D:\repo\rubash\usrbinecho` — the command STRING was folded (slashes
   eaten) before bash even parsed it. Script files dodge this layer;
   nothing user-side dodges layer 1.

   Worse: with `TEMP`/`TMP` stripped from the child env, the
   `mkdir -p /tmp/<name>` landing vanished from every expected location
   (checked `%TEMP%`, `D:\tmp`, the niubash root) while rc stayed 0 — the
   landing can be **non-attributable**, which disqualifies the channel for
   any landing assertion.

**Rules:** never hand a POSIX spelling to the product through the agent
bash; never interpret a landing observed through it. It is fine for probing
niubash behavior itself and for relative-path snippets (relative tokens are
never rewritten).

## Channel 2 — PowerShell hands argv to native exes verbatim

PowerShell (`powershell -NoProfile -Command "& 'D:\...\winuxcmd-driver.exe'
realpath /usr/bin/echo"`) delivered the spelling untouched:

```text
realpath: /usr/bin/echo: No such file or directory   (rc 1)
```

and the same channel reproduced the recorded product landfall:

```powershell
$env:TEMP=''; $env:TMP=''; $env:TMPDIR=''
& 'D:\repo\wc-20inv\build-vs\usr\bin\winuxcmd.exe' mkdir -p /tmp/<name>
# -> dir created at D:\tmp\<name>  (the niubash#346 live-probe landfall:
#    with no TMPDIR/TEMP/TMP, /tmp is outside the shell namespace and the
#    literal spelling resolves against the cwd drive)
```

This is the product-contract channel: rubash → winuxcmd is a native
argv handoff, PowerShell reproduces it faithfully. `run-gate.sh
--posix-mode` uses it for exactly that reason (Start-Process with file
redirects also keeps stdout/stderr BYTES raw, which PowerShell text
pipelines would re-encode). Pitfalls:

- `-ArgumentList` takes a raw argument string on Windows PowerShell 5.1
  (arrays are joined WITHOUT quoting) — the harness quotes each token
  itself (`cmdq()` in run-gate.sh); no embedded double quotes in the posix
  corpus.
- Write `.rc` with `-Encoding Ascii` — the PS 5.1 default re-encodes as
  UTF-16 and the harness would read garbage.
- Use full exe paths; PATH lookup differs from bash's.

The QC layer needs no PowerShell: Python `subprocess.run([exe, ...])` is
verbatim by construction (`qc_common.run_win_side`).

## Channel 3 — WSL GNU oracle: script FILE + env, never inline `-c`

The GNU side of the gate is the POSIX oracle: real coreutils 9.4 on ext4.
Correct use: write the probe to an LF-terminated `.sh` file under the tree,
address it through the `/mnt/<drive>` view, pass data by file or env —
never by inline quoting:

```sh
cat > target/gate/probe.sh <<'EOF'
#!/bin/bash
mkdir -p /tmp/probe && realpath /tmp/probe
EOF
MSYS_NO_PATHCONV=1 wsl bash /mnt/d/repo/wc-20inv/target/gate/probe.sh
```

Measured failure of the inline form (the same hazard rubash's docs record):
`wsl bash -c 'echo a\\b'` printed `ab` — wsl.exe's command-line passthrough
collapsed the doubled backslash before bash saw it. Anything with doubled
backslashes, multi-level quoting, or `$` expansions must go through a file.
`MSYS_NO_PATHCONV=1` on the CALLER side only stops Git Bash from rewriting
`/mnt/...` arguments of `wsl.exe` itself; it has no effect inside WSL and
none on niubash (channel 1).

## How the gate applies this

- **run-gate.sh --posix-mode** — Windows sides via PowerShell (channel 2);
  GNU side via script file (channel 3); setup snippets run THROUGH THE
  SIDE'S OWN EXE so no POSIX spelling ever crosses a shell conversion layer.
- **qc_*.py** — native `subprocess` (verbatim), setups run under local bash
  restricted to relative-only snippets when they accompany POSIX-spelled
  cases.
- **Landing assertions** — a verdict must never be scored by an accidental
  literal landfall: `mkdir -p /tmp/x` on an unmapped spelling happily
  creates `D:\tmp\x` and matches GNU's rc. The posix run therefore walks
  each POSIX operand's tail against the cwd-drive roots after every case
  (`landing=leak` → pending) and records the resolved landings in stdout for
  the audit (`realpath` rows).
