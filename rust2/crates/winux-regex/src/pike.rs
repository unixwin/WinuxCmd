//! Thompson-NFA bounds search (Pike VM without submatches) — the fast path
//! for "is there a match" and "where is the leftmost-longest match", added
//! for the sed port (WinuxCmd #1488: `sed -n '/lorem.*amet/p'` over 64 MiB
//! took 22 s on the backtracking matcher, whose per-position state sets
//! clone capture vectors).
//!
//! Semantics are the backtracker's, by construction:
//!
//! * the same candidate start positions (`start`, then every later
//!   non-continuation byte; the fast-path pre-filters only skip futile
//!   ones), the same `.` width rule (`utf8_char_width_at`), the same
//!   class / literal / word-boundary predicates (shared functions);
//! * POSIX leftmost-longest: threads are deduplicated per (position,
//!   instruction) keeping the SMALLEST start — two threads at the same
//!   instruction and position have identical futures, so the smaller start
//!   dominates — and the winner is the smallest start, then the largest
//!   end.
//!
//! Patterns with backreferences, or whose interval expansion would exceed
//! [`MAX_INSTS`], have no NFA; callers fall back to the backtracker.
//! Capture spans are not computed here: `Regex::find_at_bytes_with` asks
//! the backtracker for the captures of the (already located) match only.

use crate::ast::Node;
use crate::matcher::{chars_equal, class_matches};
use crate::Regex;

const MAX_INSTS: usize = 20_000;
/// Ring size: a `.` step advances at most 4 bytes.
const SLOTS: usize = 5;

#[derive(Debug, Clone, Copy)]
enum Assert {
    Start,
    End,
    LineStart(u8),
    LineEnd(u8),
    WordStart,
    WordEnd,
    WordBoundary,
    NotWordBoundary,
}

#[derive(Debug, Clone, Copy)]
enum Inst {
    /// Consume one byte in set `.0`, continue at `.1`.
    Set(u32, u32),
    /// `.` (codepoint width), optional newline / invalid-byte exclusion.
    Dot {
        exclude_newline: bool,
        reject_invalid: bool,
        next: u32,
    },
    Assert(Assert, u32),
    Split(u32, u32),
    Match,
}

#[derive(Debug, Clone)]
pub(crate) struct Nfa {
    insts: Vec<Inst>,
    sets: Vec<[u64; 4]>,
    start: u32,
}

struct Builder {
    insts: Vec<Inst>,
    sets: Vec<[u64; 4]>,
    ignore_case: bool,
}

impl Builder {
    fn push(&mut self, i: Inst) -> Option<u32> {
        if self.insts.len() >= MAX_INSTS {
            return None;
        }
        self.insts.push(i);
        Some((self.insts.len() - 1) as u32)
    }

    fn set_of(&mut self, f: impl Fn(u8) -> bool) -> u32 {
        let mut s = [0u64; 4];
        for b in 0..=255u8 {
            if f(b) {
                s[(b >> 6) as usize] |= 1u64 << (b & 63);
            }
        }
        if let Some(i) = self.sets.iter().position(|x| *x == s) {
            return i as u32;
        }
        self.sets.push(s);
        (self.sets.len() - 1) as u32
    }

    /// Continuation-passing emit: returns the entry instruction of `node`
    /// that continues at `next`.
    fn emit(&mut self, node: &Node, next: u32) -> Option<u32> {
        let ic = self.ignore_case;
        Some(match node {
            Node::Empty => next,
            Node::Literal(c) => {
                let c = *c;
                let s = self.set_of(|b| chars_equal(ic, b, c));
                self.push(Inst::Set(s, next))?
            }
            Node::CharClass { negated, atoms } => {
                let s = self.set_of(|b| class_matches(ic, *negated, atoms, b));
                self.push(Inst::Set(s, next))?
            }
            Node::Dot => self.push(Inst::Dot {
                exclude_newline: false,
                reject_invalid: false,
                next,
            })?,
            Node::DotRestricted {
                exclude_newline,
                reject_invalid_utf8,
            } => self.push(Inst::Dot {
                exclude_newline: *exclude_newline,
                reject_invalid: *reject_invalid_utf8,
                next,
            })?,
            Node::AnchorStart | Node::BufferStart => {
                self.push(Inst::Assert(Assert::Start, next))?
            }
            Node::AnchorEnd | Node::BufferEnd => self.push(Inst::Assert(Assert::End, next))?,
            Node::LineStart(d) => self.push(Inst::Assert(Assert::LineStart(*d), next))?,
            Node::LineEnd(d) => self.push(Inst::Assert(Assert::LineEnd(*d), next))?,
            Node::WordStart => self.push(Inst::Assert(Assert::WordStart, next))?,
            Node::WordEnd => self.push(Inst::Assert(Assert::WordEnd, next))?,
            Node::WordBoundary => self.push(Inst::Assert(Assert::WordBoundary, next))?,
            Node::NotWordBoundary => self.push(Inst::Assert(Assert::NotWordBoundary, next))?,
            Node::Sequence(children) => {
                let mut cur = next;
                for c in children.iter().rev() {
                    cur = self.emit(c, cur)?;
                }
                cur
            }
            Node::Alternation(children) => {
                let mut entries = Vec::with_capacity(children.len());
                for c in children {
                    entries.push(self.emit(c, next)?);
                }
                let mut cur = *entries.last()?;
                for e in entries.iter().rev().skip(1) {
                    cur = self.push(Inst::Split(*e, cur))?;
                }
                cur
            }
            Node::Capture { child, .. } => self.emit(child, next)?,
            Node::BackReference(_) => return None,
            Node::Repeat { min, max, child } => {
                let mut cur = next;
                match max {
                    None => {
                        let l = self.push(Inst::Split(0, 0))?;
                        let body = self.emit(child, l)?;
                        self.insts[l as usize] = Inst::Split(body, next);
                        cur = l;
                    }
                    Some(m) => {
                        for _ in *min..*m {
                            let e = self.emit(child, cur)?;
                            cur = self.push(Inst::Split(e, cur))?;
                        }
                    }
                }
                for _ in 0..*min {
                    cur = self.emit(child, cur)?;
                }
                cur
            }
        })
    }
}

impl Nfa {
    pub(crate) fn build(root: &Node, ignore_case: bool) -> Option<Nfa> {
        let mut b = Builder {
            insts: Vec::new(),
            sets: Vec::new(),
            ignore_case,
        };
        let m = b.push(Inst::Match)?;
        let start = b.emit(root, m)?;
        Some(Nfa {
            insts: b.insts,
            sets: b.sets,
            start,
        })
    }

    fn in_set(&self, set: u32, b: u8) -> bool {
        self.sets[set as usize][(b >> 6) as usize] & (1u64 << (b & 63)) != 0
    }
}

/// Reusable scratch for [`Regex::find_at_bytes_with`] /
/// [`Regex::is_match_with`]: one per regex per thread of use (it is sized to
/// the regex's NFA on first use).
#[derive(Debug, Default, Clone)]
pub struct MatchCache {
    slots: Vec<Slot>,
    stack: Vec<u32>,
    gen: u32,
}

#[derive(Debug, Default, Clone)]
struct Slot {
    pos: usize,
    dense: Vec<u32>,
    sparse: Vec<u32>,
    start: Vec<usize>,
    vgen: Vec<u32>,
    vstart: Vec<usize>,
    gen: u32,
}

impl MatchCache {
    pub fn new() -> MatchCache {
        MatchCache::default()
    }

    fn prepare(&mut self, n: usize) {
        if self.slots.len() != SLOTS || self.slots[0].sparse.len() != n {
            self.slots = (0..SLOTS)
                .map(|_| Slot {
                    pos: usize::MAX,
                    dense: Vec::with_capacity(n),
                    sparse: vec![0; n],
                    start: vec![0; n],
                    vgen: vec![0; n],
                    vstart: vec![0; n],
                    gen: 0,
                })
                .collect();
            self.gen = 0;
        }
        for s in self.slots.iter_mut() {
            s.pos = usize::MAX;
            s.dense.clear();
        }
    }
}

fn check_assert(a: Assert, text: &[u8], pos: usize) -> bool {
    use crate::matcher::{is_word_cp_at, is_word_cp_before};
    match a {
        Assert::Start => pos == 0,
        Assert::End => pos == text.len(),
        Assert::LineStart(d) => pos == 0 || text[pos - 1] == d,
        Assert::LineEnd(d) => pos == text.len() || text[pos] == d,
        Assert::WordStart => is_word_cp_at(text, pos) && !is_word_cp_before(text, pos),
        Assert::WordEnd => is_word_cp_before(text, pos) && !is_word_cp_at(text, pos),
        Assert::WordBoundary => is_word_cp_before(text, pos) != is_word_cp_at(text, pos),
        Assert::NotWordBoundary => is_word_cp_before(text, pos) == is_word_cp_at(text, pos),
    }
}

impl Regex {
    /// Leftmost-longest bounds via the NFA; `None` inside `Some` = no
    /// match. Outer `None` = this regex has no NFA (backreferences).
    /// `first_only` stops at the first Match reached (existence test).
    pub(crate) fn nfa_search(
        &self,
        cache: &mut MatchCache,
        text: &[u8],
        start: usize,
        first_only: bool,
    ) -> Option<Option<(usize, usize)>> {
        let nfa = self.nfa.as_ref()?;
        if start > text.len() {
            return Some(None);
        }
        if self.fast.anchored && start > 0 {
            return Some(None);
        }
        let n = nfa.insts.len();
        cache.prepare(n);
        let len = text.len();
        let mut best: Option<(usize, usize)> = None;
        let mut p = start;
        loop {
            // ---- seed a new start thread at p? ----
            if best.is_none() && self.is_seed(text, start, p) {
                let gen = self.slot_for(cache, p);
                self.add_thread(nfa, cache, gen, text, p, nfa.start, p);
            }
            let si = p % SLOTS;
            if cache.slots[si].pos == p {
                // ---- step every thread at p ----
                let count = cache.slots[si].dense.len();
                let mut k = 0;
                while k < count {
                    let inst = cache.slots[si].dense[k];
                    let s = cache.slots[si].start[inst as usize];
                    k += 1;
                    if let Some((bs, _)) = best {
                        if s > bs {
                            continue;
                        }
                    }
                    match nfa.insts[inst as usize] {
                        Inst::Match => {
                            let better = match best {
                                None => true,
                                Some((bs, be)) => s < bs || (s == bs && p > be),
                            };
                            if better {
                                best = Some((s, p));
                            }
                            if first_only {
                                return Some(best);
                            }
                        }
                        Inst::Set(set, next) => {
                            if p < len && nfa.in_set(set, text[p]) {
                                self.slot_for(cache, p + 1);
                                let g = cache.slots[(p + 1) % SLOTS].gen;
                                self.add_thread(nfa, cache, g, text, p + 1, next, s);
                            }
                        }
                        Inst::Dot {
                            exclude_newline,
                            reject_invalid,
                            next,
                        } => {
                            if p < len && !(exclude_newline && text[p] == b'\n') {
                                let w = crate::matcher::utf8_char_width_at(text, p);
                                if !(reject_invalid && w == 1 && text[p] >= 0x80) {
                                    self.slot_for(cache, p + w);
                                    let g = cache.slots[(p + w) % SLOTS].gen;
                                    self.add_thread(nfa, cache, g, text, p + w, next, s);
                                }
                            }
                        }
                        Inst::Assert(..) | Inst::Split(..) => {}
                    }
                }
                cache.slots[si].pos = usize::MAX;
                cache.slots[si].dense.clear();
            }
            // ---- advance ----
            let alive = cache
                .slots
                .iter()
                .any(|s| s.pos != usize::MAX && s.pos > p && !s.dense.is_empty());
            if !alive {
                if best.is_some() {
                    return Some(best);
                }
                match self.next_seed(text, start, p + 1) {
                    Some(q) => p = q,
                    None => return Some(None),
                }
            } else {
                p += 1;
            }
            if p > len {
                return Some(best);
            }
        }
    }

    /// Make sure the ring slot for `pos` is initialized; returns its gen.
    fn slot_for(&self, cache: &mut MatchCache, pos: usize) -> u32 {
        let si = pos % SLOTS;
        if cache.slots[si].pos != pos {
            cache.gen = cache.gen.wrapping_add(1);
            if cache.gen == 0 {
                // generation wrap: clear the visit stamps
                for s in cache.slots.iter_mut() {
                    s.vgen.iter_mut().for_each(|g| *g = 0);
                }
                cache.gen = 1;
            }
            let g = cache.gen;
            let slot = &mut cache.slots[si];
            slot.pos = pos;
            slot.dense.clear();
            slot.gen = g;
        }
        cache.slots[si].gen
    }

    #[allow(clippy::too_many_arguments)]
    fn add_thread(
        &self,
        nfa: &Nfa,
        cache: &mut MatchCache,
        gen: u32,
        text: &[u8],
        pos: usize,
        inst: u32,
        s: usize,
    ) {
        let si = pos % SLOTS;
        let mut stack = std::mem::take(&mut cache.stack);
        stack.clear();
        stack.push(inst);
        while let Some(i) = stack.pop() {
            let iu = i as usize;
            {
                let slot = &mut cache.slots[si];
                if slot.vgen[iu] == gen && slot.vstart[iu] <= s {
                    continue;
                }
                slot.vgen[iu] = gen;
                slot.vstart[iu] = s;
            }
            match nfa.insts[iu] {
                Inst::Split(a, b) => {
                    stack.push(b);
                    stack.push(a);
                }
                Inst::Assert(a, next) => {
                    if check_assert(a, text, pos) {
                        stack.push(next);
                    }
                }
                Inst::Set(..) | Inst::Dot { .. } | Inst::Match => {
                    let slot = &mut cache.slots[si];
                    let d = slot.sparse[iu] as usize;
                    if d < slot.dense.len() && slot.dense[d] == i {
                        if s < slot.start[iu] {
                            slot.start[iu] = s;
                        }
                    } else {
                        slot.sparse[iu] = slot.dense.len() as u32;
                        slot.dense.push(i);
                        slot.start[iu] = s;
                    }
                }
            }
        }
        cache.stack = stack;
    }

    /// Is `p` one of the backtracker's attempt positions that the fast-path
    /// pre-filters keep?
    fn is_seed(&self, text: &[u8], start: usize, p: usize) -> bool {
        if p > text.len() {
            return false;
        }
        if p != start && p < text.len() && (0x80..0xC0).contains(&text[p]) {
            return false;
        }
        if self.fast.anchored && p != 0 {
            return false;
        }
        if !self.fast.prefix.is_empty() {
            return self.prefix_matches_at(text, p);
        }
        if !self.fast.first_any {
            return p < text.len() && self.fast.first[text[p] as usize];
        }
        true
    }

    /// Next seed position at or after `from` (no thread alive).
    fn next_seed(&self, text: &[u8], start: usize, from: usize) -> Option<usize> {
        if self.fast.anchored {
            return None;
        }
        if !self.fast.prefix.is_empty() {
            let mut q = from;
            while q <= text.len() {
                let hit = self.find_prefix_candidate(text, q)?;
                if self.is_seed(text, start, hit) {
                    return Some(hit);
                }
                q = hit + 1;
            }
            return None;
        }
        let mut q = from;
        while q <= text.len() {
            if self.is_seed(text, start, q) {
                return Some(q);
            }
            q += 1;
        }
        None
    }
}
