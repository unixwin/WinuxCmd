//! Lazy line DFA — the "does this line match?" accelerator for grep-class
//! consumers (WinuxCmd #1488 grep lane).
//!
//! GNU grep answers line selection with a DFA (`dfa.c`) and only falls back
//! to the backtracking `regexec` when a pattern has back-references or the
//! caller needs match spans (`-o`, `--color`). This module is the same split
//! for winux-regex: it compiles the SAME parsed node tree the interpreter
//! runs on into a Thompson NFA and determinizes it lazily (subset
//! construction, cached per (state, byte class)), so selection costs one
//! table lookup per input byte instead of an interpreter attempt per
//! position.
//!
//! # Equivalence contract
//!
//! The DFA decides exactly what the interpreter decides:
//!
//! * [`LineMode::Contains`] — `Regex::find_at_bytes(line, 0).is_some()` for
//!   at least one of the regexes;
//! * [`LineMode::Entire`] — `Regex::matches_entire_bytes(line)` for at least
//!   one of them.
//!
//! The per-node rules are reused, not re-derived: literals and bracket
//! expressions go through the engine's own `chars_equal` / `class_matches`
//! (so `-i` folding is identical), anchors are absolute line ends, and the
//! unanchored scan attempts exactly the interpreter's positions (position 0
//! and every later position that is not a UTF-8 continuation byte, the
//! `scan_positions` advance rule) through a `next-is-not-a-continuation`
//! lookahead.
//!
//! Two node kinds depend on the input's UTF-8 shape: `.` advances a whole
//! codepoint, which is an exact byte rule on well-formed UTF-8
//! (`std::str::from_utf8`: a lead byte always carries its continuations);
//! the word assertions (`\< \> \b \B`) classify codepoints, and because
//! a single-byte bracket can stop in the MIDDLE of a codepoint, where the
//! interpreter's classification of a continuation byte depends on how far
//! back the lead is, they are byte rules only on ASCII lines.
//! [`LineDfa::input_requirement`] reports which lines the DFA is exact on;
//! the caller routes the others to the interpreter. Patterns without `.`
//! and word assertions are exact on arbitrary bytes.
//!
//! Back-references make the language non-regular: [`LineDfa::new`] returns
//! `None` for them (and for NFAs above a size cap), and the caller keeps the
//! interpreter. The same `None` is returned for the dialect-only node kinds
//! of [`crate::CompileOptions`] (sed `M` line anchors, `` \` ``/`\'` buffer
//! anchors, the restricted `.`): this model's `eol` already delimits lines
//! and it carries no buffer-anchor state, so those stay on the interpreter.
//!
//! # Scanning model
//!
//! Lines are terminated by the `eol` byte given at construction (`\n`, or
//! `\0` for `grep -z`); the eol byte never occurs inside a line, so it is
//! excluded from every byte set and handled as its own byte class whose
//! transition either reports the match (end-of-line acceptance) or restarts
//! at the start state. [`LineDfa::find_line`] therefore scans a whole buffer
//! of lines in one pass, which is what makes buffer-at-a-time selection
//! (GNU `grepbuf`) cheap.

use std::collections::HashMap;

use crate::ast::Node;
use crate::matcher::{chars_equal, class_matches};
use crate::Regex;

/// The input lines on which a [`LineDfa`] is exact.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum InputRequirement {
    /// Any bytes.
    Any,
    /// Well-formed UTF-8 lines (`.` present).
    ValidUtf8,
    /// ASCII-only lines (word assertions present: mid-codepoint positions,
    /// reachable after a single-byte bracket consumed a lead byte, classify
    /// continuation bytes by the interpreter's ill-formed-sequence rule).
    Ascii,
}

/// Which question the DFA answers per line.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LineMode {
    /// Some regex matches somewhere in the line (`find_at_bytes(line, 0)`).
    Contains,
    /// Some regex matches the whole line (`matches_entire_bytes(line)`).
    Entire,
}

/// NFA size cap: bigger automata (huge `{m,n}` expansions) stay on the
/// interpreter.
const MAX_NFA_STATES: usize = 40_000;
/// Cached DFA states before the cache is flushed and rebuilt lazily.
const MAX_DFA_STATES: usize = 6_000;

/// 256-bit byte set.
#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
struct ByteSet([u64; 4]);

impl ByteSet {
    const EMPTY: ByteSet = ByteSet([0; 4]);

    fn insert(&mut self, b: u8) {
        self.0[(b >> 6) as usize] |= 1u64 << (b & 63);
    }

    fn contains(&self, b: u8) -> bool {
        self.0[(b >> 6) as usize] & (1u64 << (b & 63)) != 0
    }

    fn from_fn(eol: u8, f: impl Fn(u8) -> bool) -> ByteSet {
        let mut s = ByteSet::EMPTY;
        for b in 0..=255u8 {
            if b != eol && f(b) {
                s.insert(b);
            }
        }
        s
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Assert {
    /// `^`: position 0.
    LineStart,
    /// `$`: end of line.
    LineEnd,
    /// `\<`
    WordStart,
    /// `\>`
    WordEnd,
    /// `\b`
    WordBoundary,
    /// `\B`
    NotWordBoundary,
    /// Scan-position rule: end of line, or the next byte is not a UTF-8
    /// continuation byte.
    NextNotContinuation,
}

#[derive(Clone, Debug)]
enum NState {
    Bytes { set: u32, next: u32 },
    Eps(Vec<u32>),
    Assert(Assert, u32),
    Match,
}

struct Nfa {
    states: Vec<NState>,
    sets: Vec<ByteSet>,
    set_ids: HashMap<ByteSet, u32>,
    eol: u8,
    uses_prev_start: bool,
    uses_word: bool,
    has_dot: bool,
    too_big: bool,
}

impl Nfa {
    fn push(&mut self, st: NState) -> u32 {
        if self.states.len() >= MAX_NFA_STATES {
            self.too_big = true;
        }
        self.states.push(st);
        (self.states.len() - 1) as u32
    }

    fn set_id(&mut self, set: ByteSet) -> u32 {
        if let Some(id) = self.set_ids.get(&set) {
            return *id;
        }
        let id = self.sets.len() as u32;
        self.sets.push(set);
        self.set_ids.insert(set, id);
        id
    }

    fn bytes(&mut self, set: ByteSet, next: u32) -> u32 {
        let set = self.set_id(set);
        self.push(NState::Bytes { set, next })
    }

    /// Build the fragment for `node` that continues to `next`; returns the
    /// entry state. `None` = unsupported (back-reference) or too big.
    fn build(&mut self, node: &Node, ic: bool, next: u32) -> Option<u32> {
        if self.too_big {
            return None;
        }
        let eol = self.eol;
        let id = match node {
            Node::Empty => next,
            Node::Literal(c) => {
                let c = *c;
                self.bytes(ByteSet::from_fn(eol, |b| chars_equal(ic, b, c)), next)
            }
            Node::Dot => {
                // The engine's `.` width rule restricted to well-formed
                // UTF-8 (see the module contract): a lead byte consumes its
                // continuations, every other byte is a single unit.
                self.has_dot = true;
                let cont = ByteSet::from_fn(eol, |b| (0x80..=0xBF).contains(&b));
                let c1 = self.bytes(cont, next);
                let c2a = self.bytes(cont, next);
                let c2 = self.bytes(cont, c2a);
                let c3b = self.bytes(cont, next);
                let c3a = self.bytes(cont, c3b);
                let c3 = self.bytes(cont, c3a);
                let single = self.bytes(ByteSet::from_fn(eol, |b| b < 0xC0 || b >= 0xF8), next);
                let lead2 = self.bytes(ByteSet::from_fn(eol, |b| (0xC0..=0xDF).contains(&b)), c1);
                let lead3 = self.bytes(ByteSet::from_fn(eol, |b| (0xE0..=0xEF).contains(&b)), c2);
                let lead4 = self.bytes(ByteSet::from_fn(eol, |b| (0xF0..=0xF7).contains(&b)), c3);
                self.push(NState::Eps(vec![single, lead2, lead3, lead4]))
            }
            Node::AnchorStart => {
                self.uses_prev_start = true;
                self.push(NState::Assert(Assert::LineStart, next))
            }
            Node::AnchorEnd => self.push(NState::Assert(Assert::LineEnd, next)),
            Node::WordStart => {
                self.uses_word = true;
                self.push(NState::Assert(Assert::WordStart, next))
            }
            Node::WordEnd => {
                self.uses_word = true;
                self.push(NState::Assert(Assert::WordEnd, next))
            }
            Node::WordBoundary => {
                self.uses_word = true;
                self.push(NState::Assert(Assert::WordBoundary, next))
            }
            Node::NotWordBoundary => {
                self.uses_word = true;
                self.push(NState::Assert(Assert::NotWordBoundary, next))
            }
            Node::CharClass { negated, atoms } => {
                let set = ByteSet::from_fn(eol, |b| class_matches(ic, *negated, atoms, b));
                self.bytes(set, next)
            }
            Node::Sequence(children) => {
                let mut cur = next;
                for child in children.iter().rev() {
                    cur = self.build(child, ic, cur)?;
                }
                cur
            }
            Node::Alternation(children) => {
                let mut entries = Vec::with_capacity(children.len());
                for child in children {
                    entries.push(self.build(child, ic, next)?);
                }
                self.push(NState::Eps(entries))
            }
            Node::Capture { child, .. } => self.build(child, ic, next)?,
            Node::BackReference(_) => return None,
            // Dialect-only nodes (sed `M` line anchors, `` \` ``/`\'` buffer
            // anchors, the restricted `.`) are not part of this line-DFA
            // model: its eol byte already delimits lines and it has no
            // buffer-anchor state. Unsupported here means "keep the
            // interpreter", so the answer stays exact.
            Node::LineStart(_)
            | Node::LineEnd(_)
            | Node::BufferStart
            | Node::BufferEnd
            | Node::DotRestricted { .. } => return None,
            Node::Repeat { min, max, child } => {
                // The interpreter ends a repeat at the first EMPTY
                // iteration and lets the remaining copies count as empty;
                // when the body can match empty only in some contexts
                // (an assertion-gated alternative such as `(^|\w){2}`) that
                // rule reaches fewer positions than the regular language
                // (GNU matches `_ ` against `(^|\w){2} +`; the interpreter
                // does not). Keep such counted repeats on the interpreter so
                // the two paths never disagree.
                let counted = *min > 1 || (max.is_some() && *max != Some(*min));
                if counted && nullable(child) && has_assertion(child) {
                    return None;
                }
                let min = *min as usize;
                let mut cur = match max {
                    None => {
                        // child* : L = Eps[child -> L, next]
                        let l = self.push(NState::Eps(Vec::new()));
                        let body = self.build(child, ic, l)?;
                        self.states[l as usize] = NState::Eps(vec![body, next]);
                        l
                    }
                    Some(max) => {
                        let max = *max as usize;
                        let mut cur = next;
                        for _ in min..max.max(min) {
                            let body = self.build(child, ic, cur)?;
                            cur = self.push(NState::Eps(vec![body, next]));
                            if self.too_big {
                                return None;
                            }
                        }
                        cur
                    }
                };
                for _ in 0..min {
                    cur = self.build(child, ic, cur)?;
                    if self.too_big {
                        return None;
                    }
                }
                cur
            }
        };
        if self.too_big {
            None
        } else {
            Some(id)
        }
    }
}

/// Can `node` match the empty string (assertions counted as empty)?
fn nullable(node: &Node) -> bool {
    match node {
        Node::Empty
        | Node::AnchorStart
        | Node::AnchorEnd
        | Node::LineStart(_)
        | Node::LineEnd(_)
        | Node::BufferStart
        | Node::BufferEnd
        | Node::WordStart
        | Node::WordEnd
        | Node::WordBoundary
        | Node::NotWordBoundary => true,
        Node::Literal(_) | Node::Dot | Node::DotRestricted { .. } | Node::CharClass { .. } => false,
        Node::BackReference(_) => true,
        Node::Sequence(c) => c.iter().all(nullable),
        Node::Alternation(c) => c.iter().any(nullable),
        Node::Capture { child, .. } => nullable(child),
        Node::Repeat { min, child, .. } => *min == 0 || nullable(child),
    }
}

/// Does `node` contain a zero-width assertion?
fn has_assertion(node: &Node) -> bool {
    match node {
        Node::AnchorStart
        | Node::AnchorEnd
        | Node::LineStart(_)
        | Node::LineEnd(_)
        | Node::BufferStart
        | Node::BufferEnd
        | Node::WordStart
        | Node::WordEnd
        | Node::WordBoundary
        | Node::NotWordBoundary => true,
        Node::Sequence(c) | Node::Alternation(c) => c.iter().any(has_assertion),
        Node::Capture { child, .. } | Node::Repeat { child, .. } => has_assertion(child),
        _ => false,
    }
}

/// Look-behind context of a DFA state.
const PREV_START: u8 = 0;
const PREV_WORD: u8 = 1;
const PREV_OTHER: u8 = 2;

/// Special premultiplied state ids.
const DEAD: u32 = 0;
const UNKNOWN: u32 = u32::MAX;

#[derive(Clone, PartialEq, Eq, Hash)]
struct Key {
    kernel: Box<[u32]>,
    prev: u8,
}

/// A lazily determinized line DFA over one or more regexes. See the module
/// documentation for the equivalence contract.
pub struct LineDfa {
    nfa: Nfa,
    start_kernel: Vec<u32>,
    eol: u8,
    /// byte -> class id
    classes: [u8; 256],
    /// one representative byte per class
    reps: Vec<u8>,
    stride: usize,
    /// transitions, premultiplied by `stride`; index = state + class
    table: Vec<u32>,
    keys: Vec<Key>,
    ids: HashMap<Key, u32>,
    start: u32,
    match_id: u32,
    // scratch
    visited: Vec<u32>,
    generation: u32,
    stack: Vec<u32>,
    flushes: usize,
}

impl std::fmt::Debug for LineDfa {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("LineDfa")
            .field("nfa_states", &self.nfa.states.len())
            .field("classes", &self.stride)
            .field("dfa_states", &self.keys.len())
            .finish()
    }
}

impl LineDfa {
    /// Build a DFA over `regexes` (an empty slice matches nothing). `None`
    /// when a regex holds a back-reference or the automaton is too big.
    pub fn new(regexes: &[&Regex], mode: LineMode, eol: u8) -> Option<LineDfa> {
        let nodes: Vec<(&Node, bool)> = regexes
            .iter()
            .map(|r| (r.root(), r.ignore_case()))
            .collect();
        Self::from_nodes(&nodes, mode, eol)
    }

    /// Build from raw node trees, each with its `-i` flag (grep builds its
    /// `-F` literal sets this way). Same contract as [`LineDfa::new`]
    /// applied to regexes with those roots and flags.
    pub fn from_nodes(nodes: &[(&Node, bool)], mode: LineMode, eol: u8) -> Option<LineDfa> {
        let mut nfa = Nfa {
            states: Vec::new(),
            sets: Vec::new(),
            set_ids: HashMap::new(),
            eol,
            uses_prev_start: false,
            uses_word: false,
            has_dot: false,
            too_big: false,
        };
        let m = nfa.push(NState::Match);
        let accept = match mode {
            LineMode::Contains => m,
            LineMode::Entire => nfa.push(NState::Assert(Assert::LineEnd, m)),
        };
        let mut entries = Vec::with_capacity(nodes.len());
        for (node, ic) in nodes {
            entries.push(nfa.build(node, *ic, accept)?);
        }
        let p0 = nfa.push(NState::Eps(entries));
        let start_kernel = match mode {
            LineMode::Entire => vec![p0],
            LineMode::Contains => {
                // Unanchored scan: attempt at 0, then after each consumed
                // byte attempt again iff the next byte is not a UTF-8
                // continuation (the interpreter's scan_positions rule).
                let lc = nfa.push(NState::Eps(Vec::new()));
                let any = ByteSet::from_fn(eol, |_| true);
                let lb = nfa.bytes(any, lc);
                let attempt = nfa.push(NState::Assert(Assert::NextNotContinuation, p0));
                nfa.states[lc as usize] = NState::Eps(vec![attempt, lb]);
                vec![p0, lb]
            }
        };
        if nfa.too_big {
            return None;
        }

        // Byte classes: refine by every byte set the NFA tests plus the
        // context predicates the assertions read.
        let mut signature: Vec<Vec<bool>> = vec![Vec::new(); 256];
        let mut extra: Vec<ByteSet> = nfa.sets.clone();
        extra.push(ByteSet::from_fn(eol, |b| (0x80..=0xBF).contains(&b)));
        if nfa.uses_word {
            extra.push(ByteSet::from_fn(eol, word_byte));
        }
        for (b, sig) in signature.iter_mut().enumerate() {
            let b = b as u8;
            sig.push(b == eol);
            for s in &extra {
                sig.push(s.contains(b));
            }
        }
        let mut classes = [0u8; 256];
        let mut reps: Vec<u8> = Vec::new();
        let mut seen: HashMap<Vec<bool>, u8> = HashMap::new();
        for b in 0..256usize {
            let sig = std::mem::take(&mut signature[b]);
            let next_id = seen.len();
            let id = *seen.entry(sig).or_insert_with(|| {
                reps.push(b as u8);
                next_id as u8
            });
            classes[b] = id;
        }
        let stride = reps.len();
        let nstates = nfa.states.len();
        let mut dfa = LineDfa {
            nfa,
            start_kernel,
            eol,
            classes,
            reps,
            stride,
            table: Vec::new(),
            keys: Vec::new(),
            ids: HashMap::new(),
            start: 0,
            match_id: 0,
            visited: vec![0; nstates],
            generation: 0,
            stack: Vec::new(),
            flushes: 0,
        };
        dfa.reset_cache();
        Some(dfa)
    }

    /// Which lines the DFA is exact on (see the module contract). Lines
    /// outside the class must be decided by the interpreter.
    pub fn input_requirement(&self) -> InputRequirement {
        if self.nfa.uses_word {
            InputRequirement::Ascii
        } else if self.nfa.has_dot {
            InputRequirement::ValidUtf8
        } else {
            InputRequirement::Any
        }
    }

    /// Number of times the state cache overflowed and was rebuilt
    /// (diagnostics for benchmarks).
    pub fn cache_flushes(&self) -> usize {
        self.flushes
    }

    fn reset_cache(&mut self) {
        self.table.clear();
        self.keys.clear();
        self.ids.clear();
        // id 0 = DEAD, id 1 = MATCH (both rows unused but allocated so ids
        // stay premultiplied indexes into the table).
        for _ in 0..2 {
            self.keys.push(Key {
                kernel: Box::new([]),
                prev: 255,
            });
            self.table
                .extend(std::iter::repeat(UNKNOWN).take(self.stride));
        }
        self.match_id = self.stride as u32;
        let key = Key {
            kernel: self.start_kernel.clone().into_boxed_slice(),
            prev: PREV_START,
        };
        self.start = self.intern(key);
    }

    fn intern(&mut self, key: Key) -> u32 {
        if let Some(id) = self.ids.get(&key) {
            return *id;
        }
        let id = (self.keys.len() * self.stride) as u32;
        self.keys.push(key.clone());
        self.ids.insert(key, id);
        self.table
            .extend(std::iter::repeat(UNKNOWN).take(self.stride));
        id
    }

    fn prev_ctx(&self, b: u8) -> u8 {
        if self.nfa.uses_word {
            if word_byte(b) {
                PREV_WORD
            } else {
                PREV_OTHER
            }
        } else if self.nfa.uses_prev_start {
            PREV_OTHER
        } else {
            PREV_START
        }
    }

    /// Compute (and cache) the transition of premultiplied state `s` on
    /// byte class `cls`.
    fn compute(&mut self, s: u32, cls: usize) -> u32 {
        let si = s as usize / self.stride;
        let key = self.keys[si].clone();
        let b = self.reps[cls];
        let at_end = b == self.eol;
        let prev = key.prev;
        let prev_word = prev == PREV_WORD;
        let next_word = !at_end && word_byte(b);
        let next_cont = !at_end && (0x80..=0xBF).contains(&b);

        self.generation = self.generation.wrapping_add(1);
        if self.generation == 0 {
            self.visited.iter_mut().for_each(|v| *v = 0);
            self.generation = 1;
        }
        let gen = self.generation;
        let mut matched = false;
        let mut kernel: Vec<u32> = Vec::new();
        self.stack.clear();
        for &k in key.kernel.iter() {
            self.stack.push(k);
        }
        while let Some(id) = self.stack.pop() {
            let idx = id as usize;
            if self.visited[idx] == gen {
                continue;
            }
            self.visited[idx] = gen;
            match &self.nfa.states[idx] {
                NState::Match => matched = true,
                NState::Eps(list) => {
                    for &n in list.iter().rev() {
                        self.stack.push(n);
                    }
                }
                NState::Assert(kind, n) => {
                    let ok = match kind {
                        Assert::LineStart => prev == PREV_START,
                        Assert::LineEnd => at_end,
                        Assert::WordStart => !prev_word && next_word,
                        Assert::WordEnd => prev_word && !next_word,
                        Assert::WordBoundary => prev_word != next_word,
                        Assert::NotWordBoundary => prev_word == next_word,
                        Assert::NextNotContinuation => !next_cont,
                    };
                    if ok {
                        self.stack.push(*n);
                    }
                }
                NState::Bytes { set, next } => {
                    if !at_end && self.nfa.sets[*set as usize].contains(b) {
                        kernel.push(*next);
                    }
                }
            }
        }
        let target = if matched {
            self.match_id
        } else if at_end {
            self.start
        } else if kernel.is_empty() {
            DEAD
        } else {
            kernel.sort_unstable();
            kernel.dedup();
            let key = Key {
                kernel: kernel.into_boxed_slice(),
                prev: self.prev_ctx(b),
            };
            if self.keys.len() >= MAX_DFA_STATES && !self.ids.contains_key(&key) {
                // Cache overflow: drop every cached state and continue
                // lazily from this one (correctness is unaffected; only the
                // memoized work is lost). The caller re-reads `s` through
                // the returned id, never through stale ids.
                self.flushes += 1;
                self.reset_cache();
                return self.intern(key) | FLUSHED_BIT;
            }
            self.intern(key)
        };
        self.table[s as usize + cls] = target;
        target
    }

    /// Find the first line of `buf[start..end]` that matches. `start` must
    /// be a line start and `buf[end - 1]` must be the eol byte (the region
    /// is a run of complete lines). Returns `(line_start, line_end)` where
    /// `line_end` indexes the line's eol byte.
    pub fn find_line(&mut self, buf: &[u8], start: usize, end: usize) -> Option<(usize, usize)> {
        debug_assert!(end == start || buf[end - 1] == self.eol);
        let mut s = self.start;
        let mut i = start;
        let mut line_start = start;
        let stride_special = 2 * self.stride as u32;
        while i < end {
            let b = buf[i];
            let mut n = self.table[s as usize + self.classes[b as usize] as usize];
            if n >= stride_special {
                if n != UNKNOWN {
                    // hot path: an ordinary cached transition
                    if b == self.eol {
                        line_start = i + 1;
                    }
                    s = n;
                    i += 1;
                    continue;
                }
                n = self.compute(s, self.classes[b as usize] as usize);
                if n & FLUSHED_BIT != 0 {
                    n &= !FLUSHED_BIT;
                }
                if n >= stride_special {
                    if b == self.eol {
                        line_start = i + 1;
                    }
                    s = n;
                    i += 1;
                    continue;
                }
            }
            if n == self.match_id {
                let line_end = if b == self.eol {
                    i
                } else {
                    i + memchr(self.eol, &buf[i..end]).unwrap_or(end - i - 1)
                };
                return Some((line_start, line_end));
            }
            // DEAD: no match possible on this line; skip to its end.
            match memchr(self.eol, &buf[i..end]) {
                Some(off) => {
                    i += off + 1;
                    line_start = i;
                    s = self.start;
                }
                None => return None,
            }
        }
        None
    }

    /// Does the single line `line` (without its eol byte) match?
    pub fn is_match_line(&mut self, line: &[u8]) -> bool {
        let mut s = self.start;
        let stride_special = 2 * self.stride as u32;
        let eol = self.eol;
        for &b in line.iter().chain(std::iter::once(&eol)) {
            let cls = self.classes[b as usize] as usize;
            let mut n = self.table[s as usize + cls];
            if n == UNKNOWN {
                n = self.compute(s, cls) & !FLUSHED_BIT;
            }
            if n == self.match_id {
                return true;
            }
            if n < stride_special {
                return false; // DEAD
            }
            s = n;
        }
        false
    }
}

/// Marker bit on `compute`'s return value: the cache was flushed while
/// computing (the returned id is valid in the NEW cache). State ids stay far
/// below 2^31 (MAX_DFA_STATES * 256).
const FLUSHED_BIT: u32 = 1 << 31;

/// The engine's word rule as a byte rule over well-formed UTF-8: ASCII
/// alnum/underscore, or any byte of a multi-byte codepoint.
fn word_byte(b: u8) -> bool {
    b.is_ascii_alphanumeric() || b == b'_' || b >= 0x80
}

/// Position of the first `needle` in `hay` (word-at-a-time scan).
fn memchr(needle: u8, hay: &[u8]) -> Option<usize> {
    const LO: u64 = 0x0101_0101_0101_0101;
    const HI: u64 = 0x8080_8080_8080_8080;
    let rep = LO.wrapping_mul(needle as u64);
    let mut i = 0usize;
    while i + 8 <= hay.len() {
        let w = u64::from_le_bytes(hay[i..i + 8].try_into().expect("8 bytes"));
        let x = w ^ rep;
        if (x.wrapping_sub(LO) & !x & HI) != 0 {
            break;
        }
        i += 8;
    }
    hay[i..].iter().position(|&b| b == needle).map(|p| p + i)
}
