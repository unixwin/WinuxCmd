//! `rm` — GNU coreutils semantics (probed against coreutils 9.4 on WSL).
//!
//! Owning GNU functions (coreutils 9.4, clean-room reference):
//! * src/rm.c `main`                    — option dispatch, the last-wins
//!   -f/-i/-I/--interactive state machine, missing-operand check, -I bulk
//!   prompt, aggregate exit status        (rm.c:206-369; getopt loop
//!   227-326; operand check 328-337; root setup 339-346; bulk 351-364)
//! * src/rm.c `long_opts`/`interactive_args` — the option table including
//!   the hidden `-presume-input-tty` long name and the
//!   never/no/none/once/always/yes argmatch table   (rm.c:61-96)
//! * src/rm.c `diagnose_leading_hyphen` — "Try 'rm ./-foo' ..." hint after
//!   a getopt error when a dash-named file exists   (rm.c:101-122)
//! * src/remove.c `rm`/`rm_fts`         — traversal driver: EISDIR/
//!   ENOTEMPTY dir gate, '.'/'..' refusal, ROOT_DEV_INO (preserve-root)
//!   check, --preserve-root=all mount check, descend/remove prompt split,
//!   --one-file-system skip               (remove.c:441-648)
//! * src/remove.c `prompt`              — write-protected detection, the
//!   dir_status-driven prompt kind selection     (remove.c:188-326)
//! * src/remove.c `excise`              — unlinkat + "removed %s"/"cannot
//!   remove %s" diagnostics                    (remove.c:390-433)
//! * src/remove.c `nonexistent_file_errno`/`ignorable_missing` — the
//!   EILSEQ/EINVAL/ENOENT/ENOTDIR -f suppression set  (remove.c:329-361)
//! * src/remove.c `mark_ancestor_dirs`  — a failed or declined child keeps
//!   its parent directories                     (remove.c:375-385)
//! * src/remove.c `get_dir_status`/`directory_status` — -1 empty, 0
//!   nonempty, positive errno                   (remove.c:164-176)
//!
//! Windows value model:
//! * Reparse points (symlinks AND junctions) are removed as links, never
//!   traversed — RemoveDirectoryW on a directory reparse point deletes the
//!   link itself, matching unlink(2) (#1433). A trailing separator
//!   dereferences the link like POSIX lstat("ldir/"): the operand then
//!   walks the TARGET, and its final rmdir reports ENOTDIR (probed:
//!   `rm -rf ldir/` empties the target, keeps link and dir, silent).
//!   RemoveDirectoryW on the link api path would wrongly unlink the
//!   reparse point, so the postorder step emits Not a directory instead.
//! * FILE_ATTRIBUTE_READONLY is the write-protected analog of GNU's
//!   faccessat(W_OK): it selects the write-protected prompt wording under
//!   -i or a tty stdin, and is pre-cleared before DeleteFileW since that
//!   call refuses what unlink(2) accepts (same shape as unlink.rs).
//! * Volume serial numbers stand in for st_dev: --one-file-system skips a
//!   child directory on a different volume, and --preserve-root=all
//!   refuses an operand whose own volume differs from its parent's.
//! * preserve-root floor: with -r, refuse `/` spellings (any separator
//!   run), drive/UNC filesystem roots, and any operand that resolves to
//!   the configured shell root — the accepted "native-root floor" verdict.
//! * Long paths and device names go through fs_win::api_path (the \\?\
//!   extended-spelling mapper, 1.x make_api_path_operand_w).

use std::ffi::OsString;
use std::io::{Read, Write};
use std::path::PathBuf;

use crate::argopt::{self, OptSpec};
use crate::fs_win;
use winux_core::{q, tr, usage_error, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('f'),
        long: "force",
        takes_value: false,
    },
    // -i/-I have no long forms in GNU (rm.c:61-82 long_opts); the bare `-h`
    // -> --help mapping still applies through argopt.
    OptSpec {
        short: Some('i'),
        long: "",
        takes_value: false,
    },
    OptSpec {
        short: Some('I'),
        long: "",
        takes_value: false,
    },
    OptSpec {
        short: Some('d'),
        long: "dir",
        takes_value: false,
    },
    OptSpec {
        short: Some('r'),
        long: "recursive",
        takes_value: false,
    },
    // -R is a short-only alias for -r (optstring "dfirvIR").
    OptSpec {
        short: Some('R'),
        long: "",
        takes_value: false,
    },
    OptSpec {
        short: Some('v'),
        long: "verbose",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "one-file-system",
        takes_value: false,
    },
    // Registered so `--no-preserve-root=x` surfaces GNU's
    // "option '--no-preserve-root' doesn't allow an argument" through the
    // shared parser; every bare spelling is stripped by the pre-scan.
    OptSpec {
        short: None,
        long: "no-preserve-root",
        takes_value: false,
    },
];

/// Every long-option name getopt_long knows (rm.c:61-82 plus the virtual
/// help/version), used by the pre-scan to resolve abbreviations for the
/// options argopt cannot model (optional_argument forms and the hidden
/// triple-dash name).
const ALL_LONGS: &[&str] = &[
    "-presume-input-tty",
    "dir",
    "force",
    "help",
    "interactive",
    "no-preserve-root",
    "one-file-system",
    "preserve-root",
    "recursive",
    "verbose",
    "version",
];

/// Per-invocation stdin classification, composed by the caller (the same
/// shape as tail's StdinMeta): the library cannot see whether the injected
/// `io.stdin` is a terminal, and GNU's RMI_SOMETIMES default prompts for
/// write-protected entries only on one (rm_option_init's `isatty`,
/// rm.c:198-199).
pub struct RmMeta {
    pub stdin_tty: bool,
}

impl RmMeta {
    /// Default for dispatch hosts that cannot report tty-ness: stdin is
    /// not a terminal, so the default mode never prompts.
    pub fn no_tty() -> Self {
        RmMeta { stdin_tty: false }
    }
}

#[derive(Clone, Copy, PartialEq)]
enum Interactive {
    Never,
    /// RMI_SOMETIMES: prompt only for write-protected entries, and only
    /// when stdin is a terminal (rm.c:192).
    Sometimes,
    Always,
}

struct Cfg {
    ignore_missing: bool,
    interactive: Interactive,
    prompt_once: bool,
    recursive: bool,
    remove_empty_dirs: bool,
    verbose: bool,
    one_file_system: bool,
    preserve_root: bool,
    preserve_all_root: bool,
    stdin_tty: bool,
}

/// Ordered option events for the last-wins state machine — the getopt
/// switch applies each in argv order (rm.c:227-326).
enum Event {
    Force,
    Always,
    Once,
    /// --interactive resolved to a WHEN mode.
    InteractiveMode(Interactive),
    /// Bare --preserve-root: restores the default after
    /// --no-preserve-root (last-wins).
    PreserveRoot,
    PreserveAllRoot,
    NoPreserveRoot,
    PresumeTty,
}

// ---------------------------------------------------------------------------
// Option pre-scan
// ---------------------------------------------------------------------------

/// getopt_long-style resolution of a `--` token's name part: exact match
/// first, then a prefix matching exactly one candidate.
enum LongRes {
    Hit(&'static str),
    None,
    Ambiguous,
}

fn resolve_long(name: &str) -> LongRes {
    if name.is_empty() {
        return LongRes::Ambiguous;
    }
    if let Some(exact) = ALL_LONGS.iter().find(|n| **n == name) {
        return LongRes::Hit(exact);
    }
    let mut hit = None;
    let mut count = 0usize;
    for cand in ALL_LONGS {
        if cand.starts_with(name) {
            count += 1;
            hit = Some(*cand);
        }
    }
    match count {
        0 => LongRes::None,
        1 => LongRes::Hit(hit.unwrap()),
        _ => LongRes::Ambiguous,
    }
}

/// GNU argmatch for --interactive=WHEN (rm.c:84-96): exact spelling wins;
/// a prefix matching spellings of a single mode resolves; a prefix
/// spanning modes is ambiguous; no match is invalid.
fn interactive_mode(word: &str) -> Result<Interactive, bool> {
    const NAMES: &[(&str, Interactive)] = &[
        ("never", Interactive::Never),
        ("no", Interactive::Never),
        ("none", Interactive::Never),
        ("once", Interactive::Sometimes),
        ("always", Interactive::Always),
        ("yes", Interactive::Always),
    ];
    if let Some((_, m)) = NAMES.iter().find(|(n, _)| *n == word) {
        return Ok(*m);
    }
    let mut modes: Vec<Interactive> = Vec::new();
    for (n, m) in NAMES {
        if n.starts_with(word) && !modes.contains(m) {
            modes.push(*m);
        }
    }
    match modes.len() {
        0 => Err(false), // invalid
        1 => Ok(modes[0]),
        _ => Err(true), // ambiguous
    }
}

/// A malformed stripped-option diagnostic; carries its argv index so GNU's
/// first-error-in-argv-order rule can pick between it and an argopt
/// diagnostic on a token that stayed in `rest`.
enum Pending {
    /// --interactive=<word> with no (false) or multiple (true) mode hits.
    InteractiveBad { ambiguous: bool, arg: String },
    /// --preserve-root=<word> that is not "all" ("" included).
    PreserveRootBad { arg: String },
    /// Abbreviated --no-preserve-root spelling.
    AbbrevNoPreserve,
    /// getopt-shaped "doesn't allow an argument" for the stripped
    /// ---presume-input-tty name (argopt never sees it).
    PresumeTakesArg,
    /// Bare `-h`: GNU rm has no -h (optstring "dfirvIR"), so it is an
    /// invalid option — argopt's bare-`-h` -> --help convention is
    /// overridden here.
    BadShortH,
}

struct Prescan {
    events: Vec<Event>,
    rest: Vec<OsString>,
    /// First malformed stripped option, in argv order.
    pending: Option<(usize, Pending)>,
    /// Original argv index of the first `--` token in `rest` that argopt
    /// would diagnose (unrecognized/ambiguous name, value on a flag), or
    /// of the first short cluster containing a char outside "dfirvIR" —
    /// i.e. where GNU's getopt would stop with an error.
    first_bad_rest: Option<usize>,
    /// Original argv index of the first help/version `--` token in `rest`
    /// (no `=value`): where GNU's getopt would exit 0.
    first_help: Option<usize>,
}

/// Scan argv for the last-wins prompt options plus the forms argopt cannot
/// model (--interactive[=WHEN], --preserve-root[=all], bare
/// --no-preserve-root spellings, ---presume-input-tty), and split argv
/// into (events, rest) for the shared parser.
fn prescan(args: &[OsString]) -> Prescan {
    let mut out = Prescan {
        events: Vec::new(),
        rest: Vec::new(),
        pending: None,
        first_bad_rest: None,
        first_help: None,
    };
    macro_rules! pending {
        ($i:expr, $p:expr) => {
            if out.pending.is_none() {
                out.pending = Some(($i, $p));
            }
        };
    }
    macro_rules! bad_rest {
        ($i:expr) => {
            if out.first_bad_rest.is_none() {
                out.first_bad_rest = Some($i);
            }
        };
    }
    macro_rules! push_rest {
        ($i:expr) => {
            out.rest.push(args[$i].clone());
        };
    }

    let mut i = 0usize;
    while i < args.len() {
        let s = args[i].to_string_lossy().into_owned();
        if s == "--" {
            out.rest.extend_from_slice(&args[i..]);
            break;
        }
        if let Some(long) = s.strip_prefix("--") {
            let (name, inline) = match long.split_once('=') {
                Some((n, v)) => (n, Some(v)),
                None => (long, None),
            };
            match resolve_long(name) {
                LongRes::Hit("interactive") => {
                    match inline {
                        // optional_argument: bare --interactive is 'always'
                        // and never eats the next token (rm.c:258-265).
                        None => out.events.push(Event::InteractiveMode(Interactive::Always)),
                        Some(v) => match interactive_mode(v) {
                            Ok(m) => out.events.push(Event::InteractiveMode(m)),
                            Err(amb) => pending!(
                                i,
                                Pending::InteractiveBad {
                                    ambiguous: amb,
                                    arg: v.to_string(),
                                }
                            ),
                        },
                    }
                }
                LongRes::Hit("preserve-root") => match inline {
                    None => out.events.push(Event::PreserveRoot),
                    Some("all") => out.events.push(Event::PreserveAllRoot),
                    // rm.c:299-309: anything else ("" included) errors.
                    Some(v) => pending!(i, Pending::PreserveRootBad { arg: v.to_string() }),
                },
                LongRes::Hit("no-preserve-root") => {
                    if inline.is_some() {
                        // leave for argopt: "doesn't allow an argument"
                        bad_rest!(i);
                        push_rest!(i);
                    } else if name == "no-preserve-root" {
                        out.events.push(Event::NoPreserveRoot);
                    } else {
                        // rm.c:292-297: abbreviated spellings are refused
                        pending!(i, Pending::AbbrevNoPreserve);
                    }
                }
                LongRes::Hit("-presume-input-tty") => {
                    if inline.is_some() {
                        pending!(i, Pending::PresumeTakesArg);
                    } else {
                        out.events.push(Event::PresumeTty);
                    }
                }
                LongRes::Hit("help") | LongRes::Hit("version") => {
                    if inline.is_some() {
                        // "--help=x": a flag given a value is a getopt
                        // error, not help.
                        bad_rest!(i);
                    } else if out.first_help.is_none() {
                        out.first_help = Some(i);
                    }
                    push_rest!(i);
                }
                LongRes::Hit("force") => {
                    // argopt validates; the event only feeds last-wins.
                    out.events.push(Event::Force);
                    if inline.is_some() {
                        bad_rest!(i);
                    }
                    push_rest!(i);
                }
                LongRes::Hit(_) => {
                    // a registered flag: an inline value is a getopt error
                    if inline.is_some() {
                        bad_rest!(i);
                    }
                    push_rest!(i);
                }
                LongRes::None | LongRes::Ambiguous => {
                    bad_rest!(i);
                    push_rest!(i);
                }
            }
            i += 1;
            continue;
        }
        if s.len() > 1 && s.starts_with('-') {
            // short cluster: optstring "dfirvIR" takes no values, so every
            // char is a flag or an error. A bare `-h` is GNU-invalid and
            // becomes a pending BadShortH (argopt would map it to --help);
            // inside a longer cluster 'h' stays an ordinary bad short.
            if s == "-h" {
                pending!(i, Pending::BadShortH);
            } else {
                for c in s[1..].chars() {
                    match c {
                        'f' => out.events.push(Event::Force),
                        'i' => out.events.push(Event::Always),
                        'I' => out.events.push(Event::Once),
                        'd' | 'r' | 'R' | 'v' => {}
                        _ => bad_rest!(i),
                    }
                }
            }
            push_rest!(i);
            i += 1;
            continue;
        }
        push_rest!(i);
        i += 1;
    }
    out
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

pub fn rm_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    rm_with_meta(args, io, ctx, &RmMeta::no_tty())
}

/// `rm_cmd` with the stdin tty classification injected (rm_option_init's
/// `isatty (STDIN_FILENO)`, rm.c:198). The driver composes it from the
/// process handle; the ABI and tests get [`RmMeta::no_tty`].
pub fn rm_with_meta(args: &[OsString], io: &mut Io, ctx: &Ctx, meta: &RmMeta) -> CmdResult {
    let scan = prescan(args);

    // argopt validates the residual argv with stderr captured so the
    // leading-hyphen hint can be inserted in GNU's line order: getopt
    // error, then "Try 'rm ./-foo' ...", then the try-help line
    // (diagnose_leading_hyphen, rm.c:101-122).
    let mut cap: Vec<u8> = Vec::new();
    let parsed = {
        let mut sub = Io {
            stdin: &mut *io.stdin,
            stdout: &mut *io.stdout,
            stderr: &mut cap,
        };
        argopt::parse("rm", &scan.rest, OPTS, &mut sub, ctx, 1)
    };
    let p = match parsed {
        Ok(p) => {
            let _ = io.stderr.write_all(&cap);
            p
        }
        Err(status) => {
            // GNU's getopt reports the FIRST bad token in argv order: the
            // pending stripped-option diagnostic wins when it precedes the
            // token argopt just rejected (first_bad_rest).
            let pending_first = match (&scan.pending, scan.first_bad_rest) {
                (Some((idx, _)), Some(first)) => *idx < first,
                (Some(_), None) => true,
                _ => false,
            };
            if pending_first {
                if let Some((_, p)) = &scan.pending {
                    emit_pending(io, ctx, args, p);
                }
            } else {
                emit_parse_error(io, ctx, &cap, args);
            }
            return Ok(status);
        }
    };

    // GNU exits at the first --help/--version getopt_long encounters
    // (#1573: argopt stops the scan at the record site). A pending
    // diagnostic only outranks it when earlier in argv order.
    if let Some(which) = p
        .flags
        .iter()
        .find(|f| f.as_str() == argopt::HELP || f.as_str() == argopt::VERSION)
    {
        let pending_first = match &scan.pending {
            Some((idx, _)) => scan.first_help.map(|h| *idx < h).unwrap_or(true),
            None => false,
        };
        if let (true, Some((_, pending))) = (pending_first, &scan.pending) {
            emit_pending(io, ctx, args, pending);
            return Ok(1);
        }
        return Ok(match which.as_str() {
            "help" => {
                write!(
                    io.stdout,
                    "{}{}",
                    RM_HELP_BODY,
                    crate::gnuhelp::footer("rm", "rm invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    crate::gnuhelp::coreutils_version(
                        "rm",
                        "Paul Rubin, David MacKenzie, Richard M. Stallman,\nand Jim Meyering"
                    )
                )?;
                0
            }
        });
    }
    if let Some((_, p)) = &scan.pending {
        emit_pending(io, ctx, args, p);
        return Ok(1);
    }

    // Last-wins state machine (rm.c:229-322).
    let mut cfg = Cfg {
        ignore_missing: false,
        interactive: Interactive::Sometimes,
        prompt_once: false,
        recursive: p.has("recursive") || p.has("R"),
        remove_empty_dirs: p.has("dir"),
        verbose: p.has("verbose"),
        one_file_system: p.has("one-file-system"),
        preserve_root: true,
        preserve_all_root: false,
        stdin_tty: meta.stdin_tty,
    };
    for e in &scan.events {
        match e {
            Event::Force => {
                cfg.ignore_missing = true;
                cfg.interactive = Interactive::Never;
                cfg.prompt_once = false;
            }
            Event::Always => {
                cfg.ignore_missing = false;
                cfg.interactive = Interactive::Always;
                cfg.prompt_once = false;
            }
            Event::Once => {
                cfg.ignore_missing = false;
                cfg.interactive = Interactive::Sometimes;
                cfg.prompt_once = true;
            }
            Event::InteractiveMode(m) => {
                match m {
                    // never does NOT clear ignore_missing (rm.c:268-270)
                    Interactive::Never => {
                        cfg.interactive = Interactive::Never;
                        cfg.prompt_once = false;
                    }
                    Interactive::Sometimes => {
                        cfg.ignore_missing = false;
                        cfg.interactive = Interactive::Sometimes;
                        cfg.prompt_once = true;
                    }
                    Interactive::Always => {
                        cfg.ignore_missing = false;
                        cfg.interactive = Interactive::Always;
                        cfg.prompt_once = false;
                    }
                }
            }
            Event::PreserveRoot => {
                cfg.preserve_root = true;
            }
            Event::PreserveAllRoot => {
                cfg.preserve_root = true;
                cfg.preserve_all_root = true;
            }
            Event::NoPreserveRoot => {
                cfg.preserve_root = false;
            }
            Event::PresumeTty => {
                cfg.stdin_tty = true;
            }
        }
    }

    if p.positionals.is_empty() {
        if cfg.ignore_missing {
            return Ok(0);
        }
        let _ = usage_error(
            ctx,
            io,
            "rm",
            &tr!(ctx, "command.rm.error.missing_operand", "missing operand"),
        );
        return Ok(1);
    }

    // -I / --interactive=once bulk prompt (rm.c:351-364).
    if cfg.prompt_once && (cfg.recursive || p.positionals.len() > 3) {
        let n = p.positionals.len();
        let (key, fallback) = match (cfg.recursive, n == 1) {
            (true, true) => (
                "command.rm.prompt.bulk_one_recursive",
                "remove {count} argument recursively? ",
            ),
            (true, false) => (
                "command.rm.prompt.bulk_recursive",
                "remove {count} arguments recursively? ",
            ),
            (false, true) => ("command.rm.prompt.bulk_one", "remove {count} argument? "),
            (false, false) => ("command.rm.prompt.bulk", "remove {count} arguments? "),
        };
        let count = n.to_string();
        write!(
            io.stderr,
            "rm: {}",
            trd(ctx, key, fallback, &[("count", &count)])
        )?;
        let _ = io.stderr.flush();
        if !yesno(io) {
            return Ok(0);
        }
    }

    let mut failed = false;
    for operand in &p.positionals {
        let name = operand.to_string_lossy().into_owned();
        if remove_operand(&name, &cfg, io, ctx) == Outcome::Error {
            failed = true;
        }
    }
    Ok(if failed { 1 } else { 0 })
}

/// Stripped-option diagnostics (rm.c argmatch / preserve-root classes).
/// The argmatch pair carries the valid-arguments block and try-help; the
/// preserve-root pair prints neither (error() exits before usage()); the
/// getopt-shaped cases take the leading-hyphen hint and try-help.
fn emit_pending(io: &mut Io, ctx: &Ctx, args: &[OsString], p: &Pending) {
    match p {
        Pending::InteractiveBad { ambiguous, arg } => {
            let (key, fallback) = if *ambiguous {
                (
                    "command.rm.error.ambiguous_interactive_arg",
                    "ambiguous argument '{arg}' for '--interactive'",
                )
            } else {
                (
                    "command.rm.error.invalid_interactive_arg",
                    "invalid argument '{arg}' for '--interactive'",
                )
            };
            let _ = writeln!(
                io.stderr,
                "rm: {}",
                trd(ctx, key, fallback, &[("arg", arg.as_str())])
            );
            let _ = writeln!(
                io.stderr,
                "{}",
                tr!(
                    ctx,
                    "command.rm.error.interactive_valid_args",
                    "Valid arguments are:\n  - 'never', 'no', 'none'\n  - 'once'\n  - 'always', 'yes'"
                )
            );
            let _ = writeln!(io.stderr, "Try 'rm --help' for more information.");
        }
        Pending::PreserveRootBad { arg } => {
            let _ = writeln!(
                io.stderr,
                "rm: {}",
                tr!(
                    ctx,
                    "command.rm.error.bad_preserve_root_arg",
                    "unrecognized --preserve-root argument: {arg}",
                    arg = q(arg)
                )
            );
        }
        Pending::AbbrevNoPreserve => {
            let _ = writeln!(
                io.stderr,
                "rm: {}",
                tr!(
                    ctx,
                    "command.rm.error.abbrev_no_preserve_root",
                    "you may not abbreviate the --no-preserve-root option"
                )
            );
        }
        Pending::PresumeTakesArg => {
            // getopt prints the long name as stored in long_opts — one
            // leading dash, so the diagnostic shows two.
            let _ = writeln!(
                io.stderr,
                "rm: option '--presume-input-tty' doesn't allow an argument"
            );
            emit_hint_and_tryhelp(io, ctx, args);
        }
        Pending::BadShortH => {
            let _ = writeln!(io.stderr, "rm: invalid option -- 'h'");
            emit_hint_and_tryhelp(io, ctx, args);
        }
    }
}

/// The getopt-error tail: diagnose_leading_hyphen's hint when a dash-named
/// file exists, then the try-help line.
fn emit_hint_and_tryhelp(io: &mut Io, ctx: &Ctx, args: &[OsString]) {
    if let Some(hint) = hyphen_hint(ctx, args) {
        let _ = writeln!(
            io.stderr,
            "{}",
            tr!(
                ctx,
                "command.rm.error.leading_hyphen_hint",
                "Try 'rm ./{name}' to remove the file {quoted}.",
                name = hint.0,
                quoted = hint.1
            )
        );
    }
    let _ = writeln!(io.stderr, "Try 'rm --help' for more information.");
}

/// argopt already wrote its diagnostics into `cap`; re-emit them with the
/// leading-hyphen hint inserted before the try-help line
/// (diagnose_leading_hyphen, rm.c:101-122). The hint names the first argv
/// token shaped like an option that is also an existing file.
fn emit_parse_error(io: &mut Io, ctx: &Ctx, cap: &[u8], args: &[OsString]) {
    let text = String::from_utf8_lossy(cap);
    let hint = hyphen_hint(ctx, args);
    let marker = "Try 'rm --help'";
    match (text.find(marker), hint) {
        (Some(pos), Some(hint)) => {
            let _ = io.stderr.write_all(&cap[..pos]);
            let _ = writeln!(
                io.stderr,
                "{}",
                tr!(
                    ctx,
                    "command.rm.error.leading_hyphen_hint",
                    "Try 'rm ./{name}' to remove the file {quoted}.",
                    name = hint.0,
                    quoted = hint.1
                )
            );
            let _ = io.stderr.write_all(&cap[pos..]);
        }
        _ => {
            let _ = io.stderr.write_all(cap);
        }
    }
}

/// First argv token shaped like an option (`-x`, not bare `-`/`--`) whose
/// name exists as a file — diagnose_leading_hyphen's argv scan. Returns
/// (typed name, GNU-quoted name).
fn hyphen_hint(ctx: &Ctx, args: &[OsString]) -> Option<(String, String)> {
    for a in args {
        let s = a.to_string_lossy().into_owned();
        if s.len() > 1 && s.starts_with('-') && s != "--" {
            if fs_win::file_meta(ctx, &s, true).is_ok() {
                return Some((s.clone(), q(&s)));
            }
        }
    }
    None
}

// ---------------------------------------------------------------------------
// Answers and diagnostics
// ---------------------------------------------------------------------------

/// yesno(): read one line from the injected stdin; a leading 'y'/'Y'
/// accepts, anything else (including EOF and read failure) declines.
fn yesno(io: &mut Io) -> bool {
    let mut buf = [0u8; 1];
    let mut first: Option<u8> = None;
    loop {
        match io.stdin.read(&mut buf) {
            Ok(0) | Err(_) => break,
            Ok(_) => {
                if buf[0] == b'\n' {
                    break;
                }
                if first.is_none() {
                    first = Some(buf[0]);
                }
            }
        }
    }
    matches!(first, Some(b'y') | Some(b'Y'))
}

fn cannot_remove(io: &mut Io, ctx: &Ctx, name: &str, reason: &str) {
    let _ = writeln!(
        io.stderr,
        "rm: {}",
        tr!(
            ctx,
            "command.rm.error.cannot_remove",
            "cannot remove {operand}: {reason}",
            operand = q(name),
            reason = reason
        )
    );
}

// ---------------------------------------------------------------------------
// Outcome model (GNU RM_status)
// ---------------------------------------------------------------------------

#[derive(Clone, Copy, PartialEq)]
enum Outcome {
    /// Removed or silently skipped under -f.
    Ok,
    /// The user declined a prompt or a child was declined/kept: the path
    /// stays but the aggregate status is still success.
    Declined,
    /// A real failure was diagnosed; the aggregate status becomes 1.
    Error,
}

impl Outcome {
    fn merge(&mut self, other: Outcome) {
        if other == Outcome::Error {
            *self = Outcome::Error;
        } else if other == Outcome::Declined && *self == Outcome::Ok {
            *self = Outcome::Declined;
        }
    }
}

/// tr! for the dynamically-keyed prompt/noun families (the macro needs
/// literal keys): render the English fallback through the same template
/// machinery, then let the catalog template win when present.
fn trd(ctx: &Ctx, key: &str, fallback_template: &str, bindings: &[(&str, &str)]) -> String {
    let fallback =
        winux_core::i18n::format_with_fallback(Some(fallback_template), String::new(), bindings);
    winux_core::i18n::format_with_fallback(ctx.host.message(key), fallback, bindings)
}

/// The noun GNU's file_type() prints in typed prompts (remove.c
/// file_type + the lstat inside prompt(), remove.c:285-292).
fn noun_for(meta: &fs_win::FileMeta, is_link: bool, is_fifo: bool, ctx: &Ctx) -> String {
    let (key, fallback) = if is_link {
        ("command.rm.noun.symbolic_link", "symbolic link")
    } else if is_fifo {
        ("command.rm.noun.fifo", "fifo")
    } else if meta.is_dir {
        ("command.rm.noun.directory", "directory")
    } else if meta.character_device {
        (
            "command.rm.noun.character_special",
            "character special file",
        )
    } else if meta.size == 0 {
        ("command.rm.noun.regular_empty_file", "regular empty file")
    } else {
        ("command.rm.noun.regular_file", "regular file")
    };
    trd(ctx, key, fallback, &[])
}

/// Emit `rm: <prompt>` and read the answer.
fn ask(io: &mut Io, ctx: &Ctx, key: &'static str, fallback: &'static str, operand: &str) -> bool {
    let qo = q(operand);
    let text = trd(ctx, key, fallback, &[("operand", &qo)]);
    let _ = write!(io.stderr, "rm: {text}");
    let _ = io.stderr.flush();
    yesno(io)
}

/// The `remove [write-protected] <type> '<name>'?` prompt family
/// (remove.c:299-309).
fn ask_typed(io: &mut Io, ctx: &Ctx, wp: bool, noun: &str, operand: &str) -> bool {
    let (key, fallback) = if wp {
        (
            "command.rm.prompt.remove_wp",
            "remove write-protected {kind} {operand}? ",
        )
    } else {
        ("command.rm.prompt.remove", "remove {kind} {operand}? ")
    };
    let qo = q(operand);
    let text = trd(ctx, key, fallback, &[("kind", noun), ("operand", &qo)]);
    let _ = write!(io.stderr, "rm: {text}");
    let _ = io.stderr.flush();
    yesno(io)
}

fn ask_descend(io: &mut Io, ctx: &Ctx, wp: bool, operand: &str) -> bool {
    if wp {
        ask(
            io,
            ctx,
            "command.rm.prompt.descend_wp",
            "descend into write-protected directory {operand}? ",
            operand,
        )
    } else {
        ask(
            io,
            ctx,
            "command.rm.prompt.descend",
            "descend into directory {operand}? ",
            operand,
        )
    }
}

fn ask_remove_dir(io: &mut Io, ctx: &Ctx, operand: &str) -> bool {
    ask(
        io,
        ctx,
        "command.rm.prompt.remove_dir",
        "remove directory {operand}? ",
        operand,
    )
}

// ---------------------------------------------------------------------------
// Path / attribute plumbing
// ---------------------------------------------------------------------------

/// Trailing `/` or `\` — POSIX lstat reads them as directory demands and
/// Win32 attribute probes reject them on non-directories, so probes and
/// filesystem calls work on the stripped name while diagnostics keep the
/// original spelling (the 1.x had_trailing_separator model).
fn strip_seps(name: &str) -> &str {
    name.trim_end_matches(['/', '\\'])
}

fn api(ctx: &Ctx, operand: &str) -> PathBuf {
    PathBuf::from(fs_win::api_path(ctx, operand))
}

fn io_reason(e: &std::io::Error) -> &'static str {
    match e.raw_os_error() {
        Some(c) => fs_win::win32_reason(c as u32),
        None => crate::fsutil::reason(e),
    }
}

/// nonexistent_file_errno classes (remove.c:329-354): ENOENT, ENOTDIR,
/// EINVAL, EILSEQ — on Windows the not-found / invalid-name / ENOTDIR
/// code family.
fn nonexistent_code(code: u32) -> bool {
    // ERROR_FILE_NOT_FOUND / ERROR_PATH_NOT_FOUND / ERROR_INVALID_NAME /
    // ERROR_CANT_ACCESS_FILE (and its 2062 alias) / ERROR_DIRECTORY
    // (ENOTDIR) / ERROR_INVALID_PARAMETER (EINVAL class).
    matches!(code, 2 | 3 | 123 | 1920 | 2062 | 267 | 87)
}

/// Clear FILE_ATTRIBUTE_READONLY so DeleteFileW/RemoveDirectoryW accept
/// what unlink(2)/rmdir(2) accept (unlink.rs's pre-clear, mirroring the
/// 1.x clear_readonly).
fn clear_readonly(api: &PathBuf) {
    if let Ok(md) = std::fs::metadata(api) {
        let mut perms = md.permissions();
        if perms.readonly() {
            perms.set_readonly(false);
            let _ = std::fs::set_permissions(api, perms);
        }
    }
}

/// directory_status (remove.c:164-176): -1 empty / 0 nonempty / errno.
enum DirStatus {
    Empty,
    NonEmpty,
    /// Enumeration refused; the stored reason becomes the diagnostic.
    Inaccessible(&'static str),
}

fn dir_status(api: &PathBuf) -> DirStatus {
    match std::fs::read_dir(api) {
        Ok(mut it) => {
            if it.next().is_some() {
                DirStatus::NonEmpty
            } else {
                DirStatus::Empty
            }
        }
        Err(e) => DirStatus::Inaccessible(io_reason(&e)),
    }
}

/// Lexical last component on either separator (GNU last_component sees
/// `/`; Windows adds `\`).
fn last_component(name: &str) -> &str {
    let stripped = strip_seps(name);
    stripped.rsplit(['/', '\\']).next().unwrap_or(stripped)
}

fn is_dot_or_dotdot(name: &str) -> bool {
    let last = last_component(name);
    last == "." || last == ".."
}

#[derive(PartialEq)]
enum RootKind {
    /// A pure separator run: prints `on '/'` (except exactly two).
    Slash,
    /// Drive/UNC root or the configured shell root:
    /// `on 'X' (same as '/')`.
    Same,
}

/// The preserve-root floor (remove.c:483-488's ROOT_DEV_INO_CHECK plus
/// the accepted native-root floor): `/` spellings, filesystem roots, and
/// the configured shell root are refused under -r.
fn root_kind(ctx: &Ctx, name: &str, stripped: &str) -> Option<RootKind> {
    if stripped.is_empty() {
        // GNU collapses 3+ separators to '/' and reports '//' verbatim.
        let seps = name.chars().count();
        return Some(if seps == 2 {
            RootKind::Same
        } else {
            RootKind::Slash
        });
    }
    let resolved = api(ctx, stripped);
    if crate::fsutil::is_preserve_root_operand(ctx, name, &resolved) {
        return Some(RootKind::Same);
    }
    if let Some(root) = winux_core::path::shell_root(ctx.env) {
        let want = root.replace('/', "\\");
        let got = winux_core::strip_verbatim(resolved)
            .to_string_lossy()
            .replace('/', "\\");
        if got
            .trim_end_matches('\\')
            .eq_ignore_ascii_case(want.trim_end_matches('\\'))
        {
            return Some(RootKind::Same);
        }
    }
    None
}

fn refuse_root(io: &mut Io, ctx: &Ctx, name: &str, kind: RootKind) {
    if kind == RootKind::Slash {
        let _ = writeln!(
            io.stderr,
            "rm: {}",
            tr!(
                ctx,
                "command.rm.error.dangerous_root",
                "it is dangerous to operate recursively on {operand}",
                operand = "'/'"
            )
        );
    } else {
        let _ = writeln!(
            io.stderr,
            "rm: {}",
            tr!(
                ctx,
                "command.rm.error.dangerous_root_same",
                "it is dangerous to operate recursively on {operand} (same as '/')",
                operand = q(name)
            )
        );
    }
    let _ = writeln!(
        io.stderr,
        "rm: {}",
        tr!(
            ctx,
            "command.rm.error.no_preserve_hint",
            "use --no-preserve-root to override this failsafe"
        )
    );
}

// ---------------------------------------------------------------------------
// Removal
// ---------------------------------------------------------------------------

/// One command-line operand: the rm_fts FTS_D/leaf dispatch for a
/// root-level entry.
fn remove_operand(name: &str, cfg: &Cfg, io: &mut Io, ctx: &Ctx) -> Outcome {
    // #1650: an empty operand is a real operand — its lstat fails ENOENT
    // (remove.c:229-236's "happens, e.g., with 'rm '''"), never
    // "missing operand"; -f ignores it.
    if name.is_empty() {
        if cfg.ignore_missing {
            return Outcome::Ok;
        }
        cannot_remove(io, ctx, name, "No such file or directory");
        return Outcome::Error;
    }

    let stripped = strip_seps(name);
    let had_sep = stripped.len() != name.len();

    // POSIX resolves a trailing "." / ".." component inside the path
    // parser: "e/.." is a directory exactly when "e" is one. The \\?\ api
    // path disables Win32's own '..' folding, so probe the operand with
    // the dot component removed (following links: "link/.." resolves
    // through the link target).
    let last = last_component(stripped);
    let dot_tail = last == "." || last == "..";
    let probe = if dot_tail {
        let base = strip_seps(&stripped[..stripped.len() - last.len()]);
        if base.is_empty() {
            "."
        } else {
            base
        }
    } else {
        stripped
    };

    let lstat = if stripped.is_empty() {
        None
    } else {
        match fs_win::file_meta(ctx, probe, !dot_tail) {
            Ok(m) => Some(m),
            Err(code) => {
                if cfg.ignore_missing && nonexistent_code(code) {
                    return Outcome::Ok;
                }
                cannot_remove(io, ctx, name, fs_win::win32_reason(code));
                return Outcome::Error;
            }
        }
    };

    if dot_tail {
        match &lstat {
            Some(m) if m.is_dir => {
                return remove_dir_operand(name, stripped, true, m.clone(), cfg, io, ctx);
            }
            Some(_) => {
                // "f/." "f/..": traversal through a non-directory.
                if cfg.ignore_missing {
                    return Outcome::Ok;
                }
                cannot_remove(io, ctx, name, "Not a directory");
                return Outcome::Error;
            }
            None => {}
        }
    }

    // Resolve the operand kind. `sep_link` is the trailing-separator
    // deref of a dir reparse point: POSIX then treats the operand as the
    // TARGET directory, and the final rmdir reports ENOTDIR.
    let (is_dir, is_link, sep_link, meta) = match &lstat {
        // All-separator operand ("/", "//"): a directory spelling; the
        // recursive root check below handles it, the non-recursive gate
        // reports Is a directory.
        None => (true, false, false, fs_win::FileMeta::default()),
        Some(m) if had_sep && m.is_symlink => match fs_win::file_meta(ctx, stripped, false) {
            Ok(t) if t.is_dir => (true, false, true, t),
            Ok(_) => {
                if cfg.ignore_missing {
                    return Outcome::Ok;
                }
                cannot_remove(io, ctx, name, "Not a directory");
                return Outcome::Error;
            }
            Err(code) => {
                if cfg.ignore_missing && nonexistent_code(code) {
                    return Outcome::Ok;
                }
                cannot_remove(io, ctx, name, fs_win::win32_reason(code));
                return Outcome::Error;
            }
        },
        Some(m) if had_sep && !m.is_dir => {
            // "f/" on a non-directory: ENOTDIR, ignorable under -f.
            if cfg.ignore_missing {
                return Outcome::Ok;
            }
            cannot_remove(io, ctx, name, "Not a directory");
            return Outcome::Error;
        }
        Some(m) => (m.is_dir, m.is_symlink, false, m.clone()),
    };

    if is_link {
        return remove_leaf(name, stripped, &meta, cfg, io, ctx, true);
    }
    if !is_dir {
        return remove_leaf(name, stripped, &meta, cfg, io, ctx, false);
    }
    remove_dir_operand(name, stripped, sep_link, meta, cfg, io, ctx)
}

/// The directory half of rm_fts's FTS_D handling (remove.c:447-544): the
/// non-recursive gate, the root-level refusals, the descend prompt, then
/// the -d excise or the recursive descent.
#[allow(clippy::too_many_arguments)]
fn remove_dir_operand(
    name: &str,
    stripped: &str,
    sep_link: bool,
    meta: fs_win::FileMeta,
    cfg: &Cfg,
    io: &mut Io,
    ctx: &Ctx,
) -> Outcome {
    // For an all-separator operand keep the original spelling: api_path
    // maps "/" through the shell-tree fold (the scratch shell root or the
    // cwd's drive root), while api("") would resolve to the cwd.
    let api_p = api(ctx, if stripped.is_empty() { name } else { stripped });
    let status = dir_status(&api_p);

    // FTS_D preorder gate (remove.c:448-462): non-recursive directories
    // are refused unless -d applies to this dir's fill status.
    if !cfg.recursive {
        let pass = cfg.remove_empty_dirs && !matches!(status, DirStatus::NonEmpty);
        if !pass {
            let reason = if cfg.remove_empty_dirs {
                "Directory not empty"
            } else {
                "Is a directory"
            };
            cannot_remove(io, ctx, name, reason);
            return Outcome::Error;
        }
    }

    // Root-level-only checks (remove.c:464-522): '.'/'..' refusal, then
    // preserve-root, then --preserve-root=all.
    if is_dot_or_dotdot(stripped) {
        let _ = writeln!(
            io.stderr,
            "rm: {}",
            tr!(
                ctx,
                "command.rm.error.refusing_dot",
                "refusing to remove '.' or '..' directory: skipping {operand}",
                operand = q(name)
            )
        );
        return Outcome::Error;
    }

    if cfg.recursive && cfg.preserve_root {
        if let Some(kind) = root_kind(ctx, name, stripped) {
            refuse_root(io, ctx, name, kind);
            return Outcome::Error;
        }
    }

    if cfg.recursive && cfg.preserve_all_root {
        let parent_probe = format!("{stripped}/..");
        match fs_win::file_meta(ctx, &parent_probe, false) {
            Err(_) => {
                let _ = writeln!(
                    io.stderr,
                    "rm: {}",
                    tr!(
                        ctx,
                        "command.rm.error.failed_stat_parent",
                        "failed to stat {parent}: skipping {operand}",
                        parent = q(&parent_probe),
                        operand = q(stripped)
                    )
                );
                return Outcome::Error;
            }
            Ok(pm) if pm.volume_serial != meta.volume_serial => {
                let _ = writeln!(
                    io.stderr,
                    "rm: {}",
                    tr!(
                        ctx,
                        "command.rm.error.skipping_device",
                        "skipping {operand}, since it's on a different device",
                        operand = q(name)
                    )
                );
                let _ = writeln!(
                    io.stderr,
                    "rm: {}",
                    tr!(
                        ctx,
                        "command.rm.error.preserve_all_effect",
                        "and --preserve-root=all is in effect"
                    )
                );
                return Outcome::Error;
            }
            _ => {}
        }
    }

    // Descend/remove prompt selection (remove.c:prompt, 188-326). The
    // write-protected gate: !ignore_missing && (Always || tty) && !link.
    let wp_dir = !cfg.ignore_missing
        && (cfg.interactive == Interactive::Always || cfg.stdin_tty)
        && meta.read_only;

    if cfg.interactive == Interactive::Always {
        match status {
            DirStatus::Empty => {
                if !ask_remove_dir(io, ctx, name) {
                    return Outcome::Declined;
                }
                // RM_USER_ACCEPTED + DS_EMPTY: excise at preorder, never
                // descend (remove.c:529-536).
                return excise_dir(name, &api_p, sep_link, cfg, io, ctx);
            }
            DirStatus::NonEmpty => {
                if !ask_descend(io, ctx, wp_dir, name) {
                    return Outcome::Declined;
                }
            }
            DirStatus::Inaccessible(reason) => {
                if !cfg.remove_empty_dirs {
                    cannot_remove(io, ctx, name, reason);
                    return Outcome::Error;
                }
                // -d on an unreadable dir: PA_DESCEND returns OK silently
                // (remove.c:270-276); the removal prompt happens postorder.
            }
        }
    } else if cfg.interactive == Interactive::Sometimes && wp_dir {
        match status {
            DirStatus::NonEmpty => {
                if !ask_descend(io, ctx, true, name) {
                    return Outcome::Declined;
                }
            }
            DirStatus::Empty => {
                if !ask_typed(io, ctx, true, &noun_for(&meta, false, false, ctx), name) {
                    return Outcome::Declined;
                }
            }
            DirStatus::Inaccessible(_) => {}
        }
    }

    if !cfg.recursive {
        // -d reached: attempt the empty-dir removal.
        return excise_dir(name, &api_p, sep_link, cfg, io, ctx);
    }

    remove_tree(
        name,
        stripped,
        &api_p,
        sep_link,
        meta.volume_serial,
        cfg,
        io,
        ctx,
    )
}

/// Leaf removal (non-dir or link): prompt, then unlink (remove.c's
/// FTS_F/FTS_SL bucket -> prompt -> excise).
fn remove_leaf(
    name: &str,
    stripped: &str,
    meta: &fs_win::FileMeta,
    cfg: &Cfg,
    io: &mut Io,
    ctx: &Ctx,
    is_link: bool,
) -> Outcome {
    let api_p = api(ctx, stripped);
    remove_leaf_common(name, &api_p, meta, is_link, cfg, io, ctx)
}

/// Shared leaf path for an operand and an enumerated child (whose api
/// path is already known). `is_link` selects RemoveDirectoryW for dir
/// reparse points.
fn remove_leaf_common(
    name: &str,
    api_p: &PathBuf,
    meta: &fs_win::FileMeta,
    is_link: bool,
    cfg: &Cfg,
    io: &mut Io,
    ctx: &Ctx,
) -> Outcome {
    let is_fifo = fs_win::is_winux_fifo_path(&api_p.to_string_lossy());
    let noun = noun_for(meta, is_link, is_fifo, ctx);
    // remove.c:211-217: write_protected only when not -f, only under
    // Always or a tty stdin, and never for a link.
    let wp = !is_link
        && !cfg.ignore_missing
        && (cfg.interactive == Interactive::Always || cfg.stdin_tty)
        && meta.read_only;

    if cfg.interactive == Interactive::Always {
        if !ask_typed(io, ctx, wp, &noun, name) {
            return Outcome::Declined;
        }
    } else if wp && cfg.interactive == Interactive::Sometimes && cfg.stdin_tty {
        if !ask_typed(io, ctx, true, &noun, name) {
            return Outcome::Declined;
        }
    }

    excise_leaf(name, api_p, is_link && meta.is_dir, cfg, io, ctx)
}

fn excise_leaf(
    name: &str,
    api_p: &PathBuf,
    dir_link: bool,
    cfg: &Cfg,
    io: &mut Io,
    ctx: &Ctx,
) -> Outcome {
    clear_readonly(api_p);
    // RemoveDirectoryW on a reparse point removes the link itself —
    // the unlink(2) shape for directory links (#1433).
    let result = if dir_link {
        std::fs::remove_dir(api_p)
    } else {
        std::fs::remove_file(api_p)
    };
    match result {
        Ok(()) => {
            if cfg.verbose {
                let _ = writeln!(
                    io.stdout,
                    "{}",
                    tr!(
                        ctx,
                        "command.rm.verbose.removed",
                        "removed {operand}",
                        operand = q(name)
                    )
                );
            }
            Outcome::Ok
        }
        Err(e) => {
            let code = e.raw_os_error().unwrap_or_default() as u32;
            if cfg.ignore_missing && nonexistent_code(code) {
                return Outcome::Ok;
            }
            cannot_remove(io, ctx, name, io_reason(&e));
            Outcome::Error
        }
    }
}

/// rmdir attempt. `sep_link` operands are the POSIX ENOTDIR case: GNU's
/// unlinkat("ldir/", AT_REMOVEDIR) fails, while RemoveDirectoryW on the
/// Windows link would wrongly succeed, so report the GNU outcome.
fn excise_dir(
    name: &str,
    api_p: &PathBuf,
    sep_link: bool,
    cfg: &Cfg,
    io: &mut Io,
    ctx: &Ctx,
) -> Outcome {
    if sep_link {
        if cfg.ignore_missing {
            return Outcome::Ok;
        }
        cannot_remove(io, ctx, name, "Not a directory");
        return Outcome::Error;
    }
    clear_readonly(api_p);
    match std::fs::remove_dir(api_p) {
        Ok(()) => {
            if cfg.verbose {
                let _ = writeln!(
                    io.stdout,
                    "{}",
                    tr!(
                        ctx,
                        "command.rm.verbose.removed_dir",
                        "removed directory {operand}",
                        operand = q(name)
                    )
                );
            }
            Outcome::Ok
        }
        Err(e) => {
            let code = e.raw_os_error().unwrap_or_default() as u32;
            if cfg.ignore_missing && nonexistent_code(code) {
                return Outcome::Ok;
            }
            cannot_remove(io, ctx, name, io_reason(&e));
            Outcome::Error
        }
    }
}

/// Recursive post-order traversal for an already-accepted nonempty dir
/// operand (rm_fts's FTS_D -> children -> FTS_DP shape): one interleaved
/// enumeration pass, files excised at visit, each dir descended after its
/// own descend prompt and removed at postorder. mark_ancestor_dirs: any
/// declined or failed child keeps this dir — its postorder prompt and
/// rmdir are skipped.
#[allow(clippy::too_many_arguments)]
fn remove_tree(
    name: &str,
    join_base: &str,
    api_p: &PathBuf,
    sep_link: bool,
    root_vol: u32,
    cfg: &Cfg,
    io: &mut Io,
    ctx: &Ctx,
) -> Outcome {
    let entries = match std::fs::read_dir(api_p) {
        Ok(rd) => rd,
        Err(e) => {
            // FTS_DNR-ish: enumeration refused at descend time.
            cannot_remove(io, ctx, name, io_reason(&e));
            return Outcome::Error;
        }
    };

    let mut aggregate = Outcome::Ok;
    for entry in entries.flatten() {
        let entry_name = entry.file_name().to_string_lossy().into_owned();
        let disp = format!("{join_base}/{entry_name}");
        let eapi = entry.path();
        let emd = entry.metadata();
        let attrs = emd.map(|m| file_attrs(&m)).unwrap_or(0);
        let is_link = attrs & FILE_ATTRIBUTE_REPARSE_POINT != 0;
        let is_dir = attrs & FILE_ATTRIBUTE_DIRECTORY != 0;

        if is_link || !is_dir {
            let child_meta = std::fs::symlink_metadata(&eapi)
                .map(|m| meta_from_std(&m))
                .unwrap_or_default();
            let o = remove_leaf_common(&disp, &eapi, &child_meta, is_link, cfg, io, ctx);
            aggregate.merge(o);
            continue;
        }

        // Real directory child.
        let child_meta = fs_win::file_meta(ctx, &disp, false).unwrap_or_default();

        // --one-file-system (remove.c:556-568): a child dir whose volume
        // differs from the ROOT operand's (fts->fts_dev) is skipped,
        // marked, and fails the run — its subtree is never entered.
        if cfg.one_file_system && child_meta.volume_serial != root_vol {
            let _ = writeln!(
                io.stderr,
                "rm: {}",
                tr!(
                    ctx,
                    "command.rm.error.skipping_device",
                    "skipping {operand}, since it's on a different device",
                    operand = q(&disp)
                )
            );
            aggregate.merge(Outcome::Error);
            continue;
        }

        // Preorder dir handling mirrors the operand path: descend prompt
        // under Always (or the write-protected descend under Sometimes),
        // the empty-dir remove prompt, the inaccessible-dir error.
        let child_status = dir_status(&eapi);
        let wp_dir = !cfg.ignore_missing
            && (cfg.interactive == Interactive::Always || cfg.stdin_tty)
            && child_meta.read_only;

        if cfg.interactive == Interactive::Always {
            match child_status {
                DirStatus::Empty => {
                    if !ask_remove_dir(io, ctx, &disp) {
                        aggregate.merge(Outcome::Declined);
                        continue;
                    }
                    aggregate.merge(excise_dir(&disp, &eapi, false, cfg, io, ctx));
                    continue;
                }
                DirStatus::NonEmpty => {
                    if !ask_descend(io, ctx, wp_dir, &disp) {
                        aggregate.merge(Outcome::Declined);
                        continue;
                    }
                }
                DirStatus::Inaccessible(reason) => {
                    if !cfg.remove_empty_dirs {
                        cannot_remove(io, ctx, &disp, reason);
                        aggregate.merge(Outcome::Error);
                        continue;
                    }
                }
            }
        } else if cfg.interactive == Interactive::Sometimes && wp_dir {
            match child_status {
                DirStatus::NonEmpty => {
                    if !ask_descend(io, ctx, true, &disp) {
                        aggregate.merge(Outcome::Declined);
                        continue;
                    }
                }
                DirStatus::Empty => {
                    if !ask_typed(io, ctx, true, "directory", &disp) {
                        aggregate.merge(Outcome::Declined);
                        continue;
                    }
                }
                DirStatus::Inaccessible(_) => {}
            }
        }

        let sub = remove_tree(&disp, &disp, &eapi, false, root_vol, cfg, io, ctx);
        aggregate.merge(sub);
    }

    if aggregate != Outcome::Ok {
        return aggregate;
    }

    // Postorder: PA_REMOVE_DIR prompt under -i, then the rmdir.
    if cfg.interactive == Interactive::Always && !ask_remove_dir(io, ctx, name) {
        return Outcome::Declined;
    }
    excise_dir(name, api_p, sep_link, cfg, io, ctx)
}

/// Attributes word for a directory-entry snapshot (DirEntry::metadata is
/// the no-follow probe).
#[cfg(windows)]
fn file_attrs(m: &std::fs::Metadata) -> u32 {
    use std::os::windows::fs::MetadataExt;
    m.file_attributes()
}

#[cfg(not(windows))]
fn file_attrs(m: &std::fs::Metadata) -> u32 {
    let mut a = 0u32;
    if m.is_dir() {
        a |= FILE_ATTRIBUTE_DIRECTORY;
    }
    if m.file_type().is_symlink() {
        a |= FILE_ATTRIBUTE_REPARSE_POINT;
    }
    a
}

/// std metadata -> the FileMeta view rm consumes (lstat-shaped).
#[cfg(windows)]
fn meta_from_std(m: &std::fs::Metadata) -> fs_win::FileMeta {
    let a = file_attrs(m);
    fs_win::FileMeta {
        is_symlink: a & FILE_ATTRIBUTE_REPARSE_POINT != 0,
        is_dir: a & FILE_ATTRIBUTE_DIRECTORY != 0,
        read_only: a & FILE_ATTRIBUTE_READONLY != 0,
        character_device: a & FILE_ATTRIBUTE_DEVICE != 0,
        size: m.len(),
        ..Default::default()
    }
}

#[cfg(not(windows))]
fn meta_from_std(m: &std::fs::Metadata) -> fs_win::FileMeta {
    fs_win::FileMeta {
        is_symlink: m.file_type().is_symlink(),
        is_dir: m.is_dir(),
        read_only: m.permissions().readonly(),
        size: m.len(),
        ..Default::default()
    }
}

#[cfg(windows)]
use std::os::windows::fs::MetadataExt as _;

const FILE_ATTRIBUTE_READONLY: u32 = 0x01;
const FILE_ATTRIBUTE_DIRECTORY: u32 = 0x10;
const FILE_ATTRIBUTE_DEVICE: u32 = 0x40;
const FILE_ATTRIBUTE_REPARSE_POINT: u32 = 0x400;

/// The GNU 9.4 `--help` body verbatim (probed on WSL), minus the shared
/// gnuhelp::footer tail.
const RM_HELP_BODY: &str = "Usage: rm [OPTION]... [FILE]...\n\
Remove (unlink) the FILE(s).\n\
\n\
  -f, --force           ignore nonexistent files and arguments, never prompt\n\
  -i                    prompt before every removal\n\
  -I                    prompt once before removing more than three files, or\n\
                          when removing recursively; less intrusive than -i,\n\
                          while still giving protection against most mistakes\n\
      --interactive[=WHEN]  prompt according to WHEN: never, once (-I), or\n\
                          always (-i); without WHEN, prompt always\n\
      --one-file-system  when removing a hierarchy recursively, skip any\n\
                          directory that is on a file system different from\n\
                          that of the corresponding command line argument\n\
      --no-preserve-root  do not treat '/' specially\n\
      --preserve-root[=all]  do not remove '/' (default);\n\
                              with 'all', reject any command line argument\n\
                              on a separate device from its parent\n\
  -r, -R, --recursive   remove directories and their contents recursively\n\
  -d, --dir             remove empty directories\n\
  -v, --verbose         explain what is being done\n\
      --help        display this help and exit\n\
      --version     output version information and exit\n\
\n\
By default, rm does not remove directories.  Use the --recursive (-r or -R)\n\
option to remove each listed directory, too, along with all of its contents.\n\
\n\
To remove a file whose name starts with a '-', for example '-foo',\n\
use one of these commands:\n\
  rm -- -foo\n\
\n\
  rm ./-foo\n\
\n\
Note that if you use rm to remove a file, it might be possible to recover\n\
some of its contents, given sufficient expertise and/or time.  For greater\n\
assurance that the contents are truly unrecoverable, consider using shred(1).\n\
\n";

#[cfg(test)]
mod tests;
