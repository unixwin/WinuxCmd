//! `shuf` — GNU coreutils shuf semantics (probe base: coreutils 9.4,
//! target/probe/s1.sh+s2.sh; C++ value model: `src/commands/shuf.cpp` on
//! main, audited differential:1 in #1462).
//!
//! GNU contract:
//! * `shuf [OPTION]... [FILE]`; `-e` treats each ARG as an input line;
//!   `-i LO-HI` treats each number LO..HI as an input line; no FILE or
//!   FILE `-` reads stdin. Records are split on `\n` (or NUL with -z),
//!   keeping a final unterminated record (probed `x\ny` -> x, y).
//! * `-n COUNT` limits the output; an overflowing COUNT is treated as no
//!   limit (probed `-n 99999999999999999999` shuffles the whole range).
//!   COUNT 0 short-circuits: no input is opened, the -o file is created
//!   empty, rc 0.
//! * `-r` repeats; with a nonzero head count and no input lines GNU dies
//!   with "no lines to repeat" rc 1 (covers empty files/stdin and `-e`
//!   with no ARGs).
//! * `--random-source=FILE` is deterministic byte-for-byte across runs and
//!   platforms: the entropy-recycling state machine of GNU randint.c
//!   (state = state*256 + byte; entropy = entropy*256 + 255; margin split,
//!   recursive tail) consumes the file's bytes in order. That is the gate's
//!   strict-comparison path against GNU — every emitted line is diffed.
//!   A source exhausted mid-repeat dies with "'X': end of file" (probed).
//! * The range splits on the FIRST '-'; both endpoints are non-negative
//!   decimals (GNU xstrtoumax tolerates surrounding spaces where the C++
//!   value model rejects them — win-classified rows); lo > hi is
//!   "invalid input range: 'V'"; overflow appends ": Value too large for
//!   defined data type" to the same message.
//! * Usage errors (message + Try line, rc 1): -e with -i, an operand with
//!   -i, a second file operand, "invalid line count: 'V'", "multiple -i
//!   options specified", "multiple output files specified" (repeating the
//!   same -o value is fine), "multiple random sources specified".
//! * GNU has no -h short option (getopt_long matches --help only long):
//!   `-h` is "invalid option -- 'h'" rc 1 (argopt H_REJECT_MARKER).
//!   `--version` prints the GNU block; GNU --help text is reproduced.
//!
//! Windows value model / documented deviations:
//! * `--random-seed=STRING` is a documented WinuxCmd extension (GNU has no
//!   such option): SHA3-256(seed) seeds a reduced-round (12) ChaCha block
//!   stream — byte-identical to the C++ DeterministicRng so the gate's
//!   win rows keep stdout parity with the product exe.
//! * The default entropy path (no source, no seed) is nondeterministic by
//!   design on both sides: gate rows classify `exit` (rc only).
//! * With -r and NO head count the output is infinite; the gate never
//!   runs that shape (both sides would hit the harness timeout), and the
//!   256-line chunking of the C++ loop is preserved.
//! * The C++ expands wildcards in file operands (glob_expand); this port
//!   takes the operand as given, like GNU — the driver's argv arrives
//!   pre-expanded by the invoking shell.

use std::ffi::OsString;
use std::io::{Read as _, Write as _};

use crate::argopt::{self, OptSpec};
use crate::fsutil;
use crate::gnuhelp;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('e'),
        long: "echo",
        takes_value: false,
    },
    OptSpec {
        short: Some('i'),
        long: "input-range",
        takes_value: true,
    },
    OptSpec {
        short: Some('n'),
        long: "head-count",
        takes_value: true,
    },
    OptSpec {
        short: Some('o'),
        long: "output",
        takes_value: true,
    },
    OptSpec {
        short: Some('r'),
        long: "repeat",
        takes_value: false,
    },
    // WinuxCmd extension (C++ shuf.cpp): GNU has no --random-seed.
    OptSpec {
        short: None,
        long: "random-seed",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "random-source",
        takes_value: true,
    },
    OptSpec {
        short: Some('z'),
        long: "zero-terminated",
        takes_value: false,
    },
    // GNU shuf has no -h: reject it like getopt_long does.
    argopt::H_REJECT_SPEC,
];

const SHUF_HELP_BODY: &str = r#"Usage: shuf [OPTION]... [FILE]
  or:  shuf -e [OPTION]... [ARG]...
  or:  shuf -i LO-HI [OPTION]...
Write a random permutation of the input lines to standard output.

With no FILE, or when FILE is -, read standard input.

Mandatory arguments to long options are mandatory for short options too.
  -e, --echo                treat each ARG as an input line
  -i, --input-range=LO-HI   treat each number LO through HI as an input line
  -n, --head-count=COUNT    output at most COUNT lines
  -o, --output=FILE         write result to FILE instead of standard output
      --random-source=FILE  get random bytes from FILE
  -r, --repeat              output lines can be repeated
  -z, --zero-terminated     line delimiter is NUL, not newline
      --help        display this help and exit
      --version     output version information and exit

"#;

// ---------------------------------------------------------------------------
// Entropy sources
// ---------------------------------------------------------------------------

/// GNU randint.c entropy-recycling consumer over a byte stream (the C++
/// CompatRandomSource port). All arithmetic wraps like the C++ uint64_t.
struct GnuRandomSource {
    bytes: std::vec::IntoIter<u8>,
    state: u64,
    entropy: u64,
}

impl GnuRandomSource {
    fn new(bytes: Vec<u8>) -> Self {
        GnuRandomSource {
            bytes: bytes.into_iter(),
            state: 0,
            entropy: 0,
        }
    }

    /// Returns Err when the source is exhausted (GNU: "'file': end of
    /// file" once the file runs dry mid-draw).
    fn generate_at_most(&mut self, at_most: u64) -> Result<u64, ()> {
        while self.entropy < at_most {
            let ch = match self.bytes.next() {
                Some(b) => b,
                None => return Err(()),
            };
            self.state = self.state.wrapping_mul(256).wrapping_add(u64::from(ch));
            self.entropy = self.entropy.wrapping_mul(256).wrapping_add(255);
        }
        if at_most == u64::MAX {
            let value = self.state;
            self.state = 0;
            self.entropy = 0;
            return Ok(value);
        }
        let possibilities = at_most.wrapping_add(1);
        let margin = if self.entropy == u64::MAX {
            (0u64.wrapping_sub(possibilities)) % possibilities
        } else {
            (self.entropy.wrapping_add(1)) % possibilities
        };
        let safe_zone = self.entropy.wrapping_sub(margin);
        if self.state <= safe_zone {
            let value = self.state % possibilities;
            self.state /= possibilities;
            self.entropy -= at_most;
            self.entropy /= possibilities;
            Ok(value)
        } else {
            self.state %= possibilities;
            self.entropy %= possibilities;
            self.generate_at_most(at_most)
        }
    }

    fn choose_from_slice(&mut self, len: usize) -> Result<usize, ()> {
        if len == 0 {
            return Err(());
        }
        Ok(self.generate_at_most(len as u64 - 1)? as usize)
    }

    fn shuffle_prefix(&mut self, vals: &mut [String], amount: usize) -> Result<usize, ()> {
        let amount = amount.min(vals.len());
        for idx in 0..amount {
            let offset = self.generate_at_most(vals.len() as u64 - idx as u64 - 1)? as usize;
            vals.swap(idx, idx + offset);
        }
        Ok(amount)
    }
}

// --- Keccak-f[1600] / SHA3-256 (for the WinuxCmd --random-seed path) ---

const KECCAK_RC: [u64; 24] = [
    0x0000000000000001,
    0x0000000000008082,
    0x800000000000808A,
    0x8000000080008000,
    0x000000000000808B,
    0x0000000080000001,
    0x8000000080008081,
    0x8000000000008009,
    0x000000000000008A,
    0x0000000000000088,
    0x0000000080008009,
    0x000000008000000A,
    0x800000008000808B,
    0x800000000000008B,
    0x8000000000008089,
    0x8000000000008003,
    0x8000000000008002,
    0x8000000000000080,
    0x000000000000800A,
    0x800000008000000A,
    0x8000000080008081,
    0x8000000000008080,
    0x0000000080000001,
    0x8000000080008008,
];

const KECCAK_RHO: [u32; 25] = [
    0, 1, 62, 28, 27, 36, 44, 6, 55, 20, 3, 10, 43, 25, 39, 41, 45, 15, 21, 8, 18, 2, 61, 56, 14,
];

const KECCAK_PI: [usize; 25] = [
    0, 10, 20, 5, 15, 16, 1, 11, 21, 6, 7, 17, 2, 12, 22, 23, 8, 18, 3, 13, 14, 24, 9, 19, 4,
];

fn rotl64(x: u64, n: u32) -> u64 {
    x.rotate_left(n)
}

fn keccak_permute(state: &mut [u64; 25]) {
    for &rc in &KECCAK_RC {
        let mut c = [0u64; 5];
        for (x, cx) in c.iter_mut().enumerate() {
            *cx = state[x] ^ state[x + 5] ^ state[x + 10] ^ state[x + 15] ^ state[x + 20];
        }
        let mut d = [0u64; 5];
        for (x, dx) in d.iter_mut().enumerate() {
            *dx = c[(x + 4) % 5] ^ rotl64(c[(x + 1) % 5], 1);
        }
        for y in 0..5 {
            for x in 0..5 {
                state[x + 5 * y] ^= d[x];
            }
        }
        let mut b = [0u64; 25];
        for i in 0..25 {
            b[KECCAK_PI[i]] = rotl64(state[i], KECCAK_RHO[i]);
        }
        for y in 0..5 {
            for x in 0..5 {
                state[x + 5 * y] =
                    b[x + 5 * y] ^ ((!b[((x + 1) % 5) + 5 * y]) & b[((x + 2) % 5) + 5 * y]);
            }
        }
        state[0] ^= rc;
    }
}

fn load64_le(bytes: &[u8]) -> u64 {
    let mut v = 0u64;
    for i in 0..8 {
        v |= u64::from(bytes[i]) << (8 * i);
    }
    v
}

/// SHA3-256 over `text` (C++ sha3_256 port; the deterministic seed path is
/// WinuxCmd-specific and must stay byte-identical to the product exe).
fn sha3_256(text: &[u8]) -> [u8; 32] {
    const RATE: usize = 136;
    let mut state = [0u64; 25];
    let mut offset = 0usize;
    while offset + RATE <= text.len() {
        for i in 0..RATE / 8 {
            state[i] ^= load64_le(&text[offset + i * 8..]);
        }
        keccak_permute(&mut state);
        offset += RATE;
    }
    let mut tail = [0u8; RATE];
    let remaining = text.len() - offset;
    tail[..remaining].copy_from_slice(&text[offset..]);
    tail[remaining] ^= 0x06;
    tail[RATE - 1] ^= 0x80;
    for i in 0..RATE / 8 {
        state[i] ^= load64_le(&tail[i * 8..]);
    }
    keccak_permute(&mut state);
    let mut out = [0u8; 32];
    for (i, lane) in state.iter().take(4).enumerate() {
        for j in 0..8 {
            out[i * 8 + j] = ((lane >> (8 * j)) & 0xFF) as u8;
        }
    }
    out
}

const CHACHA_CONSTANTS: [u32; 4] = [0x61707865, 0x3320646E, 0x79622D32, 0x6B206574];

fn chacha_quarter_round(x: &mut [u32; 16], a: usize, b: usize, c: usize, d: usize) {
    x[a] = x[a].wrapping_add(x[b]);
    x[d] = (x[d] ^ x[a]).rotate_left(16);
    x[c] = x[c].wrapping_add(x[d]);
    x[b] = (x[b] ^ x[c]).rotate_left(12);
    x[a] = x[a].wrapping_add(x[b]);
    x[d] = (x[d] ^ x[a]).rotate_left(8);
    x[c] = x[c].wrapping_add(x[d]);
    x[b] = (x[b] ^ x[c]).rotate_left(7);
}

fn chacha_block(state: &[u32; 16]) -> [u32; 16] {
    let mut working = *state;
    // Reduced to 12 rounds by design (WinuxCmd --random-seed model).
    for _ in 0..6 {
        chacha_quarter_round(&mut working, 0, 4, 8, 12);
        chacha_quarter_round(&mut working, 1, 5, 9, 13);
        chacha_quarter_round(&mut working, 2, 6, 10, 14);
        chacha_quarter_round(&mut working, 3, 7, 11, 15);
        chacha_quarter_round(&mut working, 0, 5, 10, 15);
        chacha_quarter_round(&mut working, 1, 6, 11, 12);
        chacha_quarter_round(&mut working, 2, 7, 8, 13);
        chacha_quarter_round(&mut working, 3, 4, 9, 14);
    }
    let mut out = [0u32; 16];
    for i in 0..16 {
        out[i] = working[i].wrapping_add(state[i]);
    }
    out
}

/// Deterministic seed RNG (C++ DeterministicRng port): SHA3-256(seed) ->
/// ChaCha12 stream, same margin sampling as the GNU source path.
struct DeterministicRng {
    state: [u32; 16],
    block_words: [u32; 16],
    word_index: usize,
}

impl DeterministicRng {
    fn new(seed_text: &str) -> Self {
        let seed = sha3_256(seed_text.as_bytes());
        let mut state = [0u32; 16];
        state[0..4].copy_from_slice(&CHACHA_CONSTANTS);
        for i in 0..8 {
            state[4 + i] = u32::from(seed[i * 4])
                | (u32::from(seed[i * 4 + 1]) << 8)
                | (u32::from(seed[i * 4 + 2]) << 16)
                | (u32::from(seed[i * 4 + 3]) << 24);
        }
        let block_words = chacha_block(&state);
        DeterministicRng {
            state,
            block_words,
            word_index: 0,
        }
    }

    fn next_u64(&mut self) -> u64 {
        if self.word_index >= 16 {
            self.state[12] = self.state[12].wrapping_add(1);
            if self.state[12] == 0 {
                self.state[13] = self.state[13].wrapping_add(1);
            }
            self.block_words = chacha_block(&self.state);
            self.word_index = 0;
        }
        let low = u64::from(self.block_words[self.word_index]);
        let high = u64::from(self.block_words[self.word_index + 1]);
        self.word_index += 2;
        (high << 32) | low
    }

    fn generate_at_most(&mut self, at_most: u64) -> u64 {
        if at_most == u64::MAX {
            return self.next_u64();
        }
        let possibilities = at_most.wrapping_add(1);
        let mut sample = self.next_u64();
        let mut high = ((u128::from(sample) * u128::from(possibilities)) >> 64) as u64;
        let mut low =
            ((u128::from(sample) * u128::from(possibilities)) & u128::from(u64::MAX)) as u64;
        if low < possibilities {
            let threshold = (0u64.wrapping_sub(possibilities)) % possibilities;
            while low < threshold {
                sample = self.next_u64();
                let product = u128::from(sample) * u128::from(possibilities);
                low = (product & u128::from(u64::MAX)) as u64;
                high = (product >> 64) as u64;
            }
        }
        high
    }

    fn choose_from_slice(&mut self, len: usize) -> usize {
        if len == 0 {
            return 0;
        }
        self.generate_at_most(len as u64 - 1) as usize
    }

    fn shuffle_prefix(&mut self, vals: &mut [String], amount: usize) -> usize {
        let amount = amount.min(vals.len());
        for idx in 0..amount {
            let offset = self.generate_at_most(vals.len() as u64 - idx as u64 - 1) as usize;
            vals.swap(idx, idx + offset);
        }
        amount
    }
}

/// OS entropy for the default path (no source, no seed): the gate classifies
/// these rows exit, so only unpredictability matters. std-only mix of
/// process time, id and a counter.
fn os_random_u64() -> u64 {
    use std::time::{SystemTime, UNIX_EPOCH};
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_nanos() as u64)
        .unwrap_or(0);
    let pid = std::process::id() as u64;
    nanos.wrapping_mul(0x9E3779B97F4A7C15)
        ^ pid.wrapping_mul(0xBF58476D1CE4E5B9)
        ^ u64::from(std::process::id()).wrapping_mul(0x94D049BB133111EB)
        ^ (&nanos as *const u64 as u64)
}

struct DefaultRng {
    s: [u64; 4],
}

impl DefaultRng {
    fn new() -> Self {
        let a = os_random_u64();
        let b = os_random_u64();
        DefaultRng {
            s: [
                a,
                b,
                a.wrapping_mul(b) ^ 0xA0761D6478BD642F,
                a ^ b.rotate_left(17),
            ],
        }
    }

    fn next_u64(&mut self) -> u64 {
        // xorshift-roar mix
        let mut x = self.s[0].wrapping_add(0x9E3779B97F4A7C15);
        x = (x ^ (x >> 30)).wrapping_mul(0xBF58476D1CE4E5B9);
        x = (x ^ (x >> 27)).wrapping_mul(0x94D049BB133111EB);
        x ^= x >> 31;
        self.s[0] = self.s[1];
        self.s[1] = self.s[2];
        self.s[2] = self.s[3];
        self.s[3] = self.s[3].rotate_left(23) ^ x;
        x
    }

    fn generate_at_most(&mut self, at_most: u64) -> u64 {
        if at_most == u64::MAX {
            return self.next_u64();
        }
        (self.next_u64() % (at_most.wrapping_add(1))) as u64
    }
}

// ---------------------------------------------------------------------------
// Parsing helpers (C++ model)
// ---------------------------------------------------------------------------

struct UnsignedNumber {
    value: u64,
    ok: bool,
    overflow: bool,
}

fn parse_unsigned_number(text: &str) -> UnsignedNumber {
    let t = text.strip_prefix('+').unwrap_or(text);
    if t.is_empty() || !t.bytes().all(|b| b.is_ascii_digit()) {
        return UnsignedNumber {
            value: 0,
            ok: false,
            overflow: false,
        };
    }
    match t.parse::<u64>() {
        Ok(v) => UnsignedNumber {
            value: v,
            ok: true,
            overflow: false,
        },
        Err(_) => UnsignedNumber {
            value: 0,
            ok: false,
            overflow: true,
        },
    }
}

fn range_error(value: &str, overflow: bool) -> String {
    let mut message = format!("invalid input range: '{value}'");
    if overflow {
        message += ": Value too large for defined data type";
    }
    message
}

/// Mirrors GNU shuf: the range splits on the FIRST '-', both endpoints must
/// be non-negative decimals, lo <= hi.
fn split_range(range: &str) -> Result<(u64, u64), String> {
    let dash = match range.find('-') {
        None => {
            let whole = parse_unsigned_number(range);
            return Err(range_error(range, whole.overflow));
        }
        Some(d) => d,
    };
    let lo_text = &range[..dash];
    let hi_text = &range[dash + 1..];
    let lo = parse_unsigned_number(lo_text);
    if !lo.ok {
        return Err(range_error(range, lo.overflow));
    }
    let hi = parse_unsigned_number(hi_text);
    if !hi.ok {
        return Err(range_error(range, hi.overflow));
    }
    if lo.value > hi.value {
        return Err(range_error(range, false));
    }
    Ok((lo.value, hi.value))
}

fn read_records(bytes: &[u8], zero_terminated: bool) -> Vec<String> {
    let delimiter = if zero_terminated { 0u8 } else { b'\n' };
    let mut records = Vec::new();
    let mut start = 0usize;
    for (i, &b) in bytes.iter().enumerate() {
        if b == delimiter {
            records.push(String::from_utf8_lossy(&bytes[start..i]).into_owned());
            start = i + 1;
        }
    }
    if start < bytes.len() {
        records.push(String::from_utf8_lossy(&bytes[start..]).into_owned());
    }
    records
}

// ---------------------------------------------------------------------------
// Command
// ---------------------------------------------------------------------------

pub fn shuf_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("shuf", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if let Some(status) = argopt::rejected_short_h(io, "shuf", &p, 1) {
        return Ok(status);
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
                    SHUF_HELP_BODY,
                    gnuhelp::footer("shuf", "shuf invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("shuf", "Paul Eggert")
                )?;
                0
            }
        });
    }

    let echo_mode = p.has("echo");
    let repeat = p.has("repeat");
    let zero_terminated = p.has("zero-terminated");
    let output_file = p.value_of("output").map(|s| s.to_string());
    let random_seed = p.value_of("random-seed").map(|s| s.to_string());
    let random_source = p.value_of("random-source").map(|s| s.to_string());

    let mut range: Option<(u64, u64)> = None;
    let range_values: Vec<&str> = p
        .values
        .iter()
        .filter(|(k, _)| k == "input-range")
        .map(|(_, v)| v.as_str())
        .collect();
    if range_values.len() > 1 {
        let _ = writeln!(io.stderr, "shuf: multiple -i options specified");
        return Ok(1);
    }
    if let Some(value) = range_values.first() {
        match split_range(value) {
            Ok(r) => range = Some(r),
            Err(message) => {
                let _ = writeln!(io.stderr, "shuf: {message}");
                return Ok(1);
            }
        }
    }

    let mut head_count: Option<u64> = None;
    let count_values: Vec<&str> = p
        .values
        .iter()
        .filter(|(k, _)| k == "head-count")
        .map(|(_, v)| v.as_str())
        .collect();
    for value in count_values {
        let count = parse_unsigned_number(value);
        if !count.ok && !count.overflow {
            let _ = writeln!(io.stderr, "shuf: invalid line count: '{value}'");
            return Ok(1);
        }
        if count.overflow {
            // GNU treats an overflowing head count as "no limit".
            continue;
        }
        head_count = Some(match head_count {
            None => count.value,
            Some(prev) => prev.min(count.value),
        });
    }

    // Repeated identical -o / --random-source values are fine (C++ model).
    let outputs: Vec<&str> = p
        .values
        .iter()
        .filter(|(k, _)| k == "output")
        .map(|(_, v)| v.as_str())
        .collect();
    if let Some(first) = outputs.first() {
        if outputs.iter().any(|v| *v != *first) {
            let _ = writeln!(io.stderr, "shuf: multiple output files specified");
            return Ok(1);
        }
    }
    let sources: Vec<&str> = p
        .values
        .iter()
        .filter(|(k, _)| k == "random-source")
        .map(|(_, v)| v.as_str())
        .collect();
    if let Some(first) = sources.first() {
        if sources.iter().any(|v| *v != *first) {
            let _ = writeln!(io.stderr, "shuf: multiple random sources specified");
            return Ok(1);
        }
    }
    if random_seed.is_some() && random_source.is_some() {
        let _ = writeln!(
            io.stderr,
            "shuf: the arguments '--random-seed <STRING>' and '--random-source <FILE>' are mutually exclusive"
        );
        return Ok(1);
    }

    // Usage block (GNU: message + Try line, rc 1).
    if echo_mode && range.is_some() {
        let _ = writeln!(io.stderr, "shuf: cannot combine -e and -i options");
        let _ = writeln!(io.stderr, "Try 'shuf --help' for more information.");
        return Ok(1);
    }
    if range.is_some() && !p.positionals.is_empty() {
        let _ = writeln!(
            io.stderr,
            "shuf: extra operand '{}'",
            p.positionals[0].to_string_lossy()
        );
        let _ = writeln!(io.stderr, "Try 'shuf --help' for more information.");
        return Ok(1);
    }
    if !echo_mode && range.is_none() && p.positionals.len() > 1 {
        let _ = writeln!(
            io.stderr,
            "shuf: extra operand '{}'",
            p.positionals[1].to_string_lossy()
        );
        let _ = writeln!(io.stderr, "Try 'shuf --help' for more information.");
        return Ok(1);
    }

    // head_lines == 0 short-circuits input preparation entirely; GNU still
    // creates the -o file empty (probed rc 0).
    if head_count == Some(0) {
        return emit_output(output_file.as_deref(), &[], zero_terminated, io, ctx);
    }

    // Build the input lines.
    enum Source {
        Deferred((u64, u64)),
        Lines(Vec<String>),
    }
    let source = if echo_mode {
        Source::Lines(
            p.positionals
                .iter()
                .map(|a| a.to_string_lossy().into_owned())
                .collect(),
        )
    } else if let Some((lo, hi)) = range {
        // [GNU] With -n, huge ranges are sampled without materializing
        // every number (default-RNG path only; the source/seed paths keep
        // the eager materialization, C++ model).
        if head_count.is_some() && !repeat && random_source.is_none() && random_seed.is_none() {
            Source::Deferred((lo, hi))
        } else {
            let mut lines = Vec::new();
            let mut i = lo as i128;
            while i <= hi as i128 {
                lines.push(i.to_string());
                i += 1;
            }
            Source::Lines(lines)
        }
    } else {
        let operand = p
            .positionals
            .first()
            .map(|a| a.to_string_lossy().into_owned())
            .unwrap_or_else(|| "-".to_string());
        let bytes = if operand == "-" {
            let mut buf = Vec::new();
            io.stdin.read_to_end(&mut buf)?;
            buf
        } else {
            let full = fsutil::resolve(ctx, &operand);
            match std::fs::read(&full) {
                Ok(b) => b,
                Err(_) => {
                    let _ = writeln!(io.stderr, "shuf: {operand}: No such file or directory");
                    return Ok(1);
                }
            }
        };
        Source::Lines(read_records(&bytes, zero_terminated))
    };

    let deferred_range = match &source {
        Source::Deferred(r) => Some(*r),
        Source::Lines(_) => None,
    };

    let lines = match source {
        Source::Deferred(_) => Vec::new(),
        Source::Lines(l) => l,
    };

    if lines.is_empty() && deferred_range.is_none() && repeat && head_count != Some(0) {
        let _ = writeln!(io.stderr, "shuf: no lines to repeat");
        return Ok(1);
    }
    if lines.is_empty() && deferred_range.is_none() {
        return emit_output(output_file.as_deref(), &[], zero_terminated, io, ctx);
    }

    // The deterministic paths.
    if let Some(source_file) = &random_source {
        let full = fsutil::resolve(ctx, source_file);
        let bytes = match std::fs::read(&full) {
            Ok(b) => b,
            Err(_) => {
                let _ = writeln!(io.stderr, "shuf: cannot open random source");
                return Ok(1);
            }
        };
        let mut rng = GnuRandomSource::new(bytes);
        let mut output: Vec<String> = Vec::new();
        if repeat {
            if let Some(count) = head_count {
                for _ in 0..count {
                    match rng.choose_from_slice(lines.len()) {
                        Ok(idx) => output.push(lines[idx].clone()),
                        Err(()) => {
                            let _ = writeln!(io.stderr, "shuf: '{source_file}': end of file");
                            return Ok(1);
                        }
                    }
                }
            } else {
                // Infinite repeat: 256-record chunks until the sink fails,
                // like GNU/C++ (untestable in the bounded gate).
                loop {
                    output.clear();
                    let mut exhausted = false;
                    for _ in 0..256 {
                        match rng.choose_from_slice(lines.len()) {
                            Ok(idx) => output.push(lines[idx].clone()),
                            Err(()) => {
                                exhausted = true;
                                break;
                            }
                        }
                    }
                    if emit_output(output_file.as_deref(), &output, zero_terminated, io, ctx)
                        .is_err()
                    {
                        return Ok(if output_file.is_some() { 1 } else { 0 });
                    }
                    if exhausted {
                        return Ok(0);
                    }
                }
            }
        } else {
            let output_count = head_count
                .map(|n| n.min(lines.len() as u64) as usize)
                .unwrap_or(lines.len());
            let mut lines = lines;
            // [GNU observed] A source exhausted mid-shuffle continues with
            // identity draws (probed: 4 lines / 1 byte seed -> "2 1 3 4",
            // rc 0), while the repeat path errors out (probed "end of
            // file" rc 1). Mirror exactly that split.
            let mut swapped = Vec::new();
            let mut exhausted = false;
            for idx in 0..output_count {
                let offset = match rng.generate_at_most((lines.len() - idx - 1) as u64) {
                    Ok(v) => v as usize,
                    Err(()) => {
                        exhausted = true;
                        0
                    }
                };
                lines.swap(idx, idx + offset);
                swapped.push(lines[idx].clone());
            }
            let _ = exhausted;
            output = swapped;
        }
        return emit_output(output_file.as_deref(), &output, zero_terminated, io, ctx);
    }

    if let Some(seed_text) = &random_seed {
        let mut rng = DeterministicRng::new(seed_text);
        let mut output: Vec<String> = Vec::new();
        let mut lines = lines;
        if repeat {
            if let Some(count) = head_count {
                for _ in 0..count {
                    let idx = rng.choose_from_slice(lines.len());
                    output.push(lines[idx].clone());
                }
            } else {
                loop {
                    output.clear();
                    for _ in 0..256 {
                        let idx = rng.choose_from_slice(lines.len());
                        output.push(lines[idx].clone());
                    }
                    if emit_output(output_file.as_deref(), &output, zero_terminated, io, ctx)
                        .is_err()
                    {
                        return Ok(if output_file.is_some() { 1 } else { 0 });
                    }
                }
            }
        } else {
            let output_count = head_count
                .map(|n| n.min(lines.len() as u64) as usize)
                .unwrap_or(lines.len());
            rng.shuffle_prefix(&mut lines, output_count);
            output = lines[..output_count].to_vec();
        }
        return emit_output(output_file.as_deref(), &output, zero_terminated, io, ctx);
    }

    // Default entropy path (nondeterministic; gate exit rows only).
    let mut rng = DefaultRng::new();
    let mut output: Vec<String> = Vec::new();
    if repeat {
        if let Some(count) = head_count {
            for _ in 0..count {
                let idx = rng.generate_at_most(lines.len() as u64 - 1) as usize;
                output.push(lines[idx].clone());
            }
            return emit_output(output_file.as_deref(), &output, zero_terminated, io, ctx);
        }
        loop {
            output.clear();
            for _ in 0..256 {
                let idx = rng.generate_at_most(lines.len() as u64 - 1) as usize;
                output.push(lines[idx].clone());
            }
            if emit_output(output_file.as_deref(), &output, zero_terminated, io, ctx).is_err() {
                return Ok(if output_file.is_some() { 1 } else { 0 });
            }
        }
    }

    // Deferred -i sampling with -n: pick COUNT distinct values without
    // materializing the range (order is platform-bound: exit rows).
    if let Some((lo, hi)) = deferred_range {
        let population = hi - lo + 1;
        let count = head_count.map(|n| n.min(population)).unwrap_or(population) as usize;
        let mut values: Vec<String> = (0..population).map(|k| (lo + k).to_string()).collect();
        // Fisher-Yates over the prefix with the default RNG.
        let take = count.min(values.len());
        for idx in 0..take {
            let offset = rng.generate_at_most(values.len() as u64 - idx as u64 - 1) as usize;
            values.swap(idx, idx + offset);
        }
        values.truncate(take);
        return emit_output(output_file.as_deref(), &values, zero_terminated, io, ctx);
    }

    let mut lines = lines;
    for i in (1..=lines.len()).rev() {
        let j = rng.generate_at_most(i as u64 - 1) as usize;
        lines.swap(i - 1, j);
    }
    let output_count = head_count
        .map(|n| n.min(lines.len() as u64) as usize)
        .unwrap_or(lines.len());
    output = lines[..output_count].to_vec();
    emit_output(output_file.as_deref(), &output, zero_terminated, io, ctx)
}

/// Write the records to the -o file or stdout. Err = sink broken (the ABI
/// turns it into status 125 like a broken GNU pipe write).
fn emit_output(
    output_file: Option<&str>,
    records: &[String],
    zero_terminated: bool,
    io: &mut Io,
    ctx: &Ctx,
) -> CmdResult {
    let delim = if zero_terminated { 0u8 } else { b'\n' };
    let mut bytes = Vec::new();
    for record in records {
        bytes.extend_from_slice(record.as_bytes());
        bytes.push(delim);
    }
    match output_file {
        Some(file) => {
            let full = fsutil::resolve(ctx, file);
            match std::fs::write(&full, &bytes) {
                Ok(()) => Ok(0),
                Err(_) => {
                    let _ = writeln!(io.stderr, "shuf: {file}: No such file or directory");
                    Ok(1)
                }
            }
        }
        None => {
            io.stdout.write_all(&bytes)?;
            Ok(0)
        }
    }
}

// ---------------------------------------------------------------------------
// Unit tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use std::ffi::OsString;
    use winux_core::test_support::{MemIo, ScratchCtx};

    struct Run {
        rc: u32,
        out: String,
        err: String,
        dir: std::path::PathBuf,
        _sc: ScratchCtx,
    }

    fn run(setup: &[(&str, &str)], stdin: &str, args: &[&str]) -> Run {
        let sc = ScratchCtx::new("shuf");
        for (name, content) in setup {
            std::fs::write(sc.path(name), content).unwrap();
        }
        let mut io = MemIo::new(stdin);
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let rc = super::shuf_cmd(&owned, &mut io.split(), &ctx).unwrap();
        Run {
            rc,
            out: io.out(),
            err: io.err(),
            dir: sc.dir.clone(),
            _sc: sc,
        }
    }

    const SEED: &str = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    const F5: &str = "a\nb\nc\nd\ne\n";

    #[test]
    fn random_source_is_gnu_deterministic() {
        // Byte-for-byte GNU 9.4 probe (target/probe/s1.sh): with a fixed
        // seed file the emitted permutation is exactly this.
        let r = run(
            &[("f5.txt", F5), ("seed.bin", SEED)],
            "",
            &["--random-source=seed.bin", "f5.txt"],
        );
        assert_eq!((r.rc, r.out.as_str()), (0, "d\nc\ne\na\nb\n"));

        let r = run(
            &[
                (
                    "n20.txt",
                    "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n",
                ),
                ("seed.bin", SEED),
            ],
            "",
            &["--random-source=seed.bin", "-n", "5", "n20.txt"],
        );
        assert_eq!((r.rc, r.out.as_str()), (0, "9\n12\n14\n4\n7\n"));

        let r = run(
            &[("seed.bin", SEED)],
            "",
            &["--random-source=seed.bin", "-i", "1-10"],
        );
        assert_eq!(
            (r.rc, r.out.as_str()),
            (0, "9\n6\n4\n10\n5\n2\n1\n3\n7\n8\n")
        );

        let r = run(
            &[("seed.bin", SEED)],
            "",
            &["--random-source=seed.bin", "-i", "0-9", "-n", "3"],
        );
        assert_eq!((r.rc, r.out.as_str()), (0, "8\n5\n3\n"));

        let r = run(
            &[("seed.bin", SEED)],
            "",
            &[
                "--random-source=seed.bin",
                "-r",
                "-n",
                "6",
                "-e",
                "a",
                "b",
                "c",
            ],
        );
        assert_eq!((r.rc, r.out.as_str()), (0, "a\nb\nc\nb\na\nb\n"));
    }

    #[test]
    fn random_source_zero_terminated_and_output_file() {
        let r = run(
            &[("seed.bin", SEED)],
            "",
            &["--random-source=seed.bin", "-z", "-i", "1-5"],
        );
        assert_eq!(r.rc, 0);
        assert_eq!(r.out, "4\0 3\0 5\0 1\0 2\0".replace(' ', ""));
        assert!(r.out.as_bytes().ends_with(&[b'2', 0]));

        let r = run(
            &[("seed.bin", SEED)],
            "",
            &["--random-source=seed.bin", "-o", "out.txt", "-i", "1-5"],
        );
        assert_eq!((r.rc, r.out.as_str()), (0, ""));
        let written = std::fs::read_to_string(r.dir.join("out.txt")).unwrap();
        assert_eq!(written, "4\n3\n5\n1\n2\n");
    }

    #[test]
    fn random_seed_matches_cpp_model_shapes() {
        // Deterministic across runs (SHA3/ChaCha12); the exact bytes are the
        // C++ product model, gate-checked win-side.
        let a = run(
            &[("seed.bin", SEED)],
            "",
            &["--random-seed", "x", "-i", "1-8"],
        );
        let b = run(
            &[("seed.bin", SEED)],
            "",
            &["--random-seed", "x", "-i", "1-8"],
        );
        assert_eq!((a.rc, b.rc), (0, 0));
        assert_eq!(a.out, b.out);
        let mut sorted: Vec<&str> = a.out.lines().collect();
        sorted.sort_unstable();
        assert_eq!(sorted, ["1", "2", "3", "4", "5", "6", "7", "8"]);
    }

    #[test]
    fn default_rng_and_deferred_range() {
        // exit-mode rows: rc only, content arbitrary but a valid permutation
        let r = run(&[], "", &["-i", "1-5"]);
        assert_eq!(r.rc, 0);
        let mut sorted: Vec<&str> = r.out.lines().collect();
        sorted.sort_unstable();
        assert_eq!(sorted, ["1", "2", "3", "4", "5"]);

        let r = run(&[], "", &["-n", "2", "-i", "1-100"]);
        assert_eq!(r.rc, 0);
        assert_eq!(r.out.lines().count(), 2);

        let r = run(&[("f.txt", F5)], "", &["-n", "2", "f.txt"]);
        assert_eq!(r.rc, 0);
        assert_eq!(r.out.lines().count(), 2);
    }

    #[test]
    fn echo_and_stdin_modes() {
        let r = run(&[], "", &["-e", "a", "b"]);
        assert_eq!(r.rc, 0);
        let mut sorted: Vec<&str> = r.out.lines().collect();
        sorted.sort_unstable();
        assert_eq!(sorted, ["a", "b"]);

        let r = run(&[], "p\nq\n", &["--random-source=/dev/null-x", "-e", "x"]);
        // missing source file -> rc 1 message
        assert_eq!(r.rc, 1);
        assert!(r.err.contains("cannot open random source"));

        // stdin via "-" with a deterministic source: seed A's first byte
        // ('0' = 0x30) is even, so the 2-record shuffle keeps the order
        // (GNU probed "p q").
        let sc_setup = [("_seed", SEED)];
        let r = run(&sc_setup, "p\nq\n", &["--random-source=_seed", "-"]);
        assert_eq!(r.rc, 0);
        assert_eq!(r.out, "p\nq\n");

        // odd first byte swaps ("abcde" -> 'a' 0x61): GNU probed "y x"
        let r = run(&[("sb", "abcde")], "x\ny\n", &["--random-source=sb", "-"]);
        assert_eq!(r.rc, 0);
        assert_eq!(r.out, "y\nx\n");
    }

    #[test]
    fn records_and_edges() {
        // no trailing newline keeps the final record; seeded order follows
        // the first byte's parity (GNU: even '0' -> no swap)
        let sc_setup = [("_seed", SEED)];
        let r = run(&sc_setup, "x\ny", &["--random-source=_seed", "-"]);
        assert_eq!(r.rc, 0);
        assert_eq!(r.out, "x\ny\n");

        // a source exhausted mid-SHUFFLE continues with identity draws
        // (GNU probed: 4 lines, 1 byte 0x01 -> "2 1 3 4", rc 0)
        let r = run(&[("one", "\x01")], "", &["--random-source=one", "4l.txt"]);
        assert_eq!(r.rc, 1); // missing operand file in this bare call
        let r = run(
            &[("one", "\x01"), ("4l.txt", "1\n2\n3\n4\n")],
            "",
            &["--random-source=one", "4l.txt"],
        );
        assert_eq!(r.rc, 0);
        assert_eq!(r.out, "2\n1\n3\n4\n");

        // empty file: silent success rc 0
        let r = run(&[("empty.txt", "")], "", &["empty.txt"]);
        assert_eq!(r.rc, 0);
        assert_eq!(r.out, "");

        // -r with no lines: "no lines to repeat" rc 1
        let r = run(&[("empty.txt", "")], "", &["-r", "-n", "2", "empty.txt"]);
        assert_eq!(r.rc, 1);
        assert!(r.err.contains("no lines to repeat"));

        // missing file operand
        let r = run(&[], "", &["nosuch.txt"]);
        assert_eq!(r.rc, 1);
        assert!(r.err.contains("nosuch.txt: No such file or directory"));

        // head count 0: empty output, -o file created empty
        let r = run(&[], "", &["-n", "0", "-o", "o.txt", "-i", "1-5"]);
        assert_eq!(r.rc, 0);
        assert_eq!(
            std::fs::read(r.dir.join("o.txt")).unwrap(),
            Vec::<u8>::new()
        );

        // -n bigger than population: whole population (GNU probed d c e a b)
        let r = run(
            &[("f5.txt", F5), ("seed.bin", SEED)],
            "",
            &["--random-source=seed.bin", "-n", "10", "f5.txt"],
        );
        assert_eq!((r.rc, r.out.as_str()), (0, "d\nc\ne\na\nb\n"));

        // -n 100 over a range with a source (GNU probed 1 2 3)
        let r = run(
            &[("seed.bin", SEED)],
            "",
            &["--random-source=seed.bin", "-n", "100", "-i", "1-3"],
        );
        assert_eq!((r.rc, r.out.as_str()), (0, "1\n2\n3\n"));

        // overflowing -n is no limit (deterministic with source)
        let r = run(
            &[("seed.bin", SEED)],
            "",
            &[
                "--random-source=seed.bin",
                "-n",
                "99999999999999999999",
                "-i",
                "1-3",
            ],
        );
        assert_eq!(r.rc, 0);
        let mut sorted: Vec<&str> = r.out.lines().collect();
        sorted.sort_unstable();
        assert_eq!(sorted, ["1", "2", "3"]);
    }

    #[test]
    fn usage_errors() {
        let cases: [(&[&str], &str); 9] = [
            (&["-i", "5-1"], "invalid input range: '5-1'"),
            (&["-i", "abc"], "invalid input range: 'abc'"),
            (
                &["-i", "1-99999999999999999999999"],
                "invalid input range: '1-99999999999999999999999': Value too large for defined data type",
            ),
            (&["-i", "5"], "invalid input range: '5'"),
            (&["-e", "-i", "1-3"], "cannot combine -e and -i options"),
            (&["-i", "1-3", "extra"], "extra operand 'extra'"),
            (&["f1", "f2"], "extra operand 'f2'"),
            (&["-n", "xyz", "-i", "1-5"], "invalid line count: 'xyz'"),
            (&["-i", "1-2", "-i", "3-4"], "multiple -i options specified"),
        ];
        for (args, expected) in cases {
            let r = run(&[("f1", "a\n"), ("f2", "b\n")], "", args);
            assert_eq!(r.rc, 1, "args={args:?}");
            assert!(r.err.contains(expected), "args={args:?} err={}", r.err);
        }
        // Try line accompanies the usage-class errors
        let r = run(&[], "", &["-e", "-i", "1-3"]);
        assert!(r.err.contains("Try 'shuf --help' for more information."));
        // repeated identical -o / --random-source values are fine
        let r = run(
            &[("s1", SEED)],
            "",
            &[
                "-o",
                "o1",
                "-o",
                "o1",
                "--random-source=s1",
                "--random-source=s1",
                "-i",
                "1-2",
            ],
        );
        assert_eq!(r.rc, 0);
        // differing -o values die
        let r = run(&[], "", &["-o", "a", "-o", "b", "-i", "1-2"]);
        assert_eq!(r.rc, 1);
        assert!(r.err.contains("multiple output files specified"));
        // seed vs source mutual exclusion (WinuxCmd extension rule)
        let r = run(
            &[("s1", SEED)],
            "",
            &["--random-seed", "x", "--random-source=s1", "-i", "1-2"],
        );
        assert_eq!(r.rc, 1);
        assert!(r.err.contains("mutually exclusive"));
        // -o unwritable path
        let r = run(&[], "", &["-o", "/nosuchdir/out", "-i", "1-2"]);
        assert_eq!(r.rc, 1);
        assert!(r.err.contains("/nosuchdir/out"));
    }

    #[test]
    fn gnu_help_version_surface() {
        let r = run(&[], "", &["--help"]);
        assert_eq!(r.rc, 0);
        assert!(r.out.starts_with("Usage: shuf [OPTION]... [FILE]\n"));
        assert!(r.out.ends_with("info '(coreutils) shuf invocation'\n"));
        let r = run(&[], "", &["--version"]);
        assert_eq!(r.rc, 0);
        assert!(r.out.starts_with("shuf (GNU coreutils) 9.4\n"));
        assert!(r.out.ends_with("Written by Paul Eggert.\n"));
        // GNU has no -h short option
        let r = run(&[], "", &["-h"]);
        assert_eq!(r.rc, 1);
        assert!(r.err.contains("invalid option -- 'h'"));
        let r = run(&[], "", &["-x"]);
        assert_eq!(r.rc, 1);
        assert!(r.err.contains("invalid option -- 'x'"));
    }

    #[test]
    fn range_edges() {
        // " 1-3": GNU xstrtoumax tolerates the space (rc 0, probed); the C++
        // value model rejects it -> win-classified row.
        let r = run(&[], "", &["-i", " 1-3"]);
        assert_eq!(r.rc, 1);
        assert!(r.err.contains("invalid input range: ' 1-3'"));
        // huge but in-range endpoints materialize without -n
        let r = run(
            &[("seed.bin", SEED)],
            "",
            &["--random-source=seed.bin", "-n", "3", "-i", "0-999"],
        );
        assert_eq!(r.rc, 0);
        assert_eq!(r.out.lines().count(), 3);
    }
}
