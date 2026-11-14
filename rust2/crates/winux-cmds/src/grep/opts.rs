//! Command-line parsing: a port of grep.c `main`'s getopt_long loop
//! (grep 3.11, `short_options` / `long_options` / `get_nondigit_option`).
//!
//! Every switch takes effect in argv order exactly like the C `switch`:
//! value errors (`invalid max count`, context lengths, `-D`/`--binary-files`
//! methods, `-d` argmatch, matcher conflicts, `-f`/`--exclude-from` open
//! failures) die at their position, while `--help`/`--version`/a bogus
//! `--color` only set flags that are honored after the whole command line
//! was parsed (so `grep --help --bogus` is a usage error, probed).

use std::ffi::OsString;

use super::text::{argmatch_quote, Ui};
use winux_core::{tr, Ctx};

pub const SHORT_WITH_ARG: &[u8] = b"ABCDXdefm";
pub const SHORT_FLAGS: &[u8] = b"EFGHIPTUVabchiLlnoqRrsuvwxyZz";

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum HasArg {
    No,
    Required,
    Optional,
}

/// One `long_options` row: name, argument kind, and the switch it feeds.
pub struct LongOpt {
    pub name: &'static str,
    pub has_arg: HasArg,
    pub key: Key,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Key {
    Short(u8),
    BinaryFiles,
    Color,
    ExcludeDir,
    Exclude,
    ExcludeFrom,
    GroupSeparator,
    NoGroupSeparator,
    Help,
    Include,
    LineBuffered,
    Label,
    NoIgnoreCase,
}

/// grep.c `long_options`, same order (getopt's ambiguity list follows it).
pub const LONG_OPTS: &[LongOpt] = &[
    lo("basic-regexp", HasArg::No, Key::Short(b'G')),
    lo("extended-regexp", HasArg::No, Key::Short(b'E')),
    lo("fixed-regexp", HasArg::No, Key::Short(b'F')),
    lo("fixed-strings", HasArg::No, Key::Short(b'F')),
    lo("perl-regexp", HasArg::No, Key::Short(b'P')),
    lo("after-context", HasArg::Required, Key::Short(b'A')),
    lo("before-context", HasArg::Required, Key::Short(b'B')),
    lo("binary-files", HasArg::Required, Key::BinaryFiles),
    lo("byte-offset", HasArg::No, Key::Short(b'b')),
    lo("context", HasArg::Required, Key::Short(b'C')),
    lo("color", HasArg::Optional, Key::Color),
    lo("colour", HasArg::Optional, Key::Color),
    lo("count", HasArg::No, Key::Short(b'c')),
    lo("devices", HasArg::Required, Key::Short(b'D')),
    lo("directories", HasArg::Required, Key::Short(b'd')),
    lo("exclude", HasArg::Required, Key::Exclude),
    lo("exclude-from", HasArg::Required, Key::ExcludeFrom),
    lo("exclude-dir", HasArg::Required, Key::ExcludeDir),
    lo("file", HasArg::Required, Key::Short(b'f')),
    lo("files-with-matches", HasArg::No, Key::Short(b'l')),
    lo("files-without-match", HasArg::No, Key::Short(b'L')),
    lo("group-separator", HasArg::Required, Key::GroupSeparator),
    lo("help", HasArg::No, Key::Help),
    lo("include", HasArg::Required, Key::Include),
    lo("ignore-case", HasArg::No, Key::Short(b'i')),
    lo("no-ignore-case", HasArg::No, Key::NoIgnoreCase),
    lo("initial-tab", HasArg::No, Key::Short(b'T')),
    lo("label", HasArg::Required, Key::Label),
    lo("line-buffered", HasArg::No, Key::LineBuffered),
    lo("line-number", HasArg::No, Key::Short(b'n')),
    lo("line-regexp", HasArg::No, Key::Short(b'x')),
    lo("max-count", HasArg::Required, Key::Short(b'm')),
    lo("no-filename", HasArg::No, Key::Short(b'h')),
    lo("no-group-separator", HasArg::No, Key::NoGroupSeparator),
    lo("no-messages", HasArg::No, Key::Short(b's')),
    lo("null", HasArg::No, Key::Short(b'Z')),
    lo("null-data", HasArg::No, Key::Short(b'z')),
    lo("only-matching", HasArg::No, Key::Short(b'o')),
    lo("quiet", HasArg::No, Key::Short(b'q')),
    lo("recursive", HasArg::No, Key::Short(b'r')),
    lo("dereference-recursive", HasArg::No, Key::Short(b'R')),
    lo("regexp", HasArg::Required, Key::Short(b'e')),
    lo("invert-match", HasArg::No, Key::Short(b'v')),
    lo("silent", HasArg::No, Key::Short(b'q')),
    lo("text", HasArg::No, Key::Short(b'a')),
    lo("binary", HasArg::No, Key::Short(b'U')),
    lo("unix-byte-offsets", HasArg::No, Key::Short(b'u')),
    lo("version", HasArg::No, Key::Short(b'V')),
    lo("with-filename", HasArg::No, Key::Short(b'H')),
    lo("word-regexp", HasArg::No, Key::Short(b'w')),
];

const fn lo(name: &'static str, has_arg: HasArg, key: Key) -> LongOpt {
    LongOpt { name, has_arg, key }
}

/// One option event as getopt_long delivers it.
#[derive(Debug, Clone)]
pub enum Event {
    /// A digit option character, with the argv index of its element.
    Digit(u8, usize),
    Opt(Key, Option<OsString>),
}

/// getopt_long outcome for the whole command line.
pub struct Scanned {
    pub events: Vec<Event>,
    pub operands: Vec<OsString>,
    /// getopt's own diagnostic (message without the "grep: " prefix); it
    /// stops the scan at its position after the events before it.
    pub error: Option<String>,
}

fn lossy(s: &OsString) -> String {
    s.to_string_lossy().into_owned()
}

/// getopt_long(argc, argv, short_options, long_options): permutes operands
/// unless POSIXLY_CORRECT, `--` ends options, clusters, attached and
/// detached arguments, unambiguous long prefixes (prefixes naming entries
/// with identical has_arg/key are not ambiguous, getopt's rule).
pub fn getopt(args: &[OsString], posixly_correct: bool) -> Scanned {
    let mut events = Vec::new();
    let mut operands = Vec::new();
    let mut i = 0usize;
    while i < args.len() {
        let arg = &args[i];
        let s = lossy(arg);
        if s == "--" {
            operands.extend(args[i + 1..].iter().cloned());
            break;
        }
        if s.len() < 2 || !s.starts_with('-') {
            if posixly_correct {
                operands.extend(args[i..].iter().cloned());
                break;
            }
            operands.push(arg.clone());
            i += 1;
            continue;
        }
        if let Some(body) = s.strip_prefix("--") {
            let (name, inline) = match body.split_once('=') {
                Some((n, v)) => (n, Some(v.to_string())),
                None => (body, None),
            };
            let exact = LONG_OPTS.iter().position(|o| o.name == name);
            let idx = match exact {
                Some(k) => k,
                None => {
                    let cands: Vec<usize> = LONG_OPTS
                        .iter()
                        .enumerate()
                        .filter(|(_, o)| o.name.starts_with(name))
                        .map(|(k, _)| k)
                        .collect();
                    if cands.is_empty() {
                        return Scanned {
                            events,
                            operands,
                            error: Some(format!("unrecognized option '--{body}'")),
                        };
                    }
                    let first = &LONG_OPTS[cands[0]];
                    let ambiguous = cands.iter().any(|&k| {
                        LONG_OPTS[k].has_arg != first.has_arg || LONG_OPTS[k].key != first.key
                    });
                    if ambiguous {
                        let list: Vec<String> = cands
                            .iter()
                            .map(|&k| format!("'--{}'", LONG_OPTS[k].name))
                            .collect();
                        return Scanned {
                            events,
                            operands,
                            error: Some(format!(
                                "option '{s}' is ambiguous; possibilities: {}",
                                list.join(" ")
                            )),
                        };
                    }
                    cands[0]
                }
            };
            let opt = &LONG_OPTS[idx];
            let value = match opt.has_arg {
                HasArg::No => {
                    if inline.is_some() {
                        return Scanned {
                            events,
                            operands,
                            error: Some(format!(
                                "option '--{}' doesn't allow an argument",
                                opt.name
                            )),
                        };
                    }
                    None
                }
                HasArg::Optional => inline.map(OsString::from),
                HasArg::Required => match inline {
                    Some(v) => Some(OsString::from(v)),
                    None => {
                        i += 1;
                        match args.get(i) {
                            Some(v) => Some(v.clone()),
                            None => {
                                return Scanned {
                                    events,
                                    operands,
                                    error: Some(format!(
                                        "option '--{}' requires an argument",
                                        opt.name
                                    )),
                                }
                            }
                        }
                    }
                },
            };
            events.push(Event::Opt(opt.key, value));
            i += 1;
            continue;
        }
        // short cluster
        let chars: Vec<char> = s.chars().skip(1).collect();
        let mut ci = 0usize;
        let element = i;
        while ci < chars.len() {
            let ch = chars[ci];
            let c = if ch.is_ascii() { ch as u8 } else { 0 };
            if c.is_ascii_digit() {
                events.push(Event::Digit(c, element));
                ci += 1;
                continue;
            }
            if c != 0 && SHORT_WITH_ARG.contains(&c) {
                let rest: String = chars[ci + 1..].iter().collect();
                let val = if !rest.is_empty() {
                    OsString::from(rest)
                } else {
                    i += 1;
                    match args.get(i) {
                        Some(v) => v.clone(),
                        None => {
                            return Scanned {
                                events,
                                operands,
                                error: Some(format!("option requires an argument -- '{ch}'")),
                            }
                        }
                    }
                };
                events.push(Event::Opt(Key::Short(c), Some(val)));
                ci = chars.len();
                continue;
            }
            if c != 0 && SHORT_FLAGS.contains(&c) {
                events.push(Event::Opt(Key::Short(c), None));
                ci += 1;
                continue;
            }
            return Scanned {
                events,
                operands,
                error: Some(format!("invalid option -- '{ch}'")),
            };
        }
        i += 1;
    }
    Scanned {
        events,
        operands,
        error: None,
    }
}

/// xstrtoimax(str, 0, 10, &v, "") accepting LONGINT_OK and
/// LONGINT_OVERFLOW (clamped): leading whitespace and a sign are allowed,
/// any trailing byte is invalid.
pub fn xstrtoimax(s: &str) -> Option<i64> {
    let t = s.trim_start_matches([' ', '\t', '\n', '\r', '\x0b', '\x0c']);
    let (neg, digits) = match t.as_bytes().first() {
        Some(b'-') => (true, &t[1..]),
        Some(b'+') => (false, &t[1..]),
        _ => (false, t),
    };
    if digits.is_empty() || !digits.bytes().all(|b| b.is_ascii_digit()) {
        return None;
    }
    let mut v: i128 = 0;
    for b in digits.bytes() {
        v = (v * 10 + (b - b'0') as i128).min(i64::MAX as i128 + 1);
    }
    let v = if neg { -v } else { v };
    Some(v.clamp(i64::MIN as i128, i64::MAX as i128) as i64)
}

/// `context_length_arg`: a non-negative context count, or the GNU error.
pub fn context_length(s: &str) -> Result<i64, String> {
    match xstrtoimax(s) {
        Some(n) if n >= 0 => Ok(n),
        _ => Err(format!("{s}: invalid context length argument")),
    }
}

/// argmatch with unambiguous abbreviations (gnulib argmatch.c).
pub fn argmatch(arg: &str, valid: &[&str]) -> Result<usize, bool> {
    if let Some(k) = valid.iter().position(|v| *v == arg) {
        return Ok(k);
    }
    let cands: Vec<usize> = (0..valid.len())
        .filter(|&k| valid[k].starts_with(arg))
        .collect();
    match cands.len() {
        1 => Ok(cands[0]),
        0 => Err(false),
        _ => Err(true),
    }
}

/// The XARGMATCH failure text for `--directories` (argmatch_die ->
/// usage(EXIT_FAILURE): rc 1, probed).
pub fn argmatch_failure(diag: &mut Ui, ctx: &Ctx, arg: &str, ambiguous: bool, utf8: bool) {
    let kind = if ambiguous { "ambiguous" } else { "invalid" };
    diag.error(&tr!(
        ctx,
        "command.grep.error.invalid_directories_argument",
        "{kind} argument {arg} for {opt}",
        kind = kind,
        arg = argmatch_quote(arg, utf8),
        opt = argmatch_quote("--directories", utf8)
    ));
    let mut text = format!(
        "{}\n",
        tr!(
            ctx,
            "command.grep.error.valid_arguments_header",
            "Valid arguments are:"
        )
    );
    for v in ["read", "recurse", "skip"] {
        text.push_str(&format!("  - {}\n", argmatch_quote(v, utf8)));
    }
    diag.raw(&text);
}
