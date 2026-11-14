//! Consumer-level smoke gate for the regex-unify API (`CompileOptions`,
//! `MatchCache` + `*_with`, `match_ends_at_bytes`, `dialect`).
//!
//! Two real gate tables are replayed through the unify surface, with the
//! expected bytes taken from GNU (not from the engine):
//!
//! * **grep** — 30 of the 162 rows of `gate/cases/{grep,egrep,fgrep}.tsv`
//!   (origin/rust2/grep-impl): the pattern is compiled the way the grep lane
//!   does (`CompileOptions::default` + `-i`, BRE/ERE per table), lines are
//!   selected with [`Regex::find_at_bytes_with`], `-w` walks
//!   [`Regex::match_ends_at_bytes`] (the "retry shorter at the same start"
//!   rule of GNU `-w`), `-x` uses `matches_entire_bytes`, and `-o` emits
//!   leftmost-longest slices. Expected stdout comes from `grep 3.11` /
//!   `egrep 3.11` (WSL, LC_ALL=C) replaying each row's own setup + argv.
//! * **find** — 27 of the 317 `gate/cases/find.tsv` rows that carry
//!   `-regex`/`-iregex`/`-regextype` (origin/rust2/find-impl), plus 3 derived
//!   dialect probes, compiled through [`dialect::compile`] and matched whole
//!   path with `matches_entire_bytes`. Expected matches come from
//!   `find (GNU findutils) 4.9.0` replaying each row's setup + argv; error
//!   rows (rc != 0) must be compile errors of the same class.
//!
//! Regeneration: the oracle scripts run the gate row's `setup` and `args`
//! verbatim in a GNU sandbox and dump `{id, argv, input, stdout, rc}`; the
//! tables below are the byte-for-byte Rust transcription of that dump.

use winux_regex::dfa::{LineDfa, LineMode};
use winux_regex::dialect::{self, Dialect, DialectError};
use winux_regex::{CompileOptions, MatchCache, Regex, Syntax};

struct GrepRow {
    id: &'static str,
    syntax: Syntax,
    ignore_case: bool,
    word: bool,
    whole_line: bool,
    only_matching: bool,
    patterns: &'static [&'static [u8]],
    text: &'static [u8],
    /// GNU grep stdout replaying the row (`-o` slices or selected lines).
    gnu: &'static [u8],
}

enum FindKind {
    /// GNU matched these whole paths.
    Match,
    /// GNU failed to compile the pattern (rc != 0).
    Compile,
    /// `-regextype foo` is not a findutils name (rc != 0).
    BadName,
}

struct FindClause {
    /// `-regextype` name, `None` = find's default (`emacs`).
    dialect: Option<&'static str>,
    ignore_case: bool,
    pattern: &'static [u8],
}

struct FindRow {
    id: &'static str,
    kind: FindKind,
    /// Predicates of the `-o`-joined expression (one for every row but the
    /// positional `-regextype` row, which ORs two).
    clauses: &'static [FindClause],
    /// Everything `find` would visit (its own `-print` output).
    candidates: &'static [&'static [u8]],
    /// GNU's selection (sorted).
    matched: &'static [&'static [u8]],
}

fn word_byte(b: u8) -> bool {
    b.is_ascii_alphanumeric() || b == b'_'
}

/// The grep lane's `-w` rule on top of [`Regex::match_ends_at_bytes`]: a
/// match is a whole word when neither neighbour is a word constituent, and
/// the longest end wins among the attempts anchored at the leftmost start.
fn find_word(re: &Regex, line: &[u8], from: usize) -> Option<(usize, usize)> {
    let mut start = from;
    while start <= line.len() {
        if start > from && start < line.len() && (0x80..0xC0).contains(&line[start]) {
            start += 1;
            continue;
        }
        for end in re.match_ends_at_bytes(line, start) {
            let before = start == 0 || !word_byte(line[start - 1]);
            let after = end == line.len() || !word_byte(line[end]);
            if before && after {
                return Some((start, end));
            }
        }
        start += 1;
    }
    None
}

fn first_match(
    re: &Regex,
    cache: &mut MatchCache,
    line: &[u8],
    from: usize,
    word: bool,
) -> Option<(usize, usize)> {
    if word {
        find_word(re, line, from)
    } else {
        re.find_at_bytes_with(cache, line, from)
            .map(|m| (m.begin, m.end))
    }
}

/// Replay one grep row through the unify API, byte for byte like the tool.
fn grep_replay(row: &GrepRow) -> Vec<u8> {
    let opts = CompileOptions {
        ignore_case: row.ignore_case,
        ..CompileOptions::default()
    };
    let mut compiled = Vec::new();
    for pat in row.patterns {
        match Regex::compile_with(row.syntax, pat, opts) {
            Ok(re) => compiled.push(re),
            // GNU prints nothing and exits 2 on a compile error.
            Err(_) => return Vec::new(),
        }
    }
    let mut caches: Vec<MatchCache> = compiled.iter().map(|re| re.new_cache()).collect();
    let mut lines: Vec<&[u8]> = if row.text.is_empty() {
        Vec::new()
    } else {
        row.text.split(|&b| b == b'\n').collect()
    };
    if row.text.ends_with(b"\n") {
        lines.pop();
    }
    let mut out = Vec::new();
    for line in lines {
        if row.only_matching {
            let mut cursor = 0usize;
            loop {
                let mut best: Option<(usize, usize)> = None;
                for (re, cache) in compiled.iter().zip(caches.iter_mut()) {
                    if let Some((b, e)) = first_match(re, cache, line, cursor, row.word) {
                        best = match best {
                            Some((bb, be)) if bb < b || (bb == b && be >= e) => Some((bb, be)),
                            _ => Some((b, e)),
                        };
                    }
                }
                let Some((begin, end)) = best else { break };
                if end > begin {
                    out.extend_from_slice(&line[begin..end]);
                    out.push(b'\n');
                    cursor = end;
                } else {
                    cursor = begin + 1;
                }
                if cursor > line.len() {
                    break;
                }
            }
            continue;
        }
        let selected = if row.whole_line {
            compiled.iter().any(|re| re.matches_entire_bytes(line))
        } else if row.word {
            compiled.iter().any(|re| find_word(re, line, 0).is_some())
        } else {
            compiled
                .iter()
                .zip(caches.iter_mut())
                .any(|(re, cache)| re.is_match_with(cache, line))
        };
        if selected {
            out.extend_from_slice(line);
            out.push(b'\n');
        }
    }
    out
}

#[test]
fn grep_gate_rows_smoke() {
    assert_eq!(GREP_ROWS.len(), 30, "smoke table must stay 30 rows");
    let mut failures = Vec::new();
    for row in GREP_ROWS {
        let got = grep_replay(row);
        if got != row.gnu {
            failures.push(format!(
                "{}: got {:?} want {:?}",
                row.id,
                String::from_utf8_lossy(&got),
                String::from_utf8_lossy(row.gnu)
            ));
        }
    }
    assert!(
        failures.is_empty(),
        "grep smoke diverged:\n{}",
        failures.join("\n")
    );
}

#[test]
fn find_gate_rows_smoke() {
    assert_eq!(FIND_ROWS.len(), 30, "smoke table must stay 30 rows");
    for row in FIND_ROWS {
        if let FindKind::BadName = row.kind {
            assert_eq!(row.clauses.len(), 1);
            assert!(
                Dialect::from_name("foo").is_none(),
                "{}: unknown name",
                row.id
            );
            continue;
        }
        let resolve = |name: Option<&str>| match name {
            Some(name) => Dialect::from_name(name)
                .unwrap_or_else(|| panic!("{}: unknown name {name}", row.id)),
            None => Dialect::Emacs,
        };
        match row.kind {
            FindKind::BadName => unreachable!(),
            FindKind::Compile => {
                let clause = &row.clauses[0];
                match dialect::compile(resolve(clause.dialect), clause.pattern, clause.ignore_case)
                {
                    Err(DialectError::BadRepeat) => {}
                    Err(DialectError::Regex(e)) => {
                        assert!(
                            !e.message().is_empty()
                                && !e.message().starts_with("invalid preceding"),
                            "{}: expected a glibc compile error, got {e}",
                            row.id
                        );
                    }
                    Ok(_) => panic!("{}: pattern must not compile", row.id),
                }
            }
            FindKind::Match => {
                let regexes: Vec<Regex> = row
                    .clauses
                    .iter()
                    .map(|clause| {
                        dialect::compile(
                            resolve(clause.dialect),
                            clause.pattern,
                            clause.ignore_case,
                        )
                        .unwrap_or_else(|e| panic!("{}: {:?}", row.id, e))
                    })
                    .collect();
                let got: Vec<&[u8]> = row
                    .candidates
                    .iter()
                    .copied()
                    .filter(|c| regexes.iter().any(|re| re.matches_entire_bytes(c)))
                    .collect();
                assert_eq!(
                    got,
                    row.matched,
                    "{}: clauses {:?}",
                    row.id,
                    row.clauses
                        .iter()
                        .map(|c| (c.dialect, String::from_utf8_lossy(c.pattern).into_owned()))
                        .collect::<Vec<_>>()
                );
            }
        }
    }
}

/// The additive surface the three wire lanes adopt: canonical
/// `CompileOptions`, the cache-driven entry points, and the unchanged
/// grep/ find hooks.
#[test]
fn additive_api_shape() {
    // 1. CompileOptions::default() is exactly the legacy grep compile.
    let legacy = Regex::compile(Syntax::Basic, r"\(a\)\1").unwrap();
    let unified =
        Regex::compile_with(Syntax::Basic, br"\(a\)\1", CompileOptions::default()).unwrap();
    assert_eq!(
        legacy.find_at_bytes(b"zabab", 0),
        unified.find_at_bytes(b"zabab", 0)
    );

    // 2. Cache-driven find: same spans and captures as the legacy path.
    let re =
        Regex::compile_with(Syntax::Basic, br"\(ab\)*\(c\)", CompileOptions::default()).unwrap();
    let mut cache = re.new_cache();
    for (text, start) in [
        (&b"ababc"[..], 0usize),
        (&b"zzababc"[..], 0),
        (&b"zzac"[..], 0),
        (&b"c"[..], 0),
        (&b"zzz"[..], 0),
    ] {
        let want = re.find_at_bytes(text, start);
        let got = re.find_at_bytes_with(&mut cache, text, start);
        assert_eq!(got, want);
        assert_eq!(
            re.is_match_with(&mut cache, text),
            re.find_at_bytes(text, 0).is_some()
        );
        let _ = (text, start);
    }

    // 3. match_ends_at_bytes: longest first, deduplicated, anchored starts.
    let lit = Regex::compile(Syntax::Basic, "ab").unwrap();
    assert_eq!(lit.match_ends_at_bytes(b"zab", 1), vec![3]);
    assert!(lit.match_ends_at_bytes(b"zab", 0).is_empty());
    let alt = Regex::compile(Syntax::Basic, r"a\|ab").unwrap();
    assert_eq!(alt.match_ends_at_bytes(b"ab", 0), vec![2, 1]);

    // 3b. A back-reference pattern has no NFA; the cache is regex-sized on
    //     first use and re-sizes when reused with another regex.
    let backref = Regex::compile(Syntax::Basic, r"\(a\)\1").unwrap();
    assert!(backref.is_match_with(&mut cache, b"aa"));
    assert!(!backref.is_match_with(&mut cache, b"ab"));
    assert_eq!(
        backref.find_at_bytes_with(&mut cache, b"xaa", 0),
        backref.find_at_bytes(b"xaa", 0)
    );

    // 4. dialect::compile is the CompileOptions convenience layer: the
    //    mapping table is queryable and the strict families opt in.
    assert!(Dialect::Sed.options(false).posix_strict);
    assert!(!Dialect::Grep.options(false).posix_strict);
    assert!(Dialect::from_name("posix-minimal-basic").is_some());
    assert!(dialect::compile(Dialect::Emacs, b"a", false).is_ok());

    // 5. sed preset: I flag, buffer anchors, restricted `.`.
    let sed = CompileOptions::sed(true, false);
    assert!(sed.ignore_case && sed.buffer_anchors && sed.strict_utf8_dot);
    let sre = Regex::compile_with(Syntax::Basic, br"\`ab\'", sed).unwrap();
    assert!(sre.matches_entire_bytes(b"AB"));
    // A restricted `.` under sed's UTF-8 rule leaves the invalid byte.
    let dot = Regex::compile_with(Syntax::Basic, b".", CompileOptions::sed(false, false)).unwrap();
    assert!(dot.find_at_bytes(b"\xff", 0).is_none());
}

/// The `find_at_bytes_with` fallback (NFA bounds disagree with the
/// backtracker) must not fire: it is `debug_assert!`-guarded, so this
/// differential corpus panics in debug builds if any case reaches it, and a
/// panicking case can never be papered over with empty captures again.
#[test]
fn nfa_bounds_never_disagree_with_backtracker() {
    let patterns: &[&[u8]] = &[
        br"\(a\|ab\)\(b\)\?",
        br"\(a*\)\(b*\)",
        br"\(ab\)*\(c\)",
        br"\(a\)\1",
        br"\(a\|b\)\{2,3\}",
        br"x\(y\)\{1,2\}",
        br"\(a\)\{0,2\}b",
        br"[[:alpha:]]*\(q\)",
        br"^\(a*\)$",
        br"\(a\)\{3\}",
        br"\`\(ab\)*\'",
        br"\(a\)\(b\)\(c\)\?",
    ];
    let texts: &[&[u8]] = &[
        b"", b"a", b"ab", b"abc", b"aab", b"abab", b"aaq", b"xy", b"xxyy", b"aabbcc",
    ];
    for pat in patterns {
        let re = Regex::compile_with(Syntax::Basic, pat, CompileOptions::default()).unwrap();
        let mut cache = re.new_cache();
        for text in texts {
            for start in 0..=text.len() {
                let want = re.find_at_bytes(text, start);
                let got = re.find_at_bytes_with(&mut cache, text, start);
                assert_eq!(
                    got,
                    want,
                    "pattern {:?} text {:?} start {}",
                    String::from_utf8_lossy(pat),
                    String::from_utf8_lossy(text),
                    start
                );
            }
        }
    }
}

/// The line DFA declines the dialect-only node kinds instead of guessing:
/// those patterns stay on the interpreter.
#[test]
fn line_dfa_declines_dialect_nodes() {
    let plain = Regex::compile(Syntax::Basic, "^a$").unwrap();
    assert!(LineDfa::new(&[&plain], LineMode::Contains, b'\n').is_some());
    let sed = Regex::compile_with(Syntax::Basic, b"^a$", CompileOptions::sed(false, true)).unwrap();
    assert!(LineDfa::new(&[&sed], LineMode::Contains, b'\n').is_none());
    let dot = Regex::compile_with(Syntax::Basic, b"a.", CompileOptions::sed(false, false)).unwrap();
    assert!(LineDfa::new(&[&dot], LineMode::Contains, b'\n').is_none());
}
// ---- grep rows: 30 of the 162 rows in gate/cases/{grep,egrep}.tsv
const GREP_ROWS: &[GrepRow] = &[
    GrepRow {
        id: "gp01-plain",
        syntax: Syntax::Basic,
        ignore_case: false,
        word: false,
        whole_line: false,
        only_matching: false,
        patterns: &[b"foo"],
        text: b"foo bar\nbaz\nfoofoo\n",
        gnu: b"foo bar\nfoofoo\n",
    },
    GrepRow {
        id: "gp02-nomatch",
        syntax: Syntax::Basic,
        ignore_case: false,
        word: false,
        whole_line: false,
        only_matching: false,
        patterns: &[b"zzz"],
        text: b"foo bar\nbaz\nfoofoo\n",
        gnu: b"",
    },
    GrepRow {
        id: "gp03-stdin",
        syntax: Syntax::Basic,
        ignore_case: false,
        word: false,
        whole_line: false,
        only_matching: false,
        patterns: &[b"foo"],
        text: b"foo bar\nbaz\n",
        gnu: b"foo bar\n",
    },
    GrepRow {
        id: "gp04-stdin-empty",
        syntax: Syntax::Basic,
        ignore_case: false,
        word: false,
        whole_line: false,
        only_matching: false,
        patterns: &[b"foo"],
        text: b"",
        gnu: b"",
    },
    GrepRow {
        id: "gp31-o-i",
        syntax: Syntax::Basic,
        ignore_case: true,
        word: false,
        whole_line: false,
        only_matching: true,
        patterns: &[b"foo"],
        text: b"FoO fOo\n",
        gnu: b"FoO\nfOo\n",
    },
    GrepRow {
        id: "gp32-o-w",
        syntax: Syntax::Basic,
        ignore_case: false,
        word: true,
        whole_line: false,
        only_matching: true,
        patterns: &[b"fox"],
        text: b"fox foxes firefox afox\n",
        gnu: b"fox\n",
    },
    GrepRow {
        id: "gp33-o-leftmost-longest",
        syntax: Syntax::Basic,
        ignore_case: false,
        word: false,
        whole_line: false,
        only_matching: true,
        patterns: &[b"foo", b"foobar"],
        text: b"foobar here\n",
        gnu: b"foobar\n",
    },
    GrepRow {
        id: "gp34-o-bre-interval",
        syntax: Syntax::Basic,
        ignore_case: false,
        word: false,
        whole_line: false,
        only_matching: true,
        patterns: &[b"o\\{2,3\\}"],
        text: b"sooooo\n",
        gnu: b"ooo\noo\n",
    },
    GrepRow {
        id: "gp36-i",
        syntax: Syntax::Basic,
        ignore_case: true,
        word: false,
        whole_line: false,
        only_matching: false,
        patterns: &[b"foo"],
        text: b"FOO foo Foo\n",
        gnu: b"FOO foo Foo\n",
    },
    GrepRow {
        id: "gp37-w",
        syntax: Syntax::Basic,
        ignore_case: false,
        word: true,
        whole_line: false,
        only_matching: false,
        patterns: &[b"fox"],
        text: b"fox foxes firefox afox\n",
        gnu: b"fox foxes firefox afox\n",
    },
    GrepRow {
        id: "gp38-x",
        syntax: Syntax::Basic,
        ignore_case: false,
        word: false,
        whole_line: true,
        only_matching: false,
        patterns: &[b"foo"],
        text: b"foo\nfoo bar\nxfoox\n",
        gnu: b"foo\n",
    },
    GrepRow {
        id: "gp39-x-E-alt",
        syntax: Syntax::Extended,
        ignore_case: false,
        word: false,
        whole_line: true,
        only_matching: false,
        patterns: &[b"foo|baz"],
        text: b"foo\nfoo bar\nbaz\n",
        gnu: b"foo\nbaz\n",
    },
    GrepRow {
        id: "gp51-E-alt",
        syntax: Syntax::Extended,
        ignore_case: false,
        word: false,
        whole_line: false,
        only_matching: false,
        patterns: &[b"foo|baz"],
        text: b"foo bar\nbaz\nfoofoo\n",
        gnu: b"foo bar\nbaz\nfoofoo\n",
    },
    GrepRow {
        id: "gp52-E-interval",
        syntax: Syntax::Extended,
        ignore_case: false,
        word: false,
        whole_line: false,
        only_matching: true,
        patterns: &[b"o{2,3}"],
        text: b"sooooo\nso\n",
        gnu: b"ooo\noo\n",
    },
    GrepRow {
        id: "gp53-BRE-alt",
        syntax: Syntax::Basic,
        ignore_case: false,
        word: false,
        whole_line: false,
        only_matching: false,
        patterns: &[b"\\(foo\\|baz\\)"],
        text: b"foo bar\nbaz\nfoofoo\n",
        gnu: b"foo bar\nbaz\nfoofoo\n",
    },
    GrepRow {
        id: "gp54-BRE-plus",
        syntax: Syntax::Basic,
        ignore_case: false,
        word: false,
        whole_line: false,
        only_matching: true,
        patterns: &[b"o\\+"],
        text: b"sooooo\nso\n",
        gnu: b"ooooo\no\n",
    },
    GrepRow {
        id: "gp55-BRE-qmark",
        syntax: Syntax::Basic,
        ignore_case: false,
        word: false,
        whole_line: false,
        only_matching: false,
        patterns: &[b"colou\\?r"],
        text: b"color\ncolour\n",
        gnu: b"color\ncolour\n",
    },
    GrepRow {
        id: "gp56-BRE-backref",
        syntax: Syntax::Basic,
        ignore_case: false,
        word: false,
        whole_line: false,
        only_matching: false,
        patterns: &[b"\\(abc\\)\\1"],
        text: b"abcabc\nabcx\n",
        gnu: b"abcabc\n",
    },
    GrepRow {
        id: "gp57-ERE-backref",
        syntax: Syntax::Extended,
        ignore_case: false,
        word: false,
        whole_line: false,
        only_matching: false,
        patterns: &[b"(a)\\1"],
        text: b"aa\nab\n",
        gnu: b"aa\n",
    },
    GrepRow {
        id: "gp58-class",
        syntax: Syntax::Basic,
        ignore_case: false,
        word: false,
        whole_line: false,
        only_matching: true,
        patterns: &[b"[[:alpha:]]\\+"],
        text: b"foo bar\nbaz\nfoofoo\n",
        gnu: b"foo\nbar\nbaz\nfoofoo\n",
    },
    GrepRow {
        id: "gp105-invalid-regex",
        syntax: Syntax::Basic,
        ignore_case: false,
        word: false,
        whole_line: false,
        only_matching: false,
        patterns: &[b"["],
        text: b"foo bar\nbaz\nfoofoo\n",
        gnu: b"",
    },
    GrepRow {
        id: "gp118-utf8-dot",
        syntax: Syntax::Basic,
        ignore_case: false,
        word: false,
        whole_line: false,
        only_matching: true,
        patterns: &[b"."],
        text: b"caf\xc3\xa9 next\ncafe next\n",
        gnu: b"c\na\nf\n\xc3\xa9\n \nn\ne\nx\nt\nc\na\nf\ne\n \nn\ne\nx\nt\n",
    },
    GrepRow {
        id: "gp119-crlf-match",
        syntax: Syntax::Basic,
        ignore_case: false,
        word: false,
        whole_line: false,
        only_matching: false,
        patterns: &[b"bar"],
        text: b"bar\r\nfoo\n",
        gnu: b"bar\r\n",
    },
    GrepRow {
        id: "gp124-y-alias",
        syntax: Syntax::Basic,
        ignore_case: true,
        word: false,
        whole_line: false,
        only_matching: false,
        patterns: &[b"foo"],
        text: b"FOO\n",
        gnu: b"FOO\n",
    },
    GrepRow {
        id: "gp130-no-ignore-case",
        syntax: Syntax::Basic,
        ignore_case: false,
        word: false,
        whole_line: false,
        only_matching: false,
        patterns: &[b"foo"],
        text: b"FOO\n",
        gnu: b"",
    },
    GrepRow {
        id: "gp142-w-x",
        syntax: Syntax::Basic,
        ignore_case: false,
        word: true,
        whole_line: true,
        only_matching: false,
        patterns: &[b"fox"],
        text: b"foo bar\nbaz\nfoofoo\n",
        gnu: b"",
    },
    GrepRow {
        id: "eg01-default-ere",
        syntax: Syntax::Extended,
        ignore_case: false,
        word: false,
        whole_line: false,
        only_matching: false,
        patterns: &[b"foo|baz"],
        text: b"foo bar\nbaz\nfoofoo\n",
        gnu: b"foo bar\nbaz\nfoofoo\n",
    },
    GrepRow {
        id: "eg03-E-ok",
        syntax: Syntax::Extended,
        ignore_case: false,
        word: false,
        whole_line: false,
        only_matching: false,
        patterns: &[b"foo|baz"],
        text: b"foo bar\nbaz\nfoofoo\n",
        gnu: b"foo bar\nbaz\nfoofoo\n",
    },
    GrepRow {
        id: "eg09-o-leftmost",
        syntax: Syntax::Extended,
        ignore_case: false,
        word: false,
        whole_line: false,
        only_matching: true,
        patterns: &[b"foo(bar)?|foo"],
        text: b"foo bar\nbaz\nfoofoo\n",
        gnu: b"foo\nfoo\nfoo\n",
    },
    GrepRow {
        id: "eg11-w",
        syntax: Syntax::Extended,
        ignore_case: false,
        word: true,
        whole_line: false,
        only_matching: false,
        patterns: &[b"fox"],
        text: b"foxes firefox afox\n",
        gnu: b"",
    },
];

// ---- find rows: 27 regex gate rows + 3 derived dialect probes
const FIND_ROWS: &[FindRow] = &[
    FindRow {
        id: "fd023-regextype-bogus",
        kind: FindKind::BadName,
        clauses: &[FindClause {
            dialect: Some("foo"),
            ignore_case: false,
            pattern: b"",
        }],
        candidates: &[b"."],
        matched: &[],
    },
    FindRow {
        id: "fd070-regex-unmatched-group",
        kind: FindKind::Compile,
        clauses: &[FindClause {
            dialect: None,
            ignore_case: false,
            pattern: b"\\(a",
        }],
        candidates: &[b"."],
        matched: &[],
    },
    FindRow {
        id: "fd071-posix-extended-leading-star",
        kind: FindKind::Compile,
        clauses: &[FindClause {
            dialect: Some("posix-extended"),
            ignore_case: false,
            pattern: b"*a",
        }],
        candidates: &[b"."],
        matched: &[],
    },
    FindRow {
        id: "fd130-regex-default-emacs",
        kind: FindKind::Match,
        clauses: &[FindClause {
            dialect: None,
            ignore_case: false,
            pattern: b".*\\.rs",
        }],
        candidates: &[
            b"t",
            b"t/a",
            b"t/a/b",
            b"t/a/b/big",
            b"t/a/b/y.rs",
            b"t/a/f5",
            b"t/a/x.rs",
            b"t/d",
            b"t/d/.hid",
            b"t/e",
        ],
        matched: &[b"t/a/b/y.rs", b"t/a/x.rs"],
    },
    FindRow {
        id: "fd131-regex-emacs-plus",
        kind: FindKind::Match,
        clauses: &[FindClause {
            dialect: None,
            ignore_case: false,
            pattern: b"r/a+b",
        }],
        candidates: &[
            b"r", b"r/[x]", b"r/a(b)", b"r/a+b", b"r/a.b", b"r/aXb", b"r/aa", b"r/aab", b"r/ab",
            b"r/abab", b"r/a{2}", b"r/b", b"r/x$y", b"r/x^y",
        ],
        matched: &[b"r/aab", b"r/ab"],
    },
    FindRow {
        id: "fd132-regex-emacs-escaped-plus",
        kind: FindKind::Match,
        clauses: &[FindClause {
            dialect: None,
            ignore_case: false,
            pattern: b"r/a\\+b",
        }],
        candidates: &[
            b"r", b"r/[x]", b"r/a(b)", b"r/a+b", b"r/a.b", b"r/aXb", b"r/aa", b"r/aab", b"r/ab",
            b"r/abab", b"r/a{2}", b"r/b", b"r/x$y", b"r/x^y",
        ],
        matched: &[b"r/a+b"],
    },
    FindRow {
        id: "fd133-regex-emacs-group-alt",
        kind: FindKind::Match,
        clauses: &[FindClause {
            dialect: None,
            ignore_case: false,
            pattern: b"r/\\(a\\|b\\)",
        }],
        candidates: &[
            b"r", b"r/[x]", b"r/a(b)", b"r/a+b", b"r/a.b", b"r/aXb", b"r/aa", b"r/aab", b"r/ab",
            b"r/abab", b"r/a{2}", b"r/b", b"r/x$y", b"r/x^y",
        ],
        matched: &[b"r/b"],
    },
    FindRow {
        id: "fd134-regex-emacs-literal-braces",
        kind: FindKind::Match,
        clauses: &[FindClause {
            dialect: None,
            ignore_case: false,
            pattern: b"r/a\\{2\\}",
        }],
        candidates: &[
            b"r", b"r/[x]", b"r/a(b)", b"r/a+b", b"r/a.b", b"r/aXb", b"r/aa", b"r/aab", b"r/ab",
            b"r/abab", b"r/a{2}", b"r/b", b"r/x$y", b"r/x^y",
        ],
        matched: &[b"r/a{2}"],
    },
    FindRow {
        id: "fd135-regex-emacs-no-classes",
        kind: FindKind::Match,
        clauses: &[FindClause {
            dialect: None,
            ignore_case: false,
            pattern: b".*[[:upper:]].*",
        }],
        candidates: &[
            b"r", b"r/[x]", b"r/a(b)", b"r/a+b", b"r/a.b", b"r/aXb", b"r/aa", b"r/aab", b"r/ab",
            b"r/abab", b"r/a{2}", b"r/b", b"r/x$y", b"r/x^y",
        ],
        matched: &[],
    },
    FindRow {
        id: "fd136-regex-emacs-backref",
        kind: FindKind::Match,
        clauses: &[FindClause {
            dialect: None,
            ignore_case: false,
            pattern: b"r/\\(ab\\)\\1",
        }],
        candidates: &[
            b"r", b"r/[x]", b"r/a(b)", b"r/a+b", b"r/a.b", b"r/aXb", b"r/aa", b"r/aab", b"r/ab",
            b"r/abab", b"r/a{2}", b"r/b", b"r/x$y", b"r/x^y",
        ],
        matched: &[b"r/abab"],
    },
    FindRow {
        id: "fd137-regex-whole-path",
        kind: FindKind::Match,
        clauses: &[FindClause {
            dialect: None,
            ignore_case: false,
            pattern: b"ab",
        }],
        candidates: &[
            b"r", b"r/[x]", b"r/a(b)", b"r/a+b", b"r/a.b", b"r/aXb", b"r/aa", b"r/aab", b"r/ab",
            b"r/abab", b"r/a{2}", b"r/b", b"r/x$y", b"r/x^y",
        ],
        matched: &[],
    },
    FindRow {
        id: "fd138-iregex",
        kind: FindKind::Match,
        clauses: &[FindClause {
            dialect: None,
            ignore_case: true,
            pattern: b"r/AXB",
        }],
        candidates: &[
            b"r", b"r/[x]", b"r/a(b)", b"r/a+b", b"r/a.b", b"r/aXb", b"r/aa", b"r/aab", b"r/ab",
            b"r/abab", b"r/a{2}", b"r/b", b"r/x$y", b"r/x^y",
        ],
        matched: &[b"r/aXb"],
    },
    FindRow {
        id: "fd139-regextype-posix-basic",
        kind: FindKind::Match,
        clauses: &[FindClause {
            dialect: Some("posix-basic"),
            ignore_case: false,
            pattern: b"r/a\\+b",
        }],
        candidates: &[
            b"r", b"r/[x]", b"r/a(b)", b"r/a+b", b"r/a.b", b"r/aXb", b"r/aa", b"r/aab", b"r/ab",
            b"r/abab", b"r/a{2}", b"r/b", b"r/x$y", b"r/x^y",
        ],
        matched: &[b"r/aab", b"r/ab"],
    },
    FindRow {
        id: "fd140-regextype-posix-extended",
        kind: FindKind::Match,
        clauses: &[FindClause {
            dialect: Some("posix-extended"),
            ignore_case: false,
            pattern: b"r/a{1,2}b",
        }],
        candidates: &[
            b"r", b"r/[x]", b"r/a(b)", b"r/a+b", b"r/a.b", b"r/aXb", b"r/aa", b"r/aab", b"r/ab",
            b"r/abab", b"r/a{2}", b"r/b", b"r/x$y", b"r/x^y",
        ],
        matched: &[b"r/aab", b"r/ab"],
    },
    FindRow {
        id: "fd141-regextype-posix-egrep",
        kind: FindKind::Match,
        clauses: &[FindClause {
            dialect: Some("posix-egrep"),
            ignore_case: false,
            pattern: b"r/(a|b)",
        }],
        candidates: &[
            b"r", b"r/[x]", b"r/a(b)", b"r/a+b", b"r/a.b", b"r/aXb", b"r/aa", b"r/aab", b"r/ab",
            b"r/abab", b"r/a{2}", b"r/b", b"r/x$y", b"r/x^y",
        ],
        matched: &[b"r/b"],
    },
    FindRow {
        id: "fd142-regextype-egrep",
        kind: FindKind::Match,
        clauses: &[FindClause {
            dialect: Some("egrep"),
            ignore_case: false,
            pattern: b"r/a?b",
        }],
        candidates: &[
            b"r", b"r/[x]", b"r/a(b)", b"r/a+b", b"r/a.b", b"r/aXb", b"r/aa", b"r/aab", b"r/ab",
            b"r/abab", b"r/a{2}", b"r/b", b"r/x$y", b"r/x^y",
        ],
        matched: &[b"r/ab", b"r/b"],
    },
    FindRow {
        id: "fd143-regextype-grep",
        kind: FindKind::Match,
        clauses: &[FindClause {
            dialect: Some("grep"),
            ignore_case: false,
            pattern: b"r/a\\{2\\}",
        }],
        candidates: &[
            b"r", b"r/[x]", b"r/a(b)", b"r/a+b", b"r/a.b", b"r/aXb", b"r/aa", b"r/aab", b"r/ab",
            b"r/abab", b"r/a{2}", b"r/b", b"r/x$y", b"r/x^y",
        ],
        matched: &[b"r/aa"],
    },
    FindRow {
        id: "fd144-regextype-sed",
        kind: FindKind::Compile,
        clauses: &[FindClause {
            dialect: Some("sed"),
            ignore_case: false,
            pattern: b"r/a**",
        }],
        candidates: &[
            b"r", b"r/[x]", b"r/a(b)", b"r/a+b", b"r/a.b", b"r/aXb", b"r/aa", b"r/aab", b"r/ab",
            b"r/abab", b"r/a{2}", b"r/b", b"r/x$y", b"r/x^y",
        ],
        matched: &[],
    },
    FindRow {
        id: "fd145-regextype-ed",
        kind: FindKind::Match,
        clauses: &[FindClause {
            dialect: Some("ed"),
            ignore_case: false,
            pattern: b"r/a\\?b",
        }],
        candidates: &[
            b"r", b"r/[x]", b"r/a(b)", b"r/a+b", b"r/a.b", b"r/aXb", b"r/aa", b"r/aab", b"r/ab",
            b"r/abab", b"r/a{2}", b"r/b", b"r/x$y", b"r/x^y",
        ],
        matched: &[b"r/ab", b"r/b"],
    },
    FindRow {
        id: "fd146-regextype-emacs-explicit",
        kind: FindKind::Match,
        clauses: &[FindClause {
            dialect: Some("emacs"),
            ignore_case: false,
            pattern: b"r/a?b",
        }],
        candidates: &[
            b"r", b"r/[x]", b"r/a(b)", b"r/a+b", b"r/a.b", b"r/aXb", b"r/aa", b"r/aab", b"r/ab",
            b"r/abab", b"r/a{2}", b"r/b", b"r/x$y", b"r/x^y",
        ],
        matched: &[b"r/ab", b"r/b"],
    },
    FindRow {
        id: "fd147-regextype-findutils-default",
        kind: FindKind::Match,
        clauses: &[FindClause {
            dialect: Some("findutils-default"),
            ignore_case: false,
            pattern: b"r/a.b",
        }],
        candidates: &[
            b"r", b"r/[x]", b"r/a(b)", b"r/a+b", b"r/a.b", b"r/aXb", b"r/aa", b"r/aab", b"r/ab",
            b"r/abab", b"r/a{2}", b"r/b", b"r/x$y", b"r/x^y",
        ],
        matched: &[b"r/a+b", b"r/a.b", b"r/aXb", b"r/aab"],
    },
    FindRow {
        id: "fd148-regextype-posix-awk",
        kind: FindKind::Match,
        clauses: &[FindClause {
            dialect: Some("posix-awk"),
            ignore_case: false,
            pattern: b"r/(ab)\\1",
        }],
        candidates: &[
            b"r", b"r/[x]", b"r/a(b)", b"r/a+b", b"r/a.b", b"r/aXb", b"r/aa", b"r/aab", b"r/ab",
            b"r/abab", b"r/a{2}", b"r/b", b"r/x$y", b"r/x^y",
        ],
        matched: &[b"r/abab"],
    },
    FindRow {
        id: "fd149-regextype-awk",
        kind: FindKind::Match,
        clauses: &[FindClause {
            dialect: Some("awk"),
            ignore_case: false,
            pattern: b".*[\\]x].*",
        }],
        candidates: &[
            b"r", b"r/[x]", b"r/a(b)", b"r/a+b", b"r/a.b", b"r/aXb", b"r/aa", b"r/aab", b"r/ab",
            b"r/abab", b"r/a{2}", b"r/b", b"r/x$y", b"r/x^y",
        ],
        matched: &[b"r/[x]", b"r/x$y", b"r/x^y"],
    },
    FindRow {
        id: "fd150-regextype-gnu-awk",
        kind: FindKind::Match,
        clauses: &[FindClause {
            dialect: Some("gnu-awk"),
            ignore_case: false,
            pattern: b"r/a\\w",
        }],
        candidates: &[
            b"r", b"r/[x]", b"r/a(b)", b"r/a+b", b"r/a.b", b"r/aXb", b"r/aa", b"r/aab", b"r/ab",
            b"r/abab", b"r/a{2}", b"r/b", b"r/x$y", b"r/x^y",
        ],
        matched: &[b"r/aa", b"r/ab"],
    },
    FindRow {
        id: "fd151-regextype-posix-minimal-basic",
        kind: FindKind::Match,
        clauses: &[FindClause {
            dialect: Some("posix-minimal-basic"),
            ignore_case: false,
            pattern: b"r/a\\+b",
        }],
        candidates: &[
            b"r", b"r/[x]", b"r/a(b)", b"r/a+b", b"r/a.b", b"r/aXb", b"r/aa", b"r/aab", b"r/ab",
            b"r/abab", b"r/a{2}", b"r/b", b"r/x$y", b"r/x^y",
        ],
        matched: &[b"r/a+b"],
    },
    FindRow {
        id: "fd152-regextype-positional",
        kind: FindKind::Match,
        clauses: &[
            FindClause {
                dialect: None,
                ignore_case: false,
                pattern: b"r/a+b",
            },
            FindClause {
                dialect: Some("posix-basic"),
                ignore_case: false,
                pattern: b"r/a\\?b",
            },
        ],
        candidates: &[
            b"r", b"r/[x]", b"r/a(b)", b"r/a+b", b"r/a.b", b"r/aXb", b"r/aa", b"r/aab", b"r/ab",
            b"r/abab", b"r/a{2}", b"r/b", b"r/x$y", b"r/x^y",
        ],
        matched: &[b"r/aab", b"r/ab", b"r/b"],
    },
    FindRow {
        id: "fd153-regex-posix-basic-badrepeat",
        kind: FindKind::Compile,
        clauses: &[FindClause {
            dialect: Some("posix-basic"),
            ignore_case: false,
            pattern: b"r/a**",
        }],
        candidates: &[
            b"r", b"r/[x]", b"r/a(b)", b"r/a+b", b"r/a.b", b"r/aXb", b"r/aa", b"r/aab", b"r/ab",
            b"r/abab", b"r/a{2}", b"r/b", b"r/x$y", b"r/x^y",
        ],
        matched: &[],
    },
    FindRow {
        id: "ux01-derived-posix-basic-interval",
        kind: FindKind::Match,
        clauses: &[FindClause {
            dialect: Some("posix-basic"),
            ignore_case: false,
            pattern: b"r/a\\{2\\}",
        }],
        candidates: &[
            b"r", b"r/[x]", b"r/a(b)", b"r/a+b", b"r/a.b", b"r/aXb", b"r/aa", b"r/aab", b"r/ab",
            b"r/abab", b"r/a{2}", b"r/b", b"r/x$y", b"r/x^y",
        ],
        matched: &[b"r/aa"],
    },
    FindRow {
        id: "ux02-derived-sed-optional",
        kind: FindKind::Match,
        clauses: &[FindClause {
            dialect: Some("sed"),
            ignore_case: false,
            pattern: b"r/a\\?b",
        }],
        candidates: &[
            b"r", b"r/[x]", b"r/a(b)", b"r/a+b", b"r/a.b", b"r/aXb", b"r/aa", b"r/aab", b"r/ab",
            b"r/abab", b"r/a{2}", b"r/b", b"r/x$y", b"r/x^y",
        ],
        matched: &[b"r/ab", b"r/b"],
    },
    FindRow {
        id: "ux03-derived-gnu-awk-word",
        kind: FindKind::Match,
        clauses: &[FindClause {
            dialect: Some("gnu-awk"),
            ignore_case: false,
            pattern: b"r/a\\w",
        }],
        candidates: &[
            b"r", b"r/[x]", b"r/a(b)", b"r/a+b", b"r/a.b", b"r/aXb", b"r/aa", b"r/aab", b"r/ab",
            b"r/abab", b"r/a{2}", b"r/b", b"r/x$y", b"r/x^y",
        ],
        matched: &[b"r/aa", b"r/ab"],
    },
];
