//! `tee` — GNU coreutils tee.c (9.4) semantics, ported from the source and
//! probed against WSL coreutils 9.4.
//!
//! Copies stdin to stdout and to every named file. All files are opened
//! BEFORE reading starts (mode "wb", or "ab" with -a); a failed open
//! reports the error and that output is dropped while the others run.
//! Output is copied in chunks; a failed write drops that output (the
//! remaining outputs still receive the data). Failure semantics follow
//! tee.c fail_output() with the default output-error=sigpipe: an EPIPE
//! class failure is SILENT and does not fail the command (SIGPIPE is
//! ignored, the pipe output is just dropped); any other write error is
//! reported and sets rc 1. -p/--output-error selects the POSIX modes:
//! warn-nopipe / exit-nopipe / warn / exit. stdin read errors report
//! "read error" and set rc 1. `-` is NOT special (POSIX; it names a file
//! literally, as GNU documents). -i is accepted and ignored (SIGINT
//! handling does not apply on Windows).
//!
//! Exit rc 1 only when a reportable open/write/close error occurred.

use std::ffi::OsString;

use std::io::{ErrorKind, Write};

use winux_core::{usage_error, CmdResult, Ctx, Io};

#[derive(Clone, Copy, PartialEq, Eq)]
enum OutputError {
    /// default: EPIPE silent, other errors reported (rc 1)
    Sigpipe,
    /// -p: EPIPE silent+unreported, others reported
    WarnNopipe,
    /// --output-error=exit-nopipe: EPIPE silent but exits rc 1
    ExitNopipe,
    /// --output-error=warn: all write errors reported
    Warn,
    /// --output-error=exit: all write errors reported and exit rc 1
    Exit,
}

fn epipe_like(e: &std::io::Error) -> bool {
    // Windows reports broken pipes as BrokenPipe; the CRT also surfaces
    // ERROR_NO_DATA / "pipe is being closed" as Uncategorized on some
    // stacks, so match the classic raw codes too.
    if e.kind() == ErrorKind::BrokenPipe {
        return true;
    }
    matches!(
        e.raw_os_error(),
        Some(232) /* ERROR_NO_DATA */ | Some(109) /* ERROR_BROKEN_PIPE */
    )
}

/// tee_files(): open all outputs, stream stdin to each.
fn tee_files(
    io: &mut Io,
    files: &[String],
    ctx: &Ctx,
    append: bool,
    output_error: OutputError,
) -> CmdResult {
    // descriptors[0] is stdout; entry i is files[i-1]
    struct Dest<'a> {
        name: String,
        handle: Option<Box<dyn Write + 'a>>,
    }
    let mut dests: Vec<Dest<'_>> = Vec::with_capacity(files.len() + 1);
    dests.push(Dest {
        name: "standard output".to_string(),
        handle: Some(Box::new(&mut *io.stdout)),
    });
    let mut ok = true;
    for name in files {
        let name = crate::fs_win::map_device(name);
        let path = if std::path::Path::new(name).is_absolute() {
            std::path::PathBuf::from(name)
        } else {
            ctx.cwd.join(name)
        };
        match std::fs::OpenOptions::new()
            .write(true)
            .create(true)
            .append(append)
            .truncate(!append)
            .open(&path)
        {
            Ok(f) => dests.push(Dest {
                name: name.to_string(),
                handle: Some(Box::new(f)),
            }),
            Err(e) => {
                let _ = writeln!(io.stderr, "tee: {name}: {}", reason_str(&e));
                ok = false;
                dests.push(Dest {
                    name: name.to_string(),
                    handle: None,
                });
            }
        }
    }

    let mut buf = [0u8; 8192];
    loop {
        let n = match io.stdin.read(&mut buf) {
            Ok(0) => break,
            Ok(n) => n,
            Err(e) if e.kind() == ErrorKind::Interrupted => continue,
            Err(_) => {
                let _ = writeln!(io.stderr, "tee: read error");
                ok = false;
                break;
            }
        };
        for d in dests.iter_mut() {
            let Some(handle) = d.handle.as_mut() else {
                continue;
            };
            match handle.write_all(&buf[..n]).and_then(|()| handle.flush()) {
                Ok(()) => {}
                Err(e) => {
                    let fail = !epipe_like(&e)
                        || matches!(
                            output_error,
                            OutputError::Exit | OutputError::Warn | OutputError::ExitNopipe
                        );
                    // exit_* modes stop the command right away (GNU calls
                    // error() with a nonzero status, which exits)
                    let exit_now =
                        matches!(output_error, OutputError::Exit | OutputError::ExitNopipe);
                    if fail {
                        let _ = writeln!(io.stderr, "tee: {}: {}", d.name, reason_str(&e));
                        ok = false;
                    }
                    d.handle = None;
                    if exit_now {
                        return Ok(1);
                    }
                }
            }
        }
    }
    Ok(if ok { 0 } else { 1 })
}

fn reason_str(e: &std::io::Error) -> &'static str {
    match e.kind() {
        ErrorKind::NotFound => "No such file or directory",
        ErrorKind::PermissionDenied => "Permission denied",
        ErrorKind::BrokenPipe => "Broken pipe",
        _ => "Invalid argument",
    }
}

pub fn tee_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut append = false;
    let mut output_error = OutputError::Sigpipe;
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
                "append" => append = true,
                "ignore-interrupts" => { /* SIGINT does not apply on Windows */ }
                "output-error" => {
                    let v = match inline {
                        Some(v) => v,
                        None => {
                            // bare -p / --output-error = warn-nopipe
                            output_error = OutputError::WarnNopipe;
                            idx += 1;
                            continue;
                        }
                    };
                    match v.as_str() {
                        "warn-nopipe" => output_error = OutputError::WarnNopipe,
                        "exit-nopipe" => output_error = OutputError::ExitNopipe,
                        "warn" => output_error = OutputError::Warn,
                        "exit" => output_error = OutputError::Exit,
                        _ => {
                            let _ = usage_error(
                                io,
                                "tee",
                                &format!("invalid argument '{v}' for '--output-error'"),
                            );
                            return Ok(1);
                        }
                    }
                }
                "help" | "version" => return Ok(0),
                _ => {
                    let _ = usage_error(io, "tee", &format!("unrecognized option '{arg}'"));
                    return Ok(1);
                }
            }
            idx += 1;
            continue;
        }
        let chars: Vec<char> = arg.chars().skip(1).collect();
        let mut ci = 0usize;
        while ci < chars.len() {
            match chars[ci] {
                'a' => append = true,
                'i' => { /* ignore-interrupts: no-op on Windows */ }
                'p' => {
                    // bare -p is warn-nopipe; getopt never gives -p a value
                    output_error = OutputError::WarnNopipe;
                }
                c => {
                    let _ = usage_error(io, "tee", &format!("invalid option -- '{c}'"));
                    return Ok(1);
                }
            }
            ci += 1;
        }
        idx += 1;
    }
    tee_files(io, &operands, ctx, append, output_error)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str], stdin: &str) -> (u32, String, String) {
        let sc = ScratchCtx::new("tee");
        let mut io = MemIo::new(stdin);
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = tee_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    fn read_sc(sc: &ScratchCtx, name: &str) -> String {
        std::fs::read_to_string(sc.path(name)).unwrap_or_default()
    }

    #[test]
    fn basic_copy() {
        let sc = ScratchCtx::new("tee-basic");
        let (r, out, err) = {
            let mut io = MemIo::new("hello\n");
            let ctx = sc.ctx();
            let owned: Vec<OsString> = ["out1"].iter().map(Into::into).collect();
            let r = tee_cmd(&owned, &mut io.split(), &ctx).unwrap();
            (r, io.out(), io.err())
        };
        assert_eq!(r, 0);
        assert_eq!(out, "hello\n");
        assert_eq!(read_sc(&sc, "out1"), "hello\n");
        assert_eq!(err, "");
    }

    #[test]
    fn multi_file_and_no_args() {
        let sc = ScratchCtx::new("tee-multi");
        let (r, out) = {
            let mut io = MemIo::new("xy\n");
            let ctx = sc.ctx();
            let owned: Vec<OsString> = ["out1", "out2"].iter().map(Into::into).collect();
            let r = tee_cmd(&owned, &mut io.split(), &ctx).unwrap();
            (r, io.out())
        };
        assert_eq!(r, 0);
        assert_eq!(out, "xy\n");
        assert_eq!(read_sc(&sc, "out1"), "xy\n");
        assert_eq!(read_sc(&sc, "out2"), "xy\n");
        // no operands: stdout passthrough only
        let (r, out) = {
            let mut io = MemIo::new("abc");
            let ctx = sc.ctx();
            let owned: Vec<OsString> = vec![];
            let r = tee_cmd(&owned, &mut io.split(), &ctx).unwrap();
            (r, io.out())
        };
        assert_eq!(r, 0);
        assert_eq!(out, "abc");
    }

    #[test]
    fn append_and_truncate() {
        let sc = ScratchCtx::new("tee-append");
        std::fs::write(sc.path("f"), "old\n").unwrap();
        {
            let mut io = MemIo::new("new\n");
            let ctx = sc.ctx();
            let owned: Vec<OsString> = ["-a", "f"].iter().map(Into::into).collect();
            assert_eq!(tee_cmd(&owned, &mut io.split(), &ctx).unwrap(), 0);
        }
        assert_eq!(read_sc(&sc, "f"), "old\nnew\n");
        {
            let mut io = MemIo::new("x\n");
            let ctx = sc.ctx();
            let owned: Vec<OsString> = ["f"].iter().map(Into::into).collect();
            assert_eq!(tee_cmd(&owned, &mut io.split(), &ctx).unwrap(), 0);
        }
        assert_eq!(read_sc(&sc, "f"), "x\n");
    }

    #[test]
    fn dashdash_and_dash_operand() {
        // `-` is a literal file name (POSIX, GNU documented)
        let sc = ScratchCtx::new("tee-dash");
        {
            let mut io = MemIo::new("z\n");
            let ctx = sc.ctx();
            let owned: Vec<OsString> = ["out1", "-", "out2"].iter().map(Into::into).collect();
            assert_eq!(tee_cmd(&owned, &mut io.split(), &ctx).unwrap(), 0);
        }
        assert!(sc.path("-").exists());
        assert_eq!(read_sc(&sc, "out1"), "z\n");
        assert_eq!(read_sc(&sc, "out2"), "z\n");
        // -- ends options
        let sc2 = ScratchCtx::new("tee-dd");
        {
            let mut io = MemIo::new("q\n");
            let ctx = sc2.ctx();
            let owned: Vec<OsString> = ["--", "out1"].iter().map(Into::into).collect();
            assert_eq!(tee_cmd(&owned, &mut io.split(), &ctx).unwrap(), 0);
        }
        assert_eq!(read_sc(&sc2, "out1"), "q\n");
    }

    #[test]
    fn open_failure_continues() {
        let sc = ScratchCtx::new("tee-fail");
        let (r, out, err) = {
            let mut io = MemIo::new("q\n");
            let ctx = sc.ctx();
            let owned: Vec<OsString> = ["missing-dir/out", "ok1"].iter().map(Into::into).collect();
            let r = tee_cmd(&owned, &mut io.split(), &ctx).unwrap();
            (r, io.out(), io.err())
        };
        assert_eq!(r, 1);
        assert_eq!(out, "q\n");
        assert!(err.contains("missing-dir/out"));
        assert_eq!(read_sc(&sc, "ok1"), "q\n");
        assert_eq!(run(&["--bogus"], "x\n").0, 1);
        assert_eq!(run(&["--help"], "").0, 0);
    }

    #[test]
    fn binary_passthrough() {
        let sc = ScratchCtx::new("tee-bin");
        let input = "binary\0data".to_string();
        let (r, out) = {
            let mut io = MemIo::new(&input);
            let ctx = sc.ctx();
            let owned: Vec<OsString> = ["out1"].iter().map(Into::into).collect();
            let r = tee_cmd(&owned, &mut io.split(), &ctx).unwrap();
            (r, io.out())
        };
        assert_eq!(r, 0);
        assert_eq!(out, input);
        assert_eq!(std::fs::read(sc.path("out1")).unwrap(), input.as_bytes());
    }
}
