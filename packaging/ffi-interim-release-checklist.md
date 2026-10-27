# FFI interim release checklist — WinuxCmd 1.1.21

Prep-only document. This checklist prepares the interim release; it does
not authorize it. Tagging, running `scripts/release.ps1`, and publishing
happen only on the main session's order.

Release identity:

- Version: **1.1.21** (patch increment in the 1.1.x series; see
  PROJECT_VERSION history — every release since 1.1.0 has bumped the
  patch segment by one).
- Prep branch: `chore/ffi-interim-release-prep` (draft PR; prep-only).
- Drafted against: `main` @ `ea8e40cc` (2026-10-27).
- **Release baseline: `main` after PRs #1414, #1415 and #1423 merge.**
  The CHANGELOG [1.1.21] section must be re-reconciled against that
  final `main` before tagging (gate 6).

Every gate below starts **unmet** unless its status line says otherwise.
Do not check a box on intention; check it on evidence.

## Gates

### Gate 1 — Prototype line: Wine real-run matrix

- [ ] Prototype line's Wine run matrix for the FFI DLL passes.
  - Conclusion (filled by the prototype line, coordinated separately):
    _— empty —_
  - Status: **未满足 (unmet)** — no conclusion recorded yet.

### Gate 2 — Regression verification line: evidence pack

- [ ] Regression/differential verification evidence pack passes
  (FFI vs spawn byte-level differential, incl. large output, pipe
  middle, stdin shapes).
  - Conclusion (filled by the verification line, coordinated
    separately): _— empty —_
  - Status: **未满足 (unmet)** — no conclusion recorded yet.

### Gate 3 — PR #1414 merged, x64 CI final state green

- [ ] PR #1414 (`fix(diff): pair same-name entries in recursive
  directory comparison (#1344)`) is merged into `main`, with the
  `build-windows (x64)` job of the CMake Windows Multi-Arch CI at a
  green final state.
  - Snapshot at prep time (2026-10-27, gh): PR OPEN (draft);
    `build-windows (x64, x64, x64)` = **FAILURE**; arm64 = SUCCESS;
    GNU differential oracle/compare = SUCCESS.
  - Status: **未满足 (unmet)** — open, and x64 is red, not merely queued.

### Gate 4 — PR #1415 merged, x64 CI final state green

- [ ] PR #1415 (`WIP: 路径层修复 — 尾点/空格操作数与盘上 NUL.txt
  (#1332, #1333)`) is merged into `main`, with x64 CI green.
  - Snapshot at prep time (2026-10-27, gh): PR OPEN (draft);
    `build-windows (x64, x64, x64)` = **FAILURE**; GNU differential
    `oracle` = **FAILURE**; arm64 = SUCCESS.
  - Status: **未满足 (unmet)** — open, x64 and oracle red.

### Gate 5 — PR #1423 merged, x64 CI final state green

- [ ] PR #1423 (`WIP: winx-reclaim — GCC wall class completion + cpio
  operand boundary (#1412, #1422)`) is merged into `main`, with x64 CI
  green.
  - Snapshot at prep time (2026-10-27, gh): PR OPEN (draft);
    `build-windows (x64, x64, x64)` = **IN_PROGRESS**; GNU differential
    `oracle` = **FAILURE**; arm64 = SUCCESS.
  - Status: **未满足 (unmet)** — open, x64 not finished, oracle red.

### Gate 6 — Version / CHANGELOG reconciled against final main

- [ ] After gates 3–5: re-run the reconciliation of the CHANGELOG
  [1.1.21] section against the final `main`:
  - every substantive change on final `main` since 1.1.20 is covered;
  - each `[PENDING PR #…]` entry is either promoted to a regular entry
    (PR merged) or deleted (PR not merged — its change must not be
    advertised in this release);
  - the section header is retitled from `Unreleased` to the release
    date (the release script does not retitle; see AGENTS.md release
    flow);
  - if the FFI payload does not ship (gates 1/2/7 failed and the DLL is
    dropped from the package), the FFI entry in the CHANGELOG is
    removed in the same pass — the changelog must not advertise a
    payload the package does not contain.
  - Status: **未满足 (unmet)** — section is drafted against
    `ea8e40cc`, with three `[PENDING]` placeholders outstanding.

### Gate 7 — FFI payload actually in the package

- [ ] The CMake install-rule patch proposal (§Proposal A below) has
  been applied by the coordinator (this prep does not touch
  `CMakeLists.txt` / `CMakePresets.json` / `src/**`), and a locally
  produced package archive for 1.1.21 has been inspected to contain:
  - `usr/bin/ffi/v1.1.21/winuxcore.dll`
  - `usr/bin/ffi/v1.1.21/tiers.tsv`
  - `usr/bin/ffi/current.txt` containing the single line `v1.1.21`
  - `usr/bin/ffi/FFI-EXPERIMENTAL-NOTICE.md`
  - `SHA256SUMS.txt` including the DLL line (§Proposal B).
  - Status: **未满足 (unmet)** — proposal written, not applied; no
    package built with the payload yet.

### Gate 8 — DLL hash pinned in the wpm index (P2 wiring)

- [ ] The wpm-source index entry for winuxcmd 1.1.21 carries the `ffi`
  field set specified in `packaging/ffi/README.md` §4, and its `sha256`
  matches both the shipped DLL and the `SHA256SUMS.txt` line.
  (Wiring is applied by the wpm-side/index change and the
  `update-wpm-source-index` automation at release time; this gate is
  verified, not performed, by the prep.)
  - Status: **未满足 (unmet)**.

### Gate 9 — Signing / experimental label (D8)

- [ ] One of the following is true, and the shipped state matches the
  CHANGELOG + notice wording:
  - (interim path) the DLL ships **unsigned**, labelled experimental:
    `FFI-EXPERIMENTAL-NOTICE.md` is in the package (gate 7), the
    feature is default-off with `NIU_WINUXCMD_FFI=1` as the only
    enable path, and the SmartScreen/Defender notes are present; or
  - (signed path) the DLL is covered by the product's code-signing
    line (niu #511) and the signature verifies.
  - Signing itself belongs to the niu #511 line; this prep only lists
    the check.
  - Status: **未满足 (unmet)** — no DLL built yet; the interim path is
    the planned one. A signed DLL remains the hard gate for any
    non-experimental FFI release.

### Gate 10 — I18N review (repository AGENTS.md rule)

- [ ] The version bump and user-visible documentation changes in this
  release have been reviewed for I18N impact per the repository
  AGENTS.md synchronization rule; the release workflow's English
  catalog publication is expected, and any zh-CN catalog follow-up in
  the separate i18n repository is identified (it is a separate repo and
  outside this prep's file surface).
  - Status: **未满足 (unmet)** — not yet reviewed.

### Gate 11 — Rollback plan ready

- [x] Rollback is documented: flip `usr/bin/ffi/current.txt` back to
  the retained previous version directory (wpm: temp file + atomic
  rename), new shells pick it up; previous version retained by the
  keep-last-2 GC rule in `packaging/ffi/README.md` §2/§5. Full
  rollback = reinstall the previous package version.
  - Status: **文档已备 (documented)**; the flip itself is untested
    until a two-version tree exists (first test happens with the
    release after this one, or in the prototype line's staging).

### Gate 12 — Release execution (main session's order only)

- [ ] On the main session's explicit order only: retitle the CHANGELOG
  section, run `scripts/release.ps1 -Version 1.1.21 -PullRequestFlow`
  from the release branch, tag `v1.1.21`, and let CI build, publish
  and update the wpm index.
  - Status: **未满足 (unmet)** — and not to be started by this prep
    line under any circumstances.

## Proposal A — CMake install rules for the FFI payload (to be applied by the coordinator)

Not applied by this prep (`CMakeLists.txt` is outside its file
surface). For the coordinator to apply, in `CMakeLists.txt`, near the
existing install rules (and respecting the existing `BUILD_FFI` /
`src/ffi` arrangement — note the comment there that presets currently
pass `BUILD_FFI=ON`, cf. DESIGN P1's preset-drift cleanup):

```cmake
if (BUILD_FFI AND TARGET winuxcore)
    install(TARGETS winuxcore
            RUNTIME DESTINATION "usr/bin/ffi/v${PROJECT_VERSION}"
            COMPONENT Runtime)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/ffi/tiers.tsv"
            DESTINATION "usr/bin/ffi/v${PROJECT_VERSION}"
            COMPONENT Runtime)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/ffi/FFI-EXPERIMENTAL-NOTICE.md"
            DESTINATION "usr/bin/ffi"
            COMPONENT Runtime)
    # Initial pointer for a fresh install; afterwards wpm is the only
    # writer and flips it atomically (DESIGN §4.2).
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ffi-current.txt" "v${PROJECT_VERSION}\n")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ffi-current.txt"
            DESTINATION "usr/bin/ffi"
            RENAME "current.txt"
            COMPONENT Runtime)
endif ()
```

Open points the coordinator/prototype line must confirm when applying
(this prep could not verify them — `src/ffi` is not tracked in this
checkout):

- the DLL target is actually named `winuxcore` in
  `src/ffi/CMakeLists.txt`;
- `tiers.tsv` is generated into `src/ffi/` (source dir) vs the build
  dir — adjust the `install(FILES …)` path accordingly;
- `current.txt` line ending is LF (the pointer is compared as a
  directory name by the host).

## Proposal B — SHA256SUMS.txt generation step (to be applied by the coordinator)

Generate after install staging, before CPack/archive creation, over the
staged tree (per `packaging/ffi/README.md` §3): one
`<lowercase sha256>  <relative path>` line per payload file, sorted by
path, manifest itself excluded, DLL line mandatory. Use the same
`Get-FileHash -Algorithm SHA256` primitive the wpm index script already
uses. Where the step lives (CI package step vs a small script under
`scripts/`) is the coordinator's call; both are outside this prep's
file surface.
