//! Pattern compilation and the two matcher entry points GNU grep's
//! `execute` provides (dfasearch.c EGexecute / kwsearch.c Fexecute):
//!
//! * [`PatternMatcher::find_line`] — `execute(buf, size, &len, NULL)`: the
//!   first SELECTED line of a buffer region (line mode);
//! * [`PatternMatcher::piece`] — `execute(..., start_ptr)`: the leftmost
//!   (then longest) exact match at or after a position inside one line, for
//!   `-o` and `--color`.
//!
//! Line selection is layered like GNU's kwset -> DFA -> regex chain:
//! a single case-sensitive literal is found with a packed-pair byte search
//! ([`super::bytes::Finder`]); literal sets (`-F`, `-i`, metacharacter-free
//! `-G`/`-E` patterns) and back-reference-free regexes go through the
//! winux-regex lazy line DFA, which scans whole buffers; everything else
//! (back-references, lines the DFA is not exact on) runs the interpreter
//! per line. `-w` uses the literal search / DFA as a necessary-condition
//! prefilter and confirms candidate lines with the exact GNU word walk.

use winux_regex::ast::Node;
use winux_regex::dfa::{InputRequirement, LineDfa, LineMode};
use winux_regex::{ErrorKind, Regex, Syntax};

use super::bytes::{memchr, memrchr, Finder};
use super::text::Ui;
use super::Matcher;

/// One pattern line plus where it came from (for `FILE:LINENO:` error
/// prefixes, dfasearch.c regex_compile).
#[derive(Clone, Debug)]
pub struct RawPattern {
    pub bytes: Vec<u8>,
    /// `-f` operand and 1-based line number; None for -e / operand keys.
    pub origin: Option<(String, usize)>,
}

#[derive(Clone, Copy, Debug)]
pub struct MatchFlags {
    pub ignore_case: bool,
    pub words: bool,
    pub lines: bool,
    pub utf8: bool,
    pub eol: u8,
}

/// Case-aware literal set (the kwset of Fexecute).
struct LitSet {
    lits: Vec<Vec<u8>>,
    ic: bool,
    has_empty: bool,
    /// literal indexes by first byte (both cases under -i)
    by_first: Vec<Vec<u32>>,
}

impl LitSet {
    fn new(lits: Vec<Vec<u8>>, ic: bool) -> LitSet {
        let mut by_first = vec![Vec::new(); 256];
        let mut has_empty = false;
        for (k, l) in lits.iter().enumerate() {
            match l.first() {
                None => has_empty = true,
                Some(&b) => {
                    if ic && b.is_ascii_alphabetic() {
                        by_first[b.to_ascii_lowercase() as usize].push(k as u32);
                        by_first[b.to_ascii_uppercase() as usize].push(k as u32);
                    } else {
                        by_first[b as usize].push(k as u32);
                    }
                }
            }
        }
        LitSet {
            lits,
            ic,
            has_empty,
            by_first,
        }
    }

    fn eq(&self, a: &[u8], b: &[u8]) -> bool {
        if self.ic {
            a.eq_ignore_ascii_case(b)
        } else {
            a == b
        }
    }

    /// Longest literal matching at `pos` with length <= `max_len`.
    fn longest_at(&self, line: &[u8], pos: usize, max_len: usize) -> Option<usize> {
        let mut best: Option<usize> = if self.has_empty { Some(0) } else { None };
        if pos < line.len() {
            for &k in &self.by_first[line[pos] as usize] {
                let l = &self.lits[k as usize];
                if l.len() <= max_len
                    && pos + l.len() <= line.len()
                    && self.eq(&line[pos..pos + l.len()], l)
                    && best.map_or(true, |b| l.len() > b)
                {
                    best = Some(l.len());
                }
            }
        }
        best
    }

    /// Leftmost, then longest occurrence at or after `from`.
    fn find(&self, line: &[u8], from: usize) -> Option<(usize, usize)> {
        let mut p = from;
        while p <= line.len() {
            if let Some(len) = self.longest_at(line, p, usize::MAX) {
                return Some((p, len));
            }
            p += 1;
        }
        None
    }

    fn any_equal(&self, line: &[u8]) -> bool {
        self.lits.iter().any(|l| self.eq(l, line))
    }

    fn any_occurs(&self, line: &[u8]) -> bool {
        self.has_empty || self.find(line, 0).is_some()
    }

    /// The literal set as a trie-shaped node tree (shared prefixes keep
    /// the NFA small for big `-f` word lists).
    fn trie(&self) -> Option<Node> {
        if self.lits.iter().any(|l| l.len() > 4096) {
            return None;
        }
        let words: Vec<&[u8]> = self.lits.iter().map(|l| l.as_slice()).collect();
        Some(trie_node(&words, self.ic, 0))
    }
}

fn trie_node(words: &[&[u8]], ic: bool, depth: usize) -> Node {
    let key = |b: u8| if ic { b.to_ascii_lowercase() } else { b };
    let mut groups: Vec<(u8, Vec<&[u8]>)> = Vec::new();
    let mut has_empty = false;
    for w in words {
        match w.first() {
            None => has_empty = true,
            Some(&b) => match groups.iter_mut().find(|(k, _)| *k == key(b)) {
                Some((_, v)) => v.push(&w[1..]),
                None => groups.push((key(b), vec![&w[1..]])),
            },
        }
    }
    let mut alts = Vec::new();
    for (b, rest) in groups {
        let tail = if rest.len() == 1 {
            Node::Sequence(rest[0].iter().map(|&c| Node::Literal(c)).collect())
        } else if depth > 512 {
            // keep recursion bounded: fall back to a flat alternation
            Node::Alternation(
                rest.iter()
                    .map(|r| Node::Sequence(r.iter().map(|&c| Node::Literal(c)).collect()))
                    .collect(),
            )
        } else {
            trie_node(&rest, ic, depth + 1)
        };
        alts.push(Node::Sequence(vec![Node::Literal(b), tail]));
    }
    if has_empty {
        alts.push(Node::Empty);
    }
    match alts.len() {
        0 => Node::Alternation(Vec::new()),
        1 => alts.pop().expect("one"),
        _ => Node::Alternation(alts),
    }
}

enum Pats {
    Lits(LitSet),
    Res(Vec<Regex>),
}

/// Compiled pattern set with the selection fast paths.
pub struct PatternMatcher {
    pats: Pats,
    f: MatchFlags,
    /// an empty pattern without -w/-x: every line is selected
    match_all: bool,
    /// single case-sensitive literal search (prefilter or exact)
    finder: Option<Finder>,
    dfa: Option<LineDfa>,
    req: InputRequirement,
    /// the fast path decides alone (no per-line confirmation)
    fast_exact: bool,
    // ill-formed-line cache for the DFA input requirement, valid for the
    // current buffer (reset by `new_buffer`)
    good_from: usize,
    good_end: usize,
}

impl PatternMatcher {
    /// Must be called whenever the scanned buffer's contents move.
    pub fn new_buffer(&mut self) {
        self.good_from = usize::MAX;
        self.good_end = 0;
    }

    /// GNU `execute(..., NULL)`: the first selected line of
    /// `buf[from..lim]` (`from` a line start, `buf[lim - 1]` the eol byte).
    /// Returns (line start, index of the line's eol byte).
    pub fn find_line(&mut self, buf: &[u8], from: usize, lim: usize) -> Option<(usize, usize)> {
        let eol = self.f.eol;
        if from >= lim {
            return None;
        }
        if self.match_all {
            let le = from + memchr(eol, &buf[from..lim]).unwrap_or(lim - 1 - from);
            return Some((from, le));
        }
        if let Some(finder) = &self.finder {
            let mut p = from;
            loop {
                let pos = finder.find(&buf[..lim], p)?;
                let ls = match memrchr(eol, &buf[from..pos]) {
                    Some(k) => from + k + 1,
                    None => from,
                };
                let le = pos + memchr(eol, &buf[pos..lim]).unwrap_or(lim - 1 - pos);
                if self.fast_exact || self.line_selected(&buf[ls..le]) {
                    return Some((ls, le));
                }
                p = le + 1;
                if p >= lim {
                    return None;
                }
            }
        }
        if self.dfa.is_some() {
            return self.find_line_dfa(buf, from, lim);
        }
        let mut p = from;
        while p < lim {
            let le = p + memchr(eol, &buf[p..lim]).unwrap_or(lim - 1 - p);
            if self.line_selected(&buf[p..le]) {
                return Some((p, le));
            }
            p = le + 1;
        }
        None
    }

    fn find_line_dfa(&mut self, buf: &[u8], from: usize, lim: usize) -> Option<(usize, usize)> {
        let eol = self.f.eol;
        let mut p = from;
        while p < lim {
            // [p, good_end) holds only lines the DFA is exact on; the line
            // starting at good_end (if < lim) is not.
            if !(self.good_from <= p && p <= self.good_end) {
                self.good_from = p;
                self.good_end = match self.req {
                    InputRequirement::Any => lim,
                    InputRequirement::ValidUtf8 => match std::str::from_utf8(&buf[p..lim]) {
                        Ok(_) => lim,
                        Err(e) => line_start_of(buf, p, p + e.valid_up_to(), eol),
                    },
                    InputRequirement::Ascii => match first_non_ascii(&buf[p..lim]) {
                        None => lim,
                        Some(k) => line_start_of(buf, p, p + k, eol),
                    },
                };
            }
            let good_end = self.good_end;
            if p < good_end {
                let dfa = self.dfa.as_mut().expect("dfa");
                match dfa.find_line(buf, p, good_end) {
                    Some((ls, le)) => {
                        if self.fast_exact || self.line_selected(&buf[ls..le]) {
                            return Some((ls, le));
                        }
                        p = le + 1;
                        continue;
                    }
                    None => p = good_end,
                }
            }
            if p >= lim {
                return None;
            }
            // the ill-formed line: interpreter
            let le = p + memchr(eol, &buf[p..lim]).unwrap_or(lim - 1 - p);
            let selected = self.line_selected(&buf[p..le]);
            // the cache restarts after this line
            self.good_from = usize::MAX;
            if selected {
                return Some((p, le));
            }
            p = le + 1;
        }
        None
    }

    /// Exact selection of one line (no eol byte).
    pub fn line_selected(&self, line: &[u8]) -> bool {
        if self.match_all {
            return true;
        }
        match &self.pats {
            Pats::Lits(set) => {
                if self.f.lines {
                    set.any_equal(line)
                } else if self.f.words {
                    self.fword_find(set, line, 0).is_some()
                } else {
                    set.any_occurs(line)
                }
            }
            Pats::Res(res) => res.iter().any(|re| {
                if self.f.lines {
                    re.matches_entire_bytes(line)
                } else if self.f.words {
                    self.re_word_find(re, line, 0).is_some()
                } else {
                    re.find_at_bytes(line, 0).is_some()
                }
            }),
        }
    }

    /// GNU `execute(..., start_ptr)`: (start, length) of the leftmost, then
    /// longest exact match at or after `cur` within `line`.
    pub fn piece(&self, line: &[u8], cur: usize) -> Option<(usize, usize)> {
        let cur = self.align(line, cur);
        if cur > line.len() {
            return None;
        }
        match &self.pats {
            Pats::Lits(set) => {
                if self.f.lines {
                    // kwset with eol-wrapped keys: only a whole line
                    if cur == 0 && set.any_equal(line) {
                        Some((0, line.len()))
                    } else {
                        None
                    }
                } else if self.f.words {
                    self.fword_find(set, line, cur)
                } else {
                    set.find(line, cur)
                }
            }
            Pats::Res(res) => {
                let mut best: Option<(usize, usize)> = None;
                for re in res {
                    let hit = if self.f.words {
                        self.re_word_find(re, line, cur)
                    } else {
                        re.find_at_bytes(line, cur).map(|m| (m.begin, m.end - m.begin))
                    };
                    if let Some((s, l)) = hit {
                        best = match best {
                            None => Some((s, l)),
                            Some((bs, bl)) => {
                                if s < bs || (s == bs && l > bl) {
                                    Some((s, l))
                                } else {
                                    Some((bs, bl))
                                }
                            }
                        };
                    }
                }
                best
            }
        }
    }

    /// Next character boundary at or after `pos` (UTF-8 locale: skip
    /// continuation bytes; C locale: bytes are characters).
    fn align(&self, line: &[u8], mut pos: usize) -> usize {
        if self.f.utf8 {
            while pos < line.len() && (0x80..0xC0).contains(&line[pos]) {
                pos += 1;
            }
        }
        pos
    }

    fn next_char(&self, line: &[u8], pos: usize) -> usize {
        self.align(line, pos + 1)
    }

    /// kwsearch.c Fexecute -w walk.
    fn fword_find(&self, set: &LitSet, line: &[u8], from: usize) -> Option<(usize, usize)> {
        let mut beg = from;
        loop {
            let (s, len0) = set.find(line, beg)?;
            if !wordchar_before(line, s, self.f.utf8) {
                let mut len = len0;
                loop {
                    if !wordchar_at(line, s + len, self.f.utf8) {
                        return Some((s, len));
                    }
                    if len == 0 {
                        break;
                    }
                    match set.longest_at(line, s, len - 1) {
                        Some(l) => len = l,
                        None => break,
                    }
                }
            }
            // skip the word constituents that cannot precede a match
            let b = s + wordchars_size(line, s, self.f.utf8);
            if b >= line.len() {
                return None;
            }
            beg = self.next_char(line, b);
        }
    }

    /// dfasearch.c EGexecute -w walk for one regex.
    fn re_word_find(&self, re: &Regex, line: &[u8], from: usize) -> Option<(usize, usize)> {
        let utf8 = self.f.utf8;
        let m = re.find_at_bytes(line, from)?;
        let (mut s, mut len) = (m.begin, m.end - m.begin);
        let mut ends: Option<(usize, Vec<usize>)> = None;
        loop {
            if !wordchar_at(line, s + len, utf8) && !wordchar_before(line, s, utf8) {
                return Some((s, len));
            }
            let mut shorter = 0usize;
            if len > 0 {
                len -= 1;
                if ends.as_ref().map_or(true, |(at, _)| *at != s) {
                    ends = Some((s, re.match_ends_at_bytes(line, s)));
                }
                let list = &ends.as_ref().expect("set").1;
                if let Some(e) = list.iter().copied().find(|&e| e <= s + len) {
                    shorter = e - s;
                }
            }
            if shorter > 0 {
                len = shorter;
                continue;
            }
            if s >= line.len() {
                return None;
            }
            let next = self.next_char(line, s);
            let m = re.find_at_bytes(line, next)?;
            s = m.begin;
            len = m.end - m.begin;
        }
    }
}

fn line_start_of(buf: &[u8], from: usize, at: usize, eol: u8) -> usize {
    match memrchr(eol, &buf[from..at]) {
        Some(k) => from + k + 1,
        None => from,
    }
}

fn first_non_ascii(b: &[u8]) -> Option<usize> {
    let mut i = 0usize;
    while i + 8 <= b.len() {
        let w = u64::from_le_bytes(b[i..i + 8].try_into().expect("8"));
        if w & 0x8080_8080_8080_8080 != 0 {
            break;
        }
        i += 8;
    }
    b[i..].iter().position(|&c| c >= 0x80).map(|p| p + i)
}

// --- word constituents (searchutils.c wordchar: '_' or iswalnum) ---------

fn decode_at(line: &[u8], pos: usize) -> Option<(char, usize)> {
    let lead = *line.get(pos)?;
    let w = match lead {
        0x00..=0x7F => 1,
        0xC2..=0xDF => 2,
        0xE0..=0xEF => 3,
        0xF0..=0xF4 => 4,
        _ => return None,
    };
    let s = std::str::from_utf8(line.get(pos..pos + w)?).ok()?;
    s.chars().next().map(|c| (c, w))
}

fn is_word_char(c: char) -> bool {
    c == '_' || c.is_alphanumeric()
}

/// Is the character starting at `pos` a word constituent? (end of line:
/// no)
pub fn wordchar_at(line: &[u8], pos: usize, utf8: bool) -> bool {
    if pos >= line.len() {
        return false;
    }
    let b = line[pos];
    if b < 0x80 || !utf8 {
        return b.is_ascii_alphanumeric() || b == b'_';
    }
    decode_at(line, pos).map_or(false, |(c, _)| is_word_char(c))
}

/// Is the character ending at `pos` a word constituent? (line start: no)
pub fn wordchar_before(line: &[u8], pos: usize, utf8: bool) -> bool {
    if pos == 0 {
        return false;
    }
    let b = line[pos - 1];
    if b < 0x80 || !utf8 {
        return b.is_ascii_alphanumeric() || b == b'_';
    }
    for w in 2..=4usize {
        if pos >= w {
            if let Some((c, cw)) = decode_at(line, pos - w) {
                if cw == w {
                    return is_word_char(c);
                }
            }
        }
    }
    false
}

/// Byte length of the run of word constituents starting at `pos`.
fn wordchars_size(line: &[u8], pos: usize, utf8: bool) -> usize {
    let mut p = pos;
    while p < line.len() && wordchar_at(line, p, utf8) {
        p += if utf8 {
            decode_at(line, p).map_or(1, |(_, w)| w)
        } else {
            1
        };
    }
    p - pos
}

// --- compilation ----------------------------------------------------------

/// glibc regerror text for a winux-regex compile error (the messages GNU
/// grep prints come from re_compile_pattern, probed per class).
fn gnu_regex_message(e: &winux_regex::Error, pattern: &[u8]) -> &'static str {
    match e.kind() {
        ErrorKind::TrailingBackslash => "Trailing backslash",
        ErrorKind::UnmatchedOpen => "Unmatched ( or \\(",
        ErrorKind::UnmatchedClose => "Unmatched ) or \\)",
        ErrorKind::UnmatchedBracket => {
            // glibc parse_bracket_exp: END_OF_RE right after `[` / `[^`
            // is REG_BADPAT; later ends are REG_EBRACK.
            if unterminated_bracket_is_bare(pattern) {
                "Invalid regular expression"
            } else {
                "Unmatched [, [^, [:, [., or [="
            }
        }
        ErrorKind::InvalidInterval => {
            // REG_EBRACE only when no closing `\}` follows at all;
            // a closed but malformed body is REG_BADBR.
            let closed = pattern[e.offset().min(pattern.len())..]
                .windows(2)
                .any(|w| w == b"\\}");
            if e.message().starts_with("unmatched") && !closed {
                "Unmatched \\{"
            } else {
                "Invalid content of \\{\\}"
            }
        }
        ErrorKind::IntervalTooBig | ErrorKind::TooComplex => "Regular expression too big",
        ErrorKind::InvalidBackReference => "Invalid back reference",
        ErrorKind::InvalidClassName => "Invalid character class name",
        ErrorKind::InvalidCollation => "Invalid collation character",
        ErrorKind::InvalidRangeEnd => "Invalid range end",
        ErrorKind::WorkLimit => "Memory exhausted",
    }
}

/// Is the first unterminated bracket of `p` a bare `[` / `[^` at the end?
fn unterminated_bracket_is_bare(p: &[u8]) -> bool {
    let mut i = 0usize;
    while i < p.len() {
        match p[i] {
            b'\\' => i += 2,
            b'[' => {
                let (end, _, closed) = scan_bracket(p, i);
                if !closed {
                    let mut q = i + 1;
                    if p.get(q) == Some(&b'^') {
                        q += 1;
                    }
                    return q >= p.len();
                }
                i = end;
            }
            _ => i += 1,
        }
    }
    false
}

/// Metacharacter-free pattern: the regex IS its byte string.
fn is_plain_literal(m: Matcher, p: &[u8]) -> bool {
    p.iter().all(|&b| match b {
        b'\\' | b'[' | b'.' | b'*' | b'^' | b'$' => false,
        b'+' | b'?' | b'{' | b'}' | b'(' | b')' | b'|' => m != Matcher::Extended,
        _ => true,
    })
}

/// dfa.c lexer pass after a successful regex compile: ERE "X at start of
/// expression" warnings and the confusing-bracket error, in pattern order.
/// Returns false after printing the fatal confusing-bracket error.
fn dfa_lex_pass(m: Matcher, patterns: &[&[u8]], ui: &mut Ui) -> bool {
    let ere = m == Matcher::Extended;
    for p in patterns {
        let mut laststart = true;
        let mut i = 0usize;
        while i < p.len() {
            let c = p[i];
            match c {
                b'\\' => {
                    let d = p.get(i + 1).copied().unwrap_or(0);
                    i += 2;
                    match d {
                        b'<' | b'>' | b'b' | b'B' | b'`' | b'\'' => {}
                        b'(' | b'|' if !ere => laststart = true,
                        b'{' if !ere => {
                            // BRE interval: consumed by the lexer as REPMN
                            if let Some(len) = interval_len(p, i, true) {
                                i += len;
                            }
                            laststart = false;
                        }
                        _ => laststart = false,
                    }
                    continue;
                }
                b'[' => {
                    let (end, confusing, _) = scan_bracket(p, i);
                    if confusing {
                        ui.error("character class syntax is [[:space:]], not [:space:]");
                        return false;
                    }
                    i = end;
                    laststart = false;
                    continue;
                }
                b'(' | b'|' if ere => laststart = true,
                b'*' if ere && laststart => ui.error("warning: * at start of expression"),
                b'+' if ere && laststart => ui.error("warning: + at start of expression"),
                b'?' if ere && laststart => ui.error("warning: ? at start of expression"),
                b'*' | b'+' | b'?' if ere => {}
                b'*' => {
                    if !laststart {
                        // BRE star after an atom: STAR, laststart unchanged
                    } else {
                        laststart = false; // BRE leading star is a literal
                    }
                }
                b'^' | b'$' => {}
                b'{' if ere => match interval_len(p, i + 1, false) {
                    Some(len) => {
                        if laststart {
                            ui.error("warning: {...} at start of expression");
                        }
                        laststart = false;
                        i += 1 + len;
                        continue;
                    }
                    None => laststart = false,
                },
                _ => laststart = false,
            }
            i += 1;
        }
    }
    true
}

/// Length of a valid interval body starting at `p[at]` (after the `{`),
/// including the closing `}` / `\}`; None when invalid (ERE: then `{` is
/// an ordinary character).
fn interval_len(p: &[u8], at: usize, bre: bool) -> Option<usize> {
    let mut q = at;
    let mut min: Option<u64> = None;
    while q < p.len() && p[q].is_ascii_digit() {
        min = Some(min.unwrap_or(0) * 10 + (p[q] - b'0') as u64);
        q += 1;
    }
    let mut max = min;
    if q < p.len() && p[q] == b',' {
        q += 1;
        max = None;
        let mut mx: Option<u64> = None;
        while q < p.len() && p[q].is_ascii_digit() {
            mx = Some(mx.unwrap_or(0) * 10 + (p[q] - b'0') as u64);
            q += 1;
        }
        if mx.is_some() {
            max = mx;
        }
        if min.is_none() {
            min = Some(0);
        }
    }
    if bre {
        if q < p.len() && p[q] == b'\\' {
            q += 1;
        } else {
            return None;
        }
    }
    if q < p.len() && p[q] == b'}' {
        q += 1;
    } else {
        return None;
    }
    let min = min?;
    if let Some(mx) = max {
        if min > mx {
            return None;
        }
    }
    Some(q - at)
}

/// Scan a bracket expression at `p[at] == '['`: returns the index after
/// its `]`, whether dfa.c's colon_warning_state reached 7 ("[:x:]"), and
/// whether the bracket was terminated.
fn scan_bracket(p: &[u8], at: usize) -> (usize, bool, bool) {
    let mut i = at + 1;
    if i < p.len() && p[i] == b'^' {
        i += 1;
    }
    let mut state: u8 = (p.get(i) == Some(&b':')) as u8;
    let mut first = true;
    while i < p.len() {
        let c = p[i];
        if c == b']' && !first {
            return (i + 1, state == 7, true);
        }
        first = false;
        state &= !2;
        if c == b'[' && matches!(p.get(i + 1), Some(b':') | Some(b'.') | Some(b'=')) {
            let t = p[i + 1];
            let mut j = i + 2;
            while j + 1 < p.len() && !(p[j] == t && p[j + 1] == b']') {
                j += 1;
            }
            state |= 8;
            i = (j + 2).min(p.len());
            continue;
        }
        if p.get(i + 1) == Some(&b'-') && p.get(i + 2).map_or(false, |&n| n != b']') {
            state |= 8;
            i += 3;
            continue;
        }
        state |= if c == b':' { 2 } else { 4 };
        i += 1;
    }
    (p.len(), false, false)
}

/// GEAcompile / Fcompile: compile the deduplicated pattern list. Errors
/// are printed here; Err(()) means exit with status 2.
pub fn compile(
    matcher: Matcher,
    raw: &[RawPattern],
    f: MatchFlags,
    ui: &mut Ui,
) -> Result<PatternMatcher, ()> {
    let lines: Vec<&[u8]> = raw.iter().map(|r| r.bytes.as_slice()).collect();
    let literal = matcher == Matcher::Fixed || lines.iter().all(|p| is_plain_literal(matcher, p));
    let pats = if literal {
        Pats::Lits(LitSet::new(lines.iter().map(|p| p.to_vec()).collect(), f.ignore_case))
    } else {
        let syntax = if matcher == Matcher::Extended {
            Syntax::Extended
        } else {
            Syntax::Basic
        };
        let mut res = Vec::with_capacity(raw.len());
        let mut failed = false;
        for r in raw {
            match Regex::compile_bytes_ignore_case(syntax, &r.bytes, f.ignore_case) {
                Ok(re) => res.push(re),
                Err(e) => {
                    failed = true;
                    let msg = gnu_regex_message(&e, &r.bytes);
                    match &r.origin {
                        Some((file, n)) => ui.error(&format!("{file}:{n}: {msg}")),
                        None => ui.error(msg),
                    }
                }
            }
        }
        if failed {
            return Err(());
        }
        if !dfa_lex_pass(matcher, &lines, ui) {
            return Err(());
        }
        Pats::Res(res)
    };
    if literal && matcher != Matcher::Fixed && !dfa_lex_pass(matcher, &lines, ui) {
        return Err(());
    }

    let has_empty = lines.iter().any(|p| p.is_empty());
    let match_all = has_empty && !f.words && !f.lines;
    let mut finder = None;
    let mut dfa = None;
    let mut fast_exact = true;
    if !match_all {
        let mode = if f.lines {
            LineMode::Entire
        } else {
            LineMode::Contains
        };
        // -w: the fast paths only find candidate lines
        fast_exact = !f.words;
        match &pats {
            Pats::Lits(set) => {
                if set.lits.len() == 1 && !f.ignore_case && !f.lines && !set.lits[0].contains(&f.eol)
                {
                    finder = Some(Finder::new(&set.lits[0]));
                } else if let Some(node) = set.trie() {
                    dfa = LineDfa::from_nodes(&[(&node, f.ignore_case)], mode, f.eol);
                }
            }
            Pats::Res(res) => {
                let refs: Vec<&Regex> = res.iter().collect();
                dfa = LineDfa::new(&refs, mode, f.eol);
            }
        }
    }
    let req = dfa
        .as_ref()
        .map_or(InputRequirement::Any, |d| d.input_requirement());
    Ok(PatternMatcher {
        pats,
        f,
        match_all,
        finder,
        dfa,
        req,
        fast_exact,
        good_from: usize::MAX,
        good_end: 0,
    })
}
