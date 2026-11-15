# mv wire lane — I18N catalog patch (issue #1488)

Prepared by the mv wire lane (`rust2/mv-impl-devin`, worktree
`D:/repo/wc-devin-mv`). The captain lands these files in the separate
`unixwin/winuxcmd-i18n` repository; nothing here was written into the shared
checkout (lane 15 owns `D:/repo/winuxcmd-i18n`).

## What is in this directory

| file | meaning |
|---|---|
| `en-US-mv.json` | `python scripts/i18n_batch.py extract --output catalog/en-US-mv.json` output for this tree (3878 keys) |
| `zh-CN-mv-patch.json` | the **52** new `command.mv.*` keys with reviewed zh-CN text (patch payload) |
| `mv-new-keys.txt` | flat `key<TAB>repr(english)` list of the 52 new keys |
| `mv-delta.json` | added (167) / removed (73) key delta against the pre-wire tree, with the mv attribution split out |
| `zh-CN-mv-merged-validate.json` | en-US overlaid with the patch — the file the validator was run against |

## Numbers

* new stable keys: **52** — 45 `command.mv.error.*`, 2 `command.mv.prompt.*`,
  3 `command.mv.verbose.*`, 1 `command.mv.warning.*`, 1 `command.mv.debug.*`.
* removed keys attributable to the retired 1.x `src/commands/mv.cpp` body:
  **8** `legacy.<fnv>` digests — `mv: remove `, `directory `, ` arguments`,
  ` (backup: `, `removed `, `cannot create backup for destination`,
  `rename failed and copying is disabled by --no-copy`,
  `--exchange requires exactly one source and one destination`. The remaining
  65 removed `legacy.*` entries and 115 added non-mv keys are pre-existing
  catalog drift from other in-flight lanes (cp / grep / stat / ln / …), not
  this wire.
* English fallbacks live in the Rust source (`tr!` calls in
  `rust2/crates/winux-cmds/src/mv.rs` and `mv/opts.rs`); extraction
  discovers them through `scripts/i18n_rust.py`.
* 33 of the 52 texts are byte-identical to their `command.cp.*` counterpart
  and reuse the already-reviewed cp translation (the file is loaded from the
  landed `winuxcmd-i18n-cp-runtime.patch.json`); the 19 mv-specific ones were
  translated here (prompt shapes, backup/rename wording, `-v` lines).
* validation: `python scripts/i18n_batch.py validate --base
  catalog/en-US-mv.json --translated catalog/zh-CN-mv-merged-validate.json`
  → `valid` (keys equal, placeholders equal, no option-spelling drift).
* The `mv: ` program prefix stays in code (`fail()`/`say()` write it, the
  `-i` prompt writes it), so no fallback embeds it; the `Try 'mv --help'`
  tail is the shared `common.try_help` key on the C++ dispatcher and the
  Rust operand grammar writes its own literal line (GNU wording).

## Release note

Every change under `catalogs/` in `unixwin/winuxcmd-i18n` must bump that
repository's `VERSION` file in the same PR (AGENTS.md). The captain sequences
that bump with the catalog drop; this lane did not touch the shared repo.
