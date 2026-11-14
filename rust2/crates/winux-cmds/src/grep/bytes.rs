//! Byte-search primitives for the grep scanner (std-only).
//!
//! GNU grep leans on glibc `memchr`/`memrchr` and a Boyer-Moore kwset; std
//! exposes neither, so these are the equivalents: SSE2 on x86_64 (baseline
//! ISA, always available there), a word-at-a-time scan elsewhere. Every
//! function is a pure function of its slices and has a scalar reference
//! twin in the unit tests.

/// First index of `needle` in `hay`.
#[inline]
pub fn memchr(needle: u8, hay: &[u8]) -> Option<usize> {
    #[cfg(target_arch = "x86_64")]
    {
        sse2::memchr(needle, hay)
    }
    #[cfg(not(target_arch = "x86_64"))]
    {
        swar_memchr(needle, hay)
    }
}

/// Last index of `needle` in `hay`.
#[inline]
pub fn memrchr(needle: u8, hay: &[u8]) -> Option<usize> {
    #[cfg(target_arch = "x86_64")]
    {
        sse2::memrchr(needle, hay)
    }
    #[cfg(not(target_arch = "x86_64"))]
    {
        hay.iter().rposition(|&b| b == needle)
    }
}

/// Number of `needle` bytes in `hay` (GNU nlscan).
pub fn count(needle: u8, hay: &[u8]) -> usize {
    #[cfg(target_arch = "x86_64")]
    {
        sse2::count(needle, hay)
    }
    #[cfg(not(target_arch = "x86_64"))]
    {
        hay.iter().filter(|&&b| b == needle).count()
    }
}

#[cfg(not(target_arch = "x86_64"))]
fn swar_memchr(needle: u8, hay: &[u8]) -> Option<usize> {
    const LO: u64 = 0x0101_0101_0101_0101;
    const HI: u64 = 0x8080_8080_8080_8080;
    let rep = LO.wrapping_mul(needle as u64);
    let mut i = 0usize;
    while i + 8 <= hay.len() {
        let w = u64::from_le_bytes(hay[i..i + 8].try_into().expect("8 bytes"));
        let x = w ^ rep;
        if (x.wrapping_sub(LO) & !x & HI) != 0 {
            break;
        }
        i += 8;
    }
    hay[i..].iter().position(|&b| b == needle).map(|p| p + i)
}

/// A compiled single-needle substring searcher (case-sensitive bytes).
#[derive(Clone, Debug)]
pub struct Finder {
    needle: Vec<u8>,
    /// index of the second probe byte (the first probe is needle[0])
    second: usize,
}

impl Finder {
    pub fn new(needle: &[u8]) -> Finder {
        // Probe the first byte and the rarest-looking other byte: a byte
        // that is not a common ASCII letter/space keeps the candidate rate
        // low (GNU's kwset picks the last byte; a rarity heuristic is the
        // memchr-crate refinement of the same idea).
        let mut second = needle.len().saturating_sub(1);
        let mut best = u32::MAX;
        for (i, &b) in needle.iter().enumerate().skip(1) {
            let r = rank(b);
            if r < best {
                best = r;
                second = i;
            }
        }
        Finder {
            needle: needle.to_vec(),
            second,
        }
    }

    /// First occurrence at or after `from`.
    pub fn find(&self, hay: &[u8], from: usize) -> Option<usize> {
        let n = self.needle.len();
        if n == 0 {
            return if from <= hay.len() { Some(from) } else { None };
        }
        if from + n > hay.len() {
            return None;
        }
        if n == 1 {
            return memchr(self.needle[0], &hay[from..]).map(|p| p + from);
        }
        #[cfg(target_arch = "x86_64")]
        {
            sse2::find_pair(&self.needle, self.second, hay, from)
        }
        #[cfg(not(target_arch = "x86_64"))]
        {
            let first = self.needle[0];
            let mut i = from;
            while i + n <= hay.len() {
                let p = memchr(first, &hay[i..hay.len() - n + 1])? + i;
                if &hay[p..p + n] == self.needle.as_slice() {
                    return Some(p);
                }
                i = p + 1;
            }
            None
        }
    }
}

/// Rough byte frequency rank in text (lower = rarer).
fn rank(b: u8) -> u32 {
    match b {
        b' ' => 1000,
        b'e' | b't' | b'a' | b'o' | b'i' | b'n' | b's' | b'r' => 900,
        b'h' | b'l' | b'd' | b'c' | b'u' | b'm' => 700,
        b'a'..=b'z' => 500,
        b'0'..=b'9' => 400,
        b'A'..=b'Z' => 300,
        b'\t' | b'.' | b',' | b'-' | b'_' => 350,
        0x80..=0xFF => 200,
        _ => 100,
    }
}

#[cfg(target_arch = "x86_64")]
mod sse2 {
    use std::arch::x86_64::*;

    #[inline]
    pub fn memchr(needle: u8, hay: &[u8]) -> Option<usize> {
        let len = hay.len();
        let ptr = hay.as_ptr();
        let mut i = 0usize;
        // SAFETY: SSE2 is part of the x86_64 baseline; every load reads 16
        // bytes inside `hay` (i + 16 <= len) through unaligned loads.
        unsafe {
            let v = _mm_set1_epi8(needle as i8);
            while i + 64 <= len {
                let a = _mm_cmpeq_epi8(_mm_loadu_si128(ptr.add(i) as *const __m128i), v);
                let b = _mm_cmpeq_epi8(_mm_loadu_si128(ptr.add(i + 16) as *const __m128i), v);
                let c = _mm_cmpeq_epi8(_mm_loadu_si128(ptr.add(i + 32) as *const __m128i), v);
                let d = _mm_cmpeq_epi8(_mm_loadu_si128(ptr.add(i + 48) as *const __m128i), v);
                let any = _mm_or_si128(_mm_or_si128(a, b), _mm_or_si128(c, d));
                if _mm_movemask_epi8(any) != 0 {
                    for (k, m) in [a, b, c, d].iter().enumerate() {
                        let mask = _mm_movemask_epi8(*m);
                        if mask != 0 {
                            return Some(i + k * 16 + mask.trailing_zeros() as usize);
                        }
                    }
                }
                i += 64;
            }
            while i + 16 <= len {
                let m = _mm_movemask_epi8(_mm_cmpeq_epi8(
                    _mm_loadu_si128(ptr.add(i) as *const __m128i),
                    v,
                ));
                if m != 0 {
                    return Some(i + m.trailing_zeros() as usize);
                }
                i += 16;
            }
        }
        hay[i..].iter().position(|&b| b == needle).map(|p| p + i)
    }

    #[inline]
    pub fn memrchr(needle: u8, hay: &[u8]) -> Option<usize> {
        let ptr = hay.as_ptr();
        let mut end = hay.len();
        // SAFETY: as in memchr; loads cover [end - 16, end) within `hay`.
        unsafe {
            let v = _mm_set1_epi8(needle as i8);
            while end >= 16 {
                let m = _mm_movemask_epi8(_mm_cmpeq_epi8(
                    _mm_loadu_si128(ptr.add(end - 16) as *const __m128i),
                    v,
                ));
                if m != 0 {
                    return Some(end - 16 + (31 - (m as u32).leading_zeros()) as usize);
                }
                end -= 16;
            }
        }
        hay[..end].iter().rposition(|&b| b == needle)
    }

    pub fn count(needle: u8, hay: &[u8]) -> usize {
        let len = hay.len();
        let ptr = hay.as_ptr();
        let mut i = 0usize;
        let mut total = 0usize;
        // SAFETY: as in memchr.
        unsafe {
            let v = _mm_set1_epi8(needle as i8);
            while i + 16 <= len {
                let m = _mm_movemask_epi8(_mm_cmpeq_epi8(
                    _mm_loadu_si128(ptr.add(i) as *const __m128i),
                    v,
                ));
                total += (m as u32).count_ones() as usize;
                i += 16;
            }
        }
        total + hay[i..].iter().filter(|&&b| b == needle).count()
    }

    /// Packed-pair substring search: candidates are positions where both
    /// probe bytes match, each verified with a slice compare.
    pub fn find_pair(needle: &[u8], second: usize, hay: &[u8], from: usize) -> Option<usize> {
        let n = needle.len();
        let last_start = hay.len() - n; // caller guarantees from + n <= len
        let ptr = hay.as_ptr();
        let mut i = from;
        // SAFETY: loads at i and i + second read 16 bytes each; the loop
        // keeps i + 16 <= last_start + 1, so i + second + 16 <= len.
        unsafe {
            let v1 = _mm_set1_epi8(needle[0] as i8);
            let v2 = _mm_set1_epi8(needle[second] as i8);
            while i + 16 <= last_start + 1 {
                let a = _mm_cmpeq_epi8(_mm_loadu_si128(ptr.add(i) as *const __m128i), v1);
                let b = _mm_cmpeq_epi8(_mm_loadu_si128(ptr.add(i + second) as *const __m128i), v2);
                let mut mask = _mm_movemask_epi8(_mm_and_si128(a, b)) as u32;
                while mask != 0 {
                    let p = i + mask.trailing_zeros() as usize;
                    if hay.get_unchecked(p..p + n) == needle {
                        return Some(p);
                    }
                    mask &= mask - 1;
                }
                i += 16;
            }
        }
        while i <= last_start {
            if hay[i] == needle[0] && &hay[i..i + n] == needle {
                return Some(i);
            }
            i += 1;
        }
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn lcg(seed: &mut u64) -> u64 {
        *seed = seed
            .wrapping_mul(6364136223846793005)
            .wrapping_add(1442695040888963407);
        *seed >> 33
    }

    #[test]
    fn primitives_match_scalar_reference() {
        let mut seed = 7u64;
        for len in 0..300usize {
            let hay: Vec<u8> = (0..len)
                .map(|_| b"abc\nx"[(lcg(&mut seed) % 5) as usize])
                .collect();
            for needle in [b'a', b'\n', b'z'] {
                assert_eq!(memchr(needle, &hay), hay.iter().position(|&b| b == needle));
                assert_eq!(
                    memrchr(needle, &hay),
                    hay.iter().rposition(|&b| b == needle)
                );
                assert_eq!(
                    count(needle, &hay),
                    hay.iter().filter(|&&b| b == needle).count()
                );
            }
            for pat in [&b"ab"[..], b"abc", b"x\na", b"cc", b"abcabcab", b"q"] {
                let f = Finder::new(pat);
                for from in [0usize, 1, 5, 17] {
                    let want = if from + pat.len() <= hay.len() {
                        (from..=hay.len() - pat.len()).find(|&i| &hay[i..i + pat.len()] == pat)
                    } else {
                        None
                    };
                    assert_eq!(f.find(&hay, from), want, "{pat:?} from {from} in {hay:?}");
                }
            }
        }
    }
}
