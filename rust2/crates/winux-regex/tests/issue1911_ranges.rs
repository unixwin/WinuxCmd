//! WinuxCmd #1911: only the FIRST range in a bracket expression was a
//! range — `is_range` wrongly required `prev_takes_range`, so every range
//! after the first (or after a POSIX class / collating element) degraded
//! to literal members. `[A-Za-z]` lost `a-z`, `[A-Za-z_]+` silently
//! mis-matched. GNU grep 3.12 / sed 4.9 / findutils 4.10 semantics,
//! probed in the #1911 triage dossier (1.2.1 gives the GNU answers;
//! 1.2.2+ regressed).

use winux_regex::{ErrorKind, Regex, Syntax};

fn ere(pattern: &str) -> Regex {
    Regex::compile_bytes(Syntax::Extended, pattern.as_bytes())
        .unwrap_or_else(|e| panic!("{pattern} must compile: {e:?}"))
}

fn bre(pattern: &str) -> Regex {
    Regex::compile_bytes(Syntax::Basic, pattern.as_bytes())
        .unwrap_or_else(|e| panic!("{pattern} must compile: {e:?}"))
}

fn ere_i(pattern: &str) -> Regex {
    Regex::compile_bytes_ignore_case(Syntax::Extended, pattern.as_bytes(), true)
        .unwrap_or_else(|e| panic!("{pattern} must compile: {e:?}"))
}

/// Does `re` match anywhere in the single-byte text `b`?
fn hits(re: &Regex, text: &[u8]) -> bool {
    re.find_at_bytes(text, 0).is_some()
}

#[test]
fn second_range_after_first_range_is_a_range() {
    let re = ere("[A-Za-z]");
    assert!(hits(&re, b"Q"), "first range still works");
    assert!(hits(&re, b"b"), "second range a-z must match b (#1911)");
    assert!(hits(&re, b"m"), "second range a-z must match m (#1911)");
    assert!(!hits(&re, b"5"));
    assert!(!hits(&re, b"-"), "'-' is not a member of [A-Za-z] (#1911)");
}

#[test]
fn ranges_in_either_order() {
    let re = ere("[a-zA-Z]");
    assert!(hits(&re, b"Q"), "second range A-Z must match Q (#1911)");
    assert!(hits(&re, b"b"));
    assert!(!hits(&re, b"-"));
}

#[test]
fn letter_class_plus_extracts_identifier() {
    // The reporter's shape: grep -o '[A-Za-z_]*' style extraction.
    let re = ere("[A-Za-z_]+");
    let m = re
        .find_at_bytes(b"<Log_Foo_Bar>", 0)
        .expect("identifier must match");
    assert_eq!((m.begin, m.end), (1, 12), "whole Foo_Bar, not a prefix");
}

#[test]
fn range_after_posix_class_is_a_range() {
    let re = ere("[[:digit:]a-f]");
    assert!(hits(&re, b"7"), "POSIX class still works");
    assert!(
        hits(&re, b"e"),
        "range after [:digit:] must match e (#1911)"
    );
    assert!(!hits(&re, b"g"));
    assert!(!hits(&re, b"-"));
}

#[test]
fn negated_multi_range() {
    let re = ere("[^A-Za-z]");
    assert!(hits(&re, b"5"));
    assert!(!hits(&re, b"b"), "negated class must exclude a-z (#1911)");
    assert!(!hits(&re, b"Q"));
}

#[test]
fn three_ranges_all_live() {
    let re = ere("[0-9A-Fa-f]");
    assert!(hits(&re, b"3"));
    assert!(hits(&re, b"C"), "second range must match C (#1911)");
    assert!(hits(&re, b"c"), "third range must match c (#1911)");
    assert!(!hits(&re, b"g"));
    assert!(!hits(&re, b"-"));
}

#[test]
fn ignore_case_multi_range() {
    // Triage form: grep -i '[0-9A-F]' — endpoints fold after the raw
    // range check; the second range must still be a range.
    let re = ere_i("[0-9A-F]");
    assert!(hits(&re, b"e"), "-i second range must match e (#1911)");
    assert!(hits(&re, b"E"));
    assert!(hits(&re, b"4"));
    assert!(!hits(&re, b"g"));
}

#[test]
fn bre_multi_range() {
    let re = bre("[A-Za-z]");
    assert!(hits(&re, b"b"), "BRE second range must match b (#1911)");
    assert!(!hits(&re, b"-"));
}

#[test]
fn matches_entire_multi_range() {
    // find -regex face: the whole name must be letters.
    let re = ere("[A-Za-z]*");
    assert!(re.matches_entire_bytes(b"abcXYZ"));
    assert!(!re.matches_entire_bytes(b"abc1"));
}

// --- Regression faces: shapes that must NOT change with the fix. ---

#[test]
fn range_end_after_range_still_errors() {
    let err = Regex::compile_bytes(Syntax::Extended, b"[a-c-e]").unwrap_err();
    assert_eq!(err.kind(), ErrorKind::InvalidRangeEnd);
}

#[test]
fn range_end_after_posix_class_still_errors() {
    let err = Regex::compile_bytes(Syntax::Extended, b"[[:alpha:]-9]").unwrap_err();
    assert_eq!(err.kind(), ErrorKind::InvalidRangeEnd);
}

#[test]
fn range_after_collating_symbol_still_errors() {
    let err = Regex::compile_bytes(Syntax::Extended, b"[[.a.]-z]").unwrap_err();
    assert_eq!(err.kind(), ErrorKind::InvalidRangeEnd);
}

#[test]
fn trailing_dash_stays_literal() {
    let re = ere("[a-c-]");
    assert!(hits(&re, b"b"));
    assert!(hits(&re, b"-"), "dash before ] stays a literal member");
    assert!(!hits(&re, b"d"));
}

#[test]
fn first_position_bracket_is_not_a_range_start() {
    // GNU []-a] is the three members ], -, a — never a range.
    let re = ere("[]-a]");
    assert!(hits(&re, b"]"));
    assert!(hits(&re, b"-"));
    assert!(hits(&re, b"a"));
    assert!(!hits(&re, b"b"));
}
