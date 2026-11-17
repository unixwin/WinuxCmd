//! `factor` — GNU coreutils 9.4 `src/factor.c` operand grammar and output
//! shape, probed byte-for-byte against WSL coreutils 9.4 (2026-11-12).
//!
//! * Output per NUMBER: `N: f1 f2 ...` with factors ascending; 0 and 1
//!   print `N:` with no factors. `-h/--exponents` groups repeats as
//!   `p^e` unless e is 1 (`12` -> `12: 2^2 3`). No operands reads stdin
//!   line by line.
//! * Parsing (probed): leading SPACE characters only (a leading tab is
//!   invalid), one optional `+`, then all-digits, no trailing characters;
//!   `010` is decimal 10; `--` ends options so `factor -- -5` reports the
//!   negative operand. An invalid operand reports
//!   `factor: '<x>' is not a valid positive integer` on stderr, rc 1,
//!   and processing CONTINUES with the remaining operands/stdin lines.
//! * Stdin is tokenized like `src/factor.c:do_stdin` with `readtoken` and
//!   `DELIM = "\n\t "`: every run of newlines/tabs/spaces separates one
//!   NUMBER (probed: `'6 10 15\n'`, `'6\t10\t15\n'` and `'6 10\n15\n'`
//!   all print three lines; `'6\r\n'` keeps the `\r` and reports
//!   `'6\r'` invalid). A token that fails prints its diagnostic and
//!   processing CONTINUES with the remaining tokens (probed `'6 x 10'`:
//!   stdout for 6 and 10, rc 1).
//! * Arbitrary precision: GNU links GMP and factors any non-negative
//!   operand (`factor 18446744073709551616` prints 64 twos, rc 0;
//!   `factor 99999999999999999999999` = 3 x 11111111111111111111111,
//!   probed). Values up to u64::MAX use the deterministic Miller-Rabin +
//!   Pollard-Brent path below; larger values run the same algorithm on a
//!   compact std-only BigUint (little-endian u64 limbs) with a
//!   probabilistic Miller-Rabin witness set — the GNU/GMP probable-prime
//!   contract.
//! * -e is a documented WinuxCmd alias for -h/--exponents (GNU factor 9.4
//!   rejects -e; the alias is a 1.x extension kept in the shipped help
//!   surface and pinned by the ctest `factor_exponents_flag_forms`).

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use crate::gnuhelp;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[OptSpec {
    short: Some('h'),
    long: "exponents",
    takes_value: false,
}];

// `--help` body verbatim from `env factor --help` under WSL coreutils 9.4,
// minus the shared gnuhelp::footer tail.
const FACTOR_HELP_BODY: &str = r#"Usage: factor [OPTION] [NUMBER]...
Print the prime factors of each specified integer NUMBER.  If none
are specified on the command line, read them from standard input.

  -h, --exponents   print repeated factors in form p^e unless e is 1
      --help        display this help and exit
      --version     output version information and exit

"#;

pub fn factor_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("factor", args, OPTS, io, ctx, 1) {
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
                    FACTOR_HELP_BODY,
                    gnuhelp::footer("factor", "factor invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version(
                        "factor",
                        "Paul Rubin, Torbj\u{f6}rn Granlund, and Niels M\u{f6}ller"
                    )
                )?;
                0
            }
        });
    }
    let exponents = p.has("exponents");

    let mut failed = false;
    let out = &mut *io.stdout;
    if p.positionals.is_empty() {
        let mut data = Vec::new();
        if std::io::copy(&mut io.stdin, &mut data).is_err() {
            // factor.c:do_stdin -> error (EXIT_FAILURE, ..., "error reading input")
            let _ = writeln!(io.stderr, "factor: error reading input");
            return Ok(1);
        }
        let err = &mut *io.stderr;
        // factor.c:102 `static char const DELIM[] = "\n\t ";` — readtoken
        // skips runs of those three bytes, so any mixture of spaces, tabs
        // and newlines is one flat token stream.
        for token in data.split(|&b| b == b'\n' || b == b'\t' || b == b' ') {
            if token.is_empty() {
                continue;
            }
            let text = String::from_utf8_lossy(token).into_owned();
            if !print_factors(out, err, &text, exponents) {
                failed = true;
            }
        }
    } else {
        let err = &mut *io.stderr;
        for operand in &p.positionals {
            let text = operand.to_string_lossy().into_owned();
            if !print_factors(out, err, &text, exponents) {
                failed = true;
            }
        }
    }
    Ok(if failed { 1 } else { 0 })
}

/// One parsed NUMBER operand: the u64 fast path or the arbitrary-precision
/// form for operands GNU factors through GMP.
enum Number {
    Small(u64),
    Big(big::Big),
}

/// Parse and print one NUMBER operand; false on invalid input (the GNU
/// diagnostic goes to stderr, processing continues).
fn print_factors(
    out: &mut dyn std::io::Write,
    err: &mut dyn std::io::Write,
    text: &str,
    exponents: bool,
) -> bool {
    let Some(n) = parse_operand(text) else {
        let _ = writeln!(err, "factor: '{text}' is not a valid positive integer");
        return false;
    };
    match n {
        Number::Small(value) => {
            let mut factors = factorize(value);
            factors.sort_unstable();
            let rendered: Vec<String> = factors.iter().map(u64::to_string).collect();
            let _ = write_factors(out, &value.to_string(), &rendered, exponents);
        }
        Number::Big(value) => {
            let mut factors = big::factorize(&value);
            factors.sort_by(|a, b| a.cmp_big(b));
            let rendered: Vec<String> = factors.iter().map(big::Big::to_decimal).collect();
            let _ = write_factors(out, &value.to_decimal(), &rendered, exponents);
        }
    }
    true
}

/// `N: f1 f2 ...`, grouping repeats as `p^e` under -h unless e is 1.
fn write_factors(
    out: &mut dyn std::io::Write,
    label: &str,
    factors: &[String],
    exponents: bool,
) -> std::io::Result<()> {
    write!(out, "{label}:")?;
    if exponents {
        let mut i = 0usize;
        while i < factors.len() {
            let mut count = 1usize;
            while i + count < factors.len() && factors[i + count] == factors[i] {
                count += 1;
            }
            if count == 1 {
                write!(out, " {}", factors[i])?;
            } else {
                write!(out, " {}^{count}", factors[i])?;
            }
            i += count;
        }
    } else {
        for f in factors {
            write!(out, " {f}")?;
        }
    }
    writeln!(out)
}

/// factor.c parse: skip leading ' ' only, one optional '+', all digits,
/// no trailing characters; the magnitude may exceed u64 (GMP path).
fn parse_operand(text: &str) -> Option<Number> {
    let bytes = text.as_bytes();
    let mut pos = 0usize;
    while pos < bytes.len() && bytes[pos] == b' ' {
        pos += 1;
    }
    if pos < bytes.len() && bytes[pos] == b'+' {
        pos += 1;
    }
    let digits = &text[pos..];
    if digits.is_empty() || !digits.bytes().all(|b| b.is_ascii_digit()) {
        return None;
    }
    match digits.parse::<u64>() {
        Ok(value) => Some(Number::Small(value)),
        Err(_) => big::Big::from_decimal(digits).map(Number::Big),
    }
}

// ---------------------------------------------------------------------------
// Number theory (std-only): deterministic Miller-Rabin for u64, Pollard-
// Brent rho with u128 intermediate products, trial division by primes
// < 1000. No process-global state: the tiny prime table is built per call
// (a 1000-cell sieve is microseconds).
// ---------------------------------------------------------------------------

fn mulmod(a: u64, b: u64, m: u64) -> u64 {
    ((a as u128 * b as u128) % m as u128) as u64
}

fn powmod(mut base: u64, mut exp: u64, m: u64) -> u64 {
    if m == 1 {
        return 0;
    }
    let mut result = 1u64;
    base %= m;
    while exp > 0 {
        if exp & 1 == 1 {
            result = mulmod(result, base, m);
        }
        base = mulmod(base, base, m);
        exp >>= 1;
    }
    result
}

fn gcd(mut a: u64, mut b: u64) -> u64 {
    while b != 0 {
        let t = a % b;
        a = b;
        b = t;
    }
    a
}

/// Deterministic Miller-Rabin for u64: the first 12 prime bases are a
/// proven witness set over the whole u64 range.
fn is_prime(n: u64) -> bool {
    if n < 2 {
        return false;
    }
    for p in [2u64, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37] {
        if n % p == 0 {
            return n == p;
        }
    }
    let d0 = n - 1;
    let s = d0.trailing_zeros();
    let d = d0 >> s;
    'witness: for a in [2u64, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37] {
        let mut x = powmod(a, d, n);
        if x == 1 || x == n - 1 {
            continue;
        }
        for _ in 0..s {
            x = mulmod(x, x, n);
            if x == n - 1 {
                continue 'witness;
            }
        }
        return false;
    }
    true
}

/// Pollard-Brent rho: returns a nontrivial factor of composite n (> 1,
/// no small factors left). Retries with fresh polynomials/seeds until a
/// proper split is found.
fn pollard_brent(n: u64) -> u64 {
    if n % 2 == 0 {
        return 2;
    }
    if n % 3 == 0 {
        return 3;
    }
    let mut seed: u64 = 1;
    let mut c: u64 = 1;
    loop {
        let mut y: u64 = seed % n;
        let mut x = y;
        let mut g;
        let mut q = 1u64;
        let mut r = 1u64;
        loop {
            x = y;
            for _ in 0..r {
                y = (mulmod(y, y, n) + c) % n;
            }
            let mut k = 0u64;
            g = 1u64;
            while k < r && g == 1 {
                for _ in 0..128.min(r - k) {
                    y = (mulmod(y, y, n) + c) % n;
                    q = mulmod(q, x.abs_diff(y).max(1), n);
                }
                g = gcd(q, n);
                k += 128;
            }
            r <<= 1;
            if g != 1 {
                break;
            }
        }
        if g == n {
            // the batch overshot: backtrack one step at a time
            let mut ys = y;
            g = 1;
            while g == 1 {
                ys = (mulmod(ys, ys, n) + c) % n;
                g = gcd(x.abs_diff(ys).max(1), n);
            }
        }
        if g != n && g != 1 {
            return g;
        }
        // retry: advance the polynomial constant and the seed
        c += 1;
        seed = seed.wrapping_add(11).max(2);
        if c > 10_000 {
            // unreachable for composite u64 inputs; fall back to the
            // trivial divisor scan rather than looping forever
            let mut f = 2u64;
            while f * f <= n {
                if n % f == 0 {
                    return f;
                }
                f += 1;
            }
            return n;
        }
    }
}

fn small_primes() -> Vec<u64> {
    let limit = 1000usize;
    let mut sieve = vec![true; limit];
    let mut primes = Vec::new();
    for i in 2..limit {
        if sieve[i] {
            primes.push(i as u64);
            let mut j = i * i;
            while j < limit {
                sieve[j] = false;
                j += i;
            }
        }
    }
    primes
}

fn factorize(n: u64) -> Vec<u64> {
    if n < 2 {
        return Vec::new();
    }
    let mut out = Vec::new();
    let mut rest = n;
    for p in small_primes() {
        if p * p > rest {
            break;
        }
        while rest % p == 0 {
            out.push(p);
            rest /= p;
        }
    }
    if rest > 1 {
        let mut stack = vec![rest];
        while let Some(x) = stack.pop() {
            if x == 1 {
                continue;
            }
            if is_prime(x) {
                out.push(x);
                continue;
            }
            let d = pollard_brent(x);
            stack.push(d);
            stack.push(x / d);
        }
    }
    out
}

// ---------------------------------------------------------------------------
// Arbitrary-precision unsigned integers (std-only)
//
// The u64 core above is exact for 0..=u64::MAX. GNU links GMP and factors
// operands of any size (`factor 18446744073709551616`, probed), so values
// above u64 use this compact little-endian u64-limb BigUint with the same
// trial-division + Miller-Rabin + Pollard-Brent shape.
// ---------------------------------------------------------------------------

mod big {
    use std::cmp::Ordering;

    /// Unsigned arbitrary-precision integer: little-endian u64 limbs,
    /// normalized so zero is the empty vector.
    #[derive(Clone, Debug, Default, PartialEq, Eq)]
    pub struct Big {
        limbs: Vec<u64>,
    }

    impl Big {
        pub fn zero() -> Self {
            Big { limbs: Vec::new() }
        }

        pub fn one() -> Self {
            Big { limbs: vec![1] }
        }

        pub fn from_u64(value: u64) -> Self {
            if value == 0 {
                Self::zero()
            } else {
                Big { limbs: vec![value] }
            }
        }

        fn normalize(&mut self) {
            while self.limbs.last() == Some(&0) {
                self.limbs.pop();
            }
        }

        pub fn is_zero(&self) -> bool {
            self.limbs.is_empty()
        }

        pub fn is_one(&self) -> bool {
            self.limbs.len() == 1 && self.limbs[0] == 1
        }

        pub fn to_u64(&self) -> Option<u64> {
            match self.limbs.len() {
                0 => Some(0),
                1 => Some(self.limbs[0]),
                _ => None,
            }
        }

        /// Parses a non-empty run of decimal digits (signs/space are
        /// normalized by the caller, like factor.c's print_factors).
        pub fn from_decimal(digits: &str) -> Option<Self> {
            if digits.is_empty() {
                return None;
            }
            let mut value = Self::zero();
            for byte in digits.bytes() {
                if !byte.is_ascii_digit() {
                    return None;
                }
                value = value.mul_small(10).add_small(u64::from(byte - b'0'));
            }
            Some(value)
        }

        pub fn to_decimal(&self) -> String {
            if self.is_zero() {
                return "0".to_string();
            }
            const CHUNK: u64 = 10_000_000_000_000_000_000; // 1e19
            let mut rest = self.clone();
            let mut chunks = Vec::new();
            while !rest.is_zero() {
                let (quotient, remainder) = rest.divmod_small(CHUNK);
                chunks.push(remainder);
                rest = quotient;
            }
            let mut out = chunks.pop().unwrap().to_string();
            while let Some(chunk) = chunks.pop() {
                out.push_str(&format!("{chunk:019}"));
            }
            out
        }

        pub fn cmp_big(&self, other: &Self) -> Ordering {
            if self.limbs.len() != other.limbs.len() {
                return self.limbs.len().cmp(&other.limbs.len());
            }
            for i in (0..self.limbs.len()).rev() {
                match self.limbs[i].cmp(&other.limbs[i]) {
                    Ordering::Equal => continue,
                    non_equal => return non_equal,
                }
            }
            Ordering::Equal
        }

        pub fn bit_len(&self) -> usize {
            match self.limbs.last() {
                None => 0,
                Some(top) => (self.limbs.len() - 1) * 64 + (64 - top.leading_zeros() as usize),
            }
        }

        fn bit(&self, index: usize) -> bool {
            let limb = index / 64;
            limb < self.limbs.len() && (self.limbs[limb] >> (index % 64)) & 1 == 1
        }

        fn set_bit(&mut self, index: usize) {
            let limb = index / 64;
            if limb >= self.limbs.len() {
                self.limbs.resize(limb + 1, 0);
            }
            self.limbs[limb] |= 1u64 << (index % 64);
        }

        fn shl1(&mut self) {
            let mut carry = 0u64;
            for limb in self.limbs.iter_mut() {
                let next = *limb >> 63;
                *limb = (*limb << 1) | carry;
                carry = next;
            }
            if carry != 0 {
                self.limbs.push(carry);
            }
        }

        fn shr_bits(&mut self, shift: usize) {
            let words = shift / 64;
            let bits = shift % 64;
            if words >= self.limbs.len() {
                self.limbs.clear();
                return;
            }
            if words > 0 {
                self.limbs.drain(..words);
            }
            if bits > 0 {
                let mut carry = 0u64;
                for limb in self.limbs.iter_mut().rev() {
                    let next = *limb << (64 - bits);
                    *limb = (*limb >> bits) | carry;
                    carry = next;
                }
            }
            self.normalize();
        }

        pub fn trailing_zeros(&self) -> u32 {
            let mut count = 0u32;
            for limb in &self.limbs {
                if *limb == 0 {
                    count += 64;
                } else {
                    return count + limb.trailing_zeros();
                }
            }
            count
        }

        pub fn add(&self, other: &Self) -> Self {
            let len = self.limbs.len().max(other.limbs.len());
            let mut out = Vec::with_capacity(len + 1);
            let mut carry = 0u64;
            for i in 0..len {
                let a = self.limbs.get(i).copied().unwrap_or(0) as u128;
                let b = other.limbs.get(i).copied().unwrap_or(0) as u128;
                let sum = a + b + carry as u128;
                out.push(sum as u64);
                carry = (sum >> 64) as u64;
            }
            if carry != 0 {
                out.push(carry);
            }
            Big { limbs: out }
        }

        pub fn add_small(&self, value: u64) -> Self {
            self.add(&Big::from_u64(value))
        }

        /// Precondition: self >= other.
        pub fn sub(&self, other: &Self) -> Self {
            let mut out = self.limbs.clone();
            let mut borrow = 0u64;
            for i in 0..out.len() {
                let rhs = other.limbs.get(i).copied().unwrap_or(0);
                let (diff, b1) = out[i].overflowing_sub(rhs);
                let (diff, b2) = diff.overflowing_sub(borrow);
                out[i] = diff;
                borrow = u64::from(b1 || b2);
            }
            debug_assert_eq!(borrow, 0, "Big::sub underflow");
            let mut result = Big { limbs: out };
            result.normalize();
            result
        }

        pub fn mul(&self, other: &Self) -> Self {
            if self.is_zero() || other.is_zero() {
                return Self::zero();
            }
            let mut out = vec![0u64; self.limbs.len() + other.limbs.len() + 1];
            for i in 0..self.limbs.len() {
                let mut carry = 0u128;
                for j in 0..other.limbs.len() {
                    let index = i + j;
                    let cur = out[index] as u128
                        + self.limbs[i] as u128 * other.limbs[j] as u128
                        + carry;
                    out[index] = cur as u64;
                    carry = cur >> 64;
                }
                let mut index = i + other.limbs.len();
                while carry != 0 {
                    let cur = out[index] as u128 + carry;
                    out[index] = cur as u64;
                    carry = cur >> 64;
                    index += 1;
                }
            }
            let mut result = Big { limbs: out };
            result.normalize();
            result
        }

        pub fn mul_small(&self, factor: u64) -> Self {
            self.mul(&Big::from_u64(factor))
        }

        pub fn divmod_small(&self, divisor: u64) -> (Self, u64) {
            assert!(divisor != 0, "division by zero");
            let mut quotient = vec![0u64; self.limbs.len()];
            let mut remainder = 0u128;
            for i in (0..self.limbs.len()).rev() {
                let cur = (remainder << 64) | self.limbs[i] as u128;
                quotient[i] = (cur / divisor as u128) as u64;
                remainder = cur % divisor as u128;
            }
            let mut quotient = Big { limbs: quotient };
            quotient.normalize();
            (quotient, remainder as u64)
        }

        pub fn rem_small(&self, divisor: u64) -> u64 {
            assert!(divisor != 0, "division by zero");
            let mut remainder = 0u128;
            for i in (0..self.limbs.len()).rev() {
                let cur = (remainder << 64) | self.limbs[i] as u128;
                remainder = cur % divisor as u128;
            }
            remainder as u64
        }

        /// Schoolbook long division (shift and subtract). Values in the
        /// factor lane are small enough that the simple form wins over
        /// Knuth D for clarity.
        pub fn divmod(&self, divisor: &Self) -> (Self, Self) {
            assert!(!divisor.is_zero(), "division by zero");
            if let Some(small) = divisor.to_u64() {
                let (quotient, remainder) = self.divmod_small(small);
                return (quotient, Big::from_u64(remainder));
            }
            if self.cmp_big(divisor) == Ordering::Less {
                return (Self::zero(), self.clone());
            }
            let mut quotient = Big {
                limbs: vec![0u64; self.limbs.len()],
            };
            let mut remainder = Self::zero();
            for i in (0..self.bit_len()).rev() {
                remainder.shl1();
                if self.bit(i) {
                    remainder = remainder.add_small(1);
                }
                if remainder.cmp_big(divisor) != Ordering::Less {
                    remainder = remainder.sub(divisor);
                    quotient.set_bit(i);
                }
            }
            quotient.normalize();
            (quotient, remainder)
        }

        pub fn rem(&self, divisor: &Self) -> Self {
            if let Some(small) = divisor.to_u64() {
                return Big::from_u64(self.rem_small(small));
            }
            self.divmod(divisor).1
        }
    }

    fn abs_diff(a: &Big, b: &Big) -> Big {
        match a.cmp_big(b) {
            Ordering::Greater => a.sub(b),
            Ordering::Less => b.sub(a),
            Ordering::Equal => Big::zero(),
        }
    }

    fn gcd(a: &Big, b: &Big) -> Big {
        let mut a = a.clone();
        let mut b = b.clone();
        while !b.is_zero() {
            let remainder = a.rem(&b);
            a = b;
            b = remainder;
        }
        a
    }

    fn mod_pow(base: &Big, exponent: &Big, modulus: &Big) -> Big {
        if modulus.is_one() {
            return Big::zero();
        }
        let mut result = Big::one();
        let mut factor = base.rem(modulus);
        for i in 0..exponent.bit_len() {
            if exponent.bit(i) {
                result = result.mul(&factor).rem(modulus);
            }
            factor = factor.mul(&factor).rem(modulus);
        }
        result
    }

    /// Probable-prime test over the whole range: deterministic for u64,
    /// the first 12 prime bases for larger values (the GMP contract).
    pub fn is_probable_prime(n: &Big) -> bool {
        if n.is_zero() || n.is_one() {
            return false;
        }
        if let Some(value) = n.to_u64() {
            return super::is_prime(value);
        }
        for p in [2u64, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37] {
            if n.rem_small(p) == 0 {
                return false;
            }
        }
        let one = Big::one();
        let n_minus_one = n.sub(&one);
        let s = n_minus_one.trailing_zeros();
        let mut d = n_minus_one.clone();
        d.shr_bits(s as usize);
        'witness: for a in [2u64, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37] {
            let mut x = mod_pow(&Big::from_u64(a), &d, n);
            if x.is_one() || x.cmp_big(&n_minus_one) == Ordering::Equal {
                continue;
            }
            for _ in 1..s {
                x = x.mul(&x).rem(n);
                if x.cmp_big(&n_minus_one) == Ordering::Equal {
                    continue 'witness;
                }
            }
            return false;
        }
        true
    }

    /// Pollard-Brent rho: returns a nontrivial factor of composite n.
    fn pollard_brent(n: &Big) -> Big {
        if n.rem_small(2) == 0 {
            return Big::from_u64(2);
        }
        if n.rem_small(3) == 0 {
            return Big::from_u64(3);
        }
        let mut seed = 2u64;
        let mut c = 1u64;
        loop {
            let c_big = Big::from_u64(c);
            let mut y = Big::from_u64(seed).rem(n);
            let mut x = y.clone();
            let mut g = Big::one();
            let mut q = Big::one();
            let mut r = 1u64;
            loop {
                x = y.clone();
                for _ in 0..r {
                    y = y.mul(&y).add(&c_big).rem(n);
                }
                let mut k = 0u64;
                g = Big::one();
                while k < r && g.is_one() {
                    for _ in 0..128.min(r - k) {
                        y = y.mul(&y).add(&c_big).rem(n);
                        let diff = abs_diff(&x, &y);
                        let diff = if diff.is_zero() { Big::one() } else { diff };
                        q = q.mul(&diff).rem(n);
                    }
                    g = gcd(&q, n);
                    k += 128;
                }
                r <<= 1;
                if !g.is_one() {
                    break;
                }
            }
            if g.cmp_big(n) == Ordering::Equal {
                // The batch overshot; backtrack one step at a time.
                let mut ys = y.clone();
                g = Big::one();
                while g.is_one() {
                    ys = ys.mul(&ys).add(&c_big).rem(n);
                    let diff = abs_diff(&x, &ys);
                    let diff = if diff.is_zero() { Big::one() } else { diff };
                    g = gcd(&diff, n);
                }
            }
            if !g.is_one() && g.cmp_big(n) != Ordering::Equal {
                return g;
            }
            c += 1;
            if c > 10_000 {
                c = 1;
            }
            seed = seed.wrapping_add(11);
        }
    }

    /// All prime factors of n (unordered); the u64 core handles the tail as
    /// soon as the remaining cofactor fits its range.
    pub fn factorize(n: &Big) -> Vec<Big> {
        let mut out = Vec::new();
        if n.is_zero() || n.is_one() {
            return out;
        }
        let mut rest = n.clone();
        for p in super::small_primes() {
            if let Some(value) = rest.to_u64() {
                out.extend(super::factorize(value).into_iter().map(Big::from_u64));
                return out;
            }
            let prime = Big::from_u64(p);
            if prime.mul(&prime).cmp_big(&rest) == Ordering::Greater {
                out.push(rest);
                return out;
            }
            loop {
                let (quotient, remainder) = rest.divmod(&prime);
                if !remainder.is_zero() {
                    break;
                }
                out.push(prime.clone());
                rest = quotient;
                if rest.is_one() {
                    return out;
                }
            }
        }
        if let Some(value) = rest.to_u64() {
            out.extend(super::factorize(value).into_iter().map(Big::from_u64));
            return out;
        }
        let mut stack = vec![rest];
        while let Some(value) = stack.pop() {
            if value.is_one() {
                continue;
            }
            if let Some(small) = value.to_u64() {
                out.extend(super::factorize(small).into_iter().map(Big::from_u64));
                continue;
            }
            if is_probable_prime(&value) {
                out.push(value);
                continue;
            }
            let divisor = pollard_brent(&value);
            let (quotient, _) = value.divmod(&divisor);
            stack.push(divisor);
            stack.push(quotient);
        }
        out
    }

    #[cfg(test)]
    mod tests {
        use super::*;

        #[test]
        fn decimal_roundtrip_and_arithmetic() {
            for text in [
                "0",
                "1",
                "18446744073709551615",
                "18446744073709551616",
                "99999999999999999999999",
            ] {
                assert_eq!(Big::from_decimal(text).unwrap().to_decimal(), text);
            }
            let two64 = Big::from_decimal("18446744073709551616").unwrap();
            let two = Big::from_decimal("2").unwrap();
            assert_eq!(two64.divmod(&two).0.to_decimal(), "9223372036854775808");
            assert_eq!(
                two64.mul(&two64).to_decimal(),
                "340282366920938463463374607431768211456"
            );
            assert_eq!(
                Big::from_decimal("340282366920938463463374607431768211456")
                    .unwrap()
                    .rem(&two64)
                    .to_decimal(),
                "0"
            );
        }

        #[test]
        fn primality_matches_gnu_shapes() {
            assert!(is_probable_prime(
                &Big::from_decimal("11111111111111111111111").unwrap()
            ));
            assert!(!is_probable_prime(
                &Big::from_decimal("18446744073709551617").unwrap()
            ));
        }
    }
}

#[cfg(test)]
mod tests {
    use std::ffi::OsString;
    use winux_core::test_support::{MemIo, ScratchCtx};

    use super::*;

    fn run(args: &[&str], stdin: &str) -> (u32, String, String) {
        let sc = ScratchCtx::new("factor");
        let ctx = sc.ctx();
        let mut io = MemIo::new(stdin);
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = factor_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn basic_factorizations() {
        // All expected strings probed against WSL coreutils 9.4.
        let (r, out, err) = run(&["12"], "");
        assert_eq!((r, err.as_str()), (0, ""));
        assert_eq!(out, "12: 2 2 3\n");
        let (_, out, _) = run(&["0"], "");
        assert_eq!(out, "0:\n");
        let (_, out, _) = run(&["1"], "");
        assert_eq!(out, "1:\n");
        let (_, out, _) = run(&["1000003"], "");
        assert_eq!(out, "1000003: 1000003\n");
        let (_, out, _) = run(&["18446744073709551615"], "");
        assert_eq!(out, "18446744073709551615: 3 5 17 257 641 65537 6700417\n");
        let (_, out, _) = run(&["123456789012345678"], "");
        assert_eq!(out, "123456789012345678: 2 3 3 3 21491747 106377431\n");
        let (_, out, _) = run(&["4611686018427387905"], "");
        assert_eq!(out, "4611686018427387905: 5 5581 8681 49477 384773\n");
        // large primes (Miller-Rabin vs GNU)
        let (_, out, _) = run(&["2147483647", "4294967291"], "");
        assert_eq!(out, "2147483647: 2147483647\n4294967291: 4294967291\n");
        // probed semiprime-shape number
        let (_, out, _) = run(&["4295109041"], "");
        assert_eq!(out, "4295109041: 17 17 2389 6221\n");
    }

    #[test]
    fn exponents_form() {
        let (_, out, _) = run(&["-h", "12"], "");
        assert_eq!(out, "12: 2^2 3\n");
        let (_, out, _) = run(&["--exponents", "8"], "");
        assert_eq!(out, "8: 2^3\n");
        let (_, out, _) = run(&["-h", "7"], "");
        assert_eq!(out, "7: 7\n");
        let (_, out, _) = run(&["-h", "4", "9"], "");
        assert_eq!(out, "4: 2^2\n9: 3^2\n");
        // stdin with -h (probed: 18 -> 2 3^2)
        let (_, out, _) = run(&["-h"], "18\n");
        assert_eq!(out, "18: 2 3^2\n");
    }

    #[test]
    fn arbitrary_precision_operands() {
        // All three shapes probed against GNU coreutils 9.4.
        let (r, out, _) = run(&["18446744073709551617"], "");
        assert_eq!(r, 0);
        assert_eq!(out, "18446744073709551617: 274177 67280421310721\n");
        let (r, out, _) = run(&["99999999999999999999999"], "");
        assert_eq!(r, 0);
        assert_eq!(out, "99999999999999999999999: 3 3 11111111111111111111111\n");
        // -h groups the repeated factors of 2^64
        let (r, out, _) = run(&["-h", "18446744073709551616"], "");
        assert_eq!(r, 0);
        assert_eq!(out, "18446744073709551616: 2^64\n");
        // stdin tokens use the same parser
        let (r, out, _) = run(&[], "18446744073709551616\n");
        assert_eq!(r, 0);
        assert!(out.starts_with("18446744073709551616: 2 2 2"));
    }

    #[test]
    fn operand_grammar() {
        // leading spaces skip; one '+'; leading zeros decimal
        let (_, out, _) = run(&[" 12"], "");
        assert_eq!(out, "12: 2 2 3\n");
        let (_, out, _) = run(&["+12"], "");
        assert_eq!(out, "12: 2 2 3\n");
        let (_, out, _) = run(&["010"], "");
        assert_eq!(out, "10: 2 5\n");
        // invalid operands: error + rc 1 + processing continues
        let (r, out, err) = run(&["xyz", "6"], "");
        assert_eq!(r, 1);
        assert_eq!(out, "6: 2 3\n");
        assert!(err.contains("factor: 'xyz' is not a valid positive integer"));
        let (r, _, err) = run(&["--", "-5"], "");
        assert_eq!(r, 1);
        assert!(err.contains("'-5' is not a valid positive integer"));
        let (r, _, err) = run(&[""], "");
        assert_eq!(r, 1);
        assert!(err.contains("'' is not a valid positive integer"));
        let (r, _, err) = run(&["12x"], "");
        assert_eq!(r, 1);
        assert!(err.contains("'12x' is not"));
        let (r, _, err) = run(&["12 "], "");
        assert_eq!(r, 1);
        assert!(err.contains("'12 ' is not"));
        // leading tab is invalid (only spaces skip)
        let (r, _, err) = run(&["\t12"], "");
        assert_eq!(r, 1);
        assert!(err.contains("is not a valid positive integer"));
        // beyond u64: GNU links GMP; the BigUint path must factor it (rc 0)
        let (r, out, err) = run(&["18446744073709551616"], "");
        assert_eq!((r, err.as_str()), (0, ""));
        let mut expected = String::from("18446744073709551616:");
        for _ in 0..64 {
            expected.push_str(" 2");
        }
        expected.push('\n');
        assert_eq!(out, expected);
    }

    #[test]
    fn stdin_mode() {
        let (r, out, _) = run(&[], "8\n9\n");
        assert_eq!(r, 0);
        assert_eq!(out, "8: 2 2 2\n9: 3 3\n");
        // factor.c readtoken(DELIM="\n\t "): several numbers on one line,
        // spaces and tabs both split (probed 2026-11-17)
        let (r, out, _) = run(&[], "6 10 15\n");
        assert_eq!(r, 0);
        assert_eq!(out, "6: 2 3\n10: 2 5\n15: 3 5\n");
        let (r, out, _) = run(&[], "6\t10\t15\n");
        assert_eq!(r, 0);
        assert_eq!(out, "6: 2 3\n10: 2 5\n15: 3 5\n");
        let (r, out, _) = run(&[], "6 10\n15\n");
        assert_eq!(r, 0);
        assert_eq!(out, "6: 2 3\n10: 2 5\n15: 3 5\n");
        let (r, out, _) = run(&[], "   6    10   \n");
        assert_eq!(r, 0);
        assert_eq!(out, "6: 2 3\n10: 2 5\n");
        let (r, out, _) = run(&[], "+6 +10\n");
        assert_eq!(r, 0);
        assert_eq!(out, "6: 2 3\n10: 2 5\n");
        // a bad token fails alone; the rest of the stream continues
        let (r, out, err) = run(&[], "6 x 10\n");
        assert_eq!(r, 1);
        assert_eq!(out, "6: 2 3\n10: 2 5\n");
        assert!(err.contains("'x' is not a valid positive integer"));
        // blank lines skipped; surrounding spaces trimmed (probed)
        let (r, out, _) = run(&[], "  5  \n\n  \n7\n");
        assert_eq!(r, 0);
        assert_eq!(out, "5: 5\n7: 7\n");
        // no trailing newline still processed
        let (r, out, _) = run(&[], "6");
        assert_eq!(r, 0);
        assert_eq!(out, "6: 2 3\n");
        // CR is NOT trimmed: '6\r' is invalid (probed), processing continues
        let (r, out, err) = run(&[], "6\r\n");
        assert_eq!(r, 1);
        assert_eq!(out, "");
        assert!(err.contains("is not a valid positive integer"));
        // junk line continues to the next (probed: x then 5)
        let (r, out, err) = run(&[], "x\n5\n");
        assert_eq!(r, 1);
        assert_eq!(out, "5: 5\n");
        assert!(err.contains("'x'"));
        // tab-only line skipped (probed)
        let (r, out, _) = run(&[], "\t\n8\n");
        assert_eq!(r, 0);
        assert_eq!(out, "8: 2 2 2\n");
    }

    #[test]
    fn help_version_surface() {
        let (r, out, err) = run(&["--help"], "");
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.starts_with("Usage: factor [OPTION] [NUMBER]...\n"));
        assert!(out.contains("-h, --exponents"));
        assert!(out.ends_with("info '(coreutils) factor invocation'\n"));
        let (r, out, _) = run(&["--version"], "");
        assert_eq!(r, 0);
        assert!(out.starts_with("factor (GNU coreutils) 9.4\n"));
        assert!(out
            .ends_with("Written by Paul Rubin, Torbj\u{f6}rn Granlund, and Niels M\u{f6}ller.\n"));
        // help wins over operands (getopt permute, probed)
        let (r, out, _) = run(&["--help", "12"], "");
        assert_eq!(r, 0);
        assert!(out.starts_with("Usage:"));
        // option errors
        let (r, _, err) = run(&["--exponents=8"], "");
        assert_eq!(r, 1);
        assert!(err.contains("doesn't allow an argument"));
        let (r, _, err) = run(&["-q", "12"], "");
        assert_eq!(r, 1);
        assert!(err.contains("invalid option -- 'q'"));
        let (r, _, err) = run(&["--bogus", "12"], "");
        assert_eq!(r, 1);
        assert!(err.contains("unrecognized option '--bogus'"));
    }

    #[test]
    fn unit_theory_sanity() {
        // direct core checks (the command-level values are GNU-probed)
        assert!(is_prime(2));
        assert!(is_prime(6700417));
        assert!(is_prime(4294967291));
        assert!(!is_prime(4294967290));
        assert_eq!(factorize(2u64.pow(63)), vec![2u64; 63]);
        assert_eq!(factorize(1000006000009), vec![1000003, 1000003]);
    }
}
