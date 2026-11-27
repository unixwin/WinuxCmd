//! `comm` — GNU coreutils comm.c (9.4) semantics, probed byte-for-byte
//! against WSL coreutils 9.4 (C.UTF-8, i.e. bytewise collation).
//!
//! Two sorted inputs are merged one record at a time; a record unique to
//! FILE1 goes to column 1, unique to FILE2 to column 2, common to column
//! 3. Columns are prefixed by one separator per earlier non-suppressed
//! column. A record missing its final delimiter gets one (readlinebuffer).
//! Records compare without their delimiter, memcmp then length.
//!
//! Order checking (check_order): `--check-order` dies at the first
//! disorder (before printing the offending record); the default warns
//! once per file, only after an unpairable record was seen, and exits 1
//! with "input is not in sorted order" after the output; `--nocheck-order`
//! disables it. The last of the two options wins. On EOF GNU re-checks
//! the final pair of that file (seen_unpairable may have flipped).
//!
//! `--output-delimiter=STR`: a second, different STR is fatal ("multiple
//! output delimiters specified", no usage line); the empty STR outputs
//! one NUL (col_sep_len = 1). `--total` appends the counts joined by the
//! separator plus "total". `-` is stdin; `comm - -` shares one stream
//! and the second fclose(stdin) fails EBADF after the merge, so totals
//! and the disorder summary are never reached (#1712).
//!
//! Operands are opened and primed in order: FILE1 is opened and its first
//! record read before FILE2 is opened, so FILE1's error wins.

use std::cell::RefCell;
use std::cmp::Ordering;
use std::ffi::OsString;
use std::io::{BufRead, BufReader, Read, Write};

use crate::argopt::{self, OptSpec};
use crate::fsutil::{gnu_open_reason, open_read_operand, quotef, reason};
use winux_core::{tr, usage_error, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec { short: Some('1'), long: "", takes_value: false },
    OptSpec { short: Some('2'), long: "", takes_value: false },
    OptSpec { short: Some('3'), long: "", takes_value: false },
    OptSpec { short: None, long: "check-order", takes_value: false },
    OptSpec { short: None, long: "nocheck-order", takes_value: false },
    OptSpec { short: None, long: "output-delimiter", takes_value: true },
    OptSpec { short: None, long: "total", takes_value: false },
    OptSpec { short: Some('z'), long: "zero-terminated", takes_value: false },
];

#[derive(Clone, Copy, PartialEq, Eq)]
enum OrderCheck {
    Default,
    Enabled,
    Disabled,
}

enum Src {
    Stdin,
    File(BufReader<Box<dyn Read>>),
}

/// The record body without its delimiter.
fn body(rec: &[u8]) -> &[u8] {
    &rec[..rec.len() - 1]
}

/// readlinebuffer_delim: None at EOF; a partial final record gets the
/// delimiter appended.
fn read_record(r: &mut dyn BufRead, delim: u8) -> std::io::Result<Option<Vec<u8>>> {
    let mut buf = Vec::new();
    if r.read_until(delim, &mut buf)? == 0 {
        return Ok(None);
    }
    if buf.last() != Some(&delim) {
        buf.push(delim);
    }
    Ok(Some(buf))
}

struct Merge<'s> {
    only1: bool,
    only2: bool,
    both: bool,
    sep: Vec<u8>,
    mode: OrderCheck,
    seen_unpairable: bool,
    issued: [bool; 2],
    out: &'s mut dyn Write,
}

impl Merge<'_> {
    fn writeline(&mut self, rec: &[u8], class: u8) -> std::io::Result<()> {
        match class {
            1 if !self.only1 => return Ok(()),
            2 if !self.only2 => return Ok(()),
            3 if !self.both => return Ok(()),
            2 if self.only1 => self.out.write_all(&self.sep)?,
            3 => {
                if self.only1 {
                    self.out.write_all(&self.sep)?;
                }
                if self.only2 {
                    self.out.write_all(&self.sep)?;
                }
            }
            _ => {}
        }
        self.out.write_all(rec)
    }

    /// check_order(); Err(()) = fatal (--check-order).
    fn check_order(
        &mut self,
        prev: &[u8],
        cur: &[u8],
        file: usize,
        ctx: &Ctx,
        err: &mut dyn Write,
    ) -> Result<(), ()> {
        if self.mode == OrderCheck::Disabled
            || (self.mode == OrderCheck::Default && !self.seen_unpairable)
            || self.issued[file]
        {
            return Ok(());
        }
        if body(prev).cmp(body(cur)) == Ordering::Greater {
            let _ = self.out.flush();
            let _ = writeln!(
                err,
                "{}",
                tr!(
                    ctx,
                    "command.comm.error.file_not_sorted",
                    "comm: file {number} is not in sorted order",
                    number = file + 1
                )
            );
            if self.mode == OrderCheck::Enabled {
                return Err(());
            }
            self.issued[file] = true;
        }
        Ok(())
    }
}

pub fn comm_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    // GNU validates --output-delimiter as getopt returns it: a second,
    // different value dies at that argv position.
    let col_sep: RefCell<Option<String>> = RefCell::new(None);
    let on_value = |key: &str, val: &str, io: &mut Io, ctx: &Ctx, _prog: &str| -> Option<u32> {
        if key != "output-delimiter" {
            return None;
        }
        let mut cur = col_sep.borrow_mut();
        if cur.as_deref().is_some_and(|c| c != val) {
            let _ = writeln!(
                io.stderr,
                "comm: {}",
                tr!(
                    ctx,
                    "command.comm.error.multiple_output_delimiters",
                    "multiple output delimiters specified"
                )
            );
            return Some(1);
        }
        *cur = Some(val.to_string());
        None
    };
    let p = match argopt::parse_ex("comm", args, OPTS, io, ctx, 1, None, Some(&on_value)) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if p.has("help") || p.has("version") {
        return Ok(0);
    }

    let names: Vec<String> = p
        .positionals
        .iter()
        .map(|a| a.to_string_lossy().into_owned())
        .collect();
    if names.len() < 2 {
        let msg = match names.last() {
            None => crate::messages::missing_operand(ctx),
            Some(last) => crate::messages::missing_after(ctx, last),
        };
        let _ = usage_error(ctx, io, "comm", &msg);
        return Ok(1);
    }
    if names.len() > 2 {
        let _ = usage_error(ctx, io, "comm", &crate::messages::extra_operand(ctx, &names[2]));
        return Ok(1);
    }

    let mut mode = OrderCheck::Default;
    for k in &p.order {
        match k.as_str() {
            "check-order" => mode = OrderCheck::Enabled,
            "nocheck-order" => mode = OrderCheck::Disabled,
            _ => {}
        }
    }
    let delim = if p.has("zero-terminated") { b'\0' } else { b'\n' };
    let sep: Vec<u8> = match col_sep.into_inner() {
        None => b"\t".to_vec(),
        Some(s) if s.is_empty() => vec![0],
        Some(s) => s.into_bytes(),
    };
    let total_opt = p.has("total");

    let mut stdin = BufReader::new(&mut *io.stdin);
    let err: &mut dyn Write = &mut *io.stderr;
    let mut srcs: Vec<Src> = Vec::with_capacity(2);
    let mut cur: [Option<Vec<u8>>; 2] = [None, None];

    let fail = |err: &mut dyn Write, name: &str, why: &str| {
        let _ = writeln!(err, "comm: {}: {}", quotef(name), why);
        Ok(1)
    };

    for (i, name) in names.iter().enumerate() {
        if name == "-" {
            srcs.push(Src::Stdin);
        } else {
            match open_read_operand(ctx, name) {
                Ok(r) => srcs.push(Src::File(BufReader::new(r))),
                Err(e) => return fail(err, name, &gnu_open_reason(ctx, name, &e, false)),
            }
        }
        let r = match &mut srcs[i] {
            Src::Stdin => read_record(&mut stdin, delim),
            Src::File(f) => read_record(f, delim),
        };
        match r {
            Ok(rec) => cur[i] = rec,
            Err(e) => return fail(err, name, &gnu_open_reason(ctx, name, &e, false)),
        }
    }

    let mut m = Merge {
        only1: !p.has("1"),
        only2: !p.has("2"),
        both: !p.has("3"),
        sep,
        mode,
        seen_unpairable: false,
        issued: [false, false],
        out: &mut *io.stdout,
    };
    let mut prev: [Option<Vec<u8>>; 2] = [None, None];
    let mut total = [0u64; 3];

    while cur[0].is_some() || cur[1].is_some() {
        let order = match (&cur[0], &cur[1]) {
            (None, _) => Ordering::Greater,
            (_, None) => Ordering::Less,
            (Some(a), Some(b)) => body(a).cmp(body(b)),
        };
        match order {
            Ordering::Equal => {
                total[2] += 1;
                m.writeline(cur[1].as_deref().unwrap_or_default(), 3)?;
            }
            Ordering::Less => {
                m.seen_unpairable = true;
                total[0] += 1;
                m.writeline(cur[0].as_deref().unwrap_or_default(), 1)?;
            }
            Ordering::Greater => {
                m.seen_unpairable = true;
                total[1] += 1;
                m.writeline(cur[1].as_deref().unwrap_or_default(), 2)?;
            }
        }
        let fill = [order != Ordering::Greater, order != Ordering::Less];
        for i in 0..2 {
            if !fill[i] {
                continue;
            }
            let old = cur[i].take();
            let next = match &mut srcs[i] {
                Src::Stdin => read_record(&mut stdin, delim),
                Src::File(f) => read_record(f, delim),
            };
            let next = match next {
                Ok(n) => n,
                Err(e) => {
                    let _ = m.out.flush();
                    return fail(err, &names[i], reason(&e));
                }
            };
            let checked = match (&old, &next, &prev[i]) {
                (Some(o), Some(n), _) => m.check_order(o, n, i, ctx, err),
                // EOF: compare the last record with the one before it.
                (Some(o), None, Some(pp)) => m.check_order(pp, o, i, ctx, err),
                _ => Ok(()),
            };
            if checked.is_err() {
                return Ok(1);
            }
            prev[i] = old;
            cur[i] = next;
        }
    }

    // [GNU] both operands "-": the second fclose(stdin) fails EBADF.
    if names[0] == "-" && names[1] == "-" {
        let _ = m.out.flush();
        let _ = writeln!(
            err,
            "{}",
            tr!(
                ctx,
                "command.comm.error.bad_file_descriptor",
                "comm: -: Bad file descriptor"
            )
        );
        return Ok(1);
    }

    if total_opt {
        let label = tr!(ctx, "command.comm.total", "total");
        let mut line = Vec::new();
        for t in total {
            line.extend_from_slice(t.to_string().as_bytes());
            line.extend_from_slice(&m.sep);
        }
        line.extend_from_slice(label.as_bytes());
        line.push(delim);
        m.out.write_all(&line)?;
    }

    if m.issued[0] || m.issued[1] {
        let _ = m.out.flush();
        let _ = writeln!(
            err,
            "{}",
            tr!(
                ctx,
                "command.comm.error.not_sorted",
                "comm: input is not in sorted order"
            )
        );
        return Ok(1);
    }
    Ok(0)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn setup(tag: &str) -> ScratchCtx {
        let sc = ScratchCtx::new(tag);
        std::fs::write(sc.path("f1"), "a\nb\nd\n").unwrap();
        std::fs::write(sc.path("f2"), "b\nc\nd\ne\n").unwrap();
        std::fs::write(sc.path("s1"), "b\na\n").unwrap();
        std::fs::write(sc.path("nl1"), "x").unwrap();
        std::fs::write(sc.path("nl2"), "x\ny").unwrap();
        std::fs::write(sc.path("crlf"), "a\r\nb\n").unwrap();
        std::fs::write(sc.path("bom"), "\u{feff}a\nb\n").unwrap();
        std::fs::create_dir_all(sc.path("d")).unwrap();
        sc
    }

    fn run(args: &[&str], stdin: &str) -> (u32, String, String) {
        let sc = setup("comm");
        let mut io = MemIo::new(stdin);
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = comm_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn three_columns() {
        let want = (0, "a\n\t\tb\n\tc\n\t\td\n\te\n".to_string(), String::new());
        assert_eq!(run(&["f1", "f2"], ""), want);
    }

    #[test]
    fn suppress_columns() {
        assert_eq!(run(&["-12", "f1", "f2"], "").1, "b\nd\n");
        assert_eq!(run(&["-3", "f1", "f2"], "").1, "a\n\tc\n\te\n");
        assert_eq!(run(&["-2", "f1", "f2"], "").1, "a\n\tb\n\td\n");
        assert_eq!(run(&["-123", "f1", "f2"], ""), (0, String::new(), String::new()));
    }

    #[test]
    fn output_delimiter_and_total() {
        let out = run(&["--output-delimiter=::", "--total", "-12", "f1", "f2"], "").1;
        assert_eq!(out, "b\nd\n1::2::2::total\n");
        let out = run(&["--output-delimiter=", "--total", "f1", "f2"], "").1;
        assert_eq!(out, "a\n\0\0b\n\0c\n\0\0d\n\0e\n1\x002\x002\0total\n");
        let same = ["--output-delimiter=x", "--output-delimiter=x", "f1", "f2"];
        assert_eq!(run(&same, "").0, 0);
        let differ = ["--output-delimiter=x", "--output-delimiter=y", "f1", "f2"];
        let (rc, out, err) = run(&differ, "");
        assert_eq!((rc, out.as_str()), (1, ""));
        assert_eq!(err, "comm: multiple output delimiters specified\n");
        let out = run(&["--tot", "f1", "f2"], "").1;
        assert_eq!(out.lines().last(), Some("1\t2\t2\ttotal"));
    }

    #[test]
    fn order_checking() {
        let (rc, out, err) = run(&["s1", "f2"], "");
        assert_eq!(rc, 1);
        assert_eq!(out, "\t\tb\na\n\tc\n\td\n\te\n");
        assert_eq!(
            err,
            "comm: file 1 is not in sorted order\ncomm: input is not in sorted order\n"
        );
        assert_eq!(run(&["s1", "s1"], ""), (0, "\t\tb\n\t\ta\n".into(), String::new()));
        let (rc, out, err) = run(&["--check-order", "s1", "f2"], "");
        assert_eq!((rc, out.as_str()), (1, "\t\tb\n"));
        assert_eq!(err, "comm: file 1 is not in sorted order\n");
        assert_eq!(run(&["--check-order", "--nocheck-order", "s1", "f2"], "").0, 0);
        assert_eq!(run(&["--nocheck-order", "--check-order", "s1", "f2"], "").0, 1);
    }

    #[test]
    fn records() {
        assert_eq!(run(&["nl1", "nl2"], "").1, "\t\tx\n\ty\n");
        let out = run(&["--total", "-z", "f1", "f2"], "").1;
        assert_eq!(out, "a\nb\nd\n\0\tb\nc\nd\ne\n\x001\t1\t0\ttotal\0");
        assert_eq!(run(&["crlf", "f1"], "").1, "\ta\na\r\n\t\tb\n\td\n");
        let (rc, out, _) = run(&["bom", "f1"], "");
        assert_eq!((rc, out.as_str()), (1, "\ta\n\tb\n\td\n\u{feff}a\nb\n"));
    }

    #[test]
    fn stdin_operands() {
        assert_eq!(run(&["-", "f2"], "b\n").1, "\t\tb\n\tc\n\td\n\te\n");
        let (rc, out, err) = run(&["--total", "-", "-"], "a\nb\nd\n");
        assert_eq!(rc, 1);
        assert_eq!(out, "a\n\tb\nd\n");
        assert_eq!(err, "comm: -: Bad file descriptor\n");
    }

    #[test]
    fn operand_and_file_errors() {
        let th = "Try 'comm --help' for more information.\n";
        assert_eq!(run(&[], "").2, format!("comm: missing operand\n{th}"));
        assert_eq!(run(&["f1"], "").2, format!("comm: missing operand after 'f1'\n{th}"));
        assert_eq!(run(&["f1", "f2", "f3"], "").2, format!("comm: extra operand 'f3'\n{th}"));
        assert_eq!(run(&["-4", "f1", "f2"], "").2, format!("comm: invalid option -- '4'\n{th}"));
        let nope = (1, String::new(), "comm: nope: No such file or directory\n".to_string());
        assert_eq!(run(&["nope", "d"], ""), nope);
        assert_eq!(run(&["d", "f2"], "").2, "comm: d: Is a directory\n");
        assert_eq!(run(&["f1", "d"], "").2, "comm: d: Is a directory\n");
        assert_eq!(run(&["--", "-z", "f1"], "").2, "comm: -z: No such file or directory\n");
    }
}
