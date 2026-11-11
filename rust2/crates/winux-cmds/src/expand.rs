//! `expand` — GNU coreutils expand semantics (src/expand.c +
//! expand-common.c, coreutils 9.4). Every case below was probed
//! byte-for-byte against WSL coreutils 9.4 (target/probes/p_expand*.sh).
//!
//! Tabs become spaces up to the next tab stop. Tab stops come from -t
//! (lists like "4,8", repeat forms "/N" every-multiple and "+N"
//! after-last-anchored) and the obsolescent -NUM form; ALL specs join into
//! ONE list in argv order (probed: `-t 4 -t 8` is {4,8}, `-8 -t 4` is the
//! fatal descending {8,4}, `-4,8 -t 2` is the fatal {4,8,2}). A repeat
//! value of 0 is ignored entirely (`-t /0` behaves as the default 8).
//!
//! * `-i` converts only tabs before the first non-blank of each line.
//! * Backspace moves the column back; column growth saturates.
//! * `-` and a missing operand mean stdin; files are processed
//!   sequentially, each with its own conversion; an open failure reports
//!   `expand: FILE: <reason>` and rc 1 while later files still run.

use std::ffi::OsString;
use std::io::{Read, Write};

use crate::argopt::{self, OptSpec};
use crate::fsutil::resolve;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('t'),
        long: "tabs",
        takes_value: true,
    },
    OptSpec {
        short: Some('i'),
        long: "initial",
        takes_value: false,
    },
];

/// Repeat mode of the tail interval (expand-common.c).
#[derive(PartialEq, Eq, Clone, Copy)]
pub enum Mode {
    /// '/N': smallest multiple of N strictly greater than the column.
    EveryMultiple,
    /// '+N': anchored at the last listed stop, then multiples of N.
    AfterLast,
    /// A finite list: after the last stop the column advances by one.
    None,
}

pub struct TabStops {
    pub stops: Vec<u64>,
    pub interval: u64,
    pub mode: Mode,
}

impl Default for TabStops {
    fn default() -> Self {
        TabStops {
            stops: Vec::new(),
            interval: 8,
            mode: Mode::EveryMultiple,
        }
    }
}

impl Default for Mode {
    fn default() -> Self {
        Mode::EveryMultiple
    }
}

fn invalid_chars(rest: &str) -> String {
    format!("tab size contains invalid character(s): '{rest}'")
}

/// Parse one spec token: optional '/'/'+' marker then decimal digits.
/// Returns (marker, value, digit_text). On bad text, the GNU diagnostics.
pub fn parse_token(token: &str) -> Result<(Option<char>, u64, String), String> {
    let marker = if token.starts_with('/') || token.starts_with('+') {
        Some(token.chars().next().unwrap())
    } else {
        None
    };
    let value_text = if marker.is_some() { &token[1..] } else { token };
    let digits: String = value_text
        .chars()
        .take_while(|c| c.is_ascii_digit())
        .collect();
    if digits.len() < value_text.len() {
        let rest = &value_text[digits.len()..];
        let c = rest.chars().next().unwrap();
        if c == '/' || c == '+' {
            return Err(format!("'{c}' specifier not at start of number: '{rest}'"));
        }
        return Err(invalid_chars(rest));
    }
    let value: u64 = if digits.is_empty() {
        return Err(invalid_chars(""));
    } else {
        match digits.parse::<u64>() {
            Ok(v) => v,
            Err(_) => return Err(format!("tab stop is too large '{digits}'")),
        }
    };
    Ok((marker, value, digits))
}

/// One-shot parse of the JOINED spec list (expand joins every -t and -NUM
/// piece in argv order into a single list, then parses and validates once).
fn parse_tab_stops(spec: &str) -> Result<TabStops, String> {
    let mut out = TabStops {
        interval: 8,
        mode: Mode::EveryMultiple,
        stops: Vec::new(),
    };
    let normalized = spec.replace(',', " ");
    let tokens: Vec<&str> = normalized.split_whitespace().collect();
    if tokens.is_empty() {
        return Ok(out);
    }

    let mut slash_repeat = false;
    let mut plus_repeat = false;
    let mut repeat_specified = false;
    let mut repeat_not_last: Option<String> = None;

    for (i, token) in tokens.iter().enumerate() {
        let (marker, value, digits) = parse_token(token)?;
        match marker {
            None => out.stops.push(value),
            Some(m) => {
                if value == 0 {
                    // GNU probed: a repeat value of 0 is silently ignored
                    // (`-t /0` runs with the default interval 8).
                    continue;
                }
                if i + 1 != tokens.len() && repeat_not_last.is_none() {
                    repeat_not_last =
                        Some(format!("'{m}' specifier only allowed with the last value"));
                }
                out.interval = value;
                out.mode = if m == '/' {
                    slash_repeat = true;
                    Mode::EveryMultiple
                } else {
                    plus_repeat = true;
                    Mode::AfterLast
                };
                repeat_specified = true;
            }
        }
    }

    if slash_repeat && plus_repeat {
        return Err("'/' specifier is mutually exclusive with '+'".to_string());
    }
    if let Some(e) = repeat_not_last {
        return Err(e);
    }
    let mut prev = 0u64;
    for stop in &out.stops {
        if *stop == 0 {
            return Err("tab size cannot be 0".to_string());
        }
        if *stop <= prev {
            return Err("tab sizes must be ascending".to_string());
        }
        prev = *stop;
    }

    // finalize_tab_stops: a single stop without a repeat becomes the
    // repeating interval; an explicit repeat over a multi-stop list pins
    // the repeat to the tail.
    if out.stops.len() == 1 && !repeat_specified {
        out.interval = out.stops[0];
        out.stops.clear();
    } else if out.stops.len() > 1 && out.mode == Mode::EveryMultiple && !slash_repeat {
        out.mode = Mode::None;
    }
    Ok(out)
}

/// The next tab stop strictly greater than `column`, saturating instead of
/// wrapping (an overflowing stop would corrupt the padding count).
fn next_stop(column: u64, ts: &TabStops) -> u64 {
    for stop in &ts.stops {
        if *stop > column {
            return *stop;
        }
    }
    let next_multiple = |col: u64, interval: u64| -> u64 {
        let delta = interval - col % interval; // in [1, interval]
        col.saturating_add(delta)
    };
    match ts.mode {
        Mode::EveryMultiple => next_multiple(column, ts.interval),
        Mode::AfterLast => {
            let anchor = ts.stops.last().copied().unwrap_or(0);
            if column < anchor {
                return anchor;
            }
            anchor.saturating_add(next_multiple(column - anchor, ts.interval))
        }
        Mode::None => column.saturating_add(1),
    }
}

/// Expand tabs in one line (expand.c process_line column walk). Padding
/// runs are streamed through a fixed-size buffer: `expand -t 99999999999`
/// must really write tens of billions of spaces without materializing them.
fn expand_line(
    line: &str,
    ts: &TabStops,
    initial_only: bool,
    out: &mut dyn Write,
) -> std::io::Result<()> {
    let mut column: u64 = 0;
    let mut before_non_blank = true;
    for c in line.chars() {
        match c {
            '\t' => {
                let next = next_stop(column, ts);
                if !initial_only || before_non_blank {
                    let mut count = next - column;
                    const CHUNK: &[u8] = &[b' '; 8192];
                    while count >= CHUNK.len() as u64 {
                        out.write_all(CHUNK)?;
                        count -= CHUNK.len() as u64;
                    }
                    out.write_all(&CHUNK[..count as usize])?;
                } else {
                    out.write_all(b"\t")?;
                }
                column = next;
            }
            '\n' => {
                out.write_all(b"\n")?;
                column = 0;
                before_non_blank = true;
            }
            '\u{8}' => {
                out.write_all(b"\x08")?;
                column = column.saturating_sub(1);
                before_non_blank = false;
            }
            _ => {
                let mut buf = [0u8; 4];
                out.write_all(c.encode_utf8(&mut buf).as_bytes())?;
                column = column.saturating_add(1);
                if c != ' ' {
                    before_non_blank = false;
                }
            }
        }
    }
    Ok(())
}

/// Normalize argv: the obsolescent `-NUM[,NUM...]` form becomes --tabs, so
/// the whole digits run (including comma continuations) is one spec piece.
/// A digit-led arg with other trailing characters is the GNU invalid-option
/// path ("expand: invalid option -- 'x'").
fn preprocess_obsolescent(
    args: &[OsString],
    prog: &str,
    io: &mut Io,
) -> Result<Vec<OsString>, u32> {
    let mut out = Vec::with_capacity(args.len());
    for a in args {
        let text = a.to_string_lossy();
        let mut chars = text.chars();
        if chars.next() == Some('-') {
            let rest: String = chars.collect();
            let mut rchars = rest.chars();
            if rchars.next().map(|c| c.is_ascii_digit()).unwrap_or(false) {
                if rest.chars().all(|c| c.is_ascii_digit() || c == ',') {
                    out.push(OsString::from(format!("--tabs={rest}")));
                } else {
                    let bad = rest
                        .chars()
                        .find(|c| !c.is_ascii_digit() && *c != ',')
                        .unwrap();
                    let msg = format!("invalid option -- '{bad}'");
                    let _ = winux_core::usage_error(io, prog, &msg);
                    return Err(1);
                }
                continue;
            }
        }
        out.push(a.clone());
    }
    Ok(out)
}

pub fn expand_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let args = match preprocess_obsolescent(args, "expand", io) {
        Ok(a) => a,
        Err(s) => return Ok(s),
    };
    let p = match argopt::parse("expand", &args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if p.has("help") || p.has("version") {
        return Ok(0);
    }
    let initial_only = p.has("initial");

    // Collect every -t piece in argv order (argopt keeps values in
    // occurrence order) and join them into one spec list.
    let mut specs: Vec<String> = Vec::new();
    for (k, v) in &p.values {
        if k == "tabs" {
            specs.push(v.clone());
        }
    }
    let tab_stops = if specs.is_empty() {
        TabStops::default()
    } else {
        match parse_tab_stops(&specs.join(" ")) {
            Ok(ts) => ts,
            Err(e) => {
                let _ = writeln!(io.stderr, "expand: {e}");
                return Ok(1);
            }
        }
    };

    let files: Vec<String> = if p.positionals.is_empty() {
        vec!["-".to_string()]
    } else {
        p.positionals
            .iter()
            .map(|a| a.to_string_lossy().into_owned())
            .collect()
    };

    let mut failed = false;
    for file in &files {
        let bytes = if file == "-" {
            let mut buf = Vec::new();
            if io.stdin.read_to_end(&mut buf).is_err() {
                let _ = writeln!(io.stderr, "expand: {file}: read error");
                failed = true;
                continue;
            }
            buf
        } else {
            match std::fs::metadata(resolve(ctx, file)) {
                Ok(m) if m.is_dir() => {
                    let _ = writeln!(io.stderr, "expand: {file}: Is a directory");
                    failed = true;
                    continue;
                }
                _ => {}
            }
            match std::fs::read(resolve(ctx, file)) {
                Ok(b) => b,
                Err(_) => {
                    let _ = writeln!(io.stderr, "expand: {file}: No such file or directory");
                    failed = true;
                    continue;
                }
            }
        };
        let text = String::from_utf8_lossy(&bytes);
        if expand_line(&text, &tab_stops, initial_only, io.stdout).is_err() {
            failed = true;
        }
    }
    Ok(if failed { 1 } else { 0 })
}

#[cfg(test)]
mod tests {
    use std::ffi::OsString;
    use std::sync::atomic::{AtomicU64, Ordering};

    static SEQ: AtomicU64 = AtomicU64::new(0);

    fn scratch(tag: &str) -> winux_core::test_support::ScratchCtx {
        let n = SEQ.fetch_add(1, Ordering::SeqCst);
        winux_core::test_support::ScratchCtx::new(&format!("{}-{}", tag, n))
    }
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str], stdin: &str) -> (u32, String, String) {
        let mut io = MemIo::new(stdin);
        let sc = scratch("expand");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::expand_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    fn expect(args: &[&str], stdin: &str, want: &str) {
        let (r, out, err) = run(args, stdin);
        assert_eq!(r, 0, "rc {args:?}: {err}");
        assert_eq!(out, want, "out for {args:?}");
    }

    #[test]
    fn default_and_single() {
        expect(&[], "a\tb\n", "a       b\n");
        expect(&["-t", "4"], "a\tb\tc\n", "a   b   c\n");
        expect(&["-t4"], "a\tb\tc\n", "a   b   c\n");
        expect(&["-8"], "a\tb\n", "a       b\n");
        expect(&["-t", "4,8"], "ab\tcd\tef\n", "ab  cd  ef\n");
        expect(&["-t", "4", "-t", "8"], "ab\tcd\tef\n", "ab  cd  ef\n");
    }

    #[test]
    fn lists_repeat_and_finite() {
        // every-multiple
        expect(&["-t", "/4"], "ab\tc\td\n", "ab  c   d\n");
        // after-last anchored
        expect(&["-t", "4,+3"], "a\tb\tc\n", "a   b  c\n");
        expect(&["-t", "2,6,+4"], "ab\tc\td\te\n", "ab    c   d   e\n");
        // the first stop strictly greater than the column wins (GNU probed)
        expect(&["-t", "2,6"], "ab\tcd\n", "ab    cd\n");
        expect(&["-t", "4,8"], "ab\tcd\tef\n", "ab  cd  ef\n");
    }

    #[test]
    fn obsolescent_forms() {
        expect(&["-45"], "ab\tcd\n", &format!("ab{}cd\n", " ".repeat(43)));
        expect(&["-4,8"], "ab\tcd\n", "ab  cd\n");
        // digit-led arg with trailing junk is the invalid-option path
        let (r, _, err) = run(&["-4x"], "ab\tcd\n");
        assert_eq!(r, 1);
        assert!(err.contains("invalid option -- 'x'"));
    }

    #[test]
    fn initial_only() {
        expect(&["-i"], "  \tword\ttail\n", "        word\ttail\n");
        expect(&["-i", "-t", "4,8"], "  \tX\n", "    X\n");
        // tab after non-blank stays a tab
        expect(&["-i"], "x\ty\n", "x\ty\n");
    }

    #[test]
    fn backspace_and_newline_reset() {
        expect(&["-t", "4"], "ab\tc\u{8}\tz\n", "ab  c\u{8}    z\n");
        // a newline resets the column and the initial-blank state
        expect(&["-i"], "x\ty\nz\tw\n", "x\ty\nz\tw\n");
    }

    #[test]
    fn errors() {
        for (args, msg) in [
            (vec!["-t", "0"], "tab size cannot be 0"),
            (vec!["-0"], "tab size cannot be 0"),
            (vec!["-t", "8,4"], "tab sizes must be ascending"),
            (vec!["-t", "4,4"], "tab sizes must be ascending"),
            (
                vec!["-t", "abc"],
                "tab size contains invalid character(s): 'abc'",
            ),
            (
                vec!["-t", "999999999999999999999"],
                "tab stop is too large '999999999999999999999'",
            ),
            (
                vec!["-t", "4/"],
                "'/' specifier not at start of number: '/'",
            ),
            (
                vec!["-t", "/4,+2"],
                "'/' specifier is mutually exclusive with '+'",
            ),
            (
                vec!["-t", "/4,8"],
                "'/' specifier only allowed with the last value",
            ),
        ] {
            let (r, _, err) = run(&args, "");
            assert_eq!(r, 1, "rc for {args:?}");
            assert!(err.contains(msg), "msg for {args:?}: {err}");
        }
        // repeat zero is silently ignored (GNU probed)
        expect(&["-t", "/0"], "a\tb\n", "a       b\n");
        expect(&["-t", "5,/0"], "a\tb\tc\n", "a    b    c\n");
        // descending specs joined from mixed forms are fatal
        let (r, _, err) = run(&["-8", "-t", "4"], "ab\tcd\n");
        assert_eq!(r, 1);
        assert!(err.contains("tab sizes must be ascending"));
    }

    #[test]
    fn files_and_errors() {
        let sc = scratch("expand-files");
        std::fs::write(sc.path("ok"), "a\tb\n").unwrap();
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = vec!["nosuchf".into(), "ok".into()];
        let r = super::expand_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io
            .err()
            .contains("expand: nosuchf: No such file or directory"));
        assert_eq!(io.out(), "a       b\n");
        // directory operand
        std::fs::create_dir(sc.path("ddir")).unwrap();
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = vec!["ddir".into()];
        let r = super::expand_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("expand: ddir: Is a directory"));
        // stdin dash
        let mut io = MemIo::new("a\tb\n");
        let owned: Vec<OsString> = vec!["-".into()];
        let r = super::expand_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert_eq!(io.out(), "a       b\n");
    }

    #[test]
    fn unterminated_line_and_empty() {
        expect(&["-t", "4"], "a\tb", "a   b");
        expect(&[], "", "");
        // a tab at the end still writes up to the stop
        expect(&[], "\t\n", "        \n");
    }
}
