//! `head` — GNU coreutils head.c (9.4) semantics, probed byte-for-byte
//! against WSL coreutils 9.4 (rust2/target/probe/p-head.sh, o-head.txt).
//!
//! Count argument grammar (head.c's `xdectoumax`-shaped parse): optional
//! leading `+` (same as bare K: head -n +2 == head -n 2, probed) or `-`
//! (elide-from-end: all but the last K records), digits, then an optional
//! multiplier suffix (b 512, kB 1000, K 1024, MB/M 1024^2, ... R/Q; the
//! probed `-n 1b` / `-c 1b` rows cover the b suffix). `-n -0` / `-c -0`
//! print everything (all but last 0, probed). A malformed count dies with
//! `invalid number of lines|bytes: '<arg>'` (curly quotes, rc 1).
//!
//! Records are lines, or NUL-delimited chunks with -z. Inputs are read
//! whole (the -K form needs the tail anyway); files are processed
//! sequentially with the header written before each (probed: first header
//! has no leading blank, later headers are preceded by a blank line;
//! `-` prints `standard input`). Headers appear when more than one input
//! is read or -v is given; -q never prints them.
//!
//! Errors (rc 1, later files still processed): a failed open reports
//! `cannot open 'X' for reading: <reason>` (straight quotes, probed); a
//! directory operand opens on POSIX and dies at the read, which Windows
//! recovers from a metadata probe (`error reading 'X': Is a directory`).

use std::ffi::OsString;
use std::io::{Read, Write};

use crate::argopt::{self, OptSpec};
use crate::fsutil::{gnu_open_reason, resolve};
use crate::gnuhelp;
use winux_core::{usage_error, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('c'),
        long: "bytes",
        takes_value: true,
    },
    OptSpec {
        short: Some('n'),
        long: "lines",
        takes_value: true,
    },
    OptSpec {
        short: Some('q'),
        long: "quiet",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "silent",
        takes_value: false,
    },
    OptSpec {
        short: Some('v'),
        long: "verbose",
        takes_value: false,
    },
    OptSpec {
        short: Some('z'),
        long: "zero-terminated",
        takes_value: false,
    },
];

/// The parsed `[+-]NUM[suffix]` count argument.
#[derive(Debug, PartialEq)]
pub(crate) enum Count {
    /// print the first K records
    First(u128),
    /// print all but the last K records
    Elide(u128),
}

/// C-locale `isspace` (includes `\v`, which `u8::is_ascii_whitespace`
/// omits) — xstrtol skips it before the sign check and strtoumax skips
/// it again before the digits.
fn is_c_space(b: u8) -> bool {
    matches!(b, b' ' | b'\t' | b'\n' | 0x0b | 0x0c | b'\r')
}

/// The pieces of one `__xstrtol` run (base 10) a caller needs to pick
/// GNU's diagnostic. `value` saturates at u64::MAX like strtoumax /
/// bkm_scale do; it is meaningful only when no error bit is set.
#[derive(Debug, Default)]
pub(crate) struct Xnum {
    pub value: u128,
    /// strtoumax reported ERANGE on the digit run.
    pub mantissa_range: bool,
    /// A suffix scale overflowed (bkm_scale).
    pub scale_overflow: bool,
    /// LONGINT_INVALID: '-' after blanks, or no digits and the first
    /// byte is not a valid suffix char.
    pub invalid: bool,
    /// LONGINT_INVALID_SUFFIX_CHAR: bad suffix char or trailing junk.
    pub bad_suffix: bool,
}

/// `__xstrtol(nptr, endptr=NULL, base=10, &val, valid_suffixes)` — the
/// lib/xstrtol.c grammar both head.c's `string_to_integer` and tail.c's
/// `xdectoumax` counts share. `valid_suffixes` is the caller's suffix
/// set; the '0' member doubles as the flag enabling two-character
/// suffixes (`kB`=1000, `kiB`=1024, `kD`=1000-obsolescent).
pub(crate) fn xnum(s: &str, valid_suffixes: &str) -> Xnum {
    let b = s.as_bytes();
    let suf = valid_suffixes.as_bytes();
    let mut x = Xnum::default();

    // Unsigned types: first non-blank '-' is LONGINT_INVALID up front.
    let mut j = 0;
    while j < b.len() && is_c_space(b[j]) {
        j += 1;
    }
    if j < b.len() && b[j] == b'-' {
        x.invalid = true;
        return x;
    }

    // strtoumax: leading blanks, optional '+', decimal digits.
    let mut i = 0;
    while i < b.len() && is_c_space(b[i]) {
        i += 1;
    }
    if i < b.len() && b[i] == b'+' {
        i += 1;
    }
    let digits_start = i;
    let mut val: u128 = 0;
    while i < b.len() && b[i].is_ascii_digit() {
        let d = (b[i] - b'0') as u128;
        if val > (u64::MAX as u128 - d) / 10 {
            x.mantissa_range = true;
            val = u64::MAX as u128;
        } else {
            val = val * 10 + d;
        }
        i += 1;
    }

    if i == digits_start {
        // No conversion: endptr == nptr. The string is a valid suffix
        // char at face value ("K" == 1K) or LONGINT_INVALID.
        match b.first() {
            Some(&c) if suf.contains(&c) => {
                val = 1;
                i = 0; // *p == nptr: the suffix scan starts at byte 0
            }
            _ => {
                x.invalid = true;
                return x;
            }
        }
    }

    if i >= b.len() {
        x.value = val;
        return x;
    }

    let c = b[i];
    if !suf.contains(&c) {
        // strtoumax already stored the mantissa; a caller that masks
        // LONGINT_INVALID_SUFFIX_CHAR (tail's obsolete xstrtoumax) still
        // uses it.
        x.value = val;
        x.bad_suffix = true;
        return x;
    }

    // Optional second suffix char, only for the "0"-flagged tables.
    let mut xbase: u128 = 1024;
    let mut consume = 1usize;
    if suf.contains(&b'0')
        && matches!(
            c,
            b'E' | b'G'
                | b'g'
                | b'k'
                | b'K'
                | b'M'
                | b'm'
                | b'P'
                | b'Q'
                | b'R'
                | b'T'
                | b't'
                | b'Y'
                | b'Z'
        )
    {
        match (b.get(i + 1), b.get(i + 2)) {
            (Some(b'i'), Some(b'B')) => consume = 3,
            (Some(b'B'), _) | (Some(b'D'), _) => {
                xbase = 1000;
                consume = 2;
            }
            _ => {}
        }
    }

    let scale = match c {
        b'b' => Some(512u128),
        b'k' | b'K' => xbase.checked_pow(1),
        b'm' | b'M' => xbase.checked_pow(2),
        b'g' | b'G' => xbase.checked_pow(3),
        b't' | b'T' => xbase.checked_pow(4),
        b'P' => xbase.checked_pow(5),
        b'E' => xbase.checked_pow(6),
        b'Z' => xbase.checked_pow(7),
        b'Y' => xbase.checked_pow(8),
        b'R' => xbase.checked_pow(9),
        b'Q' => xbase.checked_pow(10),
        _ => None,
    };
    // bkm_scale overflows against UINTMAX_MAX (u64), not u128.
    match scale.and_then(|sc| val.checked_mul(sc)) {
        Some(v) if v <= u64::MAX as u128 => x.value = v,
        _ => {
            x.scale_overflow = true;
            x.value = u64::MAX as u128;
        }
    }
    i += consume;
    if i < b.len() {
        x.bad_suffix = true;
    }
    x
}

/// head.c/tail.c count suffix table (`"bkKmMGTPEZYRQ0"` in both).
const COUNT_SUFFIXES: &str = "bkKmMGTPEZYRQ0";

/// Why a count string failed GNU's count parse.
#[derive(Debug, PartialEq)]
pub(crate) enum CountVal {
    Invalid,
    /// LONGINT_OVERFLOW with errno=EOVERFLOW — the diagnostic gains
    /// ": Value too large for defined data type".
    Overflow,
}

/// `xdectoumax(s, 0, UINTMAX_MAX, "bkKmMGTPEZYRQ0", err, 0)` outcome:
/// Invalid -> clean "invalid number of X: 's'"; Overflow -> same plus
/// the EOVERFLOW suffix; Ok -> the saturated value.
pub(crate) fn xdectoumax_count(s: &str) -> Result<u128, CountVal> {
    let x = xnum(s, COUNT_SUFFIXES);
    if x.invalid || x.bad_suffix {
        Err(CountVal::Invalid)
    } else if x.mantissa_range || x.scale_overflow {
        Err(CountVal::Overflow)
    } else {
        Ok(x.value)
    }
}

/// End offset of every record: lines end after each `delim` byte; byte
/// mode (-c) counts every byte as its own record.
pub(crate) fn record_bounds(input: &[u8], delim: u8, bytes_mode: bool) -> Vec<usize> {
    let mut bounds = Vec::new();
    if bytes_mode {
        bounds.extend(1..=input.len());
    } else {
        for (i, &b) in input.iter().enumerate() {
            if b == delim {
                bounds.push(i + 1);
            }
        }
    }
    bounds
}

/// Emit `all but the last k` records.
fn emit_elide(
    input: &[u8],
    k: u128,
    delim: u8,
    bytes_mode: bool,
    out: &mut dyn Write,
) -> std::io::Result<()> {
    if k == 0 {
        return out.write_all(input);
    }
    let bounds = record_bounds(input, delim, bytes_mode);
    let count = bounds.len() as u128;
    if count <= k {
        return Ok(()); // everything is elided
    }
    let end = bounds[(count - k - 1) as usize];
    out.write_all(&input[..end])
}

/// Emit the first `k` records. `k == 0` prints nothing.
fn emit_first(
    input: &[u8],
    k: u128,
    delim: u8,
    bytes_mode: bool,
    out: &mut dyn Write,
) -> std::io::Result<()> {
    if k == 0 {
        return Ok(());
    }
    let bounds = record_bounds(input, delim, bytes_mode);
    let count = bounds.len() as u128;
    if count < k {
        return out.write_all(input);
    }
    let end = bounds[(k - 1) as usize];
    out.write_all(&input[..end])
}

// `--help` body verbatim from `env head --help` under WSL coreutils 9.4,
// minus the shared gnuhelp::footer tail.
const HEAD_HELP_BODY: &str = r#"Usage: head [OPTION]... [FILE]...
Print the first 10 lines of each FILE to standard output.
With more than one FILE, precede each with a header giving the file name.

With no FILE, or when FILE is -, read standard input.

Mandatory arguments to long options are mandatory for short options too.
  -c, --bytes=[-]NUM       print the first NUM bytes of each file;
                             with the leading '-', print all but the last
                             NUM bytes of each file
  -n, --lines=[-]NUM       print the first NUM lines instead of the first 10;
                             with the leading '-', print all but the last
                             NUM lines of each file
  -q, --quiet, --silent    never print headers giving file names
  -v, --verbose            always print headers giving file names
  -z, --zero-terminated    line delimiter is NUL, not newline
      --help        display this help and exit
      --version     output version information and exit

NUM may have a multiplier suffix:
b 512, kB 1000, K 1024, MB 1000*1000, M 1024*1024,
GB 1000*1000*1000, G 1024*1024*1024, and so on for T, P, E, Z, Y, R, Q.
Binary prefixes can be used, too: KiB=K, MiB=M, and so on.

"#;

/// GNU header_mode (head.c/tail.c enum): multiple_files is the default —
/// headers appear only with more than one operand. -q/-v assign it, so
/// the LAST of quiet/verbose wins.
#[derive(Debug, Clone, Copy, PartialEq)]
pub(crate) enum HeaderMode {
    Multiple,
    Always,
    Never,
}

/// head.c's "invalid number of lines|bytes" diagnostic; `post` is the
/// count string as xdectoumax saw it (after head's own '-' elide strip).
fn invalid_count(io: &mut Io, word: &str, post: &str, e: &CountVal) {
    let errno = match e {
        CountVal::Overflow => ": Value too large for defined data type",
        CountVal::Invalid => "",
    };
    let _ = writeln!(
        io.stderr,
        "head: invalid number of {word}: ‘{post}’{errno}"
    );
}

pub fn head_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    // head.c main: the obsolescent `-NUM[clbkmqvz]*` form is recognized
    // ONLY as argv[1], before getopt runs. Each appended letter ASSIGNS
    // (c/b/k/m -> bytes with b/k/m leaving a multiplier char, l -> lines,
    // q/v -> header mode, z -> NUL delimiter); any other letter is
    // "invalid trailing option -- X" + usage(1). A later getopt -c/-n
    // then overwrites count_lines/elide/n_units wholesale.
    let mut rest: &[OsString] = args;
    let mut n_units: u128 = 10; // DEFAULT_NUMBER
    let mut count_lines = true;
    let mut elide_from_end = false;
    let mut header_mode = HeaderMode::Multiple;
    let mut line_end_nul = false;
    if let Some(first) = args.first() {
        let s = first.to_string_lossy();
        let b = s.as_bytes();
        if b.len() >= 2 && b[0] == b'-' && b[1].is_ascii_digit() {
            let mut i = 1;
            while i < b.len() && b[i].is_ascii_digit() {
                i += 1;
            }
            let digits = &s[1..i];
            let mut mult_char = 0u8;
            for &c in &b[i..] {
                match c {
                    b'c' => {
                        count_lines = false;
                        mult_char = 0;
                    }
                    b'b' | b'k' | b'm' => {
                        count_lines = false;
                        mult_char = c;
                    }
                    b'l' => count_lines = true,
                    b'q' => header_mode = HeaderMode::Never,
                    b'v' => header_mode = HeaderMode::Always,
                    b'z' => line_end_nul = true,
                    _ => {
                        let _ = usage_error(
                            ctx,
                            io,
                            "head",
                            &format!("invalid trailing option -- {}", c as char),
                        );
                        return Ok(1);
                    }
                }
            }
            // *end_n_string = multiplier_char: the digit string gains the
            // LAST b/k/m seen (0 after a 'c'), then NUL — "-5bl" parses as
            // "5b" in lines mode, "-5bc" as plain "5" bytes.
            let n_string = if mult_char != 0 {
                format!("{digits}{}", mult_char as char)
            } else {
                digits.to_string()
            };
            match xdectoumax_count(&n_string) {
                Ok(v) => n_units = v,
                Err(e) => {
                    invalid_count(io, if count_lines { "lines" } else { "bytes" }, &n_string, &e);
                    return Ok(1);
                }
            }
            rest = &args[1..];
        }
    }

    // getopt_long's optstring carries '0'..'9': a -NUM anywhere but
    // argv[1] reaches main's default case -> "invalid trailing option --
    // N" + usage(1), in argv order (the argopt hook below).
    let digit_hook = |c: char, io: &mut Io, ctx: &Ctx, prog: &str| -> Option<u32> {
        if c.is_ascii_digit() {
            let _ = usage_error(ctx, io, prog, &format!("invalid trailing option -- {c}"));
            Some(1)
        } else {
            None
        }
    };
    // main() validates each -c/-n optarg as getopt returns it — BEFORE
    // any later token's parse-level diagnostic. The ValueHook is that
    // point; the events pass below only assigns (its error arms are
    // unreachable once the hook has run).
    let count_hook = |key: &str, v: &str, io: &mut Io, _ctx: &Ctx, _prog: &str| -> Option<u32> {
        if key != "bytes" && key != "lines" {
            return None;
        }
        let lines = key == "lines";
        let post = if v.as_bytes().first() == Some(&b'-') {
            &v[1..]
        } else {
            v
        };
        match xdectoumax_count(post) {
            Ok(_) => None,
            Err(e) => {
                invalid_count(io, if lines { "lines" } else { "bytes" }, post, &e);
                Some(1)
            }
        }
    };
    let p = match argopt::parse_ex(
        "head",
        rest,
        OPTS,
        io,
        ctx,
        1,
        Some(&digit_hook),
        Some(&count_hook),
    ) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };

    // getopt returns one option per call and main validates each -c/-n
    // value as it arrives: every occurrence ASSIGNS count_lines,
    // elide_from_end and n_units (there is no sticky from_start in head),
    // q/v assign header_mode the same way, and the first invalid count
    // exits before later options — including --help — are seen.
    for ev in &p.events {
        match ev.key.as_str() {
            argopt::HELP | argopt::VERSION => break,
            "bytes" | "lines" => {
                let lines = ev.key == "lines";
                let v = ev.value.as_deref().unwrap_or("");
                // elide_from_end = (*optarg == '-'); ++optarg on elide —
                // the diagnostic quotes the post-strip string.
                let (post, elide) = if v.as_bytes().first() == Some(&b'-') {
                    (&v[1..], true)
                } else {
                    (v, false)
                };
                match xdectoumax_count(post) {
                    Ok(n) => {
                        count_lines = lines;
                        elide_from_end = elide;
                        n_units = n;
                    }
                    Err(e) => {
                        invalid_count(io, if lines { "lines" } else { "bytes" }, post, &e);
                        return Ok(1);
                    }
                }
            }
            "quiet" | "silent" => header_mode = HeaderMode::Never,
            "verbose" => header_mode = HeaderMode::Always,
            _ => {}
        }
    }
    if p.has("zero-terminated") {
        line_end_nul = true;
    }
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
                    HEAD_HELP_BODY,
                    gnuhelp::footer("head", "head invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("head", "David MacKenzie and Jim Meyering")
                )?;
                0
            }
        });
    }

    let count = if elide_from_end {
        Count::Elide(n_units)
    } else {
        Count::First(n_units)
    };
    let bytes_mode = !count_lines;
    let delim: u8 = if line_end_nul { 0 } else { b'\n' };

    let files: Vec<String> = if p.positionals.is_empty() {
        vec!["-".to_string()]
    } else {
        p.positionals
            .iter()
            .map(|a| a.to_string_lossy().into_owned())
            .collect()
    };
    let print_headers = match header_mode {
        HeaderMode::Always => true,
        HeaderMode::Never => false,
        HeaderMode::Multiple => files.len() > 1,
    };

    let mut first_header = true;
    let mut failed = false;
    for file in &files {
        let bytes = if file == "-" {
            let mut buf = Vec::new();
            match io.stdin.read_to_end(&mut buf) {
                Ok(_) => buf,
                Err(_) => {
                    let _ = writeln!(
                        io.stderr,
                        "head: error reading 'standard input': Is a directory"
                    );
                    failed = true;
                    continue;
                }
            }
        } else {
            match crate::fsutil::open_read_operand(ctx, file) {
                Ok(mut f) => {
                    let mut buf = Vec::new();
                    match f.read_to_end(&mut buf) {
                        Ok(_) => buf,
                        Err(_) => {
                            // POSIX head opens the operand then reads it: a
                            // directory dies at the read (probed via stdin;
                            // Windows fails the open, so probe metadata)
                            let reason = if std::fs::metadata(resolve(ctx, file))
                                .map(|m| m.is_dir())
                                .unwrap_or(false)
                            {
                                "Is a directory".to_string()
                            } else {
                                "read error".to_string()
                            };
                            let _ = writeln!(io.stderr, "head: error reading '{file}': {reason}");
                            failed = true;
                            continue;
                        }
                    }
                }
                Err(e) => {
                    if std::fs::metadata(resolve(ctx, file))
                        .map(|m| m.is_dir())
                        .unwrap_or(false)
                    {
                        // POSIX opens the directory then dies at the read
                        let _ = writeln!(io.stderr, "head: error reading '{file}': Is a directory");
                        failed = true;
                        continue;
                    }
                    let reason = gnu_open_reason(ctx, file, &e, false);
                    let _ = writeln!(
                        io.stderr,
                        "head: cannot open '{file}' for reading: {reason}"
                    );
                    failed = true;
                    continue;
                }
            }
        };
        if print_headers {
            if first_header {
                first_header = false;
            } else {
                let _ = write!(io.stdout, "\n");
            }
            let name = if file == "-" {
                "standard input"
            } else {
                file.as_str()
            };
            let _ = writeln!(io.stdout, "==> {name} <==");
        }
        let r = match count {
            Count::First(k) => emit_first(&bytes, k, delim, bytes_mode, io.stdout),
            Count::Elide(k) => emit_elide(&bytes, k, delim, bytes_mode, io.stdout),
        };
        if r.is_err() {
            let _ = writeln!(io.stderr, "head: write error");
            failed = true;
        }
    }
    Ok(if failed { 1 } else { 0 })
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str], stdin: &str) -> (u32, String, String) {
        let sc = ScratchCtx::new("head");
        let mut io = MemIo::new(stdin);
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = head_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn gnu_help_version_surface() {
        let (r, out, err) = run(&["--help"], "");
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.starts_with("Usage: head [OPTION]... [FILE]...\n"));
        assert!(out.contains("  -c, --bytes=[-]NUM"));
        assert!(out.ends_with("info '(coreutils) head invocation'\n"));
        let (r, out, _) = run(&["--version"], "");
        assert_eq!(r, 0);
        assert!(out.starts_with("head (GNU coreutils) 9.4\n"));
        assert!(out.ends_with("Written by David MacKenzie and Jim Meyering.\n"));
    }

    #[test]
    fn xnum_gnu_grammar() {
        // xdectoumax(s, 0, UINTMAX_MAX, "bkKmMGTPEZYRQ0", err, 0):
        // blanks and '+' skip, '-' is invalid, suffixes scale by 1024
        // (1000 with B/D), a bare suffix char means 1x.
        assert_eq!(xdectoumax_count("3"), Ok(3));
        assert_eq!(xdectoumax_count("+3"), Ok(3));
        assert_eq!(xdectoumax_count(" 3"), Ok(3));
        assert_eq!(xdectoumax_count("3 "), Err(CountVal::Invalid));
        assert_eq!(xdectoumax_count("-2"), Err(CountVal::Invalid));
        assert_eq!(xdectoumax_count("x"), Err(CountVal::Invalid));
        assert_eq!(xdectoumax_count("1x"), Err(CountVal::Invalid));
        assert_eq!(xdectoumax_count(""), Err(CountVal::Invalid));
        assert_eq!(xdectoumax_count("1b"), Ok(512));
        assert_eq!(xdectoumax_count("2K"), Ok(2048));
        assert_eq!(xdectoumax_count("K"), Ok(1024));
        assert_eq!(xdectoumax_count("kB"), Ok(1000));
        assert_eq!(xdectoumax_count("kiB"), Ok(1024));
        assert_eq!(xdectoumax_count("1ki"), Err(CountVal::Invalid));
        assert_eq!(xdectoumax_count("5kB"), Ok(5000));
        assert_eq!(xdectoumax_count("5KiB"), Ok(5120));
        assert_eq!(xdectoumax_count("5kD"), Ok(5000));
        assert_eq!(xdectoumax_count("5kd"), Err(CountVal::Invalid));
        assert_eq!(xdectoumax_count("0x10"), Err(CountVal::Invalid));
        assert_eq!(
            xdectoumax_count("18446744073709551615"),
            Ok(u64::MAX as u128)
        );
        assert_eq!(
            xdectoumax_count("18446744073709551616"),
            Err(CountVal::Overflow)
        );
        assert_eq!(
            xdectoumax_count("99999999999999999999"),
            Err(CountVal::Overflow)
        );
        // mantissa fits but the 'b' scale overflows: still EOVERFLOW
        assert_eq!(
            xdectoumax_count("9999999999999999999b"),
            Err(CountVal::Overflow)
        );
    }

    #[test]
    fn count_option_last_wins() {
        // GNU 9.4 (WSL probes): head has NO sticky state — the last
        // -c/-n occurrence assigns count_lines, elide_from_end and
        // n_units wholesale.
        let five = "h1\nh2\nh3\nh4\nh5\n";
        assert_eq!(run(&["-c", "5", "-n", "2"], five).1, "h1\nh2\n");
        assert_eq!(run(&["-n", "2", "-c", "5"], five).1, "h1\nh2");
        // the elide sign follows the LAST count option too
        assert_eq!(run(&["-n", "-2", "-c", "3"], five).1, "h1\n");
        assert_eq!(run(&["-c", "3", "-n", "-2"], five).1, "h1\nh2\nh3\n");
        // the first invalid count in argv order dies before --help
        let (r, _, err) = run(&["-n", "x", "--help"], "");
        assert_eq!(r, 1);
        assert!(err.contains("invalid number of lines"));
        let (r, out, _) = run(&["--help", "-n", "x"], "");
        assert_eq!(r, 0);
        assert!(out.starts_with("Usage: head"));
    }

    #[test]
    fn obsolete_num_forms() {
        // head.c main's argv[1] `-NUM[clbkmqvz]*` form: letters assign,
        // b/k/m leave a multiplier char appended to the digits.
        let five = "h1\nh2\nh3\nh4\nh5\n";
        assert_eq!(run(&["-2"], five).1, "h1\nh2\n");
        assert_eq!(run(&["-2c"], five).1, "h1");
        assert_eq!(run(&["-2l"], five).1, "h1\nh2\n");
        // "-5bl" -> bytes then lines with mult 'b' still attached:
        // parses "5b" = 2560 LINES (the multiplier survives 'l')
        assert_eq!(run(&["-2bl"], five).1, five);
        // "-2bc" -> 'c' resets the multiplier: plain 2 bytes
        assert_eq!(run(&["-2bc"], five).1, "h1");
        // a getopt -c/-n after the obsolete form reassigns everything
        assert_eq!(run(&["-2", "-c", "3"], five).1, "h1\n");
        // invalid trailing letter
        let (r, _, err) = run(&["-2x"], five);
        assert_eq!(r, 1);
        assert!(err.contains("invalid trailing option -- x"));
        // digit overflow in the obsolete form
        let (r, _, err) = run(&["-99999999999999999999"], five);
        assert_eq!(r, 1);
        assert!(err.contains("invalid number of lines"));
        assert!(err.contains("Value too large"));
        // -NUM anywhere but argv[1] is getopt's "invalid trailing option"
        let (r, _, err) = run(&["f", "-2"], five);
        assert_eq!(r, 1);
        assert!(err.contains("invalid trailing option -- 2"));
    }

    #[test]
    fn defaults_and_counts() {
        let twelve: String = (1..=12).map(|i| format!("{i}\n")).collect();
        let (r, out, _) = run(&[], &twelve);
        assert_eq!(r, 0);
        let first10: String = (1..=10).map(|i| format!("{i}\n")).collect();
        assert_eq!(out, first10);

        assert_eq!(run(&["-n", "2"], "a\nb\nc\n").1, "a\nb\n");
        assert_eq!(run(&["-n", "+2"], "a\nb\nc\n").1, "a\nb\n");
        assert_eq!(run(&["-n", "-2"], "a\nb\nc\nd\ne\n").1, "a\nb\nc\n");
        // GNU quirk probed: head -n -0 prints everything
        assert_eq!(run(&["-n", "-0"], "a\nb\n").1, "a\nb\n");
        assert_eq!(run(&["-n", "99"], "a\nb\n").1, "a\nb\n");
        assert_eq!(run(&["-n", "0"], "a\nb\n").1, "");
        assert_eq!(run(&["-n", "x"], "a\n").0, 1);
        assert!(run(&["-n", "x"], "a\n")
            .2
            .contains("invalid number of lines"));
        // no trailing newline on the last kept record
        assert_eq!(run(&[], "abc").1, "abc");
    }

    #[test]
    fn byte_counts() {
        assert_eq!(run(&["-c", "2"], "abcde").1, "ab");
        assert_eq!(run(&["-c", "-2"], "abcde").1, "abc");
        assert_eq!(run(&["-c", "+2"], "abcde").1, "ab");
        assert_eq!(run(&["-c", "0"], "abcde").1, "");
        assert_eq!(run(&["-c", "1b"], "abcde").1, "abcde");
        assert!(run(&["-c", "x"], "abc")
            .2
            .contains("invalid number of bytes"));
        assert_eq!(run(&["--bytes=1"], "ab\n").1, "a");
        assert_eq!(run(&["--lines=1"], "ab\n").1, "ab\n");
        assert_eq!(run(&["-3"], "a\nb\nc\nd\n").1, "a\nb\nc\n");
    }

    #[test]
    fn zero_terminated() {
        assert_eq!(run(&["-z", "-n", "2"], "a\0b\0c\0").1, "a\0b\0");
        assert_eq!(run(&["-z", "-n", "-1"], "a\0b\0").1, "a\0");
    }

    #[test]
    fn file_headers() {
        let sc = ScratchCtx::new("head-files");
        std::fs::write(sc.path("f1"), "1\n2\n3\n").unwrap();
        std::fs::write(sc.path("f2"), "4\n5\n").unwrap();
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = ["f1", "f2"].iter().map(Into::into).collect();
        let r = head_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert_eq!(io.out(), "==> f1 <==\n1\n2\n3\n\n==> f2 <==\n4\n5\n");
        // -q suppresses; -v forces on even for one file; '-' says stdin
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["-q", "f1", "f2"].iter().map(Into::into).collect();
        head_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(io.out(), "1\n2\n3\n4\n5\n");
        let mut io = MemIo::new("x\n");
        let owned: Vec<OsString> = ["-v"].iter().map(Into::into).collect();
        head_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(io.out(), "==> standard input <==\nx\n");
        let mut io = MemIo::new("x\n");
        let owned: Vec<OsString> = ["-v", "-"].iter().map(Into::into).collect();
        head_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(io.out(), "==> standard input <==\nx\n");
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["f1"].iter().map(Into::into).collect();
        head_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(io.out(), "1\n2\n3\n");
    }

    #[test]
    fn errors_and_mixed_operands() {
        let sc = ScratchCtx::new("head-err");
        std::fs::write(sc.path("f2"), "b\n").unwrap();
        std::fs::create_dir(sc.path("adir")).unwrap();
        // missing file: rc 1, message, later files continue
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = ["missing", "f2"].iter().map(Into::into).collect();
        let r = head_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert_eq!(io.out(), "==> f2 <==\nb\n");
        assert!(io
            .err()
            .starts_with("head: cannot open 'missing' for reading:"));
        // directory operand dies at the read like POSIX
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["adir"].iter().map(Into::into).collect();
        let r = head_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io
            .err()
            .contains("head: error reading 'adir': Is a directory"));
        // mixed stdin and files keep header order
        let mut io = MemIo::new("s\n");
        let owned: Vec<OsString> = ["-v", "f2", "-"].iter().map(Into::into).collect();
        let r = head_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert_eq!(io.out(), "==> f2 <==\nb\n\n==> standard input <==\ns\n");
        assert_eq!(run(&["--bogus"], "x\n").0, 1);
    }
}
