//! `factor` — GNU coreutils factor semantics (probe base: coreutils 9.4,
//! target/probe/f1.sh+f2.sh), with the Windows value model of the 1.x C++
//! applet (`src/commands/factor.cpp` on main).
//!
//! GNU contract:
//! * `factor [OPTION] [NUMBER]...`; with no NUMBER, tokens are read from
//!   stdin, split on exactly SPACE, TAB and NEWLINE (`factor.c:125` sets
//!   DELIM "\n\t " — any other byte, e.g. `\r`, stays inside the token and
//!   makes it invalid; probed: `printf '6\t7\r\n' | factor` reports
//!   `'7\r' is not a valid positive integer`).
//! * An operand skips leading SPACE characters (only `' '`, not TAB) and
//!   one leading `+`, then requires at least one digit and nothing else
//!   (factor.c parse_character). 0 and 1 print with no factors; factors
//!   are ascending, space-separated after the `N:` prefix.
//! * `-h/--exponents` groups repeated factors as `p^e` unless e is 1
//!   (GNU 9.4 help). `-e` is a documented WinuxCmd extension alias (GNU
//!   rejects it with "invalid option -- 'e'"; C++ accepts — gate rows are
//!   `win`-classified).
//! * Any invalid operand prints
//!   `factor: 'T' is not a valid positive integer` and the run exits 1
//!   after processing every operand (probed: both `abc` and `def` are
//!   reported for `factor abc def`).
//!
//! Windows value model: the arbitrary-precision arithmetic is ported to a
//! std-only little-endian base-2^64 biguint. Factor extraction is trial
//! division by primes < 1024, then Pollard-Brent rho on the remainder with
//! Miller-Rabin probable-prime tests over the first 16 prime bases —
//! deterministic far beyond every realistic operand (GNU additionally runs
//! a Lucas test we do not port; no known composite passes all 16 bases, and
//! the gate's own GNU diff pins every emitted factor line byte-for-byte).
//! Performance boundary (not a correctness one): rho needs ~sqrt(p) steps
//! for a smallest prime factor p, so operands whose smallest factor
//! approaches 2^65 (GNU's optimized MP factors Fermat F7 instantly; our
//! std-only port would run minutes) are out of the gate's 10 s envelope.
//! Output ordering (ascending factors) is platform-independent, so value
//! cases gate `strict` against GNU.

use std::ffi::OsString;
use std::io::Read as _;

use crate::argopt::{self, OptSpec};
use crate::gnuhelp;
use winux_core::{CmdResult, Ctx, Io};

// ---------------------------------------------------------------------------
// Minimal biguint: little-endian base-2^64 limbs, no trailing zero limbs.
// Zero is the empty limb vector. Only the operations factor needs.
// ---------------------------------------------------------------------------

#[derive(Clone, Debug, PartialEq, Eq)]
struct Big {
    limbs: Vec<u64>,
}

impl Big {
    fn zero() -> Big {
        Big { limbs: Vec::new() }
    }

    fn one() -> Big {
        Big { limbs: vec![1] }
    }

    fn is_zero(&self) -> bool {
        self.limbs.is_empty()
    }

    fn is_one(&self) -> bool {
        self.limbs.len() == 1 && self.limbs[0] == 1
    }

    /// Decimal digits -> biguint. The caller has already validated that the
    /// text is all digits.
    fn from_decimal(digits: &str) -> Big {
        let mut v = Big::zero();
        for d in digits.bytes() {
            v.mul_add_small(10, u64::from(d - b'0'));
        }
        v
    }

    /// self = self * m + a, with m small (base conversion).
    fn mul_add_small(&mut self, m: u64, a: u64) {
        let mut carry = a;
        for limb in self.limbs.iter_mut() {
            let prod = u128::from(*limb) * u128::from(m) + u128::from(carry);
            *limb = prod as u64;
            carry = (prod >> 64) as u64;
        }
        if carry != 0 {
            self.limbs.push(carry);
        }
        self.normalize();
    }

    fn normalize(&mut self) {
        while self.limbs.last() == Some(&0) {
            self.limbs.pop();
        }
    }

    fn cmp(&self, other: &Big) -> std::cmp::Ordering {
        if self.limbs.len() != other.limbs.len() {
            return self.limbs.len().cmp(&other.limbs.len());
        }
        for i in (0..self.limbs.len()).rev() {
            if self.limbs[i] != other.limbs[i] {
                return self.limbs[i].cmp(&other.limbs[i]);
            }
        }
        std::cmp::Ordering::Equal
    }

    /// self / d with remainder, d >= 2 small. Returns (quotient, remainder).
    fn divmod_small(&self, d: u64) -> (Big, u64) {
        let mut q = vec![0u64; self.limbs.len()];
        let mut r: u64 = 0;
        for i in (0..self.limbs.len()).rev() {
            let cur = (u128::from(r) << 64) | u128::from(self.limbs[i]);
            q[i] = (cur / u128::from(d)) as u64;
            r = (cur % u128::from(d)) as u64;
        }
        let mut quotient = Big { limbs: q };
        quotient.normalize();
        (quotient, r)
    }

    /// self - other for self >= other.
    fn sub(&self, other: &Big) -> Big {
        let mut out = Vec::with_capacity(self.limbs.len());
        let mut borrow: u64 = 0;
        for i in 0..self.limbs.len() {
            let b = if i < other.limbs.len() {
                other.limbs[i]
            } else {
                0
            };
            let (d1, b1) = self.limbs[i].overflowing_sub(b);
            let (d2, b2) = d1.overflowing_sub(borrow);
            out.push(d2);
            borrow = u64::from(b1) + u64::from(b2);
        }
        let mut r = Big { limbs: out };
        r.normalize();
        r
    }

    fn add(&self, other: &Big) -> Big {
        let mut out = Vec::with_capacity(self.limbs.len().max(other.limbs.len()) + 1);
        let mut carry: u64 = 0;
        for i in 0..self.limbs.len().max(other.limbs.len()) {
            let a = if i < self.limbs.len() {
                self.limbs[i]
            } else {
                0
            };
            let b = if i < other.limbs.len() {
                other.limbs[i]
            } else {
                0
            };
            let s = u128::from(a) + u128::from(b) + u128::from(carry);
            out.push(s as u64);
            carry = (s >> 64) as u64;
        }
        if carry != 0 {
            out.push(carry);
        }
        let mut r = Big { limbs: out };
        r.normalize();
        r
    }

    /// Schoolbook product.
    fn mul(&self, other: &Big) -> Big {
        if self.is_zero() || other.is_zero() {
            return Big::zero();
        }
        let mut out = vec![0u64; self.limbs.len() + other.limbs.len()];
        for (i, &a) in self.limbs.iter().enumerate() {
            let mut carry: u64 = 0;
            for (j, &b) in other.limbs.iter().enumerate() {
                let cur =
                    u128::from(a) * u128::from(b) + u128::from(out[i + j]) + u128::from(carry);
                out[i + j] = cur as u64;
                carry = (cur >> 64) as u64;
            }
            let mut k = i + other.limbs.len();
            while carry != 0 {
                let cur = u128::from(out[k]) + u128::from(carry);
                out[k] = cur as u64;
                carry = (cur >> 64) as u64;
                k += 1;
            }
        }
        let mut r = Big { limbs: out };
        r.normalize();
        r
    }

    /// self mod modulus via binary long division (shift-and-subtract). The
    /// operand sizes this command sees make the O(bits) loop trivial.
    fn rem(&self, modulus: &Big) -> Big {
        let ord = self.cmp(modulus);
        if ord == std::cmp::Ordering::Less {
            return self.clone();
        }
        // align modulus above the dividend's top bit, then walk down
        let bits = self.bit_len();
        let mut rem = Big::zero();
        for i in (0..bits).rev() {
            // rem = rem << 1 | bit_i
            rem.shl1();
            if self.bit(i) {
                if rem.limbs.is_empty() {
                    rem.limbs.push(0);
                }
                rem.limbs[0] |= 1;
            }
            if rem.cmp(modulus) != std::cmp::Ordering::Less {
                rem = rem.sub(modulus);
            }
        }
        rem
    }

    fn bit_len(&self) -> usize {
        match self.limbs.last() {
            None => 0,
            Some(&top) => (self.limbs.len() - 1) * 64 + (64 - top.leading_zeros() as usize),
        }
    }

    fn bit(&self, i: usize) -> bool {
        let limb = i / 64;
        if limb >= self.limbs.len() {
            return false;
        }
        (self.limbs[limb] >> (i % 64)) & 1 == 1
    }

    fn shl1(&mut self) {
        let mut carry: u64 = 0;
        for limb in self.limbs.iter_mut() {
            let new_carry = *limb >> 63;
            *limb = (*limb << 1) | carry;
            carry = new_carry;
        }
        if carry != 0 {
            self.limbs.push(carry);
        }
    }

    /// (self * other) mod modulus.
    fn mulmod(&self, other: &Big, modulus: &Big) -> Big {
        self.mul(other).rem(modulus)
    }

    /// (self + other) mod modulus, both already reduced.
    fn add_mod(&self, other: &Big, modulus: &Big) -> Big {
        let s = self.add(other);
        if s.cmp(modulus) == std::cmp::Ordering::Less {
            s
        } else {
            s.sub(modulus)
        }
    }

    /// self^exponent mod modulus (square-and-multiply).
    fn powmod(&self, exponent: &Big, modulus: &Big) -> Big {
        let mut result = Big::one();
        let mut base = self.rem(modulus);
        let bits = exponent.bit_len();
        for i in 0..bits {
            if exponent.bit(i) {
                result = result.mulmod(&base, modulus);
            }
            base = base.mulmod(&base, modulus);
        }
        result
    }

    fn gcd(mut a: Big, mut b: Big) -> Big {
        while !b.is_zero() {
            let r = a.rem(&b);
            a = b;
            b = r;
        }
        a
    }

    fn to_decimal(&self) -> String {
        if self.is_zero() {
            return "0".to_string();
        }
        let mut chunks: Vec<u64> = Vec::new();
        let mut cur = self.clone();
        let chunk = 10_000_000_000_000_000_000u64; // 10^19
        while !cur.is_zero() {
            let (q, r) = cur.divmod_small(chunk);
            chunks.push(r);
            cur = q;
        }
        let mut out = String::new();
        for &c in chunks.iter().rev() {
            if out.is_empty() {
                out.push_str(&c.to_string());
            } else {
                out.push_str(&format!("{c:019}"));
            }
        }
        out
    }
}

// ---------------------------------------------------------------------------
// Primality + rho
// ---------------------------------------------------------------------------

/// Trial primes < 1024, sieved per call (168 primes; trivial work).
fn small_primes() -> Vec<u64> {
    let limit = 1024u64;
    let mut sieve = vec![true; limit as usize];
    let mut primes = Vec::new();
    for p in 2..limit {
        if sieve[p as usize] {
            primes.push(p);
            let mut m = p * p;
            while m < limit {
                sieve[m as usize] = false;
                m += p;
            }
        }
    }
    primes
}

/// Miller-Rabin over the first 16 prime bases. See the module comment for
/// the determinism note versus GNU's 12-base + Lucas split.
fn is_probable_prime(n: &Big) -> bool {
    if n.cmp(&Big::from_decimal("2")) == std::cmp::Ordering::Less {
        return false;
    }
    for p in small_primes() {
        let pn = Big::from_decimal(&p.to_string());
        match n.cmp(&pn) {
            std::cmp::Ordering::Equal => return true,
            std::cmp::Ordering::Less => return false,
            std::cmp::Ordering::Greater => {}
        }
        if n.divmod_small(p).1 == 0 {
            return false;
        }
    }
    let one = Big::one();
    let nm1 = n.sub(&one);
    // write n-1 = d * 2^s
    let mut d = nm1.clone();
    let mut s = 0usize;
    while !d.is_zero() && d.limbs[0] & 1 == 0 {
        // halve d
        let (q, _r) = d.divmod_small(2);
        d = q;
        s += 1;
    }
    let bases = [
        2u64, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53,
    ];
    'bases: for &b in &bases {
        let bn = Big::from_decimal(&b.to_string());
        if bn.cmp(n) == std::cmp::Ordering::Greater {
            continue;
        }
        let mut x = bn.powmod(&d, n);
        if x.is_one() || x == nm1 {
            continue;
        }
        for _ in 1..s {
            x = x.mulmod(&x, n);
            if x == nm1 {
                continue 'bases;
            }
        }
        return false;
    }
    true
}

/// Pollard-Brent rho with a deterministic seed walk (y=2, c stepping from 1).
/// Retries with a fresh c until a nontrivial factor is found, so the caller
/// can always split.
fn pollard_brent(n: &Big) -> Big {
    let two = Big::from_decimal("2");
    let mut c: u64 = 1;
    loop {
        let step = Big::from_decimal(&c.to_string());
        let advance = |y: &Big| y.mulmod(y, n).add_mod(&step, n);
        let mut y = two.clone();
        let mut qprod = Big::one();
        let mut g = Big::one();
        let m: usize = 128;
        let mut r = 1usize;
        let mut x = y.clone();
        let mut ys = y.clone();
        while g.is_one() {
            x = y.clone();
            for _ in 0..r {
                y = advance(&y);
            }
            let mut k = 0usize;
            while k < r && g.is_one() {
                ys = y.clone();
                let batch = m.min(r - k);
                for _ in 0..batch {
                    y = advance(&y);
                    let diff = if x.cmp(&y) == std::cmp::Ordering::Greater {
                        x.sub(&y)
                    } else {
                        y.sub(&x)
                    };
                    qprod = qprod.mulmod(&diff, n);
                }
                g = Big::gcd(qprod.clone(), n.clone());
                k += m;
            }
            r *= 2;
        }
        if g == *n {
            // The batched product caught n itself: redo the tail one step at
            // a time from the last batch start.
            g = Big::one();
            y = ys.clone();
            while g.is_one() {
                ys = y.clone();
                y = advance(&y);
                let diff = if x.cmp(&y) == std::cmp::Ordering::Greater {
                    x.sub(&y)
                } else {
                    y.sub(&x)
                };
                g = Big::gcd(diff, n.clone());
            }
        }
        if g.cmp(n) == std::cmp::Ordering::Less && !g.is_one() {
            return g;
        }
        c += 1;
    }
}

/// All prime factors of n (n >= 2), ascending.
fn factorize(n: &Big) -> Vec<Big> {
    let mut factors: Vec<Big> = Vec::new();
    let mut rem = n.clone();
    for p in small_primes() {
        loop {
            let (q, r) = rem.divmod_small(p);
            if r != 0 {
                break;
            }
            factors.push(Big::from_decimal(&p.to_string()));
            rem = q;
        }
        if rem.is_one() {
            return sorted(factors);
        }
    }
    let mut stack = vec![rem];
    while let Some(x) = stack.pop() {
        if x.is_one() {
            continue;
        }
        if is_probable_prime(&x) {
            factors.push(x);
            continue;
        }
        let d = pollard_brent(&x);
        // d is a true factor of x; binary long division yields the cofactor.
        let (q, _r) = big_divmod(&x, &d);
        stack.push(d);
        stack.push(q);
    }
    sorted(factors)
}

fn sorted(mut factors: Vec<Big>) -> Vec<Big> {
    factors.sort_by(|a, b| a.cmp(b));
    factors
}

/// Full biguint divmod (binary long division). Used only on the rho split
/// path, where operands are already rare and small enough.
fn big_divmod(dividend: &Big, divisor: &Big) -> (Big, Big) {
    let bits = dividend.bit_len();
    let mut q = Big::zero();
    let mut r = Big::zero();
    for i in (0..bits).rev() {
        r.shl1();
        if dividend.bit(i) {
            if r.limbs.is_empty() {
                r.limbs.push(0);
            }
            r.limbs[0] |= 1;
        }
        if r.cmp(divisor) != std::cmp::Ordering::Less {
            r = r.sub(divisor);
            // set bit i of q
            let limb = i / 64;
            while q.limbs.len() <= limb {
                q.limbs.push(0);
            }
            q.limbs[limb] |= 1u64 << (i % 64);
        }
    }
    q.normalize();
    (q, r)
}

// ---------------------------------------------------------------------------
// Command surface
// ---------------------------------------------------------------------------

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('h'),
        long: "exponents",
        takes_value: false,
    },
    // Documented WinuxCmd extension (C++ factor.cpp): GNU rejects -e. The
    // long name is deliberately not a prefix of "exponents" so the
    // abbreviation table keeps GNU's unambiguous "--ex" resolution.
    OptSpec {
        short: Some('e'),
        long: "exponents-winux",
        takes_value: false,
    },
];

const FACTOR_HELP_BODY: &str = r#"Usage: factor [OPTION] [NUMBER]...
Print the prime factors of each specified integer NUMBER.  If none
are specified on the command line, read them from standard input.

  -h, --exponents   print repeated factors in form p^e unless e is 1
      --help        display this help and exit
      --version     output version information and exit

"#;

/// GNU factor.c parse_character: leading SPACEs (only ' '), one optional
/// '+', then digits and nothing else.
fn parse_operand(text: &str) -> Option<Big> {
    let b = text.as_bytes();
    let mut pos = 0usize;
    while pos < b.len() && b[pos] == b' ' {
        pos += 1;
    }
    if pos < b.len() && b[pos] == b'+' {
        pos += 1;
    }
    let digits_begin = pos;
    while pos < b.len() && b[pos].is_ascii_digit() {
        pos += 1;
    }
    if pos == digits_begin || pos != b.len() {
        return None;
    }
    Some(Big::from_decimal(&text[digits_begin..pos]))
}

fn print_factors(n: &Big, exponents: bool, io: &mut Io) -> std::io::Result<()> {
    let factors = if n.cmp(&Big::from_decimal("2")) == std::cmp::Ordering::Less {
        Vec::new()
    } else {
        factorize(n)
    };
    write!(io.stdout, "{}:", n.to_decimal())?;
    if exponents {
        let mut i = 0usize;
        while i < factors.len() {
            let mut count = 1usize;
            while i + count < factors.len() && factors[i + count] == factors[i] {
                count += 1;
            }
            write!(io.stdout, " {}", factors[i].to_decimal())?;
            if count > 1 {
                write!(io.stdout, "^{count}")?;
            }
            i += count;
        }
    } else {
        for f in &factors {
            write!(io.stdout, " {}", f.to_decimal())?;
        }
    }
    writeln!(io.stdout)?;
    Ok(())
}

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
                        "Paul Rubin, Torbjörn Granlund, and Niels Möller"
                    )
                )?;
                0
            }
        });
    }
    let exponents = p.has("exponents") || p.has("exponents-winux");

    let mut had_error = false;
    if p.positionals.is_empty() {
        // stdin mode: tokens split on exactly "\n", "\t", " " (factor.c DELIM).
        let mut input = Vec::new();
        io.stdin.read_to_end(&mut input)?;
        let mut start: Option<usize> = None;
        let bytes = input.as_slice();
        let mut emit = |tok: &[u8], io: &mut Io, had_error: &mut bool| {
            let text = String::from_utf8_lossy(tok).into_owned();
            match parse_operand(&text) {
                Some(v) => {
                    let _ = print_factors(&v, exponents, io);
                }
                None => {
                    let _ = writeln!(
                        io.stderr,
                        "factor: '{text}' is not a valid positive integer"
                    );
                    *had_error = true;
                }
            }
        };
        for (i, &b) in bytes.iter().enumerate() {
            if b == b' ' || b == b'\t' || b == b'\n' {
                if let Some(s) = start.take() {
                    emit(&bytes[s..i], io, &mut had_error);
                }
            } else if start.is_none() {
                start = Some(i);
            }
        }
        if let Some(s) = start {
            emit(&bytes[s..], io, &mut had_error);
        }
    } else {
        for arg in &p.positionals {
            let text = arg.to_string_lossy().into_owned();
            match parse_operand(&text) {
                Some(v) => print_factors(&v, exponents, io)?,
                None => {
                    writeln!(
                        io.stderr,
                        "factor: '{text}' is not a valid positive integer"
                    )?;
                    had_error = true;
                }
            }
        }
    }
    Ok(if had_error { 1 } else { 0 })
}

#[cfg(test)]
mod tests {
    use std::ffi::OsString;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str]) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("factor");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::factor_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    fn run_stdin(stdin: &str, args: &[&str]) -> (u32, String, String) {
        let mut io = MemIo::new(stdin);
        let sc = ScratchCtx::new("factor-stdin");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::factor_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn small_values() {
        assert_eq!(run(&["12"]).1, "12: 2 2 3\n");
        assert_eq!(run(&["1"]).1, "1:\n");
        assert_eq!(run(&["0"]).1, "0:\n");
        assert_eq!(run(&["2"]).1, "2: 2\n");
        assert_eq!(run(&["97"]).1, "97: 97\n");
        assert_eq!(run(&["100"]).1, "100: 2 2 5 5\n");
    }

    #[test]
    fn exponents_form() {
        assert_eq!(run(&["-h", "72"]).1, "72: 2^3 3^2\n");
        assert_eq!(run(&["--exponents", "72"]).1, "72: 2^3 3^2\n");
        assert_eq!(run(&["-h", "17"]).1, "17: 17\n");
        // WinuxCmd extension alias
        assert_eq!(run(&["-e", "72"]).1, "72: 2^3 3^2\n");
    }

    #[test]
    fn sixtyfour_bit_semiprime() {
        // 2^64-1: GNU probed "3 5 17 257 641 65537 6700417"
        assert_eq!(
            run(&["18446744073709551615"]).1,
            "18446744073709551615: 3 5 17 257 641 65537 6700417\n"
        );
    }

    #[test]
    fn big_semiprimes() {
        // 2^127-1 (Mersenne prime): exercises the Miller-Rabin-only path on a
        // 128-bit operand.
        assert_eq!(
            run(&["170141183460469231731687303715884105727"]).1,
            "170141183460469231731687303715884105727: 170141183460469231731687303715884105727\n"
        );
        // 25-digit semiprime 10000000019 * 1000000000039 (probed GNU). The
        // smallest factor stays near 1e10 so the std-only rho finds it in
        // ~1e5 steps; operands whose smallest factor approaches 2^65 (e.g.
        // Fermat F7) take rho ~sqrt(p) iterations and are a documented
        // performance boundary of this port, not a correctness one.
        assert_eq!(
            run(&["10000000019390000000741"]).1,
            "10000000019390000000741: 10000000019 1000000000039\n"
        );
    }

    #[test]
    fn operand_edges() {
        assert_eq!(run(&["+5"]).1, "5: 5\n");
        assert_eq!(run(&[" 5"]).1, "5: 5\n");
        let (r, out, err) = run(&["5 "]);
        assert_eq!(r, 1);
        // GNU emits nothing for the malformed token (probed), C++ likewise.
        assert_eq!(out, "");
        assert!(err.contains("'5 ' is not a valid positive integer"));
        // A bare "-5" is an option cluster for getopt on both sides
        // ("invalid option -- '5'"); after "--" it is an invalid operand.
        let (r, _, err) = run(&["--", "-5"]);
        assert_eq!(r, 1);
        assert!(err.contains("'-5'"));
        let (r, _, err) = run(&[""]);
        assert_eq!(r, 1);
        assert!(err.contains("''"));
        let (r, _, err) = run(&["abc", "def"]);
        assert_eq!(r, 1);
        assert!(err.contains("'abc'"));
        assert!(err.contains("'def'"));
    }

    #[test]
    fn stdin_tokens() {
        let (r, out, err) = run_stdin("6 7\nabc 8\n", &[]);
        assert_eq!(r, 1);
        assert_eq!(out, "6: 2 3\n7: 7\n8: 2 2 2\n");
        assert!(err.contains("'abc'"));
        // \r stays inside the token (factor.c DELIM has no \r; probed GNU)
        let (r, out, err) = run_stdin("6\t7\r\n", &[]);
        assert_eq!(r, 1);
        assert_eq!(out, "6: 2 3\n");
        assert!(err.contains("'7\r'"));
        let (r, out, _) = run_stdin("", &[]);
        assert_eq!((r, out.as_str()), (0, ""));
    }

    #[test]
    fn help_version_surface() {
        let (r, out, _) = run(&["--help"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("Usage: factor [OPTION] [NUMBER]...\n"));
        assert!(out.ends_with("info '(coreutils) factor invocation'\n"));
        let (r, out, _) = run(&["--version"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("factor (GNU coreutils) 9.4\n"));
        assert!(out.ends_with("Written by Paul Rubin, Torbjörn Granlund, and Niels Möller.\n"));
    }

    #[test]
    fn bad_option() {
        let (r, _, err) = run(&["-x", "5"]);
        assert_eq!(r, 1);
        assert!(err.contains("invalid option -- 'x'"));
    }
}
