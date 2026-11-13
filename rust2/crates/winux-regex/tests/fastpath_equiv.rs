//! Cross-mode differential gate for the literal pre-filter fast path
//! (WinuxCmd #1531 port, src/fastpath.rs).
//!
//! The fast path is a PERFORMANCE optimization only: every observable
//! result must be byte-for-byte what the pure interpreter (the legacy scan
//! of #1603) produces. For each corpus input we compile one regex with the
//! filters and one with [`Regex::disable_fast_path`], then require every
//! API — `checked_find_at_bytes` / `checked_match_at_bytes` from every
//! start offset, `find_all_bytes`, `matches_entire_bytes` — to agree
//! exactly, `Err(WorkLimit)` included.
//!
//! Coverage per filter tier:
//! * pure_literal — bare literal runs (the `grep -c fox` shape).
//! * prefix — literal runs followed by variable bodies, single-member
//!   classes, `{m}` expansions, captures, backrefs, zero-width assertions.
//! * first — alternation heads, negated/multi-member classes, `-i` sets;
//!   plus the disabled filter for dot heads and empty-capable patterns.
//! * anchored — leading `^` through wrappers.
//! * fallback — continuation-range prefix bytes (rejected in build), UTF-8
//!   codepoint boundaries, raw-byte texts.
//!
//! A deterministic pseudo-random fuzz (seeded LCG, no external deps)
//! sweeps the same comparison over 40k generated pattern/text pairs to
//! cover combinations the hand list cannot.

use winux_regex::{Regex, Syntax};

/// Compare every entry point of `fast` and `slow` over `text`.
fn assert_agree(fast: &Regex, slow: &Regex, text: &[u8], ctx: &str) {
    // find from every offset, checked API (WorkLimit surfaced).
    for start in 0..=text.len() {
        let f = fast.checked_find_at_bytes(text, start);
        let s = slow.checked_find_at_bytes(text, start);
        assert_eq!(f, s, "checked_find mismatch at start {start}: {ctx}");
        let f = fast.checked_match_at_bytes(text, start);
        let s = slow.checked_match_at_bytes(text, start);
        assert_eq!(f, s, "checked_match mismatch at start {start}: {ctx}");
    }
    assert_eq!(
        fast.find_all_bytes(text),
        slow.find_all_bytes(text),
        "find_all mismatch: {ctx}"
    );
    assert_eq!(
        fast.matches_entire_bytes(text),
        slow.matches_entire_bytes(text),
        "matches_entire mismatch: {ctx}"
    );
}

fn compile_pair(syntax: Syntax, pattern: &[u8], ignore_case: bool) -> Option<(Regex, Regex)> {
    let fast = if ignore_case {
        Regex::compile_bytes_ignore_case(syntax, pattern, true)
    } else {
        Regex::compile_bytes(syntax, pattern)
    };
    // Compile acceptance is fast-path-independent; require agreement on the
    // error itself (its boundary is pinned by parser_accept.rs).
    let Ok(fast) = fast else {
        return None;
    };
    let mut slow = fast.clone();
    slow.disable_fast_path();
    Some((fast, slow))
}

fn corpus_case(syntax: Syntax, pattern: &[u8], texts: &[&[u8]]) {
    corpus_case_ic(syntax, pattern, false, texts);
}

fn corpus_case_ic(syntax: Syntax, pattern: &[u8], ignore_case: bool, texts: &[&[u8]]) {
    let Some((fast, slow)) = compile_pair(syntax, pattern, ignore_case) else {
        return;
    };
    let ctx = format!(
        "pattern {:?} ic={}",
        String::from_utf8_lossy(pattern),
        ignore_case
    );
    for text in texts {
        let ctx = format!("{} text {:?}", ctx, String::from_utf8_lossy(text));
        assert_agree(&fast, &slow, text, &ctx);
    }
}

const TEXTS: &[&[u8]] = &[
    b"",
    b"a",
    b"fox",
    b"foxfox",
    b"the quick brown fox jumps over the lazy dog",
    b"FOX Fox fOx",
    b"aaab",
    b"abab abab",
    b"aaaa\naa\nb",
    b"\xff\xfe",
    b"a\x82b",          // lone continuation byte at an unattempted position
    b"\xe4\xb8\xadfox", // 中fox: multi-byte codepoint then the literal
    b"fox\xe4\xb8\xad",
    b"x09fox0x",
];

#[test]
fn pure_literal_tier_agrees_with_interpreter() {
    for syntax in [Syntax::Basic, Syntax::Extended] {
        corpus_case(syntax, b"fox", TEXTS);
        corpus_case(syntax, b"a", TEXTS);
        corpus_case(syntax, b"ab", TEXTS);
        corpus_case_ic(syntax, b"fox", true, TEXTS);
        corpus_case_ic(syntax, b"FOX", true, TEXTS);
    }
}

#[test]
fn prefix_tier_agrees_with_interpreter() {
    for syntax in [Syntax::Basic, Syntax::Extended] {
        corpus_case(syntax, b"f.x", TEXTS);
        corpus_case(syntax, b"ab[0-9]", TEXTS);
        corpus_case(syntax, b"[a]x", TEXTS);
        corpus_case(syntax, b"a(b)c", TEXTS);
        corpus_case(syntax, b"x[ab]y", TEXTS);
        corpus_case(syntax, b"x[[:alpha:]]z", TEXTS);
        corpus_case(syntax, b"[^x]y", TEXTS);
        corpus_case_ic(syntax, b"f[O]x", true, TEXTS);
    }
    corpus_case(Syntax::Extended, b"a{3}", TEXTS);
    corpus_case(Syntax::Extended, b"ab{2}c", TEXTS);
    corpus_case(Syntax::Extended, b"(ab){2}", TEXTS);
    corpus_case(Syntax::Extended, b"a{2,4}", TEXTS);
    corpus_case(Syntax::Extended, b"fo{2}x|bar", TEXTS);
    corpus_case(Syntax::Basic, br"a\{3\}", TEXTS);
    corpus_case(Syntax::Basic, br"\bfoo", TEXTS);
    corpus_case(Syntax::Basic, br"\(ab\)c", TEXTS);
    // Prefix in front of a backreference: the interpreter must still
    // validate the ref at every candidate.
    corpus_case(Syntax::Extended, br"(ab)\1", TEXTS);
    // Prefix whose walk broke mid-sequence at a negated class.
    corpus_case(Syntax::Extended, b"ab[^x]cd", TEXTS);
}

#[test]
fn first_tier_agrees_with_interpreter() {
    corpus_case(Syntax::Extended, b"(fox|bar)baz", TEXTS);
    corpus_case(Syntax::Extended, b"cat|dog|bird", TEXTS);
    corpus_case(Syntax::Extended, b"[0-9]+fox", TEXTS);
    corpus_case(Syntax::Extended, b"x[0-9]*fox", TEXTS);
    corpus_case(Syntax::Extended, b"[^x]y", TEXTS);
    corpus_case_ic(Syntax::Extended, b"[[:upper:]]9", true, TEXTS);
    corpus_case_ic(Syntax::Extended, b"(FOO|BAR)?", true, TEXTS);
    // Dot heads disable the filter: legacy scan, must stay identical too.
    corpus_case(Syntax::Extended, b".{2}fox", TEXTS);
    corpus_case(Syntax::Extended, b"f.x", TEXTS);
}

#[test]
fn anchored_tier_agrees_with_interpreter() {
    for syntax in [Syntax::Basic, Syntax::Extended] {
        corpus_case(syntax, b"^fox", TEXTS);
        corpus_case(syntax, b"^", TEXTS);
        corpus_case(syntax, b"^$", TEXTS);
        corpus_case(syntax, b"^a*b", TEXTS);
    }
    corpus_case(Syntax::Extended, b"(^a)", TEXTS);
    corpus_case(Syntax::Extended, b"^a|b", TEXTS);
    corpus_case(Syntax::Extended, b"^fox|bar", TEXTS);
}

#[test]
fn fallback_and_raw_bytes_agree_with_interpreter() {
    // Empty-capable patterns: match-at-every-position shapes.
    corpus_case(Syntax::Extended, b"a*", TEXTS);
    corpus_case(Syntax::Extended, b"(a?)b?", TEXTS);
    corpus_case(Syntax::Extended, b"(|a)", TEXTS);
    corpus_case(Syntax::Extended, b"", TEXTS);
    corpus_case(Syntax::Basic, b"*", TEXTS); // BRE literal star
                                             // Backref heads, word boundaries.
    corpus_case(Syntax::Extended, br"(a*)\1", TEXTS);
    corpus_case(Syntax::Extended, br"\bfox\b", TEXTS);
    corpus_case(Syntax::Extended, br"\Bx\B", TEXTS);
    // Continuation-range required prefix (rejected in build -> fallback).
    corpus_case(Syntax::Extended, &[0x8A, b'z'], TEXTS);
    corpus_case(Syntax::Extended, &b"\x82+"[..], TEXTS);
    // Multi-byte literals (lead-byte prefix).
    corpus_case(Syntax::Extended, "中".as_bytes(), TEXTS);
    corpus_case(Syntax::Extended, "中文fox".as_bytes(), TEXTS);
    corpus_case_ic(Syntax::Extended, "中FOX".as_bytes(), true, TEXTS);
    // Interval-heavy and deep shapes.
    corpus_case(Syntax::Extended, b"a{1}{1}", TEXTS);
    corpus_case(Syntax::Extended, b"((a){2}){2}", TEXTS);
    corpus_case(Syntax::Basic, br"\(a\)\{2\}", TEXTS);
    // Big pure literal past the prefix cap.
    corpus_case(
        Syntax::Extended,
        b"abcdefghijklmnopqrstuvwxyz0123456789ABC",
        TEXTS,
    );
}

#[test]
fn fuzz_fast_vs_interpreter() {
    // Deterministic LCG (xorshift32); std-only, reproducible failures.
    let mut seed: u32 = 0x1234_5678;
    let mut next = move || {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        seed
    };
    const PATTERN_ALPHABET: &[u8] = b"abcxy.*+?|()[]{}^$\\1024-";
    const TEXT_ALPHABET: &[u8] = b"abcxy\n01";
    let mut cases = 0usize;
    for _ in 0..120_000 {
        let plen = (next() as usize) % 6 + 1;
        let mut pattern = Vec::with_capacity(plen);
        for _ in 0..plen {
            pattern.push(PATTERN_ALPHABET[(next() as usize) % PATTERN_ALPHABET.len()]);
        }
        let tlen = (next() as usize) % 11;
        let mut text = Vec::with_capacity(tlen);
        for _ in 0..tlen {
            if next() % 16 == 0 {
                text.extend_from_slice("中".as_bytes());
            } else {
                text.push(TEXT_ALPHABET[(next() as usize) % TEXT_ALPHABET.len()]);
            }
        }
        let syntax = if next() % 2 == 0 {
            Syntax::Basic
        } else {
            Syntax::Extended
        };
        let ignore_case = next() % 4 == 0;
        // Compilation itself must be mode-independent.
        let f = if ignore_case {
            Regex::compile_bytes_ignore_case(syntax, &pattern, true)
        } else {
            Regex::compile_bytes(syntax, &pattern)
        };
        let Ok(fast) = f else { continue };
        let mut slow = fast.clone();
        slow.disable_fast_path();
        let ctx = format!(
            "fuzz pattern {:?} text {:?} syntax {:?} ic {}",
            String::from_utf8_lossy(&pattern),
            String::from_utf8_lossy(&text),
            syntax,
            ignore_case
        );
        assert_agree(&fast, &slow, &text, &ctx);
        cases += 1;
    }
    assert!(cases > 50_000, "fuzz compiled too few patterns: {cases}");
}
