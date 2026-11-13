//! `tee` — GNU coreutils tee.c (9.4) semantics, ported from the source and
//! probed against WSL coreutils 9.4.
//!
//! Copies stdin to stdout and to every named file. All files are opened
//! BEFORE reading starts (mode "wb", or "ab" with -a); a failed open
//! reports the error and that output is dropped while the others run.
//! Output is copied in chunks; a failed write drops that output (the
//! remaining outputs still receive the data). Failure semantics follow
//! tee.c fail_output() with the default output-error=sigpipe: SIGPIPE
//! stays at its DEFAULT disposition (tee.c:172-173 ignores it only under
//! -p/--output-error), so a broken-pipe write KILLS tee by signal — the
//! Windows emulation fails the run right there, silent, rc 1; any other
//! write error is reported and sets rc 1. -p/--output-error selects the
//! POSIX modes: warn-nopipe / exit-nopipe / warn / exit (SIGPIPE ignored
//! there, EPIPE silent per fail_output). stdin read errors report
//! "read error" and set rc 1. `-` is NOT special (POSIX; it names a file
//! literally, as GNU documents). -i is accepted and ignored (SIGINT
//! handling does not apply on Windows). --output-error=MODE resolves MODE
//! through GNU argmatch (argmatch.c): exact match, else a unique prefix
//! (`warn-n` -> warn-nopipe); ambiguous prefixes report "ambiguous
//! argument" and unknown values "invalid argument", both followed by the
//! Valid-arguments list and rc 1. An open failure under the exit modes
//! dies before stdin is read (later operands are never opened).
//!
//! A `!<fifo>` marker operand (#1038) opens through the named-pipe
//! rendezvous (fs_win::open_fifo_write) — GNU tee.c's O_WRONLY open on a
//! fifo blocks until a reader arrives, and the marker file itself is
//! never created/truncated.
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
        // the one operand boundary (fs_win::api_path): devices resolve
        // verbatim (tee > NUL discards), POSIX spellings map through the
        // shell tree (#1488); the reported name keeps the operand text
        let path = crate::fs_win::api_path(ctx, name);
        // #1038 fifo bridge: a `!<fifo>` marker operand is a named pipe
        // behind an on-disk file — GNU tee opens it O_WRONLY (blocking
        // until a reader arrives, create/truncate never apply) through the
        // rendezvous, NOT through the plain open below, which would clobber
        // the marker and strand every POSIX-blocking reader (the wave3a CI
        // regression; the mkfifo/head rendezvous pins spawn `tee <fifo>` as
        // the writer). Mirrors 1.x create_binary_file's marker branch.
        let opened: std::io::Result<Box<dyn Write>> = if crate::fs_win::is_winux_fifo_path(&path) {
            crate::fs_win::open_fifo_write(&path).map(|f| Box::new(f) as Box<dyn Write>)
        } else {
            std::fs::OpenOptions::new()
                .write(true)
                .create(true)
                .append(append)
                .truncate(!append)
                .open(&path)
                .map(|f| Box::new(f) as Box<dyn Write>)
        };
        match opened {
            Ok(f) => dests.push(Dest {
                name: name.to_string(),
                handle: Some(f),
            }),
            Err(e) => {
                let _ = writeln!(io.stderr, "tee: {name}: {}", reason_str(&e));
                // tee.c tee_files(): the open-loop error carries a nonzero
                // status under the exit modes (error(exit || exit_nopipe,
                // errno, ...)), which EXITS right there — later operands are
                // never opened and stdin is never read. probed WSL GNU
                // coreutils 9.4 with `--output-error=exit no/dir/f good.txt`:
                // empty stdout, good.txt not created, rc 1 (same under
                // exit-nopipe); the non-exit modes keep the remaining
                // outputs flowing.
                if matches!(output_error, OutputError::Exit | OutputError::ExitNopipe) {
                    return Ok(1);
                }
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
                    // GNU's DEFAULT (sigpipe) output-error mode does NOT
                    // install SIG_IGN — tee.c:172-173 ignores SIGPIPE only
                    // under -p/--output-error, so in the default mode the
                    // first broken-pipe write kills tee by signal (probed
                    // WSL GNU 9.4: `tee fifo` with a reader that stops
                    // early exits 141). Windows has no signal delivery;
                    // the emulation fails the run right there — silent,
                    // rc 1 (the 1.x C++ tee shape, and the contract the
                    // head_fifo writer-rc pins adjudicate).
                    if epipe_like(&e) && matches!(output_error, OutputError::Sigpipe) {
                        return Ok(1);
                    }
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

/// GNU argmatch over tee.c's output_error_args table (tee.c:69-73, in this
/// order: "warn", "warn-nopipe", "exit", "exit-nopipe"). argmatch.c
/// argmatch(): an exact match wins immediately; otherwise the operand is a
/// prefix of an element when the first `arglen` bytes agree, the first such
/// element is remembered, and a second prefix match over a DIFFERENT value
/// is a real ambiguity (all four enum values here are distinct). Case
/// sensitive (strncmp), and the empty string is a prefix of everything, so
/// `--output-error=` reports ambiguous. Probed WSL coreutils 9.4:
/// `warn-n`/`warn-` -> warn-nopipe rc 0; `warn` exact rc 0; `w`/`ex`/`` ->
/// "ambiguous argument"; `WAR`/`bogus` -> "invalid argument"; rc 1 both.
fn argmatch_output_error(arg: &str) -> Result<usize, bool> {
    const ARGS: [&str; 4] = ["warn", "warn-nopipe", "exit", "exit-nopipe"];
    let arglen = arg.len();
    let mut matchind: isize = -1;
    let mut ambiguous = false;
    for (i, el) in ARGS.iter().enumerate() {
        if el.len() >= arglen && &el.as_bytes()[..arglen] == arg.as_bytes() {
            if el.len() == arglen {
                return Ok(i);
            } else if matchind == -1 {
                matchind = i as isize;
            } else {
                ambiguous = true;
            }
        }
    }
    if ambiguous {
        Err(true)
    } else if matchind == -1 {
        Err(false)
    } else {
        Ok(matchind as usize)
    }
}

/// __xargmatch_internal failure path: argmatch_invalid names the problem,
/// argmatch_valid lists the alternatives one per line (the values are all
/// distinct so no synonyms share a line), and XARGMATCH dies through
/// usage(EXIT_FAILURE) -> emit_try_help. probed byte-shape on WSL GNU
/// coreutils 9.4 (quoting normalized to ASCII like the 1.x applet).
fn output_error_argfail(io: &mut Io, arg: &str, ambiguous: bool) -> CmdResult {
    let verdict = if ambiguous { "ambiguous" } else { "invalid" };
    let _ = writeln!(
        io.stderr,
        "tee: {verdict} argument '{arg}' for '--output-error'"
    );
    let _ = writeln!(io.stderr, "Valid arguments are:");
    for a in ["warn", "warn-nopipe", "exit", "exit-nopipe"] {
        let _ = writeln!(io.stderr, "  - '{a}'");
    }
    let _ = writeln!(io.stderr, "Try 'tee --help' for more information.");
    Ok(1)
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
                    match argmatch_output_error(&v) {
                        Ok(i) => {
                            output_error = match i {
                                0 => OutputError::Warn,
                                1 => OutputError::WarnNopipe,
                                2 => OutputError::Exit,
                                _ => OutputError::ExitNopipe,
                            };
                        }
                        Err(ambiguous) => return output_error_argfail(io, &v, ambiguous),
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
    fn argmatch_abbreviates_output_error() {
        // unambiguous prefixes resolve to the full mode (GNU XARGMATCH);
        // probed WSL coreutils 9.4: `warn-n`/`warn-` -> warn-nopipe, rc 0.
        for v in ["warn-n", "warn-", "warn-nopip"] {
            let sc = ScratchCtx::new("tee-abbrev");
            let (r, out, err) = {
                let mut io = MemIo::new("d\n");
                let ctx = sc.ctx();
                let owned: Vec<OsString> = [format!("--output-error={v}").as_str(), "o1"]
                    .iter()
                    .map(Into::into)
                    .collect();
                let r = tee_cmd(&owned, &mut io.split(), &ctx).unwrap();
                (r, io.out(), io.err())
            };
            assert_eq!(r, 0, "{v} rc");
            assert_eq!(err, "", "{v} stderr");
            assert_eq!(out, "d\n");
            assert_eq!(read_sc(&sc, "o1"), "d\n");
        }
        // exact full names still work
        for v in ["warn", "warn-nopipe", "exit", "exit-nopipe"] {
            let (r, _, err) = run(&[format!("--output-error={v}").as_str(), "-"], "");
            assert_eq!(r, 0, "{v} rc");
            assert_eq!(err, "", "{v} stderr");
        }
    }

    #[test]
    fn argmatch_rejects_ambiguous_and_invalid() {
        // probed WSL coreutils 9.4: `w`/`ex`/`` ambiguous, `WAR`/`bogus`
        // invalid; both print the Valid-arguments list and the Try line.
        let cases = [
            ("w", "ambiguous"),
            ("ex", "ambiguous"),
            ("", "ambiguous"),
            ("WAR", "invalid"),
            ("bogus", "invalid"),
        ];
        for (v, verdict) in cases {
            let (r, _, err) = run(&[format!("--output-error={v}").as_str()], "x\n");
            assert_eq!(r, 1, "{v} rc");
            let first = err.lines().next().unwrap_or_default();
            assert_eq!(
                first,
                format!("tee: {verdict} argument '{v}' for '--output-error'"),
                "{v} first line"
            );
            assert!(err.contains("Valid arguments are:"), "{v} list");
            for name in ["warn", "warn-nopipe", "exit", "exit-nopipe"] {
                assert!(err.contains(&format!("  - '{name}'")), "{v} entry {name}");
            }
            assert!(
                err.contains("Try 'tee --help' for more information."),
                "{v} try"
            );
        }
    }

    #[test]
    fn exit_mode_open_failure_is_fatal() {
        // probed WSL coreutils 9.4 `--output-error=exit no/dir/f good.txt`:
        // the open-loop error exits before stdin is read — empty stdout,
        // the later operand never created, rc 1; exit-nopipe likewise.
        for mode in ["exit", "exit-nopipe"] {
            let sc = ScratchCtx::new("tee-exitmode");
            let (r, out, err) = {
                let mut io = MemIo::new("data\n");
                let ctx = sc.ctx();
                let owned: Vec<OsString> = [
                    format!("--output-error={mode}").as_str(),
                    "no/dir/f",
                    "good.txt",
                ]
                .iter()
                .map(Into::into)
                .collect();
                let r = tee_cmd(&owned, &mut io.split(), &ctx).unwrap();
                (r, io.out(), io.err())
            };
            assert_eq!(r, 1, "{mode} rc");
            assert_eq!(out, "", "{mode} must not copy stdin");
            assert!(
                err.contains("tee: no/dir/f: No such file or directory"),
                "{mode} stderr"
            );
            assert!(
                !sc.path("good.txt").exists(),
                "{mode} later operand must stay unopened"
            );
        }
        // warn (non-exit) keeps the remaining outputs flowing, rc 1
        let sc = ScratchCtx::new("tee-warnmode");
        let (r, out) = {
            let mut io = MemIo::new("data\n");
            let ctx = sc.ctx();
            let owned: Vec<OsString> = ["--output-error=warn", "no/dir/f", "good.txt"]
                .iter()
                .map(Into::into)
                .collect();
            let r = tee_cmd(&owned, &mut io.split(), &ctx).unwrap();
            (r, io.out())
        };
        assert_eq!(r, 1);
        assert_eq!(out, "data\n");
        assert_eq!(read_sc(&sc, "good.txt"), "data\n");
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
