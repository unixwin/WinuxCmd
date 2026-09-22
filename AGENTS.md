# WinuxCmd I18N workflow

WinuxCmd uses English as the built-in fallback. A language is enabled only
when `WINUX_LANG` selects a catalog under `.wpm/i18n/<locale>/catalog.json`.

# How to build 
- Always use the scripts/build-with-vs.ps1 to build the project.
- Remember use scripts/format.py or scripts/format.sh to format the files.

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
