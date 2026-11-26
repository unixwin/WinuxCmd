//! `strings` — GNU binutils strings semantics, ported for WinuxCmd 2.0 (#1488).
//!
//! Prints sequences of printable characters from FILE(s) or stdin.
//! Explicit `Io`/`Ctx`, no process-global stdio or exit().

use std::ffi::OsString;
use std::io::{Read, Write};

use crate::argopt;
use winux_core::{usage_error, CmdResult, Ctx, Io};

#[derive(Clone, Copy, PartialEq, Eq)]
enum Encoding {
    Ascii,
    Utf8,
    BigEndian16,
    LittleEndian16,
    BigEndian32,
    LittleEndian32,
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Radix {
    None,
    Octal,
    Decimal,
    Hex,
}

struct Config {
    min_length: usize,
    radix: Radix,
    encoding: Encoding,
    print_filenames: bool,
    include_all_whitespace: bool,
    output_separator: Vec<u8>,
    files: Vec<String>,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            min_length: 4,
            radix: Radix::None,
            encoding: Encoding::Ascii,
            print_filenames: false,
            include_all_whitespace: false,
            output_separator: b"\n".to_vec(),
            files: Vec::new(),
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

fn encoding_width(enc: Encoding) -> usize {
    match enc {
        Encoding::Ascii | Encoding::Utf8 => 1,
        Encoding::BigEndian16 | Encoding::LittleEndian16 => 2,
        Encoding::BigEndian32 | Encoding::LittleEndian32 => 4,
    }
}

fn read_encoded_char(data: &[u8], offset: usize, enc: Encoding) -> Option<u32> {
    let w = encoding_width(enc);
    if offset + w > data.len() {
        return None;
    }
    Some(match enc {
        Encoding::Ascii | Encoding::Utf8 => data[offset] as u32,
        Encoding::BigEndian16 => u16::from_be_bytes([data[offset], data[offset + 1]]) as u32,
        Encoding::LittleEndian16 => u16::from_le_bytes([data[offset], data[offset + 1]]) as u32,
        Encoding::BigEndian32 => {
            u32::from_be_bytes([data[offset], data[offset + 1], data[offset + 2], data[offset + 3]])
        }
        Encoding::LittleEndian32 => {
            u32::from_le_bytes([data[offset], data[offset + 1], data[offset + 2], data[offset + 3]])
        }
    })
}

fn is_graphic_char(ch: u32, cfg: &Config) -> bool {
    if ch > 255 {
        return false;
    }
    let b = ch as u8;
    if b == b'\t' {
        return true;
    }
    if cfg.include_all_whitespace && matches!(b, b' ' | b'\n' | b'\r' | 0x0b | 0x0c) {
        return true;
    }
    if (32..=126).contains(&b) {
        return true;
    }
    cfg.encoding == Encoding::Utf8 && b > 127
}

fn extract_strings_buffer(data: &[u8], display_name: &str, cfg: &Config, out: &mut dyn Write) -> std::io::Result<()> {
    let mut pos = 0usize;
    let width = encoding_width(cfg.encoding);
    let mut current: Vec<u8> = Vec::new();

    while pos < data.len() {
        let mut scan = pos;
        current.clear();
        let mut enough = true;

        for _ in 0..cfg.min_length {
            match read_encoded_char(data, scan, cfg.encoding) {
                Some(ch) if is_graphic_char(ch, cfg) => {
                    current.push((ch & 0xff) as u8);
                    scan += width;
                }
                _ => {
                    enough = false;
                    break;
                }
            }
        }

        if !enough {
            pos += 1;
            continue;
        }

        let mut stopped_on_boundary = false;
        while let Some(ch) = read_encoded_char(data, scan, cfg.encoding) {
            if !is_graphic_char(ch, cfg) {
                stopped_on_boundary = true;
                break;
            }
            current.push((ch & 0xff) as u8);
            scan += width;
        }

        if cfg.print_filenames {
            write!(out, "{display_name}: ")?;
        }
        match cfg.radix {
            Radix::Octal => write!(out, "{pos:7o} ")?,
            Radix::Decimal => write!(out, "{pos:7} ")?,
            Radix::Hex => write!(out, "{pos:7x} ")?,
            Radix::None => {}
        }
        out.write_all(&current)?;
        out.write_all(&cfg.output_separator)?;

        pos = if stopped_on_boundary { scan + 1 } else { scan };
    }
    Ok(())
}

pub fn strings_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut cfg = Config::default();
    let mut idx = 0usize;

    while idx < args.len() {
        let arg = args[idx].to_string_lossy().into_owned();
        if arg == "--" {
            idx += 1;
            for a in &args[idx..] {
                cfg.files.push(a.to_string_lossy().into_owned());
            }
            break;
        }
        if arg == "-" || !arg.starts_with('-') {
            cfg.files.push(arg);
            idx += 1;
            continue;
        }
        if let Some(long) = arg.strip_prefix("--") {
            let (name, inline) = match long.split_once('=') {
                Some((n, v)) => (n, Some(v.to_string())),
                None => (long, None),
            };
            match name {
                "all" | "data" => {}
                "print-file-name" => cfg.print_filenames = true,
                "include-all-whitespace" => cfg.include_all_whitespace = true,
                "bytes" => {
                    let Some(v) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(ctx, io, "strings", &argopt::option_requires_argument(ctx, "--bytes"));
                        return Ok(1);
                    };
                    match v.parse::<usize>() {
                        Ok(n) if n >= 1 => cfg.min_length = n,
                        _ => {
                            let _ = writeln!(io.stderr, "strings: invalid minimum length");
                            return Ok(1);
                        }
                    }
                }
                "radix" => {
                    let Some(v) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(ctx, io, "strings", &argopt::option_requires_argument(ctx, "--radix"));
                        return Ok(1);
                    };
                    cfg.radix = match v.as_str() {
                        "o" | "octal" => Radix::Octal,
                        "d" | "decimal" => Radix::Decimal,
                        "x" | "hex" => Radix::Hex,
                        _ => {
                            let _ = writeln!(io.stderr, "strings: invalid radix");
                            return Ok(1);
                        }
                    };
                }
                "encoding" => {
                    let Some(v) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(ctx, io, "strings", &argopt::option_requires_argument(ctx, "--encoding"));
                        return Ok(1);
                    };
                    cfg.encoding = match v.as_str() {
                        "s" | "ascii" => Encoding::Ascii,
                        "S" | "UTF8" => Encoding::Utf8,
                        "b" => Encoding::BigEndian16,
                        "l" => Encoding::LittleEndian16,
                        "B" => Encoding::BigEndian32,
                        "L" => Encoding::LittleEndian32,
                        _ => {
                            let _ = writeln!(io.stderr, "strings: invalid encoding");
                            return Ok(1);
                        }
                    };
                }
                "output-separator" => {
                    let Some(v) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(ctx, io, "strings", &argopt::option_requires_argument(ctx, "--output-separator"));
                        return Ok(1);
                    };
                    cfg.output_separator = v.into_bytes();
                }
                "target" => {
                    let Some(_) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(ctx, io, "strings", &argopt::option_requires_argument(ctx, "--target"));
                        return Ok(1);
                    };
                }
                "unicode" => {
                    let Some(v) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(ctx, io, "strings", &argopt::option_requires_argument(ctx, "--unicode"));
                        return Ok(1);
                    };
                    match v.as_str() {
                        "default" | "d" => {}
                        "locale" | "l" | "escape" | "e" | "invalid" | "i" | "hex" | "x"
                        | "highlight" | "h" => cfg.encoding = Encoding::Utf8,
                        _ => {
                            let _ = writeln!(io.stderr, "strings: invalid unicode mode");
                            return Ok(1);
                        }
                    }
                }
                "help" | "version" => return Ok(0),
                _ => {
                    let _ = usage_error(ctx, io, "strings", &argopt::unrecognized_option(ctx, &arg));
                    return Ok(1);
                }
            }
            idx += 1;
            continue;
        }

        let rest_after_dash = &arg[1..];
        if !rest_after_dash.is_empty() && rest_after_dash.bytes().all(|b| b.is_ascii_digit()) {
            match rest_after_dash.parse::<usize>() {
                Ok(n) if n >= 1 => cfg.min_length = n,
                _ => {
                    let _ = writeln!(io.stderr, "strings: invalid minimum length");
                    return Ok(1);
                }
            }
            idx += 1;
            continue;
        }

        let chars: Vec<char> = rest_after_dash.chars().collect();
        let mut ci = 0usize;
        let mut consumed_next = false;
        while ci < chars.len() {
            let c = chars[ci];
            match c {
                'a' | 'd' => ci += 1,
                'f' => {
                    cfg.print_filenames = true;
                    ci += 1;
                }
                'w' => {
                    cfg.include_all_whitespace = true;
                    ci += 1;
                }
                'o' => {
                    cfg.radix = Radix::Octal;
                    ci += 1;
                }
                'V' => return Ok(0),
                'n' | 't' | 'e' | 's' | 'T' | 'U' => {
                    let rest: String = chars[ci + 1..].iter().collect();
                    let val = if !rest.is_empty() {
                        ci = chars.len();
                        rest
                    } else if let Some(next_arg) = args.get(idx + 1) {
                        consumed_next = true;
                        ci = chars.len();
                        next_arg.to_string_lossy().into_owned()
                    } else {
                        let _ = usage_error(ctx, io, "strings", &argopt::short_option_requires_argument(ctx, &c.to_string()));
                        return Ok(1);
                    };
                    match c {
                        'n' => match val.parse::<usize>() {
                            Ok(n) if n >= 1 => cfg.min_length = n,
                            _ => {
                                let _ = writeln!(io.stderr, "strings: invalid minimum length");
                                return Ok(1);
                            }
                        },
                        't' => {
                            cfg.radix = match val.as_str() {
                                "o" | "octal" => Radix::Octal,
                                "d" | "decimal" => Radix::Decimal,
                                "x" | "hex" => Radix::Hex,
                                _ => {
                                    let _ = writeln!(io.stderr, "strings: invalid radix");
                                    return Ok(1);
                                }
                            };
                        }
                        'e' => {
                            cfg.encoding = match val.as_str() {
                                "s" | "ascii" => Encoding::Ascii,
                                "S" | "UTF8" => Encoding::Utf8,
                                "b" => Encoding::BigEndian16,
                                "l" => Encoding::LittleEndian16,
                                "B" => Encoding::BigEndian32,
                                "L" => Encoding::LittleEndian32,
                                _ => {
                                    let _ = writeln!(io.stderr, "strings: invalid encoding");
                                    return Ok(1);
                                }
                            };
                        }
                        's' => cfg.output_separator = val.into_bytes(),
                        'T' => {}
                        'U' => match val.as_str() {
                            "default" | "d" => {}
                            "locale" | "l" | "escape" | "e" | "invalid" | "i" | "hex" | "x"
                            | "highlight" | "h" => cfg.encoding = Encoding::Utf8,
                            _ => {
                                let _ = writeln!(io.stderr, "strings: invalid unicode mode");
                                return Ok(1);
                            }
                        },
                        _ => unreachable!(),
                    }
                }
                _ => {
                    let _ = usage_error(ctx, io, "strings", &argopt::invalid_option(ctx, &c.to_string()));
                    return Ok(1);
                }
            }
        }
        idx += if consumed_next { 2 } else { 1 };
    }

    if cfg.files.is_empty() {
        cfg.files.push("-".to_string());
    }

    let mut exit_code = 0u32;
    for file in &cfg.files {
        let data = if file == "-" {
            let mut buf = Vec::new();
            if let Err(e) = io.stdin.read_to_end(&mut buf) {
                let _ = writeln!(io.stderr, "strings: -: {}", crate::fsutil::reason(&e));
                exit_code = 1;
                continue;
            }
            buf
        } else {
            match crate::fsutil::read_operand_all(ctx, file) {
                Ok(b) => b,
                Err(e) => {
                    let reason = crate::fsutil::gnu_open_reason(ctx, file, &e, false);
                    let _ = writeln!(io.stderr, "strings: '{file}': {reason}");
                    exit_code = 1;
                    continue;
                }
            }
        };
        let display_name = if file == "-" { "{standard input}" } else { file.as_str() };
        if let Err(e) = extract_strings_buffer(&data, display_name, &cfg, &mut *io.stdout) {
            let _ = writeln!(io.stderr, "strings: write error: {}", crate::fsutil::reason(&e));
            return Ok(1);
        }
    }

    let _ = io.stdout.flush();
    Ok(exit_code)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn basic_and_min_len_and_encodings() {
        let sc = ScratchCtx::new("strings");
        let ctx = sc.ctx();

        let mut io = MemIo::new("\x00\x01hello\x00\x00abc\x00world\x00");
        assert_eq!(strings_cmd(&[], &mut io.split(), &ctx).unwrap(), 0);
        assert_eq!(io.out(), "hello\nworld\n");

        let mut io2 = MemIo::new("\x00hello\x00");
        let args2 = vec![OsString::from("-t"), OsString::from("x")];
        assert_eq!(strings_cmd(&args2, &mut io2.split(), &ctx).unwrap(), 0);
        assert_eq!(io2.out(), "      1 hello\n");

        let p = sc.path("be16.bin");
        std::fs::write(&p, b"\x00h\x00e\x00l\x00l\x00o\x00").unwrap();
        let mut io3 = MemIo::new("");
        let args3 = vec![
            OsString::from("-e"),
            OsString::from("b"),
            OsString::from(p.to_string_lossy().as_ref()),
        ];
        assert_eq!(strings_cmd(&args3, &mut io3.split(), &ctx).unwrap(), 0);
        assert_eq!(io3.out(), "hello\n");
    }
}
