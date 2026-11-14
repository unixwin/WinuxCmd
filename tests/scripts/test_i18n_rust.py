"""Regression fixtures for the source-to-catalog boundary (not CLI output)."""

from pathlib import Path
import tempfile
import unittest

from scripts.i18n_rust import (
    POSITIONAL_TO_NAMED,
    extract_rust_messages,
    merge_rust_messages,
)


class RustExtractionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="winux-i18n-extract-")
        self.addCleanup(self.temporary.cleanup)
        self.crates = Path(self.temporary.name) / "crates"
        self.crate = self.crates / "sample"
        self.source = self.crate / "src"
        self.source.mkdir(parents=True)
        (self.crate / "Cargo.toml").write_text('[package]\nname = "sample"\n', encoding="utf-8")

    def write(self, name, source):
        path = self.source / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source, encoding="utf-8")

    def extract(self, source):
        self.write("lib.rs", source)
        return extract_rust_messages(self.crates)

    def test_named_macro_and_rust_escapes(self):
        self.assertEqual(
            self.extract(r'''
fn run(ctx: &Ctx) {
    winux_core::tr!(ctx, "command.sample.error", "line\n{name}\t\x41\u{4e2d}",
                    name = format!("{}", arg));
}
'''),
            {"command.sample.error": "line\n{name}\tA\u4e2d"},
        )

    def test_raw_help_and_concat_constant(self):
        self.assertEqual(
            self.extract('''
const HELP: &str = concat!(r##"Usage: braces {x}, quotes "# remain
"##, "done\\n");
fn run(ctx: &Ctx) { winux_core::i18n::text(ctx, "command.sample.help", HELP); }
'''),
            {"command.sample.help": 'Usage: braces {x}, quotes "# remain\ndone\n'},
        )

    def test_comments_strings_and_characters_are_not_calls(self):
        self.assertEqual(
            self.extract(r'''
// tr!(ctx, "not.real", "ignored");
/* nested /* tr!(ctx, "not.real", "ignored"); */ comment */
const DATA: &str = r#"tr!(ctx, "not.real", "ignored")"#;
const CH: char = '{';
const BYTES: &[u8] = b"\xff tr!(ignored)";
fn run(ctx: &Ctx<'_>) { tr!(ctx, "real.message", "real"); }
'''),
            {"real.message": "real"},
        )

    def test_test_modules_are_not_visited_but_later_runtime_is(self):
        self.write("only_tests.rs", 'fn f() { tr!(ctx, "fake.test", "fake"); }')
        self.assertEqual(
            self.extract('''
#[cfg(test)] mod only_tests;
#[cfg(all(windows, test))]
mod inline_tests { fn f() { tr!(ctx, "fake.inline", "fake"); } }
#[test] fn lone_test() { tr!(ctx, "fake.lone", "fake"); }
#[cfg(not(test))]
fn production(ctx: &Ctx) { tr!(ctx, "real.message", "real"); }
'''),
            {"real.message": "real"},
        )

    def test_out_of_line_and_inline_module_scope(self):
        self.write("child.rs", '''
const HELP: &str = "child";
fn run(ctx: &Ctx) { i18n::text(ctx, "child.help", HELP); }
''')
        self.assertEqual(
            self.extract('''
const HELP: &str = "parent";
mod child;
mod nested {
    const HELP: &str = "nested";
    fn run(ctx: &Ctx) { i18n::text(ctx, "nested.help", HELP); }
}
fn run(ctx: &Ctx) { i18n::text(ctx, "parent.help", HELP); }
'''),
            {"child.help": "child", "nested.help": "nested", "parent.help": "parent"},
        )

    def test_same_key_same_fallback_is_allowed(self):
        self.assertEqual(
            self.extract('fn f(ctx: &Ctx) { tr!(ctx, "same.key", "same"); tr!(ctx, "same.key", "same"); }'),
            {"same.key": "same"},
        )

    def test_inner_test_cfg_disables_entire_scope(self):
        self.assertEqual(self.extract(
            '#![cfg(test)] fn f(ctx: &Ctx) { tr!(ctx, "fake.test", "fake"); }'
        ), {})

    def test_invalid_calls_fail_closed(self):
        sources = [
            'fn f(ctx: &Ctx) { tr!(ctx, dynamic, "text"); }',
            'fn f(ctx: &Ctx) { tr!(ctx, "oneword", "text"); }',
            'fn f(ctx: &Ctx) { i18n::text(ctx, "valid.key", unknown); }',
            'fn f(ctx: &Ctx) { tr!(ctx, "valid.key", "{path:>8}", path = p); }',
            'fn f(ctx: &Ctx) { tr!(ctx, "valid.key", "{path}"); }',
            'fn f(ctx: &Ctx) { tr!(ctx, "valid.key", "text", path = p); }',
            'fn f(ctx: &Ctx) { tr!(ctx, "valid.key", "{path}", path = p, path = q); }',
            'fn f(ctx: &Ctx) { tr!(ctx, "valid.key", "broken {"); }',
            'fn f(ctx: &Ctx) { tr!(ctx, "same.key", "one"); tr!(ctx, "same.key", "two"); }',
            'const HELP: &str = HELP; fn f(ctx: &Ctx) { i18n::text(ctx, "valid.key", HELP); }',
            '#[path="other.rs"] mod other;',
            'include!("generated.rs");',
            'mod missing;',
            'macro_rules! wrapper { () => { i18n::text(ctx, "hidden.key", "hidden") }; }',
            'macro_rules! wrapper { () => { tr!(ctx, "hidden.key", "hidden") }; }',
        ]
        for source in sources:
            with self.subTest(source=source), self.assertRaises(ValueError):
                self.extract(source)

    def test_duplicate_conditional_fallbacks_cannot_silently_choose_one(self):
        with self.assertRaisesRegex(ValueError, "ambiguous"):
            self.extract('''
#[cfg(windows)] const HELP: &str = "one";
#[cfg(unix)] const HELP: &str = "two";
fn run(ctx: &Ctx) { i18n::text(ctx, "valid.key", HELP); }
''')

    def test_escaped_literal_braces_are_not_bindings(self):
        self.assertEqual(
            self.extract('fn f(ctx: &Ctx) { tr!(ctx, "valid.key", "{{literal}} {path}", path=p); }'),
            {"valid.key": "{{literal}} {path}"},
        )

    def test_annotated_positional_and_named_spellings_merge(self):
        # The C++ catalog entry stays positioned (std::format), the Rust
        # fallback names the same field; the annotation is the permission.
        messages = {
            "common.try_help": "Try '{} --help' for more information.",
            "common.error.extra_operand": "extra operand '{}'",
        }
        merge_rust_messages(messages, {
            "common.try_help": "Try '{prog} --help' for more information.",
            "common.error.extra_operand": "extra operand '{operand}'",
        })
        self.assertEqual(messages["common.try_help"],
                         "Try '{} --help' for more information.")
        self.assertEqual(messages["common.error.extra_operand"],
                         "extra operand '{}'")
        self.assertEqual(POSITIONAL_TO_NAMED["common.try_help"], ("prog",))

    def test_unannotated_fallback_conflicts_still_fail(self):
        with self.assertRaisesRegex(ValueError, "conflicting"):
            merge_rust_messages(
                {"command.sample.error": "one '{}'"},
                {"command.sample.error": "one '{thing}'"},
            )

    def test_annotated_spelling_must_match_shape_and_order(self):
        messages = {"common.try_help": "Try '{} --help' for more information."}
        for named in (
            "Try '{wrong} --help' for more information.",  # wrong field name
            "Try '{prog} --help'!",                        # different literal
            "Try '{prog}' for more information.",          # different shape
        ):
            with self.subTest(named=named), self.assertRaisesRegex(
                    ValueError, "conflicting"):
                merge_rust_messages(dict(messages), {"common.try_help": named})

    def test_custom_cargo_target_is_rejected(self):
        (self.crate / "Cargo.toml").write_text(
            '[package]\nname="sample"\n[lib] # a valid TOML header comment\npath="custom.rs"\n',
            encoding="utf-8"
        )
        with self.assertRaisesRegex(ValueError, "custom target"):
            self.extract("fn f() {}")


if __name__ == "__main__":
    unittest.main()
