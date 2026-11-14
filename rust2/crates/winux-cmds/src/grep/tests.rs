//! grep unit tests: in-memory IO (MemIo) and scratch directories. The GNU
//! byte faces asserted here were probed against WSL GNU grep 3.11 (the
//! differential gate holds the full matrix); these tests pin the
//! library-level contracts — streaming equivalence across read sizes and
//! buffer boundaries, early stop, routing — that the gate cannot see.

use super::*;
use std::io::Read;
use winux_core::test_support::{os, MemIo, ScratchCtx};

fn run_with(args: &[&str], input: &[u8], env: &[(&str, &str)]) -> (u32, Vec<u8>, String) {
    let mut sc = ScratchCtx::new("grep");
    for (k, v) in env {
        sc.env.set(k, v);
    }
    let mut m = MemIo::new("");
    m.input = std::io::Cursor::new(input.to_vec());
    let rc = {
        let mut io = m.split();
        grep_cmd(&os(args), &mut io, &sc.ctx()).unwrap()
    };
    (rc, m.output.clone(), m.err())
}

fn g(args: &[&str], input: &str) -> (u32, String, String) {
    let (rc, out, err) = run_with(args, input.as_bytes(), &[]);
    (rc, String::from_utf8_lossy(&out).into_owned(), err)
}

/// A reader that hands out at most `n` bytes per call (pipe-shaped).
struct Trickle {
    data: Vec<u8>,
    pos: usize,
    n: usize,
    reads: usize,
}

impl Read for Trickle {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        self.reads += 1;
        let k = self.n.min(buf.len()).min(self.data.len() - self.pos);
        buf[..k].copy_from_slice(&self.data[self.pos..self.pos + k]);
        self.pos += k;
        Ok(k)
    }
}

fn run_trickle(args: &[&str], data: &[u8], n: usize) -> (u32, Vec<u8>, usize, usize) {
    let sc = ScratchCtx::new("grep-trickle");
    let mut src = Trickle {
        data: data.to_vec(),
        pos: 0,
        n,
        reads: 0,
    };
    let mut out = Vec::new();
    let mut err = Vec::new();
    let rc = {
        let mut io = Io::new(&mut src, &mut out, &mut err);
        grep_cmd(&os(args), &mut io, &sc.ctx()).unwrap()
    };
    (rc, out, src.pos, src.reads)
}

#[test]
fn basic_selection_and_status() {
    assert_eq!(g(&["foo"], "foo bar\nbaz\nfoofoo\n"), (0, "foo bar\nfoofoo\n".into(), "".into()));
    assert_eq!(g(&["zzz"], "foo\n").0, 1);
    assert_eq!(g(&["-v", "foo"], "foo\nbar\n").1, "bar\n");
    assert_eq!(g(&["-c", "o"], "foo\nbar\nxo\n").1, "2\n");
    assert_eq!(g(&["foo"], "foo").1, "foo\n"); // missing final newline is added
    assert_eq!(g(&["-n", "-b", "x"], "ab\nx\n").1, "2:3:x\n");
}

#[test]
fn regex_literal_and_fixed_paths_agree() {
    let text = "a.c abc\nxyz\nABC\n";
    assert_eq!(g(&["-F", "a.c"], text).1, "a.c abc\n");
    assert_eq!(g(&["a.c"], text).1, "a.c abc\n");
    assert_eq!(g(&["-i", "abc"], text).1, "a.c abc\nABC\n");
    assert_eq!(g(&["-F", "-e", "xyz", "-e", "ABC"], text).1, "xyz\nABC\n");
    assert_eq!(g(&["-E", "x(y)z|^AB"], text).1, "xyz\nABC\n");
    // back-reference: interpreter path
    assert_eq!(g(&[r"\(b\)c a\1"], "bc ab\nbc ac\n").1, "bc ab\n");
}

#[test]
fn only_matching_is_leftmost_longest_over_the_set() {
    assert_eq!(g(&["-o", "-e", "foo", "-e", "foobar"], "foobar here\n").1, "foobar\n");
    assert_eq!(g(&["-o", "-b", "o"], "foo bar\nbaz\nfoofoo\n").1, "1:o\n2:o\n13:o\n14:o\n16:o\n17:o\n");
    // empty matches advance one character, nothing printed for them
    assert_eq!(g(&["-o", "b*"], "abc\n").1, "b\n");
}

#[test]
fn word_and_line_modes() {
    assert_eq!(g(&["-o", "-w", "fox"], "fox foxes firefox afox\n").1, "fox\n");
    assert_eq!(g(&["-w", "-c", ""], "a  b\n").1, "1\n");
    assert_eq!(g(&["-w", "-c", ""], "a b\n").1, "0\n");
    assert_eq!(g(&["-w", r"\(-\|\)"], "a -b\n").0, 1); // GNU regex walk
    assert_eq!(g(&["-x", "-E", "foo|baz"], "foo\nfoo bar\nbaz\n").1, "foo\nbaz\n");
    assert_eq!(g(&["-c", "-x", ""], "a\n\nb\n").1, "1\n");
    // Unicode word constituents in the UTF-8 model
    assert_eq!(g(&["-o", "-w", "foo"], "éfoo foo\n").1, "foo\n");
    let (_, out, _) = run_with(&["-o", "-w", "foo"], "éfoo\n".as_bytes(), &[("LC_ALL", "C")]);
    assert_eq!(out, b"foo\n");
}

#[test]
fn context_groups_and_max_count() {
    let c = "l1 no\nl2 fox\nl3 no\nl4 no\nl5 fox\nl6 no\n";
    assert_eq!(g(&["-A0", "fox"], c).1, "l2 fox\n--\nl5 fox\n");
    assert_eq!(g(&["-C1", "fox"], c).1, "l1 no\nl2 fox\nl3 no\nl4 no\nl5 fox\nl6 no\n");
    assert_eq!(g(&["-n1", "fox"], c).1, "1-l1 no\n2:l2 fox\n3-l3 no\n4-l4 no\n5:l5 fox\n6-l6 no\n");
    assert_eq!(
        g(&["-A1", "--group-separator=XX", "fox"], c).1,
        "l2 fox\nl3 no\nXX\nl5 fox\nl6 no\n"
    );
    assert_eq!(g(&["-A0", "--group-separator=XX", "--no-group-separator", "fox"], c).1, "l2 fox\nl5 fox\n");
    // -m stops selection; trailing context still prints later matches
    assert_eq!(g(&["-m1", "-A2", "fox"], "fox\nfox\nx\nfox\ny\n").1, "fox\nfox\nx\n");
    assert_eq!(g(&["-m1", "-c", "-v", "fox"], "fox\nfox\nx\nfox\ny\n").1, "1\n");
    assert_eq!(g(&["-m", "-1", "a"], "a\na\n").1, "a\na\n");
    assert_eq!(g(&["-v", "-m", "-1", "zz"], "a\n").0, 1);
}

#[test]
fn binary_files_and_encoding_errors() {
    let (rc, out, err) = run_with(&["fox"], b"abc\0def\nfox here\n", &[]);
    assert_eq!((rc, out.as_slice()), (0, &b""[..]));
    assert_eq!(err, "grep: (standard input): binary file matches\n");
    assert_eq!(run_with(&["-c", "a"], b"a\0a\n", &[]).1, b"2\n"); // NUL zapped to eol
    assert_eq!(run_with(&["-a", "-n", "a"], b"a\0a\nfox\n", &[]).1, b"1:a\0a\n");
    let (rc, out, _) = run_with(&["-I", "-c", "z"], b"z\nx\0y\nz\n", &[]);
    assert_eq!((rc, out), (1, b"0\n".to_vec()));
    // UTF-8 model: an output line with an encoding error is suppressed
    let (_, out, err) = run_with(&["-n", "."], b"ok\nbad\xff\nok2\n", &[]);
    assert_eq!(out, b"1:ok\n3:ok2\n");
    assert_eq!(err, "grep: (standard input): binary file matches\n");
    let (_, out, err) = run_with(&["-n", "."], b"ok\nbad\xff\nok2\n", &[("LC_ALL", "C")]);
    assert_eq!(out, b"1:ok\n2:bad\xff\n3:ok2\n");
    assert_eq!(err, "");
    let (_, out, _) = run_with(&["-A1", "ok"], b"ok\nbad\xff\nok2\nzz\n", &[]);
    assert_eq!(out, b"ok\n--\nok2\nzz\n");
}

#[test]
fn color_sgr_shape_and_grep_color_warning() {
    let (_, out, err) = run_with(&["--color=always", "foo"], b"foo bar\n", &[("GREP_COLOR", "1")]);
    assert_eq!(out, b"\x1b[1m\x1b[Kfoo\x1b[m\x1b[K bar\n");
    assert_eq!(err, "grep: warning: GREP_COLOR='1' is deprecated; use GREP_COLORS='mt=1'\n");
    let (_, _, err) = run_with(
        &["--color=always", "foo"],
        b"foo\n",
        &[("GREP_COLOR", "1"), ("GREP_COLORS", "mt=32")],
    );
    assert_eq!(err, "");
    let (_, out, _) = run_with(&["--color=always", "foo"], b"foo bar\r\n", &[("GREP_COLORS", "sl=1")]);
    assert_eq!(out, b"\x1b[1m\x1b[K\x1b[01;31m\x1b[Kfoo\x1b[m\x1b[K\x1b[1m\x1b[K bar\x1b[m\x1b[K\r\n");
    assert_eq!(g(&["--color=auto", "foo"], "foo\n").1, "foo\n");
}

#[test]
fn diagnostics_and_option_order() {
    assert_eq!(g(&["["], "x\n"), (2, "".into(), "grep: Invalid regular expression\n".into()));
    assert_eq!(g(&["[a"], "x\n").2, "grep: Unmatched [, [^, [:, [., or [=\n");
    assert_eq!(g(&["[:alpha:]"], "x\n").2, "grep: character class syntax is [[:space:]], not [:space:]\n");
    assert_eq!(g(&["-E", "*a"], "a\n").2, "grep: warning: * at start of expression\n");
    assert_eq!(g(&["--help", "--bogus"], "").0, 2);
    assert_eq!(g(&["-V", "-m", "x"], "").2, "grep: invalid max count\n");
    assert_eq!(g(&["-1234567890123456789012", "x"], "").2, "grep: 123456789012345678901...: invalid context length argument\n");
    assert_eq!(g(&["-d", "re", "a"], "").0, 1);
    assert_eq!(g(&["--ex=a", "x"], "").2.lines().next().unwrap(),
        "grep: option '--ex=a' is ambiguous; possibilities: '--extended-regexp' '--exclude' '--exclude-from' '--exclude-dir'");
    assert_eq!(g(&["-E", "-F", "x"], "").2, "grep: conflicting matchers specified\n");
    assert_eq!(g(&[], "").0, 2);
    assert!(g(&["--version"], "").1.starts_with("grep (GNU grep) 3.11\n"));
    assert!(g(&["--color=bogus"], "").1.starts_with("Usage: grep [OPTION]... PATTERNS [FILE]...\n"));
}

#[test]
fn perl_is_routed_not_implemented() {
    assert!(uses_perl(&os(&["-P", "x"])));
    assert!(uses_perl(&os(&["-inP", "x", "f"])));
    assert!(uses_perl(&os(&["--perl", "x"])));
    assert!(uses_perl(&os(&["x", "f", "-P"])));
    assert!(uses_perl(&os(&["-X", "perl", "x"])));
    assert!(!uses_perl(&os(&["-e", "-P", "f"])));
    assert!(!uses_perl(&os(&["--", "x", "-P"])));
    assert!(!uses_perl(&os(&["-E", "x"])));
    assert!(!uses_perl_with(&os(&["x", "f", "-P"]), true));
    let (rc, out, err) = g(&["-P", "foo"], "foo\n");
    assert_eq!((rc, out.as_str()), (2, ""));
    assert_eq!(err, "grep: Perl matching not supported in a --disable-perl-regexp build\n");
}

#[test]
fn egrep_fgrep_wrappers() {
    let sc = ScratchCtx::new("egrep");
    let mut m = MemIo::new("fo\nfoo\n");
    let rc = egrep_cmd(&os(&["fo+$"]), &mut m.split(), &sc.ctx()).unwrap();
    assert_eq!((rc, m.out()), (0, "fo\nfoo\n".to_string()));
    assert_eq!(m.err(), "egrep: warning: egrep is obsolescent; using grep -E\n");
    let mut m = MemIo::new("a+\n");
    let rc = fgrep_cmd(&os(&["a+"]), &mut m.split(), &sc.ctx()).unwrap();
    assert_eq!((rc, m.out()), (0, "a+\n".to_string()));
    let mut m = MemIo::new("");
    let rc = fgrep_cmd(&os(&["-E", "x"]), &mut m.split(), &sc.ctx()).unwrap();
    assert_eq!(rc, 2);
    assert!(m.err().ends_with("grep: conflicting matchers specified\n"));
}

#[test]
fn pattern_sources() {
    let sc = ScratchCtx::new("grep-f");
    std::fs::write(sc.path("p"), "foo\n[\n\\(\n").unwrap();
    std::fs::write(sc.path("e"), "").unwrap();
    std::fs::write(sc.path("t"), "foo\nbar\n").unwrap();
    let run = |args: &[&str], input: &str| {
        let mut m = MemIo::new(input);
        let rc = grep_cmd(&os(args), &mut m.split(), &sc.ctx()).unwrap();
        (rc, m.out(), m.err())
    };
    assert_eq!(run(&["-f", "p", "t"], "").0, 2);
    assert_eq!(run(&["-f", "p", "t"], "").2, "grep: p:2: Invalid regular expression\ngrep: p:3: Unmatched ( or \\(\n");
    assert_eq!(run(&["-f", "e", "t"], "").0, 1);
    assert_eq!(run(&["-v", "-f", "e", "t"], "").1, "foo\nbar\n");
    assert_eq!(run(&["-f", "-", "t"], "bar\n").1, "bar\n");
    assert_eq!(run(&["-f", "missing", "t"], "").2, "grep: missing: No such file or directory\n");
}

#[test]
fn files_recursion_and_selection_rules() {
    let sc = ScratchCtx::new("grep-r");
    std::fs::create_dir_all(sc.path("d/sub")).unwrap();
    std::fs::write(sc.path("d/sub/a.c"), "fox c\n").unwrap();
    std::fs::write(sc.path("d/b.log"), "fox log\n").unwrap();
    let run = |args: &[&str]| {
        let mut m = MemIo::new("");
        let rc = grep_cmd(&os(args), &mut m.split(), &sc.ctx()).unwrap();
        (rc, m.out(), m.err())
    };
    let (_, out, _) = run(&["-r", "--include=*.c", "fox", "d"]);
    assert_eq!(out, "d/sub/a.c:fox c\n");
    assert_eq!(run(&["-r", "--exclude-dir=sub", "fox", "d"]).1, "d/b.log:fox log\n");
    assert_eq!(run(&["-r", "--exclude-dir=d", "fox", "d"]).0, 1);
    assert_eq!(run(&["--exclude=a.c", "fox", "d/sub/a.c"]).0, 1); // unanchored suffix
    assert_eq!(run(&["fox", "d"]).2, "grep: d: Is a directory\n");
    assert_eq!(run(&["-s", "fox", "d"]), (2, String::new(), String::new()));
    assert_eq!(run(&["-d", "skip", "fox", "d"]).0, 1);
    assert_eq!(run(&["fox", "nope", "d/b.log"]).2, "grep: nope: No such file or directory\n");
    assert_eq!(run(&["-q", "fox", "nope", "d/b.log"]).0, 0);
    assert_eq!(run(&["-q", "zzz", "nope", "d/b.log"]).0, 2);
    assert_eq!(run(&["-L", "zzz", "d/b.log"]), (1, "d/b.log\n".into(), "".into()));
    assert_eq!(run(&["-l", "-Z", "fox", "d/b.log"]).1, "d/b.log\0");
    assert_eq!(run(&["-c", "fox", "d/b.log", "d/sub/a.c"]).1, "d/b.log:1\nd/sub/a.c:1\n");
    assert_eq!(run(&["-r", "fox", "d/b.log"]).1, "fox log\n"); // -r on a file: no name
}

#[test]
fn streaming_is_read_size_independent() {
    // ~1.3 MB of lines with sparse matches, NUL-free; lines of varied
    // length so buffer refills cut lines everywhere.
    let mut data = Vec::new();
    let mut n = 0u32;
    while data.len() < 1_300_000 {
        n = n.wrapping_mul(1103515245).wrapping_add(12345);
        let len = (n >> 16) % 120;
        for k in 0..len {
            data.push(b"abcdefgh xyz"[((n >> (k % 13)) % 12) as usize]);
        }
        if n % 17 == 0 {
            data.extend_from_slice(b" needle");
        }
        data.push(b'\n');
    }
    for args in [
        &["needle"][..],
        &["-n", "-b", "-C2", "needle"],
        &["-v", "-c", "e"],
        &["-o", "-E", "ne+dle|xyz a"],
        &["-B3", "-m5", "needle"],
    ] {
        let whole = run_with(args, &data, &[]);
        for chunk in [1usize, 7, 4096, 100_000] {
            let (rc, out, _, _) = run_trickle(args, &data, chunk);
            assert_eq!((rc, &out), (whole.0, &whole.1), "{args:?} chunk {chunk}");
        }
    }
    // reference: plain selection equals a naive line filter
    let want: Vec<u8> = data
        .split(|&b| b == b'\n')
        .filter(|l| l.windows(6).any(|w| w == b"needle"))
        .flat_map(|l| l.iter().copied().chain(std::iter::once(b'\n')))
        .collect();
    assert_eq!(run_with(&["needle"], &data, &[]).1, want);
}

#[test]
fn long_lines_cross_buffer_boundaries() {
    let mut line = vec![b'a'; 300_000];
    line.extend_from_slice(b"needle");
    line.extend(vec![b'b'; 250_000]);
    let mut data = b"before\n".to_vec();
    data.extend_from_slice(&line);
    data.extend_from_slice(b"\nafter\n");
    let (rc, out, _, _) = run_trickle(&["-c", "needle"], &data, 4096);
    assert_eq!((rc, out), (0, b"1\n".to_vec()));
    let (_, out, _, _) = run_trickle(&["-B1", "-A1", "-n", "needle"], &data, 65536);
    let mut want = b"1-before\n2:".to_vec();
    want.extend_from_slice(&line);
    want.extend_from_slice(b"\n3-after\n");
    assert_eq!(out, want);
}

#[test]
fn early_stop_reads_only_what_it_needs() {
    let mut data = b"hit\n".to_vec();
    data.extend(std::iter::repeat(b"filler line\n").take(500_000).flatten());
    for args in [&["-q", "hit"][..], &["-l", "hit"], &["-m1", "hit"]] {
        let (rc, _, consumed, _) = run_trickle(args, &data, 65536);
        assert_eq!(rc, 0, "{args:?}");
        assert!(consumed < 200_000, "{args:?} read {consumed} of {}", data.len());
    }
}

#[test]
fn line_buffered_flushes_every_line() {
    struct Counting {
        flushes: usize,
        data: Vec<u8>,
    }
    impl std::io::Write for Counting {
        fn write(&mut self, b: &[u8]) -> std::io::Result<usize> {
            self.data.extend_from_slice(b);
            Ok(b.len())
        }
        fn flush(&mut self) -> std::io::Result<()> {
            self.flushes += 1;
            Ok(())
        }
    }
    let sc = ScratchCtx::new("grep-lb");
    for (args, min_flushes) in [(&["--line-buffered", "a"][..], 3usize), (&["a"][..], 1)] {
        let mut input = std::io::Cursor::new(b"a\na\na\nb\n".to_vec());
        let mut out = Counting {
            flushes: 0,
            data: Vec::new(),
        };
        let mut err = Vec::new();
        let rc = {
            let mut io = Io::new(&mut input, &mut out, &mut err);
            grep_cmd(&os(args), &mut io, &sc.ctx()).unwrap()
        };
        assert_eq!(rc, 0);
        assert_eq!(out.data, b"a\na\na\n");
        if min_flushes > 1 {
            assert!(out.flushes >= min_flushes, "{args:?}: {}", out.flushes);
        } else {
            assert!(out.flushes <= 2, "{args:?}: {}", out.flushes);
        }
    }
}

#[test]
fn write_error_is_reported() {
    struct Broken;
    impl std::io::Write for Broken {
        fn write(&mut self, _: &[u8]) -> std::io::Result<usize> {
            Err(std::io::Error::new(std::io::ErrorKind::BrokenPipe, "x"))
        }
        fn flush(&mut self) -> std::io::Result<()> {
            Ok(())
        }
    }
    let sc = ScratchCtx::new("grep-we");
    let mut input = std::io::Cursor::new(b"a\n".to_vec());
    let mut out = Broken;
    let mut err = Vec::new();
    let rc = {
        let mut io = Io::new(&mut input, &mut out, &mut err);
        grep_cmd(&os(&["a"]), &mut io, &sc.ctx()).unwrap()
    };
    assert_eq!(rc, 2);
    assert!(String::from_utf8_lossy(&err).starts_with("grep: write error: "));
}

#[test]
fn null_data_mode() {
    assert_eq!(run_with(&["-z", "a"], b"a\nx\0b\0", &[]).1, b"a\nx\0");
    assert_eq!(run_with(&["-z", "-c", "a"], b"a\0b\0", &[]).1, b"1\n");
    assert_eq!(run_with(&["-z", "-n", "b"], b"a\nb\0c\0", &[]).1, b"1:a\nb\0");
}
