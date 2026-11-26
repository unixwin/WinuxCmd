//! `cat` — GNU coreutils cat.c (9.4) semantics, ported for WinuxCmd 2.0 (#1488).
//!
//! Concatenates FILE(s) (or stdin) to stdout with `-A/-b/-e/-E/-n/-s/-t/-T/-u/-v`,
//! fast passthrough, dynamic 6+ digit line numbering, cross-chunk `pending_cr`
//! under `-E`, and explicit `Io`/`Ctx`.

use std::ffi::OsString;
use std::io::{Read, Write};

use crate::argopt;
use winux_core::{usage_error, CmdResult, Ctx, Io};

#[derive(Clone, Copy, Default)]
struct CatFlags {
    show_nonprinting: bool,
    show_tabs: bool,
    number_all: bool,
    number_nonblank: bool,
    show_ends: bool,
    squeeze_blank: bool,
}

impl CatFlags {
    fn fast_passthrough(self) -> bool {
        !self.show_nonprinting
            && !self.show_tabs
            && !self.number_all
            && !self.number_nonblank
            && !self.show_ends
            && !self.squeeze_blank
    }

    fn byte_is_verbatim(self, c: u8) -> bool {
        if c == b'\n' {
            return false;
        }
        if c == b'\r' && self.show_ends && !self.show_nonprinting {
            return false;
        }
        if self.show_nonprinting {
            return (0x20..=0x7E).contains(&c);
        }
        if c == b'\t' {
            return !self.show_tabs;
        }
        true
    }
}

struct CatState {
    line_num: u64,
    at_line_start: bool,
    previous_line_empty: bool,
    pending_cr: bool,
}

impl Default for CatState {
    fn default() -> Self {
        Self {
            line_num: 1,
            at_line_start: true,
            previous_line_empty: false,
            pending_cr: false,
        }
    }
}

fn push_line_number(line_num: &mut u64, out: &mut Vec<u8>) {
    let n = *line_num;
    *line_num += 1;
    let s = format!("{n:>6}\t");
    out.extend_from_slice(s.as_bytes());
}

fn push_visible_byte(c: u8, flags: CatFlags, out: &mut Vec<u8>) {
    if flags.show_nonprinting {
        if c >= 0x20 {
            if c < 0x7F {
                out.push(c);
            } else if c == 0x7F {
                out.extend_from_slice(b"^?");
            } else {
                out.extend_from_slice(b"M-");
                let low = c - 0x80;
                if low >= 0x20 {
                    if low < 0x7F {
                        out.push(low);
                    } else {
                        out.extend_from_slice(b"^?");
                    }
                } else {
                    out.push(b'^');
                    out.push(low + 0x40);
                }
            }
        } else if c == b'\t' && !flags.show_tabs {
            out.push(b'\t');
        } else {
            out.push(b'^');
            out.push(c + 0x40);
        }
    } else if c == b'\t' && flags.show_tabs {
        out.extend_from_slice(b"^I");
    } else {
        out.push(c);
    }
}

fn feed_byte(c: u8, flags: CatFlags, state: &mut CatState, out: &mut Vec<u8>) {
    if state.at_line_start && c == b'\n' {
        if flags.squeeze_blank && state.previous_line_empty {
            return;
        }
        if flags.number_all {
            push_line_number(&mut state.line_num, out);
        }
        if flags.show_ends {
            out.push(b'$');
        }
        out.push(b'\n');
        state.previous_line_empty = true;
        return;
    }

    if state.at_line_start {
        state.previous_line_empty = false;
        if flags.number_all || flags.number_nonblank {
            push_line_number(&mut state.line_num, out);
        }
        state.at_line_start = false;
    }

    if state.pending_cr {
        state.pending_cr = false;
        if c == b'\n' {
            out.extend_from_slice(b"^M");
        } else {
            out.push(b'\r');
        }
    }

    if c == b'\n' {
        if flags.show_ends {
            out.push(b'$');
        }
        out.push(b'\n');
        state.at_line_start = true;
    } else if c == b'\r' && flags.show_ends && !flags.show_nonprinting {
        state.pending_cr = true;
    } else {
        push_visible_byte(c, flags, out);
    }
}

fn process_reader(
    reader: &mut dyn Read,
    out: &mut dyn Write,
    flags: CatFlags,
    state: &mut CatState,
) -> Result<(), (bool, std::io::Error)> {
    let mut buf = [0u8; 64 * 1024];
    if flags.fast_passthrough() {
        loop {
            let n = reader.read(&mut buf).map_err(|e| (false, e))?;
            if n == 0 {
                break;
            }
            out.write_all(&buf[..n]).map_err(|e| (true, e))?;
        }
        return Ok(());
    }

    let mut out_buf: Vec<u8> = Vec::with_capacity(64 * 1024);
    loop {
        let n = reader.read(&mut buf).map_err(|e| (false, e))?;
        if n == 0 {
            break;
        }
        let chunk = &buf[..n];
        let mut i = 0usize;
        while i < chunk.len() {
            let c = chunk[i];
            if !state.at_line_start && !state.pending_cr && flags.byte_is_verbatim(c) {
                let mut j = i + 1;
                while j < chunk.len() && flags.byte_is_verbatim(chunk[j]) {
                    j += 1;
                }
                out_buf.extend_from_slice(&chunk[i..j]);
                i = j;
            } else {
                feed_byte(c, flags, state, &mut out_buf);
                i += 1;
            }
            if out_buf.len() >= 64 * 1024 {
                out.write_all(&out_buf).map_err(|e| (true, e))?;
                out_buf.clear();
            }
        }
    }
    if !out_buf.is_empty() {
        out.write_all(&out_buf).map_err(|e| (true, e))?;
    }
    Ok(())
}

pub fn cat_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut opt_number = false;
    let mut opt_nonblank = false;
    let mut show_ends = false;
    let mut show_tabs = false;
    let mut show_nonprinting = false;
    let mut squeeze_blank = false;
    let mut raw_operands: Vec<String> = Vec::new();

    let mut idx = 0usize;
    while idx < args.len() {
        let arg = args[idx].to_string_lossy().into_owned();
        if arg == "--" {
            idx += 1;
            for a in &args[idx..] {
                raw_operands.push(a.to_string_lossy().into_owned());
            }
            break;
        }
        if arg == "-" || !arg.starts_with('-') {
            raw_operands.push(arg);
            idx += 1;
            continue;
        }
        if let Some(long) = arg.strip_prefix("--") {
            match long {
                "show-all" => {
                    show_nonprinting = true;
                    show_ends = true;
                    show_tabs = true;
                }
                "number-nonblank" => opt_nonblank = true,
                "show-ends" => show_ends = true,
                "number" => opt_number = true,
                "squeeze-blank" => squeeze_blank = true,
                "show-tabs" => show_tabs = true,
                "show-nonprinting" => show_nonprinting = true,
                "help" | "version" => return Ok(0),
                _ => {
                    let _ = usage_error(ctx, io, "cat", &argopt::unrecognized_option(ctx, &arg));
                    return Ok(1);
                }
            }
            idx += 1;
            continue;
        }
        for c in arg[1..].chars() {
            match c {
                'A' => {
                    show_nonprinting = true;
                    show_ends = true;
                    show_tabs = true;
                }
                'b' => opt_nonblank = true,
                'e' => {
                    show_nonprinting = true;
                    show_ends = true;
                }
                'E' => show_ends = true,
                'n' => opt_number = true,
                's' => squeeze_blank = true,
                't' => {
                    show_nonprinting = true;
                    show_tabs = true;
                }
                'T' => show_tabs = true,
                'u' => {}
                'v' => show_nonprinting = true,
                _ => {
                    let _ = usage_error(ctx, io, "cat", &argopt::invalid_option(ctx, &c.to_string()));
                    return Ok(1);
                }
            }
        }
        idx += 1;
    }

    let flags = CatFlags {
        show_nonprinting,
        show_tabs,
        number_all: opt_number && !opt_nonblank,
        number_nonblank: opt_nonblank,
        show_ends,
        squeeze_blank,
    };

    let mut files: Vec<String> = Vec::new();
    for op in raw_operands {
        if crate::glob::contains_wildcard(&op) {
            if let Some(expanded) = crate::glob::glob_expand_in(ctx, &op) {
                files.extend(expanded);
                continue;
            }
        }
        files.push(op);
    }
    if files.is_empty() {
        files.push("-".to_string());
    }

    let mut state = CatState::default();
    let mut exit_code = 0u32;

    for file in &files {
        if file == "-" || file == "/dev/stdin" || file == "/dev/fd/0" || file == "/proc/self/fd/0" {
            if let Err((is_write, e)) =
                process_reader(&mut io.stdin, &mut *io.stdout, flags, &mut state)
            {
                if is_write {
                    let _ = writeln!(io.stderr, "cat: write error: {}", crate::fsutil::reason(&e));
                    return Ok(1);
                }
                let _ = writeln!(io.stderr, "cat: {file}: {}", crate::fsutil::reason(&e));
                exit_code = 1;
            }
            continue;
        }
        match crate::fsutil::open_read_operand(ctx, file) {
            Ok(mut f) => {
                if let Err((is_write, e)) =
                    process_reader(&mut f, &mut *io.stdout, flags, &mut state)
                {
                    if is_write {
                        let _ =
                            writeln!(io.stderr, "cat: write error: {}", crate::fsutil::reason(&e));
                        return Ok(1);
                    }
                    let _ = writeln!(io.stderr, "cat: {file}: {}", crate::fsutil::reason(&e));
                    exit_code = 1;
                }
            }
            Err(e) => {
                let reason = crate::fsutil::gnu_open_reason(ctx, file, &e, false);
                let _ = writeln!(io.stderr, "cat: {file}: {reason}");
                exit_code = 1;
            }
        }
    }

    if state.pending_cr {
        if let Err(e) = io.stdout.write_all(b"\r") {
            let _ = writeln!(io.stderr, "cat: write error: {}", crate::fsutil::reason(&e));
            return Ok(1);
        }
    }

    if let Err(e) = io.stdout.flush() {
        let _ = writeln!(io.stderr, "cat: write error: {}", crate::fsutil::reason(&e));
        return Ok(1);
    }
    Ok(exit_code)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run_mem(args: &[&str], stdin_str: &str) -> (u32, String, String) {
        let sc = ScratchCtx::new("cat");
        let mut io = MemIo::new(stdin_str);
        let ctx = sc.ctx();
        let os_args: Vec<OsString> = args.iter().map(OsString::from).collect();
        let rc = cat_cmd(&os_args, &mut io.split(), &ctx).unwrap();
        (rc, io.out(), io.err())
    }

    #[test]
    fn passthrough_and_numbering() {
        let (rc, out, err) = run_mem(&[], "hello\nworld");
        assert_eq!(rc, 0);
        assert!(err.is_empty());
        assert_eq!(out, "hello\nworld");

        let (rc, out, _) = run_mem(&["-n"], "a\n\nb");
        assert_eq!(rc, 0);
        assert_eq!(out, "     1\ta\n     2\t\n     3\tb");

        let (rc, out, _) = run_mem(&["-n", "-b"], "a\n\nb\n");
        assert_eq!(rc, 0);
        assert_eq!(out, "     1\ta\n\n     2\tb\n");
    }

    #[test]
    fn show_ends_crlf_and_squeeze_and_vis() {
        let (rc, out, _) = run_mem(&["-E"], "a\r\nb\rc\r");
        assert_eq!(rc, 0);
        assert_eq!(out, "a^M$\nb\rc\r");

        let (rc, out, _) = run_mem(&["-s"], "a\n\n\nb\n");
        assert_eq!(rc, 0);
        assert_eq!(out, "a\n\nb\n");

        let (rc, out, _) = run_mem(&["-A"], "\x01\t\x7f\n");
        assert_eq!(rc, 0);
        assert_eq!(out, "^A^I^?$\n");
    }
}
