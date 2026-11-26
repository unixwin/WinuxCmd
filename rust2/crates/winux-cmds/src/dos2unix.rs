//! `dos2unix` / `d2u` / `unix2dos` / `u2d` — line-ending converters for WinuxCmd 2.0 (#1488).
//!
//! Converts CRLF <-> LF on stdin->stdout or in-place across FILE operands
//! through the shared `fsutil` shell-namespace operand boundary (#1385).

use std::ffi::OsString;
use std::io::{Read, Write};

use crate::argopt;
use winux_core::{usage_error, CmdResult, Ctx, Io};

#[derive(Clone, Copy, PartialEq, Eq)]
enum Direction {
    DosToUnix,
    UnixToDos,
}

fn convert_bytes(input: &[u8], dir: Direction) -> Vec<u8> {
    match dir {
        Direction::DosToUnix => {
            let mut out = Vec::with_capacity(input.len());
            let mut i = 0usize;
            while i < input.len() {
                if input[i] == b'\r' && i + 1 < input.len() && input[i + 1] == b'\n' {
                    out.push(b'\n');
                    i += 2;
                } else {
                    out.push(input[i]);
                    i += 1;
                }
            }
            out
        }
        Direction::UnixToDos => {
            let mut out = Vec::with_capacity(input.len() + input.len() / 16);
            for (i, &b) in input.iter().enumerate() {
                if b == b'\n' && (i == 0 || input[i - 1] != b'\r') {
                    out.push(b'\r');
                }
                out.push(b);
            }
            out
        }
    }
}

fn run_converter(
    prog: &str,
    dir: Direction,
    args: &[OsString],
    io: &mut Io,
    ctx: &Ctx,
) -> CmdResult {
    let mut verbose = false;
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
                "verbose" => verbose = true,
                "help" | "version" => return Ok(0),
                _ => {
                    let _ = usage_error(ctx, io, prog, &argopt::unrecognized_option(ctx, &arg));
                    return Ok(1);
                }
            }
            idx += 1;
            continue;
        }
        for c in arg[1..].chars() {
            match c {
                'v' => verbose = true,
                _ => {
                    let _ = usage_error(ctx, io, prog, &argopt::invalid_option(ctx, &c.to_string()));
                    return Ok(1);
                }
            }
        }
        idx += 1;
    }

    if raw_operands.is_empty() {
        let mut buf = Vec::new();
        if let Err(e) = io.stdin.read_to_end(&mut buf) {
            let _ = writeln!(io.stderr, "{prog}: -: {}", crate::fsutil::reason(&e));
            return Ok(1);
        }
        let converted = convert_bytes(&buf, dir);
        if let Err(e) = io.stdout.write_all(&converted) {
            let _ = writeln!(io.stderr, "{prog}: write error: {}", crate::fsutil::reason(&e));
            return Ok(1);
        }
        let _ = io.stdout.flush();
        return Ok(0);
    }

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

    let mut all_ok = true;
    for file in &files {
        let content = match crate::fsutil::read_operand_all(ctx, file) {
            Ok(b) => b,
            Err(e) => {
                let reason = crate::fsutil::gnu_open_reason(ctx, file, &e, false);
                let _ = writeln!(io.stderr, "{prog}: cannot open '{file}': {reason}");
                all_ok = false;
                continue;
            }
        };
        let converted = convert_bytes(&content, dir);
        match crate::fsutil::open_write_operand(ctx, file, false) {
            Ok(mut out_file) => {
                if let Err(e) = out_file.write_all(&converted).and_then(|_| out_file.flush()) {
                    let _ = writeln!(
                        io.stderr,
                        "{prog}: cannot write to '{file}': {}",
                        crate::fsutil::reason(&e)
                    );
                    all_ok = false;
                    continue;
                }
            }
            Err(_) => {
                let _ = writeln!(io.stderr, "{prog}: cannot write to '{file}'");
                all_ok = false;
                continue;
            }
        }
        if verbose {
            let _ = writeln!(io.stdout, "{prog}: converted '{file}'");
        }
    }

    let _ = io.stdout.flush();
    Ok(if all_ok { 0 } else { 1 })
}

pub fn dos2unix_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    run_converter("dos2unix", Direction::DosToUnix, args, io, ctx)
}

pub fn d2u_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    run_converter("d2u", Direction::DosToUnix, args, io, ctx)
}

pub fn unix2dos_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    run_converter("unix2dos", Direction::UnixToDos, args, io, ctx)
}

pub fn u2d_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    run_converter("u2d", Direction::UnixToDos, args, io, ctx)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn stdin_conversions() {
        let sc = ScratchCtx::new("dos2unix");
        let ctx = sc.ctx();

        let mut io1 = MemIo::new("a\r\nb\r\nc");
        assert_eq!(dos2unix_cmd(&[], &mut io1.split(), &ctx).unwrap(), 0);
        assert_eq!(io1.out(), "a\nb\nc");

        let mut io2 = MemIo::new("a\r\nb\nc\n");
        assert_eq!(unix2dos_cmd(&[], &mut io2.split(), &ctx).unwrap(), 0);
        assert_eq!(io2.out(), "a\r\nb\r\nc\r\n");
    }

    #[test]
    fn in_place_and_verbose() {
        let sc = ScratchCtx::new("dos2unix-inplace");
        let p = sc.path("sample.txt");
        std::fs::write(&p, b"one\r\ntwo\r\n").unwrap();
        let p_str = p.to_string_lossy().into_owned();

        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let args = vec![OsString::from("-v"), OsString::from(&p_str)];
        assert_eq!(d2u_cmd(&args, &mut io.split(), &ctx).unwrap(), 0);
        assert_eq!(std::fs::read(&p).unwrap(), b"one\ntwo\n");
        assert!(io.out().contains("d2u: converted"));

        let mut io2 = MemIo::new("");
        let args2 = vec![OsString::from(&p_str)];
        assert_eq!(u2d_cmd(&args2, &mut io2.split(), &ctx).unwrap(), 0);
        assert_eq!(std::fs::read(&p).unwrap(), b"one\r\ntwo\r\n");
    }
}
