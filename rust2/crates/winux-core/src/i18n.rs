//! Per-invocation message lookup (i18n foundation).
//!
//! [`text`] picks the host-supplied catalog string for `key` or the
//! English `fallback`; [`format_with_fallback`] (used by the [`tr!`]
//! macro) renders placeholders from explicit bindings and degrades to the
//! already-rendered English fallback on ANY template problem - it never
//! panics.
//!
//! Catalog templates accept two placeholder spellings because they share
//! one catalog with the C++ side (`winux::i18n::format` uses positioned
//! text): plain named `{field}` plus Rust's own automatic `{}` and
//! explicit `{0}` forms (automatic fields consume bindings in declaration
//! order, explicit digits index them). The extractor only permits a
//! positional catalog entry to meet a named Rust fallback for keys listed
//! in its migration annotation - the two spellings must have the same
//! literal shape and field count/order. Numeric precision or width
//! belongs in the argument expression (`value = format!("{:.2}", x)`),
//! never in the catalog text.

use crate::Ctx;

/// Look up `key` on the invocation's host; the owned catalog text wins,
/// otherwise `fallback`. Missing keys AND empty catalog entries both fall
/// back. This is the constant-string path (help blocks and friends): no
/// placeholder rendering happens here.
pub fn text(ctx: &Ctx, key: &str, fallback: &str) -> String {
    match ctx.host.message(key) {
        Some(s) if !s.is_empty() => s.to_string(),
        _ => fallback.to_string(),
    }
}

/// Render the host's `selected` template for a key with the supplied
/// `bindings`, or return `fallback` - already rendered English - when the
/// template is missing, empty, or unusable: unbalanced braces, an unknown
/// or malformed field, a required binding that was not supplied, a
/// supplied binding the template never uses, a format spec (`{x:>8}`), or
/// an out-of-range positional field. Never panics.
///
/// Supported placeholders: plain named `{identifier}`, automatic `{}`
/// (consumes bindings in declaration order) and explicit `{0}`-style
/// indexes, plus `{{`/`}}` escapes. The template must consume every
/// supplied binding at least once (repeats and reordering are fine, zero
/// bindings is vacuously valid). Values are substituted once and never
/// re-scanned, so an argument containing braces or placeholder-looking
/// text stays literal.
#[doc(hidden)]
pub fn format_with_fallback(
    selected: Option<&str>,
    fallback: String,
    bindings: &[(&str, &str)],
) -> String {
    let Some(template) = selected.filter(|s| !s.is_empty()) else {
        return fallback;
    };
    render_template(template, bindings).unwrap_or(fallback)
}

/// `{`-`}` scanner over `template`: literal text is copied verbatim
/// (UTF-8 safe - braces are ASCII and can never appear inside a multibyte
/// sequence), `{{`/`}}` collapse to one brace, and each `{name}`, `{}` or
/// `{0}` is replaced by one binding.
fn render_template(template: &str, bindings: &[(&str, &str)]) -> Option<String> {
    let b = template.as_bytes();
    let mut out = String::with_capacity(template.len());
    let mut used = vec![false; bindings.len()];
    let mut next_auto = 0usize;
    let mut i = 0usize;
    while i < b.len() {
        match b[i] {
            b'{' => {
                if b.get(i + 1) == Some(&b'{') {
                    out.push('{');
                    i += 2;
                    continue;
                }
                let mut j = i + 1;
                while j < b.len() && b[j] != b'}' {
                    j += 1;
                }
                if j >= b.len() {
                    return None; // unbalanced '{'
                }
                let field = &template[i + 1..j];
                let idx = if field.is_empty() {
                    // Automatic field: bindings in declaration order.
                    let idx = next_auto;
                    next_auto += 1;
                    idx
                } else if field.bytes().all(|c| c.is_ascii_digit()) {
                    // Explicit positional index (std::format style).
                    field.parse::<usize>().ok()?
                } else if is_ascii_ident(field) {
                    bindings.iter().position(|(k, _)| *k == field)?
                } else {
                    return None; // format spec or garbage
                };
                if idx >= bindings.len() {
                    return None; // positional field out of range
                }
                used[idx] = true;
                out.push_str(bindings[idx].1);
                i = j + 1;
            }
            b'}' => {
                if b.get(i + 1) == Some(&b'}') {
                    out.push('}');
                    i += 2;
                } else {
                    return None; // lone '}'
                }
            }
            _ => {
                let ch = template[i..].chars().next()?;
                out.push(ch);
                i += ch.len_utf8();
            }
        }
    }
    // Every supplied binding must be consumed by the template: an unused
    // binding means the catalog dropped a required field -> English.
    used.iter().all(|u| *u).then_some(out)
}

/// Plain named placeholder: ASCII identifier, letter/underscore start.
fn is_ascii_ident(s: &str) -> bool {
    let mut bytes = s.bytes();
    match bytes.next() {
        Some(b) if b.is_ascii_alphabetic() || b == b'_' => {}
        _ => return false,
    }
    bytes.all(|b| b.is_ascii_alphanumeric() || b == b'_')
}

/// Look up `key` on `ctx`'s host and render the catalog template (or the
/// English fallback) with named `{field}` bindings.
///
/// ```text
/// tr!(ctx, "stable.key", "English {name}", name = value_expr, ...)
/// ```
///
/// * `key` and `fallback` must be string literals; the fallback is
///   compile-checked by `format!` - every `{field}` in it needs a matching
///   `name =` binding, and `{{`/`}}` escape literal braces.
/// * Bindings are `ident = expr`; each value is `Display`-formatted exactly
///   once, before any binding can shadow a later argument expression
///   (pre-format precision/width inside the expression itself, e.g.
///   `value = format!("{:.2}", x)`).
/// * Catalog templates support only plain `{name}` placeholders - no
///   format specs, positions, or implicit captures; anything else falls
///   back to English.
#[macro_export]
macro_rules! tr {
    ($ctx:expr, $key:literal, $fallback:literal $(, $name:ident = $value:expr)* $(,)?) => {{
        let __selected = ($ctx).host.message($key);
        let ($($name,)*) = ($(format!("{}", $value),)*);
        let __fallback = format!(concat!($fallback), $($name = &$name),*);
        let __bindings = [$( (stringify!($name), $name.as_str()),)*];
        $crate::i18n::format_with_fallback(__selected, __fallback, &__bindings)
    }};
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::host::MapHost;
    use crate::{Ctx, MapEnv};
    use std::cell::Cell;
    use std::path::PathBuf;

    fn ctx_with<'e>(env: &'e MapEnv, host: &'e MapHost) -> Ctx<'e> {
        Ctx::with_host(PathBuf::from("wd"), env, host)
    }

    fn host_with(entries: &[(&str, &str)]) -> MapHost {
        let mut host = MapHost::default();
        for (k, v) in entries {
            host.messages.insert((*k).to_string(), (*v).to_string());
        }
        host
    }

    #[test]
    fn default_ctx_uses_english_fallback() {
        let env = MapEnv::new();
        let ctx = Ctx::new(PathBuf::from("wd"), &env);
        assert_eq!(
            text(&ctx, "no.such.key", "plain fallback"),
            "plain fallback"
        );
        assert_eq!(
            tr!(&ctx, "k", "Hello {name}", name = "world"),
            "Hello world"
        );
        assert_eq!(tr!(&ctx, "k2", "no fields"), "no fields");
    }

    #[test]
    fn missing_and_empty_entries_fall_back() {
        let env = MapEnv::new();
        let host = host_with(&[("empty.key", "")]);
        let ctx = ctx_with(&env, &host);
        assert_eq!(text(&ctx, "missing.key", "fb"), "fb");
        assert_eq!(text(&ctx, "empty.key", "fb"), "fb");
        // text() is the constant path: catalog text returns verbatim,
        // braces and all (no template rendering happens here).
        let host2 = host_with(&[("help.block", "Usage: x {not-a-field}")]);
        let ctx2 = ctx_with(&env, &host2);
        assert_eq!(text(&ctx2, "help.block", "fb"), "Usage: x {not-a-field}");
    }

    #[test]
    fn host_is_per_invocation_not_global() {
        let env = MapEnv::new();
        let h1 = host_with(&[("k", "one")]);
        let h2 = host_with(&[("k", "two")]);
        let c1 = ctx_with(&env, &h1);
        let c2 = ctx_with(&env, &h2);
        assert_eq!(text(&c1, "k", "fb"), "one");
        assert_eq!(text(&c2, "k", "fb"), "two");
        // a default-host context in the same process still misses
        let c0 = Ctx::new(PathBuf::from("wd"), &env);
        assert_eq!(text(&c0, "k", "fb"), "fb");
    }

    #[test]
    fn selected_template_renders_reorder_repeat_and_escapes() {
        let env = MapEnv::new();
        let host = host_with(&[("fmt", "{b}|{a}|{b}|{{x}}")]);
        let ctx = ctx_with(&env, &host);
        // selected differs from the English fallback on purpose
        assert_eq!(tr!(&ctx, "fmt", "{a} {b}", a = "A", b = "B"), "B|A|B|{x}");
    }

    #[test]
    fn argument_values_are_never_rescanned() {
        let env = MapEnv::new();
        let host = host_with(&[("k", "<{v}>")]);
        let ctx = ctx_with(&env, &host);
        // braces and placeholder-looking text inside a VALUE stay literal
        assert_eq!(tr!(&ctx, "k", "[{v}]", v = "{other} {{"), "<{other} {{>");
    }

    #[test]
    fn malformed_selected_falls_back_without_panicking() {
        let fb = "FB A".to_string();
        for template in [
            "",             // empty catalog entry
            "hello {name",  // unbalanced '{'
            "hello } name", // lone '}'
            "hi {missing}", // unknown field
            "hi {a} {b}",   // required binding omitted
            "hi {name:>8}", // format spec
            "hi {1}",       // positional index out of range
            "hi {} {}",     // automatic field beyond the binding count
            "hi {a-b}",     // non-identifier
            "{{ok}} {a",    // escape then unbalanced
        ] {
            assert_eq!(
                format_with_fallback(Some(template), fb.clone(), &[("a", "A")]),
                "FB A",
                "{template}"
            );
        }
        assert_eq!(format_with_fallback(None, fb, &[("a", "A")]), "FB A");
        // one representative case through tr! with a literal key
        let env = MapEnv::new();
        let host = host_with(&[("bad", "hi {name")]);
        let ctx = ctx_with(&env, &host);
        assert_eq!(tr!(&ctx, "bad", "FB {a}", a = "A"), "FB A");
    }

    #[test]
    fn positional_catalog_templates_share_the_cpp_catalog() {
        let env = MapEnv::new();
        // The C++ catalog carries positioned text ("Try '{} --help'"); the
        // Rust call site names the same field. Both render identically.
        let host = host_with(&[
            ("try", "Try '{} --help' for more information."),
            ("reorder", "{1} before {0}"),
            ("explicit", "[{0}] and [{1}]"),
            ("cn", "尝试运行“{} --help”以获取更多信息。"),
        ]);
        let ctx = ctx_with(&env, &host);
        assert_eq!(
            tr!(
                &ctx,
                "try",
                "Try '{prog} --help' for more information.",
                prog = "sort"
            ),
            "Try 'sort --help' for more information."
        );
        assert_eq!(
            tr!(&ctx, "reorder", "{a} before {b}", a = "A", b = "B"),
            "B before A"
        );
        // Explicit indexes map onto the same binding order.
        assert_eq!(
            tr!(&ctx, "explicit", "{a} and {b}", a = "A", b = "B"),
            "[A] and [B]"
        );
        assert_eq!(
            tr!(
                &ctx,
                "cn",
                "Try '{prog} --help' for more information.",
                prog = "sort"
            ),
            "尝试运行“sort --help”以获取更多信息。"
        );
        // Positional text that would leave a supplied binding unused still
        // falls back: a translation may not drop a field.
        assert_eq!(
            format_with_fallback(Some("only {}"), "EN A B".into(), &[("a", "A"), ("b", "B")]),
            "EN A B"
        );
    }

    #[test]
    fn unused_bindings_fall_back() {
        let fb = "FB".to_string();
        // template consumes nothing but a binding was supplied
        assert_eq!(
            format_with_fallback(Some("no fields"), fb.clone(), &[("a", "A")]),
            "FB"
        );
        // one of two supplied bindings is never used
        assert_eq!(
            format_with_fallback(Some("{a}"), fb.clone(), &[("a", "A"), ("b", "B")]),
            "FB"
        );
        // '{{a}}' is an escaped literal, not a binding use
        assert_eq!(
            format_with_fallback(Some("{{a}}"), fb.clone(), &[("a", "A")]),
            "FB"
        );
        // plaintext template with zero bindings stays selected
        assert_eq!(
            format_with_fallback(Some("plain text"), fb.clone(), &[]),
            "plain text"
        );
        // every binding used (repeat + reorder) renders selected
        assert_eq!(
            format_with_fallback(Some("{b} {a} {b}"), fb, &[("a", "A"), ("b", "B")]),
            "B A B"
        );
    }

    #[test]
    fn value_expressions_evaluate_exactly_once() {
        let env = MapEnv::new();
        let host = MapHost::default();
        let ctx = ctx_with(&env, &host);
        let n = Cell::new(0usize);
        let out = tr!(
            &ctx,
            "k",
            "{v}",
            v = {
                n.set(n.get() + 1);
                format!("V{}", n.get())
            }
        );
        assert_eq!(out, "V1");
        assert_eq!(n.get(), 1);
    }

    #[test]
    fn binding_names_do_not_shadow_lookup_or_each_other() {
        let env = MapEnv::new();
        let host = host_with(&[("k", "{a}|{b}")]);
        let ctx = ctx_with(&env, &host);
        let a = "original";
        // `b = a` must see the OUTER a: the tuple RHS evaluates before the
        // let bindings take effect.
        assert_eq!(
            tr!(&ctx, "k", "{a} {b}", a = "replacement", b = a),
            "replacement|original"
        );
        // a binding literally named `ctx` must not hijack host lookup
        let host2 = host_with(&[("k2", "catalog:{ctx}")]);
        let ctx2 = ctx_with(&env, &host2);
        assert_eq!(tr!(&ctx2, "k2", "fb:{ctx}", ctx = "BOUND"), "catalog:BOUND");
    }
}
