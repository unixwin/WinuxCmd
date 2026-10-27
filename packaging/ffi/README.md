# FFI payload — packaging layout and update rules

Status: packaging-side specification for the FFI interim release
(WinuxCmd 1.1.21). This document describes where the FFI payload lives
inside the package, who writes what, and how it is updated and rolled
back. It follows `ffi-route` DESIGN §4 (distribution & updates, D3:
same-package distribution; D8: explicit experimental label first).

The FFI feature itself is **experimental, opt-in and default off**
(see `FFI-EXPERIMENTAL-NOTICE.md` in this directory, which ships inside
the package). Nothing in this layout changes the default PATH / spawn
behaviour of the product.

## 1. Layout (inside the install root `<root>`)

| Path | Content | Writer |
|---|---|---|
| `<root>/usr/bin/winuxcmd.exe` | existing dispatcher (unchanged) | installer / wpm |
| `<root>/usr/bin/ffi/v1.1.21/winuxcore.dll` | versioned side-by-side FFI library for this release | package |
| `<root>/usr/bin/ffi/v1.1.21/tiers.tsv` | tier table + per-call-shape table for exactly this DLL version | package (generated at build time by the FFI build, shipped beside the DLL) |
| `<root>/usr/bin/ffi/current.txt` | pointer file: the single line `v1.1.21` on a fresh install | package ships the initial value; **wpm flips it on updates** (temp file + atomic rename) |
| `<root>/usr/bin/ffi/FFI-EXPERIMENTAL-NOTICE.md` | the experimental-feature notice | package |
| `<root>/SHA256SUMS.txt` | package SHA-256 manifest, one line per payload file, **including the DLL line** (see §3) | generated at package time |

Rules that come with the layout:

- Version directories are **side-by-side and immutable**: an update never
  overwrites or deletes a version directory that a running shell may
  have mapped. New versions land in a new `v<X.Y.Z>/` directory.
- `current.txt` is a data file, not an image: flipping it is not blocked
  by a loaded DLL. Running shells keep using the version they already
  loaded until they exit; new shells read the pointer. "Takes effect on
  next shell start" is the update semantics — there is no in-process
  hot-swap.
- The DLL is only ever found through this layout (install root +
  `current.txt`), never through the CWD or `PATH`.

Installer note: `packaging/winuxcmd.iss` stages the whole install tree
(`Source: "{#StageDir}\*"` with `recursesubdirs`), so the `ffi/`
subtree flows into the standalone installer automatically once the
build stages it — no `.iss` change is needed for the interim release.

## 2. Retention / garbage collection

- Keep the **two most recent** version directories under
  `<root>/usr/bin/ffi/` (the one `current.txt` points at, plus the
  previous one — the previous one is the rollback target).
- GC runs at **wpm update time** (wpm is the only writer of this tree
  after installation): after flipping `current.txt`, delete version
  directories older than the retained two.
- If a directory cannot be deleted because a running process still has
  its DLL mapped (sharing violation), leave it and retry on the next
  update. This self-healing lag is expected behaviour, not an error.
- The standalone (Inno Setup) installer always installs exactly its own
  version; GC does not apply to it.

## 3. SHA-256 manifest (hash pinning, inner loop)

DESIGN §4.3 pins the DLL hash in two loops: the wpm index (outer) and
load-time verification against the index / in-package manifest (inner).
The in-package half is produced like this:

- At package time, after the install tree is staged and before the
  archives are built, compute SHA-256 for every payload file and write
  `<root>/SHA256SUMS.txt`, one line per file in the form
  `<lowercase sha256>  <path relative to root, forward slashes>`,
  sorted by path. The manifest lists every payload file except itself.
- The line for the DLL is mandatory and must read (for this release):

  ```
  <sha256 of winuxcore.dll>  usr/bin/ffi/v1.1.21/winuxcore.dll
  ```

- Generation precedent in this repository:
  `.github/scripts/update-wpm-source-index.ps1` already hashes release
  archives with `Get-FileHash -Algorithm SHA256` and lower-cases the
  result; the manifest generator uses the same primitive and casing.
- Wiring note: generating the manifest inside the packaged tree needs a
  step in the build/packaging pipeline (CMake install-time or CI
  package step). That step lives outside the packaging file surface and
  is listed as a to-be-applied proposal in
  `packaging/ffi-interim-release-checklist.md` — it is **not** applied
  by the release-prep change itself.

## 4. wpm index wiring (P2 surface — description only)

The outer pinning loop is the wpm source index (`unixwin/wpm-source`,
updated today by `.github/scripts/update-wpm-source-index.ps1`, which
already records each release archive's `sha256`/`size`). For the FFI
payload the index entry for the winuxcmd package gains a dedicated DLL
field set, written by the same update step from the staged DLL:

```json
"ffi": {
  "version": "1.1.21",
  "dll": "usr/bin/ffi/v1.1.21/winuxcore.dll",
  "sha256": "<lowercase sha256 of winuxcore.dll, as shipped>"
}
```

This is a wiring description for the wpm-side change (P2); the prep
change does not edit the index script or the index. The checklist gates
the interim release on this field being populated and matching both the
shipped DLL and the in-package manifest line.

## 5. Rollback

- Fast rollback (no reinstall): write the previous version directory
  name (e.g. `v1.1.20`-style predecessor retained by §2) back into
  `<root>/usr/bin/ffi/current.txt` — wpm does this as temp file +
  atomic rename — and start new shells. The retained previous version
  exists precisely for this.
- Full rollback: reinstall the previous winuxcmd package version
  through wpm / the standalone installer, as with any release.
- A hash mismatch is not a rollback case: the host refuses to load the
  DLL and falls back to spawn (DESIGN §2.3). Rollback is for behaviour
  regressions in a correctly-loaded DLL.
