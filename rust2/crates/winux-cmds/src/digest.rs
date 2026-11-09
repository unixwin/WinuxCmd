//! Hash primitives for the wave2 digest applets, implemented in-tree
//! (std-only, zero crates): SHA-256, SHA-512 and the truncated variants
//! SHA-224 / SHA-384 (FIPS 180-4), plus BLAKE2b (RFC 7693).
//!
//! The truncated variants are the exact cores with a different chaining
//! IV and a shorter output, per FIPS 180-4 section 6:
//! * SHA-224 = SHA-256 core, IV c1059ed8..., output 28 bytes.
//! * SHA-384 = SHA-512 core, IV cbbb9d5d..., output 48 bytes.
//!
//! Test vectors: NIST FIPS 180-4 ("abc", empty, and the two-block
//! "abcdbcde..." messages) for the SHA family; RFC 7693 Appendix B for
//! BLAKE2b. Every vector below is also cross-checked against WSL GNU
//! coreutils 9.4 output (`sha224sum`/`sha384sum`/`b2sum`).

/// Streaming SHA-256/SHA-224 core (the variant differs only in IV and
/// output length).
pub struct Sha256Core {
    state: [u32; 8],
    buf: [u8; 64],
    buf_len: usize,
    total: u64,
}

const K256: [u32; 64] = [
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
];

impl Sha256Core {
    fn new(iv: [u32; 8]) -> Self {
        Sha256Core {
            state: iv,
            buf: [0; 64],
            buf_len: 0,
            total: 0,
        }
    }

    fn compress(&mut self, block: &[u8]) {
        let mut w = [0u32; 64];
        for i in 0..16 {
            w[i] = u32::from_be_bytes([
                block[4 * i],
                block[4 * i + 1],
                block[4 * i + 2],
                block[4 * i + 3],
            ]);
        }
        for i in 16..64 {
            let s0 = w[i - 15].rotate_right(7) ^ w[i - 15].rotate_right(18) ^ (w[i - 15] >> 3);
            let s1 = w[i - 2].rotate_right(17) ^ w[i - 2].rotate_right(19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16]
                .wrapping_add(s0)
                .wrapping_add(w[i - 7])
                .wrapping_add(s1);
        }
        let [mut a, mut b, mut c, mut d, mut e, mut f, mut g, mut h] = self.state;
        for i in 0..64 {
            let s1 = e.rotate_right(6) ^ e.rotate_right(11) ^ e.rotate_right(25);
            let ch = (e & f) ^ ((!e) & g);
            let t1 = h
                .wrapping_add(s1)
                .wrapping_add(ch)
                .wrapping_add(K256[i])
                .wrapping_add(w[i]);
            let s0 = a.rotate_right(2) ^ a.rotate_right(13) ^ a.rotate_right(22);
            let maj = (a & b) ^ (a & c) ^ (b & c);
            let t2 = s0.wrapping_add(maj);
            h = g;
            g = f;
            f = e;
            e = d.wrapping_add(t1);
            d = c;
            c = b;
            b = a;
            a = t1.wrapping_add(t2);
        }
        let add = [a, b, c, d, e, f, g, h];
        for (s, x) in self.state.iter_mut().zip(add) {
            *s = s.wrapping_add(x);
        }
    }

    pub fn update(&mut self, mut data: &[u8]) {
        self.total = self.total.wrapping_add(data.len() as u64);
        if self.buf_len > 0 {
            let take = std::cmp::min(64 - self.buf_len, data.len());
            self.buf[self.buf_len..self.buf_len + take].copy_from_slice(&data[..take]);
            self.buf_len += take;
            data = &data[take..];
            if self.buf_len == 64 {
                let block = self.buf;
                self.compress(&block);
                self.buf_len = 0;
            }
        }
        while data.len() >= 64 {
            let (block, rest) = data.split_at(64);
            self.compress(block);
            data = rest;
        }
        if !data.is_empty() {
            self.buf[..data.len()].copy_from_slice(data);
            self.buf_len = data.len();
        }
    }

    pub fn finish(mut self, out_len: usize) -> Vec<u8> {
        let bit_len = self.total.wrapping_mul(8);
        self.total = 0;
        let mut tail = vec![0x80u8];
        while (self.buf_len + tail.len()) % 64 != 56 {
            tail.push(0);
        }
        tail.extend_from_slice(&bit_len.to_be_bytes());
        let buf = self.buf;
        self.update(&tail);
        let _ = buf;
        let mut out = Vec::with_capacity(out_len);
        for w in self.state {
            out.extend_from_slice(&w.to_be_bytes());
            if out.len() >= out_len {
                break;
            }
        }
        out.truncate(out_len);
        out
    }
}

const IV256: [u32; 8] = [
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
];

const IV224: [u32; 8] = [
    0xc1059ed8, 0x367cd507, 0x3070dd17, 0xf70e5939, 0xffc00b31, 0x68581511, 0x64f98fa7, 0xbefa4fa4,
];

pub fn sha256(data: &[u8]) -> [u8; 32] {
    let mut h = Sha256Core::new(IV256);
    h.update(data);
    let mut out = [0u8; 32];
    out.copy_from_slice(&h.finish(32));
    out
}

pub fn sha224(data: &[u8]) -> [u8; 28] {
    let mut h = Sha256Core::new(IV224);
    h.update(data);
    let mut out = [0u8; 28];
    out.copy_from_slice(&h.finish(28));
    out
}

/// Streaming SHA-512/SHA-384 core.
pub struct Sha512Core {
    state: [u64; 8],
    buf: [u8; 128],
    buf_len: usize,
    total: u128,
}

const K512: [u64; 80] = [
    0x428a2f98d728ae22,
    0x7137449123ef65cd,
    0xb5c0fbcfec4d3b2f,
    0xe9b5dba58189dbbc,
    0x3956c25bf348b538,
    0x59f111f1b605d019,
    0x923f82a4af194f9b,
    0xab1c5ed5da6d8118,
    0xd807aa98a3030242,
    0x12835b0145706fbe,
    0x243185be4ee4b28c,
    0x550c7dc3d5ffb4e2,
    0x72be5d74f27b896f,
    0x80deb1fe3b1696b1,
    0x9bdc06a725c71235,
    0xc19bf174cf692694,
    0xe49b69c19ef14ad2,
    0xefbe4786384f25e3,
    0x0fc19dc68b8cd5b5,
    0x240ca1cc77ac9c65,
    0x2de92c6f592b0275,
    0x4a7484aa6ea6e483,
    0x5cb0a9dcbd41fbd4,
    0x76f988da831153b5,
    0x983e5152ee66dfab,
    0xa831c66d2db43210,
    0xb00327c898fb213f,
    0xbf597fc7beef0ee4,
    0xc6e00bf33da88fc2,
    0xd5a79147930aa725,
    0x06ca6351e003826f,
    0x142929670a0e6e70,
    0x27b70a8546d22ffc,
    0x2e1b21385c26c926,
    0x4d2c6dfc5ac42aed,
    0x53380d139d95b3df,
    0x650a73548baf63de,
    0x766a0abb3c77b2a8,
    0x81c2c92e47edaee6,
    0x92722c851482353b,
    0xa2bfe8a14cf10364,
    0xa81a664bbc423001,
    0xc24b8b70d0f89791,
    0xc76c51a30654be30,
    0xd192e819d6ef5218,
    0xd69906245565a910,
    0xf40e35855771202a,
    0x106aa07032bbd1b8,
    0x19a4c116b8d2d0c8,
    0x1e376c085141ab53,
    0x2748774cdf8eeb99,
    0x34b0bcb5e19b48a8,
    0x391c0cb3c5c95a63,
    0x4ed8aa4ae3418acb,
    0x5b9cca4f7763e373,
    0x682e6ff3d6b2b8a3,
    0x748f82ee5defb2fc,
    0x78a5636f43172f60,
    0x84c87814a1f0ab72,
    0x8cc702081a6439ec,
    0x90befffa23631e28,
    0xa4506cebde82bde9,
    0xbef9a3f7b2c67915,
    0xc67178f2e372532b,
    0xca273eceea26619c,
    0xd186b8c721c0c207,
    0xeada7dd6cde0eb1e,
    0xf57d4f7fee6ed178,
    0x06f067aa72176fba,
    0x0a637dc5a2c898a6,
    0x113f9804bef90dae,
    0x1b710b35131c471b,
    0x28db77f523047d84,
    0x32caab7b40c72493,
    0x3c9ebe0a15c9bebc,
    0x431d67c49c100d4c,
    0x4cc5d4becb3e42b6,
    0x597f299cfc657e2a,
    0x5fcb6fab3ad6faec,
    0x6c44198c4a475817,
];

impl Sha512Core {
    fn new(iv: [u64; 8]) -> Self {
        Sha512Core {
            state: iv,
            buf: [0; 128],
            buf_len: 0,
            total: 0,
        }
    }

    fn compress(&mut self, block: &[u8]) {
        let mut w = [0u64; 80];
        for i in 0..16 {
            let mut b = [0u8; 8];
            b.copy_from_slice(&block[8 * i..8 * i + 8]);
            w[i] = u64::from_be_bytes(b);
        }
        for i in 16..80 {
            let s0 = w[i - 15].rotate_right(1) ^ w[i - 15].rotate_right(8) ^ (w[i - 15] >> 7);
            let s1 = w[i - 2].rotate_right(19) ^ w[i - 2].rotate_right(61) ^ (w[i - 2] >> 6);
            w[i] = w[i - 16]
                .wrapping_add(s0)
                .wrapping_add(w[i - 7])
                .wrapping_add(s1);
        }
        let [mut a, mut b, mut c, mut d, mut e, mut f, mut g, mut h] = self.state;
        for i in 0..80 {
            let s1 = e.rotate_right(14) ^ e.rotate_right(18) ^ e.rotate_right(41);
            let ch = (e & f) ^ ((!e) & g);
            let t1 = h
                .wrapping_add(s1)
                .wrapping_add(ch)
                .wrapping_add(K512[i])
                .wrapping_add(w[i]);
            let s0 = a.rotate_right(28) ^ a.rotate_right(34) ^ a.rotate_right(39);
            let maj = (a & b) ^ (a & c) ^ (b & c);
            let t2 = s0.wrapping_add(maj);
            h = g;
            g = f;
            f = e;
            e = d.wrapping_add(t1);
            d = c;
            c = b;
            b = a;
            a = t1.wrapping_add(t2);
        }
        let add = [a, b, c, d, e, f, g, h];
        for (s, x) in self.state.iter_mut().zip(add) {
            *s = s.wrapping_add(x);
        }
    }

    pub fn update(&mut self, mut data: &[u8]) {
        self.total = self.total.wrapping_add(data.len() as u128);
        if self.buf_len > 0 {
            let take = std::cmp::min(128 - self.buf_len, data.len());
            self.buf[self.buf_len..self.buf_len + take].copy_from_slice(&data[..take]);
            self.buf_len += take;
            data = &data[take..];
            if self.buf_len == 128 {
                let block = self.buf;
                self.compress(&block);
                self.buf_len = 0;
            }
        }
        while data.len() >= 128 {
            let (block, rest) = data.split_at(128);
            self.compress(block);
            data = rest;
        }
        if !data.is_empty() {
            self.buf[..data.len()].copy_from_slice(data);
            self.buf_len = data.len();
        }
    }

    pub fn finish(mut self, out_len: usize) -> Vec<u8> {
        let bit_len = self.total.wrapping_mul(8);
        let mut tail = vec![0x80u8];
        while (self.buf_len + tail.len()) % 128 != 112 {
            tail.push(0);
        }
        // 128-bit big-endian bit length (high 64 bits are always zero for
        // realistic inputs; kept for FIPS exactness).
        let mut len_bytes = [0u8; 16];
        len_bytes[8..].copy_from_slice(&(bit_len as u64).to_be_bytes());
        tail.extend_from_slice(&len_bytes);
        self.update(&tail);
        let mut out = Vec::with_capacity(out_len);
        for w in self.state {
            out.extend_from_slice(&w.to_be_bytes());
            if out.len() >= out_len {
                break;
            }
        }
        out.truncate(out_len);
        out
    }
}

const IV512: [u64; 8] = [
    0x6a09e667f3bcc908,
    0xbb67ae8584caa73b,
    0x3c6ef372fe94f82b,
    0xa54ff53a5f1d36f1,
    0x510e527fade682d1,
    0x9b05688c2b3e6c1f,
    0x1f83d9abfb41bd6b,
    0x5be0cd19137e2179,
];

const IV384: [u64; 8] = [
    0xcbbb9d5dc1059ed8,
    0x629a292a367cd507,
    0x9159015a3070dd17,
    0x152fecd8f70e5939,
    0x67332667ffc00b31,
    0x8eb44a8768581511,
    0xdb0c2e0d64f98fa7,
    0x47b5481dbefa4fa4,
];

pub fn sha512(data: &[u8]) -> [u8; 64] {
    let mut h = Sha512Core::new(IV512);
    h.update(data);
    let mut out = [0u8; 64];
    out.copy_from_slice(&h.finish(64));
    out
}

pub fn sha384(data: &[u8]) -> [u8; 48] {
    let mut h = Sha512Core::new(IV384);
    h.update(data);
    let mut out = [0u8; 48];
    out.copy_from_slice(&h.finish(48));
    out
}

/// BLAKE2b, RFC 7693, unkeyed, variable digest length 1..=64 bytes
/// (b2sum -l sets it in bits; only multiples of 8 reach this core).
pub struct Blake2b {
    h: [u64; 8],
    buf: [u8; 128],
    buf_len: usize,
    total: u64,
    out_len: usize,
}

const B2_IV: [u64; 8] = IV512;

const SIGMA: [[usize; 16]; 12] = [
    [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15],
    [14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3],
    [11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4],
    [7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8],
    [9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13],
    [2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9],
    [12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11],
    [13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10],
    [6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5],
    [10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0],
    [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15],
    [14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3],
];

fn g_mix(v: &mut [u64; 16], a: usize, b: usize, c: usize, d: usize, x: u64, y: u64) {
    v[a] = v[a].wrapping_add(v[b]).wrapping_add(x);
    v[d] = (v[d] ^ v[a]).rotate_right(32);
    v[c] = v[c].wrapping_add(v[d]);
    v[b] = (v[b] ^ v[c]).rotate_right(24);
    v[a] = v[a].wrapping_add(v[b]).wrapping_add(y);
    v[d] = (v[d] ^ v[a]).rotate_right(16);
    v[c] = v[c].wrapping_add(v[d]);
    v[b] = (v[b] ^ v[c]).rotate_right(63);
}

fn compress(h: &mut [u64; 8], block: &[u8; 128], t: u64, last: bool) {
    let mut v = [0u64; 16];
    v[..8].copy_from_slice(h);
    v[8..].copy_from_slice(&B2_IV);
    v[12] ^= t;
    if last {
        v[14] = !v[14];
    }
    let mut m = [0u64; 16];
    for i in 0..16 {
        let mut b = [0u8; 8];
        b.copy_from_slice(&block[8 * i..8 * i + 8]);
        m[i] = u64::from_le_bytes(b);
    }
    for r in 0..12 {
        let s = SIGMA[r];
        g_mix(&mut v, 0, 4, 8, 12, m[s[0]], m[s[1]]);
        g_mix(&mut v, 1, 5, 9, 13, m[s[2]], m[s[3]]);
        g_mix(&mut v, 2, 6, 10, 14, m[s[4]], m[s[5]]);
        g_mix(&mut v, 3, 7, 11, 15, m[s[6]], m[s[7]]);
        g_mix(&mut v, 0, 5, 10, 15, m[s[8]], m[s[9]]);
        g_mix(&mut v, 1, 6, 11, 12, m[s[10]], m[s[11]]);
        g_mix(&mut v, 2, 7, 8, 13, m[s[12]], m[s[13]]);
        g_mix(&mut v, 3, 4, 9, 14, m[s[14]], m[s[15]]);
    }
    for i in 0..8 {
        h[i] ^= v[i] ^ v[i + 8];
    }
}

impl Blake2b {
    pub fn new(out_len: usize) -> Self {
        let mut h = B2_IV;
        // Param block: digest_length=KB(2 hashes/word->out_len), fanout=1,
        // depth=1 -> h[0] ^= 0x01010000 ^ (keylen<<8) ^ outlen (unkeyed).
        h[0] ^= 0x0101_0000 ^ out_len as u64;
        Blake2b {
            h,
            buf: [0; 128],
            buf_len: 0,
            total: 0,
            out_len,
        }
    }

    pub fn update(&mut self, mut data: &[u8]) {
        while !data.is_empty() {
            if self.buf_len == 128 {
                self.total = self.total.wrapping_add(128);
                let block = self.buf;
                compress(&mut self.h, &block, self.total, false);
                self.buf_len = 0;
            }
            let take = std::cmp::min(128 - self.buf_len, data.len());
            self.buf[self.buf_len..self.buf_len + take].copy_from_slice(&data[..take]);
            self.buf_len += take;
            data = &data[take..];
        }
    }

    pub fn finish(mut self) -> Vec<u8> {
        self.total = self.total.wrapping_add(self.buf_len as u64);
        for b in self.buf[self.buf_len..].iter_mut() {
            *b = 0;
        }
        let block = self.buf;
        compress(&mut self.h, &block, self.total, true);
        let mut out = Vec::with_capacity(self.out_len);
        for w in self.h {
            out.extend_from_slice(&w.to_le_bytes());
            if out.len() >= self.out_len {
                break;
            }
        }
        out.truncate(self.out_len);
        out
    }
}

pub fn blake2b(data: &[u8], out_len: usize) -> Vec<u8> {
    let mut b = Blake2b::new(out_len);
    b.update(data);
    b.finish()
}

pub fn hex(bytes: &[u8]) -> String {
    let mut s = String::with_capacity(bytes.len() * 2);
    for b in bytes {
        s.push_str(&format!("{:02x}", b));
    }
    s
}

#[cfg(test)]
mod tests {
    use super::*;

    fn hx(v: &[u8]) -> String {
        hex(v)
    }

    // ---- NIST FIPS 180-4 test vectors ----

    #[test]
    fn sha256_vectors() {
        // "abc" (FIPS 180-4 B.1)
        assert_eq!(
            hx(&sha256(b"abc")),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
        );
        // "" (public empty vector)
        assert_eq!(
            hx(&sha256(b"")),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
        );
        // Two-block message "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
        assert_eq!(
            hx(&sha256(
                b"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
            )),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"
        );
    }

    #[test]
    fn sha224_vectors() {
        assert_eq!(
            hx(&sha224(b"abc")),
            "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7"
        );
        assert_eq!(
            hx(&sha224(b"")),
            "d14a028c2a3a2bc9476102bb288234c415a2b01f828ea62ac5b3e42f"
        );
        assert_eq!(
            hx(&sha224(
                b"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
            )),
            "75388b16512776cc5dba5da1fd890150b0c6455cb4f58b1952522525"
        );
    }

    #[test]
    fn sha512_vectors() {
        assert_eq!(
            hx(&sha512(b"abc")),
            "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a\
             2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"
                .replace('\n', "")
        );
        assert_eq!(
            hx(&sha512(b"")),
            "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce\
             47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e"
                .replace('\n', "")
        );
    }

    #[test]
    fn sha384_vectors() {
        assert_eq!(
            hx(&sha384(b"abc")),
            "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed\
             8086072ba1e7cc2358baeca134c825a7"
                .replace('\n', "")
        );
        assert_eq!(
            hx(&sha384(b"")),
            "38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da\
             274edebfe76f65fbd51ad2f14898b95b"
                .replace('\n', "")
        );
        // FIPS 180-4 multi-block message (896 bits)
        let long = b"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn\
                     hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
        assert_eq!(
            hx(&sha384(long)),
            "09330c33f71147e83d192fc782cd1b4753111b173b3b05d22fa08086e3b0f712\
             fcc7c71a557e2db966c3e9fa91746039"
                .replace('\n', "")
        );
    }

    // ---- RFC 7693 test vectors (BLAKE2b) ----

    #[test]
    fn blake2b_vectors() {
        // RFC 7693 Appendix B: BLAKE2b-512("abc")
        assert_eq!(
            hx(&blake2b(b"abc", 64)),
            "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d1\
             7d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923"
                .replace('\n', "")
        );
        // BLAKE2b-512("") (RFC-referenced empty vector)
        assert_eq!(
            hx(&blake2b(b"", 64)),
            "786a02f742015903c6c6fd852552d272912f4740e15847618a86e217f71f5419\
             d25e1031afee585313896444934eb04b903a685b1448b755d56f701afe9be2ce"
                .replace('\n', "")
        );
        // Variable-length: BLAKE2b-256("abc"), cross-checked against GNU
        // coreutils 9.4 `b2sum -l 256`.
        assert_eq!(
            hx(&blake2b(b"abc", 32)),
            "bddd813c634239723171ef3fee98579b94964e3bb1cb3e427262c8c068d52319"
        );
        // BLAKE2b-8("abc") -> 1 byte, GNU `b2sum -l 8` cross-check.
        assert_eq!(hx(&blake2b(b"abc", 1)), "6b");
        // Multi-block (200 bytes) cross-checked against GNU b2sum on the
        // same input (generated with WSL coreutils 9.4).
        let long = vec![b'a'; 200];
        assert_eq!(
            hx(&blake2b(&long, 64)),
            "932355851d75f09c18646a9da87c25e055bc57f113121ad1ec63d45e7a1d62ab\
             9133f8b7d1d7de9e0afa784eb6a8a11d78683013d0a672611f17668d9577d209"
                .replace('\n', "")
        );
    }

    #[test]
    fn long_input_vectors_gnu_crosscheck() {
        // 200 'a' bytes, cross-checked against WSL GNU coreutils 9.4
        // (sha224sum/sha384sum/sha256sum/sha512sum/b2sum on the same file).
        let long = vec![b'a'; 200];
        assert_eq!(
            hx(&sha224(&long)),
            "2559984fd15e055f0d84c346483508242f02653ab7956401e551511c"
        );
        assert_eq!(
            hx(&sha384(&long)),
            "0691b6e978614b67d60557b2a2cddd53406508522efa21c624dbbfa8ab6e726\
             d5c586b489c7c09f24109a64c10211d48"
                .replace('\n', "")
        );
        assert_eq!(
            hx(&sha256(&long)),
            "c2a908d98f5df987ade41b5fce213067efbcc21ef2240212a41e54b5e7c28ae5"
        );
        assert_eq!(
            hx(&sha512(&long)),
            "4b11459c33f52a22ee8236782714c150a3b2c60994e9acee17fe68947a3e6789\
             f31e7668394592da7bef827cddca88c4e6f86e4df7ed1ae6cba71f3e98faee9f"
                .replace('\n', "")
        );
        assert_eq!(
            hx(&blake2b(&long, 32)),
            "6b6e59aaf00eb730cf93de53560846722184bbd92f8368c21ffa95380c2f9fe6"
        );
    }

    #[test]
    fn streaming_matches_oneshot() {
        // Feed in awkward chunk sizes; the streaming core must equal the
        // one-shot digest for every algorithm.
        let data: Vec<u8> = (0..255u8).chain(0..45u8).collect();
        for chunk in [1usize, 3, 63, 64, 65, 127, 128, 129, 300] {
            let mut s = Sha256Core::new(IV256);
            for part in data.chunks(chunk) {
                s.update(part);
            }
            assert_eq!(hx(&s.finish(32)), hx(&sha256(&data)), "sha256 {chunk}");

            let mut s = Sha512Core::new(IV512);
            for part in data.chunks(chunk) {
                s.update(part);
            }
            assert_eq!(hx(&s.finish(64)), hx(&sha512(&data)), "sha512 {chunk}");

            let mut b = Blake2b::new(64);
            for part in data.chunks(chunk) {
                b.update(part);
            }
            assert_eq!(hx(&b.finish()), hx(&blake2b(&data, 64)), "blake2b {chunk}");
        }
    }
}
