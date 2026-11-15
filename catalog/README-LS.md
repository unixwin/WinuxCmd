# ls rust2 lane — I18N catalog patch (issue #1488)

Prepared by the ls rust2 lane (`rust2/ls-impl-devin`, worktree
`D:/repo/wc-devin-ls`). The captain lands these files in the separate
`unixwin/winuxcmd-i18n` repository; nothing here was written into the shared
checkout.

## What is in this directory

| file | meaning |
|---|---|
| `en-US-ls.json` | `python scripts/i18n_batch.py extract --output catalog/en-US-ls.json` output for this tree (3640 keys) |
| `zh-CN-ls-patch.json` | the **21** new `command.ls.*` runtime keys with zh-CN text (patch payload) |
| `ls-new-keys.txt` | flat `key<TAB>repr(english)` list of the 21 new keys |
| `ls-delta.json` | added (21) / removed (0) / changed (0) key delta vs the pre-ls tree |
| `zh-CN-ls-merged-validate.json` | en-US overlaid with the patch — the file the validator was run against |

## Numbers

* new stable keys: **21** — 20 `command.ls.error.*` + 1 `command.ls.total`.
* delta baseline: `git archive HEAD` extract (3619 keys) vs this tree's
  extract (3640 keys): **+21 / -0 / ~0**, all `command.ls.*`. The only
  user-visible source added by this lane is
  `rust2/crates/winux-cmds/src/ls.rs` (`tr!` calls); `glob.rs`, `lib.rs`
  and `gate/gen-cases.py` carry no catalog keys.
* English fallbacks live in the Rust source; extraction discovers them
  through `scripts/i18n_rust.py`.
* The 21 zh-CN strings were **already landed** in
  `unixwin/winuxcmd-i18n@3013ceb` ("sync split/comm/ls error keys … + ls
  lane", 2026-11-14) — this patch re-states them verbatim from
  `catalogs/zh-CN/catalog.json` so the worktree is self-contained.
* validation: `python scripts/i18n_batch.py validate --base
  catalog/en-US-ls.json --translated catalog/zh-CN-ls-merged-validate.json`
  → `valid` (keys equal, placeholders equal, no option-spelling drift).
* `ls` is a library-only lane (not in `WINUXCMD_RUST2_COMMANDS`); the C++
  `src/commands/ls.cpp` stays the product path until the wire wave.

## Release note

Every change under `catalogs/` in `unixwin/winuxcmd-i18n` must bump that
repository's `VERSION` file in the same PR (AGENTS.md). The zh-CN half of
this delta already shipped in `3013ceb`; the captain only needs the English
side published by the release workflow — no new catalog PR is required for
this lane unless the key set changes before merge.
