//! `cksum` — GNU coreutils cksum.c (9.4) semantics, probed byte-for-byte
//! against WSL coreutils 9.4 (rust2/target/probe/w5-cksum*.sh).
//!
//! Algorithms (probed `-a` surface): crc (the POSIX CRC-32/CKSUM default:
//! poly 0x04C11DB7 (normal form), register starting at 0, the byte count appended
//! little-endian — empty input answers 4294967295, "123456789" answers
//! 930766865), bsd/sysv (sum -r/-s shapes; stdin prints no name), and the
//! digest family whose cores already live in `digest.rs`: md5/sha1/sha224/
//! sha256/sha384/sha512/blake2b. sm3 has no core in this port and is still
//! rejected, though GNU's registration-order diagnostic lists it (documented
//! boundary; no gate row exercises sm3).
//!
//! BSD check lines (`src/digest.c:bsd_split_3`): the tag is matched
//! case-sensitively (`MD5`/`SHA1`/`SHA256`/... up to the `" ("`), so the
//! `MD5 (name) = hex` form GNU writes verifies here too (probed 2026-11-17).
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

/// The algorithms this port computes (sm3's core has not landed).
const ALGORITHMS: &[&str] = &[
    "bsd", "sysv", "crc", "md5", "sha1", "sha224", "sha256", "sha384", "sha512", "blake2b", "sm3",
];

#[derive(Clone, Copy, PartialEq)]
enum Algo {
    Crc,
    Bsd,
    Sysv,
    Md5,
    Sha1,
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
        "md5" => Some(Algo::Md5),
        "sha1" => Some(Algo::Sha1),
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
        Algo::Md5 => "MD5".to_string(),
        Algo::Sha1 => "SHA1".to_string(),
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
        Algo::Md5 => digest::md5(data).to_vec(),
        Algo::Sha1 => digest::sha1(data).to_vec(),
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
    binary: bool,
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
                    let marker = if binary { '*' } else { ' ' };
                    match name {
                        Some(n) => format!("{enc} {marker}{n}"),
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
                    let marker = if binary { '*' } else { ' ' };
                    match name {
                        Some(n) => format!("{hexd} {marker}{n}"),
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

/// Resolve GNU's sequential output-mode state: `-b`/`-t` are a last-wins
/// pair, `--tag`/`--untagged` likewise, and `--tag` also forces binary mode
/// (digest.c:main case TAG_OPTION `{ prefix_tag = true; binary = 1; }`).
fn output_mode(p: &argopt::Parsed) -> (bool, Option<bool>) {
    let mut prefix_tag = true; // cksum defaults to tagged output
    let mut binary: Option<bool> = None; // GNU's `int binary = -1`
    for key in &p.order {
        match key.as_str() {
            "binary" => binary = Some(true),
            "text" => binary = Some(false),
            "tag" => {
                prefix_tag = true;
                binary = Some(true);
            }
            "untagged" => prefix_tag = false,
            _ => {}
        }
    }
    (prefix_tag, binary)
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

    let explicit_algo = p.has("algorithm");
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

    let mut bits = algorithm_bits(algo);
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
            Ok(parsed) => bits = if parsed == 0 { 512 } else { parsed },
            Err(msgs) => {
                for m in msgs {
                    let _ = writeln!(io.stderr, "cksum: {m}");
                }
                return Ok(1);
            }
        }
    }

    let (prefix_tag, binary) = output_mode(&p);
    let fail = |io: &mut Io, msg: &str| -> u32 {
        let _ = winux_core::usage_error(ctx, io, "cksum", msg);
        1
    };

    // digest.c:main validation order: check/algorithm combinations, then the
    // base64/raw pair, then the text/tagged conflict, then verification-only
    // switches.
    let do_check = p.has("check");
    if do_check && explicit_algo && matches!(algo, Algo::Crc | Algo::Bsd | Algo::Sysv) {
        return Ok(fail(
            io,
            "--check is not supported with --algorithm={bsd,sysv,crc}",
        ));
    }
    if p.has("base64") && p.has("raw") {
        return Ok(fail(io, "--base64 and --raw are mutually exclusive"));
    }
    if prefix_tag && binary == Some(false) {
        return Ok(fail(io, "--text mode is only supported with --untagged"));
    }
    if do_check {
        if p.has("zero") {
            return Ok(fail(
                io,
                "the --zero option is not supported when verifying checksums",
            ));
        }
        if binary.is_some() {
            return Ok(fail(
                io,
                "the --binary and --text options are meaningless when verifying checksums",
            ));
        }
    } else {
        if p.has("ignore-missing") {
            return Ok(fail(
                io,
                "the --ignore-missing option is meaningful only when verifying checksums",
            ));
        }
        if p.has("status") {
            return Ok(fail(
                io,
                "the --status option is meaningful only when verifying checksums",
            ));
        }
        if p.has("warn") {
            return Ok(fail(
                io,
                "the --warn option is meaningful only when verifying checksums",
            ));
        }
        if p.has("quiet") {
            return Ok(fail(
                io,
                "the --quiet option is meaningful only when verifying checksums",
            ));
        }
        if p.has("strict") {
            return Ok(fail(
                io,
                "the --strict option is meaningful only when verifying checksums",
            ));
        }
    }

    let binary_mode = binary.unwrap_or(false);
    Ok(run_files(&p, io, ctx, algo, bits, prefix_tag, binary_mode))
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

fn run_files(
    p: &argopt::Parsed,
    io: &mut Io,
    ctx: &Ctx,
    algo: Algo,
    bits: usize,
    prefix_tag: bool,
    binary: bool,
) -> u32 {
    let untagged = !prefix_tag;
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
        emit_record(
            io, algo, bits, &bytes, name, untagged, binary, raw, base64, end,
        );
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
    crate::fsutil::open_read_operand(ctx, input)
        .map_err(|e| gnu_open_reason(ctx, input, &e, false))
        .and_then(|mut f| {
            let mut buf = Vec::new();
            f.read_to_end(&mut buf)
                .map_err(|_| "read error".to_string())?;
            Ok(buf)
        })
}

// ---- check mode (GNU digest.c 9.4: valid_digits, bsd_split_3, split_3,
// digest_check; probed 2026-11-17) ----

#[derive(Clone, Copy, PartialEq)]
enum DigestKind {
    Hex,
    Base64,
}

struct CheckLine {
    algo: Algo,
    bits: usize,
    name: String,
    digest: String,
    kind: DigestKind,
}

/// digest.c:algorithm_bits[] — the fixed width of each algorithm.  For
/// blake2b the table entry is 512; `-l` applies to untagged lines and the
/// `-BITS` suffix selects a width on a tagged line.
fn algorithm_bits(algo: Algo) -> usize {
    match algo {
        Algo::Crc => 32,
        Algo::Bsd | Algo::Sysv => 16,
        Algo::Md5 => 128,
        Algo::Sha1 => 160,
        Algo::Sha224 => 224,
        Algo::Sha256 => 256,
        Algo::Sha384 => 384,
        Algo::Sha512 => 512,
        Algo::Blake2b => 512,
    }
}

/// digest.c:algorithm_tags[] — the word printed in tagged output and used
/// as DIGEST_TYPE_STRING in the --warn malformed-line diagnostic.
fn algorithm_tag(algo: Algo) -> &'static str {
    match algo {
        Algo::Crc => "CRC",
        Algo::Bsd => "BSD",
        Algo::Sysv => "SYSV",
        Algo::Md5 => "MD5",
        Algo::Sha1 => "SHA1",
        Algo::Sha224 => "SHA224",
        Algo::Sha256 => "SHA256",
        Algo::Sha384 => "SHA384",
        Algo::Sha512 => "SHA512",
        Algo::Blake2b => "BLAKE2b",
    }
}

fn is_base64_byte(b: u8) -> bool {
    b.is_ascii_alphanumeric() || b == b'+' || b == b'/'
}

/// digest.c:valid_digits — the digest text must be exactly the hex spelling
/// (bits/4 characters) or exactly the padded base64 spelling
/// (BASE64_LENGTH(bits/8), with bits%3 trailing '=').  A wrong length or
/// padding makes the LINE malformed, not a mismatched digest: GNU 9.4
/// rejects the 43-character base64 body wholesale (#1752 clear-text probe).
fn valid_digest(text: &str, bits: usize) -> Option<DigestKind> {
    if bits == 0 || bits % 8 != 0 {
        return None;
    }
    let bytes = text.as_bytes();
    let b64_len = (bits / 8).div_ceil(3) * 4;
    if bytes.len() == b64_len {
        let pad = bits % 3;
        let body = b64_len - pad;
        if bytes[..body].iter().copied().all(is_base64_byte)
            && bytes[body..].iter().all(|&b| b == b'=')
        {
            return Some(DigestKind::Base64);
        }
    }
    if bytes.len() == bits / 4 && bytes.iter().all(u8::is_ascii_hexdigit) {
        return Some(DigestKind::Hex);
    }
    None
}

/// digest.c:bsd_split_3 — `(name) = digest`; the LAST ')' ends the name
/// (GNU allows ')' inside a file name), and blanks around '=' are skipped.
fn split_bsd(rest: &str, algo: Algo, bits: usize) -> Option<CheckLine> {
    let close = rest.rfind(')')?;
    let name = &rest[..close];
    let after = rest[close + 1..].trim_start_matches([' ', '\t']);
    let digest = after.strip_prefix('=')?.trim_start_matches([' ', '\t']);
    let kind = valid_digest(digest, bits)?;
    Some(CheckLine {
        algo,
        bits,
        name: name.to_string(),
        digest: digest.to_string(),
        kind,
    })
}

/// digest.c:split_3 tagged branch: `TAG (name) = digest` (BSD) or
/// `TAG(name) = digest` (OpenSSL), plus `TAG-<bits>` for blake2b.  The tag
/// is case-sensitive; `-a` fixes which tag is accepted.
fn parse_tagged(line: &str, algo: Algo, _configured_bits: usize) -> Option<CheckLine> {
    let tag = algorithm_tag(algo);
    let rest = line.strip_prefix(tag)?;
    let mut bits = algorithm_bits(algo);
    let rest = if let Some(after_dash) = rest.strip_prefix('-') {
        let end = after_dash
            .bytes()
            .position(|b| !b.is_ascii_digit())
            .unwrap_or(after_dash.len());
        let (digits, tail) = after_dash.split_at(end);
        if digits.is_empty() {
            return None;
        }
        let length: usize = digits.parse().ok()?;
        // digest.c:split_3 — 0 < length <= the algorithm's max and a
        // multiple of 8; fixed-width digests therefore only admit their
        // own width.
        if length == 0 || length > algorithm_bits(algo) || length % 8 != 0 {
            return None;
        }
        bits = length;
        tail
    } else {
        rest
    };
    let rest = rest.strip_prefix(' ').unwrap_or(rest);
    let inner = rest.strip_prefix('(')?;
    split_bsd(inner, algo, bits)
}

/// digest.c:algorithm_from_tag — at most 7 characters (the longest tag is
/// "BLAKE2b"), scanning stops at whitespace, '-' or '('.  The legacy
/// bsd/sysv/crc tags are rejected: those outputs have no checkable tagged
/// form (digest.c:split_3 `algo_tag <= crc`).
fn algorithm_from_tag(line: &str) -> Option<Algo> {
    let line = line.trim_start_matches([' ', '\t']);
    let end = line
        .find(|c: char| c.is_whitespace() || c == '-' || c == '(')
        .unwrap_or(line.len());
    if end > 7 {
        return None;
    }
    match &line[..end] {
        "MD5" => Some(Algo::Md5),
        "SHA1" => Some(Algo::Sha1),
        "SHA224" => Some(Algo::Sha224),
        "SHA256" => Some(Algo::Sha256),
        "SHA384" => Some(Algo::Sha384),
        "SHA512" => Some(Algo::Sha512),
        "BLAKE2b" => Some(Algo::Blake2b),
        _ => None,
    }
}

/// digest.c:split_3 non-tagged branch: `hex  name`, `hex *name`, and the
/// single-space "BSD reversed" form.  Only a preceding `-a ALGO` makes this
/// style legal (#1411 probe: without -a only tagged lines parse).
fn parse_untagged(
    line: &str,
    algo: Algo,
    bits: usize,
    bsd_reversed: &mut i32,
) -> Option<CheckLine> {
    let line = line.strip_prefix('\\').unwrap_or(line);
    let s = line.trim_start_matches([' ', '\t']);
    // digest.c:MIN_DIGEST_LINE_LENGTH is 3 for cksum (the -l 8 shape).
    if s.len() < 3 {
        return None;
    }
    let end = s
        .bytes()
        .position(|b| b == b' ' || b == b'\t')
        .unwrap_or(s.len());
    if end == s.len() {
        return None;
    }
    let digest = &s[..end];
    // digest.c auto-detects a blake2b untagged digest width from the
    // leading hex run; fixed-width algorithms use the selected width.
    let bits = if algo == Algo::Blake2b {
        let hex_count = digest.bytes().take_while(|b| b.is_ascii_hexdigit()).count();
        if hex_count < 2 || !hex_count.is_multiple_of(2) || hex_count > 128 {
            return None;
        }
        hex_count * 4
    } else {
        bits
    };
    let kind = valid_digest(digest, bits)?;
    let marker_at = end + 1;
    let marker = *s.as_bytes().get(marker_at)?;
    let name_start = if s.len() - marker_at == 1 || (marker != b' ' && marker != b'*') {
        // "bsd reversed": no marker column, and mixing styles is refused.
        if *bsd_reversed == 0 {
            return None;
        }
        *bsd_reversed = 1;
        marker_at
    } else if *bsd_reversed != 1 {
        *bsd_reversed = 0;
        marker_at + 1
    } else {
        marker_at
    };
    let name = s.get(name_start..)?;
    Some(CheckLine {
        algo,
        bits,
        name: name.to_string(),
        digest: digest.to_string(),
        kind,
    })
}

/// One check line, or None when the line is improperly formatted.  `current`
/// tracks DIGEST_TYPE_STRING for the --warn diagnostic: without `-a` each
/// recognized tag selects the algorithm for the rest of the line/file.
fn parse_check_line(
    line: &str,
    explicit: Option<Algo>,
    current: &mut Algo,
    configured_bits: usize,
    bsd_reversed: &mut i32,
) -> Option<CheckLine> {
    let is_explicit = explicit.is_some();
    let algo = match explicit {
        Some(algo) => algo,
        None => {
            let algo = algorithm_from_tag(line)?;
            *current = algo;
            algo
        }
    };
    if let Some(parsed) = parse_tagged(line, algo, configured_bits) {
        return Some(parsed);
    }
    // digest.c:split_3 non-tagged branch: `hex  name` is legal only when
    // -a selected the algorithm — an untagged line carries no tag for
    // algorithm_from_tag to detect (probe p-cksum-check.sh: `-a md5 -c`
    // verifies `hex  f` -> "f: OK"; bare `-c` rejects it).
    if is_explicit {
        if let Some(parsed) =
            parse_untagged(line, algo, configured_bits, bsd_reversed)
        {
            return Some(parsed);
        }
    }
    None
}

fn digest_matches(line: &CheckLine, bytes: &[u8]) -> bool {
    let computed = algo_digest(line.algo, bytes, line.bits);
    match line.kind {
        DigestKind::Hex => digest::hex(&computed).eq_ignore_ascii_case(&line.digest),
        DigestKind::Base64 => {
            let codec = b64core::base64_codec();
            b64core::encode(&codec, &computed, 0) == line.digest.as_bytes()
        }
    }
}

/// digest_file's ENOENT distinction: only a missing file is ignorable
/// under --ignore-missing (a permission error still fails).
fn read_check_target(ctx: &Ctx, io: &mut Io, name: &str) -> Result<Vec<u8>, (String, bool)> {
    match read_input(ctx, io, name) {
        Ok(bytes) => Ok(bytes),
        Err(reason) => {
            let missing = reason == "No such file or directory";
            Err((reason, missing))
        }
    }
}

fn check_files(p: &argopt::Parsed, io: &mut Io, ctx: &Ctx, algo: Algo, bits: usize) -> u32 {
    let explicit = p.has("algorithm");
    // GNU's four verification switches clear each other in argv order:
    // --status sets warn=quiet=false, --warn sets status=quiet=false,
    // --quiet sets status=warn=false (digest.c:main).
    let mut status = false;
    let mut quiet = false;
    let mut warn = false;
    let mut ignore_missing = false;
    let mut strict = false;
    for key in &p.order {
        match key.as_str() {
            "status" => {
                status = true;
                warn = false;
                quiet = false;
            }
            "warn" => {
                status = false;
                warn = true;
                quiet = false;
            }
            "quiet" => {
                status = false;
                warn = false;
                quiet = true;
            }
            "ignore-missing" => ignore_missing = true,
            "strict" => strict = true,
            _ => {}
        }
    }

    let inputs: Vec<String> = if p.positionals.is_empty() {
        vec!["-".to_string()]
    } else {
        p.positionals
            .iter()
            .map(|a| a.to_string_lossy().into_owned())
            .collect()
    };

    let mut ok = true;
    // digest.c:bsd_reversed is a file-scope static shared by all list files.
    let mut bsd_reversed = -1i32;
    for list in &inputs {
        let text = match read_input(ctx, io, list) {
            Ok(b) => String::from_utf8_lossy(&b).into_owned(),
            Err(reason) => {
                let _ = writeln!(io.stderr, "cksum: {}: {reason}", quotef(list));
                ok = false;
                continue;
            }
        };
        let mut current = if explicit { algo } else { Algo::Crc };
        let mut proper = false;
        let mut matched = false;
        let mut misformatted = 0usize;
        let mut mismatches = 0usize;
        let mut unreadable = 0usize;
        for (idx, line) in text.lines().enumerate() {
            // digest.c:digest_check ignores comment lines and empty lines;
            // they are not "improperly formatted".
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            let parsed = parse_check_line(
                line,
                if explicit { Some(algo) } else { None },
                &mut current,
                bits,
                &mut bsd_reversed,
            );
            let Some(cl) = parsed else {
                misformatted += 1;
                if warn {
                    let _ = writeln!(
                        io.stderr,
                        "cksum: {}: {}: improperly formatted {} checksum line",
                        quotef(list),
                        idx + 1,
                        algorithm_tag(current)
                    );
                }
                continue;
            };
            proper = true;
            match read_check_target(ctx, io, &cl.name) {
                Err((_, true)) if ignore_missing => {}
                Err((reason, _)) => {
                    unreadable += 1;
                    let _ = writeln!(io.stderr, "cksum: {}: {reason}", quotef(&cl.name));
                    if !status {
                        let _ = writeln!(io.stdout, "{}: FAILED open or read", cl.name);
                    }
                }
                Ok(bytes) => {
                    if digest_matches(&cl, &bytes) {
                        matched = true;
                        if !status && !quiet {
                            let _ = writeln!(io.stdout, "{}: OK", cl.name);
                        }
                    } else {
                        mismatches += 1;
                        if !status {
                            let _ = writeln!(io.stdout, "{}: FAILED", cl.name);
                        }
                    }
                }
            }
        }

        if !proper {
            // Not gated on --status in GNU: an unusable list always names
            // itself once.
            let _ = writeln!(
                io.stderr,
                "cksum: {}: no properly formatted checksum lines found",
                quotef(list)
            );
            ok = false;
            continue;
        }

        if !status {
            // digest.c:digest_check summary order: malformed, unreadable,
            // mismatched; then the --ignore-missing "no file was verified"
            // note.
            if misformatted != 0 {
                let _ = writeln!(
                    io.stderr,
                    "cksum: WARNING: {} line{} {} improperly formatted",
                    misformatted,
                    if misformatted == 1 { "" } else { "s" },
                    if misformatted == 1 { "is" } else { "are" }
                );
            }
            if unreadable != 0 {
                let _ = writeln!(
                    io.stderr,
                    "cksum: WARNING: {} listed file{} could not be read",
                    unreadable,
                    if unreadable == 1 { "" } else { "s" }
                );
            }
            if mismatches != 0 {
                let _ = writeln!(
                    io.stderr,
                    "cksum: WARNING: {} computed checksum{} did NOT match",
                    mismatches,
                    if mismatches == 1 { "" } else { "s" }
                );
            }
            if ignore_missing && !matched {
                let _ = writeln!(io.stderr, "cksum: {}: no file was verified", quotef(list));
            }
        }

        let file_ok =
            matched && mismatches == 0 && unreadable == 0 && (!strict || misformatted == 0);
        if !file_ok {
            ok = false;
        }
    }
    if ok {
        0
    } else {
        1
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
        // diagnostic; sm3 has no core yet, so it stays in the rejected set
        for a in ["bogus", "CRC", "s", "sm3"] {
            let (r, out, err) = run3(&["-a", a], "123456789");
            assert_eq!(r, 1, "{a}");
            assert!(out.is_empty());
            assert!(err.contains(&format!("invalid argument '{a}' for '--algorithm'")));
            assert!(err.contains("- 'sha256'"));
        }
    }

    #[test]
    fn md5_sha1_wired_forms() {
        // probed 2026-11-17: cksum -a md5 / -a sha1 print the BSD tag form,
        // --untagged the hex + two spaces form (digest.c, digest_check).
        let (r, out, _) = run3(&["-a", "md5"], "abc");
        assert_eq!(r, 0);
        assert_eq!(out, "MD5 (-) = 900150983cd24fb0d6963f7d28e17f72\n");
        let (r, out, _) = run3(&["-a", "sha1"], "abc");
        assert_eq!(r, 0);
        assert_eq!(out, "SHA1 (-) = a9993e364706816aba3e25717850c26c9cd0d89d\n");
        let (_, out, _) = run3(&["-a", "md5", "--untagged"], "abc");
        assert_eq!(out, "900150983cd24fb0d6963f7d28e17f72  -\n");
        let (_, out, _) = run3(&["-a", "md5", "--base64"], "abc");
        assert_eq!(out, "MD5 (-) = kAFQmDzST7DWlj99KOF/cg==\n");
    }

    #[test]
    fn check_bsd_md5_sha1_tags() {
        // probed 2026-11-17: `MD5 (name) = hex` and `SHA1 (name) = hex`
        // verify (digest.c:bsd_split_3); the tag is case-sensitive and the
        // hex is case-insensitive.
        let sc = ScratchCtx::new("cksum-check-md5");
        std::fs::write(sc.path("ck"), "abc").unwrap();
        make_list(
            &sc,
            "md5.lst",
            "MD5 (ck) = 900150983cd24fb0d6963f7d28e17f72\n",
        );
        let (r, out, _) = run_in(&sc, &["-c", "md5.lst"], "");
        assert_eq!(r, 0);
        assert_eq!(out, "ck: OK\n");
        make_list(
            &sc,
            "sha1.lst",
            "SHA1 (ck) = A9993E364706816ABA3E25717850C26C9CD0D89D\n",
        );
        let (r, out, _) = run_in(&sc, &["-c", "sha1.lst"], "");
        assert_eq!(r, 0);
        assert_eq!(out, "ck: OK\n");
        make_list(
            &sc,
            "md5bad.lst",
            "MD5 (ck) = 00000000000000000000000000000000\n",
        );
        let (r, out, err) = run_in(&sc, &["-c", "md5bad.lst"], "");
        assert_eq!(r, 1);
        assert_eq!(out, "ck: FAILED\n");
        assert!(err.contains("WARNING: 1 computed checksum did NOT match"));
        // case-sensitive tag: mixed case is not a checksum line at all
        make_list(
            &sc,
            "md5low.lst",
            "md5 (ck) = 900150983cd24fb0d6963f7d28e17f72\n",
        );
        let (r, _, err) = run_in(&sc, &["-c", "md5low.lst"], "");
        assert_eq!(r, 1);
        assert!(err.contains("no properly formatted checksum lines found"));
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
    fn check_base64_roundtrip_issue1752() {
        // [--base64 output must verify]: GNU 9.4 accepts a tagged line
        // whose digest is base64 (probed ck46).
        let sc = ScratchCtx::new("cksum-check-b64");
        std::fs::write(sc.path("ck9"), "123456789").unwrap();
        let (r, out, _) = run_in(&sc, &["--base64", "-a", "sha256", "ck9"], "");
        assert_eq!(r, 0);
        assert!(out.contains("SHA256 (ck9) = FeKw08M4keuw8e9gnsQZQgwg4yDOlMZfvIwzEkSOsiU="));
        make_list(&sc, "b64.lst", &out);
        let (r, out, err) = run_in(&sc, &["-c", "b64.lst"], "");
        assert_eq!(r, 0, "{err}");
        assert_eq!(out, "ck9: OK\n");
        assert!(err.is_empty(), "{err}");
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
        // GNU 9.4 digest_file still reports the failed open on stderr under
        // --status (probed 2026-11-17: `cksum --status -c m.lst` prints
        // "cksum: gone: No such file or directory" and nothing else); the
        // old assertion that --status silences both streams was 1.x drift.
        assert_eq!(err, "cksum: gone: No such file or directory\n");
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

    // ---- 2026-11-17 parity probes (GNU coreutils 9.4, digest.c) ----

    #[test]
    fn check_malformed_summary_is_not_gated_on_warn() {
        // GNU digest.c:1294-1307: with at least one properly formatted line,
        // the "N line(s) are improperly formatted" summary prints unless
        // --status; --warn only adds the per-line diagnostic.
        let sc = ScratchCtx::new("cksum-malformed");
        std::fs::write(sc.path("good.txt"), "hello").unwrap();
        make_list(
            &sc,
            "mixed.lst",
            "bad-line\nMD5 (good.txt) = 5d41402abc4b2a76b9719d911017c592\n",
        );
        let (r, out, err) = run_in(&sc, &["-c", "mixed.lst"], "");
        assert_eq!(r, 0);
        assert_eq!(out, "good.txt: OK\n");
        assert_eq!(err, "cksum: WARNING: 1 line is improperly formatted\n");

        let (r, out, err) = run_in(&sc, &["--quiet", "-c", "mixed.lst"], "");
        assert_eq!(r, 0);
        assert!(out.is_empty());
        assert_eq!(err, "cksum: WARNING: 1 line is improperly formatted\n");

        // --status suppresses the summary (and stays silent otherwise).
        let (r, out, err) = run_in(&sc, &["--status", "-c", "mixed.lst"], "");
        assert_eq!(r, 0);
        assert!(out.is_empty());
        assert!(err.is_empty());

        // Plural form and the "summary still prints under --strict" rule.
        make_list(
            &sc,
            "mixed2.lst",
            "bad-line\nbad-two\nMD5 (good.txt) = 5d41402abc4b2a76b9719d911017c592\n",
        );
        let (r, out, err) = run_in(&sc, &["--strict", "-c", "mixed2.lst"], "");
        assert_eq!(r, 1);
        assert_eq!(out, "good.txt: OK\n");
        assert_eq!(err, "cksum: WARNING: 2 lines are improperly formatted\n");

        // Comment and empty lines are skipped, not counted as malformed.
        make_list(
            &sc,
            "comments.lst",
            "# note\n\nMD5 (good.txt) = 5d41402abc4b2a76b9719d911017c592\n",
        );
        let (r, out, err) = run_in(&sc, &["-c", "comments.lst"], "");
        assert_eq!(r, 0);
        assert_eq!(out, "good.txt: OK\n");
        assert!(err.is_empty());
    }

    #[test]
    fn check_warn_names_algorithm_in_force() {
        // digest.c:1209 prints DIGEST_TYPE_STRING: CRC by default, the
        // selected algorithm under -a.  With -a sha256 the MD5-tagged line is
        // malformed too (split_3 only accepts the tag in force).
        let sc = ScratchCtx::new("cksum-warn-tag");
        std::fs::write(sc.path("good.txt"), "hello").unwrap();
        make_list(
            &sc,
            "mixed.lst",
            "bad-line\nMD5 (good.txt) = 5d41402abc4b2a76b9719d911017c592\n",
        );
        let (r, out, err) = run_in(&sc, &["--warn", "-c", "mixed.lst"], "");
        assert_eq!(r, 0);
        assert_eq!(out, "good.txt: OK\n");
        assert_eq!(
            err,
            "cksum: mixed.lst: 1: improperly formatted CRC checksum line\n\
             cksum: WARNING: 1 line is improperly formatted\n"
        );

        let (_, _, err) = run_in(&sc, &["--warn", "-a", "md5", "-c", "mixed.lst"], "");
        assert!(err.contains("cksum: mixed.lst: 1: improperly formatted MD5 checksum line"));

        let (r, out, err) = run_in(&sc, &["--warn", "-a", "sha256", "-c", "mixed.lst"], "");
        assert_eq!(r, 1);
        assert!(out.is_empty());
        assert_eq!(
            err,
            "cksum: mixed.lst: 1: improperly formatted SHA256 checksum line\n\
             cksum: mixed.lst: 2: improperly formatted SHA256 checksum line\n\
             cksum: mixed.lst: no properly formatted checksum lines found\n"
        );
    }


    #[test]
    fn check_base64_digest_validation_issue1752() {
        // valid_digits: base64 must have exactly BASE64_LENGTH bytes with
        // bits%3 trailing '='.  Wrong padding is malformed (#1752); a
        // well-formed but wrong digest is a FAILED record.
        let sc = ScratchCtx::new("cksum-b64-validate");
        std::fs::write(sc.path("ck"), "hello").unwrap();
        make_list(
            &sc,
            "ok.lst",
            "SHA256 (ck) = LPJNul+wow4m6DsqxbninhsWHlwfp0JecwQzYpOLmCQ=\n",
        );
        let (r, out, err) = run_in(&sc, &["-c", "ok.lst"], "");
        assert_eq!(r, 0);
        assert_eq!(out, "ck: OK\n");
        assert!(err.is_empty());

        make_list(
            &sc,
            "pad.lst",
            "SHA256 (ck) = LPJNul+wow4m6DsqxbninhsWHlwfp0JecwQzYpOLmCQX\n",
        );
        let (r, out, err) = run_in(&sc, &["-c", "pad.lst"], "");
        assert_eq!(r, 1);
        assert!(out.is_empty());
        assert_eq!(
            err,
            "cksum: pad.lst: no properly formatted checksum lines found\n"
        );

        make_list(
            &sc,
            "tamper.lst",
            "SHA256 (ck) = XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX=\n",
        );
        let (r, out, err) = run_in(&sc, &["-c", "tamper.lst"], "");
        assert_eq!(r, 1);
        assert_eq!(out, "ck: FAILED\n");
        assert!(err.contains("WARNING: 1 computed checksum did NOT match"));
    }

    #[test]
    fn output_mode_last_wins_issue1362() {
        // -b/-t are last-wins; --tag also forces binary mode; -t with the
        // default tagged output is refused (digest.c:1416-1450,1520-1531).
        let tag = "6137cde4893c59f76f005a8123d8e8e6";
        let (r, out, _) = run3(&["-a", "md5", "-t", "-b"], "data\n");
        assert_eq!(r, 0);
        assert_eq!(out, format!("MD5 (-) = {tag}\n"));

        let (r, out, _) = run3(&["-a", "md5", "--untagged", "-t", "-b"], "data\n");
        assert_eq!(r, 0);
        assert_eq!(out, format!("{tag} *-\n"));

        let (r, out, _) = run3(&["-a", "md5", "--untagged", "-b", "-t"], "data\n");
        assert_eq!(r, 0);
        assert_eq!(out, format!("{tag}  -\n"));

        let (r, out, err) = run3(&["-a", "md5", "-b", "-t"], "data\n");
        assert_eq!(r, 1);
        assert!(out.is_empty());
        assert_eq!(
            err,
            "cksum: --text mode is only supported with --untagged\n\
             Try 'cksum --help' for more information.\n"
        );

        // --tag leaves binary=1 behind, so a later --untagged prints '*'
        // (probed `cksum -a md5 --tag --untagged f`).
        let (r, out, _) = run3(&["-a", "md5", "--tag", "--untagged"], "data\n");
        assert_eq!(r, 0);
        assert_eq!(out, format!("{tag} *-\n"));

        let (r, out, _) = run3(&["-a", "md5", "--untagged", "--tag"], "data\n");
        assert_eq!(r, 0);
        assert_eq!(out, format!("MD5 (-) = {tag}\n"));

        // CRC ignores the marker, but -t still conflicts with tagged mode.
        let (r, out, _) = run3(&["--untagged", "-t"], "data\n");
        assert_eq!(r, 0);
        assert_eq!(out, "2797087131 5\n");
        let (r, _, err) = run3(&["-t"], "data\n");
        assert_eq!(r, 1);
        assert!(err.contains("--text mode is only supported with --untagged"));
    }

    #[test]
    fn check_switch_order_matches_gnu() {
        // --status/--warn/--quiet clear each other in argv order.
        let sc = ScratchCtx::new("cksum-switch-order");
        std::fs::write(sc.path("good.txt"), "hello").unwrap();
        make_list(
            &sc,
            "mixed.lst",
            "bad-line\nMD5 (good.txt) = 5d41402abc4b2a76b9719d911017c592\n",
        );
        let (r, out, err) = run_in(&sc, &["--status", "--warn", "-c", "mixed.lst"], "");
        assert_eq!(r, 0);
        assert_eq!(out, "good.txt: OK\n");
        assert!(err.contains("improperly formatted CRC checksum line"));

        let (r, out, err) = run_in(&sc, &["--warn", "--status", "-c", "mixed.lst"], "");
        assert_eq!(r, 0);
        assert!(out.is_empty());
        assert!(err.is_empty());
    }

    #[test]
    fn verification_only_options_require_check() {
        // digest.c:main rejects these switches without -c, with the same
        // "Try ... --help" trailer argopt emits for option errors.
        for (args, needle) in [
            (
                &["--warn"][..],
                "the --warn option is meaningful only when verifying checksums",
            ),
            (
                &["--quiet"][..],
                "the --quiet option is meaningful only when verifying checksums",
            ),
            (
                &["--status"][..],
                "the --status option is meaningful only when verifying checksums",
            ),
            (
                &["--strict"][..],
                "the --strict option is meaningful only when verifying checksums",
            ),
            (
                &["--ignore-missing"][..],
                "the --ignore-missing option is meaningful only when verifying checksums",
            ),
        ] {
            let (r, out, err) = run3(args, "");
            assert_eq!(r, 1, "{args:?}");
            assert!(out.is_empty());
            assert!(err.contains(needle), "{err}");
            assert!(err.contains("Try 'cksum --help' for more information."));
        }

        let sc = ScratchCtx::new("cksum-verify-errors");
        std::fs::write(sc.path("f"), "data\n").unwrap();
        make_list(&sc, "l.lst", "MD5 (f) = 6137cde4893c59f76f005a8123d8e8e6\n");
        let (r, _, err) = run_in(&sc, &["-c", "-b", "l.lst"], "");
        assert_eq!(r, 1);
        assert!(err
            .contains("the --binary and --text options are meaningless when verifying checksums"));
        let (r, _, err) = run_in(&sc, &["-c", "-t", "l.lst"], "");
        assert_eq!(r, 1);
        assert!(err.contains("--text mode is only supported with --untagged"));
        let (r, _, err) = run_in(&sc, &["-c", "-z", "l.lst"], "");
        assert_eq!(r, 1);
        assert!(err.contains("the --zero option is not supported when verifying checksums"));
        let (r, _, err) = run_in(&sc, &["-a", "crc", "-c", "l.lst"], "");
        assert_eq!(r, 1);
        assert!(err.contains("--check is not supported with --algorithm={bsd,sysv,crc}"));
        let (r, _, err) = run3(&["--base64", "--raw", "f"], "");
        assert_eq!(r, 1);
        assert!(err.contains("--base64 and --raw are mutually exclusive"));
    }
}
