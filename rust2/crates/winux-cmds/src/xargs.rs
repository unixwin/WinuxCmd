//! `xargs` — GNU findutils 4.9.0 `xargs` semantics on the Windows process
//! model (#1488). xargs.c / buildcmd.c are the specification; every rule
//! below was probed against WSL `xargs (GNU findutils) 4.9.0` (probe
//! scripts and raw transcripts: `rust2/target/probes/xargs/p1.sh`..`p8.sh`,
//! C.UTF-8 locale). The 1.x C++ applet (`src/commands/xargs.cpp`) defines
//! the Windows mapping where GNU has no analog.
//!
//! Input parsing (xargs.c read_line / read_string, ported state by state):
//! * Default mode: items are separated by blanks (space/tab) and newlines;
//!   leading whitespace of every item is skipped with `isspace` (so `\f`,
//!   `\v`, `\r` are skipped there but literal inside an item: `a\r b` is
//!   the items `a\r`, `b`). `'...'` and `"..."` quote (no escapes inside;
//!   a newline inside quotes is "unmatched single/double quote; by default
//!   quotes are special to xargs unless you use the -0 option", rc 1, after
//!   running the batch read so far); `\` escapes the next byte, newline
//!   included. An empty quoted item (`''`) is an argument, except at EOF
//!   with nothing else buffered (probed: `printf "''"` yields no item, and
//!   a lone unterminated quote at EOF is silently dropped). A backslash at
//!   EOF is dropped. A NUL byte warns once ("WARNING: a NUL character
//!   occurred in the input. ...") and truncates the argument there.
//! * `-E END` / `-e[END]` / `--eof[=END]`: an item equal to END (after quote
//!   removal) ends the input, except the final item of an unterminated last
//!   line (probed quirk: `printf 'a END'` passes END through). Ignored with
//!   -0/-d, with the GNU warning (whose text carries a trailing newline).
//! * `-L N` / `-l[N]` / `--max-lines[=N]`: logical lines; a line ending in
//!   a blank continues on the next one; blank lines do not count.
//! * `-0` / `--null` and `-d C` / `--delimiter=C` (last of the two wins):
//!   no quoting, no EOF string, empty items between delimiters kept, a
//!   final unterminated item kept. `-d` takes one byte or the escapes
//!   `\a \b \f \n \r \t \v \\`, octal `\NNN` (<= 0377) and hex `\xHH`
//!   (<= 0xff, strtoul-parsed: `\x` alone is NUL), with the GNU fatal
//!   diagnostics for anything else.
//! * `-I R` / `-i[R]` / `--replace[=R]`: one item per line (blanks do not
//!   split, leading whitespace stripped, trailing kept, quotes and `\`
//!   still processed); every occurrence of R in the INITIAL-ARGS (not the
//!   command name: `xargs -I{} {}` runs `{}`) is replaced (buildcmd.c
//!   bc_do_insert, including its "command too long" overflow), and each
//!   item runs one command. -I implies -x.
//! * `-n`, `-L`, `-I` are mutually exclusive: the later one wins with the
//!   GNU warning text (`-n 1` after `-I` is accepted silently).
//! * Input is read lazily through a small buffer and commands run as soon
//!   as a batch is full (`-n` execs at the N-th item, `-L` at the N-th
//!   line), so `yes | xargs -n1 ...` streams with bounded memory.
//!
//! Size limits (buildcmd.c bc_push_arg accounting): every argv entry
//! counts `strlen + 1` bytes. `-s` is clamped to [1, the system maximum]
//! with GNU's non-fatal warnings; an initial argument that does not fit is
//! "cannot fit single argument within argument list size limit", an input
//! item that cannot fit beside the initial arguments is "argument line too
//! long" (after running the pending batch), and with -x (implied by -L/-I)
//! a batch overflow is "argument list too long" instead of a split.
//! Windows: the system maximum is the CreateProcess command-line limit of
//! 32767 UTF-16 units (GNU's ARG_MAX analog; the environment block is
//! separate on Windows) minus GNU's 2048-byte headroom = 30719, which is
//! also the default `-s` (GNU defaults to 128 KiB on Linux). Independently
//! of the GNU byte accounting, each batch is also measured as the quoted
//! Windows command line the launch will hand CreateProcess (8191 for the
//! `cmd.exe` route of `.cmd`/`.bat` children) and split before it would
//! overflow; a single argument that cannot fit is GNU's E2BIG face
//! "xargs: CMD: Argument list too long" (rc 126).
//!
//! Execution (xargs.c xargs_do_exec / wait_for_proc):
//! * The child is resolved execvp-style through the shared
//!   [`crate::childproc`] chain (PATH-only, PATHEXT, `.cmd`/`.bat` through
//!   COMSPEC, `#!` scripts through their interpreter) and spawned with the
//!   caller's environment and cwd, inherited stdout/stderr, and stdin from
//!   the NUL device (GNU: /dev/null) unless `-a FILE` frees the parent's
//!   stdin (inherited; `-a -` still reads stdin) or `-o` opens the console
//!   (`CONIN$`, the /dev/tty analog). `io.stdout`/`io.stderr` are flushed
//!   before every spawn.
//! * No command: GNU runs `echo`. Here the library `echo` runs in-process
//!   through `io.stdout` (the 1.x C++ applet's model, byte-identical to
//!   coreutils /bin/echo; no dependency on an echo image being on PATH). A
//!   broken output pipe maps to GNU's SIGPIPE death of the echo child:
//!   "xargs: echo: terminated by signal 13", rc 125.
//! * `-t` prints the command line to stderr in GNU's shell-escape quoting
//!   (gnulib quotearg shell_escape style, see [`shell_quote`]); `-p` prints
//!   it followed by `?...` and reads the answer from the console (GNU:
//!   /dev/tty; a leading `y`/`Y` runs). A declined command does not count
//!   as executed, so `-r`-less runs still execute once at EOF (probed).
//! * `-P N` keeps up to N children running (0 = unlimited);
//!   `--process-slot-var=VAR` sets VAR to the lowest free slot number in
//!   each child. GNU's SIGUSR1/SIGUSR2 resizing of -P has no Windows
//!   signal to listen to and is not implemented.
//! * Exit status: 0; 123 when any child exited 1-125 (or any other
//!   non-255 failure code); 124 when a child exited 255 ("xargs: CMD:
//!   exited with status 255; aborting"); 125 when a child was "terminated
//!   by signal N" — Windows has no signals, so NTSTATUS-shaped exit codes
//!   (high bit set: access violation, Ctrl-C, fast-fail abort, ...) map
//!   onto the POSIX signal of the same class (SEGV 11, INT 2, ABRT 6, ILL
//!   4, FPE 8, TRAP 5; anything else KILL 9); 126 when the command cannot
//!   run ("Permission denied" for a directory, "Exec format error" for a
//!   file no launch format accepts — glibc would retry it through /bin/sh,
//!   see childproc.rs), 127 when it is not found; 1 for xargs' own
//!   errors. Like GNU's atexit(wait_for_proc_all), every exit waits for
//!   the children still running.
//!
//! Documented gaps: `--show-limits` cannot tell whether stdin is a
//! terminal (the library never touches process stdio), so GNU's "Execution
//! of xargs will continue now..." notice is not printed; `-p`/`-o` open the
//! console unconditionally like GNU opens /dev/tty (an unattended console
//! blocks the same way an unattended pty does under GNU); `-o` without a
//! console prints GNU's child diagnostic and counts as a failed child
//! (GNU 4.9 additionally trips an assertion in the forked child and
//! reports signal 6); input bytes that are not UTF-8 reach the child
//! through lossy conversion (CreateProcessW takes UTF-16).

use std::ffi::OsString;
use std::io::{Read, Write};
use std::os::windows::process::CommandExt as _;
use std::process::{Child, Stdio};

use crate::argopt;
use crate::childproc::{
    build_child_command, quote_windows_arg, resolve_child, ChildLaunch, ChildResolve,
};
use crate::fsutil;
use winux_core::{CmdResult, Ctx, Io};

/// CreateProcess lpCommandLine limit, terminating NUL included.
const WIN_CMDLINE_MAX: usize = 32767;
/// cmd.exe's own command-string limit (the `.cmd`/`.bat` route).
const CMD_EXE_MAX: usize = 8191;
/// GNU bc_init_controlinfo's headroom below the system maximum.
const ARG_HEADROOM: usize = 2048;
/// POSIX upper limit on argument length "for this system".
const POSIX_ARG_SIZE_MAX: usize = WIN_CMDLINE_MAX - ARG_HEADROOM;
/// GNU's "sensible" default command buffer (bc_use_sensible_arg_max).
const SENSIBLE_ARG_MAX: usize = 128 * 1024;
/// xargs.c MAX_PROC_MAX.
const MAX_PROC_MAX: i64 = i32::MAX as i64;

const HELP_TEXT: &str = r#"Usage: xargs [OPTION]... COMMAND [INITIAL-ARGS]...
Run COMMAND with arguments INITIAL-ARGS and more arguments read from input.

Mandatory and optional arguments to long options are also
mandatory or optional for the corresponding short option.
  -0, --null                   items are separated by a null, not whitespace;
                                 disables quote and backslash processing and
                                 logical EOF processing
  -a, --arg-file=FILE          read arguments from FILE, not standard input
  -d, --delimiter=CHARACTER    items in input stream are separated by CHARACTER,
                                 not by whitespace; disables quote and backslash
                                 processing and logical EOF processing
  -E END                       set logical EOF string; if END occurs as a line
                                 of input, the rest of the input is ignored
                                 (ignored if -0 or -d was specified)
  -e, --eof[=END]              equivalent to -E END if END is specified;
                                 otherwise, there is no end-of-file string
  -I R                         same as --replace=R
  -i, --replace[=R]            replace R in INITIAL-ARGS with names read
                                 from standard input, split at newlines;
                                 if R is unspecified, assume {}
  -L, --max-lines=MAX-LINES    use at most MAX-LINES non-blank input lines per
                                 command line
  -l[MAX-LINES]                similar to -L but defaults to at most one non-
                                 blank input line if MAX-LINES is not specified
  -n, --max-args=MAX-ARGS      use at most MAX-ARGS arguments per command line
  -o, --open-tty               Reopen stdin as /dev/tty in the child process
                                 before executing the command; useful to run an
                                 interactive application.
  -P, --max-procs=MAX-PROCS    run at most MAX-PROCS processes at a time
  -p, --interactive            prompt before running commands
      --process-slot-var=VAR   set environment variable VAR in child processes
  -r, --no-run-if-empty        if there are no arguments, then do not run COMMAND;
                                 if this option is not given, COMMAND will be
                                 run at least once
  -s, --max-chars=MAX-CHARS    limit length of command line to MAX-CHARS
      --show-limits            show limits on command-line length
  -t, --verbose                print commands before executing them
  -x, --exit                   exit if the size (see -s) is exceeded
      --help                   display this help and exit
      --version                output version information and exit

Please see also the documentation at https://www.gnu.org/software/findutils/.
You can report (and track progress on fixing) bugs in the "xargs"
program via the GNU findutils bug-reporting page at
https://savannah.gnu.org/bugs/?group=findutils or, if
you have no web access, by sending email to <bug-findutils@gnu.org>.
"#;

const VERSION_TEXT: &str = "xargs (GNU findutils) 4.9.0\n\
Copyright (C) 2022 Free Software Foundation, Inc.\n\
License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>.\n\
This is free software: you are free to change and redistribute it.\n\
There is NO WARRANTY, to the extent permitted by law.\n\
\n\
Written by Eric B. Decker, James Youngman, and Kevin Dalley.\n";

const TRY_HELP: &str = "Try 'xargs --help' for more information.";

// ---------------------------------------------------------------------------
// option parsing (getopt_long with optstring "+0a:E:e::i::I:l::L:n:oprs:txP:d:")

#[derive(Clone, Copy, PartialEq, Debug)]
enum HasArg {
    No,
    Required,
    Optional,
}

/// Internal option codes for the long-only options.
const OPT_SHOW_LIMITS: char = 'S';
const OPT_VERSION: char = 'v';
const OPT_HELP: char = 'h';
const OPT_SLOT_VAR: char = '\u{1}';

/// xargs.c longopts[], in table order (glibc lists ambiguity candidates
/// in this order: probed `--m` -> max-lines max-args max-chars max-procs).
const LONGOPTS: &[(&str, HasArg, char)] = &[
    ("null", HasArg::No, '0'),
    ("arg-file", HasArg::Required, 'a'),
    ("delimiter", HasArg::Required, 'd'),
    ("eof", HasArg::Optional, 'e'),
    ("replace", HasArg::Optional, 'i'),
    ("max-lines", HasArg::Optional, 'l'),
    ("max-args", HasArg::Required, 'n'),
    ("open-tty", HasArg::No, 'o'),
    ("interactive", HasArg::No, 'p'),
    ("no-run-if-empty", HasArg::No, 'r'),
    ("max-chars", HasArg::Required, 's'),
    ("verbose", HasArg::No, 't'),
    ("show-limits", HasArg::No, OPT_SHOW_LIMITS),
    ("exit", HasArg::No, 'x'),
    ("max-procs", HasArg::Required, 'P'),
    ("process-slot-var", HasArg::Required, OPT_SLOT_VAR),
    ("version", HasArg::No, OPT_VERSION),
    ("help", HasArg::No, OPT_HELP),
];

fn short_has_arg(c: char) -> Option<HasArg> {
    Some(match c {
        '0' | 'o' | 'p' | 'r' | 't' | 'x' => HasArg::No,
        'a' | 'E' | 'I' | 'L' | 'n' | 's' | 'P' | 'd' => HasArg::Required,
        'e' | 'i' | 'l' => HasArg::Optional,
        _ => return None,
    })
}

/// The option set after the sequential getopt loop (xargs.c main).
#[derive(Debug)]
struct Opts {
    /// `Some` selects read_string (-0/-d) with this delimiter byte.
    delimiter: Option<u8>,
    arg_file: Option<String>,
    eof_str: Option<Vec<u8>>,
    replace: Option<Vec<u8>>,
    lines_per_exec: u64,
    args_per_exec: u64,
    arg_max: usize,
    exit_if_size_exceeded: bool,
    query: bool,
    print_command: bool,
    always_run: bool,
    max_procs: u64,
    show_limits: bool,
    open_tty: bool,
    slot_var: Option<String>,
    /// Command and INITIAL-ARGS (empty: the default `echo`).
    command: Vec<Vec<u8>>,
}

impl Default for Opts {
    fn default() -> Self {
        Opts {
            delimiter: None,
            arg_file: None,
            eof_str: None,
            replace: None,
            lines_per_exec: 0,
            args_per_exec: 0,
            arg_max: SENSIBLE_ARG_MAX.min(POSIX_ARG_SIZE_MAX),
            exit_if_size_exceeded: false,
            query: false,
            print_command: false,
            always_run: true,
            max_procs: 1,
            show_limits: false,
            open_tty: false,
            slot_var: None,
            command: Vec::new(),
        }
    }
}

fn is_c_space(b: u8) -> bool {
    matches!(b, b' ' | b'\t' | b'\n' | 0x0b | 0x0c | b'\r')
}

fn is_blank(b: u8) -> bool {
    b == b' ' || b == b'\t'
}

/// strtol(str, &end, 10) shape: leading isspace, optional sign, digits;
/// overflow saturates (errno is not consulted by xargs' parse_num).
/// `None` when no digits were consumed or trailing bytes remain.
fn strtol10(s: &str) -> Option<i64> {
    let b = s.as_bytes();
    let mut i = 0usize;
    while i < b.len() && is_c_space(b[i]) {
        i += 1;
    }
    let mut neg = false;
    if i < b.len() && (b[i] == b'+' || b[i] == b'-') {
        neg = b[i] == b'-';
        i += 1;
    }
    let start = i;
    let mut val: i64 = 0;
    let mut overflow = false;
    while i < b.len() && b[i].is_ascii_digit() {
        let d = (b[i] - b'0') as i64;
        match val.checked_mul(10).and_then(|v| v.checked_add(d)) {
            Some(v) => val = v,
            None => overflow = true,
        }
        i += 1;
    }
    if i == start || i != b.len() {
        return None;
    }
    Some(match (overflow, neg) {
        (true, false) => i64::MAX,
        (true, true) => i64::MIN,
        (false, true) => -val,
        (false, false) => val,
    })
}

/// xargs.c parse_num: a fatal range error and an invalid number exit 1
/// through usage(); a non-fatal range error clamps after the message.
fn parse_num(
    err: &mut dyn Write,
    text: &str,
    option: char,
    min: i64,
    max: Option<i64>,
    fatal: bool,
) -> Result<i64, u32> {
    let Some(val) = strtol10(text) else {
        let _ = writeln!(err, "xargs: invalid number \"{text}\" for -{option} option");
        let _ = writeln!(err, "{TRY_HELP}");
        return Err(1);
    };
    if val < min {
        let _ = writeln!(
            err,
            "xargs: value {text} for -{option} option should be >= {min}"
        );
        if fatal {
            let _ = writeln!(err, "{TRY_HELP}");
            return Err(1);
        }
        return Ok(min);
    }
    if let Some(max) = max {
        if val > max {
            let _ = writeln!(
                err,
                "xargs: value {text} for -{option} option should be <= {max}"
            );
            if fatal {
                let _ = writeln!(err, "{TRY_HELP}");
                return Err(1);
            }
            return Ok(max);
        }
    }
    Ok(val)
}

/// strtoul(p, &end, base) for the -d escapes: (value, consumed bytes,
/// overflowed). Leading whitespace, a sign and (base 16) a `0x` prefix
/// are accepted like glibc; no digits consumes nothing.
fn strtoul(p: &[u8], base: u32) -> (u64, usize, bool) {
    let mut i = 0usize;
    while i < p.len() && is_c_space(p[i]) {
        i += 1;
    }
    let mut neg = false;
    if i < p.len() && (p[i] == b'+' || p[i] == b'-') {
        neg = p[i] == b'-';
        i += 1;
    }
    if base == 16
        && i + 2 < p.len() + 1
        && i + 1 < p.len()
        && p[i] == b'0'
        && (p[i + 1] | 0x20) == b'x'
        && p.get(i + 2).is_some_and(|c| c.is_ascii_hexdigit())
    {
        i += 2;
    }
    let start = i;
    let mut val: u64 = 0;
    let mut overflow = false;
    while i < p.len() {
        let Some(d) = (p[i] as char).to_digit(base) else {
            break;
        };
        match val
            .checked_mul(base as u64)
            .and_then(|v| v.checked_add(d as u64))
        {
            Some(v) => val = v,
            None => overflow = true,
        }
        i += 1;
    }
    if i == start {
        return (0, 0, false);
    }
    if overflow {
        return (u64::MAX, i, true);
    }
    (if neg { val.wrapping_neg() } else { val }, i, false)
}

/// xargs.c get_input_delimiter / get_char_oct_or_hex_escape.
fn input_delimiter(err: &mut dyn Write, s: &str) -> Result<u8, u32> {
    let b = s.as_bytes();
    if b.len() == 1 {
        return Ok(b[0]);
    }
    if b.first() != Some(&b'\\') {
        let _ = writeln!(
            err,
            "xargs: Invalid input delimiter specification {s}: the delimiter must be either a single character or an escape sequence starting with \\."
        );
        return Err(1);
    }
    match b.get(1) {
        Some(b'a') => return Ok(0x07),
        Some(b'b') => return Ok(0x08),
        Some(b'f') => return Ok(0x0c),
        Some(b'n') => return Ok(b'\n'),
        Some(b'r') => return Ok(b'\r'),
        Some(b't') => return Ok(b'\t'),
        Some(b'v') => return Ok(0x0b),
        Some(b'\\') => return Ok(b'\\'),
        _ => {}
    }
    let (base, rest) = match b.get(1) {
        Some(b'x') => (16u32, &b[2..]),
        Some(c) if c.is_ascii_digit() => (8u32, &b[1..]),
        _ => {
            let _ = writeln!(
                err,
                "xargs: Invalid escape sequence {s} in input delimiter specification."
            );
            return Err(1);
        }
    };
    let (val, used, overflow) = strtoul(rest, base);
    if overflow || val > 0xff {
        let max = if base == 16 { "ff" } else { "377" };
        let _ = writeln!(
            err,
            "xargs: Invalid escape sequence {s} in input delimiter specification; character values must not exceed {max}."
        );
        return Err(1);
    }
    if used < rest.len() {
        let trailing = String::from_utf8_lossy(&rest[used..]);
        let _ = writeln!(
            err,
            "xargs: Invalid escape sequence {s} in input delimiter specification; trailing characters {trailing} not recognised."
        );
        return Err(1);
    }
    Ok(val as u8)
}

/// Which batching family an option belongs to (the -n/-L/-I exclusion).
fn warn_exclusive(err: &mut dyn Write, option: &str, offending: &str) {
    let _ = writeln!(
        err,
        "xargs: warning: options {offending} and {option} are mutually exclusive, ignoring previous {offending} value"
    );
}

enum Parsed {
    Run(Opts),
    Exit(u32),
}

/// Applies one getopt result in argv order, like xargs.c's switch.
fn apply_option(
    o: &mut Opts,
    code: char,
    value: Option<&str>,
    out: &mut dyn Write,
    err: &mut dyn Write,
) -> Result<Option<u32>, u32> {
    match code {
        '0' => o.delimiter = Some(0),
        'd' => o.delimiter = Some(input_delimiter(err, value.unwrap_or(""))?),
        'E' => o.eof_str = Some(value.unwrap_or("").as_bytes().to_vec()),
        'e' => o.eof_str = value.map(|v| v.as_bytes().to_vec()),
        'I' | 'i' => {
            let pat = match (code, value) {
                ('I', v) => v.unwrap_or("").to_string(),
                (_, Some(v)) => v.to_string(),
                (_, None) => "{}".to_string(),
            };
            if o.args_per_exec != 0 {
                warn_exclusive(err, "--replace/-I/-i", "--max-args");
                o.args_per_exec = 0;
            }
            if o.lines_per_exec != 0 {
                warn_exclusive(err, "--replace/-I/-i", "--max-lines");
                o.lines_per_exec = 0;
            }
            o.replace = Some(pat.into_bytes());
        }
        'L' | 'l' => {
            let lines = match value {
                Some(v) => parse_num(err, v, code, 1, None, true)? as u64,
                None => 1,
            };
            let name = if code == 'L' { "-L" } else { "--max-lines/-l" };
            if o.args_per_exec != 0 {
                warn_exclusive(err, name, "--max-args");
                o.args_per_exec = 0;
            }
            if o.replace.is_some() {
                warn_exclusive(err, name, "--replace");
                o.replace = None;
            }
            o.lines_per_exec = lines;
        }
        'n' => {
            let n = parse_num(err, value.unwrap_or(""), 'n', 1, None, true)? as u64;
            if o.lines_per_exec != 0 {
                warn_exclusive(err, "--max-args/-n", "--max-lines");
                o.lines_per_exec = 0;
            }
            // POSIX: -n 1 is compatible with -I (probed: no warning).
            if o.replace.is_some() {
                if n == 1 {
                    o.args_per_exec = 0;
                    return Ok(None);
                }
                warn_exclusive(err, "--max-args/-n", "--replace");
                o.replace = None;
            }
            o.args_per_exec = n;
        }
        's' => {
            let v = parse_num(
                err,
                value.unwrap_or(""),
                's',
                1,
                Some(POSIX_ARG_SIZE_MAX as i64),
                false,
            )?;
            o.arg_max = v as usize;
        }
        'P' => {
            o.max_procs =
                parse_num(err, value.unwrap_or(""), 'P', 0, Some(MAX_PROC_MAX), true)? as u64;
        }
        'a' => o.arg_file = Some(value.unwrap_or("").to_string()),
        'p' => {
            o.query = true;
            o.print_command = true;
        }
        't' => o.print_command = true,
        'r' => o.always_run = false,
        'x' => o.exit_if_size_exceeded = true,
        'o' => o.open_tty = true,
        OPT_SHOW_LIMITS => o.show_limits = true,
        OPT_SLOT_VAR => {
            let v = value.unwrap_or("");
            if v.contains('=') {
                let _ = writeln!(
                    err,
                    "xargs: option --process-slot-var may not be set to a value which includes `='"
                );
                return Err(1);
            }
            o.slot_var = Some(v.to_string());
        }
        OPT_VERSION => {
            let _ = out.write_all(VERSION_TEXT.as_bytes());
            return Ok(Some(0));
        }
        OPT_HELP => {
            let _ = out.write_all(HELP_TEXT.as_bytes());
            return Ok(Some(0));
        }
        _ => {}
    }
    Ok(None)
}

fn usage_fail(ctx: &Ctx, err: &mut dyn Write, msg: &str) -> u32 {
    let _ = writeln!(err, "xargs: {msg}");
    let _ = writeln!(err, "{}", winux_core::try_help(ctx, "xargs"));
    1
}

/// getopt_long("+...") over argv: option processing stops at the first
/// non-option (`xargs echo -n` hands -n to echo) or after `--`.
fn parse_args(ctx: &Ctx, args: &[OsString], out: &mut dyn Write, err: &mut dyn Write) -> Parsed {
    let argv: Vec<String> = args
        .iter()
        .map(|a| a.to_string_lossy().into_owned())
        .collect();
    let mut o = Opts::default();
    let mut i = 0usize;
    while i < argv.len() {
        let a = argv[i].as_str();
        if a == "--" {
            i += 1;
            break;
        }
        if !a.starts_with('-') || a == "-" {
            break;
        }
        if let Some(body) = a.strip_prefix("--") {
            let (name, inline) = match body.split_once('=') {
                Some((n, v)) => (n, Some(v)),
                None => (body, None),
            };
            let found = match LONGOPTS.iter().find(|(n, _, _)| *n == name) {
                Some(e) => *e,
                None => {
                    let cands: Vec<&(&str, HasArg, char)> = LONGOPTS
                        .iter()
                        .filter(|(n, _, _)| n.starts_with(name))
                        .collect();
                    match cands.len() {
                        0 => {
                            return Parsed::Exit(usage_fail(
                                ctx,
                                err,
                                &argopt::unrecognized_option(ctx, &format!("--{body}")),
                            ))
                        }
                        1 => *cands[0],
                        _ => {
                            let list: Vec<String> =
                                cands.iter().map(|(n, _, _)| format!("'--{n}'")).collect();
                            return Parsed::Exit(usage_fail(
                                ctx,
                                err,
                                &argopt::ambiguous_option(
                                    ctx,
                                    &format!("--{body}"),
                                    &list.join(" "),
                                ),
                            ));
                        }
                    }
                }
            };
            let (lname, has_arg, code) = found;
            let value: Option<String> = match has_arg {
                HasArg::No => {
                    if inline.is_some() {
                        return Parsed::Exit(usage_fail(
                            ctx,
                            err,
                            &argopt::option_no_argument(ctx, &format!("--{lname}")),
                        ));
                    }
                    None
                }
                HasArg::Optional => inline.map(|v| v.to_string()),
                HasArg::Required => match inline {
                    Some(v) => Some(v.to_string()),
                    None => {
                        i += 1;
                        match argv.get(i) {
                            Some(v) => Some(v.clone()),
                            None => {
                                return Parsed::Exit(usage_fail(
                                    ctx,
                                    err,
                                    &argopt::option_requires_argument(ctx, &format!("--{lname}")),
                                ))
                            }
                        }
                    }
                },
            };
            match apply_option(&mut o, code, value.as_deref(), out, err) {
                Ok(Some(s)) => return Parsed::Exit(s),
                Ok(None) => {}
                Err(s) => return Parsed::Exit(s),
            }
            i += 1;
            continue;
        }
        // short cluster
        let chars: Vec<char> = a[1..].chars().collect();
        let mut ci = 0usize;
        while ci < chars.len() {
            let c = chars[ci];
            let Some(has_arg) = short_has_arg(c) else {
                return Parsed::Exit(usage_fail(ctx, err, &format!("invalid option -- '{c}'")));
            };
            let rest: String = chars[ci + 1..].iter().collect();
            let (value, done): (Option<String>, bool) = match has_arg {
                HasArg::No => (None, false),
                HasArg::Optional => (if rest.is_empty() { None } else { Some(rest) }, true),
                HasArg::Required => {
                    if !rest.is_empty() {
                        (Some(rest), true)
                    } else {
                        i += 1;
                        match argv.get(i) {
                            Some(v) => (Some(v.clone()), true),
                            None => {
                                return Parsed::Exit(usage_fail(
                                    ctx,
                                    err,
                                    &argopt::short_option_requires_argument(ctx, &c.to_string()),
                                ))
                            }
                        }
                    }
                }
            };
            match apply_option(&mut o, c, value.as_deref(), out, err) {
                Ok(Some(s)) => return Parsed::Exit(s),
                Ok(None) => {}
                Err(s) => return Parsed::Exit(s),
            }
            if done {
                break;
            }
            ci += 1;
        }
        i += 1;
    }
    o.command = argv[i.min(argv.len())..]
        .iter()
        .map(|s| s.as_bytes().to_vec())
        .collect();
    Parsed::Run(o)
}

// ---------------------------------------------------------------------------
// -t / -p quoting

/// gnulib quotearg `shell_escape_quoting_style`, the style xargs' print_args
/// uses (probed byte-for-byte, p3.sh/p4.sh): bare when nothing is special;
/// otherwise single-quoted with `'\''` for quotes and `'$'\ooo''` segments
/// for unprintables (consecutive escapes share one `$'...'`); an argument
/// whose only specials are single quotes and spaces (every byte in
/// `[A-Za-z0-9 %+,./:@\]_'-]` or printable multibyte) is double-quoted.
/// `#` and `~` are special only in first position, `{`/`}` only as the
/// whole argument, `=` everywhere.
pub fn shell_quote(arg: &[u8]) -> Vec<u8> {
    if arg.is_empty() {
        return b"''".to_vec();
    }
    // Decode into units: Ok(char, bytes) for valid UTF-8, Err(byte) for
    // an invalid/incomplete sequence (escaped byte-wise like mbrtowc
    // failures).
    let mut units: Vec<Result<(char, &[u8]), u8>> = Vec::new();
    let mut rest = arg;
    while !rest.is_empty() {
        match std::str::from_utf8(rest) {
            Ok(s) => {
                let mut off = 0usize;
                for ch in s.chars() {
                    let l = ch.len_utf8();
                    units.push(Ok((ch, &rest[off..off + l])));
                    off += l;
                }
                rest = &[];
            }
            Err(e) => {
                let good = e.valid_up_to();
                let s = std::str::from_utf8(&rest[..good]).unwrap_or("");
                let mut off = 0usize;
                for ch in s.chars() {
                    let l = ch.len_utf8();
                    units.push(Ok((ch, &rest[off..off + l])));
                    off += l;
                }
                let bad = e.error_len().unwrap_or(rest.len() - good).max(1);
                for &b in &rest[good..good + bad] {
                    units.push(Err(b));
                }
                rest = &rest[good + bad..];
            }
        }
    }
    let single = units.len() == 1;
    let printable = |ch: char| !ch.is_control();
    let mut needs = false;
    let mut all_compat = true;
    let mut has_sq = false;
    for (i, u) in units.iter().enumerate() {
        let (special, compat) = match u {
            Err(_) => (true, false),
            Ok((ch, _)) => match *ch {
                '\'' => {
                    has_sq = true;
                    (true, true)
                }
                ' ' => (true, true),
                '{' | '}' => (single, false),
                '#' | '~' => (i == 0, false),
                '!' | '"' | '$' | '&' | '(' | ')' | '*' | ';' | '<' | '=' | '>' | '[' | '^'
                | '`' | '|' | '?' | '\\' => (true, false),
                c if c.is_ascii_alphanumeric() => (false, true),
                '%' | '+' | ',' | '-' | '.' | '/' | ':' | ']' | '_' => (false, true),
                c if printable(c) => (false, true),
                _ => (true, false),
            },
        };
        needs |= special;
        all_compat &= compat;
    }
    if !needs {
        return arg.to_vec();
    }
    if has_sq && all_compat {
        let mut out = Vec::with_capacity(arg.len() + 2);
        out.push(b'"');
        out.extend_from_slice(arg);
        out.push(b'"');
        return out;
    }
    let mut out = Vec::with_capacity(arg.len() + 8);
    out.push(b'\'');
    let mut pending = false;
    let escape_byte = |out: &mut Vec<u8>, pending: &mut bool, b: u8| {
        if !*pending {
            out.extend_from_slice(b"'$'");
            *pending = true;
        }
        out.push(b'\\');
        match b {
            0x07 => out.push(b'a'),
            0x08 => out.push(b'b'),
            0x0c => out.push(b'f'),
            b'\n' => out.push(b'n'),
            b'\r' => out.push(b'r'),
            b'\t' => out.push(b't'),
            0x0b => out.push(b'v'),
            _ => {
                out.push(b'0' + ((b >> 6) & 7));
                out.push(b'0' + ((b >> 3) & 7));
                out.push(b'0' + (b & 7));
            }
        }
    };
    for u in &units {
        match u {
            Err(b) => escape_byte(&mut out, &mut pending, *b),
            Ok((ch, bytes)) => {
                if *ch == '\'' {
                    out.extend_from_slice(b"'\\''");
                    pending = false;
                } else if ch.is_control() {
                    for &b in bytes.iter() {
                        escape_byte(&mut out, &mut pending, b);
                    }
                } else {
                    if pending {
                        out.extend_from_slice(b"''");
                        pending = false;
                    }
                    out.extend_from_slice(bytes);
                }
            }
        }
    }
    out.push(b'\'');
    out
}

// ---------------------------------------------------------------------------
// console (the /dev/tty analog)

/// The terminal face of -p and -o. The command uses [`ConsoleTty`]
/// (`CONIN$`, the Windows /dev/tty); unit tests inject a scripted one.
pub(crate) trait Tty {
    /// Opens the answer source; `Err` carries the strerror text.
    fn open_answer(&mut self) -> Result<(), &'static str>;
    /// Reads one answer line (without the newline); `None` at EOF.
    fn read_answer(&mut self) -> Option<Vec<u8>>;
    /// A stdin handle for a -o child.
    fn child_stdin(&mut self) -> Result<Stdio, &'static str>;
}

#[derive(Default)]
struct ConsoleTty {
    conin: Option<std::fs::File>,
}

fn open_conin() -> Result<std::fs::File, &'static str> {
    std::fs::OpenOptions::new()
        .read(true)
        .write(true)
        .open("CONIN$")
        .map_err(|_| "No such device or address")
}

impl Tty for ConsoleTty {
    fn open_answer(&mut self) -> Result<(), &'static str> {
        if self.conin.is_none() {
            self.conin = Some(open_conin()?);
        }
        Ok(())
    }

    fn read_answer(&mut self) -> Option<Vec<u8>> {
        let f = self.conin.as_mut()?;
        let mut line = Vec::new();
        let mut b = [0u8; 1];
        loop {
            match f.read(&mut b) {
                Ok(0) => return None,
                Ok(_) => {
                    if b[0] == b'\n' {
                        break;
                    }
                    // a console ^Z / ^D at line start is the EOF keystroke
                    if line.is_empty() && (b[0] == 0x1a || b[0] == 0x04) {
                        return None;
                    }
                    line.push(b[0]);
                }
                Err(e) if e.kind() == std::io::ErrorKind::Interrupted => continue,
                Err(_) => return None,
            }
        }
        if line.last() == Some(&b'\r') {
            line.pop();
        }
        Some(line)
    }

    fn child_stdin(&mut self) -> Result<Stdio, &'static str> {
        open_conin().map(Stdio::from)
    }
}

// ---------------------------------------------------------------------------
// lazy input

struct Input<'r> {
    src: &'r mut dyn Read,
    buf: Vec<u8>,
    pos: usize,
    len: usize,
    done: bool,
}

impl<'r> Input<'r> {
    fn new(src: &'r mut dyn Read) -> Self {
        Input {
            src,
            buf: vec![0u8; 16 * 1024],
            pos: 0,
            len: 0,
            done: false,
        }
    }

    /// getc(3): one byte, `None` at end of input (a read error ends the
    /// input like GNU's unchecked getc loop).
    fn getc(&mut self) -> Option<u8> {
        if self.pos < self.len {
            let b = self.buf[self.pos];
            self.pos += 1;
            return Some(b);
        }
        if self.done {
            return None;
        }
        loop {
            match self.src.read(&mut self.buf) {
                Ok(0) => {
                    self.done = true;
                    return None;
                }
                Ok(n) => {
                    self.len = n.min(self.buf.len());
                    self.pos = 1;
                    return Some(self.buf[0]);
                }
                Err(e) if e.kind() == std::io::ErrorKind::Interrupted => continue,
                Err(_) => {
                    self.done = true;
                    return None;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// command-line building and execution

/// Terminates the run with a status (after GNU's atexit wait).
struct Stop(u32);

#[derive(Clone, Copy, PartialEq)]
enum ChildStdin {
    Null,
    Inherit,
    Console,
}

struct Running {
    child: Child,
    name: Vec<u8>,
    slot: Option<usize>,
}

/// Upper bound of the UTF-16 units std's Windows command-line builder
/// spends on one argument (quoted when empty or holding blanks/quotes,
/// backslashes before a quote doubled).
fn win_quoted_len(s: &str) -> usize {
    let units = s.encode_utf16().count();
    if !s.is_empty() && !s.contains([' ', '\t', '\n', '"']) {
        return units;
    }
    let mut extra = 0usize;
    let mut bs = 0usize;
    for c in s.chars() {
        match c {
            '\\' => bs += 1,
            '"' => {
                extra += bs + 1;
                bs = 0;
            }
            _ => bs = 0,
        }
    }
    units + 2 + extra + bs
}

fn lossy_arg(arg: &[u8]) -> String {
    let end = arg.iter().position(|&b| b == 0).unwrap_or(arg.len());
    String::from_utf8_lossy(&arg[..end]).into_owned()
}

/// The resolved image is cmd.exe under any spelling (full paths
/// included): its leaf is `cmd` or `cmd.exe`, case-insensitive. This is
/// the 1.x body's `is_cmd_shell` check widened to the resolved path.
fn is_cmd_image(path: &str) -> bool {
    let leaf = path.rsplit(['\\', '/']).next().unwrap_or("");
    leaf.eq_ignore_ascii_case("cmd") || leaf.eq_ignore_ascii_case("cmd.exe")
}

/// cmd's own command-string escaping after /c (the 1.x body's
/// build_command_line): every space, tab, `^ & | < > ( )` and `"` gains
/// a `^` prefix so cmd passes the byte through literally.
fn cmd_escape_arg(arg: &str) -> String {
    let mut out = String::with_capacity(arg.len());
    for c in arg.chars() {
        if matches!(
            c,
            ' ' | '\t' | '^' | '&' | '|' | '<' | '>' | '(' | ')' | '"'
        ) {
            out.push('^');
        }
        out.push(c);
    }
    out
}

/// UTF-16 units the caret-escaped argument occupies on the command line.
fn caret_escaped_len(arg: &str) -> usize {
    arg.encode_utf16().count()
        + arg
            .chars()
            .filter(|c| {
                matches!(
                    c,
                    ' ' | '\t' | '^' | '&' | '|' | '<' | '>' | '(' | ')' | '"'
                )
            })
            .count()
}

fn is_batch(path: &str) -> bool {
    let l = path.to_ascii_lowercase();
    l.ends_with(".cmd") || l.ends_with(".bat")
}

/// (base cost, limit) of the Windows command line for a resolved launch.
fn launch_budget(launch: &ChildLaunch, ctx: &Ctx) -> (usize, usize) {
    match launch {
        ChildLaunch::Image(p) => {
            // cmd.exe parses its own command string under the 8191 cap
            let limit = if is_cmd_image(p) {
                CMD_EXE_MAX - 1
            } else {
                WIN_CMDLINE_MAX - 1
            };
            (win_quoted_len(p) + 2, limit)
        }
        ChildLaunch::Batch(p) => (win_quoted_len(p) + 2, CMD_EXE_MAX - 1),
        ChildLaunch::Script { argv } => {
            let head = argv.first().map(String::as_str).unwrap_or("");
            let mut cost = win_quoted_len(head) + 2;
            for a in argv.iter().skip(1) {
                cost += 1 + win_quoted_len(a);
            }
            let limit = if is_batch(head) {
                CMD_EXE_MAX - 1
            } else {
                WIN_CMDLINE_MAX - 1
            };
            let _ = ctx;
            (cost, limit)
        }
    }
}

struct Runner<'a, 'c, 'e> {
    out: &'a mut dyn Write,
    err: &'a mut dyn Write,
    ctx: &'c Ctx<'e>,
    tty: &'a mut dyn Tty,
    o: Opts,
    /// The default command (no COMMAND operand): library echo in-process.
    in_process_echo: bool,
    resolved: Option<ChildResolve>,
    win_base: usize,
    win_limit: usize,
    child_stdin: ChildStdin,
    // buildcmd state
    argv: Vec<Vec<u8>>,
    argv_chars: usize,
    win_chars: usize,
    /// The resolved image is cmd(.exe): arguments after the first /c or
    /// /C are caret-escaped for cmd's own parser, not Win32-quoted.
    cmd_image: bool,
    cmd_tail: bool,
    initial_argc: usize,
    initial_chars: usize,
    initial_win: usize,
    initial_cmd_tail: bool,
    // read_line state
    eof: bool,
    lineno: u64,
    null_warned: bool,
    linebuf: Vec<u8>,
    // processes
    running: Vec<Running>,
    slots: Vec<bool>,
    procs_executed: u64,
    child_error: u32,
    tty_open: bool,
}

impl<'a, 'c, 'e> Runner<'a, 'c, 'e> {
    fn error(&mut self, msg: &str) {
        let _ = self.out.flush();
        let _ = writeln!(self.err, "xargs: {msg}");
    }

    /// A fatal exit: GNU's atexit(wait_for_proc_all) reaps every child
    /// still running before the process status is final.
    fn fatal(&mut self, status: u32) -> Stop {
        for r in self.running.iter_mut() {
            let _ = r.child.wait();
        }
        self.running.clear();
        Stop(status)
    }

    fn eof_match(&self, item: &[u8]) -> bool {
        match &self.o.eof_str {
            Some(e) => {
                let end = item.iter().position(|&b| b == 0).unwrap_or(item.len());
                e.as_slice() == &item[..end]
            }
            None => false,
        }
    }

    fn arg_win_cost(&self, arg: &[u8]) -> usize {
        if self.in_process_echo || self.argv.is_empty() {
            return 0;
        }
        let s = lossy_arg(arg);
        if self.cmd_tail {
            1 + caret_escaped_len(&s)
        } else {
            1 + win_quoted_len(&s)
        }
    }

    fn clear_args(&mut self) {
        self.argv.truncate(self.initial_argc);
        self.argv_chars = self.initial_chars;
        self.win_chars = self.initial_win;
        self.cmd_tail = self.initial_cmd_tail;
    }

    /// buildcmd.c bc_push_arg (plus the Windows command-line budget).
    fn push_arg(&mut self, arg: &[u8], initial: bool) -> Result<(), Stop> {
        let len = arg.len() + 1;
        let wcost = self.arg_win_cost(arg);
        let gnu_over = self.argv_chars + len > self.o.arg_max;
        let win_over = !self.in_process_echo
            && !self.argv.is_empty()
            && self.win_base + self.win_chars + wcost > self.win_limit;
        if gnu_over || win_over {
            let first = initial || self.argv.len() == self.initial_argc;
            let strict = self.o.replace.is_some()
                || (self.o.exit_if_size_exceeded
                    && (self.o.lines_per_exec != 0 || self.o.args_per_exec != 0));
            if gnu_over && first {
                self.error("cannot fit single argument within argument list size limit");
                return Err(self.fatal(1));
            }
            if gnu_over && strict {
                self.error("argument list too long");
                return Err(self.fatal(1));
            }
            if !gnu_over && (first || strict) {
                // the Windows limit is the system's own: GNU's E2BIG face
                let name = String::from_utf8_lossy(&self.argv[0]).into_owned();
                self.error(&format!("{name}: Argument list too long"));
                return Err(self.fatal(126));
            }
            self.do_exec()?;
        }
        self.argv.push(arg.to_vec());
        self.argv_chars += len;
        self.win_chars += self.arg_win_cost(arg);
        // /c itself is still quoted-form; every LATER argument is
        // caret-escaped for cmd's parser (build_command_line's tail).
        if !self.cmd_tail && self.cmd_image && matches!(lossy_arg(arg).as_str(), "/c" | "/C") {
            self.cmd_tail = true;
        }
        if !initial
            && self.o.args_per_exec != 0
            && (self.argv.len() - self.initial_argc) as u64 == self.o.args_per_exec
        {
            self.do_exec()?;
        }
        Ok(())
    }

    /// xargs.c exec_if_possible: run what was gathered before a fatal
    /// input error.
    fn exec_if_possible(&mut self) -> Result<(), Stop> {
        if self.argv.len() != self.initial_argc {
            self.o.exit_if_size_exceeded = false;
            self.do_exec()?;
        }
        Ok(())
    }

    fn print_args(&mut self, ask: bool) -> Result<bool, Stop> {
        let mut line = Vec::new();
        for (i, a) in self.argv.iter().enumerate() {
            if i > 0 {
                line.push(b' ');
            }
            let end = a.iter().position(|&b| b == 0).unwrap_or(a.len());
            line.extend_from_slice(&shell_quote(&a[..end]));
        }
        let _ = self.err.write_all(&line);
        if !ask {
            let _ = self.err.write_all(b"\n");
            let _ = self.err.flush();
            return Ok(false);
        }
        if !self.tty_open {
            if let Err(reason) = self.tty.open_answer() {
                self.error(&format!("failed to open /dev/tty for reading: {reason}"));
                return Err(self.fatal(1));
            }
            self.tty_open = true;
        }
        let _ = self.err.write_all(b"?...");
        let _ = self.err.flush();
        match self.tty.read_answer() {
            Some(answer) => Ok(matches!(answer.first(), Some(b'y') | Some(b'Y'))),
            None => {
                self.error("Failed to read from stdin");
                Err(self.fatal(1))
            }
        }
    }

    /// Records one reaped child (xargs.c wait_for_proc status handling).
    fn child_done(&mut self, idx: usize, code: u32) -> Result<(), Stop> {
        let r = self.running.remove(idx);
        if let Some(s) = r.slot {
            self.slots[s] = false;
        }
        let name = String::from_utf8_lossy(&r.name).into_owned();
        if code == 0 {
            return Ok(());
        }
        if code == 255 {
            self.error(&format!("{name}: exited with status 255; aborting"));
            return Err(self.fatal(124));
        }
        if code >= 0x8000_0000 {
            let sig = ntstatus_signal(code);
            self.error(&format!("{name}: terminated by signal {sig}"));
            return Err(self.fatal(125));
        }
        self.child_error = 123;
        Ok(())
    }

    /// Non-blocking reap of every finished child.
    fn reap(&mut self) -> Result<(), Stop> {
        let mut i = 0usize;
        while i < self.running.len() {
            match self.running[i].child.try_wait() {
                Ok(Some(st)) => {
                    let code = st.code().map(|c| c as u32).unwrap_or(0x8000_0000);
                    self.child_done(i, code)?;
                }
                Ok(None) => i += 1,
                Err(_) => {
                    self.child_done(i, 0x8000_0000)?;
                }
            }
        }
        Ok(())
    }

    /// Blocks until one child exits.
    fn wait_one(&mut self) -> Result<(), Stop> {
        if self.running.is_empty() {
            return Ok(());
        }
        let idx = if self.running.len() == 1 {
            0
        } else {
            wait_any(&self.running)
        };
        let code = match self.running[idx].child.wait() {
            Ok(st) => st.code().map(|c| c as u32).unwrap_or(0x8000_0000),
            Err(_) => 0x8000_0000,
        };
        self.child_done(idx, code)
    }

    fn wait_all(&mut self) -> Result<(), Stop> {
        while !self.running.is_empty() {
            self.wait_one()?;
        }
        Ok(())
    }

    /// The default command: coreutils echo semantics through io.stdout.
    fn run_echo(&mut self) -> Result<(), Stop> {
        // Byte-exact argv: the in-process echo must not round-trip
        // through UTF-8 (lossy_arg turned 0xff into U+FFFD). NUL still
        // truncates each item, matching the C-string argv model.
        let args: Vec<&[u8]> = self.argv[1..]
            .iter()
            .map(|a| {
                let end = a.iter().position(|&b| b == 0).unwrap_or(a.len());
                &a[..end]
            })
            .collect();
        let mut empty = std::io::empty();
        let r = {
            let mut io = Io {
                stdin: &mut empty,
                stdout: &mut *self.out,
                stderr: &mut *self.err,
            };
            let r = crate::echo::echo_bytes(&args, &mut io, self.ctx);
            match r {
                Ok(s) => io.stdout.flush().map(|_| s).map_err(Some),
                Err(winux_core::CmdError::Io(e)) => Err(Some(e)),
            }
        };
        match r {
            Ok(0) => Ok(()),
            Ok(_) => {
                self.child_error = 123;
                Ok(())
            }
            Err(Some(e)) if is_broken_pipe(&e) => {
                self.error("echo: terminated by signal 13");
                Err(self.fatal(125))
            }
            Err(_) => {
                let _ = writeln!(self.err, "echo: write error");
                self.child_error = 123;
                Ok(())
            }
        }
    }

    fn launch(&mut self) -> Result<bool, Stop> {
        if self.in_process_echo {
            self.run_echo()?;
            return Ok(true);
        }
        let name = String::from_utf8_lossy(&self.argv[0]).into_owned();
        if self.resolved.is_none() {
            self.resolved = Some(resolve_child(&lossy_arg(&self.argv[0]), self.ctx));
        }
        let launch = match self.resolved.as_ref() {
            Some(ChildResolve::Found(l)) => l,
            Some(ChildResolve::NotFound) | None => {
                self.error(&format!("{name}: No such file or directory"));
                return Err(self.fatal(127));
            }
            Some(ChildResolve::NotExecutable) => {
                self.error(&format!("{name}: Permission denied"));
                return Err(self.fatal(126));
            }
            Some(ChildResolve::NotADirectory) => {
                self.error(&format!("{name}: Not a directory"));
                return Err(self.fatal(126));
            }
            Some(ChildResolve::BadFormat) => {
                self.error(&format!("{name}: Exec format error"));
                return Err(self.fatal(126));
            }
        };
        let args: Vec<String> = self.argv[1..].iter().map(|a| lossy_arg(a)).collect();
        let mut cmd = if matches!(launch, ChildLaunch::Image(p) if is_cmd_image(p)) {
            // cmd /c: everything after the first /c or /C is cmd's own
            // command string, so each later argument is caret-escaped
            // for cmd's parser (the 1.x body's build_command_line)
            // instead of Win32-quoted — quoting let `&` reach cmd as
            // command syntax. The empty-args build keeps env/cwd; the
            // serialized tail goes through raw_arg verbatim.
            let mut c = build_child_command(launch, &[], self.ctx);
            let mut tail = false;
            for a in &args {
                if tail {
                    c.raw_arg(cmd_escape_arg(a));
                } else {
                    c.raw_arg(quote_windows_arg(a));
                    if matches!(a.as_str(), "/c" | "/C") {
                        tail = true;
                    }
                }
            }
            c
        } else {
            build_child_command(launch, &args, self.ctx)
        };
        match self.child_stdin {
            ChildStdin::Null => {
                cmd.stdin(Stdio::null());
            }
            ChildStdin::Inherit => {
                cmd.stdin(Stdio::inherit());
            }
            ChildStdin::Console => match self.tty.child_stdin() {
                Ok(s) => {
                    cmd.stdin(s);
                }
                Err(reason) => {
                    self.error(&format!("'/dev/tty': {reason}"));
                    self.child_error = 123;
                    return Ok(false);
                }
            },
        }
        let mut slot = None;
        if let Some(var) = self.o.slot_var.clone() {
            let s = match self.slots.iter().position(|b| !*b) {
                Some(s) => s,
                None => {
                    self.slots.push(false);
                    self.slots.len() - 1
                }
            };
            self.slots[s] = true;
            slot = Some(s);
            cmd.env(var, s.to_string());
        }
        let _ = self.out.flush();
        let _ = self.err.flush();
        match cmd.spawn() {
            Ok(child) => {
                self.running.push(Running {
                    child,
                    name: self.argv[0].clone(),
                    slot,
                });
                Ok(true)
            }
            Err(e) => {
                if let Some(s) = slot {
                    self.slots[s] = false;
                }
                let (msg, status) = match e.kind() {
                    std::io::ErrorKind::NotFound => ("No such file or directory", 127),
                    _ => match e.raw_os_error() {
                        // ERROR_FILENAME_EXCED_RANGE: the command line
                        Some(206) => ("Argument list too long", 126),
                        _ => ("Permission denied", 126),
                    },
                };
                self.error(&format!("{name}: {msg}"));
                Err(self.fatal(status))
            }
        }
    }

    /// xargs.c xargs_do_exec + bc_clear_args.
    fn do_exec(&mut self) -> Result<(), Stop> {
        self.reap()?;
        let run = if self.o.query {
            self.print_args(true)?
        } else {
            if self.o.print_command {
                self.print_args(false)?;
            }
            true
        };
        if run {
            self.reap()?;
            while self.o.max_procs != 0 && self.running.len() as u64 >= self.o.max_procs {
                self.wait_one()?;
            }
            if self.launch()? {
                self.procs_executed += 1;
            }
        }
        self.clear_args();
        Ok(())
    }

    /// Appends one input byte to the item buffer (the shared tail of
    /// read_line/read_string: NUL warning and the endbuf check).
    fn append(&mut self, c: u8, warn_nul: bool) -> Result<(), Stop> {
        if warn_nul && c == 0 && !self.null_warned {
            self.null_warned = true;
            let _ = writeln!(
                self.err,
                "xargs: WARNING: a NUL character occurred in the input.  It cannot be passed through in the argument list.  Did you mean to use the --null option?"
            );
        }
        let limit = self.o.arg_max as isize - self.initial_chars as isize - 1;
        if self.linebuf.len() as isize >= limit {
            self.exec_if_possible()?;
            self.error("argument line too long");
            return Err(self.fatal(1));
        }
        self.linebuf.push(c);
        Ok(())
    }

    fn push_linebuf(&mut self) -> Result<(), Stop> {
        let item = std::mem::take(&mut self.linebuf);
        let r = self.push_arg(&item, false);
        self.linebuf = item;
        self.linebuf.clear();
        r
    }

    fn unmatched(&mut self, quotc: u8) -> Stop {
        if let Err(s) = self.exec_if_possible() {
            return s;
        }
        let kind = if quotc == b'"' { "double" } else { "single" };
        self.error(&format!(
            "unmatched {kind} quote; by default quotes are special to xargs unless you use the -0 option"
        ));
        self.fatal(1)
    }

    /// xargs.c read_line. `Ok(false)` is its -1 (no more input).
    fn read_line(&mut self, input: &mut Input) -> Result<bool, Stop> {
        #[derive(PartialEq)]
        enum St {
            Space,
            Norm,
            Quote,
            Backslash,
        }
        if self.eof {
            return Ok(false);
        }
        let replace = self.o.replace.is_some();
        let mut state = St::Space;
        let mut quotc = 0u8;
        let mut c: Option<u8> = None;
        let mut first = true;
        let mut seen_arg = false;
        self.linebuf.clear();
        loop {
            let prevc = c;
            c = input.getc();
            let Some(ch) = c else {
                self.eof = true;
                if self.linebuf.is_empty() {
                    return Ok(false);
                }
                if state == St::Quote {
                    return Err(self.unmatched(quotc));
                }
                if first && self.eof_match(&self.linebuf) {
                    return Ok(false);
                }
                if !replace {
                    self.push_linebuf()?;
                }
                return Ok(true);
            };
            match state {
                St::Space | St::Norm => {
                    if state == St::Space {
                        if is_c_space(ch) {
                            continue;
                        }
                        state = St::Norm;
                    }
                    if ch == b'\n' {
                        if !prevc.is_some_and(is_blank) {
                            self.lineno += 1;
                        }
                        if self.linebuf.is_empty() && !seen_arg {
                            state = St::Space;
                            continue;
                        }
                        if self.eof_match(&self.linebuf) {
                            self.eof = true;
                            return Ok(!first);
                        }
                        if !replace {
                            self.push_linebuf()?;
                        }
                        return Ok(true);
                    }
                    seen_arg = true;
                    if !replace && is_blank(ch) {
                        if self.eof_match(&self.linebuf) {
                            self.eof = true;
                            return Ok(!first);
                        }
                        self.push_linebuf()?;
                        state = St::Space;
                        first = false;
                        continue;
                    }
                    match ch {
                        b'\\' => {
                            state = St::Backslash;
                            continue;
                        }
                        b'\'' | b'"' => {
                            state = St::Quote;
                            quotc = ch;
                            continue;
                        }
                        _ => {}
                    }
                }
                St::Quote => {
                    if ch == b'\n' {
                        return Err(self.unmatched(quotc));
                    }
                    if ch == quotc {
                        state = St::Norm;
                        seen_arg = true;
                        continue;
                    }
                }
                St::Backslash => state = St::Norm,
            }
            self.append(ch, true)?;
        }
    }

    /// xargs.c read_string (-0 / -d).
    fn read_string(&mut self, input: &mut Input, delim: u8) -> Result<bool, Stop> {
        if self.eof {
            return Ok(false);
        }
        let replace = self.o.replace.is_some();
        self.linebuf.clear();
        loop {
            let Some(ch) = input.getc() else {
                self.eof = true;
                if self.linebuf.is_empty() {
                    return Ok(false);
                }
                if !replace {
                    self.push_linebuf()?;
                }
                return Ok(true);
            };
            if ch == delim {
                self.lineno += 1;
                if !replace {
                    self.push_linebuf()?;
                }
                return Ok(true);
            }
            self.append(ch, false)?;
        }
    }

    fn read_args(&mut self, input: &mut Input) -> Result<bool, Stop> {
        match self.o.delimiter {
            Some(d) => self.read_string(input, d),
            None => self.read_line(input),
        }
    }

    /// buildcmd.c bc_do_insert: one INITIAL-ARG with every occurrence of
    /// the replace string substituted, under the per-argument budget.
    fn insert(&mut self, template: &[u8], item: &[u8]) -> Result<Vec<u8>, Stop> {
        let pat = self.o.replace.clone().unwrap_or_default();
        let mut bytes_left = self.o.arg_max.saturating_sub(1);
        let mut out = Vec::new();
        let mut arg = template;
        loop {
            let found = if pat.is_empty() {
                Some(0)
            } else {
                arg.windows(pat.len()).position(|w| w == pat.as_slice())
            };
            let len = found.unwrap_or(arg.len());
            if bytes_left <= len {
                break;
            }
            bytes_left -= len;
            out.extend_from_slice(&arg[..len]);
            arg = &arg[len..];
            if found.is_some() {
                if bytes_left <= item.len() {
                    break;
                }
                bytes_left -= item.len();
                out.extend_from_slice(item);
                arg = &arg[pat.len()..];
                if pat.is_empty() && item.is_empty() {
                    // GNU would spin here forever (rplen 0, lblen 0)
                    bytes_left = 0;
                }
            }
            if arg.is_empty() {
                break;
            }
        }
        if !arg.is_empty() {
            self.error("command too long");
            return Err(self.fatal(1));
        }
        Ok(out)
    }

    fn run(&mut self, input: &mut Input) -> Result<u32, Stop> {
        let command = std::mem::take(&mut self.o.command);
        if self.o.replace.is_none() {
            for a in &command {
                self.push_arg(a, true)?;
            }
            self.initial_argc = self.argv.len();
            self.initial_chars = self.argv_chars;
            self.initial_win = self.win_chars;
            self.initial_cmd_tail = self.cmd_tail;
            while self.read_args(input)? {
                if self.o.lines_per_exec != 0 && self.lineno >= self.o.lines_per_exec {
                    self.do_exec()?;
                    self.lineno = 0;
                }
            }
            if self.argv.len() != self.initial_argc
                || (self.o.always_run && self.procs_executed == 0)
            {
                self.do_exec()?;
            }
        } else {
            // -I: the command name is pushed verbatim, INITIAL-ARGS get
            // the item inserted; every item runs one command.
            self.o.args_per_exec = 0;
            self.o.lines_per_exec = 0;
            while self.read_args(input)? {
                let item = std::mem::take(&mut self.linebuf);
                let end = item.iter().position(|&b| b == 0).unwrap_or(item.len());
                let item = item[..end].to_vec();
                self.argv.clear();
                self.argv_chars = 0;
                self.win_chars = 0;
                self.cmd_tail = false;
                for (i, t) in command.iter().enumerate() {
                    let arg = if i == 0 {
                        t.clone()
                    } else {
                        self.insert(t, &item)?
                    };
                    self.push_arg(&arg, false)?;
                }
                self.do_exec()?;
            }
        }
        self.wait_all()?;
        Ok(self.child_error)
    }
}

/// WaitForMultipleObjects over the running children (<= 64 handles),
/// polling beyond the API limit. Returns the index of an exited child.
fn wait_any(running: &[Running]) -> usize {
    use std::os::windows::io::AsRawHandle;
    #[link(name = "kernel32")]
    extern "system" {
        fn WaitForMultipleObjects(
            n: u32,
            handles: *const *mut core::ffi::c_void,
            wait_all: i32,
            ms: u32,
        ) -> u32;
    }
    if running.len() <= 64 {
        let handles: Vec<*mut core::ffi::c_void> = running
            .iter()
            .map(|r| r.child.as_raw_handle() as *mut core::ffi::c_void)
            .collect();
        // SAFETY: the handles are live process handles owned by `running`
        // for the duration of the call.
        let r = unsafe {
            WaitForMultipleObjects(handles.len() as u32, handles.as_ptr(), 0, 0xFFFF_FFFF)
        };
        let idx = r as usize;
        if idx < running.len() {
            return idx;
        }
    }
    loop {
        for (i, r) in running.iter().enumerate() {
            use std::os::windows::io::AsRawHandle as _;
            #[link(name = "kernel32")]
            extern "system" {
                fn WaitForSingleObject(h: *mut core::ffi::c_void, ms: u32) -> u32;
            }
            // SAFETY: live process handle; zero-timeout probe.
            let s = unsafe { WaitForSingleObject(r.child.as_raw_handle() as *mut _, 0) };
            if s == 0 {
                return i;
            }
        }
        std::thread::sleep(std::time::Duration::from_millis(5));
    }
}

/// The POSIX signal class of an NTSTATUS-shaped exit code.
fn ntstatus_signal(code: u32) -> u32 {
    match code {
        0xC000_0005 | 0xC000_00FD | 0xC000_0006 => 11,
        0xC000_001D | 0xC000_0096 => 4,
        0xC000_008C..=0xC000_0095 | 0xC000_02B4 | 0xC000_02B5 => 8,
        0xC000_013A => 2,
        0xC000_0409 | 0x4000_0015 => 6,
        0x8000_0003 | 0x8000_0004 => 5,
        _ => 9,
    }
}

fn is_broken_pipe(e: &std::io::Error) -> bool {
    e.kind() == std::io::ErrorKind::BrokenPipe || matches!(e.raw_os_error(), Some(109) | Some(232))
}

fn env_size(ctx: &Ctx) -> usize {
    ctx.env
        .entries()
        .iter()
        .map(|(k, v)| k.to_string_lossy().len() + 1 + v.to_string_lossy().len() + 1)
        .sum()
}

pub fn xargs_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut tty = ConsoleTty::default();
    run_with_tty(args, io, ctx, &mut tty, false)
}

/// The command body with an injected terminal. `inline_echo` also runs an
/// EXPLICIT `echo` command in-process (unit tests only: it lets them read
/// child output through the in-memory Io; the product spawns it).
pub(crate) fn run_with_tty(
    args: &[OsString],
    io: &mut Io,
    ctx: &Ctx,
    tty: &mut dyn Tty,
    inline_echo: bool,
) -> CmdResult {
    let o = match parse_args(ctx, args, io.stdout, io.stderr) {
        Parsed::Run(o) => o,
        Parsed::Exit(s) => return Ok(s),
    };
    let Io {
        stdin,
        stdout,
        stderr,
    } = io;
    let mut o = o;

    if o.eof_str.is_some() && o.delimiter.is_some() {
        let _ = writeln!(
            stderr,
            "xargs: warning: the -E option has no effect if -0 or -d is used.\n"
        );
    }
    if o.replace.is_some() || o.lines_per_exec != 0 {
        o.exit_if_size_exceeded = true;
    }
    if let Some(v) = &o.slot_var {
        if v.is_empty() {
            let _ = writeln!(
                stderr,
                "xargs: failed to unset environment variable : Invalid argument"
            );
            return Ok(1);
        }
    }
    let in_process_echo =
        o.command.is_empty() || (inline_echo && o.command[0].as_slice() == b"echo");
    if o.command.is_empty() {
        o.command = vec![b"echo".to_vec()];
    }
    if o.show_limits {
        let env = env_size(ctx);
        let _ = writeln!(stderr, "Your environment variables take up {env} bytes");
        let _ = writeln!(
            stderr,
            "POSIX upper limit on argument length (this system): {POSIX_ARG_SIZE_MAX}"
        );
        let _ = writeln!(
            stderr,
            "POSIX smallest allowable upper limit on argument length (all systems): 4096"
        );
        // The environment block does not share the Windows command line.
        let _ = writeln!(
            stderr,
            "Maximum length of command we could actually use: {POSIX_ARG_SIZE_MAX}"
        );
        let _ = writeln!(
            stderr,
            "Size of command buffer we are actually using: {}",
            o.arg_max
        );
        let _ = writeln!(
            stderr,
            "Maximum parallelism (--max-procs must be no greater): {MAX_PROC_MAX}"
        );
    }

    // input source: -a FILE frees stdin for the children (GNU keep_stdin)
    let mut file: Option<std::fs::File> = None;
    let mut child_stdin = ChildStdin::Null;
    let mut empty_input = std::io::empty();
    let mut dir_input = false;
    if let Some(path) = o.arg_file.clone() {
        if path != "-" {
            child_stdin = ChildStdin::Inherit;
            let native = fsutil::resolve(ctx, &path);
            if std::fs::metadata(&native)
                .map(|m| m.is_dir())
                .unwrap_or(false)
            {
                // Linux opens a directory and its first read fails, which
                // xargs' getc loop takes as end of input (probed rc 0).
                dir_input = true;
            } else {
                match std::fs::File::open(&native) {
                    Ok(f) => file = Some(f),
                    Err(e) => {
                        let reason = fsutil::gnu_open_reason(ctx, &path, &e, false);
                        let _ =
                            writeln!(stderr, "xargs: Cannot open input file '{path}': {reason}");
                        return Ok(1);
                    }
                }
            }
        }
    }
    if o.open_tty {
        child_stdin = ChildStdin::Console;
    }

    let mut runner = Runner {
        out: &mut **stdout,
        err: &mut **stderr,
        ctx,
        tty,
        resolved: None,
        win_base: 0,
        win_limit: WIN_CMDLINE_MAX - 1,
        child_stdin,
        in_process_echo,
        argv: Vec::new(),
        argv_chars: 0,
        win_chars: 0,
        cmd_image: false,
        cmd_tail: false,
        initial_argc: 0,
        initial_chars: 0,
        initial_win: 0,
        initial_cmd_tail: false,
        eof: false,
        lineno: 0,
        null_warned: false,
        linebuf: Vec::new(),
        running: Vec::new(),
        slots: Vec::new(),
        procs_executed: 0,
        child_error: 0,
        tty_open: false,
        o,
    };
    if !runner.in_process_echo {
        // resolve once for the command-line budget; diagnostics wait for
        // the first exec, like execvp (-r with no input never reports).
        let r = resolve_child(&lossy_arg(&runner.o.command[0]), ctx);
        if let ChildResolve::Found(l) = &r {
            let (base, limit) = launch_budget(l, ctx);
            runner.win_base = base;
            runner.win_limit = limit;
            runner.cmd_image = matches!(l, ChildLaunch::Image(p) if is_cmd_image(p));
        }
        runner.resolved = Some(r);
    }
    let status = {
        let src: &mut dyn Read = if dir_input {
            &mut empty_input
        } else if let Some(f) = file.as_mut() {
            f
        } else {
            &mut **stdin
        };
        let mut input = Input::new(src);
        match runner.run(&mut input) {
            Ok(s) => s,
            Err(Stop(s)) => s,
        }
    };
    Ok(status)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};
    use winux_core::MapEnv;

    /// Scripted /dev/tty: answers in order, `None` = EOF; `fail_open`
    /// models a process with no console.
    struct FakeTty {
        answers: Vec<Option<&'static str>>,
        fail_open: bool,
        opened: u32,
    }

    impl Tty for FakeTty {
        fn open_answer(&mut self) -> Result<(), &'static str> {
            self.opened += 1;
            if self.fail_open {
                Err("No such device or address")
            } else {
                Ok(())
            }
        }
        fn read_answer(&mut self) -> Option<Vec<u8>> {
            if self.answers.is_empty() {
                return None;
            }
            self.answers.remove(0).map(|s| s.as_bytes().to_vec())
        }
        fn child_stdin(&mut self) -> Result<Stdio, &'static str> {
            Err("No such device or address")
        }
    }

    fn real_env() -> MapEnv {
        let mut env = MapEnv::new();
        for (k, v) in std::env::vars_os() {
            env.set(&k.to_string_lossy(), &v.to_string_lossy());
        }
        env
    }

    fn run_tty(input: &[u8], args: &[&str], tty: &mut FakeTty) -> (u32, String, String) {
        let sc = ScratchCtx::new("xargs");
        let env = real_env();
        let ctx = Ctx {
            cwd: sc.dir.clone(),
            env: &env,
            host: &winux_core::host::DEFAULT_HOST,
        };
        let mut io = MemIo::new("");
        io.input = std::io::Cursor::new(input.to_vec());
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = run_with_tty(&owned, &mut io.split(), &ctx, tty, true).unwrap();
        (r, io.out(), io.err())
    }

    fn run(input: &str, args: &[&str]) -> (u32, String, String) {
        let mut tty = FakeTty {
            answers: vec![],
            fail_open: true,
            opened: 0,
        };
        run_tty(input.as_bytes(), args, &mut tty)
    }

    /// Byte-exact variant of run_tty for non-UTF-8 input/output probes.
    fn run_bytes(input: &[u8], args: &[&str]) -> (u32, Vec<u8>, Vec<u8>) {
        let sc = ScratchCtx::new("xargs");
        run_in(&sc.dir, input, args)
    }

    /// run_bytes with an explicit cwd (child-side file probes).
    fn run_in(dir: &std::path::Path, input: &[u8], args: &[&str]) -> (u32, Vec<u8>, Vec<u8>) {
        let env = real_env();
        let ctx = Ctx {
            cwd: dir.to_path_buf(),
            env: &env,
            host: &winux_core::host::DEFAULT_HOST,
        };
        let mut io = MemIo::new("");
        io.input = std::io::Cursor::new(input.to_vec());
        let mut tty = FakeTty {
            answers: vec![],
            fail_open: true,
            opened: 0,
        };
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = run_with_tty(&owned, &mut io.split(), &ctx, &mut tty, true).unwrap();
        (r, io.output.clone(), io.errout.clone())
    }

    fn out(input: &str, args: &[&str]) -> String {
        let (r, o, e) = run(input, args);
        assert_eq!(r, 0, "args={args:?} err={e}");
        o
    }

    #[test]
    fn default_parsing_quotes_and_escapes() {
        assert_eq!(out("a b c\nd e\n", &[]), "a b c d e\n");
        assert_eq!(out("", &[]), "\n");
        assert_eq!(out("  \n\n", &[]), "\n");
        assert_eq!(out("a 'b c' d\n", &["-n1"]), "a\nb c\nd\n");
        assert_eq!(out("a \"b c\" d\n", &["-n1"]), "a\nb c\nd\n");
        assert_eq!(out("a\\ b c\\\\d\n", &["-n1"]), "a b\nc\\d\n");
        assert_eq!(out("a \"b\\c\" d\n", &["-n1"]), "a\nb\\c\nd\n");
        assert_eq!(out("a\\\nb\n", &[]), "a\nb\n");
        assert_eq!(out("a\\", &[]), "a\n");
        assert_eq!(out("'' \"\" x\n", &[]), "  x\n");
        assert_eq!(out("''\nb\n", &["-n1"]), "\nb\n");
        assert_eq!(out("a\"b c\"d e\n", &["-n1"]), "ab cd\ne\n");
        // CR/FF/VT are skipped before an item, literal inside one
        assert_eq!(out("a\r\nb\r\n", &[]), "a\r b\r\n");
        assert_eq!(out("\x0c\x0b\ra\n", &[]), "a\n");
        // EOF quirks: '' alone at EOF is no item; a lone quote at EOF drops
        assert_eq!(out("''", &["-n1"]), "\n");
        assert_eq!(out("a '", &[]), "a\n");
    }

    #[test]
    fn unmatched_quotes_run_pending_then_fail() {
        let (r, o, e) = run("a 'b c\n", &[]);
        assert_eq!((r, o.as_str()), (1, "a\n"));
        assert_eq!(
            e,
            "xargs: unmatched single quote; by default quotes are special to xargs unless you use the -0 option\n"
        );
        let (r, o, e) = run("a \"b", &[]);
        assert_eq!((r, o.as_str()), (1, "a\n"));
        assert!(e.contains("unmatched double quote"));
    }

    #[test]
    fn nul_warns_once_and_truncates() {
        let (r, o, e) = run("ab\0cd e\0f\n", &[]);
        assert_eq!((r, o.as_str()), (0, "ab e\n"));
        assert_eq!(e.matches("WARNING: a NUL character").count(), 1);
    }

    #[test]
    fn max_args_and_lines() {
        assert_eq!(out("a b c d e\n", &["-n2"]), "a b\nc d\ne\n");
        assert_eq!(out("a b c d\n", &["-n2"]), "a b\nc d\n");
        assert_eq!(out("a b\nc\nd e f\n", &["-L1"]), "a b\nc\nd e f\n");
        assert_eq!(out("a b\nc\nd e f\n", &["-L2"]), "a b c\nd e f\n");
        assert_eq!(out("a b \nc\nd\n", &["-L1"]), "a b c\nd\n");
        assert_eq!(out("a\t\nb\nc\n", &["-L1"]), "a b\nc\n");
        assert_eq!(out("a\n\nb\n", &["-L1"]), "a\nb\n");
        assert_eq!(out("a \nb ", &["-L1"]), "a b\n");
        assert_eq!(out("a\nb\n", &["-l"]), "a\nb\n");
        assert_eq!(out("a\nb\nc\n", &["--max-lines=2"]), "a b\nc\n");
        assert_eq!(out("a b c\n", &["--max-args", "1"]), "a\nb\nc\n");
        assert_eq!(out("a b\n", &["-n", "+1"]), "a\nb\n");
        assert_eq!(out("a b\n", &["-n", " 1"]), "a\nb\n");
        assert_eq!(out("a b\n", &["-n", "99999999999999999999"]), "a b\n");
    }

    #[test]
    fn eof_string() {
        assert_eq!(out("a b\nEND\nc\n", &["-E", "END"]), "a b\n");
        assert_eq!(out("a END b\nc\n", &["-E", "END"]), "a\n");
        assert_eq!(out("END\nb\n", &["-E", "END"]), "\n");
        assert_eq!(out("a b\nEND\nc\n", &["-eEND"]), "a b\n");
        assert_eq!(out("a b\nEND\nc\n", &["-e"]), "a b END c\n");
        assert_eq!(out("a b\nEND\nc\n", &["--eof=END"]), "a b\n");
        assert_eq!(out("a 'END' b\n", &["-E", "END"]), "a\n");
        // probed quirk: the last item of an unterminated last line
        assert_eq!(out("a END", &["-E", "END"]), "a END\n");
        assert_eq!(out("  END\nb\n", &["-E", "END"]), "\n");
        let (r, o, e) = run("a\0b\0END\0", &["-0", "-E", "END"]);
        assert_eq!((r, o.as_str()), (0, "a b END\n"));
        assert_eq!(
            e,
            "xargs: warning: the -E option has no effect if -0 or -d is used.\n\n"
        );
    }

    #[test]
    fn null_and_delimiter() {
        assert_eq!(out("a b\0c\0\0d", &["-0"]), "a b c  d\n");
        assert_eq!(out("a\0b\0", &["-0"]), "a b\n");
        assert_eq!(out("a,b,,c", &["-d,"]), "a b  c\n");
        assert_eq!(out("a b\nc\n", &["-d", "\\n"]), "a b c\n");
        assert_eq!(out("aAbAc", &["-d", "\\x41"]), "a b c\n");
        assert_eq!(out("a\nb", &["-d", "\\012"]), "a b\n");
        assert_eq!(out("a\0b", &["-d", "\\x"]), "a b\n");
        assert_eq!(out("aAb", &["-d", "\\x0x41"]), "a b\n");
        assert_eq!(out("anb", &["-d", "\\nx"]), "anb\n");
        assert_eq!(out("a,b:c", &["-d,", "-d:"]), "a,b c\n");
        assert_eq!(out("a,b\0c", &["-d,", "-0"]), "a,b c\n");
        assert_eq!(out("a,b,c", &["-L1", "-d,"]), "a\nb\nc\n");
        assert_eq!(out("a\0b\0c", &["-L2", "-0"]), "a b\nc\n");
        for (spec, msg) in [
            ("ab", "Invalid input delimiter specification ab: the delimiter must be either a single character or an escape sequence starting with \\."),
            ("\\q", "Invalid escape sequence \\q in input delimiter specification."),
            ("\\xZZ", "Invalid escape sequence \\xZZ in input delimiter specification; trailing characters ZZ not recognised."),
            ("\\777", "Invalid escape sequence \\777 in input delimiter specification; character values must not exceed 377."),
            ("\\x100", "Invalid escape sequence \\x100 in input delimiter specification; character values must not exceed ff."),
            ("\\8", "Invalid escape sequence \\8 in input delimiter specification; trailing characters 8 not recognised."),
            ("", "Invalid input delimiter specification : the delimiter must be either a single character or an escape sequence starting with \\."),
        ] {
            let (r, o, e) = run("a", &["-d", spec]);
            assert_eq!((r, o.as_str()), (1, ""), "spec {spec:?}");
            assert_eq!(e, format!("xargs: {msg}\n"), "spec {spec:?}");
        }
    }

    #[test]
    fn replace_mode() {
        assert_eq!(
            out("a\n  b c \nd\n", &["-I{}", "echo", "[{}]"]),
            "[a]\n[b c ]\n[d]\n"
        );
        assert_eq!(out("x\n", &["-I", "XX", "echo", "XX-XX", "XX"]), "x-x x\n");
        assert_eq!(out("a\nb\n", &["-i", "echo", "[{}]"]), "[a]\n[b]\n");
        assert_eq!(out("a\nb\n", &["-i%", "echo", "[%]"]), "[a]\n[b]\n");
        assert_eq!(out("a\n", &["--replace=Q", "echo", "[Q]"]), "[a]\n");
        assert_eq!(out("", &["-I{}", "echo", "[{}]"]), "");
        assert_eq!(out("'a b'  c\n", &["-I{}", "echo", "[{}]"]), "[a b  c]\n");
        assert_eq!(out("\n\n  \n", &["-I{}", "echo", "[{}]"]), "");
        assert_eq!(out("a\\\nb\n", &["-I{}", "echo", "[{}]"]), "[a\nb]\n");
        assert_eq!(
            out("  END  \nb\n", &["-E", "END", "-I{}", "echo", "[{}]"]),
            "[END  ]\n[b]\n"
        );
        assert_eq!(
            out("a\nEND\nb\n", &["-E", "END", "-I{}", "echo", "{}"]),
            "a\n"
        );
        assert_eq!(
            out("a b\0c\0", &["-0", "-I{}", "echo", "[{}]"]),
            "[a b]\n[c]\n"
        );
        assert_eq!(
            out("  a,b", &["-d,", "-I{}", "echo", "[{}]"]),
            "[  a]\n[b]\n"
        );
        assert_eq!(out("a\nb\n", &["-I{}"]), "\n\n");
        assert_eq!(out("a\nb\n", &["-I{}", "echo", "hi"]), "hi\nhi\n");
        assert_eq!(out("aa\n", &["-s", "10", "-I{}", "echo", "{}{}"]), "aaaa\n");
        let (r, _, e) = run("a b\n", &["-I", "", "echo", "x"]);
        assert_eq!((r, e.as_str()), (1, "xargs: command too long\n"));
    }

    #[test]
    fn size_limits() {
        assert_eq!(out("aa bb cc dd ee\n", &["-s10"]), "aa\nbb\ncc\ndd\nee\n");
        assert_eq!(out("aa bb\n", &["-s", "11"]), "aa bb\n");
        assert_eq!(out("aa bb\n", &["-s", "10"]), "aa\nbb\n");
        assert_eq!(
            out("aa bb cc dd ee\n", &["-s", "12", "-x", "-n", "2"]),
            "aa bb\ncc dd\nee\n"
        );
        assert_eq!(out("a\n", &["-s7"]), "a\n");
        let cases: &[(&str, &[&str], &str, &str)] = &[
            (
                "aa bb cc dd ee\n",
                &["-s10", "-x", "-n2"],
                "",
                "xargs: argument list too long\n",
            ),
            ("a\n", &["-s5"], "", "xargs: argument line too long\n"),
            (
                "a\n",
                &["-s1"],
                "",
                "xargs: cannot fit single argument within argument list size limit\n",
            ),
            (
                "a\n",
                &["-s", "6", "printf", "x"],
                "",
                "xargs: cannot fit single argument within argument list size limit\n",
            ),
            (
                "",
                &["-r", "-s", "3", "printf", "x"],
                "",
                "xargs: cannot fit single argument within argument list size limit\n",
            ),
            (
                "a bbbbbbbbbbbbbbbbbbb c\n",
                &["-s", "12"],
                "a\n",
                "xargs: argument line too long\n",
            ),
            (
                "aaa bbb ccc\n",
                &["-L1", "-s", "10"],
                "",
                "xargs: argument list too long\n",
            ),
            (
                "aaaaaaaaa\n",
                &["-s", "10", "-I{}", "echo", "{}"],
                "",
                "xargs: command too long\n",
            ),
            (
                "aaaaaaaa\n",
                &["-s", "10", "-I{}", "echo", "{}"],
                "",
                "xargs: argument list too long\n",
            ),
            (
                "a\n",
                &["-s", "3", "-I{}", "echo", "{}"],
                "",
                "xargs: cannot fit single argument within argument list size limit\n",
            ),
            (
                "aaaaaaaaaa\n",
                &["-s", "10", "-I{}", "echo", "{}"],
                "",
                "xargs: argument line too long\n",
            ),
            (
                "a\nbbbbbbbbbbbb\n",
                &["-s", "10", "-I{}", "echo", "{}"],
                "a\n",
                "xargs: argument line too long\n",
            ),
        ];
        for (input, args, o, e) in cases {
            let (r, got_o, got_e) = run(input, args);
            assert_eq!(
                (r, got_o.as_str(), got_e.as_str()),
                (1, *o, *e),
                "args {args:?}"
            );
        }
        let (r, o, e) = run("a\n", &["-s0"]);
        assert_eq!((r, o.as_str()), (1, ""));
        assert_eq!(e, "xargs: value 0 for -s option should be >= 1\nxargs: cannot fit single argument within argument list size limit\n");
        let (r, o, e) = run("a\n", &["-s", "99999999999"]);
        assert_eq!((r, o.as_str()), (0, "a\n"));
        assert_eq!(
            e,
            "xargs: value 99999999999 for -s option should be <= 30719\n"
        );
    }

    #[test]
    fn exclusive_batching_options_warn() {
        let w = |a: &str, b: &str| {
            format!("xargs: warning: options {a} and {b} are mutually exclusive, ignoring previous {a} value\n")
        };
        let (_, o, e) = run("a b\nc\n", &["-n1", "-L1"]);
        assert_eq!((o.as_str(), e), ("a b\nc\n", w("--max-args", "-L")));
        let (_, o, e) = run("a b\nc\n", &["-L1", "-n1"]);
        assert_eq!(
            (o.as_str(), e),
            ("a\nb\nc\n", w("--max-lines", "--max-args/-n"))
        );
        let (_, o, e) = run("a b\nc\n", &["-I{}", "-n1", "echo", "{}"]);
        assert_eq!((o.as_str(), e.as_str()), ("a b\nc\n", ""));
        let (_, o, e) = run("a b\nc\n", &["-I{}", "-n2", "echo", "{}"]);
        assert_eq!(
            (o.as_str(), e),
            ("{} a b\n{} c\n", w("--replace", "--max-args/-n"))
        );
        let (_, o, e) = run("a b\nc\n", &["-n2", "-I{}", "echo", "{}"]);
        assert_eq!(
            (o.as_str(), e),
            ("a b\nc\n", w("--max-args", "--replace/-I/-i"))
        );
        let (_, o, e) = run("a b\nc\n", &["-I{}", "-L2", "echo", "{}"]);
        assert_eq!((o.as_str(), e), ("{} a b c\n", w("--replace", "-L")));
        let (_, _, e) = run("a\n", &["-n1", "--max-lines=1"]);
        assert_eq!(e, w("--max-args", "--max-lines/-l"));
        // -n after -I clears -I, and with it the implied -x
        let (r, o, _) = run("aaa bbb\n", &["-I{}", "-n2", "-s", "9", "echo"]);
        assert_eq!((r, o.as_str()), (0, "aaa\nbbb\n"));
    }

    #[test]
    fn option_errors() {
        let t = "Try 'xargs --help' for more information.\n";
        let cases: &[(&[&str], &str)] = &[
            (&["-n0"], "xargs: value 0 for -n option should be >= 1\n"),
            (&["-n", "x"], "xargs: invalid number \"x\" for -n option\n"),
            (&["-n", ""], "xargs: invalid number \"\" for -n option\n"),
            (&["-n", "1 "], "xargs: invalid number \"1 \" for -n option\n"),
            (&["-L0"], "xargs: value 0 for -L option should be >= 1\n"),
            (&["-P", "-1"], "xargs: value -1 for -P option should be >= 0\n"),
            (&["-P", "99999999999"], "xargs: value 99999999999 for -P option should be <= 2147483647\n"),
            (&["-h"], "xargs: invalid option -- 'h'\n"),
            (&["-v"], "xargs: invalid option -- 'v'\n"),
            (&["--bogus"], "xargs: unrecognized option '--bogus'\n"),
            (&["-n"], "xargs: option requires an argument -- 'n'\n"),
            (&["--arg-file"], "xargs: option '--arg-file' requires an argument\n"),
            (&["--p"], "xargs: option '--process-slot-var' requires an argument\n"),
            (&["--null=x"], "xargs: option '--null' doesn't allow an argument\n"),
            (&["--max"], "xargs: option '--max' is ambiguous; possibilities: '--max-lines' '--max-args' '--max-chars' '--max-procs'\n"),
            (&["--e"], "xargs: option '--e' is ambiguous; possibilities: '--eof' '--exit'\n"),
            (&["--ver"], "xargs: option '--ver' is ambiguous; possibilities: '--verbose' '--version'\n"),
        ];
        for (args, msg) in cases {
            let (r, o, e) = run("a\n", args);
            assert_eq!((r, o.as_str()), (1, ""), "{args:?}");
            assert_eq!(e, format!("{msg}{t}"), "{args:?}");
        }
        let (r, _, e) = run("a\n", &["--process-slot-var=A=B"]);
        assert_eq!(r, 1);
        assert_eq!(
            e,
            "xargs: option --process-slot-var may not be set to a value which includes `='\n"
        );
        let (r, _, e) = run("a\n", &["--process-slot-var="]);
        assert_eq!(
            (r, e.as_str()),
            (
                1,
                "xargs: failed to unset environment variable : Invalid argument\n"
            )
        );
    }

    #[test]
    fn help_version_and_abbreviations() {
        let (r, o, _) = run("", &["--help"]);
        assert_eq!(r, 0);
        assert!(o.starts_with("Usage: xargs [OPTION]... COMMAND [INITIAL-ARGS]...\n"));
        assert!(o.ends_with("<bug-findutils@gnu.org>.\n"));
        assert_eq!(o.lines().count(), 47);
        let (r, o, _) = run("", &["--version"]);
        assert_eq!((r, o.as_str()), (0, VERSION_TEXT));
        // sequential getopt: help wins before a later bad option
        assert_eq!(run("", &["--help", "--bogus"]).0, 0);
        assert_eq!(run("", &["--bogus", "--help"]).0, 1);
        assert_eq!(out("a b\n", &["--max-a=1"]), "a\nb\n");
        assert_eq!(out("a\n", &["--no-run"]), "a\n");
        assert_eq!(out("a\n", &["--", "echo", "x"]), "x a\n");
        assert_eq!(out("a\n", &["echo", "-n"]), "a");
        assert_eq!(out("a:b", &["--delimiter=:"]), "a b\n");
    }

    #[test]
    fn no_run_if_empty() {
        assert_eq!(out("", &["-r"]), "");
        assert_eq!(out("  \n", &["-r", "echo", "x"]), "");
        assert_eq!(out("''\n", &["-r", "echo", "x"]), "x \n");
        assert_eq!(out("", &["-r", "-L1", "echo", "x"]), "");
        assert_eq!(out("", &["-L1", "echo", "x"]), "x\n");
        assert_eq!(out("", &["-n1", "echo", "x"]), "x\n");
        // a missing command never reports when nothing runs
        assert_eq!(run("", &["-r", "nosuchcmd-xargs-123"]).0, 0);
    }

    #[test]
    fn verbose_quoting_matches_gnu() {
        let q = |s: &[u8]| String::from_utf8(shell_quote(s)).unwrap();
        let cases: &[(&[u8], &str)] = &[
            (b"x y", "'x y'"),
            (b"it's", "\"it's\""),
            (b"a\"b", "'a\"b'"),
            (b"$v", "'$v'"),
            (b"tab\tx", "'tab'$'\\t''x'"),
            (b"", "''"),
            (b"q*", "'q*'"),
            ("é".as_bytes(), "é"),
            (b"~h", "'~h'"),
            (b"a=b", "'a=b'"),
            (b"#c", "'#c'"),
            (b"x,y", "x,y"),
            (b"{}", "{}"),
            (b"!", "'!'"),
            (b"^", "'^'"),
            (b"x\x01y", "'x'$'\\001''y'"),
            (b"a\nb", "'a'$'\\n''b'"),
            (b"it's $x", "'it'\\''s $x'"),
            (b"it's \"x\"", "'it'\\''s \"x\"'"),
            (b"it's\\x", "'it'\\''s\\x'"),
            (b"it's ok", "\"it's ok\""),
            (b"it's (x)", "'it'\\''s (x)'"),
            (b"it's ~", "'it'\\''s ~'"),
            (b"it's {}", "'it'\\''s {}'"),
            (b"it's @", "\"it's @\""),
            (b"it's ]", "\"it's ]\""),
            (b"it's ?", "'it'\\''s ?'"),
            (b"it's %", "\"it's %\""),
            (b"{", "'{'"),
            (b"}", "'}'"),
            (b"a{", "a{"),
            (b"#", "'#'"),
            (b"a#", "a#"),
            (b"a~", "a~"),
            (b"=a", "'=a'"),
            (b"?", "'?'"),
            (b"x}", "x}"),
            (b"x\x01", "'x'$'\\001'"),
            (b"\x01x", "''$'\\001''x'"),
            (b"\x01\x02", "''$'\\001\\002'"),
            (b"a\tb\tc", "'a'$'\\t''b'$'\\t''c'"),
            (b"it's\tx", "'it'\\''s'$'\\t''x'"),
            (b"\x7f", "''$'\\177'"),
            (b"\xff", "''$'\\377'"),
            (b"a\xc3", "'a'$'\\303'"),
        ];
        for (arg, want) in cases {
            assert_eq!(q(arg), *want, "arg {:?}", String::from_utf8_lossy(arg));
        }
        let (r, o, e) = run("a b\n", &["-t", "-n1"]);
        assert_eq!(
            (r, o.as_str(), e.as_str()),
            (0, "a\nb\n", "echo a\necho b\n")
        );
        let (_, _, e) = run("", &["-t"]);
        assert_eq!(e, "echo\n");
        let (_, _, e) = run("a b\n", &["-t", "-I{}", "echo", "{}"]);
        assert_eq!(e, "echo 'a b'\n");
    }

    #[test]
    fn interactive_prompts_through_the_tty() {
        let mut tty = FakeTty {
            answers: vec![Some("y"), Some("n")],
            fail_open: false,
            opened: 0,
        };
        let (r, o, e) = run_tty(b"a b\nc\n", &["-p", "-n2"], &mut tty);
        assert_eq!(
            (r, o.as_str(), e.as_str()),
            (0, "a b\n", "echo a b?...echo c?...")
        );
        assert_eq!(tty.opened, 1);
        // a declined command does not count: the -r-less final run prompts
        let mut tty = FakeTty {
            answers: vec![Some("n"), Some("n")],
            fail_open: false,
            opened: 0,
        };
        let (r, o, e) = run_tty(b"a\n", &["-p", "-n1"], &mut tty);
        assert_eq!((r, o.as_str(), e.as_str()), (0, "", "echo a?...echo?..."));
        let mut tty = FakeTty {
            answers: vec![Some("Yx")],
            fail_open: false,
            opened: 0,
        };
        assert_eq!(run_tty(b"a\n", &["-p"], &mut tty).1, "a\n");
        // EOF on the tty and no tty at all are fatal (GNU rc 1)
        let mut tty = FakeTty {
            answers: vec![None],
            fail_open: false,
            opened: 0,
        };
        let (r, o, e) = run_tty(b"a b c\n", &["-p"], &mut tty);
        assert_eq!((r, o.as_str()), (1, ""));
        assert_eq!(e, "echo a b c?...xargs: Failed to read from stdin\n");
        let (r, _, e) = run("a b\n", &["-p", "echo"]);
        assert_eq!(r, 1);
        assert_eq!(
            e,
            "echo a bxargs: failed to open /dev/tty for reading: No such device or address\n"
        );
        // -t with -p prints once (the prompt)
        let mut tty = FakeTty {
            answers: vec![Some("y")],
            fail_open: false,
            opened: 0,
        };
        let (_, _, e) = run_tty(b"a\n", &["-t", "-p"], &mut tty);
        assert_eq!(e, "echo a?...");
    }

    #[test]
    fn arg_file_sources() {
        let sc = ScratchCtx::new("xargs-a");
        std::fs::write(sc.path("args.txt"), "f1 f2\nf3\n").unwrap();
        std::fs::create_dir_all(sc.path("d1")).unwrap();
        let env = MapEnv::new();
        let ctx = Ctx {
            cwd: sc.dir.clone(),
            env: &env,
            host: &winux_core::host::DEFAULT_HOST,
        };
        let go = |args: &[&str], input: &str| {
            let mut io = MemIo::new(input);
            let owned: Vec<OsString> = args.iter().map(Into::into).collect();
            let r = xargs_cmd(&owned, &mut io.split(), &ctx).unwrap();
            (r, io.out(), io.err())
        };
        assert_eq!(go(&["-a", "args.txt"], "ignored\n").1, "f1 f2 f3\n");
        assert_eq!(go(&["-a", "args.txt", "-L1"], "").1, "f1 f2\nf3\n");
        assert_eq!(
            go(&["-a", "nosuch.txt", "-a", "args.txt"], "").1,
            "f1 f2 f3\n"
        );
        assert_eq!(go(&["-a", "-"], "q r\n").1, "q r\n");
        assert_eq!(go(&["-a", "d1"], "").1, "\n");
        let (r, o, e) = go(&["-a", "nosuch.txt"], "");
        assert_eq!((r, o.as_str()), (1, ""));
        assert_eq!(
            e,
            "xargs: Cannot open input file 'nosuch.txt': No such file or directory\n"
        );
    }

    #[test]
    fn show_limits_reports_the_windows_budget() {
        let (r, o, e) = run("", &["--show-limits", "-r", "-s", "100"]);
        assert_eq!((r, o.as_str()), (0, ""));
        let lines: Vec<&str> = e.lines().collect();
        assert_eq!(lines.len(), 6);
        assert!(lines[0].starts_with("Your environment variables take up "));
        assert_eq!(
            lines[1],
            "POSIX upper limit on argument length (this system): 30719"
        );
        assert_eq!(
            lines[4],
            "Size of command buffer we are actually using: 100"
        );
        assert_eq!(
            lines[5],
            "Maximum parallelism (--max-procs must be no greater): 2147483647"
        );
        // execution continues after the report
        assert_eq!(run("a\n", &["--show-limits"]).1, "a\n");
    }

    /// An endless input with a stdout that breaks after a few writes: the
    /// run must stop (streaming, not slurping) with GNU's SIGPIPE face.
    #[test]
    fn streams_lazily_and_stops_on_broken_pipe() {
        struct Yes(u64);
        impl Read for Yes {
            fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
                self.0 += buf.len() as u64;
                assert!(self.0 < 1 << 24, "xargs slurped its input");
                for pair in buf.chunks_mut(2) {
                    pair[0] = b'y';
                    if pair.len() > 1 {
                        pair[1] = b'\n';
                    }
                }
                Ok(buf.len())
            }
        }
        struct Head(usize);
        impl Write for Head {
            fn write(&mut self, b: &[u8]) -> std::io::Result<usize> {
                if self.0 == 0 {
                    return Err(std::io::ErrorKind::BrokenPipe.into());
                }
                self.0 -= 1;
                Ok(b.len())
            }
            fn flush(&mut self) -> std::io::Result<()> {
                Ok(())
            }
        }
        let sc = ScratchCtx::new("xargs-yes");
        let ctx = sc.ctx();
        let mut input = Yes(0);
        let mut stdout = Head(5);
        let mut stderr = Vec::new();
        let mut io = Io {
            stdin: &mut input,
            stdout: &mut stdout,
            stderr: &mut stderr,
        };
        let owned: Vec<OsString> = vec!["-n1".into()];
        let r = xargs_cmd(&owned, &mut io, &ctx).unwrap();
        assert_eq!(r, 125);
        assert_eq!(
            String::from_utf8_lossy(&stderr),
            "xargs: echo: terminated by signal 13\n"
        );
    }

    #[cfg(windows)]
    #[test]
    fn child_exit_statuses() {
        let (r, _, _) = run("a b\n", &["-n1", "cmd", "/c", "exit", "3"]);
        assert_eq!(r, 123);
        let (r, _, e) = run("a b c\n", &["-n1", "cmd", "/c", "exit", "255"]);
        assert_eq!(r, 124);
        assert_eq!(e, "xargs: cmd: exited with status 255; aborting\n");
        let (r, _, e) = run("a b\n", &["-n1", "nosuchcmd-xargs-123"]);
        assert_eq!(r, 127);
        assert_eq!(e, "xargs: nosuchcmd-xargs-123: No such file or directory\n");
        let (r, _, e) = run("a\n", &[""]);
        assert_eq!(
            (r, e.as_str()),
            (127, "xargs: : No such file or directory\n")
        );
        let (r, _, e) = run("a\n", &["."]);
        assert_eq!((r, e.as_str()), (126, "xargs: .: Permission denied\n"));
        let (r, _, _) = run("a b c d e f\n", &["-n1", "-P3", "cmd", "/c", "exit", "0"]);
        assert_eq!(r, 0);
        let (r, _, _) = run("a b c d\n", &["-n1", "-P0", "cmd", "/c", "exit", "2"]);
        assert_eq!(r, 123);
        // NTSTATUS-shaped death (0xC0000005 access violation) -> signal 11
        let (r, _, e) = run("a\n", &["cmd", "/c", "exit", "-1073741819"]);
        assert_eq!(r, 125);
        assert_eq!(e, "xargs: cmd: terminated by signal 11\n");
    }

    #[cfg(windows)]
    #[test]
    fn process_slot_var_and_batch_children() {
        let sc = ScratchCtx::new("xargs-slot");
        let bat = sc.path("slot.cmd");
        // exit with the slot number: slots are 0 and 1 under -P2
        std::fs::write(&bat, "@echo off\r\nexit /b %XSLOT%\r\n").unwrap();
        let script = bat.to_string_lossy().into_owned();
        let (r, _, e) = run(
            "a b c d\n",
            &["-n1", "-P1", "--process-slot-var=XSLOT", &script],
        );
        assert_eq!((r, e.as_str()), (0, ""));
        let (r, _, _) = run(
            "a b c d\n",
            &["-n1", "-P2", "--process-slot-var=XSLOT", &script],
        );
        assert_eq!(r, 123, "slot 1 must reach some child");
    }

    #[test]
    fn windows_command_line_budget_splits_batches() {
        assert_eq!(win_quoted_len("abc"), 3);
        assert_eq!(win_quoted_len(""), 2);
        assert_eq!(win_quoted_len("a b"), 5);
        assert_eq!(win_quoted_len("a\"b"), 6);
        assert_eq!(win_quoted_len("a b\\"), 7);
        // GNU accounting would keep 3000 x 10-byte items in one 30719
        // batch; quoting each (they hold a space) still fits under 32766
        // UTF-16 units only because the budget splits independently.
        let item = "\"a b c d e\" ";
        let input: String = item.repeat(4000);
        let (r, _, e) = run(&input, &["cmd", "/c", "exit", "0"]);
        assert_eq!((r, e.as_str()), (0, ""));
    }

    #[test]
    fn number_and_escape_parsers() {
        assert_eq!(strtol10(" 12"), Some(12));
        assert_eq!(strtol10("+3"), Some(3));
        assert_eq!(strtol10("-4"), Some(-4));
        assert_eq!(strtol10("4x"), None);
        assert_eq!(strtol10(""), None);
        assert_eq!(strtol10("99999999999999999999"), Some(i64::MAX));
        assert_eq!(strtoul(b"41", 16), (0x41, 2, false));
        assert_eq!(strtoul(b"0x41", 16), (0x41, 4, false));
        assert_eq!(strtoul(b"", 16), (0, 0, false));
        assert_eq!(strtoul(b"012", 8), (0o12, 3, false));
        assert_eq!(ntstatus_signal(0xC000_0005), 11);
        assert_eq!(ntstatus_signal(0xC000_013A), 2);
        assert_eq!(ntstatus_signal(0xC000_0409), 6);
        assert_eq!(ntstatus_signal(0xDEAD_BEEF), 9);
    }

    #[test]
    fn default_echo_preserves_non_utf8_bytes() {
        // Review regression: lossy_arg turned 0xff into U+FFFD before
        // the in-process echo saw it.
        let (r, out, _) = run_bytes(&[0xff, 0x00], &["-0"]);
        assert_eq!((r, out.as_slice()), (0, &[0xff, 0x0a][..]));
        // a "-n" item is an echo flag: operand printed without newline
        let mut input = b"-n\0".to_vec();
        input.extend_from_slice(&[0xff, 0x00]);
        let (r, out, _) = run_bytes(&input, &["-0"]);
        assert_eq!((r, out.as_slice()), (0, &[0xff][..]));
        // "-e" decodes the \x41 escape inside the second item; the 0xff
        // byte trailing it in the SAME item still passes raw
        let mut input = b"-e\0\\x41".to_vec();
        input.extend_from_slice(&[0xff, 0x00]);
        let (r, out, _) = run_bytes(&input, &["-0"]);
        assert_eq!((r, out.as_slice()), (0, &[0x41, 0xff, 0x0a][..]));
    }

    #[test]
    fn cmd_c_tail_caret_escapes_metacharacters() {
        // Review regression: `&`/`>` reached cmd as command syntax. The
        // payload holds no space, so Win32 quoting leaves it bare; only
        // the caret tail keeps `ver>INJ_MARK` literal. Child stdout is
        // not captured (children inherit process stdio), so the marker
        // file is the observable.
        let sc = ScratchCtx::new("xargs-cmdc");
        let payload = b"a&ver>INJ_MARK\0".to_vec();
        for cmd in ["cmd", "CMD", "cmd.exe"] {
            let (r, _, e) = run_in(&sc.dir, &payload, &["-0", cmd, "/c", "echo"]);
            assert_eq!(r, 0, "cmd={cmd} err={:?}", String::from_utf8_lossy(&e));
            assert!(
                !sc.path("INJ_MARK").exists(),
                "cmd={cmd} let cmd syntax through"
            );
        }
        // /C uppercase + a full-path cmd (COMSPEC) spelling
        let comspec = std::env::var("COMSPEC").unwrap_or_else(|_| "cmd.exe".into());
        let argv: Vec<String> = ["-0", comspec.as_str(), "/C", "echo"]
            .iter()
            .map(|s| s.to_string())
            .collect();
        let argrefs: Vec<&str> = argv.iter().map(String::as_str).collect();
        let (r, _, _) = run_in(&sc.dir, &payload, &argrefs);
        assert_eq!(r, 0);
        assert!(!sc.path("INJ_MARK").exists(), "fullpath cmd injected");
        // plain operands through the same tail stay literal too
        let (r, _, _) = run_in(&sc.dir, b"hello\0", &["-0", "cmd", "/c", "echo"]);
        assert_eq!(r, 0);
    }

    #[test]
    fn cmd_c_budget_counts_caret_escapes() {
        // 3000 '&' per item: caret-escaped cost is ~6001 UTF-16 units
        // each, so the 8191 cmd budget fits ONE item per batch -> six
        // execs under -t; naive quoting (~3001) would fit two.
        let item = "&".repeat(3000);
        let mut input = Vec::new();
        for _ in 0..6 {
            input.extend_from_slice(item.as_bytes());
            input.push(0);
        }
        let (r, _, e) = run_bytes(&input, &["-t", "-0", "cmd", "/c", "rem"]);
        assert_eq!(r, 0);
        let execs = String::from_utf8_lossy(&e).lines().count();
        assert_eq!(execs, 6, "stderr={}", String::from_utf8_lossy(&e));
        // a single item whose escaped form exceeds the cap is the
        // existing "Argument list too long" 126, not a silent overflow
        let mut input = "&".repeat(9000).into_bytes();
        input.push(0);
        let (r, _, e) = run_bytes(&input, &["-0", "cmd", "/c", "echo"]);
        assert_eq!(r, 126);
        let e = String::from_utf8_lossy(&e);
        assert!(e.contains("Argument list too long"), "err={e}");
    }

    #[test]
    fn cmd_escape_helpers() {
        assert!(is_cmd_image("cmd"));
        assert!(is_cmd_image("CMD.EXE"));
        assert!(is_cmd_image(r"C:\Windows\System32\cmd.exe"));
        assert!(is_cmd_image("/usr/bin/cmd"));
        assert!(!is_cmd_image("cmd.bat"));
        assert!(!is_cmd_image("mycmd.exe"));
        assert!(!is_cmd_image("echo"));
        assert_eq!(cmd_escape_arg("a&b c"), "a^&b^ c");
        assert_eq!(cmd_escape_arg("a^b\"c"), "a^^b^\"c");
        assert_eq!(cmd_escape_arg("plain"), "plain");
        assert_eq!(caret_escaped_len("a&b"), 4);
        assert_eq!(caret_escaped_len("ab"), 2);
    }
}
