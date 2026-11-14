//! `grep`, `egrep`, `fgrep` — GNU grep 3.11 for the rust2 library
//! (#1488 grep lane). Oracle: WSL GNU grep 3.11; the C sources this ports
//! (grep.c, dfasearch.c, kwsearch.c, searchutils.c, gnulib exclude.c) were
//! read from the grep-3.11 release tarball and every observable rule below
//! was re-probed against the oracle.
//!
//! Layout:
//! * `opts`    — getopt_long + grep.c `main`'s option switch, in argv order;
//! * `pattern` — pattern compilation (glibc error texts, dfa.c warnings)
//!               and the two GNU `execute` faces (line selection, `-o`
//!               pieces) over winux-regex: literal search, the lazy line
//!               DFA, the interpreter;
//! * `scan`    — the streaming per-input scanner (fillbuf/grepbuf/prtext/
//!               prline ports, binary files, context, color);
//! * `walk`    — operands, `-r`/`-R` recursion, include/exclude rules;
//! * `bytes`   — memchr/memrchr/count/substring primitives (SSE2);
//! * `text`    — help/version texts and the buffered stdout/stderr plumbing.
//!
//! # Matchers
//!
//! `-G` (default) and `-E` compile through winux-regex (POSIX
//! leftmost-longest, back-references). `-F` patterns are literal byte
//! strings (a trie-shaped DFA for sets, a packed-pair byte search for a
//! single case-sensitive string). `-P` is NOT implemented in Rust: per the
//! 2.0 regex-engine decision (DOCS/2.0-regex-engine-decision.md §6, owner
//! point 2) Perl matching stays on the C++ PCRE2 path. The future C++
//! handler calls [`uses_perl`] on the argument vector and routes `-P`
//! invocations to PCRE2 before this library runs; a `-P` that reaches the
//! library anyway dies exactly like a GNU `--disable-perl-regexp` build:
//! "grep: Perl matching not supported in a --disable-perl-regexp build",
//! status 2, at the option's argv position.
//!
//! # Locale
//!
//! The character model follows the injected environment: LC_ALL, then
//! LC_CTYPE, then LANG; a value naming UTF-8 (or no value at all — the
//! rust2 host is a UTF-8 environment and the WSL oracle runs C.UTF-8)
//! selects the UTF-8 model: encoding errors in output lines make a file
//! "binary", `-w` word constituents are Unicode alphanumerics, `-o` never
//! starts inside a character, and argmatch quotes are curly. Any other
//! value (`C`, `POSIX`, single-byte locales) is the unibyte model.
//!
//! # Windows subset (documented deviations)
//!
//! * `--color=auto` never colors (the library sees injected handles, not
//!   a terminal; GNU checks isatty).
//! * `-T` on stdin pads to the 19-digit width GNU uses for inputs without
//!   a usable size (a pipe); GNU uses the size of a redirected file.
//! * "input file is also the output" and the /dev/null-output shortcut
//!   need fstat identity of the injected stdout; not detected.
//! * stdin is not drained/re-seeked after an early stop (`-m`, `-l`,
//!   `-q`): the library reads only what it needs (the streaming goal).
//! * Case folding (`-i`) is ASCII (winux-regex rule); GNU also folds
//!   non-ASCII letters in a UTF-8 locale.
//! * The egrep/fgrep wrappers print upstream grep 3.11's stderr warning
//!   ("egrep: warning: egrep is obsolescent; using grep -E"); Ubuntu's
//!   packaging of the same release drops it (the gate compares stdout and
//!   status only, so the rows are unaffected).

mod bytes;
mod opts;
mod pattern;
mod scan;
mod text;
mod walk;

use std::ffi::OsString;
use std::io::Read;

use winux_core::{tr, CmdResult, Ctx, EnvSource, Io};

use opts::{Event, Key};
use pattern::{MatchFlags, RawPattern};
use scan::Stop;
use text::Ui;
use walk::{ExcludeSet, Runner};

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub(crate) enum Matcher {
    Basic,
    Extended,
    Fixed,
    /// -X awk/gawk/posixawk: GNU awk syntaxes, matched as ERE here
    Awk(u8),
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub(crate) enum BinaryFiles {
    Binary,
    Text,
    WithoutMatch,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub(crate) enum ListFiles {
    None,
    Matching,
    NonMatching,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub(crate) enum Directories {
    Read,
    Recurse,
    Skip,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub(crate) enum Devices {
    ReadCommandLine,
    Read,
    Skip,
}

/// SGR capabilities (grep.c color_dict).
#[derive(Clone, Debug)]
pub(crate) struct Colors {
    pub selected_match: String,
    pub context_match: String,
    pub filename: String,
    pub line_num: String,
    pub byte_num: String,
    pub sep: String,
    pub selected_line: String,
    pub context_line: String,
    pub rv: bool,
    pub ne: bool,
}

/// Resolved run configuration (the grep.c globals after `main`'s setup).
pub(crate) struct Config {
    pub eol: u8,
    pub utf8: bool,
    pub binary_files: BinaryFiles,
    pub count_matches: bool,
    pub out_quiet: bool,
    pub done_on_match: bool,
    pub exit_on_match: bool,
    pub list_files: ListFiles,
    pub out_invert: bool,
    pub out_line: bool,
    pub out_byte: bool,
    pub out_before: i64,
    pub out_after: i64,
    pub max_count: i64,
    pub line_buffered: bool,
    pub label: Option<String>,
    pub only_matching: bool,
    pub align_tabs: bool,
    /// false under -Z (NUL after file names)
    pub filename_mask: bool,
    pub colors: Option<Colors>,
    pub group_separator: Option<Vec<u8>>,
    pub suppress_errors: bool,
    pub directories: Directories,
    pub devices: Devices,
    pub deref_recursive: bool,
    pub excludes: ExcludeSet,
    pub exclude_dirs: ExcludeSet,
}

pub fn grep_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    Ok(run(args, io, ctx))
}

/// Upstream grep 3.11 `egrep` script: warn on stderr, then
/// `exec grep -E "$@"` (so `egrep -F` is a matcher conflict).
pub fn egrep_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    // The "egrep: " prefix is the wrapper script's own $0 frame; the
    // translatable unit is the warning body.
    let _ = std::io::Write::write_all(
        io.stderr,
        format!(
            "egrep: {}\n",
            tr!(
                ctx,
                "command.egrep.warning.obsolescent",
                "warning: egrep is obsolescent; using grep -E"
            )
        )
        .as_bytes(),
    );
    let mut v = vec![OsString::from("-E")];
    v.extend_from_slice(args);
    Ok(run(&v, io, ctx))
}

/// Upstream grep 3.11 `fgrep` script (`exec grep -F "$@"`).
pub fn fgrep_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let _ = std::io::Write::write_all(
        io.stderr,
        format!(
            "fgrep: {}\n",
            tr!(
                ctx,
                "command.fgrep.warning.obsolescent",
                "warning: fgrep is obsolescent; using grep -F"
            )
        )
        .as_bytes(),
    );
    let mut v = vec![OsString::from("-F")];
    v.extend_from_slice(args);
    Ok(run(&v, io, ctx))
}

/// Routing predicate for the C++ host: does this grep argument vector
/// select the Perl matcher (`-P`, `--perl-regexp` or any unambiguous
/// prefix of it, `-X perl`) as an OPTION? Uses grep's own getopt_long
/// grammar (GNU argument permutation), so `grep -e -P f` (pattern "-P")
/// and `grep -- x -P` (operand) are not Perl. A command line whose option
/// scan fails before any -P is not Perl either (the Rust library then
/// reports the usage error itself). egrep/fgrep share the grammar.
pub fn uses_perl(args: &[OsString]) -> bool {
    uses_perl_with(args, false)
}

/// [`uses_perl`] honoring POSIXLY_CORRECT (options stop at the first
/// operand), for hosts that pass the environment flag.
pub fn uses_perl_with(args: &[OsString], posixly_correct: bool) -> bool {
    let scanned = opts::getopt(args, posixly_correct);
    scanned.events.iter().any(|e| match e {
        Event::Opt(Key::Short(b'P'), _) => true,
        Event::Opt(Key::Short(b'X'), Some(v)) => v == "perl",
        _ => false,
    })
}

fn env_str(env: &dyn EnvSource, key: &str) -> Option<String> {
    env.get(key).map(|v| v.to_string_lossy().into_owned())
}

/// Character model from the environment (see the module doc).
fn locale_is_utf8(env: &dyn EnvSource) -> bool {
    for k in ["LC_ALL", "LC_CTYPE", "LANG"] {
        if let Some(v) = env_str(env, k) {
            if !v.is_empty() {
                let l = v.to_ascii_lowercase();
                return l.contains("utf-8") || l.contains("utf8");
            }
        }
    }
    true
}

/// Split a key buffer into pattern lines (a trailing newline ends the last
/// pattern; every other newline separates).
fn split_keys(bytes: &[u8], origin: Option<&str>, out: &mut Vec<RawPattern>) {
    let mut start = 0usize;
    let mut lineno = 0usize;
    let body = bytes.strip_suffix(b"\n").unwrap_or(bytes);
    if origin.is_some() && bytes.is_empty() {
        return;
    }
    loop {
        lineno += 1;
        let end = body[start..]
            .iter()
            .position(|&b| b == b'\n')
            .map(|k| start + k)
            .unwrap_or(body.len());
        out.push(RawPattern {
            bytes: body[start..end].to_vec(),
            origin: origin.map(|f| (f.to_string(), lineno)),
        });
        if end >= body.len() {
            break;
        }
        start = end + 1;
    }
}

fn parse_grep_colors(c: &mut Colors, user_sm: &mut bool, user_cm: &mut bool, spec: &str) {
    // grep.c parse_grep_colors: "be well-formed or you're gone" — caps
    // applied before a malformed byte stay applied.
    let bytes = spec.as_bytes();
    let mut name_start = 0usize;
    let mut val_start: Option<usize> = None;
    let mut q = 0usize;
    loop {
        let ch = bytes.get(q).copied().unwrap_or(0);
        if ch == b':' || ch == 0 {
            let name = &spec[name_start..val_start.map_or(q, |v| v - 1)];
            let val = val_start.map(|v| spec[v..q].to_string());
            match name {
                "mt" => {
                    if let Some(v) = &val {
                        c.selected_match = v.clone();
                        *user_sm = false;
                    }
                    c.context_match = c.selected_match.clone();
                    *user_cm = *user_sm;
                }
                "ms" => {
                    if let Some(v) = val {
                        c.selected_match = v;
                        *user_sm = false;
                    }
                }
                "mc" => {
                    if let Some(v) = val {
                        c.context_match = v;
                        *user_cm = false;
                    }
                }
                "fn" => {
                    if let Some(v) = val {
                        c.filename = v;
                    }
                }
                "ln" => {
                    if let Some(v) = val {
                        c.line_num = v;
                    }
                }
                "bn" => {
                    if let Some(v) = val {
                        c.byte_num = v;
                    }
                }
                "se" => {
                    if let Some(v) = val {
                        c.sep = v;
                    }
                }
                "sl" => {
                    if let Some(v) = val {
                        c.selected_line = v;
                    }
                }
                "cx" => {
                    if let Some(v) = val {
                        c.context_line = v;
                    }
                }
                "rv" => c.rv = true,
                "ne" => c.ne = true,
                _ => {}
            }
            if ch == 0 {
                return;
            }
            q += 1;
            name_start = q;
            val_start = None;
        } else if ch == b'=' {
            if q == name_start || val_start.is_some() {
                return;
            }
            q += 1;
            val_start = Some(q);
        } else if val_start.is_none() || ch == b';' || ch.is_ascii_digit() {
            q += 1;
        } else {
            return;
        }
    }
}

/// [GNU] grep.c context_length_arg: "%s: invalid context length
/// argument" — the raw digits are the %s payload, the wording is the
/// translatable unit.
fn invalid_context_length(ctx: &Ctx, value: &str) -> String {
    tr!(
        ctx,
        "command.grep.error.invalid_context_length",
        "{value}: invalid context length argument",
        value = value
    )
}

fn run(args: &[OsString], io: &mut Io, ctx: &Ctx) -> u32 {
    let stdin: &mut dyn Read = &mut *io.stdin;
    let mut ui = Ui::new(&mut *io.stdout, &mut *io.stderr);
    let status = run_inner(args, stdin, &mut ui, ctx);
    ui.flush_out();
    if let Some(e) = ui.write_error.take() {
        // close_stdout / die (EXIT_TROUBLE, errno, "write error")
        let reason = crate::fsutil::reason(&e);
        let _ = std::io::Write::write_all(
            ui.stderr,
            format!(
                "grep: {}\n",
                tr!(
                    ctx,
                    "command.grep.error.write_error",
                    "write error: {reason}",
                    reason = reason
                )
            )
            .as_bytes(),
        );
        return 2;
    }
    status
}

fn run_inner(args: &[OsString], stdin: &mut dyn Read, ui: &mut Ui, ctx: &Ctx) -> u32 {
    let utf8 = locale_is_utf8(ctx.env);
    let posixly_correct = ctx.env.get("POSIXLY_CORRECT").is_some();
    let scanned = opts::getopt(args, posixly_correct);

    let mut keys: Option<Vec<RawPattern>> = None;
    let mut matcher: Option<Matcher> = None;
    let mut filename_option: i8 = 0;
    let mut out_after: i64 = -1;
    let mut out_before: i64 = -1;
    let mut default_context: i64 = -1;
    let mut devices = Devices::ReadCommandLine;
    let mut binary_files = BinaryFiles::Binary;
    let mut align_tabs = false;
    let mut show_version = false;
    let mut show_help = false;
    let mut out_byte = false;
    let mut count_matches = false;
    let mut directories = Directories::Read;
    let mut last_recursive = false;
    let mut match_icase = false;
    let mut list_files = ListFiles::None;
    let mut max_count: i64 = i64::MAX;
    let mut out_line = false;
    let mut only_matching = false;
    let mut exit_on_match = false;
    let mut deref = false;
    let mut suppress_errors = false;
    let mut out_invert = false;
    let mut match_words = false;
    let mut match_lines = false;
    let mut filename_mask = true;
    let mut eol: u8 = b'\n';
    let mut color_option: u8 = 0; // 0 never, 1 always, 2 auto
    let mut excludes = ExcludeSet::default();
    let mut exclude_dirs = ExcludeSet::default();
    let mut group_separator: Option<Vec<u8>> = Some(b"--".to_vec());
    let mut line_buffered = false;
    let mut label: Option<String> = None;

    let mut digits = String::new();
    let mut digit_elem: Option<usize> = None;
    let apply_digits = |digits: &mut String, ctxv: &mut i64, ui: &mut Ui| -> bool {
        if digits.is_empty() {
            return true;
        }
        let d = std::mem::take(digits);
        match opts::context_length(&d) {
            Ok(n) => {
                *ctxv = n;
                true
            }
            Err(_) => {
                ui.error(&invalid_context_length(ctx, &d));
                false
            }
        }
    };

    for ev in &scanned.events {
        let (key, val) = match ev {
            Event::Digit(c, elem) => {
                if digit_elem != Some(*elem) || digits.is_empty() {
                    digits.clear();
                } else if digits == "0" {
                    digits.clear();
                }
                if digits.len() == 21 {
                    digits.push_str("...");
                    if !apply_digits(&mut digits, &mut default_context, ui) {
                        return 2;
                    }
                    continue;
                }
                digits.push(*c as char);
                digit_elem = Some(*elem);
                continue;
            }
            Event::Opt(k, v) => (*k, v.as_ref().map(|s| s.to_string_lossy().into_owned())),
        };
        if !apply_digits(&mut digits, &mut default_context, ui) {
            return 2;
        }
        digit_elem = None;
        let v = || val.clone().unwrap_or_default();
        let mut setmatcher = |m: Matcher, ui: &mut Ui| -> bool {
            if let Some(prev) = matcher {
                if prev != m {
                    ui.error(&tr!(
                        ctx,
                        "command.grep.error.conflicting_matchers",
                        "conflicting matchers specified"
                    ));
                    return false;
                }
            }
            matcher = Some(m);
            true
        };
        match key {
            Key::Short(b'A') => match opts::context_length(&v()) {
                Ok(n) => out_after = n,
                Err(_) => {
                    ui.error(&invalid_context_length(ctx, &v()));
                    return 2;
                }
            },
            Key::Short(b'B') => match opts::context_length(&v()) {
                Ok(n) => out_before = n,
                Err(_) => {
                    ui.error(&invalid_context_length(ctx, &v()));
                    return 2;
                }
            },
            Key::Short(b'C') => match opts::context_length(&v()) {
                Ok(n) => default_context = n,
                Err(_) => {
                    ui.error(&invalid_context_length(ctx, &v()));
                    return 2;
                }
            },
            Key::Short(b'D') => match v().as_str() {
                "read" => devices = Devices::Read,
                "skip" => devices = Devices::Skip,
                _ => {
                    ui.error(&tr!(
                        ctx,
                        "command.grep.error.unknown_devices_method",
                        "unknown devices method"
                    ));
                    return 2;
                }
            },
            Key::Short(b'E') => {
                if !setmatcher(Matcher::Extended, ui) {
                    return 2;
                }
            }
            Key::Short(b'F') => {
                if !setmatcher(Matcher::Fixed, ui) {
                    return 2;
                }
            }
            Key::Short(b'G') => {
                if !setmatcher(Matcher::Basic, ui) {
                    return 2;
                }
            }
            Key::Short(b'P') => {
                ui.error(&tr!(
                    ctx,
                    "command.grep.error.perl_not_supported",
                    "Perl matching not supported in a --disable-perl-regexp build"
                ));
                return 2;
            }
            Key::Short(b'X') => {
                let m = match v().as_str() {
                    "grep" => Matcher::Basic,
                    "egrep" => Matcher::Extended,
                    "fgrep" => Matcher::Fixed,
                    "awk" => Matcher::Awk(0),
                    "gawk" => Matcher::Awk(1),
                    "posixawk" => Matcher::Awk(2),
                    "perl" => {
                        ui.error(&tr!(
                            ctx,
                            "command.grep.error.perl_not_supported",
                            "Perl matching not supported in a --disable-perl-regexp build"
                        ));
                        return 2;
                    }
                    other => {
                        let shown = other.to_string();
                        ui.error(&tr!(
                            ctx,
                            "command.grep.error.invalid_matcher",
                            "invalid matcher {matcher}",
                            matcher = shown
                        ));
                        return 2;
                    }
                };
                if !setmatcher(m, ui) {
                    return 2;
                }
            }
            Key::Short(b'H') => filename_option = 1,
            Key::Short(b'I') => binary_files = BinaryFiles::WithoutMatch,
            Key::Short(b'T') => align_tabs = true,
            Key::Short(b'U') => {}
            Key::Short(b'u') => ui.error(&tr!(
                ctx,
                "command.grep.warning.unix_byte_offsets_obsolete",
                "warning: --unix-byte-offsets (-u) is obsolete"
            )),
            Key::Short(b'V') => show_version = true,
            Key::Short(b'a') => binary_files = BinaryFiles::Text,
            Key::Short(b'b') => out_byte = true,
            Key::Short(b'c') => count_matches = true,
            Key::Short(b'd') => {
                let arg = v();
                match opts::argmatch(&arg, &["read", "recurse", "skip"]) {
                    Ok(0) => directories = Directories::Read,
                    Ok(1) => {
                        directories = Directories::Recurse;
                        last_recursive = true;
                    }
                    Ok(_) => directories = Directories::Skip,
                    Err(ambiguous) => {
                        opts::argmatch_failure(ui, ctx, &arg, ambiguous, utf8);
                        ui.usage_lines();
                        return 1;
                    }
                }
            }
            Key::Short(b'e') => {
                let k = keys.get_or_insert_with(Vec::new);
                let raw = val
                    .as_ref()
                    .map(|s| s.as_bytes().to_vec())
                    .unwrap_or_default();
                let mut buf = raw;
                buf.push(b'\n');
                split_keys(&buf, None, k);
            }
            Key::Short(b'f') => {
                let name = v();
                let data = if name == "-" {
                    let mut b = Vec::new();
                    if let Err(e) = stdin.read_to_end(&mut b) {
                        ui.error(&format!("{name}: {}", crate::fsutil::reason(&e)));
                        return 2;
                    }
                    b
                } else {
                    let path = crate::fsutil::resolve(ctx, &name);
                    match std::fs::read(&path) {
                        Ok(b) => b,
                        Err(e) => {
                            let reason = crate::fsutil::gnu_open_reason(ctx, &name, &e, false);
                            ui.error(&format!("{name}: {reason}"));
                            return 2;
                        }
                    }
                };
                let k = keys.get_or_insert_with(Vec::new);
                split_keys(&data, Some(&name), k);
            }
            Key::Short(b'h') => filename_option = -1,
            Key::Short(b'i') | Key::Short(b'y') => match_icase = true,
            Key::NoIgnoreCase => match_icase = false,
            Key::Short(b'L') => list_files = ListFiles::NonMatching,
            Key::Short(b'l') => list_files = ListFiles::Matching,
            Key::Short(b'm') => match opts::xstrtoimax(&v()) {
                Some(n) => max_count = n,
                None => {
                    ui.error(&tr!(
                        ctx,
                        "command.grep.error.invalid_max_count",
                        "invalid max count"
                    ));
                    return 2;
                }
            },
            Key::Short(b'n') => out_line = true,
            Key::Short(b'o') => only_matching = true,
            Key::Short(b'q') => exit_on_match = true,
            Key::Short(b'R') => {
                deref = true;
                directories = Directories::Recurse;
                last_recursive = true;
            }
            Key::Short(b'r') => {
                directories = Directories::Recurse;
                last_recursive = true;
            }
            Key::Short(b's') => suppress_errors = true,
            Key::Short(b'v') => out_invert = true,
            Key::Short(b'w') => match_words = true,
            Key::Short(b'x') => match_lines = true,
            Key::Short(b'Z') => filename_mask = false,
            Key::Short(b'z') => eol = 0,
            Key::BinaryFiles => match v().as_str() {
                "binary" => binary_files = BinaryFiles::Binary,
                "text" => binary_files = BinaryFiles::Text,
                "without-match" => binary_files = BinaryFiles::WithoutMatch,
                _ => {
                    ui.error(&tr!(
                        ctx,
                        "command.grep.error.unknown_binary_files_type",
                        "unknown binary-files type"
                    ));
                    return 2;
                }
            },
            Key::Color => match &val {
                None => color_option = 2,
                Some(a) => match a.to_ascii_lowercase().as_str() {
                    "always" | "yes" | "force" => color_option = 1,
                    "never" | "no" | "none" => color_option = 0,
                    "auto" | "tty" | "if-tty" => color_option = 2,
                    _ => show_help = true,
                },
            },
            Key::Exclude => excludes.add(&v(), false),
            Key::Include => excludes.add(&v(), true),
            Key::ExcludeFrom => {
                let name = v();
                let path = crate::fsutil::resolve(ctx, &name);
                match std::fs::read(&path) {
                    Ok(b) => {
                        for line in b.split(|&c| c == b'\n') {
                            let mut l = line;
                            while let Some((&last, rest)) = l.split_last() {
                                if last.is_ascii_whitespace() {
                                    l = rest;
                                } else {
                                    break;
                                }
                            }
                            if !l.is_empty() {
                                excludes.add(&String::from_utf8_lossy(l), false);
                            }
                        }
                    }
                    Err(e) => {
                        let reason = crate::fsutil::gnu_open_reason(ctx, &name, &e, false);
                        ui.error(&format!("{name}: {reason}"));
                        return 2;
                    }
                }
            }
            Key::ExcludeDir => {
                let a = v();
                let t = a.trim_end_matches('/');
                let t = if t.is_empty() && !a.is_empty() {
                    "/"
                } else {
                    t
                };
                exclude_dirs.add(t, false);
            }
            Key::GroupSeparator => group_separator = Some(v().into_bytes()),
            Key::NoGroupSeparator => group_separator = None,
            Key::LineBuffered => line_buffered = true,
            Key::Label => label = Some(v()),
            Key::Help => show_help = true,
            Key::Short(_) => {}
        }
    }
    if !apply_digits(&mut digits, &mut default_context, ui) {
        return 2;
    }
    if let Some(msg) = &scanned.error {
        ui.option_error(msg);
        return 2;
    }

    if show_version {
        ui.out.extend_from_slice(text::VERSION_TEXT.as_bytes());
        return 0;
    }
    if show_help {
        ui.out.extend_from_slice(text::HELP_TEXT.as_bytes());
        return 0;
    }

    let mut operands = scanned.operands;
    let mut patterns: Vec<RawPattern> = match keys {
        Some(k) => {
            if k.is_empty() {
                // -f /dev/null: match nothing (flip -v; one empty key)
                out_invert = !out_invert;
                match_lines = false;
                match_words = false;
                vec![RawPattern {
                    bytes: Vec::new(),
                    origin: None,
                }]
            } else {
                k
            }
        }
        None => {
            if operands.is_empty() {
                ui.usage_lines();
                return 2;
            }
            let pat = operands.remove(0).to_string_lossy().into_owned();
            let mut b = pat.into_bytes();
            if matcher != Some(Matcher::Fixed) && b.len() >= 2 && b[0] == b'\\' && b[1] == b'-' {
                b.remove(0);
            }
            b.push(b'\n');
            let mut v = Vec::new();
            split_keys(&b, None, &mut v);
            v
        }
    };
    // update_patterns: duplicate pattern lines are dropped
    {
        let mut seen = std::collections::HashSet::new();
        patterns.retain(|p| seen.insert(p.bytes.clone()));
    }
    let single_empty = patterns.len() == 1 && patterns[0].bytes.is_empty();

    if exit_on_match {
        list_files = ListFiles::None;
    }
    let mut done_on_match = false;
    if exit_on_match || list_files != ListFiles::None {
        count_matches = false;
        done_on_match = true;
    }
    let out_quiet = count_matches || done_on_match;
    if out_after < 0 {
        out_after = default_context;
    }
    if out_before < 0 {
        out_before = default_context;
    }
    if (max_count == 0 || (single_empty && out_invert && !match_lines && !match_words))
        && list_files != ListFiles::NonMatching
    {
        return 1;
    }

    let colors = if color_option == 1 {
        let mut c = Colors {
            selected_match: "01;31".into(),
            context_match: "01;31".into(),
            filename: "35".into(),
            line_num: "32".into(),
            byte_num: "32".into(),
            sep: "36".into(),
            selected_line: String::new(),
            context_line: String::new(),
            rv: false,
            ne: false,
        };
        let mut user_sm = false;
        let mut user_cm = false;
        let userval = env_str(ctx.env, "GREP_COLOR").unwrap_or_default();
        if !userval.is_empty() && userval.bytes().all(|b| b == b';' || b.is_ascii_digit()) {
            c.selected_match = userval.clone();
            c.context_match = userval.clone();
            user_sm = true;
            user_cm = true;
        }
        if let Some(spec) = env_str(ctx.env, "GREP_COLORS") {
            if !spec.is_empty() {
                parse_grep_colors(&mut c, &mut user_sm, &mut user_cm, &spec);
            }
        }
        if user_sm || user_cm {
            ui.error(&tr!(
                ctx,
                "command.grep.warning.grep_color_deprecated",
                "warning: GREP_COLOR='{value}' is deprecated; use GREP_COLORS='mt={value}'",
                value = userval
            ));
        }
        Some(c)
    } else {
        None
    };

    let matcher = match matcher.unwrap_or(Matcher::Basic) {
        Matcher::Awk(_) => Matcher::Extended,
        m => m,
    };
    let flags = MatchFlags {
        ignore_case: match_icase,
        words: match_words,
        lines: match_lines,
        utf8,
        eol,
    };
    let compiled = match pattern::compile(matcher, &patterns, flags, ui, ctx) {
        Ok(c) => c,
        Err(()) => return 2,
    };

    let num_operands = operands.len();
    let recursive = directories == Directories::Recurse;
    let out_file: Option<bool> = if filename_option == 0 && num_operands <= 1 {
        if recursive {
            None
        } else {
            Some(false)
        }
    } else {
        Some(filename_option >= 0)
    };
    if deref && devices == Devices::ReadCommandLine {
        devices = Devices::Read;
    }

    let cfg = Config {
        eol,
        utf8,
        binary_files,
        count_matches,
        out_quiet,
        done_on_match,
        exit_on_match,
        list_files,
        out_invert,
        out_line,
        out_byte,
        out_before,
        out_after,
        max_count,
        line_buffered,
        label,
        only_matching,
        align_tabs,
        filename_mask,
        colors,
        group_separator,
        suppress_errors,
        directories,
        devices,
        deref_recursive: deref,
        excludes,
        exclude_dirs,
    };

    let mut omit_dot_slash = false;
    let files: Vec<OsString> = if !operands.is_empty() {
        operands
    } else if recursive && last_recursive {
        omit_dot_slash = true;
        vec![OsString::from(".")]
    } else {
        vec![OsString::from("-")]
    };

    let mut runner = Runner::new(&cfg, ui, stdin, compiled, ctx, out_file, omit_dot_slash);
    let mut status = true;
    for f in &files {
        match runner.command_line_arg(f) {
            Ok(s) => status &= s,
            Err(Stop::Exit(rc)) => return rc,
            Err(Stop::WriteError) => return 2,
        }
    }
    if runner.run.errseen {
        2
    } else if status {
        1
    } else {
        0
    }
}

#[cfg(test)]
mod tests;
