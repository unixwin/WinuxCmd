//! Literal pre-filter fast path — the #1531-class acceleration ported from
//! `src/utils/regex.cppm` (`Pattern::fast_` / `build_fast_path`, itself
//! modeled on GNU grep's compile-time kwset / dfa "must" analysis).
//!
//! The interpreter used to be attempted at EVERY scan position of
//! [`Regex::checked_find_at_bytes`], so literal patterns like `fox` scanned
//! pathological inputs orders of magnitude slower than a fixed-string pass.
//! The filters below are computed at compile time from the same parsed node
//! tree the interpreter runs on — no text-layer re-lexing, so the fast path
//! cannot diverge from the interpreter that still validates every candidate:
//!
//! * `anchored`: the pattern's first consumable-or-asserted element is `^`,
//!   so every possible match begins at absolute position 0 (AnchorStart
//!   compares absolute positions). find needs the single attempt at 0 and
//!   none at all when `start > 0`.
//! * `prefix`: bytes every match MUST begin with (a run of Literal nodes,
//!   single-member classes, `{m}` expansions of literal bodies; zero-width
//!   assertions skipped). find jumps between prefix candidates and runs the
//!   interpreter only there; `match_at` / `matches_entire` reject up front.
//!   The legacy traversal attempts `start` and then, after every failed
//!   attempt, the next non-UTF-8-continuation byte; candidates whose first
//!   prefix byte is ASCII or a lead byte always sit on such positions, so
//!   skipping non-candidates cannot change which positions get attempted (a
//!   continuation-range first prefix byte is rejected in [`FastPath::build`]
//!   for exactly that reason).
//! * `first`: 256-entry set of bytes a match's first consumed byte may be,
//!   expanded per byte through the engine's own `class_matches` / fold rule.
//!   The scan keeps the legacy advance rule and only skips the (futile)
//!   interpreter attempt.
//! * `pure_literal`: the whole tree is a chain of Literal nodes and there
//!   are no captures — a prefix candidate IS the match, byte-for-byte what
//!   the interpreter would report.
//!
//! All four are necessary conditions, never sufficient: every candidate
//! still goes through the unmodified interpreter (`local_attempt`), and the
//! per-call work budget (`StepBudget` caps) is shared across attempts
//! unchanged. Anything not modeled (alternations, backreferences, dot,
//! patterns that can match empty, first sets dense enough to be useless)
//! falls back to the legacy scan. `FastPath::disabled` — also reachable at
//! runtime through [`Regex::disable_fast_path`] and at compile time by
//! building without the default `fast-path` feature — reproduces the exact
//! legacy behavior; the differential test suite requires the two modes to
//! agree byte-for-byte on every corpus input.

use crate::ast::{ClassAtom, Node};
use crate::matcher::{chars_equal, class_matches};
use crate::Regex;

/// Cap on the extracted required prefix (regex.cppm `kMaxFastPrefix`).
const MAX_FAST_PREFIX: usize = 32;

/// Sentinel: the subtree is not a pure literal chain (regex.cppm
/// `kNotPureLiteral`).
const NOT_PURE_LITERAL: usize = usize::MAX;

/// The compile-time filters. Every field is a NECESSARY condition for a
/// match (or a scan restriction derived from one); `disabled()` sets the
/// neutral configuration under which every scan falls back to the legacy
/// interpreter loop.
#[derive(Debug, Clone)]
pub(crate) struct FastPath {
    /// Every possible match begins at absolute position 0.
    pub(crate) anchored: bool,
    /// The first-byte filter is USABLE (pattern cannot start with an
    /// arbitrary byte and cannot match empty). `true` = scan everything.
    pub(crate) first_any: bool,
    /// Whole tree is a literal run, no captures: a prefix candidate is the
    /// match.
    pub(crate) pure_literal: bool,
    /// Bytes every match must begin with; empty = no usable prefix.
    pub(crate) prefix: Vec<u8>,
    /// Bytes a match's first consumed byte may be (only consulted when
    /// `first_any` is false).
    pub(crate) first: [bool; 256],
}

impl FastPath {
    /// No filters at all: every entry point behaves exactly like the legacy
    /// interpreter scan (the `fast-path` feature off and
    /// [`Regex::disable_fast_path`] land here).
    pub(crate) fn disabled() -> FastPath {
        FastPath {
            anchored: false,
            first_any: true,
            pure_literal: false,
            prefix: Vec::new(),
            first: [false; 256],
        }
    }

    /// Compute the filters for `root`. Pure function of the tree plus the
    /// two compile flags; called once per compiled regex.
    pub(crate) fn build(root: &Node, capture_count: usize, ignore_case: bool) -> FastPath {
        let mut fast = FastPath {
            anchored: analyze_anchored(root),
            first_any: true,
            pure_literal: false,
            prefix: Vec::new(),
            first: [false; 256],
        };
        let info = analyze_first(root, ignore_case);
        fast.first = info.bytes;
        // A pattern that can match empty yields a match at every position; a
        // first-byte scan would miss those empty matches, so fall back.
        fast.first_any = info.any || info.can_be_empty;

        let mut prefix = Vec::new();
        extract_required_prefix(root, ignore_case, &mut prefix);
        if let Some(&c0) = prefix.first() {
            if (0x80..0xC0).contains(&c0) {
                // A continuation-range first byte would place candidates on
                // positions the legacy traversal never attempts (it skips
                // UTF-8 continuation runs after each failed attempt); keep
                // the plain scan.
                prefix.clear();
            }
        }
        fast.prefix = prefix;

        if capture_count == 0 && !fast.prefix.is_empty() {
            let literal_len = pure_literal_length(root);
            fast.pure_literal = literal_len != NOT_PURE_LITERAL
                && literal_len <= MAX_FAST_PREFIX
                && literal_len == fast.prefix.len();
        }
        fast
    }
}

// --- compile-time analysis -------------------------------------------------

/// True iff every possible match of `node` begins at absolute position 0,
/// i.e. the first thing it can consume-or-assert is AnchorStart
/// (regex.cppm `analyze_anchored`).
fn analyze_anchored(node: &Node) -> bool {
    match node {
        Node::AnchorStart | Node::BufferStart => true,
        Node::Empty => false,
        Node::Capture { child, .. } => analyze_anchored(child),
        Node::Sequence(children) => {
            for child in children {
                if matches!(child, Node::Empty) {
                    continue;
                }
                return analyze_anchored(child);
            }
            false
        }
        Node::Alternation(children) => children.iter().all(analyze_anchored),
        _ => false,
    }
}

struct FirstInfo {
    bytes: [bool; 256],
    any: bool,
    can_be_empty: bool,
}

/// Bytes a match's first consumed byte may be, whether the first byte is
/// arbitrary, and whether the node can match empty (regex.cppm
/// `analyze_first`).
fn analyze_first(node: &Node, ignore_case: bool) -> FirstInfo {
    match node {
        Node::Empty
        | Node::AnchorStart
        | Node::AnchorEnd
        | Node::WordStart
        | Node::WordEnd
        | Node::WordBoundary
        | Node::NotWordBoundary
        | Node::LineStart(_)
        | Node::LineEnd(_)
        | Node::BufferStart
        | Node::BufferEnd => FirstInfo {
            bytes: [false; 256],
            any: false,
            can_be_empty: true,
        },
        Node::Literal(b) => FirstInfo {
            bytes: literal_byte_set(*b, ignore_case),
            any: false,
            can_be_empty: false,
        },
        Node::Dot | Node::DotRestricted { .. } => FirstInfo {
            bytes: [false; 256],
            any: true,
            can_be_empty: false,
        },
        Node::CharClass { negated, atoms } => FirstInfo {
            bytes: expand_class_set(*negated, atoms, ignore_case),
            any: false,
            can_be_empty: false,
        },
        Node::Sequence(children) => {
            let mut info = FirstInfo {
                bytes: [false; 256],
                any: false,
                can_be_empty: true,
            };
            for child in children {
                let sub = analyze_first(child, ignore_case);
                for b in 0..256 {
                    info.bytes[b] = info.bytes[b] || sub.bytes[b];
                }
                info.any = info.any || sub.any;
                if !sub.can_be_empty {
                    // The first consumed byte cannot come from anything
                    // after this child.
                    info.can_be_empty = false;
                    return info;
                }
            }
            info
        }
        Node::Alternation(children) => {
            let mut info = FirstInfo {
                bytes: [false; 256],
                any: false,
                can_be_empty: false,
            };
            for child in children {
                let sub = analyze_first(child, ignore_case);
                for b in 0..256 {
                    info.bytes[b] = info.bytes[b] || sub.bytes[b];
                }
                info.any = info.any || sub.any;
                info.can_be_empty = info.can_be_empty || sub.can_be_empty;
            }
            info
        }
        Node::Capture { child, .. } => analyze_first(child, ignore_case),
        Node::BackReference(_) => {
            // Matches an unknown (possibly empty, possibly arbitrary) span.
            FirstInfo {
                bytes: [false; 256],
                any: true,
                can_be_empty: true,
            }
        }
        Node::Repeat { min, child, .. } => {
            let mut info = analyze_first(child, ignore_case);
            info.can_be_empty = *min == 0 || info.can_be_empty;
            info
        }
    }
}

/// Bytes t satisfying the engine's own literal rule (`chars_equal` under
/// the compile flags) — regex.cppm `literal_byte_set`.
fn literal_byte_set(ch: u8, ignore_case: bool) -> [bool; 256] {
    let mut set = [false; 256];
    if !ignore_case {
        set[ch as usize] = true;
        return set;
    }
    let folded = ch.to_ascii_lowercase();
    set[folded as usize] = true;
    if folded.is_ascii_alphabetic() {
        set[folded.to_ascii_uppercase() as usize] = true;
    }
    set
}

/// Exact per-byte replication of the interpreter's `class_matches` over all
/// 256 byte values (regex.cppm `expand_class_set`) — the SAME function the
/// matcher runs, so the sets cannot drift.
fn expand_class_set(negated: bool, atoms: &[ClassAtom], ignore_case: bool) -> [bool; 256] {
    let mut set = [false; 256];
    for (byte, hit) in set.iter_mut().enumerate() {
        *hit = class_matches(ignore_case, negated, atoms, byte as u8);
    }
    set
}

/// Appends the literal bytes every match must begin with. Returns false
/// when the walk hit a construct that breaks the fixed byte run (or the cap
/// was reached); the prefix accumulated so far stays valid either way
/// (regex.cppm `extract_required_prefix`).
fn extract_required_prefix(node: &Node, ignore_case: bool, out: &mut Vec<u8>) -> bool {
    if out.len() >= MAX_FAST_PREFIX {
        return false;
    }
    match node {
        Node::Literal(b) => {
            out.push(*b);
            out.len() < MAX_FAST_PREFIX
        }
        Node::Empty
        | Node::AnchorStart
        | Node::AnchorEnd
        | Node::WordStart
        | Node::WordEnd
        | Node::WordBoundary
        | Node::NotWordBoundary
        | Node::LineStart(_)
        | Node::LineEnd(_)
        | Node::BufferStart
        | Node::BufferEnd => {
            // Zero-width: the bytes before and after stay consecutive.
            true
        }
        Node::Capture { child, .. } => extract_required_prefix(child, ignore_case, out),
        Node::CharClass { negated, atoms } => {
            let set = expand_class_set(*negated, atoms, ignore_case);
            let mut hits = 0;
            let mut only = 0usize;
            for (b, member) in set.iter().enumerate() {
                if *member {
                    hits += 1;
                    only = b;
                }
            }
            if hits != 1 {
                return false;
            }
            out.push(only as u8);
            out.len() < MAX_FAST_PREFIX
        }
        Node::Sequence(children) => {
            for child in children {
                if !extract_required_prefix(child, ignore_case, out) {
                    return false;
                }
            }
            true
        }
        Node::Repeat { min, child, .. } => {
            // {m,} / {m,n} of a literal body requires m fixed copies at the
            // start (a{3}, (ab){2}); everything past the minimum is
            // variable, so the walk stops after appending them.
            if *min == 0 {
                return false;
            }
            let start = out.len();
            if !extract_required_prefix(child, ignore_case, out) {
                return false;
            }
            let segment = out[start..].to_vec();
            if segment.is_empty() {
                return false;
            }
            let mut copy = 1u32;
            while copy < *min && out.len() < MAX_FAST_PREFIX {
                out.extend_from_slice(&segment);
                copy += 1;
            }
            false
        }
        // Dot, Alternation, BackReference.
        _ => false,
    }
}

/// Byte length iff the whole subtree is a chain of Literal nodes (no
/// anchors, no captures, no classes) — the whitelist condition under which
/// a prefix candidate IS the match, byte-for-byte what the interpreter
/// would report. Anything else falls back to the interpreter (regex.cppm
/// `pure_literal_length`).
fn pure_literal_length(node: &Node) -> usize {
    match node {
        Node::Literal(_) => 1,
        Node::Sequence(children) => {
            let mut total = 0usize;
            for child in children {
                let len = pure_literal_length(child);
                if len == NOT_PURE_LITERAL {
                    return NOT_PURE_LITERAL;
                }
                total += len;
            }
            total
        }
        _ => NOT_PURE_LITERAL,
    }
}

// --- scan-time helpers -----------------------------------------------------

impl Regex {
    /// Prefix bytes must appear at `pos` under the engine's own comparison
    /// rule (`chars_equal` folds ASCII under ignore_case; regex.cppm
    /// `prefix_matches_at`).
    pub(crate) fn prefix_matches_at(&self, text: &[u8], pos: usize) -> bool {
        let prefix = &self.fast.prefix;
        if pos + prefix.len() > text.len() {
            return false;
        }
        for (i, &p) in prefix.iter().enumerate() {
            if !chars_equal(self.ignore_case, text[pos + i], p) {
                return false;
            }
        }
        true
    }

    /// Leftmost position >= `from` where the required prefix matches
    /// (regex.cppm `find_prefix_candidate`), or None.
    pub(crate) fn find_prefix_candidate(&self, text: &[u8], from: usize) -> Option<usize> {
        let prefix = &self.fast.prefix;
        let c0 = prefix[0];
        if !self.ignore_case || !c0.is_ascii_alphabetic() {
            // The first byte compares exactly, so a memchr jump covers every
            // candidate; remaining bytes go through prefix_matches_at.
            let mut pos = from;
            while pos < text.len() {
                let hit = memchr_from(c0, text, pos)?;
                if self.prefix_matches_at(text, hit) {
                    return Some(hit);
                }
                pos = hit + 1;
            }
            return None;
        }
        // Case-folded first byte: plain byte walk with folded comparison.
        // An ASCII alpha byte is never a UTF-8 continuation byte, so the
        // candidates stay on legacy attempt positions.
        let target = c0.to_ascii_lowercase();
        let mut pos = from;
        while pos + prefix.len() <= text.len() {
            if text[pos].to_ascii_lowercase() == target && self.prefix_matches_at(text, pos) {
                return Some(pos);
            }
            pos += 1;
        }
        None
    }
}

/// First `needle` at or after `from` (std-only memchr).
fn memchr_from(needle: u8, text: &[u8], from: usize) -> Option<usize> {
    if from >= text.len() {
        return None;
    }
    text[from..]
        .iter()
        .position(|&b| b == needle)
        .map(|i| from + i)
}

#[cfg(test)]
mod tier_tests {
    // Which filter engages for a shape is itself load-bearing (the C++
    // build_fast_path invariants); pin it. The cross-mode result equality
    // lives in tests/fastpath_equiv.rs.
    use super::{FastPath, MAX_FAST_PREFIX};
    use crate::{Regex, Syntax};

    fn fast_of(syntax: Syntax, pattern: &[u8]) -> FastPath {
        let re = Regex::compile_bytes(syntax, pattern).expect("compiles");
        FastPath::build(re.root(), re.capture_count(), re.ignore_case())
    }

    fn fast_of_ic(syntax: Syntax, pattern: &[u8]) -> FastPath {
        let re = Regex::compile_bytes_ignore_case(syntax, pattern, true).expect("compiles");
        FastPath::build(re.root(), re.capture_count(), re.ignore_case())
    }

    #[test]
    fn pure_literal_engages_for_bare_runs() {
        let f = fast_of(Syntax::Basic, b"fox");
        assert!(f.pure_literal);
        assert_eq!(f.prefix, b"fox");
        assert!(!f.anchored);
        let f = fast_of(Syntax::Extended, b"a");
        assert!(f.pure_literal);
        // Anything non-literal in the tree disqualifies.
        assert!(!fast_of(Syntax::Extended, b"f.x").pure_literal);
        assert!(!fast_of(Syntax::Extended, b"(fox)").pure_literal); // capture
        assert!(!fast_of(Syntax::Extended, b"^fox").pure_literal); // anchor
    }

    #[test]
    fn anchored_detects_leading_anchor_through_wrappers() {
        assert!(fast_of(Syntax::Extended, b"^fox").anchored);
        assert!(fast_of(Syntax::Basic, b"^fox").anchored);
        assert!(fast_of(Syntax::Extended, b"(^a)").anchored); // capture wrapper
        assert!(fast_of(Syntax::Extended, b"^^ab").anchored); // double anchor
                                                              // Alternation with a non-anchored branch is NOT anchored.
        assert!(!fast_of(Syntax::Extended, b"^a|b").anchored);
        assert!(!fast_of(Syntax::Extended, b"a^b").anchored);
        assert!(!fast_of(Syntax::Basic, b"fox").anchored);
    }

    #[test]
    fn prefix_extracts_runs_single_classes_and_intervals() {
        assert_eq!(fast_of(Syntax::Extended, b"f.x").prefix, b"f");
        assert_eq!(fast_of(Syntax::Extended, b"ab[0-9]").prefix, b"ab");
        // Single-member class.
        assert_eq!(fast_of(Syntax::Extended, b"[a]x").prefix, b"ax");
        // A multi-member POSIX class (space has 6 C-locale members) stops
        // the walk but keeps the preceding run.
        assert_eq!(fast_of(Syntax::Extended, b"[[:space:]]z").prefix, b"");
        // {m} literal expansion: fixed copies only.
        assert_eq!(fast_of(Syntax::Extended, b"a{3}").prefix, b"aaa");
        assert_eq!(fast_of(Syntax::Extended, b"(ab){2}").prefix, b"abab");
        // The walk STOPS at a {m,n} node (everything past the minimum is
        // variable): the trailing c is not appended, the accumulated "abb"
        // stays valid (regex.cppm returns false from the Repeat case).
        assert_eq!(fast_of(Syntax::Extended, b"ab{2}c").prefix, b"abb");
        // Zero-width assertions stay transparent.
        assert_eq!(fast_of(Syntax::Basic, br"\bfoo").prefix, b"foo");
        // Multi-member class or dot stops the walk but keeps the run.
        assert_eq!(fast_of(Syntax::Extended, b"x[ab]y").prefix, b"x");
        // Case: the prefix keeps the pattern's original bytes.
        assert_eq!(fast_of_ic(Syntax::Extended, b"FOX").prefix, b"FOX");
    }

    #[test]
    fn prefix_rejects_continuation_range_first_byte() {
        // A required first byte in 0x80..0xBF would put candidates on UTF-8
        // continuation positions the legacy traversal never attempts.
        let f = fast_of(Syntax::Extended, &[0x8A, b'z']);
        assert!(f.prefix.is_empty());
        let f = fast_of(Syntax::Extended, &[0xE4, 0xB8, 0xAD]); // 中: lead byte
        assert_eq!(f.prefix, &[0xE4, 0xB8, 0xAD]);
    }

    #[test]
    fn first_set_expanded_through_engine_rules() {
        // Dot disables the first-byte filter entirely.
        assert!(fast_of(Syntax::Extended, b".").first_any);
        // Empty-capable patterns would miss their empty matches.
        // ((a?)b still must consume b, so its filter IS usable; (a?)b? can
        // match empty.)
        assert!(fast_of(Syntax::Extended, b"a*").first_any);
        assert!(fast_of(Syntax::Extended, b"(a?)b?").first_any);
        assert!(!fast_of(Syntax::Extended, b"(a?)b").first_any);
        // A backref at the sequence HEAD may match empty/arbitrary
        // ((a)\1 must consume 'a' first, so its filter IS usable).
        assert!(fast_of(Syntax::Extended, br"(a*)\1").first_any);
        assert!(!fast_of(Syntax::Extended, br"(a)\1").first_any);
        // Dot anywhere in the head makes the first byte arbitrary: the
        // filter is disabled (legacy scan).
        assert!(fast_of(Syntax::Extended, b".{2}fox").first_any);
        // Alternation head: usable set, no prefix.
        let f = fast_of(Syntax::Extended, b"(fox|bar)baz");
        assert!(!f.first_any);
        assert!(f.first[b'f' as usize] && f.first[b'b' as usize]);
        assert!(!f.first[b'x' as usize]);
        // Case-sensitive literal head admits only the exact byte.
        let f = fast_of(Syntax::Extended, b"Fox");
        assert!(!f.first_any);
        assert!(f.first[b'F' as usize] && !f.first[b'f' as usize]);
        assert!(!f.first[b'o' as usize]);
        // -i literal admits both folds; POSIX classes cross-fold.
        let f = fast_of_ic(Syntax::Extended, b"fox");
        assert!(!f.first_any);
        assert!(f.first[b'f' as usize] && f.first[b'F' as usize]);
        let f = fast_of_ic(Syntax::Extended, b"[[:upper:]]9");
        assert!(f.first[b'A' as usize] && f.first[b'a' as usize]);
        // Negated class keeps everything but its members.
        let f = fast_of(Syntax::Extended, b"[^x]y");
        assert!(!f.first_any);
        assert!(!f.first[b'x' as usize] && f.first[b'y' as usize]);
    }

    #[test]
    fn prefix_cap_is_bounded() {
        let f = fast_of(Syntax::Extended, b"abcdefg hijklmnop qrstuvwxyz0123456789");
        assert!(f.prefix.len() <= MAX_FAST_PREFIX);
        assert_eq!(f.prefix.len(), MAX_FAST_PREFIX);
    }

    #[test]
    fn disabled_is_the_legacy_neutral() {
        let d = FastPath::disabled();
        assert!(!d.anchored && !d.pure_literal && d.prefix.is_empty());
        assert!(d.first_any, "neutral config must scan every position");
    }
}
