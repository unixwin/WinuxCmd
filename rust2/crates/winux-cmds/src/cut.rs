//! `cut` — GNU coreutils cut.c (9.4) semantics, probed byte-for-byte
//! against WSL coreutils 9.4 (rust2/target/probe/p-cut.sh + p-fin.sh).
//!
//! -b and -c are identical BYTE selection (probed: `cut -c 2` on "ünx"
//! emits the single byte 0xbc — GNU cut is not multibyte-aware). The LIST
//! parser mirrors cut.c's strtol loop: values are read with strtol
//! (leading whitespace skipped), `,` separates, a `-` opens a range, an
//! open range end means infinity. Measured shapes: `1 3` selects {1,3}
//! (strtol's whitespace skip), `3x` dies with `invalid byte/character
//! position 'x'` / `invalid field value 'x'` (curly quotes), `1-2-3`
//! with `invalid byte or character range`, `-` with `invalid range with
//! no endpoint: -`, `0` with `byte/character positions are numbered
//! from 1` / `fields are numbered from 1`, `2-1` with `invalid
//! decreasing range`. Selection is by input order, written exactly once
//! (overlaps collapse, probed). --complement complements within the
//! existing positions (bytes) or field count.
//!
//! Field mode splits on the delimiter byte (default TAB; `-d ''` = NUL;
//! a multi-BYTE delimiter dies with `the delimiter must be a single
//! character`). A record without the delimiter prints whole unless -s.
//! Records keep their terminator (NUL with -z); a last record without a
//! terminator prints without one.
//!
//! Windows subset: none — this command is fully honest.

use std::ffi::OsString;
use std::io::Read;

use crate::argopt::{self, OptSpec};
use crate::fsutil::{gnu_open_reason, quotef, resolve};
use crate::gnuhelp;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('b'),
        long: "bytes",
        takes_value: true,
    },
    OptSpec {
        short: Some('c'),
        long: "characters",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "complement",
        takes_value: false,
    },
    OptSpec {
        short: Some('d'),
        long: "delimiter",
        takes_value: true,
    },
    OptSpec {
        short: Some('f'),
        long: "fields",
        takes_value: true,
    },
    OptSpec {
        short: Some('n'),
        long: "",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "only-delimited",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "output-delimiter",
        takes_value: true,
    },
    OptSpec {
        short: Some('s'),
        long: "only-delimited",
        takes_value: false,
    },
    OptSpec {
        short: Some('z'),
        long: "zero-terminated",
        takes_value: false,
    },
];

/// One parsed 1-based closed range (or the open-ended sentinel).
#[derive(Debug, Clone, Copy, PartialEq)]
struct PosRange {
    low: u64,
    high: u64, // u64::MAX = open
}

/// Which list mode a LIST belongs to (error wording differs).
#[derive(Debug, PartialEq, Clone, Copy)]
enum ListMode {
    Bytes,
    Fields,
}

fn strtol_skip(text: &[u8], mut i: usize) -> usize {
    // strtol skips leading whitespace before digits
    while i < text.len() && (text[i] == b' ' || text[i] == b'\t') {
        i += 1;
    }
    i
}

/// Parse cut's LIST exactly like cut.c's strtol loop. Errors carry the
/// exact GNU diagnostic (already quoted where GNU quotes).
fn parse_list(list: &str, mode: ListMode) -> Result<Vec<PosRange>, String> {
    let field_mode = mode == ListMode::Fields;
    let text = list.as_bytes();
    let value_word = if field_mode {
        "field value"
    } else {
        "byte/character position"
    };
    let range_word = if field_mode {
        "invalid field range"
    } else {
        "invalid byte or character range"
    };
    let numbered_word = if field_mode {
        "fields are numbered from 1"
    } else {
        "byte/character positions are numbered from 1"
    };
    let mut ranges: Vec<PosRange> = Vec::new();
    let mut i = 0usize;
    loop {
        if i >= text.len() {
            break;
        }
        let leading_dash = text[i] == b'-';
        if leading_dash {
            i += 1;
        }
        let start = strtol_skip(text, i);
        let mut k = start;
        while k < text.len() && text[k].is_ascii_digit() {
            k += 1;
        }
        let (low, high, closed_range);
        if leading_dash {
            if k == start {
                if i >= text.len() {
                    // bare "-" with nothing after
                    return Err("invalid range with no endpoint: -".to_string());
                }
                let rest = &list[start.min(list.len())..];
                return Err(format!("invalid {value_word} \u{2018}{rest}\u{2019}"));
            }
            let m: u64 = list[start..k]
                .parse()
                .map_err(|_| numbered_word.to_string())?;
            if m == 0 {
                return Err(numbered_word.to_string());
            }
            i = k;
            low = 1;
            high = m;
            closed_range = true;
        } else if k > start {
            let n1: u64 = list[start..k]
                .parse()
                .map_err(|_| numbered_word.to_string())?;
            if n1 == 0 {
                return Err(numbered_word.to_string());
            }
            i = k;
            if i < text.len() && text[i] == b'-' {
                i += 1;
                let start2 = strtol_skip(text, i);
                let mut k2 = start2;
                while k2 < text.len() && text[k2].is_ascii_digit() {
                    k2 += 1;
                }
                if k2 > start2 {
                    let h: u64 = list[start2..k2]
                        .parse()
                        .map_err(|_| numbered_word.to_string())?;
                    if h == 0 {
                        return Err(numbered_word.to_string());
                    }
                    high = h;
                    i = k2;
                } else {
                    high = u64::MAX; // open range "N-"
                }
                closed_range = true;
            } else {
                high = n1;
                closed_range = false;
            }
            low = n1;
        } else {
            // junk where a number was expected: GNU names the rest
            let rest = &list[start.min(list.len())..];
            return Err(format!("invalid {value_word} \u{2018}{rest}\u{2019}"));
        }
        if high < low {
            return Err("invalid decreasing range".to_string());
        }
        ranges.push(PosRange { low, high });
        if closed_range {
            // after a range GNU only accepts ',' or end ("1-2-3" dies)
            if i < text.len() && text[i] != b',' {
                return Err(range_word.to_string());
            }
        } else {
            // after a single value strtol's whitespace skip applies:
            // "1 3" reads 3 as the next spec (probed)
            i = strtol_skip(text, i);
        }
        if i < text.len() && text[i] == b',' {
            i += 1;
            continue;
        }
        if i >= text.len() {
            break;
        }
        // not a comma and not the end: loop back; the next iteration
        // either parses another spec or dies on the junk
        continue;
    }
    if ranges.is_empty() {
        return Err(numbered_word.to_string());
    }
    Ok(ranges)
}

/// Membership over the merged range set (input order, exactly once).
fn position_selected(ranges: &[PosRange], pos: u64) -> bool {
    ranges.iter().any(|r| pos >= r.low && pos <= r.high)
}

/// Merge disjoint selected positions into a sorted, non-overlapping set.
fn merge_ranges(ranges: &[PosRange]) -> Vec<(u64, u64)> {
    let mut pts: Vec<(u64, u64)> = ranges
        .iter()
        .map(|r| (r.low, r.high.min(u64::MAX - 1)))
        .collect();
    pts.sort_unstable();
    let mut out: Vec<(u64, u64)> = Vec::new();
    for (l, h) in pts {
        match out.last_mut() {
            Some(last) if l <= last.1.saturating_add(1) => {
                last.1 = last.1.max(h);
            }
            _ => out.push((l, h)),
        }
    }
    out
}

// `--help` body verbatim from `env cut --help` under WSL coreutils 9.4,
// minus the shared gnuhelp::footer tail.
const CUT_HELP_BODY: &str = r#"Usage: cut OPTION... [FILE]...
Print selected parts of lines from each FILE to standard output.

With no FILE, or when FILE is -, read standard input.

Mandatory arguments to long options are mandatory for short options too.
  -b, --bytes=LIST        select only these bytes
  -c, --characters=LIST   select only these characters
  -d, --delimiter=DELIM   use DELIM instead of TAB for field delimiter
  -f, --fields=LIST       select only these fields;  also print any line
                            that contains no delimiter character, unless
                            the -s option is specified
  -n                      (ignored)
      --complement        complement the set of selected bytes, characters
                            or fields
  -s, --only-delimited    do not print lines not containing delimiters
      --output-delimiter=STRING  use STRING as the output delimiter
                            the default is to use the input delimiter
  -z, --zero-terminated   line delimiter is NUL, not newline
      --help        display this help and exit
      --version     output version information and exit

Use one, and only one of -b, -c or -f.  Each LIST is made up of one
range, or many ranges separated by commas.  Selected input is written
in the same order that it is read, and is written exactly once.
Each range is one of:

  N     N'th byte, character or field, counted from 1
  N-    from N'th byte, character or field, to end of line
  N-M   from N'th to M'th (included) byte, character or field
  -M    from first to M'th (included) byte, character or field

"#;

pub fn cut_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("cut", args, OPTS, io, ctx, 1) {
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
                    CUT_HELP_BODY,
                    gnuhelp::footer("cut", "cut invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version(
                        "cut",
                        "David M. Ihnat, David MacKenzie, and Jim Meyering"
                    )
                )?;
                0
            }
        });
    }

    let mut mode: Option<ListMode> = None;
    let mut list_text = String::new();
    if let Some(v) = p.value_of("fields") {
        mode = Some(ListMode::Fields);
        list_text = v.to_string();
    }
    let bytes_val = p.value_of("bytes").or_else(|| p.value_of("characters"));
    if let Some(v) = bytes_val {
        if mode.is_some() {
            let _ = writeln!(io.stderr, "cut: only one list may be specified");
            let _ = writeln!(io.stderr, "Try 'cut --help' for more information.");
            return Ok(1);
        }
        mode = Some(ListMode::Bytes);
        list_text = v.to_string();
    }
    let Some(mode) = mode else {
        let _ = writeln!(
            io.stderr,
            "cut: you must specify a list of bytes, characters, or fields"
        );
        let _ = writeln!(io.stderr, "Try 'cut --help' for more information.");
        return Ok(1);
    };
    let ranges = match parse_list(&list_text, mode) {
        Ok(r) => r,
        Err(msg) => {
            let _ = writeln!(io.stderr, "cut: {msg}");
            let _ = writeln!(io.stderr, "Try 'cut --help' for more information.");
            return Ok(1);
        }
    };

    let delim_specified = p.has("delimiter");
    if delim_specified && mode == ListMode::Bytes {
        let _ = writeln!(
            io.stderr,
            "cut: an input delimiter may be specified only when operating on fields"
        );
        let _ = writeln!(io.stderr, "Try 'cut --help' for more information.");
        return Ok(1);
    }
    let delim_str = p.value_of("delimiter").unwrap_or("\t").to_string();
    // probed: `-d ''` selects the NUL delimiter; only a longer string errs
    if delim_str.len() > 1 {
        let _ = writeln!(io.stderr, "cut: the delimiter must be a single character");
        let _ = writeln!(io.stderr, "Try 'cut --help' for more information.");
        return Ok(1);
    }
    let delim_str = if delim_str.is_empty() {
        "\0".to_string()
    } else {
        delim_str
    };
    let delim = delim_str.as_bytes()[0];
    let only_delimited = p.has("only-delimited") && mode == ListMode::Fields;
    if p.has("only-delimited") && mode == ListMode::Bytes {
        let _ = writeln!(
            io.stderr,
            "cut: suppressing non-delimited lines makes sense only when operating on fields"
        );
        let _ = writeln!(io.stderr, "Try 'cut --help' for more information.");
        return Ok(1);
    }
    let out_delim_given = p.has("output-delimiter");
    if out_delim_given && mode == ListMode::Bytes {
        let _ = writeln!(
            io.stderr,
            "cut: an output delimiter may be specified only when operating on fields"
        );
        let _ = writeln!(io.stderr, "Try 'cut --help' for more information.");
        return Ok(1);
    }
    let out_delim = p
        .value_of("output-delimiter")
        .unwrap_or(&delim_str)
        .to_string();
    let complement = p.has("complement");
    let record_delim: u8 = if p.has("zero-terminated") { 0 } else { b'\n' };

    // byte mode: precompute the merged selected set, complemented within
    // 1..=len later (complement needs the length, so keep both forms)
    let merged = merge_ranges(&ranges);

    let files: Vec<String> = if p.positionals.is_empty() {
        vec!["-".to_string()]
    } else {
        p.positionals
            .iter()
            .map(|a| a.to_string_lossy().into_owned())
            .collect()
    };

    let mut failed = false;
    for file in &files {
        let mut input = Vec::new();
        if file == "-" {
            if io.stdin.read_to_end(&mut input).is_err() {
                let _ = writeln!(io.stderr, "cut: read error");
                failed = true;
                continue;
            }
        } else {
            match std::fs::File::open(resolve(ctx, file)) {
                Ok(mut f) => {
                    if f.read_to_end(&mut input).is_err() {
                        let _ = writeln!(io.stderr, "cut: {file}: read error");
                        failed = true;
                        continue;
                    }
                }
                Err(e) => {
                    let reason = gnu_open_reason(ctx, file, &e, false);
                    let _ = writeln!(io.stderr, "cut: {}: {reason}", quotef(file));
                    failed = true;
                    continue;
                }
            }
        }
        // process records, keeping each terminator
        let mut rest: &[u8] = &input;
        while !rest.is_empty() {
            let end = rest
                .iter()
                .position(|&b| b == record_delim)
                .map_or(rest.len(), |i| i + 1);
            let (record, tail) = rest.split_at(end);
            rest = tail;
            // GNU cut terminates every output record unconditionally:
            // even the last record lacking an input newline gets one
            // (probed: `printf 'ab' | cut -c1` emits "a\n")
            let content: &[u8] = if !record.is_empty() && record[record.len() - 1] == record_delim {
                &record[..record.len() - 1]
            } else {
                record
            };
            let mut line_ok = true;
            if mode == ListMode::Fields && only_delimited && !content.contains(&delim) {
                line_ok = false;
            }
            if line_ok {
                let out_bytes: Vec<u8> = if mode == ListMode::Bytes {
                    // byte selection: input order, exactly once
                    let mut sel: Vec<u8> = Vec::new();
                    for block in &merged {
                        let lo = block.0 as usize;
                        let hi = block.1.min(content.len() as u64) as usize;
                        if lo <= content.len() && lo <= hi {
                            sel.extend_from_slice(&content[lo - 1..hi]);
                        }
                    }
                    if complement {
                        let mut comp: Vec<u8> = Vec::new();
                        for (i, &b) in content.iter().enumerate() {
                            let pos = (i + 1) as u64;
                            if !position_selected(&ranges, pos) {
                                comp.push(b);
                            }
                        }
                        comp
                    } else {
                        sel
                    }
                } else {
                    // field mode
                    let has_delim = content.contains(&delim);
                    if !has_delim {
                        content.to_vec()
                    } else {
                        let fields: Vec<&[u8]> = content.split(|&b| b == delim).collect();
                        let nfields = fields.len() as u64;
                        let mut picked: Vec<&[u8]> = Vec::new();
                        for (idx, f) in fields.iter().enumerate() {
                            let pos = (idx + 1) as u64;
                            let selected = position_selected(&ranges, pos) != complement;
                            if selected {
                                picked.push(f);
                            }
                        }
                        let mut buf: Vec<u8> = Vec::new();
                        for (k, f) in picked.iter().enumerate() {
                            if k > 0 {
                                buf.extend_from_slice(out_delim.as_bytes());
                            }
                            buf.extend_from_slice(f);
                        }
                        let _ = nfields;
                        buf
                    }
                };
                if io.stdout.write_all(&out_bytes).is_err()
                    || io.stdout.write_all(&[record_delim]).is_err()
                {
                    let _ = writeln!(io.stderr, "cut: write error");
                    failed = true;
                    break;
                }
            }
        }
    }
    Ok(if failed { 1 } else { 0 })
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str], stdin: &str) -> (u32, String, String) {
        let sc = ScratchCtx::new("cut");
        let mut io = MemIo::new(stdin);
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = cut_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn gnu_help_version_surface() {
        let (r, out, err) = run(&["--help"], "");
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.starts_with("Usage: cut OPTION... [FILE]...\n"));
        assert!(out.contains("  -n                      (ignored)"));
        assert!(out.ends_with("info '(coreutils) cut invocation'\n"));
        let (r, out, _) = run(&["--version"], "");
        assert_eq!(r, 0);
        assert!(out.starts_with("cut (GNU coreutils) 9.4\n"));
        assert!(out.ends_with("Written by David M. Ihnat, David MacKenzie, and Jim Meyering.\n"));
    }

    #[test]
    fn list_parse() {
        let ok = |l: &str, m: ListMode| parse_list(l, m).unwrap();
        assert_eq!(
            ok("2", ListMode::Fields),
            vec![PosRange { low: 2, high: 2 }]
        );
        assert_eq!(
            ok("1,3", ListMode::Fields),
            vec![PosRange { low: 1, high: 1 }, PosRange { low: 3, high: 3 }]
        );
        assert_eq!(
            ok("3-", ListMode::Fields),
            vec![PosRange {
                low: 3,
                high: u64::MAX
            }]
        );
        assert_eq!(
            ok("-3", ListMode::Fields),
            vec![PosRange { low: 1, high: 3 }]
        );
        // strtol whitespace skip: "1 3" selects both, probed
        assert_eq!(ok("1 3", ListMode::Fields).len(), 2);
        assert!(parse_list("3x", ListMode::Fields)
            .err()
            .unwrap()
            .contains("invalid field value"));
        assert!(parse_list("3x", ListMode::Bytes)
            .err()
            .unwrap()
            .contains("invalid byte/character position"));
        assert_eq!(
            parse_list("1-2-3", ListMode::Bytes).err().unwrap(),
            "invalid byte or character range"
        );
        assert_eq!(
            parse_list("-", ListMode::Bytes).err().unwrap(),
            "invalid range with no endpoint: -"
        );
        assert_eq!(
            parse_list("0", ListMode::Bytes).err().unwrap(),
            "byte/character positions are numbered from 1"
        );
        assert_eq!(
            parse_list("", ListMode::Fields).err().unwrap(),
            "fields are numbered from 1"
        );
        assert_eq!(
            parse_list("2-1", ListMode::Fields).err().unwrap(),
            "invalid decreasing range"
        );
    }

    #[test]
    fn field_mode() {
        assert_eq!(run(&["-d", ":", "-f", "2"], "1:2:3:4:5\n").1, "2\n");
        assert_eq!(run(&["-d", ":", "-f", "1,3"], "1:2:3:4:5\n").1, "1:3\n");
        assert_eq!(run(&["-d", ":", "-f", "3-"], "1:2:3:4:5\n").1, "3:4:5\n");
        assert_eq!(run(&["-d", ":", "-f", "-3"], "1:2:3:4:5\n").1, "1:2:3\n");
        assert_eq!(run(&["-d", ":", "-f", "2-4"], "1:2:3:4:5\n").1, "2:3:4\n");
        assert_eq!(run(&["-d", ":", "-f", "2,2,2"], "1:2:3\n").1, "2\n");
        assert_eq!(run(&["-f", "2"], "a\tb\n").1, "b\n");
        // default output delimiter is the input delimiter (TAB here)
        assert_eq!(run(&["-f", "1-2"], "a\tb\tc\n").1, "a\tb\n");
        assert_eq!(
            run(&["-d", ":", "-f", "1-2", "--output-delimiter=XY"], "a:b\n").1,
            "aXYb\n"
        );
        // no delimiter: whole line unless -s
        assert_eq!(run(&["-f", "2"], "abc\n").1, "abc\n");
        assert_eq!(run(&["-f", "2", "-s"], "abc\n").1, "");
        assert_eq!(
            run(&["-f", "2", "-d", ":", "-s"], "a:b\nx\ny:z\n").1,
            "b\nz\n"
        );
        // complement
        assert_eq!(
            run(&["-d", ":", "-f", "1,3", "--complement"], "a:b:c\n").1,
            "b\n"
        );
        // NUL delimiter via -d ''
        let (r, _, _) = run(&["-d", "", "-f", "2"], "a\0b\0c\n");
        assert_eq!(r, 0);
    }

    #[test]
    fn byte_mode() {
        assert_eq!(run(&["-c", "2"], "abcde\n").1, "b\n");
        assert_eq!(run(&["-c", "2-4"], "abcde\n").1, "bcd\n");
        assert_eq!(run(&["-c", "2-"], "abcde\n").1, "bcde\n");
        assert_eq!(run(&["-c", "-3"], "abcde\n").1, "abc\n");
        assert_eq!(run(&["-c", "99"], "abc\n").1, "\n");
        assert_eq!(run(&["-b", "2,5"], "abcdef\n").1, "be\n");
        // input order, exactly once
        assert_eq!(run(&["-c", "1-2,1"], "a:b\n").1, "a:\n");
        assert_eq!(run(&["-c", "1 3"], "abc\n").1, "ac\n");
        assert_eq!(run(&["-c", "2-3", "--complement"], "abcdef\n").1, "adef\n");
        assert_eq!(run(&["-n", "-c", "1"], "ab\n").1, "a\n");
        // -b/-c are byte-identical: cut -c 2 on 'ünx' emits byte 0xbc
        let sc = ScratchCtx::new("cut-utf");
        let mut io = MemIo::new("ünx\n");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = ["-c", "2"].iter().map(Into::into).collect();
        cut_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(io.output, [0xbc, b'\n']);
    }

    #[test]
    fn zero_terminated_and_edges() {
        let (r, _, _) = run(&["-c", "1", "-z"], "ab\0");
        assert_eq!(r, 0);
        // last record without a terminator still gets a terminator (probed)
        assert_eq!(run(&["-d", ":", "-f", "1"], "a:b").1, "a\n");
        // past-end selections are ignored
        assert_eq!(run(&["-c", "5-9"], "abc\n").1, "\n");
    }

    #[test]
    fn errors() {
        assert_eq!(run(&["-d", ":", "-f", "1", "-c", "1"], "a\n").0, 1);
        assert!(run(&["-d", ":", "-f", "1", "-c", "1"], "a\n")
            .2
            .contains("only one list may be specified"));
        assert_eq!(run(&["-d", ":"], "a\n").0, 1);
        assert!(run(&["-d", ":"], "a\n")
            .2
            .contains("you must specify a list"));
        assert_eq!(run(&["-d", "ab", "-f", "1"], "a\n").0, 1);
        assert!(run(&["-d", "ab", "-f", "1"], "a\n")
            .2
            .contains("must be a single character"));
        assert!(run(&["-d", ":", "-f", "x"], "a\n")
            .2
            .contains("invalid field value"));
        assert!(run(&["-d", ":", "-b", "1", "-d", ":"], "a\n")
            .2
            .contains("only when operating on fields"));
        assert_eq!(run(&["--bogus"], "").0, 1);
        // missing file: rc 1, quotef'd name
        let sc = ScratchCtx::new("cut-err");
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = ["-c", "1", "nosuch"].iter().map(Into::into).collect();
        let r = cut_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("cut: nosuch: No such file or directory"));
    }
}
