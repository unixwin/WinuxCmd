//! `fold` — GNU coreutils fold.c (9.4) semantics, ported function-by-function
//! from the source and probed byte-for-byte against WSL coreutils 9.4.
//!
//! Input is a byte stream; column accounting is per byte (tab = next 8
//! stop, backspace -1, carriage return resets to 0) unless -b counts plain
//! bytes. GNU fold is NOT multibyte-aware — `-w 3` on UTF-8 splits inside a
//! code point (probed: "héllo" -> "hé\nllo\n..."); we reproduce that.
//!
//! Fold rule: after adjusting the column for byte c, `column > width`
//! triggers a break. With -s, the last blank in the pending line is found;
//! the text up to AND INCLUDING the blank is emitted, the remainder moves
//! to the next line and c is reprocessed. Otherwise the pending line plus
//! '\n' is emitted and c starts the next line; if the pending line is
//! empty, c is kept (a tab or wide byte can exceed the width alone).
//!
//! Width parsing reproduces GNU's `xdectoumax(optarg, 1, SIZE_MAX-9)`:
//! 0 or a non-number is `invalid number of columns: 'X'` (plus
//! `: Numerical result out of range` for 0, `: Value too large for defined
//! data type` for >SIZE_MAX-9), rc 1. The `-1`..`-9` short options are
//! widths (getopt optional-arg + the `optarg--` pointer trick: `-40`
//! parses "40", `-4x` parses "4x"); digits seen in a later cluster are
//! "invalid number of columns: '4x'" — wait, digits there are `invalid
//! option` per getopt; fold.c accepts them as widths wherever they appear
//! (shortopts "bsw:0::...9::"), reproducing the pointer trick exactly.
//!
//! A failed file does not stop later files; exit rc 1. `-` is stdin.

use std::ffi::OsString;
use std::io::{Read, Write};

use winux_core::{usage_error, CmdResult, Ctx, Io};

const TAB_WIDTH: usize = 8;

/// adjust_column(): the screen column printing byte c lands on.
fn adjust_column(column: usize, c: u8, count_bytes: bool) -> usize {
    if count_bytes {
        return column + 1;
    }
    match c {
        b'\x08' => column.saturating_sub(1),
        b'\r' => 0,
        b'\t' => column + TAB_WIDTH - column % TAB_WIDTH,
        _ => column + 1,
    }
}

/// fold_file(): the fold.c fold_text() loop over a byte reader.
fn fold_file(
    reader: &mut dyn Read,
    out: &mut dyn Write,
    width: usize,
    break_spaces: bool,
    count_bytes: bool,
) -> std::io::Result<()> {
    let mut column = 0usize;
    let mut line_out: Vec<u8> = Vec::new();
    let mut buf = [0u8; 4096];
    loop {
        let n = reader.read(&mut buf)?;
        if n == 0 {
            break;
        }
        for &c in &buf[..n] {
            if c == b'\n' {
                line_out.push(c);
                out.write_all(&line_out)?;
                column = 0;
                line_out.clear();
                continue;
            }
            // rescan target (fold.c's `goto rescan`)
            loop {
                column = adjust_column(column, c, count_bytes);
                if column > width {
                    if break_spaces {
                        // find the last blank in the pending line
                        let mut logical_end = line_out.len();
                        let mut found_blank = false;
                        while logical_end > 0 {
                            logical_end -= 1;
                            if line_out[logical_end] == b' ' || line_out[logical_end] == b'\t' {
                                found_blank = true;
                                break;
                            }
                        }
                        if found_blank {
                            logical_end += 1;
                            out.write_all(&line_out[..logical_end])?;
                            out.write_all(b"\n")?;
                            let remainder: Vec<u8> = line_out[logical_end..].to_vec();
                            line_out.clear();
                            line_out.extend_from_slice(&remainder);
                            column = 0;
                            for &b in &line_out {
                                column = adjust_column(column, b, count_bytes);
                            }
                            continue; // rescan c
                        }
                    }
                    if line_out.is_empty() {
                        line_out.push(c);
                        break;
                    }
                    line_out.push(b'\n');
                    out.write_all(&line_out)?;
                    column = 0;
                    line_out.clear();
                    continue; // rescan c
                }
                line_out.push(c);
                break;
            }
        }
    }
    if !line_out.is_empty() {
        out.write_all(&line_out)?;
    }
    Ok(())
}

/// Width option value sources: -w/-W value, `-N` cluster digit, or an
/// attached value after a digit option (`-4x` parses "4x" via the getopt
/// pointer trick; fold.c lines 269-279).
#[allow(dead_code)]
enum WidthArg {
    None,
    Bad(String),
    Good(usize),
}

fn parse_width(text: &str) -> WidthArg {
    // xdectoumax(optarg, 1, SIZE_MAX - TAB_WIDTH - 1, "invalid number of
    // columns", 0): parse failure -> message with no suffix; below 1 ->
    // ": Numerical result out of range"; above the max ->
    // ": Value too large for defined data type".
    if text.is_empty()
        || !text.bytes().all(|b| b.is_ascii_digit())
        || !text.bytes().next().unwrap().is_ascii_digit()
    {
        return WidthArg::Bad(text.to_string());
    }
    // GNU parses into uintmax then range-checks; reject non-digits first.
    match text.parse::<u128>() {
        Ok(v) => {
            let max = (usize::MAX as u128) - TAB_WIDTH as u128 - 1;
            if v > max {
                WidthArg::Bad(text.to_string())
            } else if v < 1 {
                WidthArg::Bad(text.to_string())
            } else {
                WidthArg::Good(v as usize)
            }
        }
        Err(_) => WidthArg::Bad(text.to_string()),
    }
}

pub fn fold_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut break_spaces = false;
    let mut count_bytes = false;
    let mut width: usize = 80;
    let mut operands: Vec<OsString> = Vec::new();
    let mut idx = 0usize;
    while idx < args.len() {
        let arg = args[idx].to_string_lossy().into_owned();
        if arg == "--" {
            idx += 1;
            for a in &args[idx..] {
                operands.push(a.clone());
            }
            break;
        }
        if arg == "-" || !arg.starts_with('-') {
            operands.push(args[idx].clone());
            idx += 1;
            continue;
        }
        if let Some(long) = arg.strip_prefix("--") {
            let (name, inline) = match long.split_once('=') {
                Some((n, v)) => (n.to_string(), Some(v.to_string())),
                None => (long.to_string(), None),
            };
            match name.as_str() {
                "bytes" => count_bytes = true,
                "spaces" => break_spaces = true,
                "width" => match take_val(inline, &args, &mut idx) {
                    Some(v) => match parse_width(&v) {
                        WidthArg::Good(w) => width = w,
                        WidthArg::Bad(t) => {
                            return width_error(io, &t);
                        }
                        WidthArg::None => unreachable!(),
                    },
                    None => {
                        let _ = usage_error(io, "fold", "option '--width' requires an argument");
                        return Ok(1);
                    }
                },
                "help" | "version" => return Ok(0),
                _ => {
                    let _ = usage_error(io, "fold", &format!("unrecognized option '{arg}'"));
                    return Ok(1);
                }
            }
            idx += 1;
            continue;
        }
        // short cluster, GNU shortopts "bsw:0::1::2::...9::"
        let chars: Vec<char> = arg.chars().skip(1).collect();
        let mut ci = 0usize;
        let mut consumed_next = false;
        while ci < chars.len() {
            let c = chars[ci];
            match c {
                'b' => {
                    count_bytes = true;
                    ci += 1;
                }
                's' => {
                    break_spaces = true;
                    ci += 1;
                }
                'w' => {
                    let rest: String = chars[ci + 1..].iter().collect();
                    let v = if !rest.is_empty() {
                        ci = chars.len();
                        rest
                    } else {
                        match args.get(idx + 1) {
                            Some(v) => {
                                consumed_next = true;
                                ci = chars.len();
                                v.to_string_lossy().into_owned()
                            }
                            None => {
                                let _ =
                                    usage_error(io, "fold", "option requires an argument -- 'w'");
                                return Ok(1);
                            }
                        }
                    };
                    match parse_width(&v) {
                        WidthArg::Good(w) => width = w,
                        WidthArg::Bad(t) => return width_error(io, &t),
                        WidthArg::None => unreachable!(),
                    }
                }
                '0'..='9' => {
                    // getopt gives the attached value (or none). fold.c's
                    // `optarg--` trick re-parses the ORIGINAL argv text:
                    // "-42" -> "42", "-4x" -> "4x", "-4" -> "4".
                    let original = &arg[1..]; // everything after the dash
                    match parse_width(original) {
                        WidthArg::Good(w) => width = w,
                        WidthArg::Bad(t) => return width_error(io, &t),
                        WidthArg::None => unreachable!(),
                    }
                    ci = chars.len();
                }
                _ => {
                    let _ = usage_error(io, "fold", &format!("invalid option -- '{c}'"));
                    return Ok(1);
                }
            }
        }
        idx += if consumed_next { 2 } else { 1 };
    }
    // GNU also rejects an empty width value ("fold -w ''" -> invalid)
    let mut ok = true;
    if operands.is_empty() {
        match fold_file(&mut io.stdin, io.stdout, width, break_spaces, count_bytes) {
            Ok(()) => {}
            Err(_) => ok = false,
        }
    } else {
        for op in &operands {
            let name = op.to_string_lossy().into_owned();
            if name == "-" {
                if fold_file(&mut io.stdin, io.stdout, width, break_spaces, count_bytes).is_err() {
                    ok = false;
                }
                continue;
            }
            let name = crate::fs_win::map_device(&name);
            // the one operand boundary (drive dialect, carrier colons,
            // shell tree — the 1.x native_path layer)
            let path = std::path::PathBuf::from(crate::fs_win::api_path(&ctx.cwd, &name));
            match std::fs::File::open(&path) {
                Ok(mut f) => {
                    if fold_file(&mut f, io.stdout, width, break_spaces, count_bytes).is_err() {
                        let _ = writeln!(io.stderr, "fold: read error: {name}");
                        ok = false;
                    }
                }
                Err(e) => {
                    // [GNU] a directory operand reads EISDIR (probed
                    // coreutils 9.4: `fold indir` -> "fold: indir: Is a
                    // directory" rc 1); Windows fails the OPEN with
                    // ERROR_ACCESS_DENIED, so the directory attribute
                    // supplies the POSIX shape
                    let reason = if e.kind() == std::io::ErrorKind::PermissionDenied
                        && crate::fs_win::file_meta(&ctx.cwd, &name, true)
                            .map(|m| m.is_dir)
                            .unwrap_or(false)
                    {
                        "Is a directory"
                    } else {
                        reason_str(&e)
                    };
                    let _ = writeln!(io.stderr, "fold: {name}: {reason}");
                    ok = false;
                }
            }
        }
    }
    Ok(if ok { 0 } else { 1 })
}

fn reason_str(e: &std::io::Error) -> &'static str {
    match e.kind() {
        std::io::ErrorKind::NotFound => "No such file or directory",
        std::io::ErrorKind::PermissionDenied => "Permission denied",
        _ => "Invalid argument",
    }
}

fn width_error(io: &mut Io, text: &str) -> CmdResult {
    // Match xdectoumax's three failure shapes by re-deriving the cause.
    let msg = if text.bytes().all(|b| b.is_ascii_digit()) && !text.is_empty() {
        match text.parse::<u128>() {
            Ok(v) if v < 1 => {
                format!("invalid number of columns: '{text}': Numerical result out of range")
            }
            _ => format!(
                "invalid number of columns: '{text}': Value too large for defined data type"
            ),
        }
    } else {
        format!("invalid number of columns: '{text}'")
    };
    let _ = writeln!(io.stderr, "fold: {msg}");
    Ok(1)
}

fn take_val(inline: Option<String>, args: &[OsString], idx: &mut usize) -> Option<String> {
    if inline.is_some() {
        return inline;
    }
    *idx += 1;
    args.get(*idx).map(|v| v.to_string_lossy().into_owned())
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str], stdin: &str) -> (u32, String, String) {
        let sc = ScratchCtx::new("fold");
        let mut io = MemIo::new(stdin);
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = fold_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn basic_folds() {
        // GNU probed: "aaa bbb ccc" -w 4 keeps the space, then folds
        assert_eq!(run(&["-w", "4"], "aaa bbb ccc\n").1, "aaa \nbbb \nccc\n");
        assert_eq!(
            run(&["-w", "4"], "aaa bbb ccc ddd\n").1,
            "aaa \nbbb \nccc \nddd\n"
        );
        assert_eq!(run(&["-w", "4"], "abcdefghij\n").1, "abcd\nefgh\nij\n");
        assert_eq!(run(&["-w", "4"], "ab\ncd\n").1, "ab\ncd\n");
        assert_eq!(
            run(&["-w", "4"], "abcdefgh\n\nxyz\n").1,
            "abcd\nefgh\n\nxyz\n"
        );
        assert_eq!(run(&["-w", "4"], "x").1, "x");
        assert_eq!(run(&[], "abc\n").1, "abc\n");
    }

    #[test]
    fn break_spaces() {
        assert_eq!(
            run(&["-s", "-w", "6"], "hello world foo bar\n").1,
            "hello \nworld \nfoo \nbar\n"
        );
        // GNU probed oddity: a lone-space line appears
        assert_eq!(
            run(&["-s", "-w", "5"], "hello world foo bar\n").1,
            "hello\n \nworld\n foo \nbar\n"
        );
        assert_eq!(run(&["-s", "-w", "3"], "aaa bbb\n").1, "aaa\n \nbbb\n");
        assert_eq!(run(&["-s", "-w", "4"], "a b\nc\n").1, "a b\nc\n");
        assert_eq!(run(&["-s", "-w", "2"], "a b c\n").1, "a \nb \nc\n");
        assert_eq!(run(&["-s", "-w", "4"], "a  b\n").1, "a  b\n");
        assert_eq!(
            run(&["-s", "-w", "12"], "aaa      bbb\n").1,
            "aaa      bbb\n"
        );
        assert_eq!(run(&["-s", "-w", "4"], "aaa bbbb\n").1, "aaa \nbbbb\n");
        assert_eq!(run(&["-s", "-w", "3"], "a\tb\n").1, "a\n\t\nb\n");
        assert_eq!(run(&["-w", "4"], "a\tb c\n").1, "a\n\t\nb c\n");
    }

    #[test]
    fn column_adjustments() {
        // GNU probed: the tab goes on its own (over-width) line
        assert_eq!(run(&["-w", "4"], "tab\there\n").1, "tab\n\t\nhere\n");
        assert_eq!(run(&["-w", "8"], "a\tb\n").1, "a\t\nb\n");
        assert_eq!(run(&["-w", "4"], "ab\rcd\n").1, "ab\rcd\n");
        assert_eq!(run(&["-w", "4"], "abc\x08d\n").1, "abc\x08d\n");
    }

    #[test]
    fn bytes_vs_utf8() {
        // GNU fold is byte-based in column mode too: é (2 bytes) splits
        assert_eq!(run(&["-b", "-w", "3"], "h\u{e9}llo\n").1, "h\u{e9}\nllo\n");
        // non -b counts the same here (2 columns for é)
        let (r, out, _) = run(&["-w", "3"], "h\u{e9}llo\n");
        assert_eq!(r, 0);
        assert_eq!(out, "h\u{e9}\nllo\n");
    }

    #[test]
    fn width_errors() {
        let (r, _, e) = run(&["-w", "0"], "abc\n");
        assert_eq!(r, 1);
        assert!(e.contains("invalid number of columns: '0'"));
        assert!(e.contains("Numerical result out of range"));
        let (r, _, e) = run(&["-w", "x"], "abc\n");
        assert_eq!(r, 1);
        assert!(e.contains("invalid number of columns: 'x'"));
        let (r, _, e) = run(&["-w", "99999999999999999999"], "abc\n");
        assert_eq!(r, 1);
        assert!(e.contains("Value too large for defined data type"));
        let (r, _, e) = run(&["-4x"], "abc\n");
        assert_eq!(r, 1);
        assert!(e.contains("invalid number of columns: '4x'"));
    }

    #[test]
    fn digit_short_options() {
        assert_eq!(run(&["-40"], "abcdefgh\n").1, "abcdefgh\n");
        assert_eq!(run(&["-4"], "abcdefghij\n").1, "abcd\nefgh\nij\n");
        assert_eq!(run(&["-4", "-s"], "a b c d e\n").1, "a b \nc d \ne\n");
    }

    #[test]
    fn files_and_errors() {
        let sc = ScratchCtx::new("fold-files");
        std::fs::write(sc.path("f1"), "aaa bbb\n").unwrap();
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = ["-w", "4", "f1"].iter().map(Into::into).collect();
        let r = fold_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert_eq!(io.out(), "aaa \nbbb\n");
        // missing file: error, rc 1, later files still processed
        std::fs::write(sc.path("f2"), "ccc\n").unwrap();
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["-w", "4", "missing", "f2"]
            .iter()
            .map(Into::into)
            .collect();
        let r = fold_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert_eq!(io.out(), "ccc\n");
        assert!(io.err().contains("No such file or directory"));
        // dash operand reads stdin
        let mut io = MemIo::new("zz\n");
        let owned: Vec<OsString> = ["-w", "4", "-"].iter().map(Into::into).collect();
        let r = fold_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert_eq!(io.out(), "zz\n");
        assert_eq!(run(&["--bogus"], "x\n").0, 1);
        assert_eq!(run(&["--help"], "").0, 0);
    }
}
