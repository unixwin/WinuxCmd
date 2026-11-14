# wave-W2 wire lane — I18N catalog patch (issue #1488)

Prepared by the wave-W2 wire lane (`rust2/wire-w2`, worktree `D:/repo/wc-w2`).
The captain lands these files in the separate `unixwin/winuxcmd-i18n`
repository; nothing here was written into the shared checkout.

## What is in this directory

| file | meaning |
|---|---|
| `en-US.json` | `python scripts/i18n_batch.py extract` output for this tree (3835 keys) |
| `zh-CN-w2-patch.json` | the **48** new `command.{cut,tr,od,sum}.*` keys with reviewed zh-CN text |
| `w2-new-keys.txt` | flat `key<TAB>english` list of the 48 new keys |
| `w2-delta.json` | added keys + removed keys vs the previous `en-US.json` |
| `zh-CN-w2-merged-validate.json` | the file the validator was run against |

## Numbers

* new stable keys: **48** — `command.cut.error.*` 16, `command.od.error.*`
  19, `command.tr.{error,warn}.*` 11, `command.sum.error.*` 2.
* one existing key keeps its C++ spelling with a corrected fallback:
  `command.tr.error.invalid_repeat_count` (GNU 9.4 curly quotes); the
  cross-spelling annotation lives in `scripts/i18n_rust.py`.
* removed keys attributable to the retired 1.x bodies: **9** `legacy.*`
  digests. The rest of the extraction delta (69 removed / 3 changed, e.g.
  `command.echo.option.repeat`, `command.dd.description`) is pre-existing
  catalog drift from other lanes, not this wire.
* the shared catalog at `VERSION 0.6.42` is still missing keys from other
  in-flight lanes (cp / chmod-family / grep / stat, 145 keys); the merged
  validate file fills those with English and validates `valid`. Only the 48
  wave-W2 keys are this lane's payload.

## Release note

Every change under `catalogs/` in `unixwin/winuxcmd-i18n` must bump that
repository's `VERSION` in the same PR (AGENTS.md). Bump **0.6.42 ->
0.6.43** when landing the 48 keys.
