//! `b2sum`, `sha224sum`, `sha384sum` — GNU coreutils digest printing
//! (probed against coreutils 9.4; the digests themselves live in
//! digest.rs with NIST / RFC 7693 vectors).
//!
//! Default output per FILE is `<lowercase hex><flag><name>` where the
//! flag is two spaces in text mode and ` *` in binary mode (`-b`), the
//! line terminator is `\n` or NUL with `-z/--zero`, and stdin (no operand
//! or `-`) prints with the name `-`. `--tag` switches to the BSD form
//! `ALGO (name) = hex` (b2sum prints `BLAKE2b`, sha384sum `SHA384`, ...).
//! Digests are identical across platforms, so GNU-side cases gate
//! strictly on stdout bytes + rc.
//!
//! b2sum adds `-l/--length BITS`: the LAST occurrence wins; values must be
//! 1..=512 and a multiple of 8 — violations report `invalid length: 'X'`
//! plus a second diagnostic line (`length is not a multiple of 8` /
//! `maximum digest length for 'BLAKE2b' is 512 bits`) and rc 1.
//!
//! `--check` machinery (ported from GNU coreutils 9.4 `src/digest.c`
//! `digest_check`/`split_3`, probed byte-for-byte against WSL coreutils
//! 9.4): `-c/--check` reads checksum lines from every operand (default
//! `-`) and verifies each named file. Line forms: `HEX[ ]*[name]`,
//! `HEX *name`, BSD-reversed `HEX name` (one space; once seen, later
//! standard lines keep their separator in the name and once a standard
//! line is seen a later reversed line is improper — the `bsd_reversed`
//! latch is shared across all operands of the invocation, as in GNU),
//! and the `--tag` forms `ALGO[(name)] = HEX` / `ALGO-N (name) = HEX`
//! (the `-N` length suffix is b2sum-only, 1..=512, multiple of 8; for
//! regular lines b2sum auto-derives the digest length from the hex run
//! 2..128 digits, even). A leading `\` marks an escaped name
//! (`\\`, `\n`, `\r`). Comment (`#...`) and empty lines are skipped,
//! CRLF is tolerated. The digest length is part of line validity
//! (uppercase hex is accepted — `hex_equal` is case-insensitive).
//!
//! Output contract (all confirmed by probe): per line `<name>: OK`
//! (suppressed by `--quiet`) / `<name>: FAILED` / `<name>: FAILED open
//! or read`, names escaped when they contain `\`, `\n` or `\r`;
//! `--status` silences stdout entirely but keeps hard errors. Summary
//! stderr: `WARNING: N line(s) is/are improperly formatted`, `WARNING:
//! N listed file(s) could not be read`, `WARNING: N computed
//! checksum(s) did NOT match` (not silenced by `--quiet`), per-file
//! `<file>: no properly formatted checksum lines found` (also under
//! `--status`), `<file>: no file was verified` under `--ignore-missing`
//! when nothing matched. `-w` reports each bad line as
//! `<file>: <lineno>: improperly formatted ALGO checksum line` (also
//! under `--status`). rc 1 when nothing matched, any mismatch, any
//! open/read failure, no properly formatted line, or — with `--strict`
//! — any improperly formatted line. Flag validation mirrors GNU's
//! order: `-z`/`--tag`/`-b`/`-t` are rejected *with* `-c`, and
//! `--quiet`/`--status`/`--strict`/`-w`/`--ignore-missing` are rejected
//! *without* it, both with rc 1.
//!
//! Text vs binary hashing are the same bytes on this platform (raw file
//! reads), matching GNU's documented no-CRLF-translation behavior.

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use crate::digest::{blake2b, hex, sha224, sha384};
use crate::fsutil::resolve;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('b'),
        long: "binary",
        takes_value: false,
    },
    OptSpec {
        short: Some('t'),
        long: "text",
        takes_value: false,
    },
    OptSpec {
        short: Some('z'),
        long: "zero",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "tag",
        takes_value: false,
    },
    OptSpec {
        short: Some('l'),
        long: "length",
        takes_value: true,
    },
    OptSpec {
        short: Some('c'),
        long: "check",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "quiet",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "status",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "strict",
        takes_value: false,
    },
    OptSpec {
        short: Some('w'),
        long: "warn",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "ignore-missing",
        takes_value: false,
    },
];

#[derive(Clone, Copy, PartialEq)]
pub enum Algo {
    Blake2b,
    Sha224,
    Sha384,
}

impl Algo {
    fn name(self) -> &'static str {
        match self {
            Algo::Blake2b => "BLAKE2b",
            Algo::Sha224 => "SHA224",
            Algo::Sha384 => "SHA384",
        }
    }

    fn digest(self, data: &[u8], bits: usize) -> Vec<u8> {
        match self {
            Algo::Blake2b => blake2b(data, bits / 8),
            Algo::Sha224 => sha224(data).to_vec(),
            Algo::Sha384 => sha384(data).to_vec(),
        }
    }

    fn default_bits(self) -> usize {
        match self {
            Algo::Blake2b => 512,
            Algo::Sha224 => 224,
            Algo::Sha384 => 384,
        }
    }

    /// `DIGEST_TYPE_STRING`: the tag/warn algorithm name.
    fn tag(self) -> &'static str {
        self.name()
    }

    /// Hex digits per digest (`DIGEST_HEX_BYTES`); b2sum's is the 512-bit
    /// default — regular check lines override it by auto-length.
    fn hex_bytes(self) -> usize {
        match self {
            Algo::Blake2b => 128,
            Algo::Sha224 => 56,
            Algo::Sha384 => 96,
        }
    }

    /// `MIN_DIGEST_LINE_LENGTH` (coreutils generated headers: hex + 2
    /// separator chars). BLAKE2's is sized for its smallest digest
    /// (2 hex digits + 2), probed: 4-char `b2sum -c` lines parse, 3-char
    /// cannot.
    fn min_line_len(self) -> usize {
        match self {
            Algo::Blake2b => 4,
            Algo::Sha224 => 58,
            Algo::Sha384 => 98,
        }
    }
}

pub fn b2sum_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    run(Algo::Blake2b, true, "b2sum", args, io, ctx)
}

pub fn sha224sum_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    run(Algo::Sha224, false, "sha224sum", args, io, ctx)
}

pub fn sha384sum_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    run(Algo::Sha384, false, "sha384sum", args, io, ctx)
}

fn run(
    algo: Algo,
    allows_length: bool,
    prog: &str,
    args: &[OsString],
    io: &mut Io,
    ctx: &Ctx,
) -> CmdResult {
    let p = match argopt::parse(prog, args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if p.has("help") || p.has("version") {
        return Ok(0);
    }
    // -b / -t: last wins; --tag overrides the mode marker entirely.
    // None = neither given (GNU's binary == -1).
    let mode_flag = if p.has("text") {
        Some(false)
    } else if p.has("binary") {
        Some(true)
    } else {
        None
    };
    let binary = mode_flag == Some(true);
    let tag = p.has("tag");
    let zero = p.has("zero");
    let check = p.has("check");
    let status = p.has("status");
    let quiet = p.has("quiet");
    let warn = p.has("warn");
    let strict = p.has("strict");
    let ignore_missing = p.has("ignore-missing");

    // GNU main() option-validation order (coreutils digest.c:1495-1586);
    // each violation is a usage error with status 1.
    if zero && check {
        return usage_fail(
            prog,
            "the --zero option is not supported when verifying checksums",
            io,
        );
    }
    if tag && check {
        return usage_fail(
            prog,
            "the --tag option is meaningless when verifying checksums",
            io,
        );
    }
    if mode_flag.is_some() && check {
        return usage_fail(
            prog,
            "the --binary and --text options are meaningless when verifying checksums",
            io,
        );
    }
    for (set, opt) in [
        (ignore_missing, "--ignore-missing"),
        (status, "--status"),
        (warn, "--warn"),
        (quiet, "--quiet"),
        (strict, "--strict"),
    ] {
        if set && !check {
            return usage_fail(
                prog,
                &format!("the {opt} option is meaningful only when verifying checksums"),
                io,
            );
        }
    }

    let mut bits = algo.default_bits();
    if allows_length {
        if let Some(raw) = p.value_of("length") {
            match parse_length(raw) {
                Ok(v) => bits = v,
                Err(kind) => {
                    let _ = writeln!(io.stderr, "{prog}: invalid length: '{raw}'");
                    match kind {
                        LengthError::NotMultiple => {
                            let _ = writeln!(io.stderr, "{prog}: length is not a multiple of 8");
                        }
                        LengthError::TooLong => {
                            let _ = writeln!(
                                io.stderr,
                                "{prog}: maximum digest length for '{}' is 512 bits",
                                algo.name()
                            );
                        }
                        LengthError::TooShort => {
                            let _ = writeln!(
                                io.stderr,
                                "{prog}: minimum digest length for '{}' is 1 bits",
                                algo.name()
                            );
                        }
                        LengthError::NotANumber => {}
                    }
                    return Ok(1);
                }
            }
        }
    }

    let files: Vec<String> = if p.positionals.is_empty() {
        vec!["-".to_string()]
    } else {
        p.positionals
            .iter()
            .map(|a| a.to_string_lossy().into_owned())
            .collect()
    };

    if check {
        // The BSD-reversed latch is a GNU process-global shared across
        // operands of one invocation (digest.c `static int bsd_reversed`).
        let mut bsd_reversed: Option<bool> = None;
        let flags = CheckFlags {
            status,
            quiet,
            warn,
            strict,
            ignore_missing,
        };
        let mut ok = true;
        for file in &files {
            ok &= check_file(algo, prog, file, &flags, &mut bsd_reversed, io, ctx);
        }
        return Ok(if ok { 0 } else { 1 });
    }

    let term: &[u8] = if zero { b"\0" } else { b"\n" };
    let mut failed = false;
    let out = &mut *io.stdout;
    for file in &files {
        let data = if file == "-" {
            let mut buf = Vec::new();
            if std::io::copy(&mut io.stdin, &mut buf).is_err() {
                failed = true;
                continue;
            }
            buf
        } else {
            match std::fs::File::open(resolve(ctx, file)) {
                Ok(mut f) => {
                    let mut buf = Vec::new();
                    if std::io::copy(&mut f, &mut buf).is_err() {
                        let _ = writeln!(io.stderr, "{prog}: {file}: read error");
                        failed = true;
                        continue;
                    }
                    buf
                }
                Err(e) => {
                    let reason = crate::fsutil::reason(&e);
                    let note = if e.kind() == std::io::ErrorKind::IsADirectory
                        || std::fs::metadata(resolve(ctx, file))
                            .map(|m| m.is_dir())
                            .unwrap_or(false)
                    {
                        format!("{file}: Is a directory")
                    } else {
                        format!("{file}: {reason}")
                    };
                    let _ = writeln!(io.stderr, "{prog}: {note}");
                    failed = true;
                    continue;
                }
            }
        };
        let digest = hex(&algo.digest(&data, bits));
        if tag {
            // `term` already carries the newline (or NUL with -z)
            write!(
                out,
                "{} ({}) = {}{}",
                algo.name(),
                file,
                digest,
                String::from_utf8_lossy(term)
            )?;
        } else {
            let marker = if binary { " *" } else { "  " };
            out.write_all(digest.as_bytes())?;
            out.write_all(marker.as_bytes())?;
            out.write_all(file.as_bytes())?;
            out.write_all(term)?;
        }
    }
    Ok(if failed { 1 } else { 0 })
}

enum LengthError {
    NotANumber,
    NotMultiple,
    TooLong,
    TooShort,
}

fn parse_length(raw: &str) -> Result<usize, LengthError> {
    let s = raw.strip_prefix('+').unwrap_or(raw);
    let Ok(v) = s.parse::<usize>() else {
        return Err(LengthError::NotANumber);
    };
    if v > 512 {
        return Err(LengthError::TooLong);
    }
    if v == 0 {
        return Err(LengthError::TooShort);
    }
    if v % 8 != 0 {
        return Err(LengthError::NotMultiple);
    }
    Ok(v)
}

// ---------------------------------------------------------------------------
// --check machinery — port of coreutils 9.4 digest.c `digest_check`,
// `split_3`, `bsd_split_3`, `filename_unescape`, `valid_digits`,
// `hex_equal`, `print_filename`. Probed against WSL coreutils 9.4; see
// the module header for the confirmed contract.
// ---------------------------------------------------------------------------

#[derive(Clone, Copy)]
struct CheckFlags {
    status: bool,
    quiet: bool,
    warn: bool,
    strict: bool,
    ignore_missing: bool,
}

fn usage_fail(prog: &str, msg: &str, io: &mut Io) -> CmdResult {
    let _ = winux_core::usage_error(io, prog, msg);
    Ok(1)
}

fn is_white(b: u8) -> bool {
    b == b' ' || b == b'\t'
}

fn is_xdigit(b: u8) -> bool {
    b.is_ascii_digit() || (b'a'..=b'f').contains(&b) || (b'A'..=b'F').contains(&b)
}

/// One parsed, properly formatted checksum line.
struct CheckLine {
    hex: Vec<u8>,
    hex_bytes: usize,
    name: Vec<u8>,
}

/// Split a checksum line into digest + name, mirroring `split_3`.
/// `bsd_reversed` carries the format latch across lines (None = unseen,
/// Some(true) = BSD-reversed seen, Some(false) = standard seen).
fn split_3(line: &[u8], algo: Algo, bsd_reversed: &mut Option<bool>) -> Option<CheckLine> {
    let is_b2 = algo == Algo::Blake2b;
    let mut i = 0usize;
    while i < line.len() && is_white(line[i]) {
        i += 1;
    }
    let mut escaped = false;
    if i < line.len() && line[i] == b'\\' {
        i += 1;
        escaped = true;
    }

    // BSD/tag format: TAG[ ... (name) = HEX]; the -N length suffix and
    // the auto-length below are BLAKE2-only in GNU (#if HASH_ALGO_BLAKE2).
    let tag = algo.tag().as_bytes();
    if line.len() >= i + tag.len() && &line[i..i + tag.len()] == tag {
        let mut j = i + tag.len();
        let mut hex_bytes = algo.hex_bytes();
        if is_b2 && j < line.len() && line[j] == b'-' {
            let start = j + 1;
            let mut k = start;
            while k < line.len() && line[k].is_ascii_digit() {
                k += 1;
            }
            if k == start {
                return None;
            }
            let bits = parse_dec(&line[start..k])?;
            if bits == 0 || bits > 512 || bits % 8 != 0 {
                return None;
            }
            hex_bytes = bits / 4;
            j = k;
        }
        if j < line.len() && line[j] == b' ' {
            j += 1;
        }
        if j >= line.len() || line[j] != b'(' {
            return None;
        }
        let (name, hex) = bsd_split(&line[j + 1..], hex_bytes, escaped)?;
        return Some(CheckLine {
            hex: hex.to_vec(),
            hex_bytes,
            name,
        });
    }

    // Regular format. Minimum length fast check (digest.c:843-848): the
    // +1 applies when another backslash follows the escape marker.
    let min_len = algo.min_line_len() + usize::from(i < line.len() && line[i] == b'\\');
    if line.len() - i < min_len {
        return None;
    }

    let digest_start = i;
    let mut hex_bytes = algo.hex_bytes();
    if is_b2 {
        // Auto-determine the digest length from the leading hex run
        // (digest.c:852-864): 2..=128 digits, even.
        let mut hb = 0usize;
        while digest_start + hb < line.len() && is_xdigit(line[digest_start + hb]) {
            hb += 1;
        }
        if hb < 2 || hb % 2 != 0 || hb > 128 {
            return None;
        }
        hex_bytes = hb;
    }

    let mut j = digest_start;
    while j < line.len() && !is_white(line[j]) {
        j += 1;
    }
    // The digest must be followed by at least one whitespace character.
    if j == line.len() {
        return None;
    }
    let hex = &line[digest_start..j];
    if hex.len() != hex_bytes || !hex.iter().all(|&b| is_xdigit(b)) {
        return None;
    }
    j += 1; // the whitespace consumed as the digest terminator

    // BSD-reversed detection (digest.c:885-902). The latch applies to
    // this and all following lines of the check file.
    let at_end = j >= line.len();
    if at_end || line.len() - j == 1 || !(line[j] == b' ' || line[j] == b'*') {
        if *bsd_reversed == Some(false) {
            return None;
        }
        *bsd_reversed = Some(true);
    } else if *bsd_reversed != Some(true) {
        *bsd_reversed = Some(false);
        j += 1; // consume the one-char ' '/'*' marker
    }

    let mut name = line[j..].to_vec();
    if escaped {
        name = filename_unescape(&name)?;
    }
    Some(CheckLine {
        hex: hex.to_vec(),
        hex_bytes,
        name,
    })
}

fn parse_dec(b: &[u8]) -> Option<usize> {
    let mut v: usize = 0;
    for &c in b {
        v = v.checked_mul(10)?.checked_add(usize::from(c - b'0'))?;
    }
    Some(v)
}

/// Port of `bsd_split_3`: `name) = HEX` with the name ending at the LAST
/// `)`; whitespace around `=` is skipped; the digest must be exactly
/// `hex_bytes` hex digits.
fn bsd_split(s: &[u8], hex_bytes: usize, escaped: bool) -> Option<(Vec<u8>, &[u8])> {
    if s.is_empty() {
        return None;
    }
    let mut i = s.len() - 1;
    while i > 0 && s[i] != b')' {
        i -= 1;
    }
    if s[i] != b')' {
        return None;
    }
    let mut name = s[..i].to_vec();
    if escaped {
        name = filename_unescape(&name)?;
    }
    let mut j = i + 1;
    while j < s.len() && is_white(s[j]) {
        j += 1;
    }
    if j >= s.len() || s[j] != b'=' {
        return None;
    }
    j += 1;
    while j < s.len() && is_white(s[j]) {
        j += 1;
    }
    let hex = &s[j..];
    if hex.len() != hex_bytes || !hex.iter().all(|&b| is_xdigit(b)) {
        return None;
    }
    Some((name, hex))
}

/// Port of `filename_unescape`: only `\\`, `\n`, `\r` may follow a
/// backslash; a trailing lone backslash or a NUL is invalid.
fn filename_unescape(b: &[u8]) -> Option<Vec<u8>> {
    let mut out = Vec::with_capacity(b.len());
    let mut i = 0usize;
    while i < b.len() {
        if b[i] == b'\\' {
            i += 1;
            if i == b.len() {
                return None;
            }
            match b[i] {
                b'n' => out.push(b'\n'),
                b'r' => out.push(b'\r'),
                b'\\' => out.push(b'\\'),
                _ => return None,
            }
        } else if b[i] == 0 {
            return None;
        } else {
            out.push(b[i]);
        }
        i += 1;
    }
    Some(out)
}

/// Port of `hex_equal`: case-insensitive hex comparison.
fn hex_eq(computed_lower: &[u8], recorded: &[u8]) -> bool {
    computed_lower.len() == recorded.len()
        && computed_lower
            .iter()
            .zip(recorded.iter())
            .all(|(a, b)| a.to_ascii_lowercase() == b.to_ascii_lowercase())
}

/// `problematic_chars`: the name needs `\`-prefixed escaping on output.
fn problematic(name: &[u8]) -> bool {
    name.contains(&b'\\') || name.contains(&b'\n') || name.contains(&b'\r')
}

/// Port of `print_filename` with the leading-`\` escape marker.
fn write_escaped<W: std::io::Write + ?Sized>(out: &mut W, name: &[u8]) -> std::io::Result<()> {
    if problematic(name) {
        out.write_all(b"\\")?;
    }
    for &b in name {
        match b {
            b'\\' => out.write_all(b"\\\\")?,
            b'\n' => out.write_all(b"\\n")?,
            b'\r' => out.write_all(b"\\r")?,
            b => out.write_all(&[b])?,
        }
    }
    Ok(())
}

/// Approximation of gnulib `quotef` for stderr diagnostics: quote with
/// `'...'` when the name is empty or contains characters outside the
/// common safe set. stderr wording is recorded by the gate, not gated.
fn quotef(name: &[u8]) -> String {
    let s = String::from_utf8_lossy(name);
    let safe = !s.is_empty()
        && s.bytes()
            .all(|b| b.is_ascii_alphanumeric() || b"_.-/:@%^+,".contains(&b));
    if safe {
        s.into_owned()
    } else {
        format!("'{s}'")
    }
}

fn plural<'a>(n: u64, one: &'a str, many: &'a str) -> &'a str {
    if n == 1 {
        one
    } else {
        many
    }
}

/// GNU reason text for a failed target open. On Windows a directory open
/// fails outright, so recover GNU's "Is a directory" via a metadata check
/// (same approach as the print path above).
fn target_open_reason(e: &std::io::Error, name: &str, ctx: &Ctx) -> String {
    let is_dir = std::fs::metadata(resolve(ctx, name))
        .map(|m| m.is_dir())
        .unwrap_or(false);
    if is_dir {
        "Is a directory".to_string()
    } else {
        crate::fsutil::reason(e).to_string()
    }
}

/// Read one check file (operand, `-` = stdin) and verify each of its
/// lines; returns the `digest_check` success value.
fn check_file(
    algo: Algo,
    prog: &str,
    operand: &str,
    flags: &CheckFlags,
    bsd_reversed: &mut Option<bool>,
    io: &mut Io,
    ctx: &Ctx,
) -> bool {
    let is_stdin = operand == "-";
    let display: &str = if is_stdin { "standard input" } else { operand };

    let data: Vec<u8> = if is_stdin {
        let mut buf = Vec::new();
        match std::io::copy(&mut io.stdin, &mut buf) {
            Ok(_) => buf,
            Err(_) => {
                let _ = writeln!(io.stderr, "{prog}: {display}: read error");
                return false;
            }
        }
    } else {
        match std::fs::File::open(resolve(ctx, operand)) {
            Ok(mut f) => {
                let mut buf = Vec::new();
                match std::io::copy(&mut f, &mut buf) {
                    Ok(_) => buf,
                    Err(_) => {
                        let _ = writeln!(io.stderr, "{prog}: {operand}: read error");
                        return false;
                    }
                }
            }
            Err(e) => {
                let reason = if e.kind() == std::io::ErrorKind::NotFound {
                    crate::fsutil::reason(&e).to_string()
                } else {
                    target_open_reason(&e, operand, ctx)
                };
                let _ = writeln!(
                    io.stderr,
                    "{prog}: {}: {}",
                    quotef(operand.as_bytes()),
                    reason
                );
                return false;
            }
        }
    };

    // getline-shaped lines: split on '\n', dropping only the phantom
    // segment after a final newline.
    let mut lines: Vec<&[u8]> = data.split(|&b| b == b'\n').collect();
    if data.is_empty() || data.last() == Some(&b'\n') {
        lines.pop();
    }

    let mut misformatted: u64 = 0;
    let mut mismatched: u64 = 0;
    let mut open_fail: u64 = 0;
    let mut any_proper = false;
    let mut matched_any = false;

    for (idx, raw) in lines.iter().enumerate() {
        let line_number = idx + 1;
        // Remove any trailing carriage return (CRLF tolerated), skip
        // comments and empty lines (digest.c:1184-1196).
        let mut line: &[u8] = raw.strip_suffix(b"\r").unwrap_or(raw);
        if line.is_empty() {
            continue;
        }
        if line[0] == b'#' {
            continue;
        }
        // Truncate at an embedded NUL: C's in-buffer terminator makes all
        // later scans stop there.
        if let Some(p) = line.iter().position(|&b| b == 0) {
            line = &line[..p];
        }

        match split_3(line, algo, bsd_reversed) {
            // A line naming "-" is improper when checking stdin itself
            // (digest.c:1200-1201), preventing a self-referential loop.
            Some(cl) if is_stdin && cl.name == b"-" => {
                misformatted += 1;
                if flags.warn {
                    let _ = writeln!(
                        io.stderr,
                        "{prog}: {}: {line_number}: improperly formatted {} checksum line",
                        quotef(display.as_bytes()),
                        algo.tag()
                    );
                }
            }
            Some(cl) => {
                any_proper = true;
                let name = String::from_utf8_lossy(&cl.name).into_owned();
                let bits = cl.hex_bytes * 4;
                let read = if name == "-" {
                    let mut buf = Vec::new();
                    std::io::copy(&mut io.stdin, &mut buf).map(|_| buf)
                } else {
                    std::fs::read(resolve(ctx, &name))
                };
                match read {
                    // --ignore-missing: ENOENT targets are skipped silently
                    // (digest_file returns success with missing=true).
                    Err(e) if e.kind() == std::io::ErrorKind::NotFound && flags.ignore_missing => {}
                    Err(e) => {
                        open_fail += 1;
                        let reason = target_open_reason(&e, &name, ctx);
                        let _ = writeln!(io.stderr, "{prog}: {}: {reason}", quotef(&cl.name));
                        if !flags.status {
                            let _ = write_escaped(io.stdout, &cl.name);
                            let _ = writeln!(io.stdout, ": FAILED open or read");
                        }
                    }
                    Ok(content) => {
                        let digest = hex(&algo.digest(&content, bits));
                        let m = hex_eq(digest.as_bytes(), &cl.hex);
                        if m {
                            matched_any = true;
                        } else {
                            mismatched += 1;
                        }
                        if !flags.status {
                            if !m || !flags.quiet {
                                let _ = write_escaped(io.stdout, &cl.name);
                            }
                            if !m {
                                let _ = writeln!(io.stdout, ": FAILED");
                            } else if !flags.quiet {
                                let _ = writeln!(io.stdout, ": OK");
                            }
                        }
                    }
                }
            }
            None => {
                misformatted += 1;
                if flags.warn {
                    let _ = writeln!(
                        io.stderr,
                        "{prog}: {}: {line_number}: improperly formatted {} checksum line",
                        quotef(display.as_bytes()),
                        algo.tag()
                    );
                }
            }
        }
    }

    if !any_proper {
        let _ = writeln!(
            io.stderr,
            "{prog}: {}: no properly formatted checksum lines found",
            quotef(display.as_bytes())
        );
        return false;
    }
    if !flags.status {
        if misformatted > 0 {
            let _ = writeln!(
                io.stderr,
                "{prog}: WARNING: {misformatted} {} improperly formatted",
                plural(misformatted, "line is", "lines are")
            );
        }
        if open_fail > 0 {
            let _ = writeln!(
                io.stderr,
                "{prog}: WARNING: {open_fail} {} could not be read",
                plural(open_fail, "listed file", "listed files")
            );
        }
        if mismatched > 0 {
            let _ = writeln!(
                io.stderr,
                "{prog}: WARNING: {mismatched} {} did NOT match",
                plural(mismatched, "computed checksum", "computed checksums")
            );
        }
        if flags.ignore_missing && !matched_any {
            let _ = writeln!(
                io.stderr,
                "{prog}: {}: no file was verified",
                quotef(display.as_bytes())
            );
        }
    }

    any_proper
        && matched_any
        && mismatched == 0
        && open_fail == 0
        && (!flags.strict || misformatted == 0)
}

#[cfg(test)]
mod tests {
    use std::ffi::OsString;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(prog: &str, args: &[&str], stdin: &str) -> (u32, String, String) {
        let mut io = MemIo::new(stdin);
        let sc = ScratchCtx::new(prog);
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = match prog {
            "b2sum" => super::b2sum_cmd(&owned, &mut io.split(), &ctx).unwrap(),
            "sha224sum" => super::sha224sum_cmd(&owned, &mut io.split(), &ctx).unwrap(),
            _ => super::sha384sum_cmd(&owned, &mut io.split(), &ctx).unwrap(),
        };
        (r, io.out(), io.err())
    }

    // Expected outputs are GNU coreutils 9.4 probed values (the digest
    // inputs are fixed, so GNU and Rust hashes are identical bytes).
    #[test]
    fn b2sum_default_format() {
        let sc = ScratchCtx::new("b2f");
        std::fs::write(sc.path("abc.txt"), b"abc").unwrap();
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = vec!["abc.txt".into()];
        let r = super::b2sum_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        let out = io.out();
        let hash = "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d1\
                    7d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923"
            .replace(char::is_whitespace, "");
        assert_eq!(out, format!("{hash}  abc.txt\n"));
    }

    #[test]
    fn b2sum_stdin_dash() {
        let (r, out, _) = run("b2sum", &[], "abc");
        assert_eq!(r, 0);
        assert!(out.trim_end().ends_with("  -"));
        assert!(out.starts_with("ba80a53f981c4d0d"));
    }

    #[test]
    fn sha224_and_sha384_formats() {
        let (r, out, _) = run("sha224sum", &[], "abc");
        assert_eq!(r, 0);
        assert!(
            out.starts_with("23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7  -"),
            "got {out}"
        );
        let (r, out, _) = run("sha384sum", &[], "abc");
        assert_eq!(r, 0);
        assert!(out.starts_with("cb00753f45a35e8b"));
        // binary marker
        let (_, out, _) = run("sha224sum", &["-b"], "abc");
        assert!(out.contains(" *-"));
        // text after binary wins
        let (_, out, _) = run("sha224sum", &["-b", "-t"], "abc");
        assert!(out.contains("  -"));
    }

    #[test]
    fn b2sum_length_flags() {
        let (_, out, _) = run("b2sum", &["-l", "8"], "abc");
        assert!(out.starts_with("6b  "), "got {out}");
        let (_, out, _) = run("b2sum", &["-l", "128"], "abc");
        assert!(
            out.starts_with("cf4ab791c62b8d2b2109c90275287816  "),
            "got {out}"
        );
        let (_, out, _) = run("b2sum", &["-l", "512", "-l", "128"], "abc");
        assert!(out.starts_with("cf4ab791c62b8d2b"), "got {out}"); // last wins
                                                                   // invalid lengths: rc 1 + second diagnostic line
        let (r, _, err) = run("b2sum", &["-l", "7"], "abc");
        assert_eq!(r, 1);
        assert!(err.contains("invalid length: '7'"));
        assert!(err.contains("length is not a multiple of 8"));
        let (r, _, err) = run("b2sum", &["-l", "520"], "abc");
        assert_eq!(r, 1);
        assert!(err.contains("maximum digest length for 'BLAKE2b' is 512 bits"));
        let (r, _, err) = run("b2sum", &["-l", "xyz"], "abc");
        assert_eq!(r, 1);
        assert!(err.contains("invalid length: 'xyz'"));
    }

    #[test]
    fn tag_format() {
        let (_, out, _) = run("b2sum", &["--tag"], "");
        assert!(out.starts_with("BLAKE2b (-) = "), "got {out}");
        let (_, out, _) = run("sha224sum", &["--tag"], "abc");
        assert!(out.starts_with("SHA224 (-) = 23097d22"), "got {out}");
        let (_, out, _) = run("sha384sum", &["--tag"], "abc");
        assert!(out.starts_with("SHA384 (-) = cb00753f"), "got {out}");
    }

    #[test]
    fn zero_terminator_and_errors() {
        let (_, out, _) = run("b2sum", &["-z"], "abc");
        assert!(out.ends_with("  -\0"));
        // missing file: rc 1, message, later files still processed
        let sc = ScratchCtx::new("b2miss");
        std::fs::write(sc.path("g"), b"x").unwrap();
        let (r, out, err) = {
            let mut io = MemIo::new("");
            let ctx = sc.ctx();
            let owned: Vec<OsString> = vec!["missing".into(), "g".into()];
            let r = super::b2sum_cmd(&owned, &mut io.split(), &ctx).unwrap();
            (r, io.out(), io.err())
        };
        assert_eq!(r, 1);
        assert!(err.contains("missing: No such file or directory"));
        assert_eq!(out.lines().count(), 1);
        // directory operand: Is a directory, rc 1
        let sc = ScratchCtx::new("b2dir");
        std::fs::create_dir(sc.path("d")).unwrap();
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = vec!["d".into()];
        let r = super::b2sum_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        let _ = io;
        assert!(io.err().contains("Is a directory"));
    }

    // ---- --check machinery ------------------------------------------------
    //
    // Expected values are GNU coreutils 9.4 probed behavior (WSL), see the
    // module header and the probe scripts under target/probe1/.

    use super::{split_3, Algo, CheckLine};

    const S224_ABC: &str = "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7";
    const S384_ABC: &str = "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7";
    const B2_ABC: &str = "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d17d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923";
    const B2_128_ABC: &str = "cf4ab791c62b8d2b2109c90275287816";
    const B2_16_ABC: &str = "ae1e"; // b2sum -l 16 of "abc" (GNU 9.4 probed)

    fn p(line: &[u8], algo: Algo, latch: &mut Option<bool>) -> Option<CheckLine> {
        split_3(line, algo, latch)
    }

    #[track_caller]
    fn expect_name(cl: &CheckLine, name: &[u8]) {
        assert_eq!(cl.name, name, "name mismatch for line");
    }

    #[test]
    fn check_standard_and_marker_lines() {
        let mut l = None;
        let cl = p(format!("{S224_ABC}  f").as_bytes(), Algo::Sha224, &mut l).unwrap();
        expect_name(&cl, b"f");
        assert_eq!(cl.hex_bytes, 56);
        assert_eq!(cl.hex, S224_ABC.as_bytes());
        assert_eq!(l, Some(false));
        // binary marker ` *`
        let mut l = None;
        let cl = p(format!("{S224_ABC} *f").as_bytes(), Algo::Sha224, &mut l).unwrap();
        expect_name(&cl, b"f");
        assert_eq!(l, Some(false));
        // tab behaves like BSD-reversed (one whitespace, no mode char)
        let mut l = None;
        let cl = p(format!("{S224_ABC}\tf").as_bytes(), Algo::Sha224, &mut l).unwrap();
        expect_name(&cl, b"f");
        assert_eq!(l, Some(true));
        // leading whitespace is skipped
        let mut l = None;
        let cl = p(format!("  {S224_ABC}  f").as_bytes(), Algo::Sha224, &mut l).unwrap();
        expect_name(&cl, b"f");
        // name keeps interior+trailing whitespace
        let mut l = None;
        let cl = p(format!("{S224_ABC}  f x ").as_bytes(), Algo::Sha224, &mut l).unwrap();
        expect_name(&cl, b"f x ");
        // digest + two spaces + EOL: name is " " (one space, from the
        // s_len-i==1 BSD branch); digest + one space + EOL is improper
        // (no separator after the NUL'd digest).
        let mut l = None;
        let cl = p(format!("{S224_ABC}  ").as_bytes(), Algo::Sha224, &mut l).unwrap();
        expect_name(&cl, b" ");
        let mut l = None;
        assert!(p(format!("{S224_ABC} ").as_bytes(), Algo::Sha224, &mut l).is_none());
    }

    #[test]
    fn check_min_length_rejects() {
        // sha224: 57-char line (digest + one space + EOL) is improper,
        // the 58-char digest + " x" line parses (checked in the test above)
        let mut l = None;
        assert!(p(format!("{S224_ABC} ").as_bytes(), Algo::Sha224, &mut l).is_none());
        // digest only, no separator
        let mut l = None;
        assert!(p(format!("{S224_ABC}").as_bytes(), Algo::Sha224, &mut l).is_none());
        // wrong digest length (sha224 needs exactly 56 hex)
        let mut l = None;
        assert!(p(
            format!("{S224_ABC}0  f").as_bytes(), // 57 hex digits: too long
            Algo::Sha224,
            &mut l
        )
        .is_none());
        assert!(p(
            format!("{}  f", &S224_ABC[..54]).as_bytes(),
            Algo::Sha224,
            &mut l
        )
        .is_none());
        // non-hex digest
        let mut l = None;
        assert!(p(
            format!("{}{}  f", &S224_ABC[..50], "zzzzzz").as_bytes(),
            Algo::Sha224,
            &mut l
        )
        .is_none());
        // uppercase hex is VALID (hex_equal is case-insensitive in GNU)
        let mut l = None;
        let up = S224_ABC.to_uppercase();
        let cl = p(format!("{up}  f").as_bytes(), Algo::Sha224, &mut l).unwrap();
        expect_name(&cl, b"f");
    }

    #[test]
    fn check_bsd_reversed_latch() {
        // one-space line latches to BSD; a following standard line keeps
        // its separator in the name
        let mut l = None;
        let cl = p(format!("{S224_ABC} f").as_bytes(), Algo::Sha224, &mut l).unwrap();
        expect_name(&cl, b"f");
        assert_eq!(l, Some(true));
        let cl = p(format!("{S224_ABC}  f").as_bytes(), Algo::Sha224, &mut l).unwrap();
        expect_name(&cl, b" f");
        // standard first, then BSD-reversed is improper
        let mut l = None;
        assert!(p(format!("{S224_ABC}  f").as_bytes(), Algo::Sha224, &mut l).is_some());
        assert_eq!(l, Some(false));
        assert!(p(format!("{S224_ABC} f").as_bytes(), Algo::Sha224, &mut l).is_none());
    }

    #[test]
    fn check_b2sum_auto_length_and_tiny() {
        // 2-hex-digit digest (BLAKE2b-8) with a 4-char line parses
        let mut l = None;
        let cl = p(b"6b f", Algo::Blake2b, &mut l).unwrap();
        assert_eq!(cl.hex_bytes, 2);
        expect_name(&cl, b"f");
        // odd-length hex run is improper
        let mut l = None;
        assert!(p(b"a f", Algo::Blake2b, &mut l).is_none());
        // over-long run (>128 digits) is improper
        let mut l = None;
        let long = "a".repeat(130);
        assert!(p(format!("{long}  f").as_bytes(), Algo::Blake2b, &mut l).is_none());
        // 128-digit run parses with hex_bytes = 128
        let mut l = None;
        let maxd = "a".repeat(128);
        let cl = p(format!("{maxd}  f").as_bytes(), Algo::Blake2b, &mut l).unwrap();
        assert_eq!(cl.hex_bytes, 128);
    }

    #[test]
    fn check_tag_lines() {
        // SHA224 tag form
        let mut l = None;
        let cl = p(
            format!("SHA224 (f) = {S224_ABC}").as_bytes(),
            Algo::Sha224,
            &mut l,
        )
        .unwrap();
        expect_name(&cl, b"f");
        assert_eq!(cl.hex_bytes, 56);
        // openssl no-space form
        let mut l = None;
        let cl = p(
            format!("SHA224(f) = {S224_ABC}").as_bytes(),
            Algo::Sha224,
            &mut l,
        )
        .unwrap();
        expect_name(&cl, b"f");
        // -N suffix is improper for the fixed-length algorithms
        let mut l = None;
        assert!(p(
            format!("SHA224-224 (f) = {S224_ABC}").as_bytes(),
            Algo::Sha224,
            &mut l
        )
        .is_none());
        // tag with wrong digest length is improper
        let mut l = None;
        assert!(p(
            format!("SHA384 (f) = {S224_ABC}").as_bytes(),
            Algo::Sha384,
            &mut l
        )
        .is_none());
        let mut l = None;
        let cl = p(
            format!("SHA384 (f) = {S384_ABC}").as_bytes(),
            Algo::Sha384,
            &mut l,
        )
        .unwrap();
        expect_name(&cl, b"f");
        // BLAKE2b tag without -N requires 128 hex digits
        let mut l = None;
        assert!(p(
            format!("BLAKE2b (f) = {B2_128_ABC}").as_bytes(),
            Algo::Blake2b,
            &mut l
        )
        .is_none());
        let mut l = None;
        let cl = p(
            format!("BLAKE2b (f) = {B2_ABC}").as_bytes(),
            Algo::Blake2b,
            &mut l,
        )
        .unwrap();
        assert_eq!(cl.hex_bytes, 128);
        // BLAKE2b-128 tag
        let mut l = None;
        let cl = p(
            format!("BLAKE2b-128 (f) = {B2_128_ABC}").as_bytes(),
            Algo::Blake2b,
            &mut l,
        )
        .unwrap();
        assert_eq!(cl.hex_bytes, 32);
        expect_name(&cl, b"f");
        // -N must be positive, <= 512 and a multiple of 8
        for bad in ["0", "130", "520", ""] {
            let mut l = None;
            let line = if bad.is_empty() {
                format!("BLAKE2b- (f) = {B2_128_ABC}\n")
            } else {
                format!("BLAKE2b-{bad} (f) = {B2_128_ABC}\n")
            };
            assert!(
                p(line.as_bytes(), Algo::Blake2b, &mut l).is_none(),
                "BLAKE2b-{} should be improper",
                bad
            );
        }
        // tag without parens is improper
        let mut l = None;
        assert!(p(format!("SHA224 nope here").as_bytes(), Algo::Sha224, &mut l).is_none());
        // missing '=' is improper
        let mut l = None;
        assert!(p(
            format!("SHA224 (f) {S224_ABC}").as_bytes(),
            Algo::Sha224,
            &mut l
        )
        .is_none());
    }

    #[test]
    fn check_escaped_names() {
        // leading \ marks an escaped name: we\\ird -> we\ird
        let mut l = None;
        let cl = p(
            format!("\\{S224_ABC}  we\\\\ird").as_bytes(),
            Algo::Sha224,
            &mut l,
        )
        .unwrap();
        expect_name(&cl, b"we\\ird");
        // \n and \r escapes
        let mut l = None;
        let cl = p(
            format!("\\{S224_ABC}  a\\nb\\rc").as_bytes(),
            Algo::Sha224,
            &mut l,
        )
        .unwrap();
        expect_name(&cl, b"a\nb\rc");
        // invalid escape sequence is improper
        let mut l = None;
        assert!(p(
            format!("\\{S224_ABC}  we\\qird").as_bytes(),
            Algo::Sha224,
            &mut l
        )
        .is_none());
        // trailing lone backslash is improper
        let mut l = None;
        assert!(p(
            format!("\\{S224_ABC}  x\\").as_bytes(),
            Algo::Sha224,
            &mut l
        )
        .is_none());
        // WITHOUT the marker a raw backslash name parses verbatim
        let mut l = None;
        let cl = p(
            format!("{S224_ABC}  we\\ird").as_bytes(),
            Algo::Sha224,
            &mut l,
        )
        .unwrap();
        expect_name(&cl, b"we\\ird");
    }

    fn check_run(
        prog: &str,
        args: &[&str],
        stdin: &str,
        files: &[(&str, &[u8])],
    ) -> (u32, String, String) {
        let sc = ScratchCtx::new(prog);
        for (name, content) in files {
            std::fs::write(sc.path(name), content).unwrap();
        }
        let mut io = MemIo::new(stdin);
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = match prog {
            "b2sum" => super::b2sum_cmd(&owned, &mut io.split(), &ctx).unwrap(),
            "sha224sum" => super::sha224sum_cmd(&owned, &mut io.split(), &ctx).unwrap(),
            _ => super::sha384sum_cmd(&owned, &mut io.split(), &ctx).unwrap(),
        };
        (r, io.out(), io.err())
    }

    #[test]
    fn check_flow_ok_failed_quiet_status() {
        let ck = format!("{S224_ABC}  f\n{S224_ABC}  g\n");
        let files = [
            ("f", &b"abc"[..]),
            ("g", &b"xyz"[..]),
            ("ck", ck.as_bytes()),
        ];
        let (r, out, _) = check_run("sha224sum", &["-c", "ck"], "", &files);
        assert_eq!(r, 1);
        assert_eq!(out, "f: OK\ng: FAILED\n");
        // quiet suppresses only OK
        let (r, out, _) = check_run("sha224sum", &["-c", "--quiet", "ck"], "", &files);
        assert_eq!(r, 1);
        assert_eq!(out, "g: FAILED\n");
        // status suppresses all stdout
        let (r, out, _) = check_run("sha224sum", &["-c", "--status", "ck"], "", &files);
        assert_eq!(r, 1);
        assert_eq!(out, "");
        // rc 0 with status when all match
        let ck = format!("{S224_ABC}  f\n");
        let files2 = [("f", &b"abc"[..]), ("ck", ck.as_bytes())];
        let (r, out, _) = check_run("sha224sum", &["-c", "--status", "ck"], "", &files2);
        assert_eq!(r, 0);
        assert_eq!(out, "");
    }

    #[test]
    fn check_flow_missing_and_ignore_missing() {
        let ck = format!("{S224_ABC}  nope\n{S224_ABC}  f\n");
        let files = [("f", &b"abc"[..]), ("ck", ck.as_bytes())];
        let (r, out, err) = check_run("sha224sum", &["-c", "ck"], "", &files);
        assert_eq!(r, 1);
        assert_eq!(out, "nope: FAILED open or read\nf: OK\n");
        assert!(err.contains("WARNING: 1 listed file could not be read"));
        // ignore-missing: skipped silently, rc 0 (something matched)
        let (r, out, err) = check_run("sha224sum", &["-c", "--ignore-missing", "ck"], "", &files);
        assert_eq!(r, 0);
        assert_eq!(out, "f: OK\n");
        assert!(!err.contains("could not be read"));
        // ignore-missing with nothing verified: "no file was verified", rc 1
        let ck = format!("{S224_ABC}  nope\n");
        let files2 = [("ck", ck.as_bytes())];
        let (r, out, err) = check_run("sha224sum", &["-c", "--ignore-missing", "ck"], "", &files2);
        assert_eq!(r, 1);
        assert_eq!(out, "");
        assert!(err.contains("ck: no file was verified"));
    }

    #[test]
    fn check_flow_bad_lines_warn_strict() {
        let ck = format!("{S224_ABC}  f\nbogus\nzz zz\n");
        let files = [("f", &b"abc"[..]), ("ck", ck.as_bytes())];
        // without --strict: warning only, rc 0
        let (r, out, err) = check_run("sha224sum", &["-c", "ck"], "", &files);
        assert_eq!(r, 0);
        assert_eq!(out, "f: OK\n");
        assert!(err.contains("WARNING: 2 lines are improperly formatted"));
        // --strict: rc 1
        let (r, _, _) = check_run("sha224sum", &["-c", "--strict", "ck"], "", &files);
        assert_eq!(r, 1);
        // -w: per-line diagnostics with line numbers (comment/blank shift them)
        let ck = format!("# comment\n\nbogus\n{S224_ABC}  f\n");
        let files2 = [("f", &b"abc"[..]), ("ck", ck.as_bytes())];
        let (r, _, err) = check_run("sha224sum", &["-c", "-w", "ck"], "", &files2);
        assert_eq!(r, 0);
        assert!(err.contains("ck: 3: improperly formatted SHA224 checksum line"));
        assert!(err.contains("WARNING: 1 line is improperly formatted"));
        // b2sum warn names BLAKE2b
        let ckb = format!("{B2_ABC}  f\ngarbage\n");
        let files3 = [("f", &b"abc"[..]), ("ck", ckb.as_bytes())];
        let (_, _, err) = check_run("b2sum", &["-c", "-w", "ck"], "", &files3);
        assert!(err.contains("improperly formatted BLAKE2b checksum line"));
        // no properly formatted lines: error, rc 1 (even under --status)
        let files4 = [("ck", b"garbage\n".as_slice())];
        let (r, out, err) = check_run("sha224sum", &["-c", "--status", "ck"], "", &files4);
        assert_eq!(r, 1);
        assert_eq!(out, "");
        assert!(err.contains("ck: no properly formatted checksum lines found"));
        // comments and blank lines alone are not checksum lines
        let files5 = [("ck", b"# only\n\n".as_slice())];
        let (r, _, err) = check_run("sha224sum", &["-c", "ck"], "", &files5);
        assert_eq!(r, 1);
        assert!(err.contains("no properly formatted checksum lines found"));
    }

    #[test]
    fn check_flow_crlf_tag_stdin_and_flags() {
        // CRLF check file (strip the CR)
        let ck = format!("{S224_ABC}  f\r\n");
        let files = [("f", &b"abc"[..]), ("ck", ck.as_bytes())];
        let (r, out, _) = check_run("sha224sum", &["-c", "ck"], "", &files);
        assert_eq!(r, 0);
        assert_eq!(out, "f: OK\n");
        // tag round trip
        let ck = format!("SHA224 (f) = {S224_ABC}\n");
        let files = [("f", &b"abc"[..]), ("ck", ck.as_bytes())];
        let (r, out, _) = check_run("sha224sum", &["-c", "ck"], "", &files);
        assert_eq!(r, 0);
        assert_eq!(out, "f: OK\n");
        // checking stdin via "-"
        let ck = format!("{S224_ABC}  f\n");
        let files = [("f", &b"abc"[..])];
        let (r, out, _) = check_run("sha224sum", &["-c", "-"], &ck, &files);
        assert_eq!(r, 0);
        assert_eq!(out, "f: OK\n");
        // a check line naming "-" while reading stdin is improper
        let ck = format!("{S224_ABC}  -\n");
        let (r, _, err) = check_run("sha224sum", &["-c", "-"], &ck, &files);
        assert_eq!(r, 1);
        assert!(err.contains("standard input"));
        // missing check file operand
        let (r, out, err) = check_run("sha224sum", &["-c", "nosuchck"], "", &files);
        assert_eq!(r, 1);
        assert_eq!(out, "");
        assert!(err.contains("nosuchck: No such file or directory"));
    }

    #[test]
    fn check_flag_validation() {
        let files = [("f", &b"abc"[..])];
        // -b/-t with -c
        let (r, _, err) = check_run("sha224sum", &["-b", "-c", "f"], "", &files);
        assert_eq!(r, 1);
        assert!(
            err.contains(
                "the --binary and --text options are meaningless when verifying checksums"
            ),
            "got {err}"
        );
        // -z with -c
        let (r, _, err) = check_run("sha224sum", &["-z", "-c", "f"], "", &files);
        assert_eq!(r, 1);
        assert!(err.contains("the --zero option is not supported when verifying checksums"));
        // --tag with -c
        let (r, _, err) = check_run("sha224sum", &["--tag", "-c", "f"], "", &files);
        assert_eq!(r, 1);
        assert!(err.contains("the --tag option is meaningless when verifying checksums"));
        // check-only flags without -c
        for opt in [
            "--quiet",
            "--status",
            "--strict",
            "--warn",
            "--ignore-missing",
        ] {
            let (r, _, err) = check_run("sha224sum", &[opt, "f"], "", &files);
            assert_eq!(r, 1, "{opt}");
            assert!(
                err.contains(&format!(
                    "the {opt} option is meaningful only when verifying checksums"
                )),
                "{opt}: {err}"
            );
        }
        // b2sum tag with -l round trip and auto-length lines
        let ck128 = format!("{B2_128_ABC}  f\n");
        let ck16 = format!("{B2_16_ABC}  f\n");
        let files = [("f", &b"abc"[..]), ("ck", ck128.as_bytes())];
        let (r, out, _) = check_run("b2sum", &["-l", "512", "-c", "ck"], "", &files);
        assert_eq!(r, 0); // -l is ignored on the check side
        assert_eq!(out, "f: OK\n");
        let files = [("f", &b"abc"[..]), ("ck", ck16.as_bytes())];
        let (r, out, _) = check_run("b2sum", &["-c", "ck"], "", &files);
        assert_eq!(r, 0);
        assert_eq!(out, "f: OK\n");
    }
}
