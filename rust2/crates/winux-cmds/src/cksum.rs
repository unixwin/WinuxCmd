//! `cksum` — GNU coreutils cksum.c (9.4) semantics, probed byte-for-byte
//! against WSL coreutils 9.4 (rust2/target/probe/w5-cksum*.sh).
//!
//! Algorithms (probed `-a` surface): crc (the POSIX CRC-32/CKSUM default:
//! poly 0x04C11DB7 (normal form), register starting at 0, the byte count appended
//! little-endian — empty input answers 4294967295, "123456789" answers
//! 930766865), bsd/sysv (sum -r/-s shapes; stdin prints no name), and the
//! digest family whose cores already live in `digest.rs`: sha224/sha256/
//! sha384/sha512/blake2b. md5/sha1/sm3 are NOT implemented here — their
//! cores belong to the wave6 digest family (documented boundary; no gate
//! rows exercise them).
//!
//! Output forms (probed): tagged by default — digests as `TAG (name) =
//! hex`, crc as `crc bytes name`, bsd/sysv as their sum shapes;
//! `--untagged` prints digests as `hex  name` (two spaces) and leaves the
//! legacy forms unchanged; `-z` ends lines with NUL; `--raw` emits the
//! raw digest big-endian for crc (probed ff ff ff ff for empty) and bare
//! bytes for digests; `--base64` base64-encodes digest tags (ignored by
//! crc, probed). `-l BITS` is blake2b-only (8..=512, multiple of 8;
//! probed diagnostics for 7 and 520).
//!
//! Check mode `-c` (probed w5-cksum4/5.sh): only tagged digest lines parse
//! (`TAG (name) = hex|base64`, hex case-insensitive per the uppercase
//! probe); CRC-format and untagged lines are rejected wholesale with
//! `no properly formatted checksum lines found` rc 1. OK/FAILED and
//! `FAILED open or read` go to stdout; WARNINGs to stderr; --status
//! silences stdout, --quiet suppresses OK, --ignore-missing skips missing
//! entries (`no file was verified` rc 1 when nothing was verified).
//! Algorithm names are matched case-sensitively and never abbreviated
//! (probed `-a CRC` and `-a s` are invalid).

use std::ffi::OsString;
use std::io::Read;

use crate::argopt::{self, OptSpec};
use crate::b64core;
use crate::digest;
use crate::fsutil::{gnu_open_reason, quotef, resolve};
use crate::gnuhelp;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('a'),
        long: "algorithm",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "base64",
        takes_value: false,
    },
    OptSpec {
        short: Some('c'),
        long: "check",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "ignore-missing",
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
        short: Some('w'),
        long: "warn",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "strict",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "tag",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "untagged",
        takes_value: false,
    },
    OptSpec {
        short: Some('z'),
        long: "zero",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "raw",
        takes_value: false,
    },
    OptSpec {
        short: Some('l'),
        long: "length",
        takes_value: true,
    },
    OptSpec {
        short: Some('t'),
        long: "text",
        takes_value: false,
    },
    OptSpec {
        short: Some('b'),
        long: "binary",
        takes_value: false,
    },
];

// `--help` body verbatim from `env cksum --help` under WSL coreutils 9.4.
const CKSUM_HELP_BODY: &str = r#"Usage: cksum [OPTION]... [FILE]...
Print or verify checksums.
By default use the 32 bit CRC algorithm.

With no FILE, or when FILE is -, read standard input.

Mandatory arguments to long options are mandatory for short options too.
  -a, --algorithm=TYPE  select the digest type to use.  See DIGEST below.
      --base64          emit base64-encoded digests, not hexadecimal
  -c, --check           read checksums from the FILEs and check them
  -l, --length=BITS     digest length in bits; must not exceed the max for
                          the blake2 algorithm and must be a multiple of 8
      --raw             emit a raw binary digest, not hexadecimal
      --tag             create a BSD-style checksum (the default)
      --untagged        create a reversed style checksum, without digest type
  -z, --zero            end each output line with NUL, not newline,
                          and disable file name escaping

The following five options are useful only when verifying checksums:
      --ignore-missing  don't fail or report status for missing files
      --quiet           don't print OK for each successfully verified file
      --status          don't output anything, status code shows success
      --strict          exit non-zero for improperly formatted checksum lines
  -w, --warn            warn about improperly formatted checksum lines

      --debug           indicate which implementation used
      --help        display this help and exit
      --version     output version information and exit

DIGEST determines the digest algorithm and default output format:
  sysv      (equivalent to sum -s)
  bsd       (equivalent to sum -r)
  crc       (equivalent to cksum)
  md5       (equivalent to md5sum)
  sha1      (equivalent to sha1sum)
  sha224    (equivalent to sha224sum)
  sha256    (equivalent to sha256sum)
  sha384    (equivalent to sha384sum)
  sha512    (equivalent to sha512sum)
  blake2b   (equivalent to b2sum)
  sm3       (only available through cksum)

When checking, the input should be a former output of this program,
or equivalent standalone program.

"#;

/// The algorithms this port computes (wave6 owns md5/sha1/sm3 cores).
const ALGORITHMS: &[&str] = &[
    "bsd", "sysv", "crc", "md5", "sha1", "sha224", "sha256", "sha384", "sha512", "blake2b", "sm3",
];

#[derive(Clone, Copy, PartialEq)]
enum Algo {
    Crc,
    Bsd,
    Sysv,
    Sha224,
    Sha256,
    Sha384,
    Sha512,
    Blake2b,
}

fn parse_algorithm(name: &str) -> Option<Algo> {
    match name {
        "crc" => Some(Algo::Crc),
        "bsd" => Some(Algo::Bsd),
        "sysv" => Some(Algo::Sysv),
        "sha224" => Some(Algo::Sha224),
        "sha256" => Some(Algo::Sha256),
        "sha384" => Some(Algo::Sha384),
        "sha512" => Some(Algo::Sha512),
        "blake2b" => Some(Algo::Blake2b),
        _ => None,
    }
}

fn algo_tag(algo: Algo, bits: usize) -> String {
    match algo {
        Algo::Sha224 => "SHA224".to_string(),
        Algo::Sha256 => "SHA256".to_string(),
        Algo::Sha384 => "SHA384".to_string(),
        Algo::Sha512 => "SHA512".to_string(),
        // probed k2/k3: the default 512 prints bare "BLAKE2b"; a -l bits
        // other than the default carries the suffix
        Algo::Blake2b => {
            if bits == 512 {
                "BLAKE2b".to_string()
            } else {
                format!("BLAKE2b-{bits}")
            }
        }
        _ => unreachable!(),
    }
}

fn algo_digest(algo: Algo, data: &[u8], bits: usize) -> Vec<u8> {
    match algo {
        Algo::Sha224 => digest::sha224(data).to_vec(),
        Algo::Sha256 => digest::sha256(data).to_vec(),
        Algo::Sha384 => digest::sha384(data).to_vec(),
        Algo::Sha512 => digest::sha512(data).to_vec(),
        Algo::Blake2b => digest::blake2b(data, bits / 8),
        _ => unreachable!(),
    }
}

/// POSIX CRC-32/CKSUM over the bytes with the byte count appended
/// little-endian (cksum.c crc_sum_buffer + crc_finish): the register
/// runs MSB-first with the normal-form poly 0x04C11DB7, starts at 0,
/// and the residue is inverted once at the end (empty input answers ~0;
/// "123456789" answers 930766865 — probed).
pub fn crc_cksum(bytes: &[u8]) -> u32 {
    let mut crc: u32 = 0;
    for &b in bytes {
        crc ^= u32::from(b) << 24;
        crc = crc_step(crc);
    }
    let mut len = bytes.len() as u64;
    while len != 0 {
        crc ^= u32::from(len as u8) << 24;
        crc = crc_step(crc);
        len >>= 8;
    }
    !crc
}

fn crc_step(mut crc: u32) -> u32 {
    for _ in 0..8 {
        crc = (crc << 1)
            ^ (if crc & 0x8000_0000 != 0 {
                0x04C1_1DB7
            } else {
                0
            });
    }
    crc
}

/// sum.c BSD checksum + 1024-byte block count (sum -r shape).
fn bsd_sum(bytes: &[u8]) -> (u16, usize) {
    let mut checksum: u32 = 0;
    for &b in bytes {
        checksum = (checksum >> 1) + ((checksum & 1) << 15);
        checksum += b as u32;
        checksum &= 0xffff;
    }
    (checksum as u16, bytes.len().div_ceil(1024))
}

/// sum.c SysV checksum + 512-byte block count (sum -s shape).
fn sysv_sum(bytes: &[u8]) -> (u16, usize) {
    let mut sum: u32 = bytes.iter().map(|&b| b as u32).sum();
    while sum > 0xffff {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    (sum as u16, bytes.len().div_ceil(512))
}

#[derive(Clone, Copy)]
enum LineEnd {
    Nul,
    Newline,
}

/// Emit one checksum record for a named input.
fn emit_record(
    io: &mut Io,
    algo: Algo,
    bits: usize,
    bytes: &[u8],
    name: Option<&str>,
    untagged: bool,
    raw: bool,
    base64: bool,
    end: LineEnd,
) {
    let terminator = match end {
        LineEnd::Nul => '\0',
        LineEnd::Newline => '\n',
    };
    let body = match algo {
        Algo::Crc => {
            let crc = crc_cksum(bytes);
            let size = bytes.len();
            if raw {
                // probed: crc --raw = 4 bytes big-endian of the residue
                let raw_bytes = io_out_bytes(crc);
                return write_raw(io, &raw_bytes, name, bytes, end);
            }
            match (name, untagged) {
                (Some(n), _) => format!("{crc} {size} {n}"),
                (None, _) => format!("{crc} {size}"),
            }
        }
        Algo::Bsd => {
            let (sum, blocks) = bsd_sum(bytes);
            match name {
                Some(n) => format!("{sum:05} {blocks:>5} {n}"),
                None => format!("{sum:05} {blocks:>5}"),
            }
        }
        Algo::Sysv => {
            let (sum, blocks) = sysv_sum(bytes);
            match name {
                Some(n) => format!("{sum} {blocks} {n}"),
                None => format!("{sum} {blocks}"),
            }
        }
        _ => {
            // probed s19: digest forms always carry a name; stdin names "-"
            let name = name.or(Some("-"));
            let digest = algo_digest(algo, bytes, bits);
            if raw {
                return write_raw(io, &digest, name, bytes, end);
            }
            let tag = algo_tag(algo, bits);
            if base64 {
                let codec = b64core::base64_codec();
                let enc = b64core::encode(&codec, &digest, 0);
                let enc = String::from_utf8_lossy(&enc).into_owned();
                if untagged {
                    match name {
                        Some(n) => format!("{enc}  {n}"),
                        None => enc,
                    }
                } else {
                    match name {
                        Some(n) => format!("{tag} ({n}) = {enc}"),
                        None => enc,
                    }
                }
            } else {
                let hexd = digest::hex(&digest);
                if untagged {
                    match name {
                        Some(n) => format!("{hexd}  {n}"),
                        None => hexd,
                    }
                } else {
                    match name {
                        Some(n) => format!("{tag} ({n}) = {hexd}"),
                        None => hexd,
                    }
                }
            }
        }
    };
    let _ = write!(io.stdout, "{body}{terminator}");
}

fn io_out_bytes(crc: u32) -> Vec<u8> {
    crc.to_be_bytes().to_vec()
}

fn write_raw(io: &mut Io, digest: &[u8], _name: Option<&str>, _bytes: &[u8], _end: LineEnd) {
    let _ = io.stdout.write_all(digest);
}

/// The probed valid-argument list (kept in GNU's registration order).
fn invalid_algorithm_message(name: &str) -> String {
    let mut m = format!("invalid argument '{name}' for '--algorithm'\nValid arguments are:");
    for a in ALGORITHMS {
        m.push_str(&format!("\n  - '{a}'"));
    }
    m
}

pub fn cksum_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("cksum", args, OPTS, io, ctx, 1) {
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
                    CKSUM_HELP_BODY,
                    gnuhelp::footer("cksum", "cksum invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("cksum", "Pádraig Brady and Q. Frank Xia")
                )?;
                0
            }
        });
    }

    let mut algo = Algo::Crc;
    if let Some(name) = p.value_of("algorithm") {
        match parse_algorithm(name) {
            Some(a) => algo = a,
            None => {
                let _ = writeln!(io.stderr, "cksum: {}", invalid_algorithm_message(name));
                return Ok(1);
            }
        }
    }
    if p.has("length") {
        // probed: -l is blake2b-only; the error precedes any file work
        if algo != Algo::Blake2b {
            let _ = writeln!(
                io.stderr,
                "cksum: --length is only supported with --algorithm=blake2b"
            );
            return Ok(1);
        }
        match parse_bits(p.value_of("length").unwrap_or("")) {
            Ok(bits) => return Ok(run_files(&p, io, ctx, algo, bits)),
            Err(msgs) => {
                for m in msgs {
                    let _ = writeln!(io.stderr, "cksum: {m}");
                }
                return Ok(1);
            }
        }
    }
    Ok(run_files(&p, io, ctx, algo, 512))
}

/// -l parse: non-numeric/non-multiple-of-8/over-512 diagnostics, probed
/// ('7' -> two lines: invalid length + not-a-multiple; '520' -> invalid
/// length + max-is-512).
fn parse_bits(raw: &str) -> Result<usize, Vec<String>> {
    let mut errs = Vec::new();
    let bits: Option<usize> = raw.parse().ok();
    match bits {
        None => {
            errs.push(format!("invalid length: '{raw}'"));
            errs.push("length is not a multiple of 8".to_string());
            return Err(errs);
        }
        Some(b) => {
            if b % 8 != 0 {
                errs.push(format!("invalid length: '{raw}'"));
                errs.push("length is not a multiple of 8".to_string());
            } else if b > 512 {
                errs.push(format!("invalid length: '{raw}'"));
                errs.push("maximum digest length for 'BLAKE2b' is 512 bits".to_string());
            }
            if errs.is_empty() {
                return Ok(b);
            }
            return Err(errs);
        }
    }
}

fn run_files(p: &argopt::Parsed, io: &mut Io, ctx: &Ctx, algo: Algo, bits: usize) -> u32 {
    let untagged = p.has("untagged");
    let zero = p.has("zero");
    let raw = p.has("raw");
    let base64 = p.has("base64");
    let end = if zero { LineEnd::Nul } else { LineEnd::Newline };

    if p.has("check") {
        return check_files(p, io, ctx, algo, bits);
    }

    let inputs: Vec<String> = if p.positionals.is_empty() {
        vec!["-".to_string()]
    } else {
        p.positionals
            .iter()
            .map(|a| a.to_string_lossy().into_owned())
            .collect()
    };
    let name_shown = !p.positionals.is_empty();

    let mut failed = false;
    for input in &inputs {
        let bytes = match read_input(ctx, io, input) {
            Ok(b) => b,
            Err(reason) => {
                let _ = writeln!(io.stderr, "cksum: {}: {reason}", quotef(input));
                failed = true;
                continue;
            }
        };
        let name = if name_shown {
            Some(input.as_str())
        } else {
            None
        };
        emit_record(io, algo, bits, &bytes, name, untagged, raw, base64, end);
    }
    if failed {
        1
    } else {
        0
    }
}

fn read_input(ctx: &Ctx, io: &mut Io, input: &str) -> Result<Vec<u8>, String> {
    if input == "-" {
        let mut buf = Vec::new();
        io.stdin
            .read_to_end(&mut buf)
            .map_err(|_| "read error".to_string())?;
        return Ok(buf);
    }
    let native = resolve(ctx, input);
    let meta = std::fs::metadata(&native);
    if meta.as_ref().is_ok_and(|m| m.is_dir()) {
        return Err("Is a directory".to_string());
    }
    std::fs::File::open(&native)
        .map_err(|e| gnu_open_reason(ctx, input, &e, false))
        .and_then(|mut f| {
            let mut buf = Vec::new();
            f.read_to_end(&mut buf)
                .map_err(|_| "read error".to_string())?;
            Ok(buf)
        })
}

// ---- check mode (probed w5-cksum4.sh / w5-cksum5.sh) ----

struct CheckLine {
    tag_bits: usize,
    algo: Algo,
    name: String,
    digest: String,
    base64: bool,
}

/// Parse `TAG (name) = digest` — hex (case-insensitive, probed) or
/// base64 (sniffed, probed s16). Returns None for anything else.
fn parse_check_line(line: &str) -> Option<CheckLine> {
    let open = line.find(" (")?;
    if open == 0 {
        return None;
    }
    let tag = &line[..open];
    let after_paren = open + 2;
    let name_end = line[after_paren..].find(')')? + after_paren;
    let name = &line[after_paren..name_end];
    let rest = &line[name_end + 1..];
    let digest = rest.strip_prefix(" = ")?;
    if name.is_empty() || digest.is_empty() {
        return None;
    }
    // tag -> (algo, bits): SHA2xx fixed; BLAKE2b / BLAKE2b-<bits>
    let (algo, bits) = if let Some(rest) = tag.strip_prefix("BLAKE2b") {
        let b = if rest.is_empty() {
            512
        } else {
            rest.strip_prefix('-')?.parse().ok()?
        };
        (Algo::Blake2b, b)
    } else {
        match tag {
            "SHA224" => (Algo::Sha224, 224),
            "SHA256" => (Algo::Sha256, 256),
            "SHA384" => (Algo::Sha384, 384),
            "SHA512" => (Algo::Sha512, 512),
            _ => return None,
        }
    };
    let base64 = !digest.bytes().all(|b| b.is_ascii_hexdigit());
    Some(CheckLine {
        tag_bits: bits,
        algo,
        name: name.to_string(),
        digest: digest.to_string(),
        base64,
    })
}

fn decode_digest(line: &CheckLine) -> Option<Vec<u8>> {
    if line.base64 {
        let codec = b64core::base64_codec();
        b64core::decode(&codec, line.digest.as_bytes(), false).ok()
    } else {
        hex_decode(&line.digest)
    }
}

fn hex_decode(s: &str) -> Option<Vec<u8>> {
    if !s.len().is_multiple_of(2) {
        return None;
    }
    let mut out = Vec::with_capacity(s.len() / 2);
    let b = s.as_bytes();
    let mut i = 0;
    while i < b.len() {
        let hi = (b[i] as char).to_digit(16)?;
        let lo = (b[i + 1] as char).to_digit(16)?;
        out.push(((hi << 4) | lo) as u8);
        i += 2;
    }
    Some(out)
}

fn check_files(p: &argopt::Parsed, io: &mut Io, ctx: &Ctx, algo: Algo, bits: usize) -> u32 {
    let _ = (algo, bits);
    let status = p.has("status");
    let quiet = p.has("quiet");
    let ignore_missing = p.has("ignore-missing");
    let warn = p.has("warn");
    let strict = p.has("strict");

    let inputs: Vec<String> = if p.positionals.is_empty() {
        vec!["-".to_string()]
    } else {
        p.positionals
            .iter()
            .map(|a| a.to_string_lossy().into_owned())
            .collect()
    };

    let mut failed = false;
    for list in &inputs {
        let text = match read_input(ctx, io, list) {
            Ok(b) => String::from_utf8_lossy(&b).into_owned(),
            Err(reason) => {
                let _ = writeln!(io.stderr, "cksum: {}: {reason}", quotef(list));
                failed = true;
                continue;
            }
        };
        let lines: Vec<&str> = text.lines().collect();
        let parsed: Vec<CheckLine> = lines.iter().filter_map(|l| parse_check_line(l)).collect();
        if parsed.is_empty() {
            // probed: CRC-format and untagged lists are rejected wholesale
            let _ = writeln!(
                io.stderr,
                "cksum: {}: no properly formatted checksum lines found",
                quotef(list)
            );
            failed = true;
            continue;
        }
        let mut bad_format: Vec<usize> = Vec::new();
        for (idx, line) in lines.iter().enumerate() {
            if parse_check_line(line).is_none() {
                bad_format.push(idx + 1);
            }
        }
        if warn {
            for ln in &bad_format {
                let _ = writeln!(
                    io.stderr,
                    "cksum: {}: {}: improperly formatted checksum line",
                    quotef(list),
                    ln
                );
            }
        }
        let mut verified = 0usize;
        let mut mismatches = 0usize;
        let mut unreadable = 0usize;
        for line in &parsed {
            let expected = decode_digest(line);
            let actual = read_input(ctx, io, &line.name)
                .ok()
                .map(|bytes| algo_digest(line.algo, &bytes, line.tag_bits));
            match (actual, expected) {
                (Some(a), Some(e)) if a == e => {
                    verified += 1;
                    if !status && !quiet {
                        let _ = writeln!(io.stdout, "{}: OK", line.name);
                    }
                }
                (Some(_), _) => {
                    mismatches += 1;
                    if !status {
                        let _ = writeln!(io.stdout, "{}: FAILED", line.name);
                    }
                }
                (None, _) => {
                    if !ignore_missing {
                        unreadable += 1;
                        if !status {
                            // probed case 4: --status silences both streams
                            let _ = writeln!(io.stdout, "{}: FAILED open or read", line.name);
                            let _ = writeln!(
                                io.stderr,
                                "cksum: {}: No such file or directory",
                                line.name
                            );
                        }
                    }
                }
            }
        }
        if !status && mismatches > 0 {
            let _ = writeln!(
                io.stderr,
                "cksum: WARNING: {} computed checksum{} did NOT match",
                mismatches,
                if mismatches == 1 { "" } else { "s" }
            );
        }
        if !status && unreadable > 0 {
            let _ = writeln!(
                io.stderr,
                "cksum: WARNING: {} listed file{} could not be read",
                unreadable,
                if unreadable == 1 { "" } else { "s" }
            );
        }
        if mismatches > 0 || unreadable > 0 {
            failed = true;
        }
        if verified == 0 && mismatches == 0 && ignore_missing {
            // probed case 6: everything ignored -> no file was verified, rc 1
            if !status {
                let _ = writeln!(io.stderr, "cksum: {}: no file was verified", quotef(list));
            }
            failed = true;
        }
        if !bad_format.is_empty() && strict {
            failed = true;
        }
    }
    if failed {
        1
    } else {
        0
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str], stdin: &str) -> (u32, String, String, ScratchCtx) {
        let sc = ScratchCtx::new("cksum");
        let mut io = MemIo::new(stdin);
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = cksum_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err(), sc)
    }

    fn run3(args: &[&str], stdin: &str) -> (u32, String, String) {
        let (r, out, err, _) = run(args, stdin);
        (r, out, err)
    }

    /// One scratch shared by several invocations (check-mode lists).
    fn run_in(sc: &ScratchCtx, args: &[&str], stdin: &str) -> (u32, String, String) {
        let mut io = MemIo::new(stdin);
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = cksum_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn crc_classic_vectors() {
        // probed: empty -> 4294967295; "123456789" -> 930766865 (the
        // canonical CRC-32/CKSUM test vector)
        assert_eq!(crc_cksum(b""), 4294967295);
        assert_eq!(crc_cksum(b"123456789"), 930766865);
        assert_eq!(crc_cksum(b"abc"), 1219131554);
    }

    #[test]
    fn crc_named_and_stdin() {
        // probed c5/c6: stdin prints no name, "-" prints "-"
        let (r, out, _) = run3(&[], "123456789");
        assert_eq!(r, 0);
        assert_eq!(out, "930766865 9\n");
        let (r, out, _) = run3(&["-"], "123456789");
        assert_eq!(r, 0);
        assert_eq!(out, "930766865 9 -\n");
    }

    #[test]
    fn bsd_sysv_forms() {
        // probed k1/k19/k20/s1/s2
        let (_, out, _) = run3(&["-a", "bsd"], "123456789");
        assert_eq!(out, "53615     1\n");
        let (_, out, _) = run3(&["-a", "sysv"], "123456789");
        assert_eq!(out, "477 1\n");
        let (_, out, _) = run3(&["-a", "bsd", "/dev/null"], "");
        assert_eq!(out, "00000     0 /dev/null\n");
        let (_, out, _) = run3(&["-a", "sysv"], "");
        assert_eq!(out, "0 0\n");
    }

    #[test]
    fn digest_tags() {
        // probed k14 (stdin name is "-"): SHA256 tag line
        let (_, out, _) = run3(&["-a", "sha256"], "123456789");
        assert_eq!(
            out,
            "SHA256 (-) = 15e2b0d3c33891ebb0f1ef609ec419420c20e320ce94c65fbc8c3312448eb225\n"
        );
        // probed k17/k18/k19: the sha2 tag words
        let (_, out, _) = run3(&["-a", "sha224"], "123456789");
        assert!(out.starts_with(
            "SHA224 (-) = 9b3e61bf29f17c75572fae2e86e17809a4513d07c8a18152acf34521\n"
        ));
        let (_, out, _) = run3(&["-a", "sha512"], "123456789");
        assert!(out.starts_with(
            "SHA512 (-) = d9e6762dd1c8eaf6d61b3c6192fc408d4d6d5f1176d0c29169bc24e71c3f274a"
        ));
        // probed 2/3: blake2b default tag and -l 256 suffix form
        let (_, out, _) = run3(&["-a", "blake2b"], "123456789");
        assert!(out.starts_with("BLAKE2b (-) = f5ab8bafa6f2f72b431188ac38ae2de7"));
        let (_, out, _) = run3(&["-a", "blake2b", "-l", "256"], "123456789");
        assert!(out.starts_with("BLAKE2b-256 (-) = 16e0bf1f85594a11e75030981c0b6703"));
        let (_, out, _) = run3(&["-a", "blake2b", "-l", "8"], "123456789");
        assert_eq!(out, "BLAKE2b-8 (-) = fb\n");
    }

    #[test]
    fn untagged_digest_two_spaces() {
        // probed k11/case-1: untagged digest = hex + TWO spaces + name
        let (_, out, _) = run3(&["--untagged", "-a", "sha256"], "123456789");
        assert_eq!(
            out,
            "15e2b0d3c33891ebb0f1ef609ec419420c20e320ce94c65fbc8c3312448eb225  -\n"
        );
    }

    #[test]
    fn raw_forms() {
        // probed s4/s6: crc --raw = 4 bytes big-endian, no name, no newline
        let sc = ScratchCtx::new("cksum-raw");
        let raw = |args: &[&str], stdin: &str| -> Vec<u8> {
            let mut io = MemIo::new(stdin);
            let ctx = sc.ctx();
            let owned: Vec<OsString> = args.iter().map(Into::into).collect();
            cksum_cmd(&owned, &mut io.split(), &ctx).unwrap();
            io.output.clone()
        };
        assert_eq!(raw(&["--raw"], "123456789"), vec![0x37, 0x7a, 0x60, 0x11]);
        assert_eq!(raw(&["--raw"], ""), vec![0xff, 0xff, 0xff, 0xff]);
    }

    #[test]
    fn base64_digest_form() {
        // probed k17/s8b: base64-encoded digest in the tag line
        let (_, out, _) = run3(&["--base64", "-a", "sha256"], "123456789");
        assert!(out.starts_with("SHA256 (-) = FeKw08M4keuw8e9gnsQZQgwg4yDOlMZfvIwzEkSOsiU=\n"));
        // crc ignores --base64 (probed s7)
        let (_, out, _) = run3(&["--base64"], "123456789");
        assert_eq!(out, "930766865 9\n");
    }

    #[test]
    fn algorithm_validation() {
        // probed k28/k29: no abbreviation, case-sensitive; full list in the
        // diagnostic; unimplemented cores (md5/sha1/sm3) are wave6's and
        // excluded by the same parse table
        for a in ["bogus", "CRC", "s", "md5", "sha1", "sm3"] {
            let (r, out, err) = run3(&["-a", a], "123456789");
            assert_eq!(r, 1, "{a}");
            assert!(out.is_empty());
            assert!(err.contains(&format!("invalid argument '{a}' for '--algorithm'")));
            assert!(err.contains("- 'sha256'"));
        }
    }

    #[test]
    fn length_only_for_blake2b() {
        // probed k22/c22: -l outside blake2b is refused before file work
        let (r, _, err) = run3(&["-a", "sha256", "-l", "128"], "123456789");
        assert_eq!(r, 1);
        assert!(err.contains("--length is only supported with --algorithm=blake2b"));
        // probed s10/s11: multiple-of-8 and max-512 diagnostics
        let (r, _, err) = run3(&["-a", "blake2b", "-l", "7"], "123456789");
        assert_eq!(r, 1);
        assert!(err.contains("invalid length: '7'"));
        assert!(err.contains("length is not a multiple of 8"));
        let (r, _, err) = run3(&["-a", "blake2b", "-l", "520"], "123456789");
        assert_eq!(r, 1);
        assert!(err.contains("invalid length: '520'"));
        assert!(err.contains("maximum digest length for 'BLAKE2b' is 512 bits"));
    }

    #[test]
    fn missing_and_dir_operands() {
        // probed k30/c9/c11: missing continues with rc 1; dir is EISDIR
        let (r, _, err) = run3(&["nosuch"], "");
        assert_eq!(r, 1);
        assert!(err.contains("cksum: nosuch: No such file or directory"));
        let sc = ScratchCtx::new("cksum-mixed");
        std::fs::write(sc.path("f"), "abc").unwrap();
        let ctx = sc.ctx();
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["nosuch", "f"].iter().map(Into::into).collect();
        let r = cksum_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert_eq!(io.out(), "1219131554 3 f\n");
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["."].iter().map(Into::into).collect();
        let r = cksum_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("cksum: .: Is a directory"));
    }

    #[test]
    fn zero_terminator() {
        // probed k13/s21: -z ends the record with NUL
        let (_, out, _) = run3(&["-z"], "123456789");
        assert_eq!(out, "930766865 9\0");
        let (_, out, _) = run3(&["-z", "-a", "sha256"], "123456789");
        assert!(out.ends_with("8eb225\0"));
    }

    fn make_list(sc: &ScratchCtx, name: &str, content: &str) {
        std::fs::write(sc.path(name), content).unwrap();
    }

    #[test]
    fn check_ok_and_failed() {
        // probed k23/w5-cksum5 case 1/2: OK on stdout rc 0; FAILED line +
        // WARNING rc 1
        let sc = ScratchCtx::new("cksum-check");
        std::fs::write(sc.path("ck"), "123456789").unwrap();
        make_list(
            &sc,
            "ok.lst",
            "SHA256 (ck) = 15e2b0d3c33891ebb0f1ef609ec419420c20e320ce94c65fbc8c3312448eb225\n",
        );
        let (r, out, err) = run_in(&sc, &["-c", "ok.lst"], "");
        assert_eq!(r, 0);
        assert_eq!(out, "ck: OK\n");
        assert!(err.is_empty());
        make_list(
            &sc,
            "bad.lst",
            "SHA256 (ck) = 0000000000000000000000000000000000000000000000000000000000000000\n",
        );
        let (r, out, err) = run_in(&sc, &["-c", "bad.lst"], "");
        assert_eq!(r, 1);
        assert_eq!(out, "ck: FAILED\n");
        assert!(err.contains("cksum: WARNING: 1 computed checksum did NOT match"));
    }

    #[test]
    fn check_crc_and_untagged_rejected() {
        // probed k22/case-7/case-8: legacy formats never parse
        let sc = ScratchCtx::new("cksum-check2");
        std::fs::write(sc.path("ck"), "123456789").unwrap();
        make_list(&sc, "c.lst", "930766865 9 ck\n");
        let (r, _, err) = run_in(&sc, &["-c", "c.lst"], "");
        assert_eq!(r, 1);
        assert!(err.contains("no properly formatted checksum lines found"));
        make_list(
            &sc,
            "u.lst",
            "15e2b0d3c33891ebb0f1ef609ec419420c20e320ce94c65fbc8c3312448eb225  ck\n",
        );
        let (r, _, err) = run_in(&sc, &["-c", "u.lst"], "");
        assert_eq!(r, 1);
        assert!(err.contains("no properly formatted checksum lines found"));
        make_list(&sc, "g.lst", "garbage\n");
        let (r, _, err) = run_in(&sc, &["-c", "g.lst"], "");
        assert_eq!(r, 1);
        assert!(err.contains("no properly formatted checksum lines found"));
        // empty list file: same rejection (probed)
        make_list(&sc, "e.lst", "");
        let (r, _, err) = run_in(&sc, &["-c", "e.lst"], "");
        assert_eq!(r, 1);
        assert!(err.contains("no properly formatted checksum lines found"));
    }

    #[test]
    fn check_missing_and_flags() {
        // probed case 3/4/5/6: FAILED open or read + warnings; --status
        // silences stdout; --quiet keeps FAILED out; --ignore-missing with
        // all missing -> no file was verified rc 1
        let sc = ScratchCtx::new("cksum-check3");
        std::fs::write(sc.path("ck"), "123456789").unwrap();
        make_list(
            &sc,
            "m.lst",
            "SHA256 (gone) = 15e2b0d3c33891ebb0f1ef609ec419420c20e320ce94c65fbc8c3312448eb225\n",
        );
        let (r, out, err) = run_in(&sc, &["-c", "m.lst"], "");
        assert_eq!(r, 1);
        assert_eq!(out, "gone: FAILED open or read\n");
        assert!(err.contains("cksum: gone: No such file or directory"));
        assert!(err.contains("cksum: WARNING: 1 listed file could not be read"));
        let (r, out, err) = run_in(&sc, &["--status", "-c", "m.lst"], "");
        assert_eq!(r, 1);
        assert!(out.is_empty());
        assert!(err.is_empty());
        make_list(
            &sc,
            "ok.lst",
            "SHA256 (ck) = 15e2b0d3c33891ebb0f1ef609ec419420c20e320ce94c65fbc8c3312448eb225\n",
        );
        let (r, out, _) = run_in(&sc, &["--quiet", "-c", "ok.lst"], "");
        assert_eq!(r, 0);
        assert!(out.is_empty());
        let (r, _, err) = run_in(&sc, &["--ignore-missing", "-c", "m.lst"], "");
        assert_eq!(r, 1);
        assert!(err.contains("cksum: m.lst: no file was verified"));
    }

    #[test]
    fn check_accepts_base64_and_case_insensitive_hex() {
        // probed s16 (base64 tag OK) and s18 (uppercase hex parses)
        let sc = ScratchCtx::new("cksum-check4");
        std::fs::write(sc.path("ck"), "123456789").unwrap();
        make_list(
            &sc,
            "b64.lst",
            "SHA256 (ck) = FeKw08M4keuw8e9gnsQZQgwg4yDOlMZfvIwzEkSOsiU=\n",
        );
        let (r, out, _) = run_in(&sc, &["-c", "b64.lst"], "");
        assert_eq!(r, 0);
        assert_eq!(out, "ck: OK\n");
        make_list(
            &sc,
            "up.lst",
            "SHA256 (ck) = 15E2B0D3C33891EBB0F1EF609EC419420C20E320CE94C65FBC8C3312448EB225\n",
        );
        let (r, out, _) = run_in(&sc, &["-c", "up.lst"], "");
        assert_eq!(r, 0);
        assert_eq!(out, "ck: OK\n");
    }

    #[test]
    fn help_version_surface() {
        let (r, out, err) = run3(&["--help"], "");
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.starts_with("Usage: cksum [OPTION]... [FILE]...\n"));
        assert!(out.contains("  sysv      (equivalent to sum -s)\n"));
        assert!(out.ends_with("info '(coreutils) cksum invocation'\n"));
        let (r, out, _) = run3(&["--version"], "");
        assert_eq!(r, 0);
        assert!(out.starts_with("cksum (GNU coreutils) 9.4\n"));
        assert!(out.ends_with("Written by Pádraig Brady and Q. Frank Xia.\n"));
    }
}
