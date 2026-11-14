# find wire lane — I18N catalog patch (issue #1488)

Prepared by the find wire lane (`rust2/find-wire`, worktree `D:/repo/wc-findwire`).
The captain lands these files in the separate `unixwin/winuxcmd-i18n`
repository; nothing here was written into the shared checkout (lane 15 owns
`D:/repo/winuxcmd-i18n`).

## What is in this directory

| file | meaning |
|---|---|
| `en-US.json` | `python scripts/i18n_batch.py extract` output for this tree (3642 keys; 183 `command.find.*`, of which 104 are the unchanged C++ registration options) |
| `zh-CN-find-patch.json` | the **77** new `command.find.{error,warning,prompt}.*` keys with reviewed zh-CN text (patch payload) |
| `find-orphan-legacy-keys.json` | the **33** `legacy.<fnv>` keys the deleted 1.x C++ find body produced (drop from the locale catalogs) |
| `delta.json` | added (77) / removed (33) key delta against the pre-wire tree |
| `find-new-keys.txt` | flat `key<TAB>repr(english)` list of the 77 new keys |
| `zh-CN-merged-validate.json` | en-US overlaid with the patch — the file the validator was run against |

## Numbers

* new stable keys: **77** — 66 `command.find.error.*`, 10
  `command.find.warning.*`, 1 `command.find.prompt.ok`.
* removed keys: **33** orphan `legacy.<fnv>` entries from
  `src/commands/find.cpp`'s deleted `safePrintLn`/`safeErrorPrintLn` bodies.
* English fallbacks live in the Rust source (`tr!` calls in
  `rust2/crates/winux-cmds/src/find*.rs`); extraction discovers them through
  `scripts/i18n_rust.py` (wired into `scripts/i18n_batch.py`).
* validation: `python scripts/i18n_batch.py validate --base catalog/en-US.json
  --translated catalog/zh-CN-merged-validate.json` → `valid` (keys equal,
  placeholders equal, no option-spelling drift).
* The program prefix (`find: `) stays inside a few fallbacks because the
  underlying GNU `error()` frame is part of the probed wording; the rest keep
  the prefix in code (`fail()`, `Run::error`, `"find: warning: {}"`).

## Release note

Every change under `catalogs/` in `unixwin/winuxcmd-i18n` must bump that
repository's `VERSION` file in the same PR (AGENTS.md). The captain sequences
that bump with the catalog drop; this lane did not touch the shared repo.
