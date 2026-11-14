# catalog/ — locale patch artifacts for this branch (not committed to the i18n repo here)

Locale catalogs are published from the separate `unixwin/winuxcmd-i18n`
repository (see `AGENTS.md` "Catalog ownership"). This branch ships the
source-side `tr!` keys and keeps the companion Chinese catalog change as a
**patch artifact** under `catalog/`, so the captain can sequence the i18n
commit with the source PR (the same pattern as the `i18n(grep)` companion
commit cd9980d, which was held until its source PR merged).

## Files

| file | purpose |
|---|---|
| `wave-w1-zh-CN.json` | WP-I W1 patch: `set` (55 new/re-worded `command.<cmd>.error.*` keys, named placeholders) and `remove` (24 dead `legacy.<fnv>` reverse-lookup entries whose C++ bodies this wave deleted). |

## Applying (in `D:/repo/winuxcmd-i18n`, separate commit, not pushed by agents)

```python
import json
patch = json.load(open("catalog/wave-w1-zh-CN.json", encoding="utf-8"))
path = "catalogs/zh-CN/catalog.json"
cat = json.load(open(path, encoding="utf-8"))
cat["messages"].update(patch["set"])
for key in patch["remove"]:
    cat["messages"].pop(key, None)
cat["messages"] = dict(sorted(cat["messages"].items()))
open(path, "w", encoding="utf-8").write(
    json.dumps(cat, ensure_ascii=False, indent=2) + "\n")
```

Then:

1. **Bump `VERSION`** (`0.6.35` -> `0.6.36`, at least the patch segment).
   Every change under `catalogs/` requires it; CI blocks the PR otherwise,
   and WPM treats an unchanged version as already installed.
2. Validate against the freshly extracted English catalog:
   `python scripts/i18n_batch.py validate --base <extract.json> --translated catalogs/zh-CN/catalog.json`
   from the WinuxCmd checkout (the extract for this wave has 3645 messages).
3. Only then push, and re-run `scripts/check_i18n_drift.py` on the WinuxCmd
   side.

## English source of truth

`python scripts/i18n_batch.py extract --output <tmp>.json` scans
`src/commands/*.cpp` (registration metadata), `src/**` (legacy literal
fallbacks) and `rust2/crates` (`tr!` fallbacks, edge-followed from the crate
roots). Before this wave: 3619 messages; after: 3645.
