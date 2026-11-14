//! Matcher: POSIX leftmost-longest with glibc-aligned capture semantics.
//!
//! Ported from `src/utils/regex.cppm` `Pattern` (regex.cppm:630-1245). The
//! three semantic loads carried over verbatim:
//!
//! * leftmost-longest: every attempt enumerates the FULL set of end states
//!   and the best end position wins (GNU regexec order semantics, not
//!   leftmost-first).
//! * capture selection: exploration order (alternation branch order,
//!   first-occurrence state dedup) decides which equal-length parse keeps
//!   the registers, matching glibc set_regs/proceed_next_node; repeated
//!   groups undo a trailing empty iteration when the group already holds a
//!   non-empty match (glibc update_regs OP_CLOSE_SUBEXP empty branch,
//!   regex.cppm:1131-1153, WinuxCmd #1496).
//! * anti-blowup: repeat expansion memoizes per (iteration count, full
//!   state), collapsing the 2^n chunk decompositions of `\(.*\)*` to one
//!   visit per distinct state (regex.cppm:1155-1233, WinuxCmd #1495), with
//!   hard step/memo/output caps that degrade to a conservative no-match
//!   flagged as [`crate::ErrorKind::WorkLimit`] (the C++ engine's
//!   thread_local, moved into the return path per the 2.0 shape contract).
//!
//! Two structural departures from the C++ (same outputs, safer stacks):
//! `match_sequence` is an iterative left fold instead of per-index
//! recursion, and repeat expansion is an explicit-stack DFS instead of
//! per-iteration recursion, so a 32767-copy interval on a long line cannot
//! exhaust the stack.

use std::collections::HashSet;

use crate::ast::{ClassAtom, Node, PosixClass};
use crate::{Error, ErrorKind, Match, Regex, Submatch};

/// One parse thread: absolute position plus the capture registers.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
struct State {
    pos: usize,
    captures: Vec<Submatch>,
}

/// Memo key: (normalized iteration count, full state) — regex.cppm VisitKey.
#[derive(Clone, PartialEq, Eq, Hash)]
struct VisitKey {
    count: u32,
    state: State,
}

/// Per-call work budget (regex.cppm StepBudget).
struct StepBudget {
    steps: usize,
    exceeded: bool,
}

const STEP_LIMIT: usize = 1 << 26;
const VISITED_LIMIT: usize = 300_000;
const OUT_LIMIT: usize = 300_000;

impl StepBudget {
    fn new() -> StepBudget {
        StepBudget {
            steps: 0,
            exceeded: false,
        }
    }

    /// Charge one node visit.
    fn charge(&mut self) -> bool {
        self.steps += 1;
        if self.steps > STEP_LIMIT {
            self.exceeded = true;
        }
        self.exceeded
    }
}

// --- UTF-8 helpers (regex.cppm:65-123; D7: `.` and word boundaries move
// whole codepoints, never split a well-formed sequence) ------------------

fn utf8_continuation(b: u8) -> bool {
    b & 0xC0 == 0x80
}

fn utf8_char_width_at(text: &[u8], pos: usize) -> usize {
    let lead = match text.get(pos) {
        Some(b) => *b,
        None => return 0,
    };
    if lead < 0x80 {
        return 1;
    }
    let need = if lead & 0xE0 == 0xC0 {
        2
    } else if lead & 0xF0 == 0xE0 {
        3
    } else if lead & 0xF8 == 0xF0 {
        4
    } else {
        return 1; // invalid lead: treat as a single byte
    };
    if pos + need > text.len() {
        return 1;
    }
    for i in 1..need {
        if !utf8_continuation(text[pos + i]) {
            return 1;
        }
    }
    need
}

fn utf8_prev_char_start(text: &[u8], pos: usize) -> usize {
    if pos == 0 {
        return 0;
    }
    let mut i = pos - 1;
    let mut scanned = 0;
    while i > 0 && scanned < 3 && utf8_continuation(text[i]) {
        i -= 1;
        scanned += 1;
    }
    if i + utf8_char_width_at(text, i) == pos {
        i
    } else {
        pos - 1
    }
}

fn is_word_char(b: u8) -> bool {
    (b as char).is_ascii_alphanumeric() || b == b'_'
}

/// Word character at a boundary: multi-byte codepoints count as word
/// characters (regex.cppm:109-118).
fn is_word_cp_at(text: &[u8], pos: usize) -> bool {
    if pos >= text.len() {
        return false;
    }
    let mut start = pos;
    if utf8_continuation(text[pos]) {
        start = utf8_prev_char_start(text, pos);
    }
    let lead = text[start];
    if lead < 0x80 {
        is_word_char(lead)
    } else {
        utf8_char_width_at(text, start) > 1
    }
}

fn is_word_cp_before(text: &[u8], pos: usize) -> bool {
    if pos == 0 {
        return false;
    }
    is_word_cp_at(text, utf8_prev_char_start(text, pos))
}

fn fold_ascii(b: u8) -> u8 {
    b.to_ascii_lowercase()
}

/// The engine's literal comparison rule (`chars_equal`): folds ASCII under
/// ignore_case. Free function so the fast-path analysis in `fastpath.rs`
/// reuses the single copy of the rule.
pub(crate) fn chars_equal(ignore_case: bool, lhs: u8, rhs: u8) -> bool {
    if !ignore_case {
        lhs == rhs
    } else {
        fold_ascii(lhs) == fold_ascii(rhs)
    }
}

/// The engine's bracket-expression rule (`class_matches`): folds the byte
/// under ignore_case, tests atoms in order, negates at the end. POSIX
/// classes test the ORIGINAL byte and only the case classes cross-fold
/// under -i (GNU grep -i [[:upper:]] matches lowercase and vice versa).
/// Free function so the fast-path analysis reuses the single copy.
pub(crate) fn class_matches(ignore_case: bool, negated: bool, atoms: &[ClassAtom], b: u8) -> bool {
    let folded = if ignore_case { fold_ascii(b) } else { b };
    let mut matched = false;
    for atom in atoms {
        matched = match atom {
            ClassAtom::Literal(c) => folded == *c,
            ClassAtom::Range(lo, hi) => folded >= *lo && folded <= *hi,
            ClassAtom::Posix(class) => {
                let mut hit = class.matches(b);
                if !hit && ignore_case {
                    hit = match class {
                        PosixClass::Lower => (b as char).is_ascii_uppercase(),
                        PosixClass::Upper => (b as char).is_ascii_lowercase(),
                        _ => false,
                    };
                }
                hit
            }
        };
        if matched {
            break;
        }
    }
    if negated {
        !matched
    } else {
        matched
    }
}

fn work_limit() -> Error {
    Error::new(
        ErrorKind::WorkLimit,
        0,
        "work limit exceeded (conservative no-match)",
    )
}

/// Scan positions from `start`, skipping UTF-8 continuation bytes after each
/// attempted position (the C++ legacy traversal's advance rule).
fn scan_positions(text: &[u8], start: usize) -> impl Iterator<Item = usize> + '_ {
    let mut cursor = start;
    std::iter::from_fn(move || {
        if cursor > text.len() {
            return None;
        }
        let current = cursor;
        cursor += 1;
        while cursor < text.len() && utf8_continuation(text[cursor]) {
            cursor += 1;
        }
        Some(current)
    })
}

/// Highest-`pos` state, FIRST maximal kept on ties — identical to C++
/// max_element over `pos` with stable order (capture selection depends on
/// exploration order).
fn best_state(states: Vec<State>) -> Option<State> {
    states
        .into_iter()
        .fold(None, |best: Option<State>, st| match best {
            None => Some(st),
            Some(b) => Some(if st.pos > b.pos { st } else { b }),
        })
}

/// Dedup preserving first occurrences (regex.cppm unique_states).
fn unique_states(states: Vec<State>) -> Vec<State> {
    let mut out = Vec::with_capacity(states.len());
    let mut seen: HashSet<State> = HashSet::default();
    for st in states {
        if seen.insert(st.clone()) {
            out.push(st);
        }
    }
    out
}

fn push_unique(
    out: &mut Vec<State>,
    seen: &mut HashSet<VisitKey>,
    state: State,
    budget: &mut StepBudget,
) {
    if out.len() >= OUT_LIMIT {
        budget.exceeded = true;
        return;
    }
    let key = VisitKey {
        count: 0,
        state: state.clone(),
    };
    if seen.insert(key) {
        out.push(state);
    }
}

/// glibc update_regs (posix/regexec.c OP_CLOSE_SUBEXP empty branch): a
/// trailing empty iteration of a repeated group is undone whenever it
/// rewrote a capture that held a non-empty match (regex.cppm:1139-1153).
fn empty_iteration_undone(before: &State, after: &State) -> bool {
    let shared = before.captures.len().min(after.captures.len());
    for i in 1..shared {
        let prev = &before.captures[i];
        let next = &after.captures[i];
        if prev.matched
            && prev.end > prev.begin
            && (next.begin != prev.begin || next.end != prev.end || next.matched != prev.matched)
        {
            return true;
        }
    }
    false
}

// --- Regex match entry points -------------------------------------------

impl Regex {
    /// Leftmost match with `begin >= start` (byte text). This is the
    /// per-line scan primitive for grep/sed consumers.
    pub fn find_at_bytes(&self, text: &[u8], start: usize) -> Option<Match> {
        match self.checked_find_at_bytes(text, start) {
            Ok(m) => m,
            Err(_) => None,
        }
    }

    /// [`Regex::find_at_bytes`] over `&str`.
    pub fn find_at(&self, text: &str, start: usize) -> Option<Match> {
        self.find_at_bytes(text.as_bytes(), start)
    }

    /// Like [`Regex::find_at_bytes`], but a tripped work budget surfaces as
    /// [`ErrorKind::WorkLimit`] instead of collapsing to no-match.
    pub fn checked_find_at_bytes(&self, text: &[u8], start: usize) -> Result<Option<Match>, Error> {
        if start > text.len() {
            return Ok(None);
        }
        let mut budget = StepBudget::new();

        if self.fast.anchored {
            // Every possible match begins at absolute position 0, and state
            // positions are absolute: only the attempt at start == 0 can
            // succeed, and no attempt at all can when start > 0 (regex.cppm
            // find_first anchored branch).
            if start > 0 {
                return Ok(None);
            }
            let m = self.local_attempt(text, 0, &mut budget);
            if budget.exceeded {
                return Err(work_limit());
            }
            return Ok(m);
        }

        if self.fast.pure_literal {
            // The whole pattern is a literal run: a prefix candidate IS the
            // match, no interpreter attempt needed (regex.cppm pure_literal
            // branch). This is byte-identical to local_attempt's result for
            // a literal-only tree (spans and group 0; capture_count == 0).
            if let Some(pos) = self.find_prefix_candidate(text, start) {
                let end = pos + self.fast.prefix.len();
                return Ok(Some(Match {
                    begin: pos,
                    end,
                    captures: vec![Submatch {
                        begin: pos,
                        end,
                        matched: true,
                    }],
                }));
            }
            return Ok(None);
        }

        if !self.fast.prefix.is_empty() {
            // Jump between the positions the required literal prefix rules
            // in, instead of attempting the interpreter everywhere
            // (regex.cppm prefix branch). Candidates sit on legacy attempt
            // positions (see fastpath.rs), so results are unchanged; only
            // the futile attempts disappear.
            let mut cursor = start;
            while cursor <= text.len() {
                let Some(pos) = self.find_prefix_candidate(text, cursor) else {
                    return Ok(None);
                };
                let attempt = self.local_attempt(text, pos, &mut budget);
                if budget.exceeded {
                    return Err(work_limit());
                }
                if attempt.is_some() {
                    return Ok(attempt);
                }
                cursor = pos + 1;
            }
            return Ok(None);
        }

        if !self.fast.first_any {
            // Same traversal as the legacy loop below, but the interpreter
            // runs only where the first consumed byte may satisfy the
            // pattern (regex.cppm first branch). scan_positions keeps the
            // legacy advance rule; only the (futile) attempts are skipped.
            for cursor in scan_positions(text, start) {
                if cursor < text.len() && !self.fast.first[text[cursor] as usize] {
                    continue;
                }
                let attempt = self.local_attempt(text, cursor, &mut budget);
                if budget.exceeded {
                    return Err(work_limit());
                }
                if attempt.is_some() {
                    return Ok(attempt);
                }
            }
            return Ok(None);
        }

        for cursor in scan_positions(text, start) {
            let states = self.match_node(&self.root, text, self.initial_state(cursor), &mut budget);
            if budget.exceeded {
                return Err(work_limit());
            }
            if let Some(best) = best_state(states) {
                return Ok(Some(self.make_match(cursor, best)));
            }
        }
        Ok(None)
    }

    /// [`Regex::checked_find_at_bytes`] over `&str`.
    pub fn checked_find_at(&self, text: &str, start: usize) -> Result<Option<Match>, Error> {
        self.checked_find_at_bytes(text.as_bytes(), start)
    }

    /// Anchored attempt: the match must START exactly at `start` (byte
    /// text); among the parses anchored there the longest end wins (C++
    /// `Pattern::match_at`).
    pub fn match_at_bytes(&self, text: &[u8], start: usize) -> Option<Match> {
        match self.checked_match_at_bytes(text, start) {
            Ok(m) => m,
            Err(_) => None,
        }
    }

    /// [`Regex::match_at_bytes`] over `&str`.
    pub fn match_at(&self, text: &str, start: usize) -> Option<Match> {
        self.match_at_bytes(text.as_bytes(), start)
    }

    /// Like [`Regex::match_at_bytes`], but a tripped work budget surfaces as
    /// [`ErrorKind::WorkLimit`].
    pub fn checked_match_at_bytes(
        &self,
        text: &[u8],
        start: usize,
    ) -> Result<Option<Match>, Error> {
        if start > text.len() {
            return Ok(None);
        }
        // Necessary conditions for a match anchored at `start`; when they
        // fail up front the interpreter attempt cannot succeed either
        // (regex.cppm match_at fast_ rejection).
        if self.fast.anchored && start > 0 {
            return Ok(None);
        }
        if !self.fast.prefix.is_empty() && !self.prefix_matches_at(text, start) {
            return Ok(None);
        }
        let mut budget = StepBudget::new();
        let states = self.match_node(&self.root, text, self.initial_state(start), &mut budget);
        if budget.exceeded {
            return Err(work_limit());
        }
        Ok(best_state(states).map(|best| self.make_match(start, best)))
    }

    /// Every end position of a match anchored at `start`, longest first
    /// (deduplicated). This is the "try a shorter match at the same place"
    /// walk of GNU grep `-w` (EGexecute re-runs `re_match` with a shrinking
    /// limit); an empty result means no match starts at `start`. A tripped
    /// work budget yields the conservative empty answer, like
    /// [`Regex::match_at_bytes`].
    pub fn match_ends_at_bytes(&self, text: &[u8], start: usize) -> Vec<usize> {
        if start > text.len() {
            return Vec::new();
        }
        if self.fast.anchored && start > 0 {
            return Vec::new();
        }
        if !self.fast.prefix.is_empty() && !self.prefix_matches_at(text, start) {
            return Vec::new();
        }
        let mut budget = StepBudget::new();
        let states = self.match_node(&self.root, text, self.initial_state(start), &mut budget);
        if budget.exceeded {
            return Vec::new();
        }
        let mut ends: Vec<usize> = states.iter().map(|st| st.pos).collect();
        ends.sort_unstable_by(|a, b| b.cmp(a));
        ends.dedup();
        ends
    }

    /// [`Regex::checked_match_at_bytes`] over `&str`.
    pub fn checked_match_at(&self, text: &str, start: usize) -> Result<Option<Match>, Error> {
        self.checked_match_at_bytes(text.as_bytes(), start)
    }

    /// Whole-text match (C++ `matches_entire`; find `-regex` semantics).
    pub fn matches_entire_bytes(&self, text: &[u8]) -> bool {
        // A full match must begin with the required literal prefix
        // (regex.cppm matches_entire fast_ rejection).
        if !self.fast.prefix.is_empty() && !self.prefix_matches_at(text, 0) {
            return false;
        }
        let mut budget = StepBudget::new();
        let states = self.match_node(&self.root, text, self.initial_state(0), &mut budget);
        if budget.exceeded {
            return false;
        }
        states.iter().any(|st| st.pos == text.len())
    }

    /// [`Regex::matches_entire_bytes`] over `&str`.
    pub fn matches_entire(&self, text: &str) -> bool {
        self.matches_entire_bytes(text.as_bytes())
    }

    /// Successive non-overlapping matches (C++ `find_all` advance rule).
    pub fn find_all_bytes(&self, text: &[u8]) -> Vec<Match> {
        let mut matches = Vec::new();
        let mut cursor = 0usize;
        while cursor <= text.len() {
            let m = match self.find_at_bytes(text, cursor) {
                Some(m) => m,
                None => break,
            };
            cursor = if m.end > m.begin { m.end } else { m.begin + 1 };
            matches.push(m);
        }
        matches
    }

    /// [`Regex::find_all_bytes`] over `&str`.
    pub fn find_all(&self, text: &str) -> Vec<Match> {
        self.find_all_bytes(text.as_bytes())
    }

    fn initial_state(&self, pos: usize) -> State {
        State {
            pos,
            captures: vec![
                Submatch {
                    begin: 0,
                    end: 0,
                    matched: false
                };
                self.capture_count + 1
            ],
        }
    }

    /// One interpreter attempt anchored at `pos` — the exact body of the
    /// legacy find loop iteration (regex.cppm `local_attempt`). The fast
    /// paths funnel every candidate through here unchanged.
    fn local_attempt(&self, text: &[u8], pos: usize, budget: &mut StepBudget) -> Option<Match> {
        let states = self.match_node(&self.root, text, self.initial_state(pos), budget);
        if budget.exceeded {
            return None;
        }
        best_state(states).map(|best| self.make_match(pos, best))
    }

    fn make_match(&self, begin: usize, mut state: State) -> Match {
        state.captures[0] = Submatch {
            begin,
            end: state.pos,
            matched: true,
        };
        Match {
            begin,
            end: state.pos,
            captures: state.captures,
        }
    }

    /// Enumerate every end state reachable from `state` under `node`.
    fn match_node(
        &self,
        node: &Node,
        text: &[u8],
        state: State,
        budget: &mut StepBudget,
    ) -> Vec<State> {
        if budget.charge() {
            return Vec::new();
        }
        match node {
            Node::Empty => vec![state],
            Node::Literal(b) => {
                if state.pos < text.len() && chars_equal(self.ignore_case, text[state.pos], *b) {
                    vec![advance(state, 1)]
                } else {
                    Vec::new()
                }
            }
            Node::Dot => {
                if state.pos < text.len() {
                    let w = utf8_char_width_at(text, state.pos);
                    vec![advance(state, w)]
                } else {
                    Vec::new()
                }
            }
            Node::AnchorStart => {
                if state.pos == 0 {
                    vec![state]
                } else {
                    Vec::new()
                }
            }
            Node::AnchorEnd => {
                if state.pos == text.len() {
                    vec![state]
                } else {
                    Vec::new()
                }
            }
            Node::WordStart => {
                if !is_word_cp_at(text, state.pos) || is_word_cp_before(text, state.pos) {
                    Vec::new()
                } else {
                    vec![state]
                }
            }
            Node::WordEnd => {
                if !is_word_cp_before(text, state.pos) || is_word_cp_at(text, state.pos) {
                    Vec::new()
                } else {
                    vec![state]
                }
            }
            Node::WordBoundary => {
                if is_word_cp_before(text, state.pos) != is_word_cp_at(text, state.pos) {
                    vec![state]
                } else {
                    Vec::new()
                }
            }
            Node::NotWordBoundary => {
                if is_word_cp_before(text, state.pos) == is_word_cp_at(text, state.pos) {
                    vec![state]
                } else {
                    Vec::new()
                }
            }
            Node::CharClass { negated, atoms } => {
                if state.pos < text.len()
                    && class_matches(self.ignore_case, *negated, atoms, text[state.pos])
                {
                    vec![advance(state, 1)]
                } else {
                    Vec::new()
                }
            }
            Node::Sequence(children) => self.match_sequence(children, text, state, budget),
            Node::Alternation(children) => {
                let mut out = Vec::new();
                for child in children {
                    out.extend(self.match_node(child, text, state.clone(), budget));
                    if budget.exceeded {
                        return Vec::new();
                    }
                }
                unique_states(out)
            }
            Node::Capture { index, child } => {
                let entry = state.pos;
                let mut states = self.match_node(child, text, state, budget);
                for next in &mut states {
                    next.captures[*index] = Submatch {
                        begin: entry,
                        end: next.pos,
                        matched: true,
                    };
                }
                unique_states(states)
            }
            Node::BackReference(n) => self.match_back_reference(*n as usize, text, state),
            Node::Repeat { min, max, child } => {
                self.match_repeat(*min, *max, child, text, state, budget)
            }
        }
    }

    /// Iterative left fold over children — same result order as the C++
    /// per-index recursion (for each earlier state, its child states in
    /// order), without recursing per child.
    fn match_sequence(
        &self,
        children: &[Node],
        text: &[u8],
        state: State,
        budget: &mut StepBudget,
    ) -> Vec<State> {
        let mut states = vec![state];
        for child in children {
            let mut next_round = Vec::new();
            for st in &states {
                next_round.extend(self.match_node(child, text, st.clone(), budget));
                if budget.exceeded {
                    return Vec::new();
                }
            }
            states = unique_states(next_round);
        }
        states
    }

    fn match_back_reference(&self, index: usize, text: &[u8], state: State) -> Vec<State> {
        if index >= state.captures.len() {
            return Vec::new();
        }
        let capture = state.captures[index];
        if !capture.matched {
            return Vec::new();
        }
        let len = capture.end - capture.begin;
        if state.pos + len > text.len() {
            return Vec::new();
        }
        for i in 0..len {
            if !chars_equal(
                self.ignore_case,
                text[state.pos + i],
                text[capture.begin + i],
            ) {
                return Vec::new();
            }
        }
        vec![State {
            pos: state.pos + len,
            captures: state.captures,
        }]
    }

    /// Expand one `child{min,max}` node: the full set of reachable end
    /// states with glibc capture alignment. Iterative DFS mirror of C++
    /// collect_repeat (regex.cppm:1155-1233).
    fn match_repeat(
        &self,
        min: u32,
        max: Option<u32>,
        child: &Node,
        text: &[u8],
        state: State,
        budget: &mut StepBudget,
    ) -> Vec<State> {
        // A repeat subproblem is a pure function of (iterations completed,
        // full state); when {0,} every count-dependent branch is constant,
        // so the count is dropped from the memo key (regex.cppm:853-884).
        let count_free = min == 0 && max.is_none();
        let mut visited: HashSet<VisitKey> = HashSet::default();
        let mut out_seen: HashSet<VisitKey> = HashSet::default();
        let mut out: Vec<State> = Vec::new();

        // Explicit DFS stack; each frame mirrors one collect_repeat
        // activation: the entry state, the body's state set, the scan
        // cursor over it, and the first empty parse seen.
        let mut stack: Vec<RepeatFrame> = Vec::new();
        match self.enter_repeat_frame(
            text,
            state,
            0,
            count_free,
            min,
            max,
            child,
            budget,
            &mut visited,
            &mut stack,
        ) {
            Ok(true) => {}
            // Memo hit at the root, or the budget already blown: no states.
            _ => return Vec::new(),
        }
        while !stack.is_empty() {
            if budget.exceeded {
                return Vec::new();
            }
            // Bounded-max early exit (`\{m\}` with the count reached): emit
            // the entry state when the lower bound is met, expand nothing;
            // below the lower bound it is a silent return (C++ semantics).
            if stack[stack.len() - 1].bounded_exit {
                let frame = stack.pop().expect("checked above");
                if frame.bounded_emit {
                    push_unique(&mut out, &mut out_seen, frame.state, budget);
                    if budget.exceeded {
                        return Vec::new();
                    }
                }
                continue;
            }
            // Advance to the next child state of the top frame.
            enum Action {
                Recurse(State, u32),
                Exhausted,
            }
            let action = {
                let top = stack.len() - 1;
                let frame = &mut stack[top];
                let mut action = Action::Exhausted;
                while frame.idx < frame.child_states.len() {
                    let next = frame.child_states[frame.idx].clone();
                    frame.idx += 1;
                    if next.pos == frame.state.pos {
                        // POSIX/GNU: an iteration matching empty ends the
                        // loop; only advancing iterations recurse (the
                        // first empty parse is kept for exit selection).
                        if frame.empty_iteration.is_none() {
                            frame.empty_iteration = Some(next);
                        }
                        continue;
                    }
                    action = Action::Recurse(next, frame.count + 1);
                    break;
                }
                action
            };
            match action {
                Action::Recurse(next, count) => {
                    match self.enter_repeat_frame(
                        text,
                        next,
                        count,
                        count_free,
                        min,
                        max,
                        child,
                        budget,
                        &mut visited,
                        &mut stack,
                    ) {
                        Err(()) => return Vec::new(), // budget exceeded
                        Ok(true) => {}                // descended
                        Ok(false) => {}               // memo hit: keep scanning
                    }
                }
                Action::Exhausted => {
                    // Children exhausted: exit-state selection
                    // (regex.cppm:1199-1232). Stopping means no more
                    // advancing iterations; glibc's set_regs walk then runs
                    // the repeat's REMAINING copies on the body's empty
                    // parse. The empty copies decide the surviving
                    // captures: the LAST copy is register-marked exactly
                    // when the max is unbounded or min+1, and update_regs
                    // then restores the last non-empty commit instead of
                    // recording the empty match.
                    let frame = stack.pop().expect("checked above");
                    // The empty iterations also count toward the {m,n}
                    // lower bound, which is why the exit is emitted even
                    // when count < min once an empty iteration exists.
                    let emit = frame.count >= min || frame.empty_iteration.is_some();
                    let exit_state = match frame.empty_iteration {
                        Some(empty) => {
                            let trailing_copy_is_marked = max.is_none() || max == Some(min + 1);
                            if trailing_copy_is_marked
                                && empty_iteration_undone(&frame.state, &empty)
                            {
                                frame.state
                            } else {
                                empty
                            }
                        }
                        None => frame.state,
                    };
                    if emit {
                        push_unique(&mut out, &mut out_seen, exit_state, budget);
                        if budget.exceeded {
                            return Vec::new();
                        }
                    }
                }
            }
        }
        if budget.exceeded {
            return Vec::new();
        }
        out
    }

    /// Head of one collect_repeat activation: budget/memo checks, the
    /// bounded-max early exit, and the body's state expansion. `Ok(false)`
    /// = memo hit (no frame), `Ok(true)` = frame pushed, `Err` = budget.
    #[allow(clippy::too_many_arguments)]
    fn enter_repeat_frame(
        &self,
        text: &[u8],
        state: State,
        count: u32,
        count_free: bool,
        min: u32,
        max: Option<u32>,
        child: &Node,
        budget: &mut StepBudget,
        visited: &mut HashSet<VisitKey>,
        stack: &mut Vec<RepeatFrame>,
    ) -> Result<bool, ()> {
        if budget.exceeded {
            return Err(());
        }
        if visited.len() >= VISITED_LIMIT {
            budget.exceeded = true;
            return Err(());
        }
        let key = VisitKey {
            count: if count_free { 0 } else { count },
            state: state.clone(),
        };
        if !visited.insert(key) {
            return Ok(false);
        }
        if let Some(max) = max {
            if count >= max {
                stack.push(RepeatFrame {
                    count,
                    state,
                    child_states: Vec::new(),
                    idx: 0,
                    empty_iteration: None,
                    bounded_exit: true,
                    bounded_emit: count >= min,
                });
                return Ok(true);
            }
        }
        let child_states = self.match_node(child, text, state.clone(), budget);
        if budget.exceeded {
            return Err(());
        }
        stack.push(RepeatFrame {
            count,
            state,
            child_states,
            idx: 0,
            empty_iteration: None,
            bounded_exit: false,
            bounded_emit: false,
        });
        Ok(true)
    }
}

/// One in-progress collect_repeat activation.
struct RepeatFrame {
    count: u32,
    state: State,
    child_states: Vec<State>,
    idx: usize,
    empty_iteration: Option<State>,
    /// Bounded-max exit reached: expand nothing.
    bounded_exit: bool,
    /// Whether that exit emits (count already met the lower bound); the
    /// C++ `count >= max && count < min` path returns silently.
    bounded_emit: bool,
}

fn advance(mut state: State, by: usize) -> State {
    state.pos += by;
    state
}
