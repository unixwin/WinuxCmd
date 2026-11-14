//! `paste` — GNU coreutils paste.c (9.4) semantics, ported
//! function-by-function from the source and probed byte-for-byte against
//! WSL coreutils 9.4.
//!
//! Parallel mode (default): one line per round from every file, joined by
//! the delimiter list; the list RESETS at the start of every output line
//! and advances per file boundary (probed: `paste -dabc f1 f2 f3` uses
//! 'a','b' on every line). A file that reaches EOF is closed; its would-be
//! delimiter is held in a side buffer and flushed before the next file's
//! data on the same line. Files with data keep the line going; when only
//! the last file produced data the line is terminated with the line
//! delimiter (a missing final newline is added, POSIX). Missing files
//! abort at open (rc 1, error(EXIT_FAILURE) — nothing is printed). `-` is
//! stdin; repeated `-` operands share one stream position (probed:
//! `paste - -` reads the input once).
//!
//! Serial mode (-s): files one at a time, lines joined with the
//! delimiter list cycling WITHIN the file and resetting per file; a
//! missing file reports an error and the next file is still processed.
//! An empty file still emits one bare newline (probed).
//!
//! -d LIST converts escapes (\0 \b \f \n \r \t \v \\; unknown \x keeps x);
//! `-d ''` becomes the one-character list "\\0" (the NUL EMPTY_DELIM, so
//! no delimiter is output). A trailing unescaped backslash is
//! `delimiter list ends with an unescaped backslash: 'X'`, rc 1.
//! -z makes NUL the input/output line delimiter. No operands = one stdin
//! operand. A `!<fifo>` marker operand (#1038) reads through the
//! named-pipe rendezvous (fs_win::open_fifo_read), the same O_RDONLY
//! blocking open GNU paste.c performs; the 7 marker bytes are never
//! content. Exit rc 1 if any open/read/close failed.

use std::cell::RefCell;
use std::ffi::OsString;
use std::io::{Read, Write};
use std::rc::Rc;

use crate::argopt;
use winux_core::{usage_error, CmdResult, Ctx, Io};

const EMPTY_DELIM: u8 = b'\0';

/// One input source; `None` = closed (EOF reached).
type Src<'a> = Rc<RefCell<Option<Box<dyn Read + 'a>>>>;

/// Wrap the injected stdin handle so several `-` operands can share it.
struct StreamReader<'a> {
    inner: &'a mut dyn Read,
}

impl Read for StreamReader<'_> {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        self.inner.read(buf)
    }
}

/// collapse_escapes(): returns the converted list, or None on a trailing
/// unescaped backslash.
fn collapse_escapes(s: &str) -> Option<Vec<u8>> {
    let bytes = s.as_bytes();
    let mut out = Vec::with_capacity(bytes.len());
    let mut i = 0usize;
    while i < bytes.len() {
        if bytes[i] != b'\\' {
            out.push(bytes[i]);
            i += 1;
        } else {
            i += 1;
            if i >= bytes.len() {
                return None; // backslash at end
            }
            match bytes[i] {
                b'0' => out.push(EMPTY_DELIM),
                b'b' => out.push(0x08),
                b'f' => out.push(0x0c),
                b'n' => out.push(b'\n'),
                b'r' => out.push(b'\r'),
                b't' => out.push(b'\t'),
                b'v' => out.push(0x0b),
                b'\\' => out.push(b'\\'),
                other => out.push(other),
            }
            i += 1;
        }
    }
    Some(out)
}

/// One input slot: a per-slot closed flag over a possibly shared stream
/// (two `-` operands read the SAME stream, like GNU's shared stdin FILE*,
/// but each slot closes independently so files_open counts down per slot).
struct Slot<'a> {
    src: Src<'a>,
    closed: bool,
}

fn read_byte(slot: &Slot<'_>) -> std::io::Result<Option<u8>> {
    if slot.closed {
        return Ok(None);
    }
    let mut guard = slot.src.borrow_mut();
    let reader = match guard.as_mut() {
        Some(r) => r,
        None => return Ok(None),
    };
    let mut b = [0u8; 1];
    loop {
        match reader.read(&mut b) {
            Ok(0) => return Ok(None),
            Ok(_) => return Ok(Some(b[0])),
            Err(e) if e.kind() == std::io::ErrorKind::Interrupted => continue,
            Err(e) => return Err(e),
        }
    }
}

/// paste_parallel(): the paste.c algorithm (delbuf handling included).
fn paste_parallel(
    sources: &mut [Slot<'_>],
    delims: &[u8],
    line_delim: u8,
    out: &mut dyn Write,
    err: &mut dyn Write,
) -> std::io::Result<bool> {
    let nfiles = sources.len();
    let mut files_open = nfiles;
    let mut ok = true;
    let mut delbuf: Vec<u8> = vec![0u8; nfiles + 2];

    while files_open > 0 {
        let mut delimptr = 0usize;
        let mut delims_saved = 0usize;
        let mut somedone = false;
        for i in 0..nfiles {
            if files_open == 0 {
                break;
            }
            let mut sometodo = false;
            let mut chr: Option<u8> = None;
            let mut had_err = false;
            if !sources[i].closed {
                match read_byte(&sources[i]) {
                    Ok(c) => chr = c,
                    Err(_) => {
                        had_err = true;
                        chr = None;
                    }
                }
                if chr.is_some() && delims_saved > 0 {
                    out.write_all(&delbuf[..delims_saved])?;
                    delims_saved = 0;
                }
                loop {
                    let c = match chr {
                        Some(c) => c,
                        None => break,
                    };
                    sometodo = true;
                    if c == line_delim {
                        break;
                    }
                    out.write_all(&[c])?;
                    match read_byte(&sources[i]) {
                        Ok(nc) => chr = nc,
                        Err(_) => {
                            had_err = true;
                            chr = None;
                        }
                    }
                }
                let _ = &mut had_err;
            }
            if !sometodo {
                // EOF, read error, or closed file: close the slot
                if !sources[i].closed {
                    if had_err {
                        let _ = writeln!(err, "paste: read error");
                        ok = false;
                    }
                    sources[i].closed = true;
                    files_open -= 1;
                }
                if i + 1 == nfiles {
                    // end of this output line
                    if somedone {
                        if delims_saved > 0 {
                            out.write_all(&delbuf[..delims_saved])?;
                            delims_saved = 0;
                        }
                        out.write_all(&[line_delim])?;
                    }
                } else {
                    // closed file; stash its delimiter
                    if delims[delimptr] != EMPTY_DELIM {
                        delbuf[delims_saved] = delims[delimptr];
                        delims_saved += 1;
                    }
                    delimptr = (delimptr + 1) % delims.len();
                }
            } else {
                somedone = true;
                if i + 1 != nfiles {
                    // replace the last newline with the delimiter
                    if let Some(c) = chr {
                        if c != line_delim {
                            out.write_all(&[c])?;
                        }
                    }
                    if delims[delimptr] != EMPTY_DELIM {
                        out.write_all(&[delims[delimptr]])?;
                    }
                    delimptr = (delimptr + 1) % delims.len();
                } else {
                    // last file: a missing final newline is added (POSIX)
                    let c = chr.unwrap_or(line_delim);
                    out.write_all(&[c])?;
                }
            }
        }
    }
    Ok(ok)
}

/// paste_serial(): join the lines of each file with the cycling delimiters.
fn paste_serial(
    sources: &[Slot<'_>],
    delims: &[u8],
    line_delim: u8,
    out: &mut dyn Write,
) -> std::io::Result<bool> {
    let ok = true;
    for src in sources.iter() {
        if src.closed {
            continue;
        }
        let mut delimptr = 0usize;
        let mut charold = read_byte(src)?;
        while let Some(co) = charold {
            let charnew = read_byte(src)?;
            let Some(cn) = charnew else {
                break;
            };
            if co == line_delim {
                if delims[delimptr] != EMPTY_DELIM {
                    out.write_all(&[delims[delimptr]])?;
                }
                delimptr = (delimptr + 1) % delims.len();
            } else {
                out.write_all(&[co])?;
            }
            charold = Some(cn);
        }
        match charold {
            Some(last) => out.write_all(&[last])?,
            None => {}
        }
        if charold != Some(line_delim) {
            out.write_all(&[line_delim])?;
        }
    }
    Ok(ok)
}

pub fn paste_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut serial = false;
    let mut zero_terminated = false;
    let mut delim_arg = "\t".to_string();
    let mut operands: Vec<String> = Vec::new();
    let mut idx = 0usize;
    while idx < args.len() {
        let arg = args[idx].to_string_lossy().into_owned();
        if arg == "--" {
            idx += 1;
            for a in &args[idx..] {
                operands.push(a.to_string_lossy().into_owned());
            }
            break;
        }
        if arg == "-" || !arg.starts_with('-') {
            operands.push(arg);
            idx += 1;
            continue;
        }
        if let Some(long) = arg.strip_prefix("--") {
            let (name, inline) = match long.split_once('=') {
                Some((n, v)) => (n.to_string(), Some(v.to_string())),
                None => (long.to_string(), None),
            };
            match name.as_str() {
                "serial" => serial = true,
                "delimiters" => match inline {
                    Some(v) => delim_arg = v,
                    None => {
                        idx += 1;
                        match args.get(idx) {
                            Some(v) => delim_arg = v.to_string_lossy().into_owned(),
                            None => {
                                let _ = usage_error(
                                    ctx,
                                    io,
                                    "paste",
                                    &argopt::option_requires_argument(ctx, "--delimiters"),
                                );
                                return Ok(1);
                            }
                        }
                    }
                },
                "zero-terminated" => zero_terminated = true,
                "help" | "version" => return Ok(0),
                _ => {
                    let _ = usage_error(ctx, io, "paste", &argopt::unrecognized_option(ctx, &arg));
                    return Ok(1);
                }
            }
            idx += 1;
            continue;
        }
        let chars: Vec<char> = arg.chars().skip(1).collect();
        let mut ci = 0usize;
        let mut consumed_next = false;
        while ci < chars.len() {
            match chars[ci] {
                's' => {
                    serial = true;
                    ci += 1;
                }
                'z' => {
                    zero_terminated = true;
                    ci += 1;
                }
                'd' => {
                    let rest: String = chars[ci + 1..].iter().collect();
                    if !rest.is_empty() {
                        delim_arg = rest;
                        ci = chars.len();
                    } else {
                        match args.get(idx + 1) {
                            Some(v) => {
                                delim_arg = v.to_string_lossy().into_owned();
                                consumed_next = true;
                                ci = chars.len();
                            }
                            None => {
                                let _ = usage_error(
                                    ctx,
                                    io,
                                    "paste",
                                    &argopt::short_option_requires_argument(ctx, "d"),
                                );
                                return Ok(1);
                            }
                        }
                    }
                }
                c => {
                    let _ = usage_error(
                        ctx,
                        io,
                        "paste",
                        &argopt::invalid_option(ctx, &c.to_string()),
                    );
                    return Ok(1);
                }
            }
        }
        idx += if consumed_next { 2 } else { 1 };
    }
    if operands.is_empty() {
        operands.push("-".to_string());
    }
    let delims = match collapse_escapes(&delim_arg) {
        Some(d) if !d.is_empty() => d,
        Some(_) => b"\0".to_vec(), // -d '' -> the NUL list
        None => {
            let _ = writeln!(
                io.stderr,
                "paste: delimiter list ends with an unescaped backslash: {delim_arg}"
            );
            return Ok(1);
        }
    };
    let line_delim: u8 = if zero_terminated { b'\0' } else { b'\n' };
    let nfiles = operands.len();
    // Build the sources; stdin is boxed once so several `-` share it.
    let stdin_src: Src<'_> = Rc::new(RefCell::new(Some(Box::new(StreamReader {
        inner: &mut io.stdin,
    }))));
    let mut ok = true;
    let mut sources: Vec<Slot<'_>> = Vec::with_capacity(nfiles);
    for op in &operands {
        if op == "-" {
            sources.push(Slot {
                src: Rc::clone(&stdin_src),
                closed: false,
            });
        } else {
            // the one operand boundary (drive dialect, carrier colons,
            // shell tree — the 1.x native_path layer)
            let path = crate::fs_win::api_path(ctx, op);
            // #1038 fifo bridge: a `!<fifo>` marker operand is a named
            // pipe behind an on-disk file — GNU paste opens it O_RDONLY
            // (blocking until a writer arrives) through the rendezvous,
            // NOT through the plain open below, which would read the 7
            // marker bytes as content (the wave3a CI regression; 1.x
            // open_binary_file took the same marker branch, 442d65bb).
            let opened: std::io::Result<Box<dyn Read>> = if crate::fs_win::is_winux_fifo_path(&path)
            {
                crate::fs_win::open_fifo_read(&path).map(|r| Box::new(r) as Box<dyn Read>)
            } else {
                std::fs::File::open(&path).map(|f| Box::new(f) as Box<dyn Read>)
            };
            match opened {
                Ok(f) => sources.push(Slot {
                    src: Rc::new(RefCell::new(Some(f))),
                    closed: false,
                }),
                Err(e) => {
                    // [GNU] a directory operand reads EISDIR (probed
                    // coreutils 9.4: `paste indir` -> "paste: indir: Is a
                    // directory" rc 1); Windows opens fail with
                    // ERROR_ACCESS_DENIED before any read, so the
                    // directory attribute supplies the POSIX shape
                    let reason = if e.kind() == std::io::ErrorKind::PermissionDenied
                        && crate::fs_win::file_meta(ctx, op, true)
                            .map(|m| m.is_dir)
                            .unwrap_or(false)
                    {
                        "Is a directory"
                    } else {
                        reason_str(&e)
                    };
                    let _ = writeln!(io.stderr, "paste: {op}: {reason}");
                    if serial {
                        // serial continues with the remaining files
                        sources.push(Slot {
                            src: Rc::new(RefCell::new(None)),
                            closed: true,
                        });
                        ok = false;
                        continue;
                    }
                    return Ok(1); // parallel aborts immediately (EXIT_FAILURE)
                }
            }
        }
    }
    let ok = if serial {
        paste_serial(&sources, &delims, line_delim, io.stdout)? && ok
    } else {
        paste_parallel(&mut sources, &delims, line_delim, io.stdout, io.stderr)? && ok
    };
    Ok(if ok { 0 } else { 1 })
}

fn reason_str(e: &std::io::Error) -> &'static str {
    match e.kind() {
        std::io::ErrorKind::NotFound => "No such file or directory",
        std::io::ErrorKind::PermissionDenied => "Permission denied",
        _ => "Invalid argument",
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn setup(tag: &str) -> ScratchCtx {
        let sc = ScratchCtx::new(tag);
        std::fs::write(sc.path("f1"), "a\nb\nc\n").unwrap();
        std::fs::write(sc.path("f2"), "1\n2\n3\n4\n").unwrap();
        std::fs::write(sc.path("f3"), "x\n").unwrap();
        std::fs::write(sc.path("fe"), "a\n\nb\n").unwrap();
        std::fs::write(sc.path("fn"), "no-newline-end").unwrap();
        std::fs::write(sc.path("fz"), "").unwrap();
        sc
    }

    fn run_sc(sc: &ScratchCtx, args: &[&str], stdin: &str) -> (u32, String, String) {
        let mut io = MemIo::new(stdin);
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = paste_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    fn run(args: &[&str], stdin: &str) -> (u32, String, String) {
        let sc = setup("paste");
        run_sc(&sc, args, stdin)
    }

    #[test]
    fn parallel_basics() {
        assert_eq!(run(&["f1", "f2"], "").1, "a\t1\nb\t2\nc\t3\n\t4\n");
        assert_eq!(run(&["f1", "f3"], "").1, "a\tx\nb\t\nc\t\n");
        assert_eq!(run(&["f2", "f1"], "").1, "1\ta\n2\tb\n3\tc\n4\t\n");
        assert_eq!(run(&["f1", "f1"], "").1, "a\ta\nb\tb\nc\tc\n");
        assert_eq!(run(&["f1"], "").1, "a\nb\nc\n");
    }

    #[test]
    fn delimiters() {
        assert_eq!(run(&["-d:", "f1", "f2"], "").1, "a:1\nb:2\nc:3\n:4\n");
        // the delimiter list RESETS each output line
        assert_eq!(run(&["-dxy", "f1", "f2"], "").1, "ax1\nbx2\ncx3\nx4\n");
        assert_eq!(run(&["-d\\t", "f1", "f2"], "").1, "a\t1\nb\t2\nc\t3\n\t4\n");
        assert_eq!(run(&["-d\\n", "f1", "f2"], "").1, "a\n1\nb\n2\nc\n3\n\n4\n");
        assert_eq!(run(&["-d@%", "f1", "f2"], "").1, "a@1\nb@2\nc@3\n@4\n");
        // three files, list cycles per line
        assert_eq!(
            run(&["-dabc", "f1", "f2", "f3"], "").1,
            "aa1bx\nba2b\nca3b\na4b\n"
        );
        // empty -d becomes the NUL list -> no delimiter output
        assert_eq!(run(&["-d", "", "f1", "f2"], "").1, "a1\nb2\nc3\n4\n");
        assert_eq!(run(&["-d\\0", "f1", "f2"], "").1, "a1\nb2\nc3\n4\n");
    }

    #[test]
    fn serial_mode() {
        assert_eq!(run(&["-s", "f1"], "").1, "a\tb\tc\n");
        assert_eq!(run(&["-s", "-d:", "f1", "f2"], "").1, "a:b:c\n1:2:3:4\n");
        assert_eq!(run(&["-s", "f1", "f2"], "").1, "a\tb\tc\n1\t2\t3\t4\n");
        // cycle continues across lines within a file, resets per file
        assert_eq!(run(&["-s", "-dxy", "f1", "f2"], "").1, "axbyc\n1x2y3x4\n");
        // empty file still emits a bare newline
        assert_eq!(run(&["-s", "fz", "f1"], "").1, "\na\tb\tc\n");
        assert_eq!(run(&["-s", "fn"], "").1, "no-newline-end\n");
    }

    #[test]
    fn eof_and_no_newline() {
        assert_eq!(run(&["fn", "f1"], "").1, "no-newline-end\ta\n\tb\n\tc\n");
        assert_eq!(run(&["f1", "fn"], "").1, "a\tno-newline-end\nb\t\nc\t\n");
        assert_eq!(run(&["fe", "f1"], "").1, "a\ta\n\tb\nb\tc\n");
        assert_eq!(run(&["fz", "f1"], "").1, "\ta\n\tb\n\tc\n");
    }

    #[test]
    fn stdin_operand() {
        assert_eq!(run(&[], "hi\n").1, "hi\n");
        assert_eq!(run(&["-"], "hi\n").1, "hi\n");
        // two stdin operands share the stream: second sees EOF
        assert_eq!(run(&["-", "-"], "q\nr\n").1, "q\tr\n");
        let sc = setup("paste-dash");
        assert_eq!(run_sc(&sc, &["-", "f1"], "q\nr\n").1, "q\ta\nr\tb\n\tc\n");
    }

    #[test]
    fn zero_terminated() {
        // NUL is the record delimiter; the file content passes through
        assert_eq!(run(&["-z", "f1"], "").1, "a\nb\nc\n\0");
        let sc = setup("paste-z");
        std::fs::write(sc.path("z2"), "a\0b\n").unwrap();
        assert_eq!(run_sc(&sc, &["-z", "z2"], "").1, "a\0b\n\0");
    }

    #[test]
    fn errors() {
        let sc = setup("paste-err");
        // parallel: missing file aborts with rc 1
        let (r, _, e) = run_sc(&sc, &["f1", "missing"], "");
        assert_eq!(r, 1);
        assert!(e.contains("paste: missing: No such file or directory"));
        // serial: missing file reported, others still run
        let (r, out, e) = run_sc(&sc, &["-s", "missing", "f1"], "");
        assert_eq!(r, 1);
        assert_eq!(out, "a\tb\tc\n");
        assert!(e.contains("paste: missing:"));
        // trailing backslash in the delimiter list
        let (r, _, e) = run(&["-d", "x\\", "f1"], "");
        assert_eq!(r, 1);
        assert!(e.contains("delimiter list ends with an unescaped backslash"));
        assert_eq!(run(&["--bogus"], "").0, 1);
        assert_eq!(run(&["--help"], "").0, 0);
        assert_eq!(run(&["--version"], "").0, 0);
    }
}
