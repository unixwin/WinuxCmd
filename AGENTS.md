# WinuxCmd I18N workflow

WinuxCmd uses English as the built-in fallback. A language is enabled only
when `WINUX_LANG` selects a catalog under `.wpm/i18n/<locale>/catalog.json`.

# How to build 
- Always use the scripts/build-with-vs.ps1 to build the project.
- Remember use scripts/format.py or scripts/format.sh to format the files.
- Rust toolchain (rust2 command library, #1488): install rustup plus
  both MSVC targets (`rustup target add x86_64-pc-windows-msvc
  aarch64-pc-windows-msvc`). `WINUXCMD_ENABLE_RUST2` defaults ON when
  cargo is found; without Rust the replaced commands (currently
  `true`, `false`) are absent from the build - no C++ fallback. A
  local arm64 (`x64_arm64`) build fails without the `aarch64` target.

## Source changes

- Every user-visible natural-language message needs a stable I18N key and an
  English fallback in the source.
- Keep command names, options, paths, URLs, hashes, protocol fields, numeric
  values, shell syntax, and user file contents unchanged.
- Use `scripts/i18n_batch.py extract` to regenerate the English catalog after
  adding commands, options, help text, or runtime messages.
- Use `scripts/i18n_batch.py validate` before publishing a locale catalog.

## Mandatory cross-repository synchronization

- Any change to WinuxCmd source, command help, options, versions, tests that assert user-visible output, or user-visible documentation MUST be reviewed for I18N impact.
- Every WinuxCmd change with user-visible text MUST update the separate `unixwin/winuxcmd-i18n` repository in the same task. Do not defer this to release time.
- For small batches, edit `catalogs/zh-CN/catalog.json` directly, preserving stable keys and valid JSON. Add the English fallback in WinuxCmd first, then add or update the Chinese translation in the I18N repository.
- Validate the catalog against the generated English catalog, commit both repositories separately, and push both before reporting completion.
- Check the previous WinuxCmd commit and the I18N repository history for missed catalog changes before starting a new sync.
- Every change under `catalogs/` in the I18N repository MUST bump the `VERSION` file in that repository in the same PR (at least the patch segment). CI blocks the PR otherwise. Merging the bump automatically publishes the `winuxcmd-i18n-zh-cn` release and opens the WPM index update PR; never ship catalog text changes without a version bump, because WPM treats an unchanged version as already installed.

## Catalog ownership

Catalogs are published in the separate `unixwin/winuxcmd-i18n` repository.
Local catalogs may be generated under `.wpm/i18n/` for testing, but must not be
committed to this repository. Use `scripts/i18n_sync.py` to prepare or publish
catalog updates.

When a command, option, version, or user-visible message changes, update the
catalog before the WinuxCmd release. The release workflow publishes the
English catalog automatically and preserves locale catalogs from the I18N
repository. Translation batches and manual review happen independently of the
WinuxCmd binary build.

# Banned commands

Never add a `link` command. Its hardlink-style name shadows the MSVC
toolchain linker `link.exe` on PATH and breaks builds that invoke it. This
command was removed once already (`10b2364`) and a bot PR re-added it
(#1065); do not accept any PR, generated or otherwise, that reintroduces it.

# wpm package contracts

Everything below was learned by shipping a package that broke, or by watching
a real installation break. They are contracts for anyone changing wpm or
publishing an artifact.

## Layouts and where the payload lives

- `layout: shim`: the artifact's `to` paths are relative to
  `<root>/opt/<pkg>/`, and `commands` become `<root>/usr/bin/<cmd>.exe`
  hardlinks of `winuxcmd.exe` that the dispatcher forwards to
  `opt/<pkg>/<cmd>.exe`. Use it whenever the payload has private DLLs or more
  than one command - it keeps everything out of a PATH directory (a flat
  openssh put `libcrypto.dll` into `usr/bin`).
- `layout: flat`: `to` paths are root-relative, so `to: foo.exe` is
  `<root>/usr/bin/foo.exe`. Use it when the binary *is* the entry the user and
  their editors resolve, so no dispatcher hop exists (git measured 12.7 ms as
  a real binary vs 32.2 ms behind a forwarding launcher).
- **Two installers put a directory of links on `%PATH%`**, so a PATH directory
  is never a private place: the standalone WinuxCmd setup adds `<root>\usr\bin`
  (`packaging/winuxcmd.iss`), and the niubash layout also has `<root>\bin` on
  PATH (this machine resolves `git` through `usr\bin` and has `bin` listed
  too). Both must therefore hold only the real thing and never a private DLL -
  a flat OpenSSH dropped `libcrypto.dll` into a PATH directory.
- wpm materializes `<root>/bin/<cmd>.exe` for every command of a package as a
  **hardlink to the real binary** (the payload under `opt/<pkg>/` for shim
  packages, the mapped binary in `usr/bin/` for flat ones) - never a launcher
  (12.7 ms as a real binary vs 32.2 ms behind a forwarding process), never a
  link into `.wpm/staging`. The links are recorded in the receipt's
  `destinations`, so install, update (the reconcile covers the expected set),
  `links rebuild` and uninstall keep them in sync (unixwin/niubash#376 rule 5).

## Install-path invariants

- **Never materialize a file from the extraction directory** when the same
  content is mapped by a directory mapping: source it from the payload
  (`payload_source_for`). Hardlinking out of `.wpm/staging` left
  `usr/bin/git.exe` on a transient inode plus a second full payload on disk,
  while the manifest declared `opt/niugit` (unixwin/WinuxCmd#1188).
- **Replace files by POSIX unlink, then create** (`delete_file_name` + copy):
  a payload file can be held by a process from the previous version - git's
  fsmonitor daemon holds `libexec/git-core/git.exe` while any monitored
  repository keeps it alive - and an in-place overwrite fails with a sharing
  violation, aborting the upgrade half-applied. Never `DeleteFileW` a name of
  the running wpm/winuxcmd image: it self-locks.
- **Prune `.wpm/staging/<pkg>` after a successful install**: the extraction
  tree is not the payload, and keeping it duplicates the package.
- Receipts record `destinations`; the reconcile removes exactly what the new
  artifact no longer covers, and never the winuxcmd image or the running exe.

## Versions

- Versions are ordered, not equality-checked: a dotted-numeric core plus an
  optional Debian-style packaging revision (`5.4.1-unixwin2` >
  `5.4.1-unixwin1` > `5.4.1`). A repackaged artifact **must** carry a revision
  or installed copies are never seen as an update. Vendor spellings
  (`9.5.0.0p1`, `r42`) fall back to "any difference is an update".
- The wpm index is the contract: `sha256`/`size` must match the published
  asset, and `files` must match the payload exactly - a mapping for a file the
  artifact no longer ships fails the install with "extracted path not found".

## Release flow

- `scripts/release.ps1 -Version X -PullRequestFlow` from a release branch;
  retitle `[Unreleased]` in the CHANGELOG first (the script does not).
- The script fetches the base branch (fatal, retried) and tags
  `origin/<base>`; tag hygiene problems must not abort a release, so tags are
  fetched separately with `--force`.
- After the tag, CI builds both architectures, publishes the release, and
  updates the wpm index automatically.

## Machine traps

- A stale local tag makes `git fetch --tags` fail while the branch ref was
  still updated: check the ref, not only the exit code.
- The local index cache can lag the published index (mirror ordering): run
  `wpm index update` before concluding an entry is wrong.
