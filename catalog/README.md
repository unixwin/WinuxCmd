# sed wire lane — I18N catalog patch (issue #1488)

Prepared by the sed wire lane (`rust2/sed-wire`, worktree `D:/repo/wc-sedwire`).
The captain lands these files in the separate `unixwin/winuxcmd-i18n`
repository; nothing here was written into a shared checkout.

## What is in this directory

| file | meaning |
|---|---|
| `en-US.json` | `python scripts/i18n_batch.py extract` output for this tree (3618 keys; 80 `command.sed.*`) |
| `zh-CN-sed-patch.json` | the 61 NEW `command.sed.*` keys with reviewed zh-CN text (patch payload) |
| `sed-orphan-legacy-keys.json` | the 40 `legacy.<fnv>` keys the deleted C++ sed body used to produce (drop from the catalogs) |
| `delta.json` | full added/removed/changed key delta of this tree vs the pre-wiring extraction |
| `sed-new-keys.txt` | flat `key<TAB>repr(english)` list of the 61 new keys |

## Numbers

* new keys: **61** (`command.sed.usage`, `command.sed.bug_reports`, 59 ×
  `command.sed.error.*` — 31 GNU `compile.c errors[]` families plus the
  getopt/usage/runtime diagnostics).
* unchanged keys: the 19 pre-existing `command.sed.synopsis` /
  `command.sed.description` / `command.sed.option.*` keys (the
  `REGISTER_COMMAND` metadata stayed byte-identical).
* orphaned keys: **40** `legacy.*` entries (the deleted C++ body's hardcoded
  diagnostics). All 40 are present in the published zh-CN catalog, so the
  i18n-drift ratchet records +40 `extra`.
* extraction delta: 3658 → 3618 keys (−40 legacy).
* `scripts/i18n_batch.py validate` on the merged catalog: **no
  placeholder or option-token mismatch**; the only reported error is the
  pre-existing key-set skew of this branch vs the published catalog
  (16 keys unrelated to sed: `command.echo.option.repeat`,
  `command.{e,f,}grep.option.fixed-regexp`, `core.error.invalid_trailing_option`,
  11 `legacy.*`).

## Translation notes

* `command.sed.usage` is the whole GNU sed 4.9 usage block; `[OPTION]` and
  the `` `l' `` token stay verbatim because the drift checker pins them.
* `command.sed.error.excess_g_opt` / `excess_p_opt` keep the English
  wording: the checker's backtick-token pairing (`\`g' options to \``) cannot
  be reproduced in a Chinese sentence without leaving the English run in
  place; a reviewed translation that keeps that run is welcome.
* `{reason}` / `{file}` / `{dir}` / `{option}` are host-filled values.
  `reason` is the Windows strerror text and stays untranslated inside the
  sentence until the host supplies a localized reason (same limitation as
  GNU's `strerror` under a non-C locale).
* The `` `s' `` / `` `y' `` tokens and `sed:` program prefix follow the GNU
  source; the prefix is part of the fallback today (GNU interpolates
  `argv[0]`, which is `sed` in the multicall product).

## Landing recipe

1. Merge the 61 messages of `zh-CN-sed-patch.json` into
   `catalogs/zh-CN/catalog.json` (JSON-merge; keep key order sorted).
2. Delete the 40 keys in `sed-orphan-legacy-keys.json` from every locale
   catalog that carries them.
3. Bump `VERSION` (patch segment) and run the i18n repo's own checks.
4. In WinuxCmd, regenerate `scripts/i18n-drift-baseline.json` with
   `python scripts/check_i18n_drift.py --i18n-repo <checkout> --update-baseline`
   **after** rebasing the lane onto current `main` (this branch's base,
   09df368d, predates main's rust2 i18n batch-1, so its drift numbers are
   not the rebased ones).
