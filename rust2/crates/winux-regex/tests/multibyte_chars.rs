//! WinuxCmd #1907 / #1909 / #1910: multibyte characters are single atoms.
//!
//! The engine used to process pattern and text byte-by-byte outside `.`:
//! bracket classes held byte sets (a multibyte member was split into its
//! UTF-8 bytes and a class consumed ONE byte of a character), case folding
//! was ASCII-only, and a quantifier after a multibyte literal bound to the
//! literal's LAST byte. GNU grep 3.12 / sed 4.9 under a UTF-8 locale treat
//! a character as one unit in all three places; the shapes below are the
//! captain-triaged repros (triage-1907-1911-2026-11-28) at engine level.

use winux_regex::dfa::{LineDfa, LineMode};
use winux_regex::{CompileOptions, Regex, Syntax};

fn span(re: &Regex, text: &[u8]) -> Option<(usize, usize)> {
    re.find_at_bytes(text, 0).map(|m| (m.begin, m.end))
}

// --- #1907: classes evaluate per character, not per byte -------------------

#[test]
fn negated_class_consumes_a_whole_character() {
    // grep -ao '可[^：]*' on 可开=T: GNU matches 可开=T (8 bytes); the byte
    // engine stopped after 可 + the first byte of 开.
    let re = Regex::compile(Syntax::Basic, "可[^：]*").unwrap();
    assert_eq!(span(&re, "可开=T".as_bytes()), Some((0, 8)));
    // grep -c '可[^：]*=' counts the line.
    let re = Regex::compile(Syntax::Basic, "可[^：]*=").unwrap();
    assert_eq!(span(&re, "可开=T".as_bytes()), Some((0, 7)));
}

#[test]
fn class_member_is_one_character_not_its_bytes() {
    // grep -ao '[：]' on ：: GNU reports ONE match of the whole character;
    // the byte engine reported three one-byte matches (ef, bc, 9a).
    let re = Regex::compile(Syntax::Extended, "[：]").unwrap();
    let all = re.find_all_bytes("：".as_bytes());
    assert_eq!(all.len(), 1);
    assert_eq!((all[0].begin, all[0].end), (0, 3));
}

#[test]
fn negated_class_anchors_count_characters() {
    // grep -c '^[^x]$' on 开 is 1 (one character); '^[^x][^x][^x]$' is 0
    // (the byte engine inverted both by counting bytes).
    let one = Regex::compile(Syntax::Basic, "^[^x]$").unwrap();
    assert!(one.matches_entire_bytes("开".as_bytes()));
    let three = Regex::compile(Syntax::Basic, "^[^x][^x][^x]$").unwrap();
    assert!(!three.matches_entire_bytes("开".as_bytes()));
}

#[test]
fn multibyte_range_matches_whole_characters() {
    // sed 's/[一-龥]/X/g' on a中b gives aXb: the range is over characters.
    let re = Regex::compile(Syntax::Extended, "[一-龥]").unwrap();
    assert_eq!(span(&re, "a中b".as_bytes()), Some((1, 4)));
    let all = re.find_all_bytes("a中b".as_bytes());
    assert_eq!(all.len(), 1);
    let anchored = Regex::compile(Syntax::Extended, "^[一-龥]$").unwrap();
    assert!(anchored.matches_entire_bytes("中".as_bytes()));
    assert!(!anchored.matches_entire_bytes("中中".as_bytes()));
}

#[test]
fn sed_dialect_negated_class_consumes_a_whole_character() {
    // sed 's/[^：]*/X/' on 开：x gives X：x (the whole 开 is one unit).
    let opts = CompileOptions::sed(false, false);
    let re = Regex::compile_with(Syntax::Basic, "[^：]*".as_bytes(), opts).unwrap();
    assert_eq!(span(&re, "开：x".as_bytes()), Some((0, 3)));
}

// --- #1909: -i folds non-ASCII letters (Unicode case fold) -----------------

#[test]
fn ignore_case_folds_latin1_letters() {
    let re = Regex::compile_ignore_case(Syntax::Extended, "é").unwrap();
    assert_eq!(span(&re, "É".as_bytes()), Some((0, 2)));
    let re = Regex::compile_ignore_case(Syntax::Extended, "É").unwrap();
    assert_eq!(span(&re, "é".as_bytes()), Some((0, 2)));
    // ... and inside a class.
    let re = Regex::compile_ignore_case(Syntax::Extended, "[é]").unwrap();
    assert_eq!(span(&re, "É".as_bytes()), Some((0, 2)));
}

#[test]
fn ignore_case_folds_cyrillic() {
    // grep -oi 'жук' on ЖУК prints ЖУК.
    let re = Regex::compile_ignore_case(Syntax::Extended, "жук").unwrap();
    assert_eq!(span(&re, "ЖУК".as_bytes()), Some((0, 6)));
}

#[test]
fn ignore_case_folds_final_sigma() {
    // grep -ci 'ς' counts Σ lines: ς, σ and Σ are one fold class.
    for pat in ["ς", "σ", "Σ"] {
        let re = Regex::compile_ignore_case(Syntax::Extended, pat).unwrap();
        for text in ["ς", "σ", "Σ"] {
            assert_eq!(
                span(&re, text.as_bytes()),
                Some((0, 2)),
                "pattern {pat} must match {text} under -i"
            );
        }
    }
}

#[test]
fn sed_i_flag_folds_non_ascii() {
    // sed 's/é/X/I' on aÉb gives aXb.
    let opts = CompileOptions::sed(true, false);
    let re = Regex::compile_with(Syntax::Basic, "é".as_bytes(), opts).unwrap();
    assert_eq!(span(&re, "aÉb".as_bytes()), Some((1, 3)));
}

// --- #1910: a quantifier repeats the whole character -----------------------

#[test]
fn quantifier_repeats_the_whole_character() {
    // grep -oE 'é+' on xééy prints ONE match éé; the byte engine printed
    // two one-character matches (the + bound to the last byte c3 a9's a9).
    let re = Regex::compile(Syntax::Extended, "é+").unwrap();
    assert_eq!(span(&re, "xééy".as_bytes()), Some((1, 5)));
    // grep -c 'é\{2\}' counts xééy.
    let re = Regex::compile(Syntax::Basic, "é\\{2\\}").unwrap();
    assert_eq!(span(&re, "xééy".as_bytes()), Some((1, 5)));
    // sed -E 's/é+/X/' on ééx gives Xx.
    let opts = CompileOptions::sed(false, false);
    let re = Regex::compile_with(Syntax::Extended, "é+".as_bytes(), opts).unwrap();
    assert_eq!(span(&re, "ééx".as_bytes()), Some((0, 4)));
}

#[test]
fn quantifier_does_not_absorb_a_trailing_lone_byte() {
    // printf 'é\xa9\n' | grep -aoE 'é+': GNU prints c3 a9 — the + must not
    // repeat the trailing byte a9 into a bogus second é (c3 a9 a9).
    let re = Regex::compile(Syntax::Extended, "é+").unwrap();
    let text: &[u8] = &[0xC3, 0xA9, 0xA9, b'\n'];
    assert_eq!(span(&re, text), Some((0, 2)));
}

#[test]
fn grouped_multibyte_quantifier_still_works() {
    // (é)+ was the reporter's workaround; it must keep working.
    let re = Regex::compile(Syntax::Extended, "(é)+").unwrap();
    assert_eq!(span(&re, "éé".as_bytes()), Some((0, 4)));
}

#[test]
fn class_quantifier_consumes_whole_characters() {
    // [^x]+ over 开开 consumes both characters (6 bytes), not 6 bytes of
    // split sequences counted as six members.
    let re = Regex::compile(Syntax::Extended, "[^x]+").unwrap();
    assert_eq!(span(&re, "开开".as_bytes()), Some((0, 6)));
    let re = Regex::compile(Syntax::Extended, "[é]+").unwrap();
    assert_eq!(span(&re, "éé".as_bytes()), Some((0, 4)));
}

// --- cross-engine agreement on the triage shapes ---------------------------

/// The line DFA and the Pike NFA must decide exactly what the interpreter
/// decides on every triage shape (their equivalence contracts).
#[test]
fn engines_agree_on_multibyte_shapes() {
    let cases: &[(&str, Syntax, bool)] = &[
        ("可[^：]*", Syntax::Basic, false),
        ("[：]", Syntax::Extended, false),
        ("^[^x]$", Syntax::Basic, false),
        ("[一-龥]", Syntax::Extended, false),
        ("é", Syntax::Extended, true),
        ("жук", Syntax::Extended, true),
        ("ς", Syntax::Extended, true),
        ("[é]", Syntax::Extended, true),
        ("é+", Syntax::Extended, false),
        ("[^x]+", Syntax::Extended, false),
        ("[é]+", Syntax::Extended, false),
    ];
    let lines: &[&[u8]] = &[
        "可开=T".as_bytes(),
        "：".as_bytes(),
        "开".as_bytes(),
        "a中b".as_bytes(),
        "É".as_bytes(),
        "é".as_bytes(),
        "ЖУК".as_bytes(),
        "Σ".as_bytes(),
        "ς".as_bytes(),
        "xééy".as_bytes(),
        "开开".as_bytes(),
        "plain".as_bytes(),
        &[0xC3, 0xA9, 0xA9],
    ];
    for (pat, syntax, ic) in cases {
        let re = Regex::compile_bytes_ignore_case(*syntax, pat.as_bytes(), *ic).unwrap();
        let mut dfa = LineDfa::new(&[&re], LineMode::Contains, b'\n');
        for line in lines {
            let interp = re.find_at_bytes(line, 0).is_some();
            if let Some(d) = dfa.as_mut() {
                assert_eq!(
                    d.is_match_line(line),
                    interp,
                    "DFA/interpreter disagree: pattern {pat} ic={ic} line {line:?}"
                );
            }
            let mut cache = re.new_cache();
            assert_eq!(
                re.is_match_with(&mut cache, line),
                interp,
                "NFA/interpreter disagree: pattern {pat} ic={ic} line {line:?}"
            );
            // Fast path on/off must not change the answer either.
            let mut bare = re.clone();
            bare.disable_fast_path();
            assert_eq!(
                bare.find_at_bytes(line, 0).is_some(),
                interp,
                "fastpath/interpreter disagree: pattern {pat} ic={ic} line {line:?}"
            );
        }
    }
}
