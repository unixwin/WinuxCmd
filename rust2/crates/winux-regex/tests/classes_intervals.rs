//! Phase 3: C-locale POSIX class tables + interval expansion.
//!
//! The class membership audit compares the engine's per-byte membership
//! against the GNU grep 3.11 table probed byte-by-byte under LC_ALL=C
//! (target/regex-probe/class_table.sh -> gnu_class_table.txt: one input
//! line per byte 0x01..0xFF, NUL and newline excluded because grep's line
//! framing cannot carry them). POSIX adds newline to cntrl and space; the
//! audit excludes 0x0A for that reason and the engine carries the POSIX
//! value there.

use winux_regex::ast::PosixClass;
use winux_regex::{Regex, Syntax};

/// The GNU-probed C-locale membership sets (hex bytes), from
/// target/regex-probe/gnu_class_table.txt.
const GNU_TABLE: &[(&str, &[u8])] = &[
    ("alnum", b"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"),
    ("alpha", b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"),
    ("blank", b" \t"),
    (
        "cntrl",
        b"\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0b\x0c\x0d\x0e\x0f\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f\x7f",
    ),
    ("digit", b"0123456789"),
    (
        "graph",
        b"!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~",
    ),
    ("lower", b"abcdefghijklmnopqrstuvwxyz"),
    (
        "print",
        b" !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~",
    ),
    (
        "punct",
        b"!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~",
    ),
    ("space", b"\t\n\x0b\x0c\r "),
    ("upper", b"ABCDEFGHIJKLMNOPQRSTUVWXYZ"),
    ("xdigit", b"0123456789ABCDEFabcdef"),
    // [:word:] is NOT a GNU class name (compile error); \w maps to Word.
    ("word", b"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz"),
];

/// Byte-by-byte audit: engine membership == GNU membership for every byte
/// the line-framing probe can carry (0x01..=0xFF minus 0x0A), for all 13
/// classes. 13 x 254 = 3302 comparisons against the GNU oracle.
#[test]
fn posix_class_tables_match_gnu_per_byte() {
    for (name, gnu_bytes) in GNU_TABLE {
        let class = if *name == "word" {
            // Not reachable via [:name:]; audit the variant directly.
            PosixClass::Word
        } else {
            PosixClass::from_name(name.as_bytes())
                .unwrap_or_else(|| panic!("engine must know GNU class name {}", name))
        };
        let gnu_set: std::collections::HashSet<u8> = gnu_bytes.iter().copied().collect();
        for byte in 0x01u8..=0xFF {
            if byte == 0x0A {
                continue; // line framing cannot probe it
            }
            let gnu = gnu_set.contains(&byte);
            let engine = class.matches(byte);
            assert_eq!(
                engine, gnu,
                "class [[:{}:]] byte 0x{:02x}: engine={} GNU={}",
                name, byte, engine, gnu
            );
        }
    }
    // POSIX newline membership (unprobed by framing, fixed by the standard).
    assert!(PosixClass::Cntrl.matches(0x0A));
    assert!(PosixClass::Space.matches(0x0A));
    assert!(!PosixClass::Graph.matches(0x0A));
    // High bytes belong to no C-locale class.
    for byte in 0x80u8..=0xFF {
        assert!(!PosixClass::Alnum.matches(byte));
        assert!(!PosixClass::Graph.matches(byte));
        assert!(!PosixClass::Punct.matches(byte));
    }
}

/// GNU grep rejects `[[:word:]]` as a class name ("Invalid character
/// class name", probed on grep 3.11); `\w` is the word shorthand.
#[test]
fn word_class_name_is_rejected_like_gnu() {
    assert!(Regex::compile(Syntax::Basic, "[[:word:]]").is_err());
    assert!(Regex::compile(Syntax::Extended, "[[:word:]]").is_err());
    let re = Regex::compile(Syntax::Extended, "\\w+").unwrap();
    let m = re.find_at("_a1", 0).unwrap();
    assert_eq!((m.begin, m.end), (0, 3));
}

/// Interval expansion: spans and group behavior at the {m,n} boundaries.
#[test]
fn interval_expansion_boundaries() {
    // Exact count: a{3} spans exactly three.
    let re = Regex::compile(Syntax::Extended, "a{3}").unwrap();
    assert_eq!(
        re.find_at("aaaaa", 0).map(|m| (m.begin, m.end)),
        Some((0, 3))
    );
    // Greedy min-max: a{2,4} takes the longest run (leftmost-longest); a
    // lone "a" between non-a bytes does not match, the trailing "aa" does.
    let re = Regex::compile(Syntax::Extended, "a{2,4}").unwrap();
    assert_eq!(
        re.find_at("aaaaa", 0).map(|m| (m.begin, m.end)),
        Some((0, 4))
    );
    assert_eq!(
        re.find_at("abaa", 0).map(|m| (m.begin, m.end)),
        Some((2, 4))
    );
    // {m,} unbounded greedy.
    let re = Regex::compile(Syntax::Extended, "a{2,}").unwrap();
    assert_eq!(
        re.find_at("baaaa", 0).map(|m| (m.begin, m.end)),
        Some((1, 5))
    );
    // {0,n} optional run: up to two b's, so "abbbc" (three) cannot match.
    let re = Regex::compile(Syntax::Extended, "ab{0,2}c").unwrap();
    assert_eq!(re.find_at("ac", 0).map(|m| (m.begin, m.end)), Some((0, 2)));
    assert_eq!(
        re.find_at("abbc", 0).map(|m| (m.begin, m.end)),
        Some((0, 4))
    );
    assert_eq!(re.find_at("abbbc", 0).map(|m| (m.begin, m.end)), None);
    // {0,0} drops the atom: ab{0}c is "ac".
    let re = Regex::compile(Syntax::Extended, "ab{0}c").unwrap();
    assert_eq!(re.find_at("ac", 0).map(|m| (m.begin, m.end)), Some((0, 2)));
    assert_eq!(re.find_at("abc", 0).map(|m| (m.begin, m.end)), None);
    // {,n} is {0,n} (GNU extension).
    let re = Regex::compile(Syntax::Extended, "ab{,2}c").unwrap();
    assert_eq!(re.find_at("ac", 0).map(|m| (m.begin, m.end)), Some((0, 2)));
    // Missing min under BRE: a\{,3\} is a{0,3} — leftmost is the empty
    // match at 0; anchored at the run it takes the greedy three.
    let re = Regex::compile(Syntax::Basic, "a\\{,3\\}").unwrap();
    assert_eq!(
        re.find_at("baaa", 0).map(|m| (m.begin, m.end)),
        Some((0, 0))
    );
    assert_eq!(
        re.match_at("baaa", 1).map(|m| (m.begin, m.end)),
        Some((1, 4))
    );
    // \{,\} is {0,}.
    let re = Regex::compile(Syntax::Basic, "a\\{,\\}b").unwrap();
    assert_eq!(re.find_at("aab", 0).map(|m| (m.begin, m.end)), Some((0, 3)));
    // Intervals over groups and classes.
    let re = Regex::compile(Syntax::Basic, "\\(ab\\)\\{2\\}").unwrap();
    assert_eq!(
        re.find_at("ababab", 0).map(|m| (m.begin, m.end)),
        Some((0, 4))
    );
    let re = Regex::compile(Syntax::Basic, "[a-c]\\{2\\}").unwrap();
    assert_eq!(
        re.find_at("abcd", 0).map(|m| (m.begin, m.end)),
        Some((0, 2))
    );
    // Interval over a backreference (GNU: \(a\)\1\{2\} over aaa -> aaa).
    let re = Regex::compile(Syntax::Basic, "\\(a\\)\\1\\{2\\}").unwrap();
    assert_eq!(re.find_at("aaa", 0).map(|m| (m.begin, m.end)), Some((0, 3)));
    // Zero-count intervals drop atoms (GNU probe: x\{1\}y\{0\} over "xy"
    // printed just "x"; x\{0\}y is equivalent to bare "y").
    let re = Regex::compile(Syntax::Basic, "x\\{1\\}y\\{0\\}").unwrap();
    assert_eq!(re.find_at("xy", 0).map(|m| (m.begin, m.end)), Some((0, 1)));
    let re = Regex::compile(Syntax::Basic, "x\\{0\\}y").unwrap();
    assert_eq!(re.find_at("xy", 0).map(|m| (m.begin, m.end)), Some((1, 2)));
    // Nested intervals stack: a{1,2}{3} == up to 6.
    let re = Regex::compile(Syntax::Extended, "a{1,2}{3}").unwrap();
    assert_eq!(
        re.find_at("aaaaaa", 0).map(|m| (m.begin, m.end)),
        Some((0, 6))
    );
    assert_eq!(
        re.find_at("aaaaaaa", 0).map(|m| (m.begin, m.end)),
        Some((0, 6))
    );
    // BRE group interval keeps the LAST copy's register (GNU:
    // s/\(a\)\{3\}/<\1>/ over aaa -> <a>).
    let re = Regex::compile(Syntax::Basic, "\\(a\\)\\{3\\}").unwrap();
    let m = re.find_at("aaa", 0).unwrap();
    assert_eq!((m.begin, m.end), (0, 3));
    assert_eq!(m.group(1), Some((2, 3)));
    // Group interval {2} over a two-byte body keeps the last copy.
    let re = Regex::compile(Syntax::Basic, "\\(aa\\)\\{2\\}").unwrap();
    let m = re.find_at("aaaa", 0).unwrap();
    assert_eq!(m.group(1), Some((2, 4)));
}

/// RE_DUP_MAX boundary at the matcher level: 32767 copies compile and
/// match; 32768 is the GNU compile error (covered in the accept matrix).
#[test]
fn interval_dup_max_boundary() {
    let re = Regex::compile(Syntax::Extended, "a{32767}").unwrap();
    let text = format!("{}b", "a".repeat(32767));
    let m = re.find_at_bytes(text.as_bytes(), 0).unwrap();
    assert_eq!((m.begin, m.end), (0, 32767));
    // Far short of the lower bound: each attempt fails in O(short) and the
    // scan stays linear.
    let short = "a".repeat(100);
    assert_eq!(re.find_at_bytes(short.as_bytes(), 0), None);
}

/// Classes composed with anchors and intervals (grep-wave shapes).
#[test]
fn class_interval_composition() {
    // hex-color-ish: [[:xdigit:]]{6} over "color #deAdaB!" spans "deAdaB".
    let re = Regex::compile(Syntax::Extended, "[[:xdigit:]]{6}").unwrap();
    assert_eq!(
        re.find_at("color #deAdaB!", 0).map(|m| (m.begin, m.end)),
        Some((7, 13))
    );
    // word-ish: [[:alpha:]_][[:alnum:]_]* under BRE (no ERE star needed).
    let re = Regex::compile(Syntax::Extended, "[[:alpha:]_][[:alnum:]_]*").unwrap();
    assert_eq!(
        re.find_at("  _a1;  ", 0).map(|m| (m.begin, m.end)),
        Some((2, 5))
    );
    // negated class with -i cross-fold: 'Z' folds into a-z, so the first
    // match is the digit.
    let re = Regex::compile_ignore_case(Syntax::Basic, "[^a-z]").unwrap();
    assert_eq!(
        re.find_at("abZ12", 0).map(|m| (m.begin, m.end)),
        Some((3, 4))
    );
}
