//! Phase 1: syntax accept/reject matrix against GNU grep 3.11
//! (WSL, LC_ALL=C, glibc regcomp). Every row below was probed with
//! `target/regex-probe/*.sh` before being encoded; rc 2 = rejected.
//!
//! Note on GNU grep vs glibc: grep renders regcomp errors to stderr with
//! its own message text; the accept/reject BOUNDARY is what this matrix
//! pins. Message strings are the engine's own.

use winux_regex::{ErrorKind, Regex, Syntax};

/// Compile `pattern`; `None` on error.
fn try_compile(syntax: Syntax, pattern: &str) -> Option<usize> {
    Regex::compile_bytes(syntax, pattern.as_bytes())
        .ok()
        .map(|re| re.capture_count())
}

fn bre(pattern: &str) -> Option<usize> {
    try_compile(Syntax::Basic, pattern)
}

fn ere(pattern: &str) -> Option<usize> {
    try_compile(Syntax::Extended, pattern)
}

/// BRE rows: (pattern, compiles, GNU probe note).
#[test]
fn bre_accept_reject_matrix() {
    let rows: &[(&str, bool, &str)] = &[
        // literals and anchors
        ("abc", true, ""),
        ("a|b", true, ""),   // `|` is a literal in BRE
        ("a\\|b", true, ""), // GNU alternation
        ("a\\|b\\|c", true, ""),
        ("^abc", true, ""),
        ("abc$", true, ""),
        ("a^b", true, ""), // mid-pattern ^ is a LITERAL in BRE (POSIX)
        ("a$b", true, ""), // mid-pattern $ is a LITERAL in BRE
        ("a$b$c", true, ""),
        ("\\(^a\\)", true, ""), // ^ anchor after \\(
        ("a\\|^b", true, ""),   // ^ anchor after \\|
        ("a\\|b^", true, ""),   // ^ literal elsewhere
        ("\\(a$\\)", true, ""), // $ anchor before \\)
        ("a$\\|b", true, ""),   // $ anchor before \\|
        ("a^*b", true, ""),     // ^ literal quantified by *
        ("^*", true, ""),       // anchor quantified by * (GNU accepts)
        ("^*a", true, ""),      // * after initial ^ is LITERAL (GNU-verified)
        ("*a", true, ""),       // * at pattern start is LITERAL (POSIX)
        ("**x", true, ""),      // literal-* then closure (GNU-verified)
        // quantifiers
        ("a\\{2,3\\}", true, ""),
        ("a\\{2\\}", true, ""),
        ("a\\{2,\\}", true, ""),
        ("a\\{,3\\}", true, ""), // GNU: missing min = 0
        ("a\\{,\\}", true, ""),  // GNU: {0,}
        ("a\\{0,0\\}", true, ""),
        ("a\\{32767\\}", true, ""), // RE_DUP_MAX boundary
        ("a\\{32768\\}", false, "too big"),
        ("a\\{99999999999\\}", false, "too big"),
        ("a\\{2,1\\}", false, "invalid content"),
        ("a\\{a\\}", false, "invalid content"),
        ("a\\{2,a\\}", false, "invalid content"),
        ("a\\{2", false, "unmatched \\{"),
        ("a\\{2,3", false, "unmatched \\{"),
        ("a\\{ }", false, "unmatched \\{"),
        ("a\\{}", false, "unmatched \\{"),
        ("\\{2\\}", true, ""),   // leading \\{ is LITERAL braces in BRE
        ("\\{2,1\\}", true, ""), // leading \\{ literal, no interval applied
        ("a\\{2,3", false, "unmatched \\{"),
        ("a\\+\\?", true, ""), // GNU \\+ \\?
        ("a\\+\\+", true, ""), // quantifier stacking (GNU-verified)
        ("x\\{1\\}y\\{0\\}", true, ""),
        ("\\(a\\)\\{0\\}b", true, ""),
        ("\\(a\\)\\1\\{2\\}", true, ""),
        ("\\(a\\)\\{3\\}", true, ""),
        ("\\(aa\\)\\{2\\}", true, ""),
        ("\\(ab\\)\\{1,2\\}", true, ""),
        ("[a-c]\\{2\\}", true, ""),
        ("[0-9]\\{3\\}", true, ""),
        ("\\(a\\)\\{2,3\\}", true, ""),
        ("\\(a*\\)\\{2,\\}", true, ""),
        // groups, alternation, backrefs
        ("\\(abc\\)\\1", true, ""),
        ("\\(\\(a\\)\\(b\\)\\)\\1\\2", true, ""),
        ("\\(\\(a\\)\\)\\2", true, ""),
        ("\\(\\(a\\)\\)\\3", false, "invalid back reference"),
        ("\\1", false, "invalid back reference"),
        ("\\(a\\)\\10", true, ""), // \\1 backref + literal 0 (GNU-verified)
        ("\\10", false, "invalid back reference"), // \\1 with no group
        ("\\0", true, ""),         // literal zero digit
        ("\\01", true, ""),
        ("\\(\\)\\1", true, ""), // empty group + empty backref
        ("\\(a\\|b\\)+", true, ""),
        ("\\(a\\|\\)", true, ""),
        ("\\(x\\|y\\)", true, ""),
        ("\\(", false, "unmatched ("),
        ("\\)", false, "unmatched )"),
        ("a\\)", false, "unmatched )"),
        ("\\(a", false, "unmatched ("),
        ("\\(a\\|b", false, "unmatched ("),
        ("a\\(|b", false, "unmatched ("),
        ("(\\(a\\))", true, ""), // parens are literals in BRE
        ("()", true, ""),
        // escapes and word boundaries
        ("\\bx", true, ""),
        ("\\babc", true, ""),
        ("ab\\B", true, ""),
        ("\\<x", true, ""),
        ("x\\>", true, ""),
        ("\\wx", true, ""),
        ("\\sa", true, ""),
        ("\\W\\S", true, ""),
        ("\\.", true, ""),
        ("\\*", true, ""),
        ("\\\\", true, ""),
        ("\\}", true, ""),     // literal }
        ("\\Qa\\E", true, ""), // no PCRE syntax: literal chars
        ("a\\", false, "trailing backslash"),
        // bracket expressions
        ("[a]", true, ""),
        ("[]a]", true, ""),  // first ] literal
        ("[]-a]", true, ""), // GNU: members ],-,a (no range from first ])
        ("[a-]", true, ""),
        ("[-a]", true, ""),
        ("[--a]", true, ""),   // GNU: range from `-` to `a`
        ("[a\\-z]", true, ""), // GNU: backslash member + range \\-z
        ("[\\a-z]", true, ""), // GNU: backslash member + range a-z
        ("[\\-z]", true, ""),  // GNU: range \\-z (0x5C..0x7A)
        ("[a\\]]", true, ""),  // GNU: class {a,\\} + literal ]
        ("[a-z\\]]", true, ""),
        ("[\\\\]", true, ""), // two backslash members
        ("[\\]]", true, ""),  // GNU: class {\\} + literal ]
        ("[\\z]]", true, ""),
        ("[\\^]", true, ""),
        ("[\\[]", true, ""),
        ("[^]]", true, ""),   // POSIX: first ] literal even negated
        ("[^\\]]", true, ""), // GNU: negated {\\} + literal ]
        ("[\\]", true, ""),   // GNU: class {\\}; the ] closes (compiles)
        ("[[:alpha:]]", true, ""),
        ("[[:alpha:][:digit:]]", true, ""),
        ("[[:space:]]", true, ""),
        ("[[:alpha:]\\\\/]", true, ""),
        ("[[.a.]]", true, ""), // collating symbol, single byte
        ("[[=a=]]", true, ""), // equivalence class, single byte
        ("[[abc]]", true, ""), // [ is an ordinary member here
        ("[[:foo:]]", false, "invalid character class name"),
        ("[[:alpha]", false, "unmatched ["), // committed [: without :]
        ("[[.ab.]]", false, "invalid collation"),
        ("[[=b=]]", true, ""),
        ("[[.]-z]", false, "unmatched ["),
        ("[a\\-]", true, ""),
        ("[0-9\\-]", true, ""),
        ("[a-c-e]", false, "invalid range end"),
        ("[a-c-e-]", false, "invalid range end"),
        ("[a-b-c]", false, "invalid range end"),
        ("[\\a-c-e]", false, "invalid range end"),
        ("[[:alpha:]-9]", false, "invalid range end"),
        ("[a-c[:digit:]-9]", false, "invalid range end"),
        ("[[:alpha:]-]", true, ""), // trailing `-` is a literal member
        ("[a-c\\]-z]", true, ""),   // GNU: `]` closes after `\\`; tail literal
        ("[z-a]", false, "invalid range end"),
        ("[]", false, "unmatched ["),
        ("[^]", false, "unmatched ["),
        ("[a-", false, "unmatched ["),
        ("[^^]", true, ""),
        ("[.]", true, ""), // plain member '.', not collating syntax
        // dot and escapes-as-literals
        (".", true, ""),
        ("[^a-z]", true, ""),
        (".*", true, ""),
        // ERE metacharacters are literals under BRE
        ("(a)", true, ""),
        ("{2}", true, ""),
        ("a{2,3}", true, ""),
        ("(x|y)", true, ""),
    ];
    for (pattern, compiles, note) in rows {
        let compiled = bre(pattern);
        assert_eq!(
            compiled.is_some(),
            *compiles,
            "BRE pattern {:?} ({}): expected compiles={}, got {:?}",
            pattern,
            note,
            compiles,
            compiled
        );
    }
}

/// ERE rows.
#[test]
fn ere_accept_reject_matrix() {
    let rows: &[(&str, bool, &str)] = &[
        // alternation, groups
        ("a|b", true, ""),
        ("a|b|c", true, ""),
        ("a|", true, ""), // trailing empty branch
        ("|a", true, ""),
        ("ab|", true, ""),
        ("(|a)", true, ""),
        ("(|", false, "unmatched ("),
        ("(a|", false, "unmatched ("),
        ("(x|y)", true, ""),
        ("(a|b)+", true, ""),
        ("(a)(b)", true, ""),
        ("()", true, ""),
        ("x(b$)y", true, ""), // ERE anchors are context-independent
        ("a|^b", true, ""),
        ("(^a)", true, ""),
        ("(a$)", true, ""),
        ("a^b", true, ""), // ^ anchor mid-pattern: accepted, never matches
        ("a$b", true, ""),
        ("x*^", true, ""),
        ("a)b", true, ""), // glibc: stray top-level ) is a literal
        (")", true, ""),
        ("(", false, "unmatched ("),
        ("(x", false, "unmatched ("),
        ("ab)", true, ""),
        ("(x(y)z)", true, ""),
        ("((a)|(b))*", true, ""),
        ("((a)|(b))+", true, ""),
        ("(a)(b)(c)(d)(e)(f)(g)(h)(i)\\9", true, ""),
        // quantifiers and intervals
        ("a+", true, ""),
        ("a?", true, ""),
        ("a*?", true, ""), // quantifier stacking, not lazy
        ("a?+", true, ""),
        ("a**", true, ""),
        ("a{2,3}", true, ""),
        ("a{2}", true, ""),
        ("a{2,}", true, ""),
        ("a{,3}", true, ""), // missing min = 0
        ("a{,}", true, ""),  // {0,}
        ("a{32767}", true, ""),
        ("a{32768}", false, "too big"),
        ("a{99999999999}", false, "too big"),
        ("a{2,1}", false, "invalid content"), // reversed after an atom: ERROR
        ("{2}", true, ""),                    // leading: repeats the empty expression (glibc warns)
        ("{2,3}", true, ""),
        ("{2,1}", true, ""), // leading reversed: LITERAL braces
        ("{a}", true, ""),   // malformed: literal braces
        ("{99999999999}", false, "too big"),
        ("x{2,3", true, ""), // unclosed: literal braces (GNU-verified)
        ("x{a}", true, ""),
        ("x{ }", true, ""),
        ("x{2,a}", true, ""),
        ("x{}", true, ""),
        ("x{2}{3}", true, ""), // stacking
        ("a{1,2}{3}", true, ""),
        ("*a", true, ""), // leading quantifiers repeat the empty expr
        ("+a", true, ""),
        ("?a", true, ""),
        ("(*a)", true, ""),
        ("a|*b", true, ""),
        ("^*a", true, ""),
        ("[a-z]{2}", true, ""),
        // backrefs (GNU ERE extension)
        ("(ab)\\1", true, ""),
        ("(a)\\1", true, ""),
        ("\\1", false, "invalid back reference"),
        ("((abc))\\1*", true, ""),
        // anchors, boundaries, escapes
        ("\\bx", true, ""),
        ("\\sa", true, ""),
        ("\\w+", true, ""),
        ("\\\\", true, ""),
        ("\\.", true, ""),
        ("\\*", true, ""),
        ("a\\", false, "trailing backslash"),
        ("\\{2\\}", true, ""), // escaped braces are literals in ERE
        // brackets behave identically in ERE
        ("[]a]", true, ""),
        ("[a-z\\]]", true, ""),
        ("[^]]", true, ""),
        ("[[:alpha:]]", true, ""),
        ("[[:foo:]]", false, "invalid character class name"),
        ("[a\\-z]", true, ""),
        ("[a-c-e]", false, "invalid range end"),
        ("[", false, "unmatched ["),
        ("]", true, ""), // lone ] is a literal in ERE
        (".", true, ""),
        ("[^a-z]", true, ""),
        // BRE syntax under ERE: \\( is a literal paren
        ("\\(a\\)", true, ""),
        ("a\\{2,3\\}", true, ""), // glibc ERE: literal braces
        ("a\\+", true, ""),
        ("a\\?", true, ""),
    ];
    for (pattern, compiles, note) in rows {
        let compiled = ere(pattern);
        assert_eq!(
            compiled.is_some(),
            *compiles,
            "ERE pattern {:?} ({}): expected compiles={}, got {:?}",
            pattern,
            note,
            compiles,
            compiled
        );
    }
}

/// Specific error kinds for the rejection families.
#[test]
fn error_kinds_are_typed() {
    let kinds: &[(&str, Syntax, ErrorKind)] = &[
        ("\\1", Syntax::Basic, ErrorKind::InvalidBackReference),
        ("\\1", Syntax::Extended, ErrorKind::InvalidBackReference),
        ("a\\", Syntax::Basic, ErrorKind::TrailingBackslash),
        ("a\\", Syntax::Extended, ErrorKind::TrailingBackslash),
        ("a\\{2", Syntax::Basic, ErrorKind::InvalidInterval),
        ("a\\{2,1\\}", Syntax::Basic, ErrorKind::InvalidInterval),
        ("a{2,1}", Syntax::Extended, ErrorKind::InvalidInterval),
        ("a\\{32768\\}", Syntax::Basic, ErrorKind::IntervalTooBig),
        ("a{32768}", Syntax::Extended, ErrorKind::IntervalTooBig),
        ("[z-a]", Syntax::Basic, ErrorKind::InvalidRangeEnd),
        ("[[:foo:]]", Syntax::Basic, ErrorKind::InvalidClassName),
        ("[[.ab.]]", Syntax::Basic, ErrorKind::InvalidCollation),
        ("[]", Syntax::Basic, ErrorKind::UnmatchedBracket),
        ("[^]", Syntax::Basic, ErrorKind::UnmatchedBracket),
        ("[a-", Syntax::Basic, ErrorKind::UnmatchedBracket),
        ("\\(", Syntax::Basic, ErrorKind::UnmatchedOpen),
        ("(", Syntax::Extended, ErrorKind::UnmatchedOpen),
        ("\\)", Syntax::Basic, ErrorKind::UnmatchedClose),
        ("(a", Syntax::Extended, ErrorKind::UnmatchedOpen),
    ];
    for (pattern, syntax, kind) in kinds {
        match Regex::compile_bytes(*syntax, pattern.as_bytes()) {
            Ok(_) => panic!("{:?} {} should not compile", syntax, pattern),
            Err(e) => assert_eq!(
                e.kind(),
                *kind,
                "{:?} {}: wrong kind {:?}",
                syntax,
                pattern,
                e.kind()
            ),
        }
    }
}

/// Structural checks: capture numbering is by OPENING delimiter, and
/// quantifier stacking nests.
#[test]
fn ast_shape() {
    use winux_regex::ast::Node;

    let re = Regex::compile(Syntax::Basic, r"\(\(a\)\(b\)\)").unwrap();
    assert_eq!(re.capture_count(), 3);
    match re.root() {
        Node::Capture { index, child } => {
            assert_eq!(*index, 1);
            match child.as_ref() {
                Node::Sequence(kids) => {
                    assert_eq!(kids.len(), 2);
                    assert!(matches!(&kids[0], Node::Capture { index: 2, .. }));
                    assert!(matches!(&kids[1], Node::Capture { index: 3, .. }));
                }
                other => panic!("expected sequence, got {:?}", other),
            }
        }
        other => panic!("expected capture, got {:?}", other),
    }

    // glibc alignment (WinuxCmd #1496): opening-order numbering makes the
    // outer group \1 here; the trailing `*` makes the root a Repeat.
    let re = Regex::compile(Syntax::Basic, r"\(\(a\)*\)*").unwrap();
    assert_eq!(re.capture_count(), 2);
    if let Node::Repeat { child, .. } = re.root() {
        assert!(matches!(child.as_ref(), Node::Capture { index: 1, .. }));
    } else {
        panic!("expected repeat root");
    }

    // ERE numbered the same way.
    let re = Regex::compile(Syntax::Extended, r"((a)(b))").unwrap();
    assert_eq!(re.capture_count(), 3);
    if let Node::Capture { index, child } = re.root() {
        assert_eq!(*index, 1);
        if let Node::Sequence(kids) = child.as_ref() {
            assert!(matches!(&kids[0], Node::Capture { index: 2, .. }));
            assert!(matches!(&kids[1], Node::Capture { index: 3, .. }));
        } else {
            panic!("expected sequence under outer capture");
        }
    } else {
        panic!("expected capture root");
    }

    // Quantifier stacking: a{2}{3} -> Repeat{3,3}(Repeat{2,2}(Literal)).
    let re = Regex::compile(Syntax::Extended, "a{2}{3}").unwrap();
    if let Node::Repeat { min, max, child } = re.root() {
        assert_eq!((*min, *max), (3, Some(3)));
        match child.as_ref() {
            Node::Repeat { min, max, .. } => assert_eq!((*min, *max), (2, Some(2))),
            other => panic!("expected inner repeat, got {:?}", other),
        }
    } else {
        panic!("expected outer repeat");
    }

    // BRE context-dependent anchors: mid-pattern ^/$ parse as literals.
    let re = Regex::compile(Syntax::Basic, "a^b").unwrap();
    assert!(matches!(re.root(), Node::Sequence(_)));
    if let Node::Sequence(kids) = re.root() {
        assert!(matches!(&kids[1], Node::Literal(b'^')));
    }

    // ERE anchors are context-independent.
    let re = Regex::compile(Syntax::Extended, "a^b").unwrap();
    if let Node::Sequence(kids) = re.root() {
        assert!(matches!(&kids[1], Node::AnchorStart));
    } else {
        panic!("expected sequence");
    }

    // Collapse: single-piece sequences and single-branch alternations vanish.
    let re = Regex::compile(Syntax::Extended, "(a)").unwrap();
    assert!(matches!(re.root(), Node::Capture { .. }));
    let re = Regex::compile(Syntax::Basic, "abc").unwrap();
    assert!(matches!(re.root(), Node::Sequence(_)));
}
