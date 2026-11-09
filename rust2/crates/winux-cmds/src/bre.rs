//! Minimal POSIX-BRE engine for `tac -r` separators.
//!
//! GNU tac compiles the separator with re_compile_pattern, i.e. the glibc
//! BRE dialect — with the gnulib quirk that `+` and `?` are unbackslashed
//! operators while alternation/grouping are backslashed (`\|`, `\(`).
//! That dialect is what `tac -r` really accepts, probed against coreutils
//! 9.4 (target/gnu-probe/p8.sh, p9.sh): 'X|Y' matches the LITERAL "X|Y"
//! string (plain `|` is a literal in BRE, hence the identity output tac
//! prints), while '[0-9]+', 'X*Y', 'X?', '[XY]', 'b+' behave as operators.
//!
//! Supported subset (covers every realistic separator and all probed
//! cases): literals, `.`, character classes `[...]` (ranges, negation, a
//! first `]` is literal, `-` ranges, backslash escapes), `*`, `+`, `?`,
//! `^` anchor at pattern start, `$` anchor at pattern end, backslashed
//! literals (`\.`, `\\`, `\[`, ...), and backslash alternation `\|`.
//! NOT supported: `\( \)` groups, `\{m,n\}` intervals, backreferences —
//! malformed forms of the supported syntax compile_err. Matching at a
//! given start is greedy backtracking (longest single-path), which
//! coincides with POSIX leftmost-longest for these operator shapes.

#[derive(Debug, Clone)]
enum Node {
    Literal(u8),
    Any,
    Class {
        negated: bool,
        items: Vec<ClassItem>,
    },
    Star(Box<Node>),
    Plus(Box<Node>),
    Quest(Box<Node>),
    StartAnchor,
    EndAnchor,
    Concat(Vec<Node>),
    Alternate(Box<Node>, Box<Node>),
}

#[derive(Debug, Clone)]
enum ClassItem {
    Single(u8),
    Range(u8, u8),
}

pub struct Regex {
    root: Node,
}

/// Split `bytes` on unescaped top-level `\|` (BRE alternation).
fn split_alternation(bytes: &[u8]) -> Vec<Vec<u8>> {
    let mut parts = Vec::new();
    let mut cur = Vec::new();
    let mut i = 0usize;
    while i < bytes.len() {
        if bytes[i] == b'\\' && i + 1 < bytes.len() && bytes[i + 1] == b'|' {
            parts.push(std::mem::take(&mut cur));
            i += 2;
            continue;
        }
        if bytes[i] == b'\\' && i + 1 < bytes.len() {
            cur.push(bytes[i]);
            cur.push(bytes[i + 1]);
            i += 2;
            continue;
        }
        cur.push(bytes[i]);
        i += 1;
    }
    parts.push(cur);
    parts
}

/// Parse one alternation-free branch into a Concat node.
fn parse_branch(bytes: &[u8], allow_leading_caret: bool) -> Result<Node, String> {
    let mut nodes: Vec<Node> = Vec::new();
    let mut i = 0usize;
    if allow_leading_caret && !bytes.is_empty() && bytes[0] == b'^' {
        nodes.push(Node::StartAnchor);
        i = 1;
    }
    while i < bytes.len() {
        let c = bytes[i];
        match c {
            b'$' if i == bytes.len() - 1 => {
                nodes.push(Node::EndAnchor);
                i += 1;
            }
            b'.' => {
                nodes.push(Node::Any);
                i += 1;
            }
            b'[' => {
                let (item, next) = parse_class(bytes, i)?;
                nodes.push(item);
                i = next;
            }
            b'*' | b'+' | b'?' => {
                let last = nodes.pop();
                let Some(last) = last else {
                    return Err("Invalid preceding regular expression".into());
                };
                if matches!(last, Node::StartAnchor | Node::EndAnchor) {
                    return Err("Invalid preceding regular expression".into());
                }
                nodes.push(match c {
                    b'*' => Node::Star(Box::new(last)),
                    b'+' => Node::Plus(Box::new(last)),
                    _ => Node::Quest(Box::new(last)),
                });
                i += 1;
            }
            b'\\' => {
                if i + 1 >= bytes.len() {
                    return Err("Trailing backslash".into());
                }
                nodes.push(Node::Literal(bytes[i + 1]));
                i += 2;
            }
            _ => {
                nodes.push(Node::Literal(c));
                i += 1;
            }
        }
    }
    Ok(Node::Concat(nodes))
}

/// Parse the character class beginning at bytes[i] == '['.
fn parse_class(bytes: &[u8], i: usize) -> Result<(Node, usize), String> {
    let mut j = i + 1;
    let negated = j < bytes.len() && bytes[j] == b'^';
    if negated {
        j += 1;
    }
    let mut items = Vec::new();
    let mut first = true;
    loop {
        if j >= bytes.len() {
            return Err("Unmatched [ or [^".into());
        }
        let c = bytes[j];
        if c == b']' && !first {
            j += 1;
            break;
        }
        first = false;
        if c == b'\\' && j + 1 < bytes.len() {
            items.push(ClassItem::Single(bytes[j + 1]));
            j += 2;
            continue;
        }
        if j + 2 < bytes.len() && bytes[j + 1] == b'-' && bytes[j + 2] != b']' {
            let (lo, hi) = (c, bytes[j + 2]);
            if hi < lo {
                return Err("Invalid range end".into());
            }
            items.push(ClassItem::Range(lo, hi));
            j += 3;
        } else {
            items.push(ClassItem::Single(c));
            j += 1;
        }
    }
    Ok((Node::Class { negated, items }, j))
}

impl Regex {
    pub fn compile(pattern: &str) -> Result<Regex, String> {
        let bytes = pattern.as_bytes();
        let parts = split_alternation(bytes);
        if parts.len() == 1 {
            let root = parse_branch(&parts[0], true)?;
            return Ok(Regex { root });
        }
        // Right-fold the branches into Alternate nodes. A trailing `\|`
        // yields an empty final branch that matches empty (GNU re_compile
        // accepts it); parse_branch of empty gives Concat([]) which matches
        // empty.
        let mut branches: Vec<Node> = Vec::new();
        for (idx, part) in parts.iter().enumerate() {
            branches.push(parse_branch(part, idx == 0)?);
        }
        let root = branches
            .into_iter()
            .rev()
            .reduce(|right, left| Node::Alternate(Box::new(left), Box::new(right)))
            .unwrap();
        Ok(Regex { root })
    }

    /// Longest match of the pattern within `buf[..limit]` starting at or
    /// before `from`, searching backward from `from` down to 0 (the
    /// rightmost start wins, mirroring tac.c's negative-range re_search).
    /// Returns (start, end) with end <= limit, or None.
    pub fn search_backward(&self, buf: &[u8], limit: usize, from: usize) -> Option<(usize, usize)> {
        let mut start = from as isize;
        while start >= 0 {
            let s = start as usize;
            if let Some(end) = match_node(&self.root, buf, s, limit) {
                return Some((s, end));
            }
            start -= 1;
        }
        None
    }
}

/// Longest match of one full pattern node against buf[..limit] starting at
/// `pos`; returns the match end.
fn match_node(node: &Node, buf: &[u8], pos: usize, limit: usize) -> Option<usize> {
    match node {
        Node::Concat(items) => match_seq(items, 0, buf, pos, limit),
        Node::Alternate(left, right) => {
            let l = match_node(left, buf, pos, limit);
            let r = match_node(right, buf, pos, limit);
            match (l, r) {
                (Some(a), Some(b)) => Some(a.max(b)),
                (Some(a), None) => Some(a),
                (None, Some(b)) => Some(b),
                (None, None) => None,
            }
        }
        other => match_one(other, buf, pos, limit),
    }
}

/// Match items[from..] at buf[pos..limit]; returns the end of the match.
/// `Star`/`Plus`/`Quest` retry the continuation for shorter repetitions on
/// failure (greedy with backtracking).
fn match_seq(items: &[Node], from: usize, buf: &[u8], pos: usize, limit: usize) -> Option<usize> {
    if from >= items.len() {
        return Some(pos);
    }
    match &items[from] {
        Node::StartAnchor => {
            if pos == 0 {
                match_seq(items, from + 1, buf, pos, limit)
            } else {
                None
            }
        }
        Node::EndAnchor => {
            if pos == limit {
                match_seq(items, from + 1, buf, pos, limit)
            } else {
                None
            }
        }
        Node::Star(inner) => repeat_positions(inner, buf, pos, limit, 0)
            .into_iter()
            .find_map(|at| match_seq(items, from + 1, buf, at, limit)),
        Node::Plus(inner) => repeat_positions(inner, buf, pos, limit, 1)
            .into_iter()
            .find_map(|at| match_seq(items, from + 1, buf, at, limit)),
        Node::Quest(inner) => repeat_positions(inner, buf, pos, limit, 0)
            .into_iter()
            .find_map(|at| match_seq(items, from + 1, buf, at, limit)),
        one => {
            let next = match_one(one, buf, pos, limit)?;
            match_seq(items, from + 1, buf, next, limit)
        }
    }
}

/// All positions reachable by matching `inner` at least `min` times
/// (possibly zero-length), longest-first for greedy backtracking.
fn repeat_positions(inner: &Node, buf: &[u8], pos: usize, limit: usize, min: usize) -> Vec<usize> {
    let mut ends = vec![pos]; // ends[k] is the position after k repetitions
    let mut p = pos;
    while ends.len() <= 4096 {
        match match_one(inner, buf, p, limit) {
            Some(next) if next != p => {
                p = next;
                ends.push(p);
            }
            _ => break,
        }
    }
    let mut out: Vec<usize> = ends.into_iter().skip(min).collect();
    out.reverse();
    out
}

/// Match a single-position node once at buf[pos]; returns the next position.
fn match_one(node: &Node, buf: &[u8], pos: usize, limit: usize) -> Option<usize> {
    if pos >= limit {
        return None;
    }
    let c = buf[pos];
    let ok = match node {
        Node::Literal(l) => c == *l,
        Node::Any => true,
        Node::Class { negated, items } => {
            let mut hit = false;
            for item in items {
                match item {
                    ClassItem::Single(s) => {
                        if c == *s {
                            hit = true;
                            break;
                        }
                    }
                    ClassItem::Range(lo, hi) => {
                        if c >= *lo && c <= *hi {
                            hit = true;
                            break;
                        }
                    }
                }
            }
            hit != *negated
        }
        _ => return None,
    };
    if ok {
        Some(pos + 1)
    } else {
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn find(re: &str, buf: &[u8], limit: usize, from: usize) -> Option<(usize, usize)> {
        Regex::compile(re)
            .unwrap()
            .search_backward(buf, limit, from)
    }

    #[test]
    fn literals_and_classes() {
        let buf = b"aXbYc";
        // 'X|Y' is the literal string in BRE: never matches (GNU probed:
        // tac -r -s 'X|Y' prints the input unchanged).
        assert_eq!(find("X|Y", buf, 5, 5), None);
        assert_eq!(find("X", buf, 5, 5), Some((1, 2)));
        assert_eq!(find("[XY]", buf, 5, 5), Some((3, 4)));
        assert_eq!(find("[0-9]", b"a1b2c", 5, 5), Some((3, 4)));
    }

    #[test]
    fn operators_and_windows() {
        let buf = b"a1b22c333";
        // rightmost start wins; the trailing digit at index 8 matches first
        assert_eq!(find("[0-9]+", buf, 9, 9), Some((8, 9)));
        // window clipping changes what matches (GNU 'a12b34c' probe path)
        assert_eq!(find("[0-9][0-9]", b"a12b34c", 7, 7), Some((4, 6)));
        assert_eq!(find("b+", b"abbbc", 5, 5), Some((3, 4)));
    }

    #[test]
    fn empty_matches_allowed() {
        // 'X?' matches empty everywhere; rightmost start (== from) wins, the
        // same zero-length-match behavior the GNU 'X?' probe exercises.
        assert_eq!(find("X?", b"aXb", 3, 3), Some((3, 3)));
        assert_eq!(find("Z*", b"abc", 3, 3), Some((3, 3)));
    }

    #[test]
    fn escaped_and_anchors() {
        assert_eq!(find(r"\.", b"a.b", 3, 3), Some((1, 2)));
        assert_eq!(find(".", b"a.b", 3, 3), Some((2, 3)));
        assert_eq!(find("^a", b"abc", 3, 3), Some((0, 1)));
        assert_eq!(find("^b", b"abc", 3, 3), None);
        assert_eq!(find("c$", b"abc", 3, 3), Some((2, 3)));
        assert_eq!(find("b$", b"abc", 3, 3), None);
    }

    #[test]
    fn alternation_backslash_form() {
        assert_eq!(find(r"X\|Y", b"aXbYc", 5, 5), Some((3, 4)));
        assert_eq!(find(r"a\|z", b"zzz", 3, 3), Some((2, 3)));
    }

    #[test]
    fn compile_errors() {
        assert!(Regex::compile("[a").is_err());
        assert!(Regex::compile("\\").is_err());
        assert!(Regex::compile("*a").is_err());
    }
}
