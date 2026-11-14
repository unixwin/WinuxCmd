//! `od` — GNU coreutils od.c (9.4) semantics, probed byte-for-byte
//! against WSL coreutils 9.4 (rust2/target/probe/p-od.sh + p2/fin).
//!
//! Layout (measured): the address label is %07o (default and -A o),
//! %07d (-A d, decimal zero-padded), %06x (-A x), none (-A n, which also
//! suppresses the trailing offset line). Each item prints as a space
//! separator plus a fixed-width field: octal/hex zero-padded (o1 3, o2 6,
//! o4 11, o8 22, x1 2, x2 4, x4 8, x8 16), decimal space-padded (d1 4,
//! d2 6, d4 11, d8 20, u1 3, u2 5, u4 10, u8 20), floats in
//! shortest-round-trip form with a C exponent (probed: the -tf8
//! subnormal of byte 0x61 prints `4.8e-322`). Bytes per line is 16 (-wN changes it; bare -w
//! means 32). Duplicate 16-byte chunks collapse to a single `*` line
//! unless -v. Multi-type dumps (-t c -t b, or comma lists) print one
//! line per type per chunk; the label column is blank-padded on the
//! later types. `-t x1z` pads the item area to the full line width and
//! appends ` >ascii<` (non-printables become `.`). Trailing padding is
//! NOT printed without z. The trailing offset line prints even for
//! empty input (just `0000000`), except under -A n.
//!
//! Partials: a trailing multi-byte unit is zero-filled and printed at
//! full width (probed `od -An -tx4` on `abc` -> `00636261`).
//!
//! Documented subsets: `--traditional` operands and `-S/--strings` are
//! not implemented (a use is a usage error); `fL` (80-bit long double)
//! cannot be represented with std floats and is rejected. `-A n` with
//! empty input prints nothing at all, matching GNU.

use std::ffi::OsString;
use std::io::{Read, Write};

use crate::argopt::{self, OptSpec};
use crate::fsutil::{gnu_open_reason, quotef, resolve};
use crate::gnuhelp;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('A'),
        long: "address-radix",
        takes_value: true,
    },
    OptSpec {
        short: Some('j'),
        long: "skip-bytes",
        takes_value: true,
    },
    OptSpec {
        short: Some('N'),
        long: "read-bytes",
        takes_value: true,
    },
    OptSpec {
        short: Some('S'),
        long: "strings",
        takes_value: true,
    },
    OptSpec {
        short: Some('t'),
        long: "format",
        takes_value: true,
    },
    OptSpec {
        short: Some('v'),
        long: "output-duplicates",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "endian",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "traditional",
        takes_value: false,
    },
    OptSpec {
        short: Some('w'),
        long: "width",
        takes_value: true,
    },
];

/// One parsed output format specification.
#[derive(Debug, Clone, Copy, PartialEq)]
enum OdType {
    Named,        // a
    Char,         // c
    Signed(u8),   // dN
    Unsigned(u8), // uN
    Octal(u8),    // oN
    Hex(u8),      // xN
    Float(u8),    // fN
}

#[derive(Debug, Clone, Copy, PartialEq)]
enum Radix {
    Decimal,
    Octal,
    Hex,
    None,
}

/// Field width (after the separator space) and zero-padding flag.
fn field_spec(t: OdType) -> (usize, bool) {
    match t {
        OdType::Named | OdType::Char => (3, false),
        OdType::Signed(n) => match n {
            1 => (4, false),
            2 => (6, false),
            4 => (11, false),
            _ => (20, false),
        },
        OdType::Unsigned(n) => match n {
            1 => (3, false),
            2 => (5, false),
            4 => (10, false),
            _ => (20, false),
        },
        OdType::Octal(n) => {
            let w = match n {
                1 => 3,
                2 => 6,
                4 => 11,
                _ => 22,
            };
            (w, true)
        }
        OdType::Hex(n) => {
            let w = match n {
                1 => 2,
                2 => 4,
                4 => 8,
                _ => 16,
            };
            (w, true)
        }
        OdType::Float(n) => {
            if n == 8 {
                (24, false)
            } else {
                (15, false)
            }
        }
    }
}

fn type_size(t: OdType) -> usize {
    match t {
        OdType::Named | OdType::Char => 1,
        OdType::Signed(n)
        | OdType::Unsigned(n)
        | OdType::Octal(n)
        | OdType::Hex(n)
        | OdType::Float(n) => n as usize,
    }
}

/// Parse a -t TYPE string (possibly comma-separated, 'z' ascii suffix).
fn parse_type_spec(spec: &str) -> Result<(Vec<OdType>, bool), String> {
    let mut types = Vec::new();
    let mut z = false;
    for part in spec.split(',') {
        if part.is_empty() {
            continue;
        }
        let mut chars = part.chars().peekable();
        while let Some(&c) = chars.peek() {
            match c {
                'z' => {
                    z = true;
                    chars.next();
                }
                'a' => {
                    types.push(OdType::Named);
                    chars.next();
                }
                'c' => {
                    types.push(OdType::Char);
                    chars.next();
                }
                'd' | 'u' | 'o' | 'x' | 'f' => {
                    chars.next();
                    let mut size_str = String::new();
                    let mut z_here = false;
                    for nxt in chars.by_ref() {
                        if nxt.is_ascii_digit() {
                            size_str.push(nxt);
                        } else {
                            if nxt == 'z' {
                                // 'z' ascii suffix after a size
                                z_here = true;
                                break;
                            }
                            match nxt {
                                'C' => size_str.push('1'),
                                'S' => size_str.push('2'),
                                'I' => size_str.push('4'),
                                'L' => {
                                    if c == 'f' {
                                        // 80-bit long double: no std repr
                                        return Err(format!(
                                            "od: unsupported float size in type '{part}'"
                                        ));
                                    }
                                    size_str.push('8');
                                }
                                'F' if c == 'f' => size_str.push('4'),
                                'D' if c == 'f' => size_str.push('8'),
                                _ => {
                                    return Err(format!(
                                        "od: invalid character '{nxt}' in type string '{part}'"
                                    ));
                                }
                            }
                            break;
                        }
                    }
                    if z_here {
                        z = true;
                    }
                    // probed defaults: -t o / -t x dump 4-byte units like
                    // -t d / -t u; -t f dumps doubles (od.c sizeof rules)
                    let size: u8 = if size_str.is_empty() {
                        if c == 'f' {
                            8
                        } else {
                            4
                        }
                    } else {
                        size_str
                            .parse()
                            .map_err(|_| format!("od: invalid character in type string '{part}'"))?
                    };
                    if c == 'f' && size != 4 && size != 8 {
                        return Err(format!("od: unsupported float size in type '{part}'"));
                    }
                    if c != 'f' && size != 1 && size != 2 && size != 4 && size != 8 {
                        return Err(format!(
                            "od: invalid character '{size_str}' in type string '{part}'"
                        ));
                    }
                    types.push(match c {
                        'd' => OdType::Signed(size),
                        'u' => OdType::Unsigned(size),
                        'o' => OdType::Octal(size),
                        'x' => OdType::Hex(size),
                        _ => OdType::Float(size),
                    });
                }
                other => {
                    return Err(format!(
                        "od: invalid character '{other}' in type string '{part}'"
                    ));
                }
            }
        }
    }
    if types.is_empty() {
        return Err("od: bad type specification".to_string());
    }
    Ok((types, z))
}

/// BYTES arguments (-j/-N/-w): hex with 0x/0X, multiplier suffixes.
fn parse_bytes_arg(text: &str) -> Option<u64> {
    let (neg, digits) = match text.strip_prefix('-') {
        Some(r) => (true, r),
        None => (false, text),
    };
    if neg {
        return None;
    }
    let (hex, body) = if let Some(r) = digits
        .strip_prefix("0x")
        .or_else(|| digits.strip_prefix("0X"))
    {
        (true, r)
    } else {
        (false, digits)
    };
    // split trailing multiplier suffix (KB/MB/.../KiB/.../b/K)
    let mut num_end = body.len();
    for (i, c) in body.char_indices() {
        if !(c.is_ascii_digit() || c == '.') {
            num_end = i;
            break;
        }
    }
    let value = if hex {
        u64::from_str_radix(&body[..num_end], 16).ok()?
    } else {
        body[..num_end].parse::<u64>().ok()?
    };
    if num_end == body.len() {
        return Some(value);
    }
    let suffix = &body[num_end..];
    let mult = match suffix {
        "b" => 512u64,
        "KB" | "kB" => 1000,
        "K" | "k" | "KiB" => 1024,
        "MB" => 1000u64.pow(2),
        "M" | "MiB" => 1024u64.pow(2),
        "GB" => 1000u64.pow(3),
        "G" | "GiB" => 1024u64.pow(3),
        "TB" => 1000u64.pow(4),
        "T" | "TiB" => 1024u64.pow(4),
        "PB" => 1000u64.pow(5),
        "P" | "PiB" => 1024u64.pow(5),
        "EB" => 1000u64.pow(6),
        "E" | "EiB" => 1024u64.pow(6),
        "ZB" => 1000u64.pow(7),
        "Z" | "ZiB" => 1024u64.pow(7),
        "YB" => 1000u64.pow(8),
        "Y" | "YiB" => 1024u64.pow(8),
        _ => return None,
    };
    value.checked_mul(mult)
}

/// The od -a named characters, 3 letters each.
fn named_char(b: u8) -> String {
    const NAMES: [&str; 32] = [
        "nul", "soh", "stx", "etx", "eot", "enq", "ack", "bel", "bs", "ht", "nl", "vt", "ff", "cr",
        "so", "si", "dle", "dc1", "dc2", "dc3", "dc4", "nak", "syn", "etb", "can", "em", "sub",
        "esc", "fs", "gs", "rs", "us",
    ];
    if b < 32 {
        NAMES[b as usize].to_string()
    } else if b == 127 {
        "del".to_string()
    } else if b == 32 {
        "sp".to_string()
    } else {
        (b as char).to_string()
    }
}

/// The od -c display: escapes for the known set, bare octal for other
/// non-printables (probed: 0x7f prints `177`, NUL prints `\0`).
fn char_display(b: u8) -> String {
    match b {
        0x00 => "\\0".to_string(),
        0x07 => "\\a".to_string(),
        0x08 => "\\b".to_string(),
        0x0c => "\\f".to_string(),
        0x0a => "\\n".to_string(),
        0x0d => "\\r".to_string(),
        0x09 => "\\t".to_string(),
        0x0b => "\\v".to_string(),
        _ if (0x20..=0x7e).contains(&b) => (b as char).to_string(),
        _ => format!("{:03o}", b),
    }
}

/// Shape a shortest-round-trip mantissa/exponent like %g: fixed notation
/// for exponents in [-4, bound), exponential otherwise, C exponent style
/// (explicit sign, at least two digits). Probed: 1.0 prints `1`, 0.1
/// prints `0.1`, and the -tf8 subnormal of byte 0x61 prints `4.8e-322`.
fn shape_shortest(mantissa: &str, exp: i32, neg: bool, bound: i32) -> String {
    if exp >= -4 && exp < bound {
        // fixed notation: place the decimal point of the shortest mantissa
        let digits: String = mantissa.chars().filter(|c| c.is_ascii_digit()).collect();
        let mut out = String::new();
        if neg {
            out.push('-');
        }
        if exp >= 0 {
            let shift = exp as usize;
            if shift + 1 >= digits.len() {
                out.push_str(&digits);
                for _ in 0..(shift + 1 - digits.len()) {
                    out.push('0');
                }
            } else {
                out.push_str(&digits[..shift + 1]);
                out.push('.');
                out.push_str(&digits[shift + 1..]);
            }
        } else {
            out.push_str("0.");
            for _ in 0..(-exp - 1) {
                out.push('0');
            }
            out.push_str(&digits);
        }
        return out;
    }
    let sign = if exp < 0 { '-' } else { '+' };
    format!("{mantissa}e{sign}{:02}", exp.abs())
}

fn float_display_f32(v: f32) -> String {
    if v == 0.0 {
        return "0".to_string();
    }
    if v.is_nan() {
        return "nan".to_string();
    }
    if v.is_infinite() {
        return if v > 0.0 { "inf" } else { "-inf" }.to_string();
    }
    let formatted = format!("{:e}", v);
    let (mantissa, exponent) = formatted.split_once('e').unwrap();
    shape_shortest(mantissa, exponent.parse().unwrap_or(0), v < 0.0, 8)
}

fn float_display_f64(v: f64) -> String {
    if v == 0.0 {
        return "0".to_string();
    }
    if v.is_nan() {
        return "nan".to_string();
    }
    if v.is_infinite() {
        return if v > 0.0 { "inf" } else { "-inf" }.to_string();
    }
    let formatted = format!("{:e}", v);
    let (mantissa, exponent) = formatted.split_once('e').unwrap();
    shape_shortest(mantissa, exponent.parse().unwrap_or(0), v < 0.0, 16)
}

fn little_endian_value(chunk: &[u8], size: usize, big_endian: bool) -> u64 {
    let mut v: u64 = 0;
    if big_endian {
        for &b in &chunk[..size] {
            v = (v << 8) | b as u64;
        }
    } else {
        for (i, &b) in chunk[..size].iter().enumerate() {
            v |= (b as u64) << (8 * i);
        }
    }
    v
}

fn format_item(t: OdType, chunk: &[u8], big_endian: bool) -> String {
    let (width, zero_pad) = field_spec(t);
    let rendered = match t {
        OdType::Named => named_char(chunk[0]),
        OdType::Char => char_display(chunk[0]),
        OdType::Signed(n) => {
            let raw = little_endian_value(chunk, n as usize, big_endian);
            let signed = match n {
                1 => raw as u8 as i8 as i64,
                2 => raw as u16 as i16 as i64,
                4 => raw as u32 as i32 as i64,
                _ => raw as i64,
            };
            format!("{signed}")
        }
        OdType::Unsigned(n) => format!("{}", little_endian_value(chunk, n as usize, big_endian)),
        OdType::Octal(n) => format!("{:o}", little_endian_value(chunk, n as usize, big_endian)),
        OdType::Hex(n) => format!("{:x}", little_endian_value(chunk, n as usize, big_endian)),
        OdType::Float(n) => {
            if n == 4 {
                let raw = little_endian_value(chunk, n as usize, big_endian) as u32;
                float_display_f32(f32::from_bits(raw))
            } else {
                let raw = little_endian_value(chunk, n as usize, big_endian);
                float_display_f64(f64::from_bits(raw))
            }
        }
    };
    if zero_pad {
        format!(" {rendered:0>width$}")
    } else {
        format!(" {rendered:>width$}")
    }
}

// `--help` body verbatim from `env od --help` under WSL coreutils 9.4,
// minus the shared gnuhelp::footer tail.
const OD_HELP_BODY: &str = r#"Usage: od [OPTION]... [FILE]...
  or:  od [-abcdfilosx]... [FILE] [[+]OFFSET[.][b]]
  or:  od --traditional [OPTION]... [FILE] [[+]OFFSET[.][b] [+][LABEL][.][b]]

Write an unambiguous representation, octal bytes by default,
of FILE to standard output.  With more than one FILE argument,
concatenate them in the listed order to form the input.

With no FILE, or when FILE is -, read standard input.

If first and second call formats both apply, the second format is assumed
if the last operand begins with + or (if there are 2 operands) a digit.
An OFFSET operand means -j OFFSET.  LABEL is the pseudo-address
at first byte printed, incremented when dump is progressing.
For OFFSET and LABEL, a 0x or 0X prefix indicates hexadecimal;
suffixes may be . for octal and b for multiply by 512.

Mandatory arguments to long options are mandatory for short options too.
  -A, --address-radix=RADIX   output format for file offsets; RADIX is one
                                of [doxn], for Decimal, Octal, Hex or None
      --endian={big|little}   swap input bytes according the specified order
  -j, --skip-bytes=BYTES      skip BYTES input bytes first
  -N, --read-bytes=BYTES      limit dump to BYTES input bytes
  -S BYTES, --strings[=BYTES]  show only NUL terminated strings
                                of at least BYTES (3) printable characters
  -t, --format=TYPE           select output format or formats
  -v, --output-duplicates     do not use * to mark line suppression
  -w[BYTES], --width[=BYTES]  output BYTES bytes per output line;
                                32 is implied when BYTES is not specified
      --traditional           accept arguments in third form above
      --help        display this help and exit
      --version     output version information and exit


Traditional format specifications may be intermixed; they accumulate:
  -a   same as -t a,  select named characters, ignoring high-order bit
  -b   same as -t o1, select octal bytes
  -c   same as -t c,  select printable characters or backslash escapes
  -d   same as -t u2, select unsigned decimal 2-byte units
  -f   same as -t fF, select floats
  -i   same as -t dI, select decimal ints
  -l   same as -t dL, select decimal longs
  -o   same as -t o2, select octal 2-byte units
  -s   same as -t d2, select decimal 2-byte units
  -x   same as -t x2, select hexadecimal 2-byte units


TYPE is made up of one or more of these specifications:
  a          named character, ignoring high-order bit
  c          printable character or backslash escape
  d[SIZE]    signed decimal, SIZE bytes per integer
  f[SIZE]    floating point, SIZE bytes per float
  o[SIZE]    octal, SIZE bytes per integer
  u[SIZE]    unsigned decimal, SIZE bytes per integer
  x[SIZE]    hexadecimal, SIZE bytes per integer

SIZE is a number.  For TYPE in [doux], SIZE may also be C for
sizeof(char), S for sizeof(short), I for sizeof(int) or L for
sizeof(long).  If TYPE is f, SIZE may also be F for sizeof(float), D
for sizeof(double) or L for sizeof(long double).

Adding a z suffix to any type displays printable characters at the end of
each output line.


BYTES is hex with 0x or 0X prefix, and may have a multiplier suffix:
  b    512
  KB   1000
  K    1024
  MB   1000*1000
  M    1024*1024
and so on for G, T, P, E, Z, Y, R, Q.
Binary prefixes can be used, too: KiB=K, MiB=M, and so on.

"#;

struct OdConfig {
    types: Vec<OdType>,
    z: bool,
    radix: Radix,
    width: usize,
    verbose: bool,
    big_endian: bool,
}

fn od_dump(input: &[u8], cfg: &OdConfig, out: &mut dyn Write) -> std::io::Result<()> {
    let bytes_per_line = cfg.width;
    let mut offset = 0usize;
    let mut prev: Option<&[u8]> = None;
    let mut star_active = false;
    let mut first_chunk = true;
    let label_width = match cfg.radix {
        Radix::None => 0,
        Radix::Hex => 6,
        _ => 7,
    };
    while offset < input.len() {
        let end = (offset + bytes_per_line).min(input.len());
        let chunk = &input[offset..end];
        if !cfg.verbose && !first_chunk && prev == Some(chunk) {
            if !star_active {
                writeln!(out, "*")?;
                star_active = true;
            }
            offset = end;
            continue;
        }
        star_active = false;
        prev = Some(chunk);
        first_chunk = false;
        for (ti, &t) in cfg.types.iter().enumerate() {
            let size = type_size(t);
            let mut line = String::new();
            if ti == 0 {
                match cfg.radix {
                    Radix::None => {}
                    Radix::Decimal => line.push_str(&format!("{:07}", offset)),
                    Radix::Octal => line.push_str(&format!("{:07o}", offset)),
                    Radix::Hex => line.push_str(&format!("{:06x}", offset)),
                }
            } else {
                // later types blank-pad the label column
                line.push_str(&" ".repeat(label_width));
            }
            let mut area = String::new();
            let mut pos = 0usize;
            while pos < chunk.len() {
                let take = size.min(chunk.len() - pos);
                let mut unit = chunk[pos..pos + take].to_vec();
                // trailing partial unit is zero-filled at full width
                unit.resize(size, 0);
                area.push_str(&format_item(t, &unit, cfg.big_endian));
                pos += size;
            }
            if cfg.z {
                // the item area (first separator included, so it starts
                // at the label width) pads to bytes_per_line*(field+1)+2
                // before `>ascii<` follows (probed: '>' lands at
                // 7+50 = 57 labeled, 0+50 = 50 under -A n)
                let full_width = bytes_per_line / size * (field_spec(t).0 + 1) + 2;
                while area.len() < full_width {
                    area.push(' ');
                }
                let ascii: String = chunk
                    .iter()
                    .map(|&b| {
                        if (0x20..=0x7e).contains(&b) {
                            (b as char).to_string()
                        } else {
                            ".".to_string()
                        }
                    })
                    .collect();
                area.push_str(&format!(">{ascii}<"));
            }
            line.push_str(&area);
            writeln!(out, "{line}")?;
        }
        offset = end;
    }
    // trailing offset line: suppressed only by -A n
    if cfg.radix != Radix::None {
        match cfg.radix {
            Radix::Decimal => writeln!(out, "{:07}", input.len())?,
            Radix::Octal => writeln!(out, "{:07o}", input.len())?,
            Radix::Hex => writeln!(out, "{:06x}", input.len())?,
            Radix::None => unreachable!(),
        }
    }
    Ok(())
}

pub fn od_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut norm: Vec<OsString> = Vec::new();
    // classic single-letter type options accumulate -t specs
    let classic: &[(&str, OdType)] = &[
        ("a", OdType::Named),
        ("b", OdType::Octal(1)),
        ("c", OdType::Char),
        ("d", OdType::Unsigned(2)),
        ("f", OdType::Float(4)),
        ("i", OdType::Signed(4)),
        ("l", OdType::Signed(8)),
        ("o", OdType::Octal(2)),
        ("s", OdType::Signed(2)),
        ("x", OdType::Hex(2)),
    ];
    let spec_of = |t: OdType| -> &'static str {
        match t {
            OdType::Named => "a",
            OdType::Char => "c",
            OdType::Signed(n) => match n {
                1 => "d1",
                2 => "d2",
                4 => "d4",
                _ => "d8",
            },
            OdType::Unsigned(n) => match n {
                1 => "u1",
                2 => "u2",
                4 => "u4",
                _ => "u8",
            },
            OdType::Octal(n) => match n {
                1 => "o1",
                2 => "o2",
                4 => "o4",
                _ => "o8",
            },
            OdType::Hex(n) => match n {
                1 => "x1",
                2 => "x2",
                4 => "x4",
                _ => "x8",
            },
            OdType::Float(n) => {
                if n == 8 {
                    "f8"
                } else {
                    "f4"
                }
            }
        }
    };
    for a in args {
        let s = a.to_string_lossy().into_owned();
        // -w takes an OPTIONAL argument (getopt "w::"): bare -w means 32,
        // so route it to the long form instead of stealing the next word
        if s == "-w" || s == "--width" {
            norm.push(OsString::from("--width=32"));
            continue;
        }
        if let Some(rest) = s.strip_prefix("-w") {
            if !rest.is_empty() && rest.bytes().all(|b| b.is_ascii_digit()) {
                norm.push(OsString::from(format!("--width={rest}")));
                continue;
            }
        }
        if s.len() == 2 && s.starts_with('-') {
            if let Some((_, t)) = classic.iter().find(|(c, _)| **c == s[1..]) {
                norm.push(OsString::from(format!("--format={}", spec_of(*t))));
                continue;
            }
        }
        norm.push(a.clone());
    }
    let p = match argopt::parse("od", &norm, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if let Some(which) = p
        .flags
        .iter()
        .find(|f| f.as_str() == argopt::HELP || f.as_str() == argopt::VERSION)
    {
        return Ok(match which.as_str() {
            "help" => {
                write!(
                    io.stdout,
                    "{}{}",
                    OD_HELP_BODY,
                    gnuhelp::footer("od", "od invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("od", "Jim Meyering")
                )?;
                0
            }
        });
    }

    let mut types: Vec<OdType> = Vec::new();
    let mut z = false;
    for v in p.values.iter().filter(|(k, _)| k == "format") {
        match parse_type_spec(&v.1) {
            Ok((mut ts, has_z)) => {
                types.append(&mut ts);
                z |= has_z;
            }
            Err(msg) => {
                let _ = writeln!(io.stderr, "{msg}");
                return Ok(1);
            }
        }
    }
    if types.is_empty() {
        types.push(OdType::Octal(2)); // default: octal 2-byte units
    }

    let radix = match p.value_of("address-radix") {
        None => Radix::Octal,
        Some(r) => match r {
            "d" => Radix::Decimal,
            "o" => Radix::Octal,
            "x" => Radix::Hex,
            "n" => Radix::None,
            other => {
                let _ = writeln!(io.stderr, "od: invalid address radix '{other}'");
                let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "od"));
                return Ok(1);
            }
        },
    };

    let mut skip = 0u64;
    if let Some(v) = p.value_of("skip-bytes") {
        match parse_bytes_arg(v) {
            Some(n) => skip = n,
            None => {
                let _ = writeln!(io.stderr, "od: invalid skip bytes {v}");
                return Ok(1);
            }
        }
    }
    let limit = match p.value_of("read-bytes") {
        None => None,
        Some(v) => match parse_bytes_arg(v) {
            Some(n) => Some(n),
            None => {
                let _ = writeln!(io.stderr, "od: invalid read bytes {v}");
                return Ok(1);
            }
        },
    };
    let width = match p.value_of("width") {
        None => 16,
        Some("") => 32,
        Some(v) => match parse_bytes_arg(v) {
            Some(n) if n > 0 && n <= usize::MAX as u64 => n as usize,
            _ => {
                let _ = writeln!(io.stderr, "od: invalid width {v}");
                return Ok(1);
            }
        },
    };
    let big_endian = match p.value_of("endian") {
        None => false,
        Some("little") => false,
        Some("big") => true,
        Some(other) => {
            let _ = writeln!(io.stderr, "od: invalid bytes to swap '{other}'");
            let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "od"));
            return Ok(1);
        }
    };
    if p.has("strings") {
        let _ = writeln!(io.stderr, "od: --strings is not supported in this build");
        return Ok(1);
    }
    if p.has("traditional") {
        let _ = writeln!(
            io.stderr,
            "od: --traditional is not supported in this build"
        );
        return Ok(1);
    }

    // read the combined input
    let files: Vec<String> = if p.positionals.is_empty() {
        vec!["-".to_string()]
    } else {
        p.positionals
            .iter()
            .map(|a| a.to_string_lossy().into_owned())
            .collect()
    };
    let mut input: Vec<u8> = Vec::new();
    let mut failed = false;
    for file in &files {
        if file == "-" {
            if io.stdin.read_to_end(&mut input).is_err() {
                let _ = writeln!(io.stderr, "od: error reading 'standard input'");
                failed = true;
            }
        } else {
            match std::fs::File::open(resolve(ctx, file)) {
                Ok(mut f) => {
                    if f.read_to_end(&mut input).is_err() {
                        if std::fs::metadata(resolve(ctx, file))
                            .map(|m| m.is_dir())
                            .unwrap_or(false)
                        {
                            let _ = writeln!(io.stderr, "od: {file}: read error: Is a directory");
                        } else {
                            let _ = writeln!(io.stderr, "od: {file}: read error");
                        }
                        failed = true;
                    }
                }
                Err(e) => {
                    let reason = gnu_open_reason(ctx, file, &e, false);
                    let _ = writeln!(io.stderr, "od: {}: {reason}", quotef(file));
                    failed = true;
                }
            }
        }
    }
    // -j skip / -N limit over the combined stream
    if skip as usize > input.len() {
        let _ = writeln!(io.stderr, "od: cannot skip past end of combined input");
        return Ok(1);
    }
    let window = &input[skip as usize..];
    let window = match limit {
        Some(n) => &window[..n.min(window.len() as u64) as usize],
        None => window,
    };

    let cfg = OdConfig {
        types,
        z,
        radix,
        width,
        verbose: p.has("output-duplicates"),
        big_endian,
    };
    if od_dump(window, &cfg, io.stdout).is_err() {
        let _ = writeln!(io.stderr, "od: write error");
        return Ok(1);
    }
    Ok(if failed { 1 } else { 0 })
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run_bytes(args: &[&str], stdin: &[u8]) -> (u32, String, String) {
        let mut io = MemIo::new("");
        io.input = std::io::Cursor::new(stdin.to_vec());
        let sc = ScratchCtx::new("od");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = od_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    fn run(args: &[&str], stdin: &str) -> (u32, String, String) {
        run_bytes(args, stdin.as_bytes())
    }

    #[test]
    fn gnu_help_version_surface() {
        let (r, out, err) = run(&["--help"], "");
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.starts_with("Usage: od [OPTION]... [FILE]...\n"));
        assert!(out.contains("  -A, --address-radix=RADIX"));
        assert!(out.ends_with("info '(coreutils) od invocation'\n"));
        let (r, out, _) = run(&["--version"], "");
        assert_eq!(r, 0);
        assert!(out.starts_with("od (GNU coreutils) 9.4\n"));
        assert!(out.ends_with("Written by Jim Meyering.\n"));
    }

    #[test]
    fn default_octal_dump() {
        // probed: default = o2, 16/line, label %07o, final offset line
        assert_eq!(run(&[], "abc").1, "0000000 061141 000143\n0000003\n");
        assert_eq!(run_bytes(&[], &[0u8; 0]).1, "0000000\n");
        assert_eq!(run(&["-A", "n"], "abc").1, " 061141 000143\n");
    }

    #[test]
    fn char_and_named() {
        assert_eq!(run(&["-c"], "abc").1, "0000000   a   b   c\n0000003\n");
        assert_eq!(run(&["-An", "-c"], "abc").1, "   a   b   c\n");
        assert_eq!(
            run_bytes(&["-An", "-c"], b"ab\0\n\t\x07\x08\x0c\r\x0b\x7f\xffz").1,
            "   a   b  \\0  \\n  \\t  \\a  \\b  \\f  \\r  \\v 177 377   z\n"
        );
        assert_eq!(run(&["-An", "-a"], "abc").1, "   a   b   c\n");
        assert_eq!(run_bytes(&["-An", "-a"], b"a\0b").1, "   a nul   b\n");
    }

    #[test]
    fn integer_types() {
        assert_eq!(run(&["-An", "-tx1"], "abc").1, " 61 62 63\n");
        assert_eq!(run(&["-An", "-tx4"], "abc").1, " 00636261\n");
        assert_eq!(run(&["-An", "-tx4"], "abcdefg").1, " 64636261 00676665\n");
        assert_eq!(run(&["-An", "-td1"], "a").1, "   97\n");
        assert_eq!(
            run_bytes(&["-An", "-td1"], b"\x7f\x80\x81").1,
            "  127 -128 -127\n"
        );
        assert_eq!(run_bytes(&["-An", "-td1"], b"\xff").1, "   -1\n");
        assert_eq!(run_bytes(&["-An", "-tu1"], b"\xff").1, " 255\n");
        assert_eq!(run(&["-An", "-td2"], "hi").1, "  26984\n");
        assert_eq!(run(&["-An", "-tu2"], "hi").1, " 26984\n");
        assert_eq!(run(&["-An", "-to2"], "hi").1, " 064550\n");
        assert_eq!(run(&["-An", "-tx2"], "hi").1, " 6968\n");
        assert_eq!(run_bytes(&["-An", "-td2"], b"\xff\xfe").1, "   -257\n");
        assert_eq!(
            run_bytes(&["-An", "-td4"], b"\xff\xfe\x01\x02").1,
            "    33685247\n"
        );
        assert_eq!(run(&["-An", "-tu4"], "abc").1, "    6513249\n");
        assert_eq!(
            run(&["-An", "-td8"], "abcdefgh").1,
            "  7523094288207667809\n"
        );
        assert_eq!(
            run(&["-An", "-tu8"], "abcdefghij").1,
            "  7523094288207667809                27241\n"
        );
        assert_eq!(run(&["-An", "-to1"], "a").1, " 141\n");
        assert_eq!(run(&["-An", "-b"], "abc").1, " 141 142 143\n");
        assert_eq!(run(&["-An", "-d"], "hi").1, " 26984\n");
        assert_eq!(run(&["-An", "-o"], "abc").1, " 061141 000143\n");
        assert_eq!(run(&["-An", "-x"], "hi").1, " 6968\n");
    }

    #[test]
    fn float_types() {
        // 1.0 double LE
        assert_eq!(
            run_bytes(&["-An", "-tf8"], &[0, 0, 0, 0, 0, 0, 0xf0, 0x3f]).1,
            "                        1\n"
        );
        // 0.1 double: %.15g collapses to 0.1
        assert_eq!(
            run_bytes(
                &["-An", "-tf8"],
                &[0x9a, 0x99, 0x99, 0x99, 0x99, 0x99, 0xb9, 0x3f]
            )
            .1,
            "                      0.1\n"
        );
        // f4 on 'abc' bytes = 9.127006e-39 (probed)
        assert_eq!(run(&["-An", "-f"], "abc").1, "    9.127006e-39\n");
        assert_eq!(run(&["-An", "-f"], "a").1, "        1.36e-43\n");
        // probed f8 subnormal shortens like %g
        assert_eq!(run(&["-An", "-tf8"], "a").1, "                 4.8e-322\n");
    }

    #[test]
    fn multi_type_and_ascii_column() {
        assert_eq!(
            run(&["-c", "-b"], "abc").1,
            "0000000   a   b   c\n        141 142 143\n0000003\n"
        );
        assert_eq!(
            run(&["-t", "x1z"], "abc").1,
            "0000000 61 62 63                                         >abc<\n0000003\n"
        );
        assert_eq!(
            run(&["-t", "x1z", "-A", "n"], "abc").1,
            " 61 62 63                                         >abc<\n"
        );
        assert_eq!(
            run_bytes(&["-t", "x1z"], b"abc\x1f d").1,
            "0000000 61 62 63 1f 20 64                                >abc. d<\n0000006\n"
        );
    }

    #[test]
    fn duplicate_suppression() {
        let input = vec![b'a'; 48];
        assert_eq!(
            run_bytes(&["-An", "-c"], &input).1,
            "   a   a   a   a   a   a   a   a   a   a   a   a   a   a   a   a\n*\n"
        );
        assert_eq!(
            run_bytes(&[], &input).1,
            "0000000 060541 060541 060541 060541 060541 060541 060541 060541\n*\n0000060\n"
        );
        // -v dumps everything
        assert_eq!(run_bytes(&["-v", "-An", "-c"], &input).1.lines().count(), 3);
    }

    #[test]
    fn skip_limit_width() {
        assert_eq!(run(&["-An", "-N", "2", "-c"], "abc").1, "   a   b\n");
        assert_eq!(run(&["-An", "-j", "2", "-c"], "abcd").1, "   c   d\n");
        assert_eq!(run(&["-j", "99"], "abc").0, 1);
        assert!(run(&["-j", "99"], "abc").2.contains("cannot skip past end"));
        assert_eq!(run(&["-N", "0"], "abc").1, "0000000\n");
        assert_eq!(run(&["-An", "-w1", "-c"], "abc").1, "   a\n   b\n   c\n");
        assert_eq!(
            run(&["-An", "-w3", "-tx1"], "abcde").1,
            " 61 62 63\n 64 65\n"
        );
        assert_eq!(
            run(&["-A", "d"], "abc").1,
            "0000000 061141 000143\n0000003\n"
        );
        assert_eq!(run(&["-A", "x"], "abc").1, "000000 061141 000143\n000003\n");
        assert_eq!(
            run(&["--format=o2", "--address-radix=d"], "abc").1,
            "0000000 061141 000143\n0000003\n"
        );
    }

    #[test]
    fn multi_file_and_errors() {
        let sc = ScratchCtx::new("od-files");
        std::fs::write(sc.path("f1"), "abc").unwrap();
        std::fs::write(sc.path("f2"), "def").unwrap();
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = ["-An", "-c", "f1", "f2"].iter().map(Into::into).collect();
        let r = od_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert_eq!(io.out(), "   a   b   c   d   e   f\n");
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["-An", "-c", "nosuch"].iter().map(Into::into).collect();
        let r = od_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("od: nosuch: No such file or directory"));
        assert_eq!(run(&["-t", "x1zq"], "abc").0, 1);
        assert_eq!(run(&["--bogus"], "").0, 1);
    }

    #[test]
    fn type_spec_parse() {
        assert_eq!(
            parse_type_spec("x1z").unwrap(),
            (vec![OdType::Hex(1)], true)
        );
        assert_eq!(
            parse_type_spec("d4,u1").unwrap(),
            (vec![OdType::Signed(4), OdType::Unsigned(1)], false)
        );
        assert!(parse_type_spec("x1zq").is_err());
        // probed: -t o / -t x / -t d / -t u default to 4-byte units
        assert_eq!(parse_type_spec("o").unwrap().0, vec![OdType::Octal(4)]);
        assert_eq!(parse_type_spec("x").unwrap().0, vec![OdType::Hex(4)]);
        assert_eq!(parse_type_spec("d").unwrap().0, vec![OdType::Signed(4)]);
        assert_eq!(parse_type_spec("f").unwrap().0, vec![OdType::Float(8)]);
        assert_eq!(parse_bytes_arg("16"), Some(16));
        assert_eq!(parse_bytes_arg("0x10"), Some(16));
        assert_eq!(parse_bytes_arg("1b"), Some(512));
        assert_eq!(parse_bytes_arg("2K"), Some(2048));
    }
}
