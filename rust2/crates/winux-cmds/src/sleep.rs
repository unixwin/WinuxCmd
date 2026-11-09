//! `sleep` — GNU coreutils semantics (probed against coreutils 9.4; the
//! duration grammar mirrors sleep.c's strtod-based parse, which the 1.x
//! C++ applet already models on main).
//!
//! * Arguments are summed. Each is NUMBER[suffix], suffix one of `s m h d`
//!   (case-sensitive; `0.001S` is invalid). The number is a strtod subject:
//!   decimal or hexadecimal float, leading '+', leading blanks, and the
//!   strings inf/infinity (positive infinity — an unbounded wait); 'nan'
//!   and negative values are invalid.
//! * EVERY invalid operand is reported (`invalid time interval 'X'` +
//!   try-help on stderr), rc 1, and nothing is slept.
//! * Zero operands: `sleep: missing operand` rc 1 — but `sleep --` HAS an
//!   argument (the terminator), sleeps 0 and exits 0 (sleep.c's argc == 1
//!   test on the raw argv, mirrored from the C++ applet).
//! * Unknown options are GNU rc 1; `-1` is an invalid option like GNU's
//!   getopt (`sleep -1` -> `invalid option -- '1'`).
//!
//! The sleep itself happens through std::thread::sleep rounded to whole
//! milliseconds; gate cases use only sub-millisecond durations.

use std::ffi::OsString;
use std::time::Duration;

use crate::argopt::{self, OptSpec};
use winux_core::{usage_error, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[];

/// Parse one GNU sleep operand into seconds. Returns None on the GNU
/// "invalid time interval" cases.
pub fn parse_duration(raw: &str) -> Option<f64> {
    let s = raw.trim_start_matches([' ', '\t', '\n', '\u{0b}', '\u{0c}', '\r']);
    if s.is_empty() {
        return None;
    }
    let (num_part, multiplier) =
        if s.len() > 1 && matches!(s.as_bytes()[s.len() - 1], b's' | b'm' | b'h' | b'd') {
            let mult = match s.as_bytes()[s.len() - 1] {
                b's' => 1.0,
                b'm' => 60.0,
                b'h' => 3600.0,
                _ => 86400.0,
            };
            (&s[..s.len() - 1], mult)
        } else {
            (s, 1.0)
        };
    let value = strtod(num_part)?;
    if value.is_nan() || value < 0.0 {
        return None;
    }
    Some(value * multiplier)
}

/// The strtod subject grammar GNU relies on: decimal and hexadecimal
/// floats (glibc accepts hex without a binary exponent), inf/infinity,
/// nan; the whole string must be consumed.
fn strtod(s: &str) -> Option<f64> {
    let bytes = s.as_bytes();
    let mut i = 0usize;
    // sign
    if i < bytes.len() && (bytes[i] == b'+' || bytes[i] == b'-') {
        i += 1;
    }
    let rest = &s[i..];
    let lower = rest.to_ascii_lowercase();
    if let Some(inf) = lower
        .strip_prefix("infinity")
        .or_else(|| lower.strip_prefix("inf"))
    {
        if inf.is_empty() {
            return Some(if bytes.first() == Some(&b'-') {
                f64::NEG_INFINITY
            } else {
                f64::INFINITY
            });
        }
        return None;
    }
    if let Some(nan_rest) = lower.strip_prefix("nan") {
        if nan_rest.is_empty() || nan_rest.starts_with('(') {
            return Some(f64::NAN);
        }
        return None;
    }
    // hexadecimal float
    if lower.starts_with("0x") {
        let hex = &rest[2..];
        let (mantissa, exp) = match hex.find(['p', 'P']) {
            Some(pos) => (&hex[..pos], Some(&hex[pos + 1..])),
            None => (hex, None),
        };
        let (int_part, frac_part) = match mantissa.find('.') {
            Some(pos) => (&mantissa[..pos], &mantissa[pos + 1..]),
            None => (mantissa, ""),
        };
        if int_part.is_empty() && frac_part.is_empty() {
            return None;
        }
        if !int_part.bytes().all(|b| b.is_ascii_hexdigit())
            || !frac_part.bytes().all(|b| b.is_ascii_hexdigit())
        {
            return None;
        }
        let mut value = if int_part.is_empty() {
            0.0f64
        } else {
            u64::from_str_radix(int_part, 16).ok()? as f64
        };
        if !frac_part.is_empty() {
            let mut scale = 1.0 / 16.0;
            for c in frac_part.bytes() {
                let digit = (c as char).to_digit(16).unwrap() as f64;
                value += digit * scale;
                scale /= 16.0;
            }
        }
        if let Some(exp) = exp {
            let (esign, ebody) = if exp.starts_with('-') {
                (-1i64, &exp[1..])
            } else if exp.starts_with('+') {
                (1i64, &exp[1..])
            } else {
                (1i64, exp)
            };
            if ebody.is_empty() || !ebody.bytes().all(|b| b.is_ascii_digit()) {
                return None;
            }
            value *= 2f64.powi((esign * ebody.parse::<i64>().ok()?) as i32);
        }
        return Some(if bytes.first() == Some(&b'-') {
            -value
        } else {
            value
        });
    }
    // decimal float: digits with optional '.' and optional exponent
    let (mantissa, exp) = match rest.find(['e', 'E']) {
        Some(pos) => (&rest[..pos], Some(&rest[pos + 1..])),
        None => (rest, None),
    };
    let (int_part, frac_part) = match mantissa.find('.') {
        Some(pos) => (&mantissa[..pos], &mantissa[pos + 1..]),
        None => (mantissa, ""),
    };
    if int_part.is_empty() && frac_part.is_empty() {
        return None;
    }
    if !int_part.bytes().all(|b| b.is_ascii_digit())
        || !frac_part.bytes().all(|b| b.is_ascii_digit())
    {
        return None;
    }
    let mut value: f64 = format!("{int_part}.{frac_part}").parse().ok()?;
    if let Some(exp) = exp {
        let (esign, ebody) = if exp.starts_with('-') {
            (-1i64, &exp[1..])
        } else if exp.starts_with('+') {
            (1i64, &exp[1..])
        } else {
            (1i64, exp)
        };
        if ebody.is_empty() || !ebody.bytes().all(|b| b.is_ascii_digit()) {
            return None;
        }
        value *= 10f64.powi((esign * ebody.parse::<i64>().ok()?) as i32);
    }
    Some(if bytes.first() == Some(&b'-') {
        -value
    } else {
        value
    })
}

pub fn sleep_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("sleep", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if p.has("help") || p.has("version") {
        return Ok(0);
    }
    // [GNU] sleep.c:112-115: the missing-operand test is argc == 1 on the
    // raw argument vector; `sleep --` sleeps 0 and exits 0.
    if p.positionals.is_empty() && args.is_empty() {
        let _ = usage_error(io, "sleep", "missing operand");
        return Ok(1);
    }

    let mut total = 0.0f64;
    let mut invalid = Vec::new();
    for arg in &p.positionals {
        let raw = arg.to_string_lossy().into_owned();
        match parse_duration(&raw) {
            Some(v) => total += v,
            None => invalid.push(raw),
        }
    }
    if !invalid.is_empty() {
        // GNU reports every invalid operand, then the try-help, rc 1.
        for raw in &invalid {
            let _ = writeln!(io.stderr, "sleep: invalid time interval '{raw}'");
        }
        let _ = writeln!(io.stderr, "Try 'sleep --help' for more information.");
        return Ok(1);
    }

    // [GNU] inf/infinity (and any overflow-scale interval) is an unbounded
    // wait. Duration::MAX is the std-side equivalent of Sleep(INFINITE).
    if total.is_infinite() || total >= (i64::MAX as f64) / 1000.0 {
        std::thread::sleep(Duration::MAX);
        return Ok(0);
    }

    // Round to the nearest millisecond like the C++ applet (GNU hands the
    // full double to xnanosleep; sub-millisecond granularity is a platform
    // limit documented in the module header).
    let ms = (total * 1000.0).round().max(0.0);
    if ms > 0.0 {
        std::thread::sleep(Duration::from_millis(ms as u64));
    }
    Ok(0)
}

#[cfg(test)]
mod tests {
    use std::ffi::OsString;
    use winux_core::test_support::{MemIo, ScratchCtx};

    use super::parse_duration;

    fn run(args: &[&str]) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("sleep");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::sleep_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn duration_grammar() {
        assert_eq!(parse_duration("0"), Some(0.0));
        assert_eq!(parse_duration("0.5"), Some(0.5));
        assert_eq!(parse_duration(".5"), Some(0.5));
        assert_eq!(parse_duration("+1"), Some(1.0));
        assert_eq!(parse_duration("1e-1"), Some(0.1));
        assert_eq!(parse_duration("0x10"), Some(16.0)); // glibc hex without p
        assert_eq!(parse_duration("0x1p-3"), Some(0.125));
        assert_eq!(parse_duration("2s"), Some(2.0));
        assert_eq!(parse_duration("0.001S"), None); // suffix is case-sensitive
        assert_eq!(parse_duration("1m"), Some(60.0));
        assert_eq!(parse_duration("1h"), Some(3600.0));
        assert_eq!(parse_duration("1d"), Some(86400.0));
        assert_eq!(parse_duration("1m1"), None);
        assert_eq!(parse_duration("1x"), None);
        assert_eq!(parse_duration("0.5x"), None);
        assert_eq!(parse_duration("x"), None);
        assert_eq!(parse_duration(""), None);
        assert_eq!(parse_duration("NaN"), None);
        assert_eq!(parse_duration("inf"), Some(f64::INFINITY));
        assert_eq!(parse_duration("infinity"), Some(f64::INFINITY));
        assert_eq!(parse_duration("-0.5"), None); // negative: invalid interval
        assert_eq!(parse_duration(" 1 "), None); // trailing blank breaks strtod
    }

    #[test]
    fn trailing_blanks_rejected_like_gnu() {
        // strtod ignores LEADING blanks only; "1 " does not consume the
        // trailing space so GNU's `*p` check rejects it.
        assert_eq!(parse_duration("1 "), None);
    }

    #[test]
    fn zero_operands_vs_terminator() {
        let (r, _, err) = run(&[]);
        assert_eq!(r, 1);
        assert!(err.contains("missing operand"));
        // `sleep --` has an argument (the terminator): sleeps 0, rc 0.
        let (r, _, _) = run(&["--"]);
        assert_eq!(r, 0);
    }

    #[test]
    fn invalid_intervals_reported_all() {
        let (r, _, err) = run(&["xx", "yy"]);
        assert_eq!(r, 1);
        assert!(err.contains("invalid time interval 'xx'"));
        assert!(err.contains("invalid time interval 'yy'"));
        assert!(err.contains("Try 'sleep --help'"));
        // empty operand is an operand -> invalid interval, not missing
        let (r, _, err) = run(&[""]);
        assert_eq!(r, 1);
        assert!(err.contains("invalid time interval ''"));
    }

    #[test]
    fn fast_sleep_ok() {
        let (r, _, _) = run(&["0"]);
        assert_eq!(r, 0);
        let (r, _, _) = run(&["0.001"]);
        assert_eq!(r, 0);
        let (r, _, _) = run(&["1e-3", "0", "0s"]);
        assert_eq!(r, 0);
        let (r, _, _) = run(&["--", "0.001"]);
        assert_eq!(r, 0);
        let (r, _, _) = run(&["--bogus"]);
        assert_eq!(r, 1);
        let (r, _, _) = run(&["-1"]);
        assert_eq!(r, 1); // getopt: invalid option -- '1'
    }
}
