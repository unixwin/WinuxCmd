# Companion catalog patch — chmod / chgrp / chown `tr!` migration

This directory holds the **patch form** of the companion catalog update for the
A1 preserve-root closure lane. It is intentionally NOT applied to
`D:/repo/winuxcmd-i18n` from this worktree: the catalog repository is shared
with the in-flight I18N lanes (#1660 grep, numfmt, …) and the captain sequences
catalog commits against WinuxCmd PRs.

## Files

| file | purpose |
|---|---|
| `0001-zh-CN-chmod-chgrp-chown-tr.patch` | Unified diff against `unixwin/winuxcmd-i18n` at `VERSION 0.6.35` (catalog HEAD `cd9980d`): adds 34 `catalog.json` keys and bumps `VERSION` to `0.6.36`. |

## What the patch adds

34 new keys (the permission-family `error()` face; program prefixes and
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

## How to apply

```sh
cd D:/repo/winuxcmd-i18n
git apply --check D:/repo/wc-a1wire/catalog/0001-zh-CN-chmod-chgrp-chown-tr.patch
git apply          D:/repo/wc-a1wire/catalog/0001-zh-CN-chmod-chgrp-chown-tr.patch
```

## Verification performed (2026-11-14, this lane)

- Extraction at `origin/main + closure`: **3653** English messages
  (`origin/main` baseline: 3619; delta **+34**, no removals).
- Targeted validation of the 34 changed keys:
  `python scripts/i18n_batch.py validate --base base-34.json --translated trans-34.json`
  → `valid` (placeholder parity per key).
- Full-catalog validation against this worktree's extraction reports ONLY the
  **pre-existing** cross-repo drift that `origin/main` already has:
  15 keys absent from the zh catalog (`command.grep.option.fixed-regexp`,
  `core.error.invalid_trailing_option`, 12 `legacy.*`) and 217 zh-only keys
  staged by the in-flight grep/numfmt lanes. Applying this patch introduces
  **zero** new missing keys and no placeholder errors.
- The patch was dry-run applied to a scratch copy of `winuxcmd-i18n@0.6.35`:
  applies cleanly, catalog grows to 3855 messages, `VERSION` becomes 0.6.36.
