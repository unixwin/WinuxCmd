//! `numfmt` — GNU coreutils numfmt semantics (src/numfmt.c, coreutils
//! 9.4). Every case below was probed byte-for-byte against WSL coreutils
//! 9.4 (target/probes/p_numfmt*.sh).
//!
//! * --from accepts none/auto/si/iec/iec-i; --to only none/si/iec/iec-i.
//!   Suffix letters are KMGTPEZYRQ, UPPERCASE only (probed: `1k` is an
//!   invalid suffix), with the iec-i `i` spelled suffix for two-letter
//!   forms. Parsing is simple_strtod_human: no exponent, no `+` sign, a
//!   decimal point must be followed by digits, one optional blank between
//!   number and suffix, trailing blanks allowed.
//! * --round defaults to from-zero (away from zero) and applies at the
//!   output precision; --to scaling keeps one decimal below 10 and
//!   rescales when rounding carries past the base (999.95 -> 1.0K).
//! * Exit statuses: abort stops at the first bad value (2), fail reports
//!   each and still exits 2, warn/ignore exit 0. Option/value validation
//!   errors exit 1; --field errors carry the try-help line.
//! * 9.4 differences from the 1.x C++ applet (Rust follows GNU, recorded
//!   by the gate): no --unit-separator option, no -M/-l shorts, --unit
//!   sizes accept only K/M/G/T/P/E (+"Ki"), grouping is locale-driven (the
//!   C locale groups nothing), and a terminal line keeps its newline only
//!   if the input line had one.

use std::cell::RefCell;
use std::ffi::OsString;
use std::io::{Read, Write};

use crate::argopt::{self, OptSpec};
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('d'),
        long: "delimiter",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "from",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "to",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "round",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "padding",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "suffix",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "field",
        takes_value: true,
    },
    OptSpec {
        short: Some('f'),
        long: "format",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "header",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "grouping",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "invalid",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "debug",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "from-unit",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "to-unit",
        takes_value: true,
    },
    OptSpec {
        short: Some('z'),
        long: "zero-terminated",
        takes_value: false,
    },
];

// ---------------------------------------------------------------- parsing

/// [GNU] valid suffix letters and their powers for --from/--to values.
fn suffix_power(c: char) -> Option<u32> {
    match c {
        'K' => Some(1),
        'M' => Some(2),
        'G' => Some(3),
        'T' => Some(4),
        'P' => Some(5),
        'E' => Some(6),
        'Z' => Some(7),
        'Y' => Some(8),
        'R' => Some(9),
        'Q' => Some(10),
        _ => None,
    }
}

fn suffix_power_char(power: u32) -> &'static str {
    match power {
        0 => "",
        1 => "K",
        2 => "M",
        3 => "G",
        4 => "T",
        5 => "P",
        6 => "E",
        7 => "Z",
        8 => "Y",
        9 => "R",
        10 => "Q",
        _ => "(error)",
    }
}

fn is_field_blank(c: char) -> bool {
    c == ' ' || c == '\t'
}

struct ParsedInput {
    ok: bool,
    value: f64,
    /// Original integer text when the input was pure digits (no fraction,
    /// no suffix): the no-scale print path keeps it exact (GNU uses long
    /// double; f64 loses the 19th digit).
    raw_int: Option<String>,
    /// Fractional digits the user wrote; zeroed once a unit suffix scales.
    precision: u32,
    /// Trailing text after a valid suffix letter (for the tail diagnostic).
    tail: String,
    err: ParseError,
}

enum ParseError {
    None,
    InvalidNumber,
    ForbiddenSuffix,
    InvalidSuffix,
    InvalidSuffixTail,
    MissingI,
    Overflow,
}

/// [GNU] simple_strtod_human: "NNNN[.NNNN][sep][SUFFIX]".
fn parse_human(input: &str, scale_from: &str) -> ParsedInput {
    let mut r = ParsedInput {
        ok: false,
        value: 0.0,
        raw_int: None,
        precision: 0,
        tail: String::new(),
        err: ParseError::None,
    };
    let s: Vec<char> = input.chars().collect();
    let mut i = 0usize;
    if i < s.len() && s[i] == '-' {
        i += 1;
    }

    let scan_digits = |i: &mut usize| -> usize {
        let start = *i;
        while *i < s.len() && s[*i].is_ascii_digit() {
            *i += 1;
        }
        *i - start
    };
    let int_digits = scan_digits(&mut i);
    let mut frac_digits = 0;
    if i < s.len() && s[i] == '.' {
        i += 1;
        frac_digits = scan_digits(&mut i);
        // [GNU] "5." is rejected: a point must be followed by digits.
        if frac_digits == 0 {
            r.err = ParseError::InvalidNumber;
            return r;
        }
    }
    if int_digits == 0 && frac_digits == 0 {
        r.err = ParseError::InvalidNumber;
        return r;
    }
    // [GNU] digits beyond the double mantissa space are an overflow.
    let digit_count = int_digits + frac_digits;
    if digit_count > 33 {
        r.err = ParseError::Overflow;
        return r;
    }
    let text: String = s[..i].iter().collect();
    r.value = text.parse::<f64>().unwrap_or(0.0);
    if frac_digits == 0 {
        r.raw_int = Some(text);
    }
    r.precision = frac_digits as u32;

    if i >= s.len() {
        r.ok = true;
        return r;
    }

    // a single blank may separate the number from the suffix
    let mut j = i;
    if j < s.len() && is_field_blank(s[j]) {
        j += 1;
    }
    if j >= s.len() {
        r.ok = true;
        return r;
    }
    let Some(power) = suffix_power(s[j]) else {
        // trailing blanks are allowed; anything else is an invalid suffix
        while j < s.len() && is_field_blank(s[j]) {
            j += 1;
        }
        if j < s.len() {
            r.err = ParseError::InvalidSuffix;
        } else {
            r.ok = true;
        }
        return r;
    };

    if scale_from.is_empty() || scale_from == "none" {
        r.err = ParseError::ForbiddenSuffix;
        return r;
    }
    j += 1;
    let mut base: f64 = if scale_from == "iec" || scale_from == "iec-i" {
        1024.0
    } else {
        1000.0
    };
    if scale_from == "auto" && j < s.len() && s[j] == 'i' {
        // auto-scaling switches to base 1024 for "Ki/Mi/Gi" style input
        base = 1024.0;
        j += 1;
    } else if scale_from == "iec-i" {
        if j < s.len() && s[j] == 'i' {
            j += 1;
        } else {
            r.raw_int = None;
            r.err = ParseError::MissingI;
            return r;
        }
    }
    r.precision = 0;
    // a scaled suffix invalidates the raw-integer print shortcut
    r.raw_int = None;

    while j < s.len() && is_field_blank(s[j]) {
        j += 1;
    }
    if j < s.len() {
        r.err = ParseError::InvalidSuffixTail;
        r.tail = s[j..].iter().collect();
        return r;
    }
    r.value *= base.powi(power as i32);
    r.ok = true;
    r
}

/// [GNU] unit_to_umax: --from-unit/--to-unit take an integer with an
/// optional single K/M/G/T/P/E suffix; a trailing 'i' selects base 1024.
fn unit_size(text: &str) -> Option<f64> {
    let chars: Vec<char> = text.chars().collect();
    if chars.is_empty() {
        return None;
    }
    let mut digits_end = chars.len();
    let mut base = 1000.0f64;
    let mut power = 0u32;
    let last = chars[chars.len() - 1];
    if !last.is_ascii_digit() {
        if last == 'i' && chars.len() >= 2 && !chars[chars.len() - 2].is_ascii_digit() {
            base = 1024.0;
            digits_end -= 1;
        }
        if digits_end == 0 {
            return None;
        }
        // [GNU] unit sizes accept only K M G T P E (probed: Z and k are
        // rejected with "invalid unit size")
        power = match chars[digits_end - 1] {
            'K' => 1,
            'M' => 2,
            'G' => 3,
            'T' => 4,
            'P' => 5,
            'E' => 6,
            _ => return None,
        };
        digits_end -= 1;
        if !chars[..digits_end].iter().all(|c| c.is_ascii_digit()) {
            return None;
        }
    }
    let digits: String = chars[..digits_end].iter().collect();
    let n: u64 = if digits.is_empty() {
        1
    } else {
        digits.parse().ok()?
    };
    if n == 0 {
        return None;
    }
    Some(n as f64 * base.powi(power as i32))
}

fn apply_rounding(value: f64, mode: &str) -> f64 {
    match mode {
        "up" => value.ceil(),
        "down" => value.floor(),
        "towards-zero" => value.trunc(),
        "nearest" => {
            if value >= 0.0 {
                (value + 0.5).floor()
            } else {
                (value - 0.5).ceil()
            }
        }
        // "from-zero" is the GNU default
        _ => {
            if value >= 0.0 {
                value.ceil()
            } else {
                value.floor()
            }
        }
    }
}

/// C %g with the default 6 significant digits, for recorded diagnostics.
fn g6(v: f64) -> String {
    if !v.is_finite() {
        return if v.is_nan() {
            "nan".to_string()
        } else if v > 0.0 {
            "inf".to_string()
        } else {
            "-inf".to_string()
        };
    }
    if v == 0.0 {
        return "0".to_string();
    }
    let exp = v.abs().log10().floor();
    let exp = if exp.is_finite() { exp as i32 } else { 0 };
    if exp < -4 || exp >= 6 {
        let mut mantissa = v / 10f64.powi(exp);
        let mut e = exp;
        let mut m = format!("{mantissa:.5}");
        if m.starts_with("10") || m.starts_with("-10") {
            // rounding carried into the next decade
            mantissa /= 10.0;
            e += 1;
            m = format!("{mantissa:.5}");
        }
        // trim trailing zeros and the point (C %g drops them)
        if m.contains('.') {
            while m.ends_with('0') {
                m.pop();
            }
            if m.ends_with('.') {
                m.pop();
            }
        }
        format!("{m}e{e:+03}")
    } else {
        let decimals = (5 - exp).max(0) as usize;
        let mut s = format!("{v:.decimals$}");
        if s.contains('.') {
            while s.ends_with('0') {
                s.pop();
            }
            if s.ends_with('.') {
                s.pop();
            }
        }
        s
    }
}

fn group_integer_part(s: &str) -> String {
    let bytes = s.as_bytes();
    let beg = usize::from(!bytes.is_empty() && (bytes[0] == b'-' || bytes[0] == b'+'));
    let end = s.find('.').unwrap_or(s.len());
    let n = end - beg;
    if n <= 3 {
        return s.to_string();
    }
    let mut first = n % 3;
    if first == 0 {
        first = 3;
    }
    let mut out = String::new();
    out.push_str(&s[..beg]);
    out.push_str(&s[beg..beg + first]);
    let mut k = beg + first;
    while k < end {
        out.push(',');
        out.push_str(&s[k..(k + 3).min(end)]);
        k += 3;
    }
    out.push_str(&s[end..]);
    out
}

/// "%[0][N][.P]f" equivalent with optional grouping after padding.
fn numeric_text(val: f64, precision: usize, grouping: bool, zero_pad: u32) -> String {
    let mut s = format!("{val:.precision$}");
    if zero_pad > 0 {
        let target = zero_pad as usize;
        if s.len() < target {
            let pad = target - s.len();
            let zeros = "0".repeat(pad);
            if let Some(rest) = s.strip_prefix('-') {
                s = format!("-{zeros}{rest}");
            } else {
                s = format!("{zeros}{s}");
            }
        }
    }
    if grouping {
        s = group_integer_part(&s);
    }
    s
}

/// Number of decimal digits before the point minus one (expld exponent).
fn decimal_exponent(val: f64) -> i32 {
    let mut x = 0i32;
    let mut v = val.abs();
    if !v.is_finite() {
        return 34;
    }
    while v >= 10.0 {
        v /= 10.0;
        x += 1;
    }
    x
}

struct FmtSpec {
    prefix: String,
    suffix: String,
    grouping: bool,
    zero_pad: u32,
    pad_width: i64,
    user_precision: Option<u32>,
}

fn parse_format(fmt: &str) -> Result<FmtSpec, String> {
    let mut out = FmtSpec {
        prefix: String::new(),
        suffix: String::new(),
        grouping: false,
        zero_pad: 0,
        pad_width: 0,
        user_precision: None,
    };
    let bytes: Vec<char> = fmt.chars().collect();
    // Locate the single directive: %% pairs are literals skipped in steps
    // of two (so "%%f" carries no directive at all — GNU probed).
    let mut i = 0usize;
    let mut prefix_len = 0usize;
    loop {
        if i >= bytes.len() {
            return Err(format!("format '{fmt}' has no % directive"));
        }
        if bytes[i] == '%' && (i + 1 >= bytes.len() || bytes[i + 1] != '%') {
            break;
        }
        prefix_len += 1;
        i += if bytes[i] == '%' { 2 } else { 1 };
    }
    out.prefix = bytes[..prefix_len].iter().collect();
    i += 1; // the '%'
    let mut zero_padding = false;
    loop {
        let mut skip = 0;
        while i + skip < bytes.len() && bytes[i + skip] == ' ' {
            skip += 1;
        }
        i += skip;
        if i < bytes.len() && bytes[i] == '\'' {
            out.grouping = true;
            i += 1;
        } else if i < bytes.len() && bytes[i] == '0' {
            zero_padding = true;
            i += 1;
        } else if skip == 0 {
            break;
        }
    }
    let digits_start = i;
    while i < bytes.len() && (bytes[i] == '-' || bytes[i].is_ascii_digit()) {
        i += 1;
    }
    let pad_text: String = bytes[digits_start..i].iter().collect();
    let pad: i64 = if pad_text.is_empty() {
        0
    } else {
        pad_text.parse().unwrap_or(0)
    };
    if pad != 0 {
        if pad < 0 {
            out.pad_width = pad;
        } else if zero_padding {
            out.zero_pad = pad.min(i32::MAX as i64) as u32;
        } else {
            out.pad_width = pad;
        }
    }
    if i >= bytes.len() {
        return Err(format!("format '{fmt}' ends in %"));
    }
    if bytes[i] == '.' {
        i += 1;
        if i < bytes.len() && (bytes[i] == '+' || bytes[i] == ' ' || bytes[i] == '\t') {
            return Err(format!("invalid precision in format '{fmt}'"));
        }
        let prec_start = i;
        while i < bytes.len() && bytes[i].is_ascii_digit() {
            i += 1;
        }
        let prec_text: String = bytes[prec_start..i].iter().collect();
        // [GNU] "%.f" parses precision 0 (strtol yields 0, no error)
        out.user_precision = Some(prec_text.parse::<u32>().unwrap_or(0));
    }
    if i >= bytes.len() || bytes[i] != 'f' {
        return Err(format!(
            "invalid format '{fmt}', directive must be %[0]['][-][N][.][N]f"
        ));
    }
    i += 1;
    let suffix_start = i;
    while i < bytes.len() {
        if bytes[i] == '%' && i + 1 < bytes.len() && bytes[i + 1] != '%' {
            return Err(format!("format '{fmt}' has too many % directives"));
        }
        i += if bytes[i] == '%' { 2 } else { 1 };
    }
    out.suffix = bytes[suffix_start..].iter().collect();
    Ok(out)
}

/// cut(1)-style --field spec: N, N-, N-M, -M, -, comma separated.
fn parse_field_spec(spec: &str) -> Result<Vec<(u64, u64)>, String> {
    let max = u64::MAX;
    let mut ranges = Vec::new();
    for item in spec.split(',') {
        if item == "-" {
            ranges.push((1, max));
            continue;
        }
        let (lo_text, hi_text) = match item.split_once('-') {
            Some((l, h)) => (l, Some(h)),
            None => (item, None),
        };
        let parse_u = |t: &str| -> Option<u64> {
            if t.is_empty() || !t.chars().all(|c| c.is_ascii_digit()) {
                return None;
            }
            t.parse().ok()
        };
        let open_lo = item.starts_with('-');
        let open_hi = matches!(hi_text, Some(h) if h.is_empty());
        let lo = if open_lo {
            1
        } else {
            parse_u(lo_text).ok_or_else(|| format!("invalid field value '{item}'"))?
        };
        let hi = match hi_text {
            None => lo,
            Some(h) if h.is_empty() => max,
            Some(h) => parse_u(h).ok_or_else(|| format!("invalid field value '{item}'"))?,
        };
        if lo == 0 || hi == 0 {
            return Err("fields are numbered from 1".to_string());
        }
        if hi < lo {
            return Err("invalid decreasing range".to_string());
        }
        ranges.push((lo, hi));
    }
    Ok(ranges)
}

fn argmatch_error(io: &mut Io, option: &str, value: &str, valid: &[&str]) {
    let _ = write!(
        io.stderr,
        "numfmt: invalid argument '{value}' for '{option}'\n"
    );
    let _ = write!(io.stderr, "Valid arguments are:\n");
    for v in valid {
        let _ = write!(io.stderr, "  - '{v}'\n");
    }
    let _ = writeln!(io.stderr, "Try 'numfmt --help' for more information.");
}

/// Grouping only inserts separators when the locale environment suggests a
/// locale with digit grouping; the C/POSIX family groups nothing (the gate
/// oracle runs under C.UTF-8, which behaves like the C locale here).
fn locale_groups_digits(ctx: &Ctx) -> bool {
    for key in ["LC_ALL", "LC_NUMERIC", "LANG"] {
        if let Some(v) = ctx.env.get(key) {
            let v = v.to_string_lossy();
            if !v.is_empty() && !v.starts_with("C") && !v.starts_with("POSIX") {
                return true;
            }
            return false;
        }
    }
    false
}

pub fn numfmt_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    // --header takes an OPTIONAL value: a bare --header means 1.
    let mut args: Vec<OsString> = args.to_vec();
    for a in args.iter_mut() {
        if a.to_string_lossy() == "--header" {
            *a = OsString::from("--header=1");
        }
    }
    let p = match argopt::parse("numfmt", &args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if p.has("help") || p.has("version") {
        return Ok(0);
    }

    let mut to_unit = p.value_of("to").unwrap_or("").to_string();
    let from_unit = p.value_of("from").unwrap_or("").to_string();
    if !to_unit.is_empty() && !["none", "si", "iec", "iec-i"].contains(&to_unit.as_str()) {
        argmatch_error(io, "--to", &to_unit, &["none", "si", "iec", "iec-i"]);
        return Ok(1);
    }
    if !from_unit.is_empty()
        && !["none", "auto", "si", "iec", "iec-i"].contains(&from_unit.as_str())
    {
        argmatch_error(
            io,
            "--from",
            &from_unit,
            &["none", "auto", "si", "iec", "iec-i"],
        );
        return Ok(1);
    }
    let to_si = to_unit == "si";
    let to_iec = to_unit == "iec";
    let to_iec_i = to_unit == "iec-i";

    let round_mode = p.value_of("round").unwrap_or("").to_string();
    if !round_mode.is_empty()
        && !["up", "down", "from-zero", "towards-zero", "nearest"].contains(&round_mode.as_str())
    {
        argmatch_error(
            io,
            "--round",
            &round_mode,
            &["up", "down", "from-zero", "towards-zero", "nearest"],
        );
        return Ok(1);
    }
    let invalid_policy = p.value_of("invalid").unwrap_or("abort").to_string();
    if !["abort", "fail", "warn", "ignore"].contains(&invalid_policy.as_str()) {
        argmatch_error(
            io,
            "--invalid",
            &invalid_policy,
            &["abort", "fail", "warn", "ignore"],
        );
        return Ok(1);
    }

    // --delimiter: single character; an explicitly empty -d '' selects NUL.
    let delimiter_given = p.value_of("delimiter").is_some();
    let mut delimiter = p.value_of("delimiter").unwrap_or("").to_string();
    if delimiter_given && delimiter.chars().count() > 1 {
        let _ = writeln!(
            io.stderr,
            "numfmt: the delimiter must be a single character"
        );
        return Ok(1);
    }
    if delimiter_given && delimiter.is_empty() {
        delimiter.push('\0');
    }

    // --header=0 is invalid; a bare --header became --header=1 above.
    if let Some(h) = p.value_of("header") {
        let n: i64 = h.parse().unwrap_or(-1);
        if n == 0 {
            let _ = writeln!(io.stderr, "numfmt: invalid header value '0'");
            return Ok(1);
        }
    }
    let header: u64 = p
        .value_of("header")
        .and_then(|h| h.parse().ok())
        .unwrap_or(0);

    // --padding=0 is invalid; negative widths left-align.
    let padding_given = p.value_of("padding").is_some();
    let padding: i64 = p
        .value_of("padding")
        .and_then(|v| v.parse().ok())
        .unwrap_or(0);
    if padding_given && padding == 0 {
        let _ = writeln!(io.stderr, "numfmt: invalid padding value '0'");
        return Ok(1);
    }

    let grouping_flag = p.has("grouping");
    let format_str = p.value_of("format").unwrap_or("").to_string();
    if grouping_flag && !format_str.is_empty() {
        let _ = writeln!(
            io.stderr,
            "numfmt: --grouping cannot be combined with --format"
        );
        return Ok(1);
    }

    // [GNU] a second --field specification is a hard error.
    let field_specs: Vec<&str> = p
        .values
        .iter()
        .filter(|(k, _)| k == "field")
        .map(|(_, v)| v.as_str())
        .collect();
    if field_specs.len() > 1 {
        let _ = writeln!(io.stderr, "numfmt: multiple field specifications");
        return Ok(1);
    }
    let mut field_ranges: Vec<(u64, u64)> = Vec::new();
    if let Some(spec) = field_specs.first() {
        match parse_field_spec(spec) {
            Ok(r) => field_ranges = r,
            Err(e) => {
                let _ = writeln!(io.stderr, "numfmt: {e}");
                let _ = writeln!(io.stderr, "Try 'numfmt --help' for more information.");
                return Ok(1);
            }
        }
    }
    let include_field = |field: u64| -> bool {
        if field_ranges.is_empty() {
            return field == 1;
        }
        field_ranges
            .iter()
            .any(|(lo, hi)| *lo <= field && field <= *hi)
    };

    let fmt_spec = if format_str.is_empty() {
        FmtSpec {
            prefix: String::new(),
            suffix: String::new(),
            grouping: false,
            zero_pad: 0,
            pad_width: 0,
            user_precision: None,
        }
    } else {
        match parse_format(&format_str) {
            Ok(f) => f,
            Err(e) => {
                let _ = writeln!(io.stderr, "numfmt: {e}");
                return Ok(1);
            }
        }
    };
    // [GNU] the %'f flag (or --grouping) conflicts with --to.
    if (fmt_spec.grouping || grouping_flag) && (to_si || to_iec || to_iec_i) {
        let _ = writeln!(io.stderr, "numfmt: grouping cannot be combined with --to");
        return Ok(1);
    }
    // Grouping separators exist only when the locale says so; the
    // C/POSIX family (the gate oracle) groups nothing even with %'f.
    let grouping_active = (fmt_spec.grouping || grouping_flag) && locale_groups_digits(ctx);

    let from_unit_value = p.value_of("from-unit").unwrap_or("").to_string();
    let to_unit_value = p.value_of("to-unit").unwrap_or("").to_string();
    let unit_or_error = |text: &str| -> f64 {
        if text.is_empty() {
            return 1.0;
        }
        unit_size(text).unwrap_or(-1.0)
    };
    let from_scale = unit_or_error(&from_unit_value);
    let to_scale = unit_or_error(&to_unit_value);
    if from_scale < 0.0 || to_scale < 0.0 {
        let bad = if from_scale < 0.0 {
            &from_unit_value
        } else {
            &to_unit_value
        };
        let _ = writeln!(io.stderr, "numfmt: invalid unit size: '{bad}'");
        return Ok(1);
    }

    let debug = p.has("debug");
    let scaling_used = !to_unit.is_empty()
        || !from_unit.is_empty()
        || !from_unit_value.is_empty()
        || !to_unit_value.is_empty();
    if debug && !scaling_used {
        let _ = writeln!(io.stderr, "numfmt: no conversion option specified");
    }

    let zero_terminated = p.has("zero-terminated");
    let suffix = p.value_of("suffix").unwrap_or("").to_string();

    let scale_kind = if to_si {
        1
    } else if to_iec {
        2
    } else if to_iec_i {
        3
    } else {
        0
    };
    let padding_width = if fmt_spec.pad_width != 0 {
        fmt_spec.pad_width
    } else {
        padding
    };
    let auto_padding = padding_width == 0 && delimiter.is_empty();

    let proc = Processor {
        field_ranges: &field_ranges,
        from_unit: &from_unit,
        from_scale,
        to_scale,
        scale_kind,
        round_mode: &round_mode,
        invalid_policy: &invalid_policy,
        fmt_spec: &fmt_spec,
        grouping_active,
        padding_width,
        auto_padding,
        delimiter: &delimiter,
        zero_terminated,
        suffix: &suffix,
        header_lines: header,
        had_invalid: false,
    };
    let proc = RefCell::new(proc);

    let reading_stdin = p.positionals.is_empty();
    if !reading_stdin {
        for arg in &p.positionals {
            if !process_line_proc(&proc, io, &arg.to_string_lossy(), true) {
                return Ok(2);
            }
        }
    } else {
        let mut input = Vec::new();
        if io.stdin.read_to_end(&mut input).is_err() {
            return Ok(1);
        }
        let text = String::from_utf8_lossy(&input).into_owned();
        let line_delim = if zero_terminated { '\0' } else { '\n' };
        let mut start = 0usize;
        let bytes: Vec<char> = text.chars().collect();
        let mut idx = 0usize;
        while idx <= bytes.len() {
            if idx == bytes.len() {
                if idx > start {
                    let line: String = bytes[start..idx].iter().collect();
                    // final segment without a terminator keeps none
                    if !process_stdin_line_proc(&proc, io, &line, false) {
                        return Ok(2);
                    }
                }
                break;
            }
            if bytes[idx] == line_delim {
                let line: String = bytes[start..idx].iter().collect();
                if !process_stdin_line_proc(&proc, io, &line, true) {
                    return Ok(2);
                }
                start = idx + 1;
            }
            idx += 1;
        }
    }

    // [GNU] abort died on the first bad value; fail reports every bad
    // value and still exits 2; warn and ignore succeed.
    if proc.borrow().had_invalid && invalid_policy == "fail" {
        return Ok(2);
    }
    Ok(0)
}

struct Processor<'a> {
    field_ranges: &'a [(u64, u64)],
    from_unit: &'a str,
    from_scale: f64,
    to_scale: f64,
    scale_kind: u32,
    round_mode: &'a str,
    invalid_policy: &'a str,
    fmt_spec: &'a FmtSpec,
    grouping_active: bool,
    padding_width: i64,
    auto_padding: bool,
    delimiter: &'a str,
    zero_terminated: bool,
    suffix: &'a str,
    header_lines: u64,
    had_invalid: bool,
}

fn invalid_message(err: &ParseError, text: &str, tail: &str) -> String {
    match err {
        ParseError::ForbiddenSuffix => {
            format!("rejecting suffix in input: '{text}' (consider using --from)")
        }
        ParseError::InvalidSuffixTail => format!("invalid suffix in input '{text}': '{tail}'"),
        ParseError::MissingI => format!("missing 'i' suffix in input: '{text}' (e.g Ki/Mi/Gi)"),
        ParseError::InvalidSuffix => format!("invalid suffix in input: '{text}'"),
        ParseError::Overflow => format!("value too large to be converted: '{text}'"),
        _ => format!("invalid number: '{text}'"),
    }
}

impl<'a> Processor<'a> {
    // [GNU] double_to_human: with a --to scale the value normalizes to
    // [1, base), rounds at an adjusted decimal power (user precision
    // clamps to power*3), rescales when rounding overflows, and only
    // values below 10 keep a visible decimal point.
    fn to_human(&self, mut val: f64, precision_used: usize) -> String {
        if self.scale_kind == 0 {
            let p10 = 10f64.powi(precision_used as i32);
            val = apply_rounding(val * p10, self.round_mode) / p10;
            return numeric_text(
                val,
                precision_used,
                self.grouping_active,
                self.fmt_spec.zero_pad,
            ) + self.suffix;
        }
        let base: f64 = if self.scale_kind == 1 { 1000.0 } else { 1024.0 };
        let mut power: i32 = 0;
        if val.is_finite() {
            while val.abs() >= base {
                val /= base;
                power += 1;
            }
        }
        let mut power_adjust = 0u32;
        if let Some(up) = self.fmt_spec.user_precision {
            power_adjust = (power as u32 * 3).min(up);
        } else if val.abs() < 10.0 {
            // values below 10 keep one decimal digit
            power_adjust = 1;
        }
        let p10 = 10f64.powi(power_adjust as i32);
        val = apply_rounding(val * p10, self.round_mode) / p10;
        // a rounded "999.9" can become 1000 and must rescale up
        if val.abs() >= base {
            val /= base;
            power += 1;
        }
        let show_decimal = val != 0.0 && val.abs() < 10.0 && power > 0;
        let prec = if self.fmt_spec.user_precision.is_none() {
            usize::from(show_decimal)
        } else {
            self.fmt_spec.user_precision.unwrap() as usize
        };
        let mut out = numeric_text(val, prec, self.grouping_active, self.fmt_spec.zero_pad);
        if power > 0 {
            out.push_str(suffix_power_char(power as u32));
            if self.scale_kind == 3 {
                out.push('i');
            }
        }
        out.push_str(self.suffix);
        out
    }

    // Process one field; returns None when the "abort" policy must stop.
    fn field(&mut self, io: &mut Io, text: &str, field: u64) -> Option<bool> {
        let include = if self.field_ranges.is_empty() {
            field == 1
        } else {
            self.field_ranges
                .iter()
                .any(|(lo, hi)| *lo <= field && field <= *hi)
        };
        if !include {
            let _ = io.stdout.write_all(text.as_bytes());
            return Some(true);
        }
        // [GNU] leading blanks are skipped before parsing.
        let nb = text.chars().take_while(|c| is_field_blank(*c)).count();
        let number_text = &text[nb..];
        // [GNU] an empty (or blank-only) field is passed through: probed,
        // a blank line in the middle of stdin stays a blank line (rc 0).
        if number_text.is_empty() {
            let _ = io.stdout.write_all(text.as_bytes());
            return Some(true);
        }
        // [GNU] auto-padding reuses the input field width when the number
        // carried leading blanks.
        let mut pad_w = self.padding_width;
        if self.auto_padding {
            pad_w = if nb > 0 { text.len() as i64 } else { 0 };
        }
        let pi = parse_human(number_text, self.from_unit);
        if !pi.ok {
            self.had_invalid = true;
            if self.invalid_policy != "ignore" {
                let _ = writeln!(
                    io.stderr,
                    "numfmt: {}",
                    invalid_message(&pi.err, number_text, &pi.tail)
                );
            }
            if self.invalid_policy == "abort" {
                return None;
            }
            let _ = io.stdout.write_all(text.as_bytes());
            return Some(true);
        }

        let mut val = pi.value;
        if self.from_scale != 1.0 || self.to_scale != 1.0 {
            val = val * self.from_scale / self.to_scale;
        }
        let precision_used = self.fmt_spec.user_precision.unwrap_or(pi.precision).min(18) as usize;

        // [GNU] without scaling, values needing more than 18 digits cannot
        // be printed; a --to scale tops out at 999Q.
        let x = decimal_exponent(val);
        if self.scale_kind == 0 && x + precision_used as i32 > 18 {
            self.had_invalid = true;
            if self.invalid_policy != "ignore" {
                let value_text = g6(val);
                if precision_used > 0 {
                    let _ = writeln!(
                        io.stderr,
                        "numfmt: value/precision too large to be printed: '{value_text}/{}' (consider using --to)",
                        precision_used
                    );
                } else {
                    let _ = writeln!(
                        io.stderr,
                        "numfmt: value too large to be printed: '{value_text}' (consider using --to)"
                    );
                }
            }
            if self.invalid_policy == "abort" {
                return None;
            }
            let _ = io.stdout.write_all(text.as_bytes());
            return Some(true);
        }
        if x > 32 {
            self.had_invalid = true;
            if self.invalid_policy != "ignore" {
                let value_text = g6(val);
                let _ = writeln!(
                    io.stderr,
                    "numfmt: value too large to be printed: '{value_text}' (cannot handle values > 999Q)"
                );
            }
            if self.invalid_policy == "abort" {
                return None;
            }
            let _ = io.stdout.write_all(text.as_bytes());
            return Some(true);
        }

        // [GNU] scale-none integer output keeps the input text exact — but
        // only when the render would be the bare number (no format
        // prefix/suffix, no width, no zero pad, no explicit precision).
        let plain = self.fmt_spec.prefix.is_empty()
            && self.fmt_spec.suffix.is_empty()
            && self.fmt_spec.zero_pad == 0
            && self.fmt_spec.pad_width == 0
            && self.fmt_spec.user_precision.is_none();
        let mut out =
            if plain && self.scale_kind == 0 && self.from_scale == 1.0 && self.to_scale == 1.0 {
                match (&pi.raw_int, precision_used) {
                    (Some(raw), 0) => format!("{raw}{}", self.suffix),
                    _ => self.to_human(val, precision_used),
                }
            } else {
                self.to_human(val, precision_used)
            };
        if pad_w > 0 && (out.len() as i64) < pad_w {
            out.insert_str(0, &" ".repeat((pad_w as usize) - out.len()));
        } else if pad_w < 0 && (out.len() as i64) < -pad_w {
            out.push_str(&" ".repeat((-pad_w as usize) - out.len()));
        }
        let _ = io.stdout.write_all(self.fmt_spec.prefix.as_bytes());
        let _ = io.stdout.write_all(out.as_bytes());
        let _ = io.stdout.write_all(self.fmt_spec.suffix.as_bytes());
        Some(true)
    }

    fn line(&mut self, io: &mut Io, text: &str, print_terminator: bool) -> bool {
        let mut field: u64 = 0;
        let mut pos = 0usize;
        let bytes: Vec<char> = text.chars().collect();
        loop {
            field += 1;
            let fstart = pos;
            let mut fend = pos;
            if !self.delimiter.is_empty() {
                let d = self.delimiter.chars().next().unwrap_or('\0');
                while fend < bytes.len() && bytes[fend] != d {
                    fend += 1;
                }
            } else {
                while fend < bytes.len() && is_field_blank(bytes[fend]) {
                    fend += 1;
                }
                while fend < bytes.len() && !is_field_blank(bytes[fend]) {
                    fend += 1;
                }
            }
            let field_text: String = bytes[fstart..fend.min(bytes.len())].iter().collect();
            if self.field(io, &field_text, field).is_none() {
                return false;
            }
            if fend >= bytes.len() {
                break;
            }
            if self.delimiter.is_empty() {
                let _ = io.stdout.write_all(b" ");
                pos = fend + 1;
            } else {
                let _ = io.stdout.write_all(self.delimiter.as_bytes());
                pos = fend + 1;
            }
        }
        if print_terminator {
            if self.zero_terminated {
                let _ = io.stdout.write_all(b"\0");
            } else {
                let _ = io.stdout.write_all(b"\n");
            }
        }
        true
    }
}

fn process_line_proc(
    proc: &RefCell<Processor<'_>>,
    io: &mut Io,
    line: &str,
    print_terminator: bool,
) -> bool {
    proc.borrow_mut().line(io, line, print_terminator)
}

/// [GNU] --header only applies to standard input: the first N lines are
/// copied verbatim (with their terminator).
fn process_stdin_line_proc(
    proc: &RefCell<Processor<'_>>,
    io: &mut Io,
    line: &str,
    print_terminator: bool,
) -> bool {
    let mut p = proc.borrow_mut();
    if p.header_lines > 0 {
        p.header_lines -= 1;
        let _ = io.stdout.write_all(line.as_bytes());
        if print_terminator {
            if p.zero_terminated {
                let _ = io.stdout.write_all(b"\0");
            } else {
                let _ = io.stdout.write_all(b"\n");
            }
        }
        return true;
    }
    p.line(io, line, print_terminator)
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
        let sc = scratch("numfmt");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::numfmt_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    fn expect(args: &[&str], stdin: &str, want: &str) {
        let (r, out, err) = run(args, stdin);
        assert_eq!(r, 0, "rc {args:?}: {err}");
        assert_eq!(out, want, "out for {args:?}");
    }

    #[test]
    fn from_scales() {
        expect(&["--from=si"], "1K\n", "1000\n");
        expect(&["--from=iec"], "1K\n", "1024\n");
        expect(&["--from=iec-i"], "1Ki\n", "1024\n");
        expect(&["--from=auto"], "1Ki\n", "1024\n");
        expect(&["--from=auto"], "1K\n", "1000\n");
        expect(&["--from=si"], "1M\n", "1000000\n");
        expect(&["--from=si"], "1.5K\n", "1500\n");
        expect(&["--from=si"], "5\n", "5\n");
    }

    #[test]
    fn to_scales() {
        expect(&["--to=si"], "1000\n", "1.0K\n");
        expect(&["--to=si"], "1500\n", "1.5K\n");
        expect(&["--to=si"], "999\n", "999\n");
        expect(&["--to=si"], "999.95\n", "1.0K\n");
        expect(&["--to=si"], "999.5\n", "1.0K\n");
        expect(&["--to=si"], "1000.4\n", "1.1K\n");
        expect(&["--to=iec"], "2048\n", "2.0K\n");
        expect(&["--to=iec"], "1023.99\n", "1.0K\n");
        expect(&["--to=iec-i"], "4096\n", "4.0Ki\n");
        expect(&["--to=iec-i"], "-1024\n", "-1.0Ki\n");
        expect(&["--to=si"], "-1500\n", "-1.5K\n");
        expect(&["--to=si"], "0\n", "0\n");
        expect(&["--to=si"], "0.5\n", "0\n");
        expect(&["--to=si"], "1000000\n", "1.0M\n");
    }

    #[test]
    fn suffix_rules() {
        // a suffix without --from is rejected
        let (r, _, err) = run(&[], "1K\n");
        assert_eq!(r, 2);
        assert!(err.contains("rejecting suffix in input: '1K' (consider using --from)"));
        // lowercase k is not a valid suffix (GNU probed)
        let (r, _, err) = run(&["--from=si"], "1k\n");
        assert_eq!(r, 2);
        assert!(err.contains("invalid suffix in input: '1k'"));
        // trailing blank after the suffix is allowed
        expect(&["--from=si"], "1K ", "1000 ");
    }

    #[test]
    fn parse_errors() {
        for (input, msg) in [
            ("xyz\n", "invalid number: 'xyz'"),
            ("5.\n", "invalid number: '5.'"),
            ("+5\n", "invalid number: '+5'"),
            ("1e3\n", "invalid suffix in input: '1e3'"),
        ] {
            let (r, _, err) = run(&[], input);
            assert_eq!(r, 2, "rc for {input:?}");
            assert!(err.contains(msg), "msg for {input:?}: {err}");
        }
        // missing i for iec-i, tail after a valid suffix
        let (r, _, err) = run(&["--from=iec-i"], "1K\n");
        assert_eq!(r, 2);
        assert!(err.contains("missing 'i' suffix in input: '1K' (e.g Ki/Mi/Gi)"));
        let (r, _, err) = run(&["--from=iec"], "1Ki\n");
        assert_eq!(r, 2);
        assert!(err.contains("invalid suffix in input '1Ki': 'i'"));
        let (r, _, err) = run(&["--from=si"], "1K.\n");
        assert_eq!(r, 2);
        assert!(err.contains("invalid suffix in input '1K.': '.'"));
    }

    #[test]
    fn rounding_modes() {
        expect(&["--format", "%.0f", "--round=nearest"], "1.5\n", "2\n");
        expect(&["--format", "%.0f", "--round=down"], "1.5\n", "1\n");
        expect(&["--format", "%.0f", "--round=up"], "1.5\n", "2\n");
        expect(
            &["--format", "%.0f", "--round=towards-zero"],
            "1.5\n",
            "1\n",
        );
        expect(&["--format", "%.0f", "--round=from-zero"], "1.5\n", "2\n");
        expect(&["--format", "%.0f", "--round=nearest"], "-1.5\n", "-2\n");
        expect(&["--format", "%.0f", "--round=from-zero"], "-1.5\n", "-2\n");
        // default rounding is from-zero
        expect(&["--format", "%.0f"], "1.5\n", "2\n");
    }

    #[test]
    fn format_directives() {
        expect(&["--format", "%10f"], "42\n", "        42\n");
        expect(&["--format", "%-10f"], "42\n", "42        \n");
        expect(&["--format", "%010f"], "42\n", "0000000042\n");
        expect(&["--format", "%.3f"], "42\n", "42.000\n");
        expect(&["--format", "x%fy"], "42\n", "x42y\n");
        expect(&["--format", "%9f"], "42\n", "       42\n");
        expect(&["--format", "%-8.2f"], "3.1\n", "3.10    \n");
        // %.f parses precision 0
        expect(&["--format", "%.f"], "42\n", "42\n");
    }

    #[test]
    fn format_errors() {
        for (fmt, msg) in [
            ("xyz", "format 'xyz' has no % directive"),
            (
                "%s",
                "invalid format '%s', directive must be %[0]['][-][N][.][N]f",
            ),
            ("x%", "format 'x%' ends in %"),
            ("%f %f", "format '%f %f' has too many % directives"),
            ("%%f", "format '%%f' has no % directive"),
        ] {
            let (r, _, err) = run(&["--format", fmt], "42\n");
            assert_eq!(r, 1, "rc for {fmt:?}");
            assert!(err.contains(msg), "msg for {fmt:?}: {err}");
        }
    }

    #[test]
    fn padding_and_autopad() {
        expect(&["--padding=8"], "42\n", "      42\n");
        expect(&["--padding=-8"], "42\n", "42      \n");
        expect(&["--to=si", "--padding=8"], "1000\n", "    1.0K\n");
        expect(&["--to=si", "--padding=3"], "1000\n", "1.0K\n");
        expect(
            &["--to=si", "--suffix=B", "--padding=10"],
            "1000\n",
            "     1.0KB\n",
        );
        // auto-padding keeps the leading blanks' width
        expect(&["--to=si"], "  42  \n", "  42  \n");
        // format width wins over --padding
        expect(&["--format", "%6f", "--padding=3"], "42\n", "    42\n");
    }

    #[test]
    fn fields_and_delimiters() {
        expect(&["--field", "2", "--to=si"], "a 1000 b\n", "a 1.0K b\n");
        expect(
            &["--field", "2-3", "--to=si"],
            "a 1000 2000 c\n",
            "a 1.0K 2.0K c\n",
        );
        expect(&["--field", "-2", "--to=si"], "1000 2000\n", "1.0K 2.0K\n");
        expect(
            &["--field", "2,4", "--to=si"],
            "1 1000 3 2000 5\n",
            "1 1.0K 3 2.0K 5\n",
        );
        expect(&["-d,", "--field", "2", "--to=si"], "a,1000\n", "a,1.0K\n");
        // GNU probed x20: field 3 "b" is inside 2-, is invalid, abort rc 2
        {
            let (r, out, err) = run(&["-d,", "--field", "2-", "--to=si"], "a,1000,b\n");
            assert_eq!(r, 2);
            assert_eq!(out, "a,1.0K,");
            assert!(err.contains("invalid number: 'b'"));
        }
        // a second --field is fatal
        let (r, _, err) = run(&["--field", "1", "--field", "2"], "1000\n");
        assert_eq!(r, 1);
        assert!(err.contains("multiple field specifications"));
        // field spec errors carry the try-help line
        let (r, _, err) = run(&["--field", "0"], "1000\n");
        assert_eq!(r, 1);
        assert!(err.contains("fields are numbered from 1"));
        assert!(err.contains("Try 'numfmt --help' for more information."));
        // delimiter must be a single character
        let (r, _, err) = run(&["-d", "ab"], "a 1000\n");
        assert_eq!(r, 1);
        assert!(err.contains("the delimiter must be a single character"));
    }

    #[test]
    fn invalid_policies() {
        let input = "1000\nxyz\n2000\n";
        let (r, out, _) = run(&["--invalid=warn", "--to=si"], input);
        assert_eq!((r, out.as_str()), (0, "1.0K\nxyz\n2.0K\n"));
        let (r, out, _) = run(&["--invalid=fail", "--to=si"], input);
        assert_eq!((r, out.as_str()), (2, "1.0K\nxyz\n2.0K\n"));
        let (r, out, _) = run(&["--invalid=ignore", "--to=si"], input);
        assert_eq!((r, out.as_str()), (0, "1.0K\nxyz\n2.0K\n"));
        let (r, out, _) = run(&["--to=si"], input);
        assert_eq!((r, out.as_str()), (2, "1.0K\n"));
    }

    #[test]
    fn headers() {
        expect(&["--header", "--to=iec"], "size\n1024\n", "size\n1.0K\n");
        expect(&["--header=2", "--to=iec"], "a\nb\n1024\n", "a\nb\n1.0K\n");
        // header only applies to stdin; args are all data
        expect(&["--header", "--to=iec", "1024"], "", "1.0K\n");
        let (r, _, err) = run(&["--header=0", "--to=si"], "1000\n");
        assert_eq!(r, 1);
        assert!(err.contains("invalid header value '0'"));
    }

    #[test]
    fn argmatch_and_option_errors() {
        for (args, msg) in [
            (vec!["--to=auto"], "invalid argument 'auto' for '--to'"),
            (vec!["--from=bad"], "invalid argument 'bad' for '--from'"),
            (vec!["--round=bad"], "invalid argument 'bad' for '--round'"),
            (
                vec!["--invalid=bad"],
                "invalid argument 'bad' for '--invalid'",
            ),
        ] {
            let (r, _, err) = run(&args, "1000\n");
            assert_eq!(r, 1, "rc for {args:?}");
            assert!(err.contains(msg), "msg for {args:?}: {err}");
            assert!(err.contains("Valid arguments are:"));
            assert!(err.contains("Try 'numfmt --help' for more information."));
        }
        // --grouping conflicts with --format and with --to
        let (r, _, err) = run(&["--grouping", "--format", "%f"], "1000\n");
        assert_eq!(r, 1);
        assert!(err.contains("--grouping cannot be combined with --format"));
        let (r, _, err) = run(&["--grouping", "--to=si"], "1000\n");
        assert_eq!(r, 1);
        assert!(err.contains("grouping cannot be combined with --to"));
        // --padding=0
        let (r, _, err) = run(&["--padding=0"], "42\n");
        assert_eq!(r, 1);
        assert!(err.contains("invalid padding value '0'"));
    }

    #[test]
    fn unit_sizes() {
        expect(&["--from-unit", "1024"], "2\n", "2048\n");
        expect(&["--from-unit", "K"], "2\n", "2000\n");
        expect(&["--from-unit", "Ki"], "2\n", "2048\n");
        expect(&["--to-unit", "Ki"], "4096\n", "4\n");
        expect(&["--from-unit", "Ki", "--to=iec"], "4\n", "4.0K\n");
        for bad in ["xyz", "Z", "k", "0"] {
            let (r, _, err) = run(&["--from-unit", bad], "2\n");
            assert_eq!(r, 1, "rc for {bad:?}");
            assert!(err.contains(&format!("invalid unit size: '{bad}'")));
        }
    }

    #[test]
    fn zero_terminated_and_terminators() {
        let (r, out, _) = run(&["-z", "--to=si"], "1000\02000\0");
        assert_eq!(r, 0);
        assert_eq!(out, "1.0K\02.0K\0");
        // an unterminated final line keeps no terminator
        expect(&["--to=si"], "1000", "1.0K");
        expect(&["--to=si"], "1000\n", "1.0K\n");
        // a blank line in the middle stays a blank line
        expect(&["--to=si"], "1000\n\n2000\n", "1.0K\n\n2.0K\n");
    }

    #[test]
    fn large_values() {
        // 19 digits without scaling still prints
        expect(&[], "999999999999999999\n", "999999999999999999\n");
        // 21 digits is beyond the print limit
        let (r, _, err) = run(&[], "999999999999999999999\n");
        assert_eq!(r, 2);
        assert!(err.contains("value too large to be printed: '1e+21' (consider using --to)"));
        // 34 digits overflow the parse
        let (r, _, err) = run(&[], "1234567890123456789012345678901234\n");
        assert_eq!(r, 2);
        assert!(err.contains("value too large to be converted"));
    }

    #[test]
    fn args_mode() {
        expect(&["--to=si", "1000", "2000"], "", "1.0K\n2.0K\n");
        // header options do not apply to args
        expect(&["--header", "--to=iec", "1024"], "", "1.0K\n");
        // abort stops at the first bad arg
        let (r, out, _) = run(&["--to=si", "1000", "xyz", "2000"], "");
        assert_eq!(r, 2);
        assert_eq!(out, "1.0K\n");
    }

    #[test]
    fn grouping_is_locale_driven() {
        // the C-locale default groups nothing, even with --grouping
        expect(&["--grouping"], "1234567\n", "1234567\n");
        expect(&["--format", "%'f"], "1234567\n", "1234567\n");
    }

    #[test]
    fn debug_warning() {
        let (r, out, err) = run(&["--debug"], "1000\n");
        assert_eq!(r, 0);
        assert_eq!(out, "1000\n");
        assert!(err.contains("no conversion option specified"));
        // with a conversion option the warning is gone
        let (r, _, err) = run(&["--debug", "--to=si"], "1000\n");
        assert_eq!(r, 0);
        assert!(!err.contains("no conversion option specified"));
    }
}
