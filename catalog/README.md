# cp lane catalog patch (WP-J)

`winuxcmd-i18n-cp-runtime.patch.json` is the zh-CN translation patch for the
runtime messages introduced by the Rust `cp` port
(`rust2/crates/winux-cmds/src/cp.rs`, `cp/opts.rs`, `cp/copy.rs`).

- It contains **only** the new runtime keys: `command.cp.error.*`,
  `command.cp.prompt.*`, `command.cp.warning.*` (46 keys).
- `command.cp.option.*`, `command.cp.synopsis` and `command.cp.description`
  are unchanged: they come from the C++ `REGISTER_COMMAND` metadata, which
  the port keeps byte-identical (REPLACEMENT-RECIPE.md), and already live in
  the published catalog.
- It is deliberately **not** written into the separate
  `unixwin/winuxcmd-i18n` repository by this lane; the captain owns the
  cross-repository commit and the `VERSION` bump.

## How it was produced / verified

```sh
python scripts/i18n_batch.py extract --output rust2/target/i18n/en-US.json
python catalog/make-cp-patch.py        # filters + writes + validates
# -> prints "valid"
```

`make-cp-patch.py` writes the filtered English base to
`rust2/target/i18n/cp-en-US-runtime.json` (ignored, not committed) and runs
`scripts/i18n_batch.py validate --base <filtered> --translated <patch>`, so
placeholder/token parity is checked against the real extracted text.

## Applying it

1. Merge `messages` into `catalogs/zh-CN/catalog.json` in
   `unixwin/winuxcmd-i18n` (keys are stable; do not reorder existing keys).
2. Bump `VERSION` (at least the patch segment) in the same PR — CI blocks
   catalog changes without a version bump, and WPM treats an unchanged
   version as already installed.
3. Re-run `scripts/i18n_batch.py validate --base <published en-US>
   --translated catalogs/zh-CN/catalog.json` there.

The English fallbacks live in the `tr!` call sites; the catalog is purely a
lookup table. An injected/invalid template always degrades to those exact
English bytes (unit test
`cp::tests::injected_catalog_renders_without_changing_english_fallback`).
