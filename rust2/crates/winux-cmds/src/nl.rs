//! `nl` — GNU coreutils nl.c (9.4) semantics, ported for WinuxCmd 2.0 (#1488).
//!
//! Numbers lines of files with logical page delimiters, numbering styles
//! (`a`, `t`, `n`, `p<BRE>`), custom width/format/separator, and explicit `Io`/`Ctx`.

use std::ffi::OsString;
use std::io::{Read, Write};

use crate::argopt;
use winux_core::{usage_error, CmdResult, Ctx, Io};
use winux_regex::{Regex, Syntax};

#[derive(Clone, Copy, PartialEq, Eq)]
enum Section {
    Header,
    Body,
    Footer,
}

#[derive(Clone)]
enum NumberStyle {
    All,
    NonEmpty,
    None,
    Pattern(Regex),
}

struct Config {
    body_style: NumberStyle,
    header_style: NumberStyle,
    footer_style: NumberStyle,
    line_increment: i64,
    join_blank_lines: u64,
    number_format: String,
    no_renumber: bool,
    section_delimiter: Vec<u8>,
    separator: Vec<u8>,
    starting_number: i64,
    number_width: usize,
    files: Vec<String>,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            body_style: NumberStyle::NonEmpty,
            header_style: NumberStyle::None,
            footer_style: NumberStyle::None,
            line_increment: 1,
            join_blank_lines: 1,
            number_format: "rn".to_string(),
            no_renumber: false,
            section_delimiter: b"\\:".to_vec(),
            separator: b"\t".to_vec(),
            starting_number: 1,
            number_width: 6,
            files: Vec::new(),
        }
    }
}

fn parse_style(style: &str, section: &str) -> Result<NumberStyle, String> {
    let first = style.as_bytes().first().copied();
    match first {
        Some(b't') => Ok(NumberStyle::NonEmpty),
        Some(b'a') => Ok(NumberStyle::All),
        Some(b'n') => Ok(NumberStyle::None),
        Some(b'p') => {
            let pat = &style[1..];
            match Regex::compile(Syntax::Basic, pat) {
                Ok(re) => Ok(NumberStyle::Pattern(re)),
                Err(_) => Err(format!(
                    "invalid {section} numbering style: '{style}'\nTry 'nl --help' for more information."
                )),
            }
        }
        _ => Err(format!(
            "invalid {section} numbering style: '{style}'\nTry 'nl --help' for more information."
        )),
    }
}

fn parse_signed_i64(text: &str, what: &str) -> Result<i64, String> {
    let (neg, digits) = if let Some(rest) = text.strip_prefix('-') {
        (true, rest)
    } else if let Some(rest) = text.strip_prefix('+') {
        (false, rest)
    } else {
        (false, text)
    };
    if digits.is_empty() || !digits.bytes().all(|b| b.is_ascii_digit()) {
        return Err(format!("invalid {what}: '{text}'"));
    }
    let mag: u128 = match digits.parse() {
        Ok(v) => v,
        Err(_) => return Err(format!("invalid {what}: '{text}': Value too large for defined data type")),
    };
    let max_pos = i64::MAX as u128;
    let max_neg = (i64::MAX as u128) + 1;
    if (!neg && mag > max_pos) || (neg && mag > max_neg) {
        return Err(format!(
            "invalid {what}: '{text}': Value too large for defined data type"
        ));
    }
    if neg {
        if mag == max_neg {
            Ok(i64::MIN)
        } else {
            Ok(-(mag as i64))
        }
    } else {
        Ok(mag as i64)
    }
}

fn parse_positive_u64(text: &str, what: &str) -> Result<u64, String> {
    let (neg, digits) = if let Some(rest) = text.strip_prefix('-') {
        (true, rest)
    } else if let Some(rest) = text.strip_prefix('+') {
        (false, rest)
    } else {
        (false, text)
    };
    if digits.is_empty() || !digits.bytes().all(|b| b.is_ascii_digit()) {
        return Err(format!("invalid {what}: '{text}'"));
    }
    if neg {
        return Err(format!(
            "invalid {what}: '{text}': Numerical result out of range"
        ));
    }
    match digits.parse::<u64>() {
        Ok(v) if v >= 1 => Ok(v),
        Ok(_) => Err(format!(
            "invalid {what}: '{text}': Numerical result out of range"
        )),
        Err(_) => Err(format!(
            "invalid {what}: '{text}': Numerical result out of range"
        )),
    }
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

fn should_number_line(
    style: &NumberStyle,
    line: &[u8],
    blank_count: &mut u64,
    join_blank_lines: u64,
    terminated: bool,
) -> bool {
    match style {
        NumberStyle::None => {
            *blank_count = 0;
            false
        }
        NumberStyle::NonEmpty => {
            *blank_count = 0;
            line.len() + usize::from(terminated) > 1
        }
        NumberStyle::Pattern(re) => {
            *blank_count = 0;
            re.find_at_bytes(line, 0).is_some()
        }
        NumberStyle::All => {
            if !line.is_empty() {
                *blank_count = 0;
                return true;
            }
            *blank_count += 1;
            if *blank_count == join_blank_lines {
                *blank_count = 0;
                true
            } else {
                false
            }
        }
    }
}

fn format_line_number(line_number: i64, cfg: &Config) -> String {
    let w = cfg.number_width;
    match cfg.number_format.as_str() {
        "ln" => format!("{line_number:<w$}"),
        "rz" => format!("{line_number:0w$}"),
        _ => format!("{line_number:>w$}"),
    }
}

fn section_matches(line: &[u8], delim: &[u8], repeats: usize) -> bool {
    if delim.is_empty() || line.len() != delim.len() * repeats {
        return false;
    }
    line.chunks_exact(delim.len()).all(|chunk| chunk == delim)
}

fn process_bytes(bytes: &[u8], cfg: &Config, out: &mut dyn Write, section: &mut Section, line_number: &mut i64, blank_count: &mut u64) -> std::io::Result<()> {
    let mut start = 0usize;
    while start < bytes.len() {
        let (line, terminated, next_start) = match bytes[start..].iter().position(|&b| b == b'\n') {
            Some(rel) => (&bytes[start..start + rel], true, start + rel + 1),
            None => (&bytes[start..], false, bytes.len()),
        };

        if section_matches(line, &cfg.section_delimiter, 3) {
            *section = Section::Header;
            *blank_count = 0;
            if !cfg.no_renumber {
                *line_number = cfg.starting_number;
            }
            out.write_all(b"\n")?;
        } else if section_matches(line, &cfg.section_delimiter, 2) {
            *section = Section::Body;
            *blank_count = 0;
            if !cfg.no_renumber {
                *line_number = cfg.starting_number;
            }
            out.write_all(b"\n")?;
        } else if section_matches(line, &cfg.section_delimiter, 1) {
            *section = Section::Footer;
            *blank_count = 0;
            if !cfg.no_renumber {
                *line_number = cfg.starting_number;
            }
            out.write_all(b"\n")?;
        } else {
            let style = match section {
                Section::Header => &cfg.header_style,
                Section::Body => &cfg.body_style,
                Section::Footer => &cfg.footer_style,
            };
            let number_it = should_number_line(style, line, blank_count, cfg.join_blank_lines, terminated);
            if number_it {
                let num_str = format_line_number(*line_number, cfg);
                out.write_all(num_str.as_bytes())?;
                out.write_all(&cfg.separator)?;
            } else {
                let spaces = cfg.number_width.saturating_add(cfg.separator.len());
                for _ in 0..spaces {
                    out.write_all(b" ")?;
                }
            }
            out.write_all(line)?;
            if terminated {
                out.write_all(b"\n")?;
            }
            if number_it {
                *line_number = line_number.wrapping_add(cfg.line_increment);
            }
        }

        start = next_start;
    }
    Ok(())
}

pub fn nl_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut cfg = Config::default();
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
            let (name, inline) = match long.split_once('=') {
                Some((n, v)) => (n, Some(v.to_string())),
                None => (long, None),
            };
            match name {
                "body-numbering" => {
                    let Some(v) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(ctx, io, "nl", &argopt::option_requires_argument(ctx, "--body-numbering"));
                        return Ok(1);
                    };
                    match parse_style(&v, "body") {
                        Ok(s) => cfg.body_style = s,
                        Err(e) => {
                            let _ = writeln!(io.stderr, "nl: {e}");
                            return Ok(1);
                        }
                    }
                }
                "header-numbering" => {
                    let Some(v) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(ctx, io, "nl", &argopt::option_requires_argument(ctx, "--header-numbering"));
                        return Ok(1);
                    };
                    match parse_style(&v, "header") {
                        Ok(s) => cfg.header_style = s,
                        Err(e) => {
                            let _ = writeln!(io.stderr, "nl: {e}");
                            return Ok(1);
                        }
                    }
                }
                "footer-numbering" => {
                    let Some(v) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(ctx, io, "nl", &argopt::option_requires_argument(ctx, "--footer-numbering"));
                        return Ok(1);
                    };
                    match parse_style(&v, "footer") {
                        Ok(s) => cfg.footer_style = s,
                        Err(e) => {
                            let _ = writeln!(io.stderr, "nl: {e}");
                            return Ok(1);
                        }
                    }
                }
                "section-delimiter" => {
                    let Some(v) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(ctx, io, "nl", &argopt::option_requires_argument(ctx, "--section-delimiter"));
                        return Ok(1);
                    };
                    cfg.section_delimiter = if v.is_empty() {
                        Vec::new()
                    } else if v.len() == 1 {
                        let mut d = v.into_bytes();
                        d.push(b':');
                        d
                    } else {
                        v.into_bytes()
                    };
                }
                "line-increment" => {
                    let Some(v) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(ctx, io, "nl", &argopt::option_requires_argument(ctx, "--line-increment"));
                        return Ok(1);
                    };
                    match parse_signed_i64(&v, "line number increment") {
                        Ok(n) => cfg.line_increment = n,
                        Err(e) => {
                            let _ = writeln!(io.stderr, "nl: {e}");
                            return Ok(1);
                        }
                    }
                }
                "join-blank-lines" => {
                    let Some(v) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(ctx, io, "nl", &argopt::option_requires_argument(ctx, "--join-blank-lines"));
                        return Ok(1);
                    };
                    match parse_positive_u64(&v, "line number of blank lines") {
                        Ok(n) => cfg.join_blank_lines = n,
                        Err(e) => {
                            let _ = writeln!(io.stderr, "nl: {e}");
                            return Ok(1);
                        }
                    }
                }
                "number-format" => {
                    let Some(v) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(ctx, io, "nl", &argopt::option_requires_argument(ctx, "--number-format"));
                        return Ok(1);
                    };
                    if v != "ln" && v != "rn" && v != "rz" {
                        let _ = writeln!(
                            io.stderr,
                            "nl: invalid line numbering format: '{v}'\nTry 'nl --help' for more information."
                        );
                        return Ok(1);
                    }
                    cfg.number_format = v;
                }
                "no-renumber" => cfg.no_renumber = true,
                "number-separator" => {
                    let Some(v) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(ctx, io, "nl", &argopt::option_requires_argument(ctx, "--number-separator"));
                        return Ok(1);
                    };
                    cfg.separator = v.into_bytes();
                }
                "starting-line-number" => {
                    let Some(v) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(ctx, io, "nl", &argopt::option_requires_argument(ctx, "--starting-line-number"));
                        return Ok(1);
                    };
                    match parse_signed_i64(&v, "starting line number") {
                        Ok(n) => cfg.starting_number = n,
                        Err(e) => {
                            let _ = writeln!(io.stderr, "nl: {e}");
                            return Ok(1);
                        }
                    }
                }
                "number-width" => {
                    let Some(v) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(ctx, io, "nl", &argopt::option_requires_argument(ctx, "--number-width"));
                        return Ok(1);
                    };
                    match parse_positive_u64(&v, "line number field width") {
                        Ok(n) => cfg.number_width = n.min(i32::MAX as u64) as usize,
                        Err(e) => {
                            let _ = writeln!(io.stderr, "nl: {e}");
                            return Ok(1);
                        }
                    }
                }
                "help" | "version" => return Ok(0),
                _ => {
                    let _ = usage_error(ctx, io, "nl", &argopt::unrecognized_option(ctx, &arg));
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
                'p' => {
                    cfg.no_renumber = true;
                    ci += 1;
                }
                'b' | 'd' | 'f' | 'h' | 'i' | 'l' | 'n' | 's' | 'v' | 'w' => {
                    let rest: String = chars[ci + 1..].iter().collect();
                    let val = if !rest.is_empty() {
                        ci = chars.len();
                        rest
                    } else if let Some(next_arg) = args.get(idx + 1) {
                        consumed_next = true;
                        ci = chars.len();
                        next_arg.to_string_lossy().into_owned()
                    } else {
                        let flag = c.to_string();
                        let _ = usage_error(ctx, io, "nl", &argopt::short_option_requires_argument(ctx, &flag));
                        return Ok(1);
                    };
                    match c {
                        'b' => match parse_style(&val, "body") {
                            Ok(s) => cfg.body_style = s,
                            Err(e) => {
                                let _ = writeln!(io.stderr, "nl: {e}");
                                return Ok(1);
                            }
                        },
                        'h' => match parse_style(&val, "header") {
                            Ok(s) => cfg.header_style = s,
                            Err(e) => {
                                let _ = writeln!(io.stderr, "nl: {e}");
                                return Ok(1);
                            }
                        },
                        'f' => match parse_style(&val, "footer") {
                            Ok(s) => cfg.footer_style = s,
                            Err(e) => {
                                let _ = writeln!(io.stderr, "nl: {e}");
                                return Ok(1);
                            }
                        },
                        'd' => {
                            cfg.section_delimiter = if val.is_empty() {
                                Vec::new()
                            } else if val.len() == 1 {
                                let mut d = val.into_bytes();
                                d.push(b':');
                                d
                            } else {
                                val.into_bytes()
                            };
                        }
                        'i' => match parse_signed_i64(&val, "line number increment") {
                            Ok(n) => cfg.line_increment = n,
                            Err(e) => {
                                let _ = writeln!(io.stderr, "nl: {e}");
                                return Ok(1);
                            }
                        },
                        'l' => match parse_positive_u64(&val, "line number of blank lines") {
                            Ok(n) => cfg.join_blank_lines = n,
                            Err(e) => {
                                let _ = writeln!(io.stderr, "nl: {e}");
                                return Ok(1);
                            }
                        },
                        'n' => {
                            if val != "ln" && val != "rn" && val != "rz" {
                                let _ = writeln!(
                                    io.stderr,
                                    "nl: invalid line numbering format: '{val}'\nTry 'nl --help' for more information."
                                );
                                return Ok(1);
                            }
                            cfg.number_format = val;
                        }
                        's' => cfg.separator = val.into_bytes(),
                        'v' => match parse_signed_i64(&val, "starting line number") {
                            Ok(n) => cfg.starting_number = n,
                            Err(e) => {
                                let _ = writeln!(io.stderr, "nl: {e}");
                                return Ok(1);
                            }
                        },
                        'w' => match parse_positive_u64(&val, "line number field width") {
                            Ok(n) => cfg.number_width = n.min(i32::MAX as u64) as usize,
                            Err(e) => {
                                let _ = writeln!(io.stderr, "nl: {e}");
                                return Ok(1);
                            }
                        },
                        _ => unreachable!(),
                    }
                }
                _ => {
                    let _ = usage_error(ctx, io, "nl", &argopt::invalid_option(ctx, &c.to_string()));
                    return Ok(1);
                }
            }
        }
        idx += if consumed_next { 2 } else { 1 };
    }

    for op in raw_operands {
        if crate::glob::contains_wildcard(&op) {
            if let Some(expanded) = crate::glob::glob_expand_in(ctx, &op) {
                cfg.files.extend(expanded);
                continue;
            }
        }
        cfg.files.push(op);
    }
    if cfg.files.is_empty() {
        cfg.files.push("-".to_string());
    }

    let mut line_number = cfg.starting_number;
    let mut blank_count = 0u64;
    let mut section = Section::Body;
    let mut all_ok = true;

    for file in &cfg.files {
        if file == "-" {
            let mut buf = Vec::new();
            if let Err(e) = io.stdin.read_to_end(&mut buf) {
                let _ = writeln!(io.stderr, "nl: -: {}", crate::fsutil::reason(&e));
                all_ok = false;
                continue;
            }
            if let Err(e) = process_bytes(&buf, &cfg, io.stdout, &mut section, &mut line_number, &mut blank_count) {
                let _ = writeln!(io.stderr, "nl: write error: {}", crate::fsutil::reason(&e));
                return Ok(1);
            }
        } else {
            match crate::fsutil::read_operand_all(ctx, file) {
                Ok(buf) => {
                    if let Err(e) = process_bytes(&buf, &cfg, io.stdout, &mut section, &mut line_number, &mut blank_count) {
                        let _ = writeln!(io.stderr, "nl: write error: {}", crate::fsutil::reason(&e));
                        return Ok(1);
                    }
                }
                Err(e) => {
                    let reason = crate::fsutil::gnu_open_reason(ctx, file, &e, false);
                    let _ = writeln!(io.stderr, "nl: {file}: {reason}");
                    all_ok = false;
                }
            }
        }
    }

    let _ = io.stdout.flush();
    Ok(if all_ok { 0 } else { 1 })
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run_mem(args: &[&str], stdin_str: &str) -> (u32, String, String) {
        let sc = ScratchCtx::new("nl");
        let mut io = MemIo::new(stdin_str);
        let ctx = sc.ctx();
        let os_args: Vec<OsString> = args.iter().map(OsString::from).collect();
        let rc = nl_cmd(&os_args, &mut io.split(), &ctx).unwrap();
        (rc, io.out(), io.err())
    }

    #[test]
    fn basic_and_formats() {
        let (rc, out, err) = run_mem(&[], "a\n\nb\n");
        assert_eq!(rc, 0);
        assert!(err.is_empty());
        assert_eq!(out, "     1\ta\n       \n     2\tb\n");

        let (rc, out, _) = run_mem(&["-n", "rz", "-w", "3", "-v", "-1", "-i", "-2", "-s", ":"], "x\ny\n");
        assert_eq!(rc, 0);
        assert_eq!(out, "-01:x\n-03:y\n");
    }

    #[test]
    fn sections_and_no_renumber() {
        let (rc, out, _) = run_mem(
            &["-h", "a", "-f", "a", "-w", "1", "-s", ":"],
            "\\:\\:\\:\nhead\n\\:\\:\nbody\n\\:\nfoot\n",
        );
        assert_eq!(rc, 0);
        assert_eq!(out, "\n1:head\n\n1:body\n\n1:foot\n");

        let (rc, out, _) = run_mem(
            &["-p", "-h", "a", "-w", "1", "-s", ":"],
            "\\:\\:\\:\nhead\n\\:\\:\nbody\n",
        );
        assert_eq!(rc, 0);
        assert_eq!(out, "\n1:head\n\n2:body\n");
    }

    #[test]
    fn pattern_and_join_blanks() {
        let (rc, out, _) = run_mem(&["-b", "p^ERR", "-w", "1", "-s", ":"], "ERR 1\nok\nERR 2\n");
        assert_eq!(rc, 0);
        assert_eq!(out, "1:ERR 1\n  ok\n2:ERR 2\n");

        let (rc, out, _) = run_mem(&["-b", "a", "-l", "2", "-w", "1", "-s", ":"], "a\n\n\n\nb\n");
        assert_eq!(rc, 0);
        assert_eq!(out, "1:a\n  \n2:\n  \n3:b\n");
    }

    #[test]
    fn invalid_args() {
        let (rc, _, err) = run_mem(&["-i", "1x"], "");
        assert_eq!(rc, 1);
        assert!(err.contains("invalid line number increment: '1x'"));

        let (rc, _, err) = run_mem(&["-w", "0"], "");
        assert_eq!(rc, 1);
        assert!(err.contains("Numerical result out of range"));
    }
}
