//! `unexpand` — GNU coreutils unexpand semantics (src/unexpand.c +
//! expand-common.c, coreutils 9.4). Every case below was probed
//! byte-for-byte against WSL coreutils 9.4 (target/probes/p_expand*.sh).
//!
//! Blanks compress to the shortest tab/space form. By default only leading
//! blanks convert; -a converts every blank run. --first-only cancels -a
//! regardless of argv order (GNU probed both orders).
//!
//! Tab stops are built per occurrence in argv order (this command — unlike
//! expand — validates each stop as it is added): -t tokens add directly;
//! the obsolescent -NUM form accumulates DIGITS into a pending value which
//! a comma flushes immediately and end-of-options flushes after all -t
//! stops (so `-t 8 -t 4` is the fatal descending {8,4} while `-8 -t 4`
//! adds {4} then 8, ascending and fine — GNU probed). Repeat values of 0
//! are silently ignored (`-t /0` runs at the default interval 8).
//!
//! The conversion loop is the GNU unexpand.c algorithm verbatim: blanks
//! accumulate in a pending buffer until a non-blank arrives or a blank
//! lands exactly on a tab stop; a pending run whose first blank reached a
//! stop collapses to one or two tabs; trailing pending blanks are flushed
//! at end-of-line (unterminated final lines keep theirs).

use std::ffi::OsString;
use std::io::{Read, Write};

use crate::argopt::{self, OptSpec};
use crate::expand::{parse_token, Mode, TabStops};
use crate::fsutil::resolve;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('t'),
        long: "tabs",
        takes_value: true,
    },
    OptSpec {
        short: Some('a'),
        long: "all",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "first-only",
        takes_value: false,
    },
];

/// add_tab_stop: per-stop validation at add time (expand-common.c).
fn add_stop(ts: &mut TabStops, value: u64) -> Result<(), String> {
    if value == 0 {
        return Err("tab size cannot be 0".to_string());
    }
    if ts.stops.last().map(|last| value <= *last).unwrap_or(false) {
        return Err("tab sizes must be ascending".to_string());
    }
    ts.stops.push(value);
    Ok(())
}

/// Parse one -t/--tabs spec occurrence, adding its stops as they come.
fn add_spec(
    ts: &mut TabStops,
    spec: &str,
    slash: &mut bool,
    plus: &mut bool,
) -> Result<(), String> {
    let normalized = spec.replace(',', " ");
    let tokens: Vec<&str> = normalized.split_whitespace().collect();
    for (i, token) in tokens.iter().enumerate() {
        let (marker, value, _) = parse_token(token)?;
        match marker {
            None => add_stop(ts, value)?,
            Some(m) => {
                if value == 0 {
                    // GNU probed: a repeat value of 0 is silently ignored.
                    continue;
                }
                if i + 1 != tokens.len() {
                    return Err(format!("'{m}' specifier only allowed with the last value"));
                }
                if m == '/' {
                    *slash = true;
                } else {
                    *plus = true;
                }
                ts.interval = value;
                ts.mode = if m == '/' {
                    Mode::EveryMultiple
                } else {
                    Mode::AfterLast
                };
            }
        }
    }
    Ok(())
}

/// Expand-common.c get_next_tab_column; LAST_TAB is set when no further
/// stop exists (unexpand stops converting from there on).
fn next_tab_column(column: u64, ts: &TabStops, last_tab: &mut bool) -> u64 {
    for stop in &ts.stops {
        if *stop > column {
            return *stop;
        }
    }
    let next_multiple = |col: u64, interval: u64| -> u64 {
        let delta = interval - col % interval;
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
        Mode::None => {
            *last_tab = true;
            column
        }
    }
}

/// Convert one line (unexpand.c's pending-blank algorithm).
fn unexpand_line(line: &str, ts: &TabStops, all_spaces: bool) -> String {
    let mut result = String::with_capacity(line.len());
    let mut convert = true;
    let mut column: u64 = 0;
    let mut one_blank_before_tab_stop = false;
    let mut prev_blank = true;
    let mut pending_blank = String::new();

    for c in line.chars() {
        let mut out_c = c;
        if convert {
            let blank = c == ' ' || c == '\t';
            if blank {
                let mut last_tab = false;
                let next_tab_column = next_tab_column(column, ts, &mut last_tab);
                if last_tab {
                    convert = false;
                }
                if convert {
                    if c == '\t' {
                        column = next_tab_column;
                        if let Some(first) = pending_blank.chars().next() {
                            if first == ' ' {
                                pending_blank.replace_range(0..1, "\t");
                            }
                        }
                    } else {
                        column = column.saturating_add(1);
                        if !(prev_blank && column == next_tab_column) {
                            if column == next_tab_column {
                                one_blank_before_tab_stop = true;
                            }
                            pending_blank.push(c);
                            prev_blank = true;
                            continue;
                        }
                        if pending_blank.is_empty() {
                            pending_blank.push(' ');
                        }
                        pending_blank.replace_range(0..1, "\t");
                        // [GNU] c is reassigned to '\t' so the final
                        // append emits the tab, not the blank.
                        out_c = '\t';
                    }
                    let keep = if one_blank_before_tab_stop { 1 } else { 0 };
                    pending_blank.truncate(keep);
                }
            } else if c == '\u{8}' {
                column = column.saturating_sub(1);
            } else {
                column = column.saturating_add(1);
            }

            if !pending_blank.is_empty() {
                if pending_blank.chars().count() > 1 && one_blank_before_tab_stop {
                    pending_blank.replace_range(0..1, "\t");
                }
                result.push_str(&pending_blank);
                pending_blank.clear();
                one_blank_before_tab_stop = false;
            }
            prev_blank = blank;
            convert = convert && (all_spaces || blank);
        }
        result.push(out_c);
    }

    // EOF flush of pending blanks (unterminated final line).
    if !pending_blank.is_empty() {
        if pending_blank.chars().count() > 1 && one_blank_before_tab_stop {
            pending_blank.replace_range(0..1, "\t");
        }
        result.push_str(&pending_blank);
    }

    result
}

pub fn unexpand_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    // Walk argv once: -t/--tabs specs and -NUM digit runs build the stop
    // list here (in occurrence order, with the pending-value rules); every
    // other argument goes to argopt, which still rejects unknown options.
    let mut ts = TabStops {
        interval: 8,
        mode: Mode::EveryMultiple,
        stops: Vec::new(),
    };
    let mut pending: u64 = 0;
    let mut have_pending = false;
    let mut slash_repeat = false;
    let mut plus_repeat = false;
    let mut explicit_tabs = false;
    let mut rest: Vec<OsString> = Vec::with_capacity(args.len());
    let mut i = 0usize;
    let mut no_more_options = false;
    while i < args.len() {
        let text = args[i].to_string_lossy().into_owned();
        if no_more_options || text == "-" || !text.starts_with('-') || text == "--" {
            if text == "--" && !no_more_options {
                no_more_options = true;
            }
            rest.push(args[i].clone());
            i += 1;
            continue;
        }
        let body = &text[1..];
        if body.starts_with('-') {
            // long option: --tabs[=SPEC], --all, --first-only, --help/version
            let long = &body[1..];
            if let Some(spec) = long.strip_prefix("tabs=") {
                explicit_tabs = true;
                if let Err(e) = add_spec(&mut ts, spec, &mut slash_repeat, &mut plus_repeat) {
                    let _ = writeln!(io.stderr, "unexpand: {e}");
                    return Ok(1);
                }
                i += 1;
                continue;
            }
            if long == "tabs" {
                // consume the detached value here so occurrence order holds
                explicit_tabs = true;
                let spec = args.get(i + 1).map(|a| a.to_string_lossy().into_owned());
                match spec {
                    Some(spec) => {
                        if let Err(e) =
                            add_spec(&mut ts, &spec, &mut slash_repeat, &mut plus_repeat)
                        {
                            let _ = writeln!(io.stderr, "unexpand: {e}");
                            return Ok(1);
                        }
                        i += 2;
                        continue;
                    }
                    None => {
                        let _ = winux_core::usage_error(
                            io,
                            "unexpand",
                            "option '--tabs' requires an argument",
                        );
                        return Ok(1);
                    }
                }
            }
            rest.push(args[i].clone());
            i += 1;
            continue;
        }
        // short cluster
        let chars: Vec<char> = body.chars().collect();
        let mut ci = 0usize;
        let mut consumed = false;
        while ci < chars.len() {
            let c = chars[ci];
            if c.is_ascii_digit() {
                // obsolescent digit run: rest of this argv element
                explicit_tabs = true;
                for ch in &chars[ci..] {
                    if ch.is_ascii_digit() {
                        pending = pending
                            .saturating_mul(10)
                            .saturating_add((*ch as u64) - ('0' as u64));
                        have_pending = true;
                    } else if *ch == ',' {
                        if let Err(e) = add_stop(&mut ts, pending) {
                            let _ = writeln!(io.stderr, "unexpand: {e}");
                            return Ok(1);
                        }
                        pending = 0;
                    } else {
                        let msg = format!("invalid option -- '{ch}'");
                        let _ = winux_core::usage_error(io, "unexpand", &msg);
                        return Ok(1);
                    }
                }
                consumed = true;
                break;
            } else if c == 't' {
                explicit_tabs = true;
                let val: String = chars[ci + 1..].iter().collect();
                if val.is_empty() {
                    let spec = args.get(i + 1).map(|a| a.to_string_lossy().into_owned());
                    match spec {
                        Some(spec) => {
                            if let Err(e) =
                                add_spec(&mut ts, &spec, &mut slash_repeat, &mut plus_repeat)
                            {
                                let _ = writeln!(io.stderr, "unexpand: {e}");
                                return Ok(1);
                            }
                            i += 1;
                        }
                        None => {
                            let _ = winux_core::usage_error(
                                io,
                                "unexpand",
                                "option '-t' requires an argument",
                            );
                            return Ok(1);
                        }
                    }
                } else if let Err(e) = add_spec(&mut ts, &val, &mut slash_repeat, &mut plus_repeat)
                {
                    let _ = writeln!(io.stderr, "unexpand: {e}");
                    return Ok(1);
                }
                consumed = true;
                break;
            } else {
                // flags and unknown shorts: let argopt handle them
                rest.push(args[i].clone());
                consumed = true;
                break;
            }
        }
        let _ = consumed;
        i += 1;
    }

    let p = match argopt::parse("unexpand", &rest, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if p.has("help") || p.has("version") {
        return Ok(0);
    }
    // Any --tabs/-t occurrence argopt parsed that the occurrence walk did
    // not consume (bundled clusters like -at4) still contributes its stops.
    for (k, v) in &p.values {
        if k == "tabs" {
            explicit_tabs = true;
            if let Err(e) = add_spec(&mut ts, v, &mut slash_repeat, &mut plus_repeat) {
                let _ = writeln!(io.stderr, "unexpand: {e}");
                return Ok(1);
            }
        }
    }

    // finalize: flush the pending -NUM value, mutual exclusion, tail mode.
    if have_pending {
        if let Err(e) = add_stop(&mut ts, pending) {
            let _ = writeln!(io.stderr, "unexpand: {e}");
            return Ok(1);
        }
    }
    if slash_repeat && plus_repeat {
        let _ = writeln!(
            io.stderr,
            "unexpand: '/' specifier is mutually exclusive with '+'"
        );
        return Ok(1);
    }
    if ts.stops.len() == 1 && ts.mode == Mode::EveryMultiple && slash_repeat {
        // a repeat mark over a single stop keeps the stop listed
    } else if ts.stops.len() == 1 && ts.mode == Mode::EveryMultiple && !slash_repeat {
        ts.interval = ts.stops[0];
        ts.stops.clear();
    } else if ts.stops.len() > 1 && ts.mode == Mode::EveryMultiple && !slash_repeat {
        ts.mode = Mode::None;
    }

    // [GNU] -t (and the obsolescent -NUM form) imply whole-line
    // conversion; --first-only cancels both -a and that implication.
    let all_spaces = (p.has("all") || explicit_tabs) && !p.has("first-only");

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
                let _ = writeln!(io.stderr, "unexpand: {file}: read error");
                failed = true;
                continue;
            }
            buf
        } else {
            match std::fs::metadata(resolve(ctx, file)) {
                Ok(m) if m.is_dir() => {
                    let _ = writeln!(io.stderr, "unexpand: {file}: Is a directory");
                    failed = true;
                    continue;
                }
                _ => {}
            }
            match std::fs::read(resolve(ctx, file)) {
                Ok(b) => b,
                Err(_) => {
                    let _ = writeln!(io.stderr, "unexpand: {file}: No such file or directory");
                    failed = true;
                    continue;
                }
            }
        };
        let text = String::from_utf8_lossy(&bytes);
        let mut out_bytes = Vec::with_capacity(bytes.len());
        for line in split_lines(&text) {
            out_bytes.extend_from_slice(unexpand_line(&line, &ts, all_spaces).as_bytes());
        }
        if io.stdout.write_all(&out_bytes).is_err() {
            failed = true;
        }
    }
    Ok(if failed { 1 } else { 0 })
}

fn split_lines(text: &str) -> Vec<String> {
    // Keep the newline on each line except a possibly unterminated tail;
    // unexpand_line treats '\n' as a column reset.
    let mut lines = Vec::new();
    let mut start = 0usize;
    let bytes = text.as_bytes();
    for (i, b) in bytes.iter().enumerate() {
        if *b == b'\n' {
            lines.push(text[start..=i].to_string());
            start = i + 1;
        }
    }
    if start < text.len() {
        lines.push(text[start..].to_string());
    }
    lines
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
        let sc = scratch("unexpand");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::unexpand_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    fn expect(args: &[&str], stdin: &str, want: &str) {
        let (r, out, err) = run(args, stdin);
        assert_eq!(r, 0, "rc {args:?}: {err}");
        assert_eq!(out, want, "out for {args:?}");
    }

    #[test]
    fn leading_only_by_default() {
        expect(&[], "        ab\n", "\tab\n");
        expect(&[], "ab      cd\n", "ab      cd\n");
        expect(&["-t", "4"], "    ab\n", "\tab\n");
        expect(&["-8"], "        x\n", "\tx\n");
        expect(&["-45"], "        ab\n", "        ab\n");
        expect(&["-4,8"], "        ab\n", "\t\tab\n");
    }

    #[test]
    fn all_mode() {
        expect(&["-a"], "ab      cd\n", "ab\tcd\n");
        // four blanks crossing the stop collapse to one tab plus two spaces
        expect(&["-a", "-t", "4"], "ab    cd\n", "ab\t  cd\n");
        // tabs pass through unchanged
        expect(&["-a"], "\t x\n", "\t x\n");
        // a mid-line run reaching a stop exactly
        expect(&["-a"], "a \t b\n", "a\t b\n");
        expect(&["-a", "-t", "8"], "a \t\n", "a\t\n");
        expect(&["-a", "-t", "2,3"], "ab  cd\n", "ab  cd\n");
    }

    #[test]
    fn first_only_wins_over_all() {
        expect(&["-a", "--first-only"], "ab      cd\n", "ab      cd\n");
        expect(&["--first-only", "-a"], "ab      cd\n", "ab      cd\n");
        expect(&["-a", "--first-only"], "        ab\n", "\tab\n");
    }

    #[test]
    fn pending_runs_and_stops() {
        // single blank before a stop stays a blank
        expect(&["-a", "-t", "2,4"], "a b c\n", "a b c\n");
        // blanks after the last stop are not converted (finite list)
        expect(&["-a", "-t", "2,6,+4"], "ab  c  d  e\n", "ab  c\t d\te\n");
        // trailing blanks are flushed on an unterminated final line
        expect(&["-a"], "ab  ", "ab  ");
        expect(&["-a"], "ab\t\n", "ab\t\n");
    }

    #[test]
    fn repeat_forms() {
        expect(&["-t", "/0"], "        ab\n", "\tab\n");
        expect(&["-t", "4,/3"], "a\tb\n", "a\tb\n");
        expect(
            &["-4,8,12"],
            "                    ab\n",
            "\t\t\t        ab\n",
        );
    }

    #[test]
    fn errors() {
        for (args, msg) in [
            (vec!["-t", "0"], "tab size cannot be 0"),
            (vec!["-t", "8,4"], "tab sizes must be ascending"),
            (vec!["-t", "8", "-t", "4"], "tab sizes must be ascending"),
            (vec!["--tabs=8", "-t", "4"], "tab sizes must be ascending"),
            (
                vec!["-t", "abc"],
                "tab size contains invalid character(s): 'abc'",
            ),
        ] {
            let (r, _, err) = run(&args, "");
            assert_eq!(r, 1, "rc for {args:?}");
            assert!(err.contains(msg), "msg for {args:?}: {err}");
        }
        // pending 8 flushes after 4, ascending, fine (GNU probed)
        expect(&["-8", "-t", "4"], "a         b\n", "a\t\t  b\n");
        let (r, _, err) = run(&["-4x"], "");
        assert_eq!(r, 1);
        assert!(err.contains("invalid option -- 'x'"));
    }

    #[test]
    fn files_and_errors() {
        let sc = scratch("unexpand-files");
        std::fs::write(sc.path("ok"), "        ab\n").unwrap();
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = vec!["nosuchf".into(), "ok".into()];
        let r = super::unexpand_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io
            .err()
            .contains("unexpand: nosuchf: No such file or directory"));
        assert_eq!(io.out(), "\tab\n");
        std::fs::create_dir(sc.path("ddir")).unwrap();
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = vec!["ddir".into()];
        let r = super::unexpand_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("unexpand: ddir: Is a directory"));
    }

    #[test]
    fn blank_line_keeps_newline() {
        expect(&["-a"], "\n", "\n");
    }
}
