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

---

## A1 lane — chmod / chgrp / chown `tr!` catalog patch

`0001-zh-CN-chmod-chgrp-chown-tr.patch` is the **patch form** of the companion
catalog update for the A1 preserve-root closure lane. It is intentionally NOT
applied to `D:/repo/winuxcmd-i18n` from this worktree: the catalog repository
is shared with the in-flight I18N lanes and the captain sequences catalog
commits against WinuxCmd PRs.

The patch is a unified diff against `unixwin/winuxcmd-i18n` at `VERSION
0.6.35` (catalog HEAD `cd9980d`): it adds 34 `catalog.json` keys and bumps
`VERSION` to `0.6.36`.

The 34 keys (the permission-family `error()` face; program prefixes and
`errno` suffixes stay in Rust code):

- `command.chmod.error.*` — 10 keys
- `command.chgrp.error.*` — 10 keys
- `command.chown.error.*` — 14 keys

The three `dangerous_recursive_root` keys carry GNU's `(same as '/')` suffix
inside the sentence instead of a `{tail}` binding: the suffix cannot be
translated independently of the clause, so the two shapes are distinct keys.
`{operand}`, `{file}`, `{group}`, `{mode}`, `{spec}` are named placeholders;
CJK text contains no literal braces, so no `{{`/`}}` escaping is required.

Catalog size: 3821 → 3855 messages. `VERSION` **must** be bumped in the same
commit (`catalogs/` change gate), 0.6.35 → 0.6.36.

---

## wave-W4 lane — mkfifo / mknod runtime message face

Prepared by the wave-W4 wiring lane (`rust2/wire-w4`, worktree
`D:/repo/wc-w4`). The captain lands the `catalogs/` part in
`unixwin/winuxcmd-i18n`; nothing here is applied to the shared catalog
checkout.

| file | meaning |
|---|---|
| `w4-mkfifo-mknod-en-additions.json` | the **15** new stable keys this wire introduces (english fallbacks as extracted) |
| `w4-mkfifo-mknod-zh-patch.json` | the **15** reviewed zh-CN translations (patch payload) |
| `w4-mkfifo-mknod-orphan-legacy.json` | the **12** `legacy.<fnv>` keys produced only by the deleted `src/commands/mkfifo.cpp` / `mknod.cpp` bodies — drop them from the locale catalogs |
| `w4-mkfifo-mknod-validate.txt` | `python scripts/i18n_batch.py validate` output (`valid`) |

Numbers:

* added: `command.mkfifo.{error.*,warning.*}` = 4 keys,
  `command.mknod.{error.*,warning.*}` = 11 keys (15 total).
* removed: 12 orphan legacy keys (the two deleted C++ bodies' `safePrintLn`
  / `std::unexpected` literals). Keys such as `legacy.7dfe466a8c65ddde`
  ("missing operand") survive because other C++ applets still emit them.
* unchanged: every `command.mkfifo.*` / `command.mknod.*`
  description/option/synopsis key (the `REGISTER_COMMAND` metadata is
  byte-identical), and no other command's key set moved in this branch.
* validation: base = the 15-key English subset, translated = the zh patch;
  `valid` (key sets equal, placeholder sets equal, no option-token drift).
  The `{}`-positioned shared keys (`common.error.*`) are not touched.
* The program prefix (`mkfifo: ` / `mknod: `) stays in Rust code; the GNU
  `errno` reason text (`File exists`, `No such file or directory`) is a
  `{reason}` binding, exactly like the find/grep `{reason}` convention —
  platform strerror text is data, not catalog text.

Release note: `VERSION` in `unixwin/winuxcmd-i18n` must be bumped in the
same PR as any `catalogs/` change (AGENTS.md); this lane did not touch
that repository.
