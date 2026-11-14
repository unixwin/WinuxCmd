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
//! Old-style operands (od.c main()/parse_old_offset): `[FILE] [[+]OFFSET
//! [.][b] [[+]LABEL[.][b]]]` is interpreted when no modern option was given
//! or under `--traditional`; OFFSET sets the skip, LABEL adds the
//! parenthesised pseudo-address column (with -A n the address column
//! becomes a bare parenthesised octal address). Probed 9.4: the offset
//! radix is octal unless 0x/0X (hex), the only multiplier is b/B (x512) —
//! the help text's stale '.' suffix makes the operand a FILE name.
//!
//! `-S/--strings` prints NUL-terminated runs of at least N printable
//! characters (bare --strings = 3), one line per run, `-N` bounding the
//! scan; `-S` with a type is "no type may be specified when dumping
//! strings". `fL` (80-bit long double) cannot be represented with std
//! floats and is rejected. `-A n` with empty input prints nothing at all,
//! matching GNU.

use std::ffi::OsString;
use std::io::{Read, Write};

use crate::argopt::{self, OptSpec};
use crate::fsutil::{gnu_open_reason, quotef, resolve};
use crate::gnuhelp;
use winux_core::{tr, CmdResult, Ctx, Io};

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
fn parse_type_spec(spec: &str, ctx: &Ctx) -> Result<(Vec<(OdType, bool)>, bool), String> {
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
                    types.push((OdType::Named, false));
                    chars.next();
                }
                'c' => {
                    types.push((OdType::Char, false));
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
                                        return Err(tr!(
                                            ctx,
                                            "command.od.error.unsupported_float_size",
                                            "od: unsupported float size in type '{part}'",
                                            part = part
                                        ));
                                    }
                                    size_str.push('8');
                                }
                                'F' if c == 'f' => size_str.push('4'),
                                'D' if c == 'f' => size_str.push('8'),
                                _ => {
                                    return Err(tr!(
                                        ctx,
                                        "command.od.error.invalid_type_char",
                                        "od: invalid character '{ch}' in type string '{part}'",
                                        ch = nxt,
                                        part = part
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
                        size_str.parse().map_err(|_| {
                            tr!(
                                ctx,
                                "command.od.error.invalid_type_char_unnamed",
                                "od: invalid character in type string '{part}'",
                                part = part
                            )
                        })?
                    };
                    if c == 'f' && size != 4 && size != 8 {
                        return Err(tr!(
                            ctx,
                            "command.od.error.unsupported_float_size",
                            "od: unsupported float size in type '{part}'",
                            part = part
                        ));
                    }
                    if c != 'f' && size != 1 && size != 2 && size != 4 && size != 8 {
                        return Err(tr!(
                            ctx,
                            "command.od.error.invalid_type_char",
                            "od: invalid character '{ch}' in type string '{part}'",
                            ch = size_str,
                            part = part
                        ));
                    }
                    types.push((
                        match c {
                            'd' => OdType::Signed(size),
                            'u' => OdType::Unsigned(size),
                            'o' => OdType::Octal(size),
                            'x' => OdType::Hex(size),
                            _ => OdType::Float(size),
                        },
                        z_here,
                    ));
                }
                other => {
                    return Err(tr!(
                        ctx,
                        "command.od.error.invalid_type_char",
                        "od: invalid character '{ch}' in type string '{part}'",
                        ch = other,
                        part = part
                    ));
                }
            }
        }
    }
    if types.is_empty() {
        return Err(tr!(
            ctx,
            "command.od.error.bad_type_specification",
            "od: bad type specification"
        ));
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

/// od.c print_function output: the value alone. Field width/precision are
/// applied by od_dump's pad loop (od.c PRINT_FIELDS: `adjusted_width =
/// pad_remaining - next_pad + width`).
fn render_item(t: OdType, chunk: &[u8], big_endian: bool) -> String {
    let (width, _zero_pad) = field_spec(t);
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
    let _ = width;
    rendered
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
    /// (type, ascii-trailer) — the `z` suffix belongs to the spec it is
    /// spelled on (od.c tspec.hexl_mode_trailer), not to the whole dump.
    types: Vec<(OdType, bool)>,
    radix: Radix,
    width: usize,
    verbose: bool,
    big_endian: bool,
    /// Absolute input offset of `input[0]` (`-j` or the traditional OFFSET).
    /// od prints addresses in the combined input's coordinate space
    /// (od.c dump(): `current_offset = n_bytes_to_skip`), so the first
    /// printed address is the skip value, not 0.
    skip: u64,
    /// `LABEL - skip` (od.c `pseudo_offset`): added to every printed
    /// address for the parenthesised pseudo-address column.
    pseudo_offset: i128,
    /// How an address is printed. od.c picks this from `flag_pseudo_start`
    /// plus the address radix: `Paren` when `-A n` was also given (od.c
    /// main(): format_address_paren, base forced to octal), `Label`
    /// otherwise.
    address_form: AddressForm,
    /// `Some(min)` = --strings/-S mode: print NUL-terminated runs of at
    /// least `min` printable characters instead of a dump.
    string_min: Option<usize>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum AddressForm {
    /// plain address column (format_address_std)
    Std,
    /// `(address)` only, used by -A n + LABEL (format_address_paren)
    Paren,
    /// address + `(address + pseudo_offset)` (format_address_label)
    Label,
}

/// od.c format_address_std: zero-padded address in the selected radix.
fn address_std(radix: Radix, address: u64) -> String {
    match radix {
        Radix::None => String::new(),
        Radix::Decimal => format!("{address:07}"),
        Radix::Octal => format!("{address:07o}"),
        Radix::Hex => format!("{address:06x}"),
    }
}

/// od.c format_address dispatch (std / paren / label).
fn format_address(cfg: &OdConfig, address: u64) -> String {
    match cfg.address_form {
        AddressForm::Std => address_std(cfg.radix, address),
        AddressForm::Paren => format!("({})", address_std(Radix::Octal, address)),
        AddressForm::Label => {
            let pseudo = (address as i128 + cfg.pseudo_offset).max(0) as u64;
            format!(
                "{} ({})",
                address_std(cfg.radix, address),
                address_std(cfg.radix, pseudo)
            )
        }
    }
}

/// Column width the later types blank-pad to (od.c `address_pad_len`: 7
/// for d/o, 6 for x, 0 for -A n; a paren address forces 7).
fn address_pad_len(cfg: &OdConfig) -> usize {
    match cfg.address_form {
        AddressForm::Paren => 7,
        AddressForm::Std | AddressForm::Label => match cfg.radix {
            Radix::None => 0,
            Radix::Hex => 6,
            _ => 7,
        },
    }
}

fn od_dump(input: &[u8], cfg: &OdConfig, out: &mut dyn Write) -> std::io::Result<()> {
    let bytes_per_line = cfg.width;
    let mut offset = 0usize;
    let mut prev: Option<&[u8]> = None;
    let mut star_active = false;
    let mut first_chunk = true;
    let label_width = address_pad_len(cfg);
    // od.c main(): width_per_block = max over specs of
    // (field_width+1)*fields_per_block; spec.pad_width = the difference.
    let width_per_block = cfg
        .types
        .iter()
        .map(|&(t, _)| {
            let size = type_size(t);
            let fields = bytes_per_line / size;
            (field_spec(t).0 + 1) * fields
        })
        .max()
        .unwrap_or(0);
    while offset < input.len() {
        let end = (offset + bytes_per_line).min(input.len());
        let chunk = &input[offset..end];
        let address = cfg.skip + offset as u64;
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
        for (ti, &(t, z_here)) in cfg.types.iter().enumerate() {
            let size = type_size(t);
            let mut line = String::new();
            if ti == 0 {
                line.push_str(&format_address(cfg, address));
            } else {
                // later types blank-pad the label column
                line.push_str(&" ".repeat(label_width));
            }
            // od.c main(): width_per_block is the widest spec's
            // (field_width+1)*fields; each spec's pad is the difference to
            // its own field_width*fields, distributed across the printed
            // fields by PRINT_FIELDS (so a -t x1 column next to a 3-wide
            // -t o1 column widens to 4).
            let (field_width, zero_pad) = field_spec(t);
            let fields_per_block = bytes_per_line / size;
            let pad_width = width_per_block - field_width * fields_per_block;
            let mut area = String::new();
            let mut pad_remaining = pad_width;
            let blank_fields = (bytes_per_line - chunk.len()) / size;
            let mut idx = fields_per_block;
            let mut pos = 0usize;
            while pos < chunk.len() && idx > blank_fields {
                let take = size.min(chunk.len() - pos);
                let mut unit = chunk[pos..pos + take].to_vec();
                // trailing partial unit is zero-filled at full width
                unit.resize(size, 0);
                let rendered = render_item(t, &unit, cfg.big_endian);
                let next_pad = if fields_per_block > 0 {
                    pad_width * (idx - 1) / fields_per_block
                } else {
                    0
                };
                let adjusted = pad_remaining - next_pad + field_width;
                let text = if zero_pad {
                    format!("{rendered:0>field_width$}")
                } else {
                    rendered
                };
                area.push_str(&format!("{text:>adjusted$}"));
                pad_remaining = next_pad;
                pos += size;
                idx -= 1;
            }
            if z_here {
                // od.c write_block(): the blank fields plus their share of
                // the pad, then dump_hexl_mode_trailer's "  >ascii<".
                let blank_pad = if fields_per_block > 0 {
                    pad_width * blank_fields / fields_per_block
                } else {
                    0
                };
                // GNU then prints dump_hexl_mode_trailer's "  >ascii<":
                // the two spaces belong to the trailer, not the padding,
                // and `printf("%*s")` adds blanks after the printed area
                // (not an absolute column).
                let trailer_pad = blank_fields * field_width + blank_pad + 2;
                for _ in 0..trailer_pad {
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
    // trailing offset line: suppressed only by -A n (od.c dump():
    // `format_address (current_offset, '\n')`, current_offset starting at
    // the skip value)
    if cfg.radix != Radix::None {
        let mut line = format_address(cfg, cfg.skip + input.len() as u64);
        line.push('\n');
        write!(out, "{line}")?;
    }
    Ok(())
}

/// od.c dump_strings(): NUL-terminated runs of at least `string_min`
/// printable (C locale) characters. The address printed is the run's
/// absolute start; `-N` bounds the scan exactly like GNU (the
/// `end_offset < string_min || end_offset - string_min <= address`
/// guard plus the `address < end_offset` inner condition), and a run cut
/// off by the read limit still prints.
fn od_dump_strings(
    input: &[u8],
    cfg: &OdConfig,
    end_offset: Option<u64>,
    out: &mut dyn Write,
) -> std::io::Result<()> {
    let min = cfg.string_min.unwrap_or(3);
    let mut pos = 0usize; // index into input (already skip-sliced)
    let mut address = cfg.skip;

    loop {
        if let Some(end) = end_offset {
            if end < min as u64 || end - min as u64 <= address {
                break;
            }
        }
        let mut buf: Vec<u8> = Vec::new();
        let mut ok = true;
        for _ in 0..min {
            if pos >= input.len() {
                return Ok(());
            }
            let c = input[pos];
            pos += 1;
            address += 1;
            if !(0x20..=0x7e).contains(&c) {
                ok = false;
                break;
            }
            buf.push(c);
        }
        let mut terminated = false;
        if ok {
            loop {
                if let Some(end) = end_offset {
                    if address >= end {
                        terminated = true;
                        break;
                    }
                }
                if pos >= input.len() {
                    return Ok(());
                }
                let c = input[pos];
                pos += 1;
                address += 1;
                if c == 0 {
                    terminated = true;
                    break;
                }
                if !(0x20..=0x7e).contains(&c) {
                    ok = false;
                    break;
                }
                buf.push(c);
            }
        }
        if !ok || !terminated {
            if !ok {
                continue; // tryline: restart from the next character
            }
            return Ok(());
        }
        // od.c: `format_address (address - i - 1, ' ')` with uintmax_t
        // arithmetic; a run cut by -N can sit at address 0 with a
        // non-empty buffer, which wraps exactly like GNU (probed:
        // `printf 'hello\0world\0' | od -S3 -N5` prints the all-ones
        // address).
        let start = address.wrapping_sub(buf.len() as u64).wrapping_sub(1);
        let mut line = format_address(cfg, start);
        // format_address_std emits the trailing separator itself; the
        // none form (`-A n`) emits nothing at all, so no stray space
        // (probed: `od -An -S3` output starts with the string).
        if !line.is_empty() {
            line.push(' ');
        }
        for &c in &buf {
            line.push(c as char);
        }
        line.push('\n');
        write!(out, "{line}")?;
    }
    Ok(())
}

/// od.c parse_old_offset(): `[+]OFFSET` with the traditional radix rule
/// (0x/0X -> hex, else octal) and one optional b/B multiplier (xstrtoumax
/// valid suffixes "Bb"). A malformed value (including the help text's stale
/// '.' suffix) is not an offset at all — od then treats the operand as a
/// file name.
fn parse_old_offset(text: &str) -> Option<u64> {
    let t = text.strip_prefix('+').unwrap_or(text);
    if t.is_empty() {
        return None;
    }
    let (radix, body) = if let Some(hex) = t.strip_prefix("0x").or_else(|| t.strip_prefix("0X")) {
        (16u32, hex)
    } else {
        (8u32, t)
    };
    let num_end = body
        .find(|c: char| !c.is_digit(radix))
        .unwrap_or(body.len());
    if num_end == 0 {
        return None;
    }
    let value = u64::from_str_radix(&body[..num_end], radix).ok()?;
    match &body[num_end..] {
        "" => Some(value),
        "b" | "B" => value.checked_mul(512),
        _ => None,
    }
}

pub fn od_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut norm: Vec<OsString> = Vec::new();
    // classic single-letter type options accumulate -t specs
    // od.c CASE_OLD_ARG table, including the obsolescent/undocumented
    // letters (D u4, e fD, F fF, H x4, I/L dL, O o4, B o2, h x2, X x4).
    // The differential corpus (regressions/79-od-legacy-letters) pins them.
    let classic: &[(&str, OdType)] = &[
        ("a", OdType::Named),
        ("b", OdType::Octal(1)),
        ("c", OdType::Char),
        ("d", OdType::Unsigned(2)),
        ("e", OdType::Float(8)),
        ("f", OdType::Float(4)),
        ("h", OdType::Hex(2)),
        ("i", OdType::Signed(4)),
        ("l", OdType::Signed(8)),
        ("o", OdType::Octal(2)),
        ("s", OdType::Signed(2)),
        ("x", OdType::Hex(2)),
        ("B", OdType::Octal(2)),
        ("D", OdType::Unsigned(4)),
        ("F", OdType::Float(4)),
        ("H", OdType::Hex(4)),
        ("I", OdType::Signed(8)),
        ("L", OdType::Signed(8)),
        ("O", OdType::Octal(4)),
        ("X", OdType::Hex(4)),
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
    // od.c main(): "modern" means one of the modern options was used; the
    // old-style `[FILE] [[+]OFFSET[.][b]]` operand form is interpreted only
    // when no modern option was given (`if (!modern || traditional)`).
    // Classic single-letter format options do NOT set modern in od.c, so
    // only the modern spellings count here.
    let mut modern = false;
    for a in args {
        let s = a.to_string_lossy();
        if let Some(long) = s.strip_prefix("--") {
            let name = long.split('=').next().unwrap_or("");
            if matches!(
                name,
                "address-radix"
                    | "skip-bytes"
                    | "read-bytes"
                    | "strings"
                    | "format"
                    | "output-duplicates"
                    | "width"
                    | "endian"
            ) {
                modern = true;
            }
        } else if s.len() > 1 && s.starts_with('-') {
            for ch in s[1..].chars() {
                if matches!(ch, 'A' | 'j' | 'N' | 'S' | 't' | 'v' | 'w') {
                    modern = true;
                }
            }
        }
    }
    // Which spelling carried --strings/-S (diagnostics only: GNU's
    // xstrtol_fatal prints the spelling the user typed).
    let mut strings_spelling = "--strings";
    for a in args {
        let s = a.to_string_lossy();
        if s == "-S" || (s.starts_with("-S") && !s.starts_with("--")) {
            strings_spelling = "-S";
        } else if s.starts_with("--strings") {
            strings_spelling = "--strings";
        }
    }
    for a in args {
        let s = a.to_string_lossy().into_owned();
        // --strings takes an OPTIONAL argument (getopt "S::"): a bare
        // --strings means 3 (od.c main() case 'S': optarg == NULL ->
        // string_min = 3). The short -S requires its argument, which the
        // OptSpec models, so only the long spelling needs normalizing.
        if s == "--strings" {
            norm.push(OsString::from("--strings=3"));
            continue;
        }
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
        // A cluster of classic format letters ("-bc", "-D -e -H -O") is
        // getopt's accumulate form: each letter becomes one -t spec, in
        // argv order.
        if s.len() >= 2 && s.starts_with('-') && !s.starts_with("--") {
            let letters: Vec<char> = s[1..].chars().collect();
            if letters.iter().all(|ch| {
                classic
                    .iter()
                    .any(|(c, _)| c.starts_with(*ch) && c.len() == 1)
            }) {
                for ch in letters {
                    let t = classic
                        .iter()
                        .find(|(c, _)| *c == ch.to_string())
                        .map(|(_, t)| *t)
                        .unwrap();
                    norm.push(OsString::from(format!("--format={}", spec_of(t))));
                }
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

    let mut types: Vec<(OdType, bool)> = Vec::new();
    for v in p.values.iter().filter(|(k, _)| k == "format") {
        match parse_type_spec(&v.1, ctx) {
            Ok((mut ts, _has_z)) => {
                types.append(&mut ts);
            }
            Err(msg) => {
                let _ = writeln!(io.stderr, "{msg}");
                return Ok(1);
            }
        }
    }
    // --strings/-S: od.c main() case 'S' (xstrtoumax base 0 + multipliers,
    // `string_min` may be 0) and the "no type may be specified when
    // dumping strings" fatal (od.c main() after the option loop; error()
    // + EXIT_FAILURE, no Try-help line).
    let string_min: Option<usize> = match p.value_of("strings") {
        None => None,
        Some(v) => {
            let q = if v.is_empty() {
                "''".to_string()
            } else {
                format!("'{}'", quotef(v))
            };
            match parse_bytes_arg(v) {
                Some(n) if n <= usize::MAX as u64 => Some(n as usize),
                Some(_) => {
                    let _ = writeln!(
                        io.stderr,
                        "od: {}",
                        tr!(
                            ctx,
                            "command.od.error.strings_too_large",
                            "{option} argument {value} too large",
                            option = strings_spelling,
                            value = q
                        )
                    );
                    return Ok(1);
                }
                None if !v.is_empty() && v.bytes().all(|b| b.is_ascii_digit()) => {
                    // syntactically a number, but beyond uintmax
                    let _ = writeln!(
                        io.stderr,
                        "od: {}",
                        tr!(
                            ctx,
                            "command.od.error.strings_too_large",
                            "{option} argument {value} too large",
                            option = strings_spelling,
                            value = q
                        )
                    );
                    return Ok(1);
                }
                None => {
                    let _ = writeln!(
                        io.stderr,
                        "od: {}",
                        tr!(
                            ctx,
                            "command.od.error.strings_invalid",
                            "invalid {option} argument {value}",
                            option = strings_spelling,
                            value = q
                        )
                    );
                    return Ok(1);
                }
            }
        }
    };
    if string_min.is_some() && !types.is_empty() {
        let _ = writeln!(
            io.stderr,
            "od: {}",
            tr!(
                ctx,
                "command.od.error.no_type_with_strings",
                "no type may be specified when dumping strings"
            )
        );
        return Ok(1);
    }
    if types.is_empty() {
        types.push((OdType::Octal(2), false)); // default: octal 2-byte units
    }

    let radix = match p.value_of("address-radix") {
        None => Radix::Octal,
        Some(r) => match r {
            "d" => Radix::Decimal,
            "o" => Radix::Octal,
            "x" => Radix::Hex,
            "n" => Radix::None,
            other => {
                let _ = writeln!(
                    io.stderr,
                    "od: {}",
                    tr!(
                        ctx,
                        "command.od.error.invalid_address_radix",
                        "invalid address radix '{radix}'",
                        radix = other
                    )
                );
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
                let _ = writeln!(
                    io.stderr,
                    "od: {}",
                    tr!(
                        ctx,
                        "command.od.error.invalid_skip_bytes",
                        "invalid skip bytes {value}",
                        value = v
                    )
                );
                return Ok(1);
            }
        }
    }
    let limit = match p.value_of("read-bytes") {
        None => None,
        Some(v) => match parse_bytes_arg(v) {
            Some(n) => Some(n),
            None => {
                let _ = writeln!(
                    io.stderr,
                    "od: {}",
                    tr!(
                        ctx,
                        "command.od.error.invalid_read_bytes",
                        "invalid read bytes {value}",
                        value = v
                    )
                );
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
                let _ = writeln!(
                    io.stderr,
                    "od: {}",
                    tr!(
                        ctx,
                        "command.od.error.invalid_width",
                        "invalid width {value}",
                        value = v
                    )
                );
                return Ok(1);
            }
        },
    };
    let big_endian = match p.value_of("endian") {
        None => false,
        Some("little") => false,
        Some("big") => true,
        Some(other) => {
            let _ = writeln!(
                io.stderr,
                "od: {}",
                tr!(
                    ctx,
                    "command.od.error.invalid_endian",
                    "invalid bytes to swap '{endian}'",
                    endian = other
                )
            );
            let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "od"));
            return Ok(1);
        }
    };
    // od.c main(): the old-style operand form.
    //   od [FILE] [[+]OFFSET[.][b] [[+]LABEL[.][b]]]
    // Interpreted when no modern option was given, always under
    // --traditional. The traditional form also takes a LABEL and rejects a
    // second file. OFFSET/LABEL parse like od.c parse_old_offset: optional
    // '+', radix by prefix (0x/0X hex, else octal), optional b multiply-512
    // suffix (probed 9.4: `2.` is a FILE, not an offset — xstrtoumax only
    // accepts the b/B multiplier, the help text's '.' spelling is stale).
    let traditional = p.has("traditional");
    let mut operands: Vec<String> = p
        .positionals
        .iter()
        .map(|a| a.to_string_lossy().into_owned())
        .collect();
    let mut pseudo_start: Option<u64> = None;
    if !modern || traditional {
        match operands.len() {
            1 => {
                if traditional || operands[0].starts_with('+') {
                    if let Some(o1) = parse_old_offset(&operands[0]) {
                        skip = o1;
                        operands.clear();
                    }
                }
            }
            2 => {
                let second_ok = traditional
                    || operands[1].starts_with('+')
                    || operands[1]
                        .chars()
                        .next()
                        .is_some_and(|c| c.is_ascii_digit());
                if second_ok {
                    if let Some(o2) = parse_old_offset(&operands[1]) {
                        let label = if traditional {
                            parse_old_offset(&operands[0])
                        } else {
                            None
                        };
                        match label {
                            Some(o1) => {
                                skip = o1;
                                pseudo_start = Some(o2);
                                operands.clear();
                            }
                            None => {
                                skip = o2;
                                operands.truncate(1);
                            }
                        }
                    }
                }
            }
            3 => {
                if traditional {
                    if let (Some(o1), Some(o2)) = (
                        parse_old_offset(&operands[1]),
                        parse_old_offset(&operands[2]),
                    ) {
                        skip = o1;
                        pseudo_start = Some(o2);
                        operands.truncate(1);
                    }
                }
            }
            _ => {}
        }
        if traditional && operands.len() > 1 {
            let _ = writeln!(
                io.stderr,
                "od: {}",
                tr!(
                    ctx,
                    "command.od.error.extra_operand",
                    "extra operand \u{2018}{operand}\u{2019}",
                    operand = operands[1]
                )
            );
            let _ = writeln!(
                io.stderr,
                "od: {}",
                tr!(
                    ctx,
                    "command.od.error.compatibility_one_file",
                    "compatibility mode supports at most one file"
                )
            );
            let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "od"));
            return Ok(1);
        }
    }

    // od.c main(): a LABEL switches the address printer; with -A n the
    // address column is a bare parenthesised octal address, otherwise the
    // normal address followed by the pseudo-address.
    let (radix, address_form) = match (pseudo_start, radix) {
        (None, r) => (r, AddressForm::Std),
        (Some(_), Radix::None) => (Radix::Octal, AddressForm::Paren),
        (Some(_), r) => (r, AddressForm::Label),
    };
    let pseudo_offset = match pseudo_start {
        None => 0i128,
        Some(p) => p as i128 - skip as i128,
    };

    // read the combined input
    let files: Vec<String> = if operands.is_empty() {
        vec!["-".to_string()]
    } else {
        operands.clone()
    };
    let mut input: Vec<u8> = Vec::new();
    let mut failed = false;
    // od.c main(): when NO operand could be opened at all, main jumps to
    // cleanup before dump() — no block lines and no trailing offset line
    // (probed: `od nosuch` prints only the diagnostic; `od /dev/null`
    // still prints 0000000 because the stream opened).
    let mut opened_any = false;
    for file in &files {
        if file == "-" {
            opened_any = true;
            if io.stdin.read_to_end(&mut input).is_err() {
                let _ = writeln!(
                    io.stderr,
                    "od: {}",
                    tr!(
                        ctx,
                        "command.od.error.reading_standard_input",
                        "error reading 'standard input'"
                    )
                );
                failed = true;
            }
        } else {
            match crate::fsutil::open_read_operand(ctx, file) {
                Ok(mut f) => {
                    opened_any = true;
                    if f.read_to_end(&mut input).is_err() {
                        if std::fs::metadata(resolve(ctx, file))
                            .map(|m| m.is_dir())
                            .unwrap_or(false)
                        {
                            let _ = writeln!(
                                io.stderr,
                                "od: {}",
                                tr!(
                                    ctx,
                                    "command.od.error.operand_reason",
                                    "{file}: {reason}",
                                    file = file,
                                    reason = tr!(
                                        ctx,
                                        "command.od.error.is_a_directory",
                                        "read error: Is a directory"
                                    )
                                )
                            );
                        } else {
                            let _ = writeln!(
                                io.stderr,
                                "od: {}",
                                tr!(
                                    ctx,
                                    "command.od.error.operand_reason",
                                    "{file}: {reason}",
                                    file = file,
                                    reason = tr!(ctx, "command.od.error.read_error", "read error")
                                )
                            );
                        }
                        failed = true;
                    }
                }
                Err(e) => {
                    let reason = gnu_open_reason(ctx, file, &e, false);
                    let _ = writeln!(
                        io.stderr,
                        "od: {}",
                        tr!(
                            ctx,
                            "command.od.error.operand_reason",
                            "{file}: {reason}",
                            file = quotef(file),
                            reason = reason
                        )
                    );
                    failed = true;
                }
            }
        }
    }
    if !opened_any {
        return Ok(1);
    }
    // -j skip / -N limit over the combined stream (od.c end_offset =
    // n_bytes_to_skip + max_bytes_to_format, with the overflow fatal).
    if skip as usize > input.len() {
        let _ = writeln!(
            io.stderr,
            "od: {}",
            tr!(
                ctx,
                "command.od.error.skip_past_end",
                "cannot skip past end of combined input"
            )
        );
        return Ok(1);
    }
    let end_offset = match limit {
        None => None,
        Some(n) => match skip.checked_add(n) {
            Some(end) => Some(end),
            None => {
                let _ = writeln!(
                    io.stderr,
                    "od: {}",
                    tr!(
                        ctx,
                        "command.od.error.skip_read_too_large",
                        "skip-bytes + read-bytes is too large"
                    )
                );
                return Ok(1);
            }
        },
    };
    let window = &input[skip as usize..];
    let window = match limit {
        Some(n) => &window[..n.min(window.len() as u64) as usize],
        None => window,
    };

    let cfg = OdConfig {
        types,
        radix,
        width,
        verbose: p.has("output-duplicates"),
        big_endian,
        skip,
        pseudo_offset,
        address_form,
        string_min,
    };
    let dumped = if cfg.string_min.is_some() {
        od_dump_strings(window, &cfg, end_offset, io.stdout)
    } else {
        od_dump(window, &cfg, io.stdout)
    };
    if dumped.is_err() {
        let _ = writeln!(
            io.stderr,
            "od: {}",
            tr!(ctx, "command.od.error.write_error", "write error")
        );
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
        let sc = ScratchCtx::new("od-types");
        let ctx = sc.ctx();
        assert_eq!(
            parse_type_spec("x1z", &ctx).unwrap(),
            (vec![(OdType::Hex(1), true)], true)
        );
        assert_eq!(
            parse_type_spec("d4,u1", &ctx).unwrap(),
            (
                vec![(OdType::Signed(4), false), (OdType::Unsigned(1), false)],
                false
            )
        );
        assert!(parse_type_spec("x1zq", &ctx).is_err());
        // probed: -t o / -t x / -t d / -t u default to 4-byte units
        assert_eq!(
            parse_type_spec("o", &ctx).unwrap().0,
            vec![(OdType::Octal(4), false)]
        );
        assert_eq!(
            parse_type_spec("x", &ctx).unwrap().0,
            vec![(OdType::Hex(4), false)]
        );
        assert_eq!(
            parse_type_spec("d", &ctx).unwrap().0,
            vec![(OdType::Signed(4), false)]
        );
        assert_eq!(
            parse_type_spec("f", &ctx).unwrap().0,
            vec![(OdType::Float(8), false)]
        );
        assert_eq!(parse_bytes_arg("16"), Some(16));
        assert_eq!(parse_bytes_arg("0x10"), Some(16));
        assert_eq!(parse_bytes_arg("1b"), Some(512));
        assert_eq!(parse_bytes_arg("2K"), Some(2048));
    }

    // --strings / -S and the old-style operand form were ported for the
    // wire; every assertion below is a WSL coreutils 9.4 probe.
    #[test]
    fn strings_mode() {
        assert_eq!(run(&["-An", "-S5"], "xxhello\0world\n").1, "xxhello\n");
        assert_eq!(
            run(&["-An", "-S5", "-v"], "xxhello\0hello\0").1,
            "xxhello\nhello\n"
        );
        // bare --strings defaults to 3
        assert_eq!(run(&["-An", "--strings"], "xxhello\0").1, "xxhello\n");
        // the printed address is the run start; -A d follows the radix
        assert_eq!(run(&["-A", "d", "-S5"], "xxhello\0").1, "0000000 xxhello\n");
        // -j moves the address base
        assert_eq!(run(&["-An", "-S3", "-j1"], "zzhello\0").1, "zhello\n");
        // -N bounds the scan; the run cut at the limit still prints
        // probed: the -N4 limit cuts the run after "hell" and it prints
        assert_eq!(run(&["-An", "-S3", "-N4"], "hello\0").1, "hell\n");
        assert_eq!(run(&["-An", "-S3", "-N8"], "hello\0wo").1, "hello\n");
        // a type is incompatible with strings mode
        let (r, _, err) = run(&["-S5", "-tx1"], "abc");
        assert_eq!(r, 1);
        assert_eq!(err, "od: no type may be specified when dumping strings\n");
        // diagnostics carry the spelling the user typed
        assert_eq!(run(&["-Sabc"], "x").2, "od: invalid -S argument 'abc'\n");
        assert_eq!(
            run(&["--strings="], "x").2,
            "od: invalid --strings argument ''\n"
        );
        assert_eq!(
            run(&["-S999999999999999999999"], "x").2,
            "od: -S argument '999999999999999999999' too large\n"
        );
    }

    #[test]
    fn traditional_operands() {
        // OFFSET: octal by default, 0x hex, b = 512, leading + accepted
        assert_eq!(parse_old_offset("2"), Some(2));
        assert_eq!(parse_old_offset("+2"), Some(2));
        assert_eq!(parse_old_offset("010"), Some(8));
        assert_eq!(parse_old_offset("0x10"), Some(16));
        assert_eq!(parse_old_offset("2b"), Some(1024));
        assert_eq!(parse_old_offset("2B"), Some(1024));
        // probed 9.4: '.' is NOT a suffix (the operand stays a file name)
        assert_eq!(parse_old_offset("2."), None);
        assert_eq!(parse_old_offset("abc"), None);
        assert_eq!(parse_old_offset(""), None);
        // offset-only form prints addresses from the skip value
        assert_eq!(
            run(&["--traditional", "2"], "abcdef").1,
            "0000002 062143 063145\n0000006\n"
        );
        // LABEL adds the pseudo-address column; -A n makes it a bare
        // parenthesised octal address
        assert_eq!(
            run(&["--traditional", "2", "3"], "abcdef").1,
            "0000002 (0000003) 062143 063145\n0000006 (0000007)\n"
        );
        assert_eq!(
            run(&["-An", "--traditional", "2", "16"], "abcdef").1,
            "(0000002) 062143 063145\n(0000006)\n"
        );
        // the old form also applies without --traditional when no modern
        // option was used (`od file 2`); a second file is the traditional
        // extra-operand fatal
        let sc = ScratchCtx::new("od-trad");
        std::fs::write(sc.path("f1"), "abcdefghij").unwrap();
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["f1", "2"].iter().map(Into::into).collect();
        let r = od_cmd(&owned, &mut io.split(), &sc.ctx()).unwrap();
        assert_eq!(r, 0);
        assert!(io.out().starts_with("0000002 062143"));
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["--traditional", "f1", "2", "3", "4"]
            .iter()
            .map(Into::into)
            .collect();
        let r = od_cmd(&owned, &mut io.split(), &sc.ctx()).unwrap();
        assert_eq!(r, 1);
        assert_eq!(
            io.err(),
            "od: extra operand \u{2018}2\u{2019}\n\
             od: compatibility mode supports at most one file\n\
             Try 'od --help' for more information.\n"
        );
    }

    #[test]
    fn skip_addresses_are_absolute() {
        // od.c dump(): current_offset starts at n_bytes_to_skip
        assert_eq!(run(&["-j2", "-c"], "abcd").1, "0000002   c   d\n0000004\n");
        assert_eq!(run(&["-N2", "-c"], "abcd").1, "0000000   a   b\n0000002\n");
    }
}
