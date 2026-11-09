//! Shared RFC 4648 base-N codec engine for `base64` and `base32`
//! (GNU coreutils basenc semantics, probed against coreutils 9.4).
//!
//! Encoding: pure alphabet substitution, lines wrapped at `wrap` columns
//! (default 76, `0` = no wrapping and NO trailing newline; a wrap value
//! that overflows i64 maps to 0 like GNU's xstrtoimax overflow path).
//!
//! Decoding reproduces GNU's exact observable behavior (probed byte-for-byte
//! through `env base64 -d` / `env base32 -d` under WSL, see
//! target/gnu-probe/bd*.txt):
//! * `\n` is always skipped in data position; every other non-alphabet byte
//!   is an error unless `-i` (ignore-garbage); `=` is NEVER skipped, even
//!   with `-i`.
//! * Bytes flush when a quantum completes: base64 at 4 data chars or a pad
//!   run starting at data-count 2 or 3; base32 at 8 data chars or a pad run
//!   completing the quantum (n=2->6 pads, 4->4, 5->3, 7->1). A pad at any
//!   other position is an immediate error that first flushes the whole
//!   bytes accumulated so far ("ZgZm8=" keeps "f\x06f").
//! * Inside a pad run only more '=' complete the quantum: a newline, space
//!   or any other byte breaks the run and errors ('Zg=\n==' -> rc 1, "f"
//!   kept; 'Zg= ' -i -> rc 1; 'Zg== ' -i -> rc 0).
//! * At EOF with an incomplete quantum: base64 additionally flushes
//!   floor(bits/8) partial bytes before the error ("Zm9" -> "fo", rc 1);
//!   base32 flushes nothing ("MZ" -> empty, rc 1).
//! * Non-canonical bits decode silently ("ZZZZ" -> 65 96 59, rc 0).

const B64_ALPHABET: &[u8] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const B32_ALPHABET: &[u8] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

pub struct Codec {
    /// encode table (64 chars for base64, 32 for base32)
    pub enc: Vec<u8>,
    /// decode table, 128 entries, 0xff = invalid
    pub dec: [u8; 128],
    /// data chars per quantum (4 for base64, 8 for base32)
    pub quantum: usize,
    /// bits accumulated per data char (6 or 5)
    pub bits: u32,
    /// required trailing pad count for n data chars in the final quantum
    /// (base64: 2->2, 3->1; base32: 2->6, 4->4, 5->3, 7->1)
    pub pads_for: fn(usize) -> Option<usize>,
    /// base64 flushes the partial quantum as soon as a valid pad is seen
    /// ("Zm=v" keeps "f"); base32 flushes only when the pad run completes
    /// the quantum ("MY=" keeps nothing, "MZ======" keeps "f")
    pub flush_on_pad_accept: bool,
}

fn b64_pads_for(n: usize) -> Option<usize> {
    match n {
        2 => Some(2),
        3 => Some(1),
        _ => None,
    }
}

fn b32_pads_for(n: usize) -> Option<usize> {
    match n {
        2 => Some(6),
        4 => Some(4),
        5 => Some(3),
        7 => Some(1),
        _ => None,
    }
}

pub fn base64_codec() -> Codec {
    codec_for(B64_ALPHABET, 4, 6, b64_pads_for)
}

pub fn base32_codec() -> Codec {
    codec_for(B32_ALPHABET, 8, 5, b32_pads_for)
}

fn codec_for(
    alphabet: &[u8],
    quantum: usize,
    bits: u32,
    pads_for: fn(usize) -> Option<usize>,
) -> Codec {
    let enc = alphabet.to_vec();
    let mut dec = [0xffu8; 128];
    for (i, &c) in alphabet.iter().enumerate() {
        dec[c as usize] = i as u8;
    }
    Codec {
        enc,
        dec,
        quantum,
        bits,
        pads_for,
        flush_on_pad_accept: quantum == 4,
    }
}

/// Encode `data`; `wrap` == 0 means no wrapping and no trailing newline.
pub fn encode(codec: &Codec, data: &[u8], wrap: usize) -> Vec<u8> {
    let mut out = Vec::new();
    let mut col = 0usize;
    {
        let push = |out: &mut Vec<u8>, col: &mut usize, b: u8| {
            out.push(b);
            *col += 1;
            if wrap > 0 && *col == wrap {
                out.push(b'\n');
                *col = 0;
            }
        };
        let bits_per = codec.bits;
        let idx_mask = (1u64 << bits_per) - 1;
        let mut acc: u64 = 0;
        let mut nbits: u32 = 0;
        for &b in data {
            acc = (acc << 8) | b as u64;
            nbits += 8;
            while nbits >= bits_per {
                nbits -= bits_per;
                let idx = ((acc >> nbits) & idx_mask) as usize;
                push(&mut out, &mut col, codec.enc[idx]);
            }
        }
        // RFC 4648 final quantum: one extra char for the leftover bits, then
        // '=' padding to a full quantum.
        if nbits > 0 {
            let idx = ((acc << (bits_per - nbits)) & idx_mask) as usize;
            push(&mut out, &mut col, codec.enc[idx]);
        }
        // Pad count derives from the DATA length alone (the RFC 4648 final
        // quantum); out.len() would count the wrap newlines.
        let bits = codec.bits as u64;
        let data_chars = ((data.len() as u64 * 8) + bits - 1) / bits;
        let pads =
            (codec.quantum as u64 - data_chars % codec.quantum as u64) % codec.quantum as u64;
        let pads = pads as usize;
        for _ in 0..pads {
            push(&mut out, &mut col, b'=');
        }
    }
    if wrap > 0 && col > 0 {
        out.push(b'\n');
    }
    out
}

/// Decode outcome: Ok(bytes) or Err(bytes_decoded_before_the_error).
/// The caller prints the partial bytes (GNU prints the decoded prefix
/// before its "invalid input" diagnostic) and exits 1 on Err.
pub fn decode(codec: &Codec, data: &[u8], ignore_garbage: bool) -> Result<Vec<u8>, Vec<u8>> {
    let mut out = Vec::new();
    let mut acc: u64 = 0;
    let mut nbits: u32 = 0;
    let mut ndata: usize = 0; // data chars in the current quantum
    let quantum = codec.quantum;

    // Flush the whole bytes held in the accumulator; keep the remainder bits.
    let flush = |out: &mut Vec<u8>, acc: &mut u64, nbits: &mut u32| {
        if *nbits >= 8 {
            let whole = *nbits / 8;
            for k in 0..whole {
                let byte = ((*acc >> (*nbits - 8 * (k + 1))) & 0xff) as u8;
                out.push(byte);
            }
            *acc &= (1u64 << (*nbits % 8)).wrapping_sub(1);
            *nbits %= 8;
        }
    };

    macro_rules! fail {
        () => {{
            flush(&mut out, &mut acc, &mut nbits);
            return Err(out);
        }};
    }

    let mut i = 0usize;
    while i < data.len() {
        let c = data[i];
        i += 1;
        if c == b'\n' {
            continue;
        }
        if c == b'=' {
            match (codec.pads_for)(ndata) {
                Some(required) => {
                    // base64 flushes the partial quantum at pad acceptance
                    // ("Zg= " keeps "f" rc 1; "Zg==" keeps "f" rc 0);
                    // base32 keeps nothing until the run completes.
                    if codec.flush_on_pad_accept {
                        flush(&mut out, &mut acc, &mut nbits);
                    }
                    // The rest of the quantum must be '=' and nothing else:
                    // a newline inside the run breaks it ('Zg=\n==' rc 1).
                    let mut got = 1usize;
                    while got < required {
                        if i < data.len() && data[i] == b'=' {
                            i += 1;
                            got += 1;
                        } else {
                            break;
                        }
                    }
                    if got == required {
                        flush(&mut out, &mut acc, &mut nbits);
                        ndata = 0;
                        nbits = 0;
                        acc = 0;
                    } else {
                        // short/broken pad run: base64 already flushed its
                        // partial quantum at pad acceptance and base32 keeps
                        // nothing — no further flush on this error
                        // ("Zg=" -> "f" rc 1, "MY=" -> empty rc 1)
                        return Err(out);
                    }
                }
                None => fail!(), // wrong pad placement: immediate error
            }
            continue;
        }
        let v = if (c as usize) < 128 {
            codec.dec[c as usize]
        } else {
            0xff
        };
        if v == 0xff {
            if ignore_garbage {
                continue;
            }
            if codec.quantum == 8 {
                // GNU base32 keeps only complete 5-char groups (25 bits ->
                // 3 bytes) on an invalid character: 'ABCDEF1@@@@' keeps
                // 004432 (3 bytes), 'ABCD*' keeps nothing.
                let groups = ndata / 5;
                let keep_bits = groups * 25;
                let mut k = 0usize;
                while k + 8 <= keep_bits {
                    let byte = ((acc >> (nbits - 8 - k as u32)) & 0xff) as u8;
                    out.push(byte);
                    k += 8;
                }
                return Err(out);
            }
            fail!();
        }
        acc = (acc << codec.bits) | v as u64;
        nbits += codec.bits;
        ndata += 1;
        if ndata == quantum {
            flush(&mut out, &mut acc, &mut nbits);
            ndata = 0;
        }
    }
    if ndata != 0 {
        // incomplete quantum at EOF: base64 flushes floor(bits/8) partial
        // bytes, base32 emits none — both error with rc 1.
        if quantum == 4 {
            flush(&mut out, &mut acc, &mut nbits);
        }
        return Err(out);
    }
    Ok(out)
}

/// Parse GNU's wrap size (xstrtoimax at basenc.c): optional leading blanks,
/// optional '+', decimal digits; empty/junk/negative -> Err, overflow ->
/// Ok(0) (wrapping disabled, no trailing newline).
pub fn parse_wrap(raw: &str) -> Result<usize, ()> {
    let s = raw.trim_start();
    let s = s.strip_prefix('+').unwrap_or(s);
    if s.is_empty() || !s.bytes().all(|b| b.is_ascii_digit()) {
        return Err(());
    }
    match s.parse::<i64>() {
        Ok(v) if v >= 0 => Ok(v as usize),
        _ => Ok(0), // overflow: GNU sets wrap_column to 0 (no wrapping)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn dec64(s: &str) -> Result<String, String> {
        match decode(&base64_codec(), s.as_bytes(), false) {
            Ok(b) => Ok(String::from_utf8_lossy(&b).into_owned()),
            Err(b) => Err(String::from_utf8_lossy(&b).into_owned()),
        }
    }

    fn dec64i(s: &str) -> Result<String, String> {
        match decode(&base64_codec(), s.as_bytes(), true) {
            Ok(b) => Ok(String::from_utf8_lossy(&b).into_owned()),
            Err(b) => Err(String::from_utf8_lossy(&b).into_owned()),
        }
    }

    // RFC 4648 test vectors (encode)
    #[test]
    fn rfc4648_base64_encode() {
        let c = base64_codec();
        let cases: Vec<(&str, &str)> = vec![
            ("", ""),
            ("f", "Zg=="),
            ("fo", "Zm8="),
            ("foo", "Zm9v"),
            ("foob", "Zm9vYg=="),
            ("fooba", "Zm9vYmE="),
            ("foobar", "Zm9vYmFy"),
        ];
        for (input, want) in cases {
            assert_eq!(
                String::from_utf8_lossy(&encode(&c, input.as_bytes(), 0)),
                want,
                "encode({input:?})"
            );
        }
    }

    #[test]
    fn rfc4648_base32_encode() {
        let c = base32_codec();
        let cases: Vec<(&str, &str)> = vec![
            ("", ""),
            ("f", "MY======"),
            ("fo", "MZXQ===="),
            ("foo", "MZXW6==="),
            ("foob", "MZXW6YQ="),
            ("fooba", "MZXW6YTB"),
            ("foobar", "MZXW6YTBOI======"),
        ];
        for (input, want) in cases {
            assert_eq!(
                String::from_utf8_lossy(&encode(&c, input.as_bytes(), 0)),
                want,
                "encode({input:?})"
            );
        }
    }

    // Decode vectors: every case below is GNU coreutils 9.4 probed output
    // (target/gnu-probe/bd.txt, bd2.txt, bd3.txt) — Ok means rc 0, Err means
    // rc 1 with the partial bytes GNU printed before "invalid input".
    #[test]
    fn base64_decode_gnu_probe_vectors() {
        assert_eq!(dec64("Zm9vYmFy"), Ok("foobar".into()));
        assert_eq!(dec64("Zg=="), Ok("f".into()));
        assert_eq!(dec64("Zm8="), Ok("fo".into()));
        assert_eq!(dec64("Zm9vYg=="), Ok("foob".into()));
        assert_eq!(dec64("Zm9vYmF"), Err("fooba".into())); // rc1, partial kept
        assert_eq!(dec64("Zm9vY"), Err("foo".into()));
        assert_eq!(dec64("Zm9"), Err("fo".into()));
        assert_eq!(dec64("Z"), Err("".into()));
        assert_eq!(dec64("Zm=v"), Err("f".into()));
        assert_eq!(dec64("ZgZm8="), Err("f\u{6}f".into()));
        assert_eq!(dec64("Z==="), Err("".into()));
        assert_eq!(dec64("===="), Err("".into()));
        assert_eq!(dec64("Z=Zg"), Err("".into()));
        assert_eq!(dec64("Zg==="), Err("f".into()));
        assert_eq!(dec64("Zg="), Err("f".into()));
        assert_eq!(dec64("Zm9v="), Err("foo".into()));
        assert_eq!(dec64("Zg==\n\n"), Ok("f".into()));
        assert_eq!(dec64("Zm9v\nYmFy"), Ok("foobar".into()));
        assert_eq!(dec64(" Zg=="), Err("".into()));
        assert_eq!(dec64("Zg== "), Err("f".into()));
        // binary-output cases asserted on raw bytes (not UTF-8-lossy strings)
        assert_eq!(
            decode(&base64_codec(), b"ZZZZ", false),
            Ok(vec![0x65, 0x96, 0x59])
        );
        assert_eq!(decode(&base64_codec(), b"/w==", false), Ok(vec![0xff]));
        // -i cases (probed): '=' is never skipped, garbage is.
        assert_eq!(dec64i("Zg=="), Ok("f".into()));
        assert_eq!(dec64i("Z***g=="), Ok("f".into()));
        assert_eq!(dec64i("Z g=="), Ok("f".into()));
        assert_eq!(dec64i("Z=Zg"), Err("".into()));
        assert_eq!(dec64i("Zg= "), Err("f".into())); // space breaks the pad run
        assert_eq!(dec64i("Zg== "), Ok("f".into())); // after the run: skipped
        assert_eq!(dec64i("Zg=\n=="), Err("f".into())); // newline breaks the run
        assert_eq!(dec64i("Zg=2="), Err("f".into()));
    }

    #[test]
    fn base32_decode_gnu_probe_vectors() {
        let d32 = |s: &str| match decode(&base32_codec(), s.as_bytes(), false) {
            Ok(b) => Ok(String::from_utf8_lossy(&b).into_owned()),
            Err(b) => Err(String::from_utf8_lossy(&b).into_owned()),
        };
        let d32i = |s: &str| match decode(&base32_codec(), s.as_bytes(), true) {
            Ok(b) => Ok(String::from_utf8_lossy(&b).into_owned()),
            Err(b) => Err(String::from_utf8_lossy(&b).into_owned()),
        };
        assert_eq!(d32("MZXW6YTBOI======"), Ok("foobar".into()));
        assert_eq!(d32("MY======"), Ok("f".into()));
        assert_eq!(d32("MZXW6YQ="), Ok("foob".into()));
        assert_eq!(d32("MZXW6==="), Ok("foo".into()));
        assert_eq!(d32("MZXW6YTB"), Ok("fooba".into()));
        assert_eq!(d32("MZ"), Err("".into())); // no EOF partial flush for base32
        assert_eq!(d32("MZXW6YQ"), Err("".into()));
        assert_eq!(d32("MZ======"), Ok("f".into()));
        assert_eq!(d32("MF======"), Ok("a".into()));
        assert_eq!(d32("MZXW6YTB==="), Err("fooba".into()));
        assert_eq!(d32("MY="), Err("".into()));
        assert_eq!(d32("MZXW="), Err("".into()));
        assert_eq!(d32("AA="), Err("".into()));
        // -i (probed): '@' skipped, 7 data chars then EOF error, no flush.
        assert_eq!(d32i("AB@CDEF1@@@@"), Err("".into()));
        assert_eq!(d32i("AB@CDEF@@@@"), Err("".into()));
    }

    #[test]
    fn binary_decode_roundtrip() {
        let c = base64_codec();
        let data: Vec<u8> = (0..=255u8).collect();
        let enc = encode(&c, &data, 0);
        assert_eq!(decode(&c, &enc, false), Ok(data.clone()));
        let c32 = base32_codec();
        let enc32 = encode(&c32, &data, 0);
        assert_eq!(decode(&c32, &enc32, false), Ok(data));
    }

    #[test]
    fn wrap_rules() {
        let c = base64_codec();
        let enc = encode(&c, b"foobar", 76);
        assert_eq!(enc, b"Zm9vYmFy\n");
        let enc = encode(&c, b"foobar", 2);
        assert_eq!(enc, b"Zm\n9v\nYm\nFy\n");
        let enc = encode(&c, b"foobar", 0);
        assert_eq!(enc, b"Zm9vYmFy"); // no trailing newline
        let enc = encode(&c, b"foobar", 1);
        assert_eq!(enc, b"Z\nm\n9\nv\nY\nm\nF\ny\n"); // one char per line
        assert_eq!(parse_wrap("76"), Ok(76));
        assert_eq!(parse_wrap("bad"), Err(()));
        assert_eq!(parse_wrap("-1"), Err(()));
        assert_eq!(parse_wrap(""), Err(()));
        assert_eq!(parse_wrap("99999999999999999999"), Ok(0)); // overflow -> 0
        assert_eq!(parse_wrap("+3"), Ok(3));
    }
}
