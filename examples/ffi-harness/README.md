# ffi-harness — host prototype for winuxcore.dll (FFI ABI v2)

A small standalone host that exercises the loader contract of the FFI
route design (sections 3.2 and 4.1) against `src/ffi/winux_ffi.h`:

```
ffi-harness.exe <install-root> <applet> [args...]
```

The install root is a WinuxCmd tree laid out as:

```
<root>/usr/bin/winuxcmd.exe            spawn form (fallback + reference)
<root>/usr/bin/ffi/current.txt         pointer file: active version dir name
<root>/usr/bin/ffi/v<version>/winuxcore.dll
<root>/usr/bin/ffi/v<version>/tiers.tsv
<root>/usr/bin/ffi/v<version>/winuxcore.sha256   pinned SHA-256 (hex)
```

Behaviour:

- `NIU_WINUXCMD_FFI` unset or `0`: every command is declined to the spawn
  form (`reason=switch-off`). `=1` enables in-process calls.
- `NIU_WINUXCMD_FFI_EXCLUDE`: comma-separated commands that always spawn.
- `NIU_WINUXCMD_FFI_DLL`: absolute-path DLL override (relative values are
  ignored with `reason=override-not-absolute`). Discovery never consults
  the current directory or PATH.
- Only `tier1` rows of `tiers.tsv` run in-process; a per-call fallback
  shape listed in the row (e.g. `rm -i`, `rm -I`) declines that call
  (`reason=shape-fallback`).
- The DLL is SHA-256-verified against `winuxcore.sha256` before loading,
  loaded once with `LoadLibraryExW(LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
  LOAD_LIBRARY_SEARCH_SYSTEM32)`, and its `winux_ffi_abi_version()` must
  equal the harness's expected ABI before `winux_ffi_init` /
  `winux_ffi_call` are used.
- Every failure is fail-closed to the spawn form with a distinguishable
  reason on stderr: `pointer-missing`, `pointer-invalid`, `dll-missing`,
  `tier-unknown`, `not-tier1`, `manifest-missing`, `hash-unreadable`,
  `hash-mismatch`, `load-failed`, `symbols-missing`, `abi-mismatch`,
  `init-failed`, `ffi-call-failed`, plus `spawn-target-missing` /
  `spawn-failed` when even the fallback cannot run. Diagnostics never go
  to stdout, so command output stays byte-comparable between routes.

## Demo doubles (`demo/`, `-DBUILD_FFI_HARNESS_DEMO=ON`)

`demo/` builds two test doubles over one shared applet implementation
(`cat`/`cp`/`rm`): `winuxcore.dll` (ABI v2 surface) and `winuxcmd-demo.exe`
(spawn twin). They exist to exercise the loader end-to-end where the real
DLL has not been built (e.g. a toolchain blocked on an unrelated module).
They are **not** WinuxCmd implementations and are never installed:
byte-equivalence between the doubles validates the harness mechanics
only, never the real commands. `smoke.ps1` assembles a demo layout and
runs the decision matrix on Windows.
