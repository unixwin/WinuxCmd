//! `xxd` — hexdump and reverse hexdump for WinuxCmd 2.0 (#1488).
//!
//! Supports default, `-p/--plain` (30-byte default wrap #1205), `-i/--include`,
//! `-r/--reverse`, `-c/--cols`, `-l/--len`, `-s/--seek`, `-o/--offset`,
//! `-u/--upper-case`, `-g/--group`, `-n/--name`, and explicit `Io`/`Ctx`.

use std::ffi::OsString;
use std::io::{Read, Write};

use crate::argopt;
use winux_core::{usage_error, CmdResult, Ctx, Io};

struct Config {
    reverse: bool,
    plain: bool,
    upper_case: bool,
    include: bool,
    name: Option<String>,
    length: Option<usize>,
    seek: usize,
    display_offset: usize,
    columns: usize,
    columns_specified: bool,
    group_width: usize,
    input_file: String,
    output_file: Option<String>,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            reverse: false,
            plain: false,
            upper_case: false,
            include: false,
            name: None,
            length: None,
            seek: 0,
            display_offset: 0,
            columns: 16,
            columns_specified: false,
            group_width: 2,
            input_file: "-".to_string(),
            output_file: None,
        }
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

fn byte_hex(v: u8, upper: bool) -> String {
    if upper {
        format!("{v:02X}")
    } else {
        format!("{v:02x}")
    }
}

fn hex_nibble(b: u8) -> Option<u8> {
    match b {
        b'0'..=b'9' => Some(b - b'0'),
        b'a'..=b'f' => Some(b - b'a' + 10),
        b'A'..=b'F' => Some(b - b'A' + 10),
        _ => None,
    }
}

fn reverse_plain(input: &[u8]) -> Option<Vec<u8>> {
    let mut out = Vec::new();
    let mut hi: Option<u8> = None;
    for &b in input {
        if b.is_ascii_whitespace() {
            continue;
        }
        let val = hex_nibble(b)?;
        if let Some(h) = hi {
            out.push((h << 4) | val);
            hi = None;
        } else {
            hi = Some(val);
        }
    }
    if hi.is_some() {
        return None;
    }
    Some(out)
}

fn reverse_normal(input: &[u8]) -> Vec<u8> {
    let text = String::from_utf8_lossy(input);
    let mut out = Vec::new();
    for raw_line in text.split('\n') {
        let line = match raw_line.find(':') {
            Some(pos) => &raw_line[pos + 1..],
            None => raw_line,
        };
        for token in line.split_whitespace() {
            if token.len() % 2 != 0 {
                break;
            }
            let bytes = token.as_bytes();
            let mut chunk = Vec::with_capacity(bytes.len() / 2);
            let mut valid = true;
            for pair in bytes.chunks_exact(2) {
                match (hex_nibble(pair[0]), hex_nibble(pair[1])) {
                    (Some(h), Some(l)) => chunk.push((h << 4) | l),
                    _ => {
                        valid = false;
                        break;
                    }
                }
            }
            if !valid {
                break;
            }
            out.extend_from_slice(&chunk);
        }
    }
    out
}

fn include_identifier(val: &str) -> String {
    let mut id: String = val
        .chars()
        .map(|c| if c.is_ascii_alphanumeric() { c } else { '_' })
        .collect();
    if id.as_bytes().first().is_some_and(|b| b.is_ascii_digit()) {
        id.insert_str(0, "__");
    }
    id
}

fn default_include_name(filename: &str) -> String {
    let base = filename.rsplit(['/', '\\']).next().unwrap_or(filename);
    include_identifier(base)
}

pub fn xxd_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut cfg = Config::default();
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
                "reverse" => cfg.reverse = true,
                "plain" => cfg.plain = true,
                "upper-case" => cfg.upper_case = true,
                "include" => cfg.include = true,
                "name" => {
                    let Some(v) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(ctx, io, "xxd", &argopt::option_requires_argument(ctx, "--name"));
                        return Ok(1);
                    };
                    cfg.name = Some(v);
                }
                "len" => {
                    let Some(v) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(ctx, io, "xxd", &argopt::option_requires_argument(ctx, "--len"));
                        return Ok(1);
                    };
                    if let Ok(n) = v.parse::<isize>() {
                        if n >= 0 {
                            cfg.length = Some(n as usize);
                        }
                    }
                }
                "seek" => {
                    let Some(v) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(ctx, io, "xxd", &argopt::option_requires_argument(ctx, "--seek"));
                        return Ok(1);
                    };
                    if let Ok(n) = v.parse::<isize>() {
                        if n >= 0 {
                            cfg.seek = n as usize;
                        }
                    }
                }
                "offset" => {
                    let Some(v) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(ctx, io, "xxd", &argopt::option_requires_argument(ctx, "--offset"));
                        return Ok(1);
                    };
                    if let Ok(n) = v.parse::<isize>() {
                        if n >= 0 {
                            cfg.display_offset = n as usize;
                        }
                    }
                }
                "cols" => {
                    let Some(v) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(ctx, io, "xxd", &argopt::option_requires_argument(ctx, "--cols"));
                        return Ok(1);
                    };
                    cfg.columns_specified = true;
                    match v.parse::<isize>() {
                        Ok(n) if n > 0 => cfg.columns = n as usize,
                        _ => {
                            let _ = writeln!(io.stderr, "xxd: invalid column count");
                            return Ok(1);
                        }
                    }
                }
                "group" => {
                    let Some(v) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(ctx, io, "xxd", &argopt::option_requires_argument(ctx, "--group"));
                        return Ok(1);
                    };
                    if let Ok(n) = v.parse::<isize>() {
                        cfg.group_width = if n < 1 { 2 } else { n as usize };
                    }
                }
                "help" | "version" => return Ok(0),
                _ => {
                    let _ = usage_error(ctx, io, "xxd", &argopt::unrecognized_option(ctx, &arg));
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
                'r' => {
                    cfg.reverse = true;
                    ci += 1;
                }
                'p' => {
                    cfg.plain = true;
                    ci += 1;
                }
                'u' => {
                    cfg.upper_case = true;
                    ci += 1;
                }
                'i' => {
                    cfg.include = true;
                    ci += 1;
                }
                'c' | 'l' | 's' | 'o' | 'g' | 'n' => {
                    let rest: String = chars[ci + 1..].iter().collect();
                    let val = if !rest.is_empty() {
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
                            "xxd",
                            &argopt::short_option_requires_argument(ctx, &c.to_string()),
                        );
                        return Ok(1);
                    };
                    match c {
                        'n' => cfg.name = Some(val),
                        'l' => {
                            if let Ok(n) = val.parse::<isize>() {
                                if n >= 0 {
                                    cfg.length = Some(n as usize);
                                }
                            }
                        }
                        's' => {
                            if let Ok(n) = val.parse::<isize>() {
                                if n >= 0 {
                                    cfg.seek = n as usize;
                                }
                            }
                        }
                        'o' => {
                            if let Ok(n) = val.parse::<isize>() {
                                if n >= 0 {
                                    cfg.display_offset = n as usize;
                                }
                            }
                        }
                        'c' => {
                            cfg.columns_specified = true;
                            match val.parse::<isize>() {
                                Ok(n) if n > 0 => cfg.columns = n as usize,
                                _ => {
                                    let _ = writeln!(io.stderr, "xxd: invalid column count");
                                    return Ok(1);
                                }
                            }
                        }
                        'g' => {
                            if let Ok(n) = val.parse::<isize>() {
                                cfg.group_width = if n < 1 { 2 } else { n as usize };
                            }
                        }
                        _ => unreachable!(),
                    }
                }
                _ => {
                    let _ = usage_error(ctx, io, "xxd", &argopt::invalid_option(ctx, &c.to_string()));
                    return Ok(1);
                }
            }
        }
        idx += if consumed_next { 2 } else { 1 };
    }

    if cfg.plain && !cfg.columns_specified {
        cfg.columns = 30;
    }

    if !positionals.is_empty() {
        let first = &positionals[0];
        if crate::glob::contains_wildcard(first) {
            if let Some(expanded) = crate::glob::glob_expand_in(ctx, first) {
                if !expanded.is_empty() {
                    cfg.input_file = expanded[0].clone();
                } else {
                    cfg.input_file = first.clone();
                }
            } else {
                cfg.input_file = first.clone();
            }
        } else {
            cfg.input_file = first.clone();
        }
    }
    if cfg.reverse && positionals.len() > 1 {
        cfg.output_file = Some(positionals[1].clone());
    }

    let mut data = if cfg.input_file == "-" {
        let mut buf = Vec::new();
        if let Err(e) = io.stdin.read_to_end(&mut buf) {
            let _ = writeln!(io.stderr, "xxd: cannot open -: {}", crate::fsutil::reason(&e));
            return Ok(1);
        }
        buf
    } else {
        match crate::fsutil::read_operand_all(ctx, &cfg.input_file) {
            Ok(b) => b,
            Err(_) => {
                let _ = writeln!(io.stderr, "xxd: cannot open {}", cfg.input_file);
                return Ok(1);
            }
        }
    };

    if cfg.seek >= data.len() {
        data.clear();
    } else if cfg.seek > 0 {
        data.drain(..cfg.seek);
    }
    if let Some(len) = cfg.length {
        if len < data.len() {
            data.truncate(len);
        }
    }

    if cfg.reverse {
        let decoded = if cfg.plain {
            match reverse_plain(&data) {
                Some(d) => d,
                None => {
                    let _ = writeln!(io.stderr, "xxd: invalid hex dump");
                    return Ok(1);
                }
            }
        } else {
            reverse_normal(&data)
        };
        if let Some(ref out_path) = cfg.output_file {
            match crate::fsutil::open_write_operand(ctx, out_path, false) {
                Ok(mut f) => {
                    if f.write_all(&decoded).and_then(|_| f.flush()).is_err() {
                        let _ = writeln!(io.stderr, "xxd: invalid hex dump");
                        return Ok(1);
                    }
                }
                Err(_) => {
                    let _ = writeln!(io.stderr, "xxd: cannot create {out_path}");
                    return Ok(1);
                }
            }
        } else if io.stdout.write_all(&decoded).and_then(|_| io.stdout.flush()).is_err() {
            let _ = writeln!(io.stderr, "xxd: invalid hex dump");
            return Ok(1);
        }
        return Ok(0);
    }

    if cfg.include {
        let cols = if cfg.columns_specified { cfg.columns } else { 12 };
        if cfg.name.is_some() || cfg.input_file != "-" {
            let ident = match cfg.name.as_deref() {
                Some(n) => include_identifier(n),
                None => default_include_name(&cfg.input_file),
            };
            let _ = writeln!(io.stdout, "unsigned char {ident}[] = {{");
            let mut offset = 0usize;
            while offset < data.len() {
                let end = (offset + cols).min(data.len());
                let _ = write!(io.stdout, "  ");
                for (i, &b) in data[offset..end].iter().enumerate() {
                    if i != 0 {
                        let _ = write!(io.stdout, ", ");
                    }
                    let _ = write!(io.stdout, "0x{}", byte_hex(b, cfg.upper_case));
                }
                if end == data.len() {
                    let _ = writeln!(io.stdout);
                } else {
                    let _ = writeln!(io.stdout, ",");
                }
                offset = end;
            }
            let _ = writeln!(io.stdout, "}};");
            let _ = writeln!(io.stdout, "unsigned int {ident}_len = {};", data.len());
        } else {
            let mut offset = 0usize;
            while offset < data.len() {
                let end = (offset + cols).min(data.len());
                let _ = write!(io.stdout, "  ");
                for (i, &b) in data[offset..end].iter().enumerate() {
                    if i != 0 {
                        let _ = write!(io.stdout, ", ");
                    }
                    let _ = write!(io.stdout, "0x{}", byte_hex(b, cfg.upper_case));
                }
                let _ = writeln!(io.stdout);
                offset = end;
            }
        }
    } else if cfg.plain {
        for chunk in data.chunks(cfg.columns) {
            for &b in chunk {
                let _ = write!(io.stdout, "{}", byte_hex(b, cfg.upper_case));
            }
            let _ = writeln!(io.stdout);
        }
    } else {
        let mut offset = 0usize;
        while offset < data.len() {
            let end = (offset + cfg.columns).min(data.len());
            let addr = cfg.display_offset + offset;
            if cfg.upper_case {
                let _ = write!(io.stdout, "{addr:08X}: ");
            } else {
                let _ = write!(io.stdout, "{addr:08x}: ");
            }
            for i in 0..cfg.columns {
                if offset + i < end {
                    let _ = write!(io.stdout, "{}", byte_hex(data[offset + i], cfg.upper_case));
                } else {
                    let _ = write!(io.stdout, "  ");
                }
                if cfg.group_width > 1 && i % cfg.group_width == cfg.group_width - 1 {
                    let _ = write!(io.stdout, " ");
                }
            }
            let _ = write!(io.stdout, " ");
            for &b in &data[offset..end] {
                let ch = if (32..=126).contains(&b) { b as char } else { '.' };
                let _ = write!(io.stdout, "{ch}");
            }
            let _ = writeln!(io.stdout);
            offset = end;
        }
    }

    let _ = io.stdout.flush();
    Ok(0)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn default_plain_include_and_reverse() {
        let sc = ScratchCtx::new("xxd");
        let ctx = sc.ctx();

        let mut io = MemIo::new("hello");
        assert_eq!(xxd_cmd(&[], &mut io.split(), &ctx).unwrap(), 0);
        assert_eq!(
            io.out(),
            "00000000: 6865 6c6c 6f                             hello\n"
        );

        let mut io2 = MemIo::new("68656c6c6f");
        let args2 = vec![OsString::from("-r"), OsString::from("-p")];
        assert_eq!(xxd_cmd(&args2, &mut io2.split(), &ctx).unwrap(), 0);
        assert_eq!(io2.out(), "hello");

        let mut io3 = MemIo::new("hi");
        let args3 = vec![OsString::from("-i")];
        assert_eq!(xxd_cmd(&args3, &mut io3.split(), &ctx).unwrap(), 0);
        assert_eq!(io3.out(), "  0x68, 0x69\n");
    }
}
