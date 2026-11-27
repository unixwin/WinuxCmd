//! `look` — util-linux look.c (2.39.3) semantics, ported for WinuxCmd 2.0 (#1488).
//!
//! Performs binary search on a sorted FILE for lines beginning with STRING.
//! Explicit `Io`/`Ctx`, no process-global stdio or exit().

use std::ffi::OsString;
use std::io::Write;

use crate::argopt;
use winux_core::{usage_error, CmdResult, Ctx, Io};

struct Config {
    alphanum: bool,
    ignore_case: bool,
    #[allow(dead_code)]
    alternative: bool,
    terminator: Vec<u8>,
    key: String,
    file: String,
}

fn take_val(inline: Option<String>, args: &[OsString], idx: &mut usize) -> Option<String> {
    if let Some(v) = inline {
        return Some(v);
    }
    if *idx + 1 < args.len() {
        *idx += 1;
        return Some(args[*idx].to_string_lossy().into_owned());
    }
    None
}

fn norm_byte(mut b: u8, alphanum: bool, ignore_case: bool) -> u8 {
    if ignore_case {
        b = b.to_ascii_lowercase();
    }
    if alphanum && !b.is_ascii_alphanumeric() && b != b' ' && b != b'\t' {
        return b' ';
    }
    b
}

fn for_compare(s: &[u8], terminator: &[u8], alphanum: bool, ignore_case: bool) -> Vec<u8> {
    let mut out = Vec::with_capacity(s.len());
    for &b in s {
        let nb = norm_byte(b, alphanum, ignore_case);
        out.push(nb);
        if !terminator.is_empty() && terminator.contains(&nb) {
            break;
        }
    }
    out
}

fn split_dict_lines(data: &[u8]) -> Vec<&[u8]> {
    let mut out = Vec::new();
    let mut start = 0usize;
    while start < data.len() {
        match data[start..].iter().position(|&b| b == b'\n') {
            Some(rel) => {
                let mut line = &data[start..start + rel];
                if line.last() == Some(&b'\r') {
                    line = &line[..line.len() - 1];
                }
                out.push(line);
                start += rel + 1;
            }
            None => {
                out.push(&data[start..]);
                break;
            }
        }
    }
    out
}

pub fn look_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut alphanum = false;
    let mut ignore_case = false;
    let mut alternative = false;
    let mut terminator: Vec<u8> = Vec::new();
    let mut positionals: Vec<String> = Vec::new();

    let mut idx = 0usize;
    while idx < args.len() {
        let arg = args[idx].to_string_lossy().into_owned();
        if arg == "--" {
            idx += 1;
            for a in &args[idx..] {
                positionals.push(a.to_string_lossy().into_owned());
            }
            break;
        }
        if arg == "-" || !arg.starts_with('-') {
            positionals.push(arg);
            idx += 1;
            continue;
        }
        if let Some(long) = arg.strip_prefix("--") {
            let (name, inline) = match long.split_once('=') {
                Some((n, v)) => (n, Some(v.to_string())),
                None => (long, None),
            };
            match name {
                "alphanum" => alphanum = true,
                "ignore-case" => ignore_case = true,
                "alternative" => alternative = true,
                "terminate" => {
                    let Some(v) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(
                            ctx,
                            io,
                            "look",
                            &argopt::option_requires_argument(ctx, "--terminate"),
                        );
                        return Ok(1);
                    };
                    if !v.is_empty() {
                        terminator = v.into_bytes();
                    }
                }
                "help" | "version" => return Ok(0),
                _ => {
                    let _ = usage_error(ctx, io, "look", &argopt::unrecognized_option(ctx, &arg));
                    return Ok(1);
                }
            }
            idx += 1;
            continue;
        }

        let chars: Vec<char> = arg.chars().skip(1).collect();
        let mut ci = 0usize;
        let mut consumed_next = false;
        while ci < chars.len() {
            let c = chars[ci];
            match c {
                'd' => {
                    alphanum = true;
                    ci += 1;
                }
                'f' => {
                    ignore_case = true;
                    ci += 1;
                }
                'a' => {
                    alternative = true;
                    ci += 1;
                }
                'V' => return Ok(0),
                't' => {
                    let rest: String = chars[ci + 1..].iter().collect();
                    let v = if !rest.is_empty() {
                        ci = chars.len();
                        rest
                    } else if let Some(next_arg) = args.get(idx + 1) {
                        consumed_next = true;
                        ci = chars.len();
                        next_arg.to_string_lossy().into_owned()
                    } else {
                        let _ = usage_error(
                            ctx,
                            io,
                            "look",
                            &argopt::short_option_requires_argument(ctx, "t"),
                        );
                        return Ok(1);
                    };
                    if !v.is_empty() {
                        terminator = v.into_bytes();
                    }
                }
                _ => {
                    let _ = usage_error(ctx, io, "look", &argopt::invalid_option(ctx, &c.to_string()));
                    return Ok(1);
                }
            }
        }
        idx += if consumed_next { 2 } else { 1 };
    }

    if positionals.is_empty() {
        let _ = writeln!(io.stderr, "look: missing operand");
        let _ = writeln!(io.stderr, "Try 'look --help' for more information.");
        return Ok(1);
    }
    let cfg = Config {
        alphanum,
        ignore_case,
        alternative,
        terminator,
        key: positionals[0].clone(),
        file: positionals.get(1).cloned().unwrap_or_default(),
    };

    if cfg.file.is_empty() {
        let _ = writeln!(io.stderr, "look: no dictionary file given");
        let _ = writeln!(io.stderr, "Try 'look --help' for more information.");
        return Ok(1);
    }

    let data = match crate::fsutil::read_operand_all(ctx, &cfg.file) {
        Ok(b) => b,
        Err(e) => {
            let reason = if crate::fsutil::failed_open_is_directory(ctx, &cfg.file) {
                "Is a directory".to_string()
            } else {
                crate::fsutil::gnu_open_reason(ctx, &cfg.file, &e, false)
            };
            let _ = writeln!(io.stderr, "look: {}: {reason}", cfg.file);
            return Ok(1);
        }
    };

    let lines = split_dict_lines(&data);
    if lines.is_empty() {
        return Ok(1);
    }

    let key = for_compare(
        cfg.key.as_bytes(),
        &cfg.terminator,
        cfg.alphanum,
        cfg.ignore_case,
    );

    let mut lo = 0usize;
    let mut hi = lines.len();
    while lo < hi {
        let mid = lo + (hi - lo) / 2;
        let cmp = for_compare(lines[mid], &cfg.terminator, cfg.alphanum, cfg.ignore_case);
        if cmp < key {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    let mut printed = 0usize;
    for &line in &lines[lo..] {
        let cmp = for_compare(line, &cfg.terminator, cfg.alphanum, cfg.ignore_case);
        if !cmp.starts_with(&key) {
            if !cmp.is_empty() && key.is_empty() {
                continue;
            }
            break;
        }
        let _ = io.stdout.write_all(line);
        let _ = io.stdout.write_all(b"\n");
        printed += 1;
    }

    let _ = io.stdout.flush();
    Ok(if printed > 0 { 0 } else { 1 })
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn binary_search_and_flags() {
        let sc = ScratchCtx::new("look");
        std::fs::write(sc.path("dict.txt"), "alpha\nbravo\nCharlie\ndelta\n").unwrap();
        let dict = sc.path("dict.txt").to_string_lossy().into_owned();

        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let args = vec![
            OsString::from("-f"),
            OsString::from("cha"),
            OsString::from(&dict),
        ];
        assert_eq!(look_cmd(&args, &mut io.split(), &ctx).unwrap(), 0);
        assert_eq!(io.out(), "Charlie\n");

        let mut io2 = MemIo::new("");
        let args2 = vec![
            OsString::from("-f"),
            OsString::from("delt"),
            OsString::from(&dict),
        ];
        assert_eq!(look_cmd(&args2, &mut io2.split(), &ctx).unwrap(), 0);
        assert_eq!(io2.out(), "delta\n");

        let mut io3 = MemIo::new("");
        let args3 = vec![OsString::from("zzz"), OsString::from(&dict)];
        assert_eq!(look_cmd(&args3, &mut io3.split(), &ctx).unwrap(), 1);
        assert!(io3.out().is_empty());
    }
}
