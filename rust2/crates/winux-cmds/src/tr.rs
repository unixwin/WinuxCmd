//! `tr` — GNU coreutils tr.c (9.4) semantics, probed byte-for-byte
//! against WSL coreutils 9.4 (rust2/target/probe/p-tr.sh + p2/p-fin).
//!
//! Byte-oriented translation: an operand char is one BYTE (probed:
//! `tr 'é' 'X'` maps BOTH bytes of é to X), even though the oracle runs
//! under a UTF-8 locale. SET grammar (tr.c parse order): `\NNN` octal
//! (1-3 digits), `\a\b\f\n\r\t\v\\` escapes (unknown `\z` degrades to
//! `z` silently, probed), `c1-c2` ranges (bare trailing `-` is literal,
//! probed), `[:class:]` with the C-locale byte tables expanded in BYTE
//! order (probed: `[:alpha:]` = A-Za-z, so `tr '[:alpha:][:digit:]' aXY`
//! maps b and c to Y), `[=c=]` equivalence, `[c*n]` repeats (count
//! decimal, octal when leading 0, empty or 0 = fill; a bad count dies
//! with `invalid repeat count 'b' in [c*n] construct`), and `[a-z]`
//! bracketed ranges. A `[` that opens none of these is a literal.
//!
//! Errors (rc 1): `missing operand`/`missing operand after 'a'` (+ the
//! translating / delete-squeeze second line, + try tail), the two-line
//! `when translating, the only character classes that may appear in
//! string2 are 'upper' and 'lower'`, `misaligned [:upper:] and/or
//! [:lower:] construct`, `range endpoints out of order`.
//!
//! Modes: translate (SET2 shorter repeats its last char; `-t` truncates
//! SET1 to SET2's length first); `-c` translate maps EVERY complement
//! byte to SET2's LAST char (probed 3x: `-c a xyz` sends b..e to z);
//! `-d` deletes (only one operand without `-s`); `-ds` deletes then
//! squeezes SET2 (SET2 required); `-s` squeezes the last-specified
//! array after translate/delete. `[:upper:]`/`[:lower:]` in SET2 only
//! as an aligned pair with the opposite class at the same element index
//! in SET1 (probed: `tr a-z '[:upper:]'` misaligned error,
//! `tr '[:lower:]' '[:upper:]'` works).

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use crate::gnuhelp;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('c'),
        long: "complement",
        takes_value: false,
    },
    OptSpec {
        short: Some('C'),
        long: "",
        takes_value: false,
    },
    OptSpec {
        short: Some('d'),
        long: "delete",
        takes_value: false,
    },
    OptSpec {
        short: Some('s'),
        long: "squeeze-repeats",
        takes_value: false,
    },
    OptSpec {
        short: Some('t'),
        long: "truncate-set1",
        takes_value: false,
    },
];

/// C-locale byte tables for the tr classes, expanded in byte order
/// ([:alpha:] = A-Za-z, probed).
fn class_bytes(name: &str) -> Option<Vec<u8>> {
    let all = |f: &dyn Fn(u8) -> bool| -> Vec<u8> { (0u8..=255).filter(|b| f(*b)).collect() };
    match name {
        "alnum" => Some(all(&|b| b.is_ascii_alphanumeric())),
        "alpha" => Some(all(&|b| b.is_ascii_alphabetic())),
        "blank" => Some(vec![b'\t', b' ']),
        "cntrl" => Some(all(&|b| b.is_ascii_control())),
        "digit" => Some(all(&|b| b.is_ascii_digit())),
        "graph" => Some(all(&|b| (0x21..=0x7e).contains(&b))),
        "lower" => Some(all(&|b| b.is_ascii_lowercase())),
        "print" => Some(all(&|b| (0x20..=0x7e).contains(&b))),
        "punct" => Some(all(&|b| b.is_ascii_punctuation())),
        "space" => Some(vec![b'\t', b'\n', 0x0b, 0x0c, b'\r', b' ']),
        "upper" => Some(all(&|b| b.is_ascii_uppercase())),
        "xdigit" => Some(all(&|b| b.is_ascii_hexdigit())),
        _ => None,
    }
}

enum SetErr {
    /// message printed verbatim, no try tail
    Plain(String),
}

/// A parsed SET: expanded bytes plus fill-marker bookkeeping.
struct SetSpec {
    bytes: Vec<u8>,
    /// Some(byte-index) of the `[c*]` fill request (and its char)
    fill_at: Option<(usize, u8)>,
    /// [:upper:]/[:lower:] anchors recorded as (element index, name)
    class_anchors: Vec<(usize, String)>,
}

fn escape_byte(input: &[u8], i: &mut usize) -> u8 {
    // input[*i] is the char after the backslash
    let c = input[*i];
    let out = match c {
        b'a' => 0x07,
        b'b' => 0x08,
        b'f' => 0x0c,
        b'n' => 0x0a,
        b'r' => 0x0d,
        b't' => 0x09,
        b'v' => 0x0b,
        b'\\' => 0x5c,
        b'0'..=b'7' => {
            let mut val: u32 = 0;
            let mut n = 0;
            while n < 3 && *i < input.len() && (b'0'..=b'7').contains(&input[*i]) {
                val = val * 8 + (input[*i] - b'0') as u32;
                *i += 1;
                n += 1;
            }
            return val as u8;
        }
        _ => c, // unknown escape degrades to the char itself
    };
    *i += 1;
    out
}

fn find_sub(hay: &[u8], from: usize, needle: &[u8]) -> Option<usize> {
    if needle.len() > hay.len() || from > hay.len() - needle.len() {
        return None;
    }
    (from..=hay.len() - needle.len()).find(|&i| &hay[i..i + needle.len()] == needle)
}

/// Index of the matching ']' for the '[' at `open` (no nesting).
fn find_close_bracket(hay: &[u8], open: usize) -> Option<usize> {
    (open + 1..hay.len()).find(|&i| hay[i] == b']')
}

fn valid_count(s: &[u8]) -> bool {
    !s.is_empty() && s.iter().all(|b| b.is_ascii_digit())
}

/// `[c*NN]` count: octal when it starts with '0', else decimal; None when
/// empty (the `[c*]` fill form). The leading-0 octal form with invalid
/// octal digits ("08") degrades to decimal, like GNU's strtoul fallback.
fn parse_repeat_count(s: &[u8]) -> Option<u64> {
    let text = String::from_utf8_lossy(s).into_owned();
    if text.starts_with('0') && text.len() > 1 {
        if let Ok(v) = u64::from_str_radix(&text, 8) {
            return Some(v);
        }
    }
    text.parse::<u64>().ok()
}

/// Parse one SET operand into bytes, handling classes, equivalence,
/// repeats and ranges. `second && translating` enables the SET2 class
/// restrictions.
fn parse_set(input: &[u8], second: bool, translating: bool) -> Result<SetSpec, SetErr> {
    let mut bytes: Vec<u8> = Vec::new();
    let mut fill_at: Option<(usize, u8)> = None;
    let mut class_anchors: Vec<(usize, String)> = Vec::new();
    let mut i = 0usize;
    while i < input.len() {
        let c = input[i];
        if c == b'\\' {
            if i + 1 >= input.len() {
                bytes.push(0x5c);
                i += 1;
            } else {
                i += 1;
                bytes.push(escape_byte(input, &mut i));
            }
            continue;
        }
        if c == b'[' {
            // [:class:] / [=c=]
            if i + 1 < input.len() && (input[i + 1] == b':' || input[i + 1] == b'=') {
                let marker = input[i + 1];
                let close = if marker == b':' { b':' } else { b'=' };
                if let Some(end) = find_sub(input, i + 2, &[close, b']']) {
                    let name_bytes = &input[i + 2..end];
                    if marker == b':' {
                        let name = String::from_utf8_lossy(name_bytes).into_owned();
                        let cb = class_bytes(&name).ok_or_else(|| {
                            SetErr::Plain(format!("invalid character class \u{2018}{name}\u{2019}"))
                        })?;
                        if second && translating {
                            if name != "upper" && name != "lower" {
                                return Err(SetErr::Plain(
                                    "when translating, the only character classes that may \
                                     appear in\nstring2 are 'upper' and 'lower'"
                                        .to_string(),
                                ));
                            }
                            class_anchors.push((bytes.len(), name));
                        } else {
                            class_anchors.push((bytes.len(), name));
                        }
                        bytes.extend_from_slice(&cb);
                        i = end + 2;
                        continue;
                    } else if name_bytes.len() == 1 {
                        bytes.push(name_bytes[0]);
                        i = end + 2;
                        continue;
                    }
                }
            }
            // [c*n] / [c1-c2]
            if let Some(end) = find_close_bracket(input, i) {
                let content = &input[i + 1..end];
                if let Some(star) = content.iter().rposition(|&b| b == b'*') {
                    let count_str = &content[star + 1..];
                    let char_part = &content[..star];
                    // the repeated char: first char of content (escape-aware)
                    let (ch, consumed) =
                        if !char_part.is_empty() && char_part[0] == b'\\' && char_part.len() > 1 {
                            let mut ci = 1usize;
                            (escape_byte(char_part, &mut ci), ci)
                        } else if !char_part.is_empty() {
                            (char_part[0], 1usize)
                        } else {
                            (0u8, 0usize)
                        };
                    if consumed == char_part.len() {
                        if !count_str.is_empty() && !valid_count(count_str) {
                            return Err(SetErr::Plain(format!(
                                "invalid repeat count \u{2018}{}\u{2019} in [c*n] construct",
                                String::from_utf8_lossy(count_str)
                            )));
                        }
                        match parse_repeat_count(count_str) {
                            Some(0) | None => {
                                if fill_at.is_none() {
                                    fill_at = Some((bytes.len(), ch));
                                }
                                bytes.push(ch);
                            }
                            Some(n) => {
                                for _ in 0..n {
                                    bytes.push(ch);
                                }
                            }
                        }
                        i = end + 1;
                        continue;
                    }
                }
                // bracketed range [c1-c2]
                if content.len() >= 3 && content[1] == b'-' {
                    let (lo, hi) = (content[0], content[2]);
                    if hi < lo {
                        return Err(SetErr::Plain("range endpoints out of order".to_string()));
                    }
                    let mut b = lo;
                    loop {
                        bytes.push(b);
                        if b == hi {
                            break;
                        }
                        b += 1;
                    }
                    i = end + 1;
                    continue;
                }
            }
            // no construct matched: literal '['
            bytes.push(b'[');
            i += 1;
            continue;
        }
        // bare range c1-c2
        if i + 2 < input.len() && input[i + 1] == b'-' && input[i + 2] != b']' {
            let lo = c;
            let hi;
            if input[i + 2] == b'\\' && i + 3 < input.len() {
                let mut j = i + 3;
                hi = escape_byte(input, &mut j);
                i = j;
            } else {
                hi = input[i + 2];
                i += 3;
            }
            if hi < lo {
                return Err(SetErr::Plain("range endpoints out of order".to_string()));
            }
            let mut b = lo;
            loop {
                bytes.push(b);
                if b == hi {
                    break;
                }
                b += 1;
            }
            continue;
        }
        bytes.push(c);
        i += 1;
    }
    Ok(SetSpec {
        bytes,
        fill_at,
        class_anchors,
    })
}

/// Validate the [:upper:]/[:lower:] pairing: every class anchor in SET2
/// needs the OPPOSITE class anchored at the same element index in SET1.
fn validate_class_alignment(set1: &SetSpec, set2: &SetSpec) -> Result<(), SetErr> {
    for (idx, name) in &set2.class_anchors {
        let want = if name == "upper" { "lower" } else { "upper" };
        let ok = set1
            .class_anchors
            .iter()
            .any(|(i1, n1)| i1 == idx && n1 == want);
        if !ok {
            return Err(SetErr::Plain(
                "misaligned [:upper:] and/or [:lower:] construct".to_string(),
            ));
        }
    }
    Ok(())
}

/// Apply `[c*]` fill + last-char repeat to shape SET2 to SET1's length.
fn shape_set2(set2: SetSpec, len1: usize) -> Vec<u8> {
    if let Some((marker_pos, fill_char)) = set2.fill_at {
        // [c*]: the marker char repeats to len1 between the fixed parts
        let before = set2.bytes[..marker_pos].to_vec();
        let after = set2.bytes[marker_pos + 1..].to_vec();
        let fill_len = len1.saturating_sub(before.len() + after.len());
        let mut result = before;
        result.extend(std::iter::repeat(fill_char).take(fill_len));
        result.extend(after);
        return result;
    }
    let mut bytes = set2.bytes;
    if bytes.is_empty() {
        return bytes;
    }
    let last = *bytes.last().unwrap();
    while bytes.len() < len1 {
        bytes.push(last);
    }
    bytes
}

/// Byte membership bitmap.
fn to_bitmap(bytes: &[u8]) -> [bool; 256] {
    let mut m = [false; 256];
    for &b in bytes {
        m[b as usize] = true;
    }
    m
}

// `--help` body verbatim from `env tr --help` under WSL coreutils 9.4,
// minus the shared gnuhelp::footer tail.
const TR_HELP_BODY: &str = r#"Usage: tr [OPTION]... STRING1 [STRING2]
Translate, squeeze, and/or delete characters from standard input,
writing to standard output.  STRING1 and STRING2 specify arrays of
characters ARRAY1 and ARRAY2 that control the action.

  -c, -C, --complement    use the complement of ARRAY1
  -d, --delete            delete characters in ARRAY1, do not translate
  -s, --squeeze-repeats   replace each sequence of a repeated character
                            that is listed in the last specified ARRAY,
                            with a single occurrence of that character
  -t, --truncate-set1     first truncate ARRAY1 to length of ARRAY2
      --help        display this help and exit
      --version     output version information and exit

ARRAYs are specified as strings of characters.  Most represent themselves.
Interpreted sequences are:

  \NNN            character with octal value NNN (1 to 3 octal digits)
  \\              backslash
  \a              audible BEL
  \b              backspace
  \f              form feed
  \n              new line
  \r              return
  \t              horizontal tab
  \v              vertical tab
  CHAR1-CHAR2     all characters from CHAR1 to CHAR2 in ascending order
  [CHAR*]         in ARRAY2, copies of CHAR until length of ARRAY1
  [CHAR*REPEAT]   REPEAT copies of CHAR, REPEAT octal if starting with 0
  [:alnum:]       all letters and digits
  [:alpha:]       all letters
  [:blank:]       all horizontal whitespace
  [:cntrl:]       all control characters
  [:digit:]       all digits
  [:graph:]       all printable characters, not including space
  [:lower:]       all lower case letters
  [:print:]       all printable characters, including space
  [:punct:]       all punctuation characters
  [:space:]       all horizontal or vertical whitespace
  [:upper:]       all upper case letters
  [:xdigit:]      all hexadecimal digits
  [=CHAR=]        all characters which are equivalent to CHAR

Translation occurs if -d is not given and both STRING1 and STRING2 appear.
-t is only significant when translating.  ARRAY2 is extended to length of
ARRAY1 by repeating its last character as necessary.  Excess characters
of ARRAY2 are ignored.  Character classes expand in unspecified order;
while translating, [:lower:] and [:upper:] may be used in pairs to
specify case conversion.  Squeezing occurs after translation or deletion.

"#;

pub fn tr_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("tr", args, OPTS, io, ctx, 1) {
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
                    TR_HELP_BODY,
                    gnuhelp::footer("tr", "tr invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("tr", "Jim Meyering")
                )?;
                0
            }
        });
    }

    let complement = p.has("complement") || p.has("C");
    let delete = p.has("delete");
    let squeeze = p.has("squeeze-repeats");
    let truncate = p.has("truncate-set1");

    let operands: Vec<String> = p
        .positionals
        .iter()
        .map(|a| a.to_string_lossy().into_owned())
        .collect();

    // operand count diagnostics (probed shapes)
    if operands.is_empty() {
        let _ = writeln!(io.stderr, "tr: missing operand");
        let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "tr"));
        return Ok(1);
    }
    if operands.len() == 1 && !delete && !squeeze {
        let _ = writeln!(
            io.stderr,
            "tr: missing operand after \u{2018}{}\u{2019}",
            operands[0]
        );
        let _ = writeln!(io.stderr, "Two strings must be given when translating.");
        let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "tr"));
        return Ok(1);
    }
    if operands.len() == 1 && delete && squeeze {
        let _ = writeln!(
            io.stderr,
            "tr: missing operand after \u{2018}{}\u{2019}",
            operands[0]
        );
        let _ = writeln!(
            io.stderr,
            "Two strings must be given when both deleting and squeezing repeats."
        );
        let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "tr"));
        return Ok(1);
    }
    if delete && !squeeze && operands.len() >= 2 {
        let _ = writeln!(
            io.stderr,
            "tr: extra operand \u{2018}{}\u{2019}",
            operands[1]
        );
        let _ = writeln!(
            io.stderr,
            "Only one string may be given when deleting without squeezing repeats."
        );
        let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "tr"));
        return Ok(1);
    }

    let translating = !delete && operands.len() >= 2;
    let set1 = match parse_set(operands[0].as_bytes(), false, translating) {
        Ok(s) => s,
        Err(SetErr::Plain(msg)) => {
            let _ = writeln!(io.stderr, "tr: {msg}");
            return Ok(1);
        }
    };

    let mut table: [u8; 256] = (0u8..=255).collect::<Vec<u8>>().try_into().unwrap();
    let mut delete_map = [false; 256];
    let mut squeeze_map = [false; 256];

    if delete {
        let mut s1 = set1.bytes.clone();
        if complement {
            let m = to_bitmap(&s1);
            s1 = (0u8..=255).filter(|b| !m[*b as usize]).collect();
        }
        for &b in &s1 {
            delete_map[b as usize] = true;
        }
        if squeeze && operands.len() >= 2 {
            match parse_set(operands[1].as_bytes(), true, false) {
                Ok(s2) => {
                    for &b in &s2.bytes {
                        squeeze_map[b as usize] = true;
                    }
                }
                Err(SetErr::Plain(msg)) => {
                    let _ = writeln!(io.stderr, "tr: {msg}");
                    return Ok(1);
                }
            }
        }
    } else if operands.len() >= 2 {
        // translate
        let set2 = match parse_set(operands[1].as_bytes(), true, true) {
            Ok(s) => s,
            Err(SetErr::Plain(msg)) => {
                let _ = writeln!(io.stderr, "tr: {msg}");
                return Ok(1);
            }
        };
        if let Err(SetErr::Plain(msg)) = validate_class_alignment(&set1, &set2) {
            let _ = writeln!(io.stderr, "tr: {msg}");
            return Ok(1);
        }
        let mut set1_bytes = set1.bytes.clone();
        let set2_raw = SetSpec {
            bytes: set2.bytes.clone(),
            fill_at: set2.fill_at,
            class_anchors: set2.class_anchors.clone(),
        };
        if truncate {
            let len2 = set2_raw.bytes.len();
            set1_bytes.truncate(len2);
        }
        // pair [:lower:]/[:upper:] anchors positionally: build the case
        // mapping for the aligned classes, then fall back to positional
        let mut case_pair_ranges: Vec<(usize, usize, bool)> = Vec::new(); // (start1, len, to_upper)
        for (idx2, name2) in &set2_raw.class_anchors {
            if let Some((idx1, name1)) = set1.class_anchors.iter().find(|(i1, _)| i1 == idx2) {
                if (name1 == "lower" && name2 == "upper") || (name1 == "upper" && name2 == "lower")
                {
                    let len = class_bytes(name1).map(|c| c.len()).unwrap_or(0);
                    case_pair_ranges.push((*idx1, len, name2 == "upper"));
                }
            }
        }
        if complement {
            // probed rule: every complement byte maps to SET2's LAST char
            let in_set1 = to_bitmap(&set1_bytes);
            let last = set2_raw.bytes.last().copied().unwrap_or(0);
            for b in 0u8..=255 {
                table[b as usize] = if in_set1[b as usize] { b } else { last };
            }
        } else {
            let set2_bytes = shape_set2(set2_raw, set1_bytes.len());
            for (i, &from) in set1_bytes.iter().enumerate() {
                let to = set2_bytes
                    .get(i)
                    .copied()
                    .unwrap_or_else(|| set2_bytes.last().copied().unwrap_or(from));
                table[from as usize] = to;
            }
            // aligned class pairs override with real case mapping
            for (start, len, to_upper) in case_pair_ranges {
                let end = start + len;
                if end <= set1_bytes.len() {
                    for i in start..end {
                        let from = set1_bytes[i];
                        let to = if to_upper {
                            from.to_ascii_uppercase()
                        } else {
                            from.to_ascii_lowercase()
                        };
                        table[from as usize] = to;
                    }
                }
            }
        }
        if squeeze {
            // squeeze set = SET2 as given (shaped to SET1 length like GNU)
            let sq = match parse_set(operands[1].as_bytes(), true, false) {
                Ok(s) => s.bytes,
                Err(SetErr::Plain(msg)) => {
                    let _ = writeln!(io.stderr, "tr: {msg}");
                    return Ok(1);
                }
            };
            for &b in &sq {
                squeeze_map[b as usize] = true;
            }
        }
    } else if squeeze {
        // -s alone: squeeze SET1 (no translation)
        for &b in &set1.bytes {
            squeeze_map[b as usize] = true;
        }
    }

    let mut input = Vec::new();
    if io.stdin.read_to_end(&mut input).is_err() {
        let _ = writeln!(io.stderr, "tr: read error");
        return Ok(1);
    }
    let mut out: Vec<u8> = Vec::with_capacity(input.len());
    for &b in &input {
        let nb = if delete {
            if delete_map[b as usize] {
                continue;
            }
            b
        } else {
            table[b as usize]
        };
        if squeeze_map[nb as usize] {
            if let Some(&last) = out.last() {
                if last == nb {
                    continue;
                }
            }
        }
        out.push(nb);
    }
    io.stdout.write_all(&out)?;
    Ok(0)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str], stdin: &str) -> (u32, String, String) {
        let mut io = MemIo::new(stdin);
        let sc = ScratchCtx::new("tr");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = tr_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn gnu_help_version_surface() {
        let (r, out, err) = run(&["--help"], "");
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.starts_with("Usage: tr [OPTION]... STRING1 [STRING2]\n"));
        assert!(out.contains("  [=CHAR=]        all characters which are equivalent to CHAR"));
        assert!(out.ends_with("info '(coreutils) tr invocation'\n"));
        let (r, out, _) = run(&["--version"], "");
        assert_eq!(r, 0);
        assert!(out.starts_with("tr (GNU coreutils) 9.4\n"));
        assert!(out.ends_with("Written by Jim Meyering.\n"));
    }

    #[test]
    fn translate_basics() {
        assert_eq!(run(&["a-z", "A-Z"], "hello\n").1, "HELLO\n");
        assert_eq!(run(&["ab", "xy"], "abcdef\n").1, "xycdef\n");
        assert_eq!(run(&["abc", "xyzw"], "abcdef\n").1, "xyzdef\n");
        assert_eq!(run(&["a-e", "XY"], "abcdefg").1, "XYYYYfg");
        assert_eq!(run(&["ab", "ba"], "ab").1, "ba");
        assert_eq!(run(&["ac", "xy"], "abc").1, "xby");
        assert_eq!(run(&["ab", "x"], "ab").1, "xx");
        assert_eq!(run(&["a-d", "1234"], "ab\ncd").1, "12\n34");
    }

    #[test]
    fn set_grammar() {
        assert_eq!(run(&["[a-c]", "[B-D]"], "abc").1, "BCD");
        assert_eq!(run(&["[a-c]", "[x*]"], "abcde").1, "xxxde");
        assert_eq!(run(&["a", "[b*3]"], "abc").1, "bbc");
        assert_eq!(run(&["a", "[b*0]"], "abcd").1, "bbcd");
        assert_eq!(run(&["a", "[b*04]"], "abc").1, "bbc");
        assert_eq!(run(&["a[b]c", "xyz"], "abc").1, "xzz");
        assert_eq!(run(&["[ac*]", "X"], "abc").1, "XbX");
        assert_eq!(run(&["a-", "XY"], "a[b").1, "X[b");
        assert_eq!(run(&["\\t", "-"], "a\tb").1, "a-b");
        assert_eq!(run(&["\\\\", "-"], "a\\b").1, "a-b");
        assert_eq!(run(&["\\101", "Z"], "aAb").1, "aZb");
        assert_eq!(run(&["\\0", "N"], "a\0b").1, "aNb");
        assert_eq!(run(&["\\z", "X"], "z").1, "X");
        assert_eq!(run(&["[=a=]", "X"], "ab").1, "Xb");
        assert_eq!(run(&["a-c", "A-C"], "a-b").1, "A-B");
    }

    #[test]
    fn classes() {
        assert_eq!(run(&["[:lower:]", "[:upper:]"], "hello\n").1, "HELLO\n");
        assert_eq!(run(&["[:upper:]", "[:lower:]"], "HELLO").1, "hello");
        assert_eq!(run(&["[:lower:]", "[:upper:]"], "AB").1, "AB");
        assert_eq!(run(&["-d", "[:digit:]"], "a1b2").1, "ab");
        assert_eq!(run(&["[:blank:]", "_"], "a b\tc").1, "a_b_c");
        assert_eq!(run(&["[:alpha:][:digit:]", "aXY"], "abc").1, "YYY");
        assert_eq!(run(&["[:alpha:]", "[X*]"], "abc").1, "XXX");
        assert_eq!(run(&["[:cntrl:]", "@"], "abc").1, "abc");
        assert_eq!(run(&["-t", "[:lower:]", "[:upper:]"], "hello").1, "HELLO");
    }

    #[test]
    fn delete_squeeze_complement() {
        assert_eq!(run(&["-d", "l"], "hello world").1, "heo word");
        assert_eq!(run(&["-d", "a"], "aaabbb").1, "bbb");
        assert_eq!(run(&["-s", "a"], "aaabbb").1, "abbb");
        assert_eq!(run(&["-s", "ab"], "aabbcc").1, "abcc");
        assert_eq!(run(&["-dc", "a"], "abcabc").1, "aa");
        assert_eq!(run(&["-c", "a", "x"], "abc").1, "axx");
        assert_eq!(run(&["-cs", "a", "xy"], "abc").1, "ay");
        assert_eq!(run(&["-t", "abc", "xy"], "abc").1, "xyc");
        assert_eq!(run(&["-t", "a", "x"], "aaa").1, "xxx");
        assert_eq!(run(&["-s", "ab", "b"], "aaab").1, "b");
        assert_eq!(run(&["-s", "ab", "xy"], "aXbX").1, "xXyX");
        assert_eq!(run(&["-ds", "a", "b"], "ab\n").1, "b\n");
        assert_eq!(run(&["-ds", "ab", "b"], "ab\n").1, "\n");
        assert_eq!(run(&["-s", "b", "a"], "aab\n").1, "a\n");
        assert_eq!(run(&["-c", "a", "[x*]"], "abc").1, "axx");
        assert_eq!(run(&["-c", "a-b", "z"], "ab").1, "ab");
        assert_eq!(run(&["-c", "a", "xyz"], "abcde").1, "azzzz");
        assert_eq!(run(&["-c", "ab", "12"], "abc").1, "ab2");
        assert_eq!(run(&["-c", "a", "xyzw"], "ab").1, "aw");
        // probed: \r and \0 escapes map whole bytes
        assert_eq!(run(&["\\r", "R"], "a\rb").1, "aRb");
    }

    #[test]
    fn errors() {
        let (r, _, e) = run(&[], "a");
        assert_eq!(r, 1);
        assert!(e.contains("tr: missing operand"));
        assert!(e.contains("Try 'tr --help'"));
        let (r, _, e) = run(&["a"], "a");
        assert_eq!(r, 1);
        assert!(e.contains("missing operand after"));
        assert!(e.contains("Two strings must be given when translating."));
        let (r, _, e) = run(&["-ds", "a"], "a");
        assert_eq!(r, 1);
        assert!(e.contains("both deleting and squeezing"));
        let (r, _, e) = run(&["[:bogus:]", "X"], "abc");
        assert_eq!(r, 1);
        assert!(e.contains("invalid character class"));
        let (r, _, e) = run(&["[a*b]", "X"], "abc");
        assert_eq!(r, 1);
        assert!(e.contains("invalid repeat count"));
        let (r, _, e) = run(&["a-z", "[:digit:]"], "ABC");
        assert_eq!(r, 1);
        assert!(e.contains("only character classes"));
        let (r, _, e) = run(&["a-z", "[:upper:]"], "ab");
        assert_eq!(r, 1);
        assert!(e.contains("misaligned"));
        let (r, _, e) = run(&["-d", "a", "b"], "ab");
        assert_eq!(r, 1);
        assert!(e.contains("Only one string may be given when deleting"));
        assert_eq!(run(&["--bogus"], "").0, 1);
    }

    #[test]
    fn bytes_not_chars() {
        // é = c3 a9: BOTH bytes map to X (byte semantics, probed)
        let mut io = MemIo::new("aéb");
        let sc = ScratchCtx::new("tr-bytes");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = ["é", "X"].iter().map(Into::into).collect();
        tr_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(io.output, [0x61, 0x58, 0x58, 0x62]);
    }
}
