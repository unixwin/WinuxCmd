//! `head` — GNU coreutils head.c (9.4) semantics, probed byte-for-byte
//! against WSL coreutils 9.4 (rust2/target/probe/p-head.sh, o-head.txt).
//!
//! Count argument grammar (head.c's `xdectoumax`-shaped parse): optional
//! leading `+` (same as bare K: head -n +2 == head -n 2, probed) or `-`
//! (elide-from-end: all but the last K records), digits, then an optional
//! multiplier suffix (b 512, kB 1000, K 1024, MB/M 1024^2, ... R/Q; the
//! probed `-n 1b` / `-c 1b` rows cover the b suffix). `-n -0` / `-c -0`
//! print everything (all but last 0, probed). A malformed count dies with
//! `invalid number of lines|bytes: '<arg>'` (ASCII quotes — the LC_ALL=C
//! shape the differential oracle pins and the 1.x C++ emits; rc 1).
//!
//! Records are lines, or NUL-delimited chunks with -z. Inputs are STREAMED
//! the way head.c does it: `head_lines`/`copy_fd` (src/head.c:789/206) read
//! BUFSIZ at a time and stop as soon as the requested record/byte count is
//! satisfied, so a live pipeline is left to EPIPE instead of being drained
//! to EOF (#1442, /dev/zero, `yes | head`). The `-K` forms buffer only the
//! last K records/bytes (`elide_tail_{lines,bytes}_pipe`, src/head.c:501/265)
//! and emit everything before that window as it arrives. Files are
//! processed sequentially with the header written before each (probed:
//! first header has no leading blank, later headers are preceded by a blank
//! line; `-` prints `standard input`). Headers appear when more than one
//! input is read or -v is given; -q never prints them.
//!
//! Operand wildcards expand over the named directory (the 1.x
//! `glob_expand` direct-invocation contract, crate::glob): a Windows shell
//! does not expand `*.txt` before the applet sees it, while a POSIX shell
//! does, so the applet reproduces the POSIX result (GNU head.c itself
//! receives expanded operands and opens a literal `*.txt` for reading).
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

/// glibc's `BUFSIZ`, the chunk size head.c's read loops use.
const BUFSIZ: usize = 8192;

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
#[derive(Debug, PartialEq, Clone, Copy)]
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

/// Which side of the copy failed, so the caller can print head.c's
/// `error reading` vs `error writing 'standard output'` frame.
#[derive(Debug)]
enum CopyError {
    Read(std::io::Error),
    Write(std::io::Error),
}

fn read_chunk(r: &mut dyn Read, buf: &mut [u8]) -> Result<usize, CopyError> {
    r.read(buf).map_err(CopyError::Read)
}

fn write_chunk(out: &mut dyn Write, bytes: &[u8]) -> Result<(), CopyError> {
    out.write_all(bytes).map_err(CopyError::Write)
}

/// Copy at most `n_bytes` bytes (head.c:copy_fd, src/head.c:206). Stops at
/// EOF without a diagnostic: head_bytes only fails on READ_ERROR, and
/// COPY_FD_UNEXPECTED_EOF is ignored. `n_bytes == u128::MAX` is the
/// "essentially unlimited" sentinel, not reachable from the count grammar.
fn copy_bytes(r: &mut dyn Read, mut n_bytes: u128, out: &mut dyn Write) -> Result<(), CopyError> {
    let mut buf = [0u8; BUFSIZ];
    while n_bytes > 0 {
        let want = std::cmp::min(n_bytes, BUFSIZ as u128) as usize;
        let n = read_chunk(r, &mut buf[..want])?;
        if n == 0 {
            break;
        }
        write_chunk(out, &buf[..n])?;
        n_bytes -= n as u128;
    }
    Ok(())
}

/// Copy until EOF (the `-K 0` / `-n -0` shape: nothing is elided).
fn copy_all(r: &mut dyn Read, out: &mut dyn Write) -> Result<(), CopyError> {
    let mut buf = [0u8; BUFSIZ];
    loop {
        let n = read_chunk(r, &mut buf)?;
        if n == 0 {
            return Ok(());
        }
        write_chunk(out, &buf[..n])?;
    }
}

/// Emit the first `lines` records (head.c:head_lines, src/head.c:789):
/// read a BUFSIZ chunk, write it up to and including the record that
/// completes the count, and return without touching the rest of the
/// stream. `lines == 0` prints nothing.
fn head_lines_stream(
    r: &mut dyn Read,
    mut lines: u128,
    delim: u8,
    out: &mut dyn Write,
) -> Result<(), CopyError> {
    let mut buf = [0u8; BUFSIZ];
    while lines > 0 {
        let n = read_chunk(r, &mut buf)?;
        if n == 0 {
            break;
        }
        let mut to_write = n;
        for (i, &b) in buf[..n].iter().enumerate() {
            if b == delim {
                lines -= 1;
                if lines == 0 {
                    to_write = i + 1;
                    break;
                }
            }
        }
        write_chunk(out, &buf[..to_write])?;
        if lines == 0 {
            break;
        }
    }
    Ok(())
}

/// Emit all but the last `k` bytes. The last `k` bytes live in `buf`; each
/// read flushes the prefix that can no longer be part of the tail
/// (head.c:elide_tail_bytes_pipe, src/head.c:265, one-buffer shape).
fn elide_bytes_stream(r: &mut dyn Read, k: u128, out: &mut dyn Write) -> Result<(), CopyError> {
    if k == 0 {
        return copy_all(r, out);
    }
    // head.c:843-844: `n_units == UINTMAX_MAX` means "essentially
    // unlimited" — print nothing and never read the input.
    if k == u64::MAX as u128 {
        return Ok(());
    }
    let mut buf: Vec<u8> = Vec::new();
    let mut start = 0usize;
    loop {
        let mut chunk = [0u8; BUFSIZ];
        let n = read_chunk(r, &mut chunk)?;
        if n == 0 {
            break;
        }
        buf.extend_from_slice(&chunk[..n]);
        let live = (buf.len() - start) as u128;
        if live > k {
            // live - k <= n (the previous iteration left at most k bytes),
            // so this always fits in the chunk just read.
            let emit = (live - k) as usize;
            write_chunk(out, &buf[start..start + emit])?;
            start += emit;
        }
        // Reclaim the flushed prefix without moving the retained tail on
        // every read.
        if start > 1 << 20 && start * 2 > buf.len() {
            buf.drain(..start);
            start = 0;
        }
    }
    Ok(())
}

/// Emit all but the last `k` records. Complete records are queued; once the
/// queue holds more than `k`, the oldest is written out. An unterminated
/// trailing fragment counts as a record (head.c:elide_tail_lines_pipe,
/// src/head.c:501: `if (last->nbytes && last->buffer[...] != line_end)`).
fn elide_lines_stream(
    r: &mut dyn Read,
    k: u128,
    delim: u8,
    out: &mut dyn Write,
) -> Result<(), CopyError> {
    if k == 0 {
        return copy_all(r, out);
    }
    if k == u64::MAX as u128 {
        return Ok(());
    }
    let mut queue: std::collections::VecDeque<Vec<u8>> = std::collections::VecDeque::new();
    let mut partial: Vec<u8> = Vec::new();
    let mut push = |rec: Vec<u8>, queue: &mut std::collections::VecDeque<Vec<u8>>| {
        queue.push_back(rec);
        if queue.len() as u128 > k {
            queue.pop_front()
        } else {
            None
        }
    };
    let mut buf = [0u8; BUFSIZ];
    loop {
        let n = read_chunk(r, &mut buf)?;
        if n == 0 {
            break;
        }
        let mut start = 0usize;
        for (i, &b) in buf[..n].iter().enumerate() {
            if b == delim {
                let mut rec = std::mem::take(&mut partial);
                rec.extend_from_slice(&buf[start..=i]);
                if let Some(front) = push(rec, &mut queue) {
                    write_chunk(out, &front)?;
                }
                start = i + 1;
            }
        }
        if start < n {
            partial.extend_from_slice(&buf[start..n]);
        }
    }
    if !partial.is_empty() {
        if let Some(front) = push(partial, &mut queue) {
            write_chunk(out, &front)?;
        }
    }
    Ok(())
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
/// ASCII quotes: the differential oracle pins LC_ALL=C (runner.sh
/// ORACLE_LOCALE), where GNU quote() renders ASCII — the same shape the
/// 1.x C++ head emits; regressions/108 pins this byte for byte.
fn invalid_count(io: &mut Io, word: &str, post: &str, e: &CountVal) {
    let errno = match e {
        CountVal::Overflow => ": Value too large for defined data type",
        CountVal::Invalid => "",
    };
    let _ = writeln!(io.stderr, "head: invalid number of {word}: '{post}'{errno}");
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
                    invalid_count(
                        io,
                        if count_lines { "lines" } else { "bytes" },
                        &n_string,
                        &e,
                    );
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

    // head.c:1077-1081 (v9.4): byte elision is bounded by OFF_T_MAX;
    // xdectoumax's uintmax range would otherwise accept a magnitude such as
    // 2^64-1, where the elision silently does nothing. Probed WSL 9.4:
    // `head -c -18446744073709551615` -> "invalid number of bytes:
    // '18446744073709551615': Value too large for defined data type", rc 1.
    if !count_lines && elide_from_end && n_units > i64::MAX as u128 {
        invalid_count(io, "bytes", &n_units.to_string(), &CountVal::Overflow);
        return Ok(1);
    }

    let count = if elide_from_end {
        Count::Elide(n_units)
    } else {
        Count::First(n_units)
    };
    let bytes_mode = !count_lines;
    let delim: u8 = if line_end_nul { 0 } else { b'\n' };

    // Direct-invocation wildcard contract (1.x glob_expand): expand each
    // operand, keeping an unmatched pattern literal so the open diagnostic
    // names exactly what the user typed.
    let mut files: Vec<String> = Vec::new();
    for a in &p.positionals {
        let s = a.to_string_lossy().into_owned();
        if crate::glob::contains_wildcard(&s) {
            if let Some(expanded) = crate::glob::glob_expand_in(ctx, &s) {
                let mut sorted = expanded;
                sorted.sort();
                files.extend(sorted);
                continue;
            }
        }
        files.push(s);
    }
    if files.is_empty() {
        files.push("-".to_string());
    }
    let print_headers = match header_mode {
        HeaderMode::Always => true,
        HeaderMode::Never => false,
        HeaderMode::Multiple => files.len() > 1,
    };

    let mut first_header = true;
    let mut failed = false;
    for file in &files {
        // GNU head_file opens the operand first and only then lets head()
        // write the header, so a failed open prints no header (ctest
        // head_multi_file_skips_header_for_missing_file).
        //
        // "-" and the /dev/stdin family all read the real fd 0 (#1452; the
        // 1.x C++ head routes them the same way, src/commands/head.cpp).
        // Without this a /dev/stdin operand resolves through
        // resolve_pseudo_device to CONIN$ and head blocks on the console
        // rather than draining the piped stdin:
        // regressions/71-head-dev-stdin-pipe timed out (rc 124) in the
        // differential baseline. GNU keeps the operand spelling in the
        // header, so only the reader changes.
        let reads_stdin = file == "-" || winux_core::path::pseudo_device_std_fd(file) == Some(0);
        let mut reader: Option<Box<dyn Read>> = None;
        if !reads_stdin {
            match crate::fsutil::open_read_operand(ctx, file) {
                Ok(r) => reader = Some(r),
                Err(e) => {
                    if std::fs::metadata(resolve(ctx, file))
                        .map(|m| m.is_dir())
                        .unwrap_or(false)
                    {
                        // POSIX opens the directory then dies at the read
                        let _ = writeln!(io.stderr, "head: error reading '{file}': Is a directory");
                    } else {
                        let reason = gnu_open_reason(ctx, file, &e, false);
                        let _ = writeln!(
                            io.stderr,
                            "head: cannot open '{file}' for reading: {reason}"
                        );
                    }
                    failed = true;
                    continue;
                }
            }
        }

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

        // Read the operand as a stream and stop the moment the count is
        // satisfied. Nothing is slurped, so /dev/zero, a fifo behind the
        // #1038 marker and a live `yes |` pipeline all terminate at the
        // requested record (#1442; head.c head_file -> head -> head_lines/
        // copy_fd, src/head.c:864/826/789/206).
        let result = match reader.as_mut() {
            None => stream_records(&mut *io.stdin, count, delim, bytes_mode, io.stdout),
            Some(r) => stream_records(r.as_mut(), count, delim, bytes_mode, io.stdout),
        };
        match result {
            Ok(()) => {}
            Err(CopyError::Read(e)) => {
                // POSIX head opens the operand then reads it: a directory
                // dies at the read (Windows fails the open, so the Err arm
                // above also probes metadata). Any other reason is GNU's
                // strerror text.
                let reason = if file != "-"
                    && std::fs::metadata(resolve(ctx, file))
                        .map(|m| m.is_dir())
                        .unwrap_or(false)
                {
                    "Is a directory".to_string()
                } else {
                    crate::fsutil::reason(&e).to_string()
                };
                let name = if file == "-" {
                    "standard input"
                } else {
                    file.as_str()
                };
                let _ = writeln!(io.stderr, "head: error reading '{name}': {reason}");
                failed = true;
            }
            Err(CopyError::Write(e)) => {
                // head.c:xwrite_stdout (src/head.c:188) fails the whole run
                // on the first output error.
                let _ = writeln!(
                    io.stderr,
                    "head: error writing 'standard output': {}",
                    crate::fsutil::reason(&e)
                );
                return Ok(1);
            }
        }
    }
    Ok(if failed { 1 } else { 0 })
}

/// head.c:head's dispatch (src/head.c:826) over the four count shapes.
fn stream_records(
    r: &mut dyn Read,
    count: Count,
    delim: u8,
    bytes_mode: bool,
    out: &mut dyn Write,
) -> Result<(), CopyError> {
    match count {
        Count::First(k) if bytes_mode => copy_bytes(r, k, out),
        Count::First(k) => head_lines_stream(r, k, delim, out),
        Count::Elide(k) if bytes_mode => elide_bytes_stream(r, k, out),
        Count::Elide(k) => elide_lines_stream(r, k, delim, out),
    }
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
        // The /dev/stdin family reads fd 0 like '-', but GNU keeps the
        // operand spelling in the header (regressions/71-head-dev-stdin-pipe).
        for operand in ["/dev/stdin", "/dev/fd/0", "/proc/self/fd/0"] {
            let mut io = MemIo::new("y\n");
            let owned: Vec<OsString> = ["-v", operand].iter().map(Into::into).collect();
            head_cmd(&owned, &mut io.split(), &ctx).unwrap();
            assert_eq!(io.out(), format!("==> {operand} <==\ny\n"));
            assert_eq!(io.err(), "");
        }
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

    /// A reader that returns at most `max` bytes per call and counts the
    /// calls, so a test can prove head stopped instead of draining to EOF.
    struct Trickle {
        data: Vec<u8>,
        pos: usize,
        max: usize,
        reads: usize,
    }

    impl Read for Trickle {
        fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
            self.reads += 1;
            let want = buf.len().min(self.max).min(self.data.len() - self.pos);
            buf[..want].copy_from_slice(&self.data[self.pos..self.pos + want]);
            self.pos += want;
            Ok(want)
        }
    }

    #[test]
    fn stream_stops_at_the_requested_record_live_pipeline() {
        // head.c:head_lines: a live producer must see the reader stop after
        // the Nth delimiter, not wait for EOF (the `yes | head -n 3` shape).
        let data = b"pipeline\npipeline\npipeline\npipeline\n".to_vec();
        let mut r = Trickle {
            data,
            pos: 0,
            max: 9,
            reads: 0,
        };
        let mut out = Vec::new();
        stream_records(&mut r, Count::First(3), b'\n', false, &mut out).unwrap();
        assert_eq!(out, b"pipeline\npipeline\npipeline\n");
        // Three 9-byte chunks suffice; reading to EOF would need five.
        assert_eq!(r.reads, 3);
    }

    #[test]
    fn stream_stops_at_the_requested_byte_count() {
        // copy_fd stops at N bytes; /dev/zero never reaches EOF.
        let data = vec![0u8; 64];
        let mut r = Trickle {
            data,
            pos: 0,
            max: 4,
            reads: 0,
        };
        let mut out = Vec::new();
        stream_records(&mut r, Count::First(8), b'\n', true, &mut out).unwrap();
        assert_eq!(out, vec![0u8; 8]);
        assert_eq!(r.reads, 2);
    }

    #[test]
    fn dev_zero_streams_the_requested_prefix() {
        // niubash#993 §5: the synthesized device is an infinite reader; the
        // byte count must end the read (ctest head_c_dev_zero_yields_nul_bytes).
        let (r, out, err) = run(&["-c", "4", "/dev/zero"], "");
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert_eq!(out.as_bytes(), &[0u8; 4]);
        let (r, out, _) = run(&["-c", "0", "/dev/urandom"], "");
        assert_eq!(r, 0);
        assert!(out.is_empty());
    }

    #[test]
    fn elide_counts_an_unterminated_trailing_fragment_as_a_record() {
        // head.c:elide_tail_lines_pipe: the incomplete last line counts, so
        // `head -n -1` drops it and keeps the rest (probed WSL 9.4).
        assert_eq!(run(&["-n", "-1"], "a\nb").1, "a\n");
        assert_eq!(run(&["-n", "-2"], "l1\nl2\nl3\nl4").1, "l1\nl2\n");
        assert_eq!(run(&["-n", "-1"], "a").1, "");
        assert_eq!(run(&["-n", "-1"], "a\nb\nc\n").1, "a\nb\n");
        assert_eq!(run(&["-z", "-n", "-1"], "a\0b").1, "a\0");
        // UINTMAX_MAX elides everything without reading (head.c:843).
        let (r, out, err) = run(&["-n", "-18446744073709551615"], "a\nb\n");
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.is_empty());
    }

    #[test]
    fn elide_bytes_window_and_short_input() {
        // head.c:elide_tail_bytes_pipe: keep only the last K bytes.
        assert_eq!(run(&["-c", "-1"], "abcdef").1, "abcde");
        assert_eq!(run(&["-c", "-3"], "abcdefgh").1, "abcde");
        assert_eq!(run(&["-c", "-4"], "abc").1, "");
        assert_eq!(run(&["-c", "-0"], "a\nb").1, "a\nb");
        // head.c:1077-1081 (v9.4): -c -K is bounded by OFF_T_MAX.
        let (r, out, err) = run(&["-c", "-18446744073709551615"], "a\n");
        assert_eq!(r, 1);
        assert!(out.is_empty());
        assert!(err.contains("invalid number of bytes"));
        assert!(err.contains("Value too large for defined data type"));
    }

    #[test]
    fn wildcard_operands_expand_over_the_directory() {
        // The 1.x glob contract (crate::glob): Windows shells do not expand
        // before the applet, so head does it; unmatched patterns stay
        // literal and reach the open diagnostic.
        let sc = ScratchCtx::new("head-glob");
        std::fs::write(sc.path("file2.txt"), "line4\nline5\n").unwrap();
        std::fs::write(sc.path("file1.txt"), "line1\nline2\n").unwrap();
        std::fs::write(sc.path("other.log"), "log1\n").unwrap();
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = ["-n", "1", "*.txt"].iter().map(Into::into).collect();
        let r = head_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        // Sorted like the POSIX shell's expansion, and *.log is excluded.
        assert_eq!(
            io.out(),
            "==> file1.txt <==\nline1\n\n==> file2.txt <==\nline4\n"
        );
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["*.nomatch"].iter().map(Into::into).collect();
        let r = head_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io
            .err()
            .contains("head: cannot open '*.nomatch' for reading:"));
    }
}
