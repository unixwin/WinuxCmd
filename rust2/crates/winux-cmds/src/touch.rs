//! `touch` — GNU coreutils touch semantics on the Windows time API.
//!
//! Timestamps are set through SetFileTime (kernel32) on a
//! FILE_WRITE_ATTRIBUTES handle; naive local dates convert through
//! TzSpecificLocalTimeToSystemTime, so the CRT global timezone is never
//! touched. -a sets only the access time, -m only the modification time;
//! with neither, both follow the reference time (which is the current
//! time for a plain `touch FILE`).
//!
//! Time sources — GNU order measured against coreutils 9.4 (WSL probes,
//! 2026-11-11, rust2/target/gate/w3a/probe/touch-precedence*.sh):
//! * `-d`/`--date` — a GNU parse-datetime subset, probed against coreutils
//!   9.4: ISO 8601 "YYYY-MM-DD[ T]HH[:MM[:SS[.frac]]]", basic "THHMM",
//!   date-only, time-only (today), bare hour, compact "YYYYMMDD[HHMM[SS]]",
//!   loose single-digit fields, slash dates, "@SECONDS" epoch, military
//!   zone letters ("1430Z", "9:30j"), weekday items ("monday", "next
//!   monday", "last friday"), relative items ("3 days ago", "+2 hours",
//!   "1 fortnight", now/today/yesterday/tomorrow, month/year calendar
//!   shifts with month-end clamping), a combined "<base> <relative item>"
//!   ("2026-03-31 1 month ago"), and a trailing timezone
//!   (+HHMM / +HH:MM / Z / UTC / GMT). Relative forms anchor at the -r
//!   reference time when both are given (touch.c:354-360 evaluates the
//!   string against the reference ATIME for the atime field and the
//!   reference MTIME for the mtime field; probed: `-r ref -d '+1 day'`
//!   lands a day past each reference stamp).
//! * `-t` — [[CC]YY]MMDDhhmm[.ss] fixed format; YY 69-99 -> 19xx,
//!   00-68 -> 20xx; .ss is exactly two digits 0-60 (probed: .99 rejected).
//!   Parsed INLINE at its argv position: a bad stamp errors immediately,
//!   before later args are examined (probed: `-t garbage --bogus` reports
//!   the stamp, not the option).
//! * `-r`/`--reference` — both times from the reference file; stat'd
//!   post-getopt, before the -d parse and before the missing-operand check
//!   (probed: `-r missing -d garbage` reports the stat failure; a missing
//!   reference with no operands reports the stat failure too).
//! * GNU precedence (probed): `-t` with any other source (-d or -r) is
//!   `cannot specify times from more than one source` (checked before the
//!   reference stat); `-d` + `-r` is LEGAL with the -d time applied to both
//!   fields; the missing-operand check runs after the -d parse
//!   (`-d garbage` with no operands reports the date, `-d ok` reports the
//!   operand).
//! * `-f` is a silently ignored BSD-compat option (probed: rc 0).
//!
//! -c/--create... no: -c/--no-create reports nothing and creates nothing
//! for missing operands; without it missing files are created (rc 1 on
//! failure, later operands still processed). -h/--no-dereference opens
//! reparse points directly. No operand is `missing file operand`, rc 1.
//! Operands keep the direct-invocation contract of the 1.x build (tests/
//! unit/touch/touch_unit_test.cpp, all GNU-rc-probed where GNU has an
//! opinion): `-` retimes standard output (POSIX; GNU touch.c:127-128) and
//! does NOT name a literal file. Retiming the host's stdout is a
//! process-handle side effect, so it is INJECTED ([`StdoutTimes`], the
//! `tail::StdinMeta` precedent): the library never asks for
//! `STD_OUTPUT_HANDLE` (#1487 red line). The registry default has no host
//! stdout (an ABI capture buffer is untimable, GNU's EINVAL shape); the
//! CLI driver supplies the real handle's implementation. A wildcard
//! operand (`*?[`) expands over
//! the named directory like the 1.x utils glob (unmatched patterns stay
//! literal, the GNU glob default); a trailing separator fails with GNU's
//! "setting times of '<op>': <reason>" on a non-directory and succeeds on
//! a directory (probed: `touch dir/` rc 0, `touch f.txt/` and `touch
//! nope/` rc 1 with that wording). "D:name" spells the literal carrier
//! file in the cwd (U+F03A fold, #1283; fs_win::api_path is the one
//! operand boundary).

use std::ffi::OsString;
use std::path::Path;

use crate::glob::{contains_wildcard, glob_expand_in};

use crate::argopt;
use crate::fs_win::{self, FileTime, SystemTime};
use winux_core::{usage_error, CmdResult, Ctx, Io};

const OPTS_HELP: &str = "";

/// The host's stdout retiming face (POSIX `touch -`).
///
/// GNU calls `utimensat(1, ...)`; the operation belongs to whoever owns
/// the process handle, so the command receives it as an injected
/// capability instead of reaching for `GetStdHandle` itself. `Err` is a
/// Win32 error code, rendered by [`fs_win::win32_reason`] like every other
/// touch failure.
pub trait StdoutTimes {
    fn set_times(&mut self, atime: Option<FileTime>, mtime: Option<FileTime>) -> Result<(), u32>;
}

struct TouchOpts {
    change_atime: bool,
    change_mtime: bool,
    no_create: bool,
    no_dereference: bool,
    date: Option<String>,
    stamp: Option<String>,
    reference: Option<String>,
    time_word: Option<String>,
}

pub fn touch_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    touch_with_stdout(args, io, ctx, None)
}

/// `touch_cmd` with the host's stdout retiming face injected (`-`).
///
/// `None` is the ABI/library shape: stdout is a capture buffer with no
/// settable times, so `touch -` reports the setting-times failure GNU
/// reports for an untimable handle (rc 1, "Invalid argument"). The CLI
/// driver passes `Some` and retimes its real stdout handle.
pub fn touch_with_stdout(
    args: &[OsString],
    io: &mut Io,
    ctx: &Ctx,
    mut stdout_times: Option<&mut dyn StdoutTimes>,
) -> CmdResult {
    let mut opts = TouchOpts {
        change_atime: false,
        change_mtime: false,
        no_create: false,
        no_dereference: false,
        date: None,
        stamp: None,
        reference: None,
        time_word: None,
    };
    let mut operands: Vec<String> = Vec::new();
    let mut stamp_time: Option<FileTime> = None;
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
                // [GNU] touch.c:284/303: -a/-m set bits in the change_times
                // mask, they do not reset the sibling flag — `-a -m` must
                // update BOTH times (issue #1704).
                "access" | "atime" => opts.change_atime = true,
                "mtime" | "modify" => opts.change_mtime = true,
                "no-create" => opts.no_create = true,
                "no-dereference" => opts.no_dereference = true,
                "create" => opts.no_create = false,
                "date" => match take_val(inline, args, &mut idx) {
                    Some(v) => opts.date = Some(v),
                    None => {
                        let _ = usage_error(
                            ctx,
                            io,
                            "touch",
                            &argopt::option_requires_argument(ctx, "--date"),
                        );
                        return Ok(1);
                    }
                },
                "reference" => match take_val(inline, args, &mut idx) {
                    Some(v) => opts.reference = Some(v),
                    None => {
                        let _ = usage_error(
                            ctx,
                            io,
                            "touch",
                            &argopt::option_requires_argument(ctx, "--reference"),
                        );
                        return Ok(1);
                    }
                },
                "time" => {
                    let v = match take_val(inline, args, &mut idx) {
                        Some(v) => v,
                        None => {
                            let _ = usage_error(
                                ctx,
                                io,
                                "touch",
                                &argopt::option_requires_argument(ctx, "--time"),
                            );
                            return Ok(1);
                        }
                    };
                    match v.to_ascii_lowercase().as_str() {
                        // [GNU] touch.c:321-324: --time accumulates into
                        // change_times through time_masks the same way.
                        "access" | "atime" | "use" => {
                            opts.change_atime = true;
                            opts.time_word = Some(v);
                        }
                        "modify" | "mtime" => {
                            opts.change_mtime = true;
                            opts.time_word = Some(v);
                        }
                        _ => {
                            let _ = writeln!(
                                io.stderr,
                                "touch: invalid argument '{v}' for '--time'\nValid arguments are:\n  - 'atime', 'access', 'use'\n  - 'mtime', 'modify'\n{}",
                                winux_core::try_help(ctx, "touch")
                            );
                            return Ok(1);
                        }
                    }
                }
                "help" | "version" => return Ok(0),
                _ => {
                    let _ = usage_error(ctx, io, "touch", &argopt::unrecognized_option(ctx, &arg));
                    return Ok(1);
                }
            }
            idx += 1;
            continue;
        }
        // short cluster
        let chars: Vec<char> = arg.chars().skip(1).collect();
        let mut ci = 0usize;
        let mut consumed_next = false;
        while ci < chars.len() {
            let c = chars[ci];
            match c {
                'a' => opts.change_atime = true,
                'm' => opts.change_mtime = true,
                'c' => opts.no_create = true,
                'h' => opts.no_dereference = true,
                // [GNU] -f is a silently ignored BSD-compat option (probed:
                // `touch -f FILE` and `touch -af FILE` succeed quietly).
                'f' => {}
                'd' => {
                    let rest: String = chars[ci + 1..].iter().collect();
                    if !rest.is_empty() {
                        opts.date = Some(rest);
                        ci = chars.len();
                    } else {
                        match args.get(idx + 1) {
                            Some(v) => {
                                opts.date = Some(v.to_string_lossy().into_owned());
                                consumed_next = true;
                                ci = chars.len();
                            }
                            None => {
                                let _ = usage_error(
                                    ctx,
                                    io,
                                    "touch",
                                    &argopt::short_option_requires_argument(ctx, "d"),
                                );
                                return Ok(1);
                            }
                        }
                    }
                }
                't' => {
                    let rest: String = chars[ci + 1..].iter().collect();
                    let v = if !rest.is_empty() {
                        ci = chars.len();
                        rest
                    } else {
                        match args.get(idx + 1) {
                            Some(v) => {
                                consumed_next = true;
                                ci = chars.len();
                                v.to_string_lossy().into_owned()
                            }
                            None => {
                                let _ = usage_error(
                                    ctx,
                                    io,
                                    "touch",
                                    &argopt::short_option_requires_argument(ctx, "t"),
                                );
                                return Ok(1);
                            }
                        }
                    };
                    // [GNU] -t parses INLINE at its argv position: a bad
                    // stamp errors before later args are examined (probed:
                    // `-t garbage --bogus` reports the stamp, not --bogus;
                    // `-d garbage -t garbage` reports the stamp too).
                    match parse_stamp(&v) {
                        Some(ft) => {
                            opts.stamp = Some(v);
                            stamp_time = Some(ft);
                        }
                        None => {
                            let _ = writeln!(io.stderr, "touch: invalid date format '{v}'");
                            return Ok(1);
                        }
                    }
                }
                'r' => {
                    let rest: String = chars[ci + 1..].iter().collect();
                    let v = if !rest.is_empty() {
                        ci = chars.len();
                        rest
                    } else {
                        match args.get(idx + 1) {
                            Some(v) => {
                                consumed_next = true;
                                ci = chars.len();
                                v.to_string_lossy().into_owned()
                            }
                            None => {
                                let _ = usage_error(
                                    ctx,
                                    io,
                                    "touch",
                                    &argopt::short_option_requires_argument(ctx, "r"),
                                );
                                return Ok(1);
                            }
                        }
                    };
                    opts.reference = Some(v);
                }
                c => {
                    let _ = usage_error(
                        ctx,
                        io,
                        "touch",
                        &argopt::invalid_option(ctx, &c.to_string()),
                    );
                    return Ok(1);
                }
            }
            ci += 1;
        }
        idx += if consumed_next { 2 } else { 1 };
    }
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default();
    let now_ft = FileTime::from_epoch(now.as_secs() as i64, now.subsec_nanos());
    let mut ref_atime: Option<FileTime> = None;
    let mut ref_mtime: Option<FileTime> = None;
    let mut date_atime: Option<FileTime> = None;
    let mut date_mtime: Option<FileTime> = None;
    // [GNU] -t combined with any other time source is rejected before the
    // reference stat (probed: `-r missing -t ok` reports "more than one
    // source", not the stat failure). -d + -r is legal; the -d time wins.
    if opts.stamp.is_some() && (opts.date.is_some() || opts.reference.is_some()) {
        let _ = writeln!(
            io.stderr,
            "touch: cannot specify times from more than one source\n{}",
            winux_core::try_help(ctx, "touch")
        );
        return Ok(1);
    }
    // Reference stat (also validates -r): GNU stats the reference before
    // the -d parse and before the missing-operand check (probed:
    // `-r missing -d garbage` and `-r missing` with no operands both
    // report the stat failure).
    if let Some(r) = &opts.reference {
        match fs_win::file_meta(ctx, r, true) {
            Ok(m) => {
                ref_atime = Some(m.atime);
                ref_mtime = Some(m.mtime);
            }
            Err(e) => {
                let _ = writeln!(
                    io.stderr,
                    "touch: failed to get attributes of '{r}': {}",
                    fs_win::win32_reason(e)
                );
                return Ok(1);
            }
        }
    }
    if let Some(d) = &opts.date {
        // [GNU] touch.c:354-360: with -r the -d string is evaluated against
        // the reference ATIME for the atime field and the reference MTIME
        // for the mtime field, so relative items anchor per field
        // (probed: `-r ref -d '+1 day'` lands a day past each reference
        // stamp). Without -r both fields use now; an absolute string then
        // parses to the same value either way.
        let (a_base, m_base) = match (ref_atime, ref_mtime) {
            (Some(a), Some(m)) => (a, m),
            _ => (now_ft, now_ft),
        };
        match parse_datetime(d, a_base) {
            Some(ft) => date_atime = Some(ft),
            None => {
                let _ = writeln!(io.stderr, "touch: invalid date format '{d}'");
                return Ok(1);
            }
        }
        match parse_datetime(d, m_base) {
            Some(ft) => date_mtime = Some(ft),
            None => {
                let _ = writeln!(io.stderr, "touch: invalid date format '{d}'");
                return Ok(1);
            }
        }
    }
    if opts.stamp.is_some() {
        date_atime = stamp_time;
        date_mtime = stamp_time;
    }
    // [GNU] the missing-operand check follows time-source resolution
    // (probed: `-d garbage` with no operands reports the date;
    // `-d ok` with no operands reports the operand).
    if operands.is_empty() {
        let _ = usage_error(
            ctx,
            io,
            "touch",
            &crate::messages::missing_file_operand(ctx),
        );
        return Ok(1);
    }
    let mut ok = true;
    'operands: for op in &operands {
        // GNU: an explicit -d/-t time wins over -r (probed: -d + -r succeeds
        // with the -d time applied); -r alone copies BOTH times from the
        // reference; with neither, both get the current time. -a/-m then
        // select which of the two actually gets written.
        let (want_atime, want_mtime) = if let (Some(a), Some(m)) = (date_atime, date_mtime) {
            (Some(a), Some(m))
        } else if opts.reference.is_some() {
            (ref_atime, ref_mtime)
        } else {
            (Some(now_ft), Some(now_ft))
        };
        // [GNU] touch.c:335-336 + 140-149: change_times==0 means BOTH;
        // otherwise each selected field is written and the other is
        // UTIME_OMIT (None here).
        let (change_atime, change_mtime) = if !opts.change_atime && !opts.change_mtime {
            (true, true)
        } else {
            (opts.change_atime, opts.change_mtime)
        };
        let atime = if change_atime { want_atime } else { None };
        let mtime = if change_mtime { want_mtime } else { None };
        // [GNU] touch.c:127-128: the '-' operand retimes standard output
        // (POSIX) instead of naming a literal file. Retiming is the
        // injected host capability; without one (the ABI capture buffer)
        // it reports the untimable-handle failure GNU reports for a pipe
        // or console (ERROR_INVALID_HANDLE renders "Invalid argument").
        if op == "-" {
            let r = match stdout_times.as_mut() {
                Some(t) => t.set_times(atime, mtime),
                None => Err(87), // ERROR_INVALID_PARAMETER -> EINVAL
            };
            if let Err(e) = r {
                let _ = writeln!(
                    io.stderr,
                    "touch: setting times of '-': {}",
                    fs_win::win32_reason(e)
                );
                ok = false;
            }
            continue 'operands;
        }
        // [GNU] touch.c:127-129,196: an empty operand is a FILE argument,
        // not "no operand" — open("") fails ENOENT and GNU reports the
        // cannot-touch diagnostic (probed `touch ''` -> "cannot touch '':
        // No such file or directory", rc 1). The check must sit ahead of
        // the operand boundary: api_path("") resolves to the cwd and
        // would silently retime the working directory (issue #1706).
        if op.is_empty() {
            let _ = writeln!(
                io.stderr,
                "touch: cannot touch '': {}",
                fs_win::win32_reason(2) // ERROR_FILE_NOT_FOUND -> ENOENT wording
            );
            ok = false;
            continue 'operands;
        }
        // [GNU] a trailing separator names the operand through the slash:
        // a directory succeeds (probed `touch dir/` rc 0); anything else
        // fails with the setting-times wording and the OS reason (probed:
        // `touch f.txt/` -> "Not a directory", `touch nope/` -> "No such
        // file or directory", both rc 1; the old 1.x wording "cannot
        // touch" is the recorded divergence). -c on a missing path stays
        // silent like the plain missing case.
        if op.ends_with('/') || op.ends_with('\\') {
            let stripped = op.trim_end_matches(['/', '\\']);
            match fs_win::file_meta(ctx, stripped, true) {
                Ok(m) if m.is_dir => {
                    // touch the directory through the stripped spelling
                    if let Err(e) = fs_win::set_file_times(
                        ctx,
                        stripped,
                        None,
                        atime,
                        mtime,
                        true,
                        opts.no_dereference,
                    ) {
                        let _ = writeln!(
                            io.stderr,
                            "touch: setting times of '{op}': {}",
                            fs_win::win32_reason(e)
                        );
                        ok = false;
                    }
                    continue 'operands;
                }
                Ok(_) => {
                    // [GNU] probed `touch f.txt/` (existing regular file):
                    // "touch: setting times of 'f.txt/': Not a directory", rc 1
                    let _ = writeln!(
                        io.stderr,
                        "touch: setting times of '{op}': {}",
                        fs_win::win32_reason(267)
                    );
                    ok = false;
                    continue 'operands;
                }
                Err(e) => {
                    if opts.no_create {
                        // GNU -c on a missing path stays silent
                        continue 'operands;
                    }
                    let _ = writeln!(
                        io.stderr,
                        "touch: setting times of '{op}': {}",
                        fs_win::win32_reason(e)
                    );
                    ok = false;
                    continue 'operands;
                }
            }
        }
        // Direct-invocation glob contract (1.x utils glob_expand): a
        // wildcard operand expands over the named directory; an unmatched
        // pattern stays literal (the GNU glob default), so GNU's
        // creates-a-literal-'*'-file diagnostic keeps a shape.
        let expanded = if contains_wildcard(op) {
            glob_expand_in(ctx, op)
        } else {
            None
        };
        for f in expanded.unwrap_or_else(|| vec![op.clone()]) {
            // missing file: create unless -c
            let missing = fs_win::file_meta(ctx, &f, true).is_err();
            if missing && !opts.no_create {
                if let Err(e) = fs_win::create_file(ctx, &f) {
                    let _ = writeln!(
                        io.stderr,
                        "touch: cannot touch '{f}': {}",
                        fs_win::win32_reason(e)
                    );
                    ok = false;
                    continue;
                }
            }
            let is_dir = fs_win::file_meta(ctx, &f, true)
                .map(|m| m.is_dir)
                .unwrap_or(false);
            match fs_win::set_file_times(ctx, &f, None, atime, mtime, is_dir, opts.no_dereference) {
                Ok(()) => {}
                Err(e) => {
                    if missing && opts.no_create {
                        // GNU -c on a missing file: silent success
                        continue;
                    }
                    let _ = writeln!(
                        io.stderr,
                        "touch: setting times of '{f}': {}",
                        fs_win::win32_reason(e)
                    );
                    ok = false;
                }
            }
        }
    }
    Ok(if ok { 0 } else { 1 })
}

fn take_val(inline: Option<String>, args: &[OsString], idx: &mut usize) -> Option<String> {
    if inline.is_some() {
        return inline;
    }
    *idx += 1;
    args.get(*idx).map(|v| v.to_string_lossy().into_owned())
}

/// parse_datetime(): the GNU parse-datetime subset, probed against
/// coreutils 9.4 (grammar inventory in the module docs). Dispatch order
/// mirrors the 1.x build (which mirrors parse-datetime's item classes):
/// fixed (ISO/compact/epoch) -> military zone -> weekday -> relative ->
/// combined. `base` anchors relative items (the -r reference time when
/// one is given, else now; probed touch.c:354-360 behavior). Returns a
/// UTC FILETIME.
pub(crate) fn parse_datetime(s: &str, base: FileTime) -> Option<FileTime> {
    let s = s.trim();
    // GNU probed: an empty date is today at local midnight (rc 0)
    if s.is_empty() {
        return midnight_of(base);
    }
    if let Some(ft) = parse_fixed(s) {
        return Some(ft);
    }
    if let Some(ft) = parse_military(s) {
        return Some(ft);
    }
    if let Some(ft) = parse_weekday(s, base) {
        return Some(ft);
    }
    if let Some(ft) = parse_relative(s, base) {
        return Some(ft);
    }
    if let Some(ft) = parse_combined(s, base, 0) {
        return Some(ft);
    }
    parse_wordy(s, base)
}

/// Fixed wall-clock forms: @epoch, ISO "YYYY-MM-DD[ T]HH[:MM[:SS[.frac]]]",
/// loose single-digit fields, slash dates, basic "THHMM[SS]", compact
/// "YYYYMMDD[HHMM[SS]]", date-only, time-only, trailing timezone.
fn parse_fixed(s: &str) -> Option<FileTime> {
    let s = s.trim();
    // @epoch seconds (integer or decimal)
    if let Some(epoch) = s.strip_prefix('@') {
        let (int_part, frac) = match epoch.split_once('.') {
            Some((i, f)) => {
                if !f.bytes().all(|b| b.is_ascii_digit()) {
                    return None;
                }
                let nanos = format!("{:0<9}", f);
                (i, nanos.parse::<u32>().ok()?)
            }
            None => (epoch, 0),
        };
        if int_part.is_empty()
            || !int_part
                .bytes()
                .all(|b| b.is_ascii_digit() || b == b'-' || b == b'+')
        {
            return None;
        }
        let secs: i64 = int_part.parse().ok()?;
        return Some(FileTime::from_epoch(secs, frac));
    }
    // ISO 8601 basic format "THHMM[SS]" (probed coreutils 9.4: `touch -d
    // T1430` rc 0 = 14:30 today)
    let b = s.as_bytes();
    if b.len() >= 5
        && (b[0] == b'T' || b[0] == b't')
        && b[1..].iter().all(|c| c.is_ascii_digit())
        && (b.len() == 5 || b.len() == 7)
    {
        let today = local_today()?;
        let h: u32 = s[1..3].parse().ok()?;
        let mi: u32 = s[3..5].parse().ok()?;
        let ss: u32 = if b.len() == 7 {
            s[5..7].parse().ok()?
        } else {
            0
        };
        if h > 23 || mi > 59 || ss > 59 {
            return None;
        }
        return finish_civil(today.0, today.1, today.2, h, mi, ss, 0, i32::MIN);
    }
    // split date/time first, THEN a trailing timezone from the time part
    // (a whole-string zone scan would eat "-DD" out of a date)
    let (date_part, time_part0) = split_datetime(s)?;
    let (time_part, zone_min) = split_zone(time_part0)?;
    let _ = time_part0;
    let today = local_today()?;
    let (year, month, day) = if date_part.is_empty() {
        (today.0, today.1, today.2)
    } else if date_part.bytes().all(|b| b.is_ascii_digit()) {
        // compact "YYYYMMDD[HHMM[SS]]" (probed: 8/12/14 digits accepted;
        // the 12/14-digit hour/minute(/second) tail rides the same parse
        // as the -t stamp fields). A 6-digit body is rejected like the
        // 1.x build; GNU's verdict on 6 digits is not recorded here.
        match date_part.len() {
            8 => {
                let y: i64 = date_part[..4].parse().ok()?;
                let m: u32 = date_part[4..6].parse().ok()?;
                let d: u32 = date_part[6..8].parse().ok()?;
                (y, m, d)
            }
            12 | 14 => {
                let y: i64 = date_part[..4].parse().ok()?;
                let m: u32 = date_part[4..6].parse().ok()?;
                let d: u32 = date_part[6..8].parse().ok()?;
                let h: u32 = date_part[8..10].parse().ok()?;
                let mi: u32 = date_part[10..12].parse().ok()?;
                let s2: u32 = if date_part.len() == 14 {
                    date_part[12..14].parse().ok()?
                } else {
                    0
                };
                if m == 0
                    || m > 12
                    || d == 0
                    || d > days_in_month(y, m)
                    || h > 23
                    || mi > 59
                    || s2 > 59
                {
                    return None;
                }
                return finish_civil(y, m, d, h, mi, s2, 0, zone_min);
            }
            _ => return None,
        }
    } else {
        parse_ymd(date_part)?
    };
    let (hour, minute, second, nanos) = if time_part.is_empty() {
        (0, 0, 0, 0)
    } else {
        parse_hms(time_part)?
    };
    finish_civil(year, month, day, hour, minute, second, nanos, zone_min)
}

/// Zone application for a broken-down wall time (local when zone_min is
/// i32::MIN, else UTC shifted by the offset). Civil fields are carried
/// instead of a SystemTime so GNU-accepted out-of-SYSTEMTIME years
/// (`-d 0000-01-02`, probed rc 0) survive.
fn finish_civil(
    y: i64,
    mo: u32,
    d: u32,
    h: u32,
    mi: u32,
    s: u32,
    nanos: u32,
    zone_min: i32,
) -> Option<FileTime> {
    if zone_min == i32::MIN {
        local_wall_to_filetime(y, mo, d, h, mi, s, nanos)
    } else {
        // interpret the wall clock as UTC and shift by the given offset
        // (local = utc + min; so utc = local - min)
        let utc_secs = days_from_civil(y, mo as i64, d as i64) * 86_400
            + h as i64 * 3600
            + mi as i64 * 60
            + s as i64;
        clamp_epoch_filetime(utc_secs - zone_min as i64 * 60, nanos)
    }
}

/// UTC wall clock (no zone conversion) -> FILETIME.
fn utc_wall_to_filetime(st: &SystemTime) -> Option<FileTime> {
    #[cfg(windows)]
    {
        // days from civil algorithm
        let days = days_from_civil(st.year as i64, st.month as i64, st.day as i64);
        let secs = days * 86_400 + st.hour as i64 * 3600 + st.minute as i64 * 60 + st.second as i64;
        Some(FileTime::from_epoch(
            secs,
            st.milliseconds as u32 * 1_000_000,
        ))
    }
    #[cfg(not(windows))]
    {
        let _ = st;
        None
    }
}

fn days_from_civil(y: i64, m: i64, d: i64) -> i64 {
    let y = if m <= 2 { y - 1 } else { y };
    let era = if y >= 0 { y } else { y - 399 } / 400;
    let yoe = y - era * 400;
    let mp = (m + 9) % 12;
    let doy = (153 * mp + 2) / 5 + d - 1;
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    era * 146_097 + doe - 719_468
}

/// Split a trailing timezone: +HHMM, +HH:MM, -HHMM, Z, UTC, GMT.
/// Returns (body, offset_minutes) where i32::MIN means "local time".
fn split_zone(s: &str) -> Option<(&str, i32)> {
    let local = i32::MIN;
    let t = s.trim_end();
    for (word, off) in [("UTC", 0i32), ("GMT", 0i32)] {
        if let Some(body) = t.strip_suffix(word) {
            let body = body.trim_end();
            if !body.is_empty() {
                return Some((body, off));
            }
        }
    }
    if let Some(body) = t.strip_suffix('Z') {
        return Some((body.trim_end(), 0));
    }
    // [GNU] parse-datetime.y zone: "03:04:05 EST" strips a trailing
    // named/military zone WORD (last whitespace-separated token); a bare
    // zone word alone is a valid `zone` item too — probed `touch -d EST`
    // -> today 00:00 in that zone.
    if let Some(w) = t.split_whitespace().last() {
        if let Some(off) = named_zone_minutes(w) {
            return Some((t[..t.len() - w.len()].trim_end(), off));
        }
    }
    // numeric zone at the end: +HH:MM (6), +HHMM (5), +HH (3)
    let bytes = t.as_bytes();
    let sign_at = |p: usize| bytes[p] == b'+' || bytes[p] == b'-';
    let digits = |a: usize, b: usize| bytes[a..b].iter().all(|x| x.is_ascii_digit());
    if bytes.len() >= 6 {
        let p = bytes.len() - 6;
        if sign_at(p) && digits(p + 1, p + 3) && bytes[p + 3] == b':' && digits(p + 4, p + 6) {
            let h: i32 = t[p + 1..p + 3].parse().ok()?;
            let m: i32 = t[p + 4..p + 6].parse().ok()?;
            let sign = if bytes[p] == b'-' { -1 } else { 1 };
            return Some((t[..p].trim_end(), sign * (h * 60 + m)));
        }
    }
    if bytes.len() >= 5 {
        let p = bytes.len() - 5;
        if sign_at(p) && digits(p + 1, p + 3) && digits(p + 3, p + 5) {
            let h: i32 = t[p + 1..p + 3].parse().ok()?;
            let m: i32 = t[p + 3..p + 5].parse().ok()?;
            let sign = if bytes[p] == b'-' { -1 } else { 1 };
            return Some((t[..p].trim_end(), sign * (h * 60 + m)));
        }
    }
    if bytes.len() >= 3 {
        let p = bytes.len() - 3;
        if sign_at(p) && digits(p + 1, p + 3) {
            let h: i32 = t[p + 1..p + 3].parse().ok()?;
            let sign = if bytes[p] == b'-' { -1 } else { 1 };
            return Some((t[..p].trim_end(), sign * h * 60));
        }
    }
    Some((t, local))
}

/// gnulib parse-datetime.y universal_time_zone_table (:1120-1126) plus
/// time_zone_table (:1133-1191): name -> minutes east of UTC. The
/// tDAYZONE entries carry their +60 inside the stored offset (GNU adds
/// it at parse-datetime.y:789 `pc->time_zone = $1 + 60 * 60`).
const NAMED_ZONES: &[(&str, i32)] = &[
    ("GMT", 0),
    ("UT", 0),
    ("UTC", 0),
    ("WET", 0),
    ("WEST", 60),
    ("BST", 60),
    ("ART", -180),
    ("BRT", -180),
    ("BRST", -120),
    ("NST", -210),
    ("NDT", -150),
    ("AST", -240),
    ("ADT", -180),
    ("CLT", -240),
    ("CLST", -180),
    ("EST", -300),
    ("EDT", -240),
    ("CST", -360),
    ("CDT", -300),
    ("MST", -420),
    ("MDT", -360),
    ("PST", -480),
    ("PDT", -420),
    ("AKST", -540),
    ("AKDT", -480),
    ("HST", -600),
    ("HAST", -600),
    ("HADT", -540),
    ("SST", -720),
    ("WAT", 60),
    ("CET", 60),
    ("CEST", 120),
    ("MET", 60),
    ("MEZ", 60),
    ("MEST", 120),
    ("MESZ", 120),
    ("EET", 120),
    ("EEST", 180),
    ("CAT", 120),
    ("SAST", 120),
    ("EAT", 180),
    ("MSK", 180),
    ("MSD", 240),
    ("IST", 330),
    ("SGT", 480),
    ("KST", 540),
    ("JST", 540),
    ("GST", 600),
    ("NZST", 720),
    ("NZDT", 780),
];

/// The named-zone half of GNU's zone items (probed case-insensitive:
/// `est`, `eSt` accepted). Military letters are NOT here — they are a
/// separate grammar class and a one-letter word would collide with
/// operands of the wordy-date forms.
fn named_zone_minutes(word: &str) -> Option<i32> {
    let up = word.to_ascii_uppercase();
    NAMED_ZONES.iter().find(|(n, _)| *n == up).map(|(_, o)| *o)
}

/// Any GNU zone word: the named table plus the single military letters
/// (parse-datetime.y military_table :1193-1220: A-I = UTC+1..+9,
/// K-M = +10..+12, N-Y = -1..-12, Z = UTC, J = the local zone; 'T' is
/// the ISO separator, never a zone). This is the lexer class GNU uses
/// for 'a fortnight' — 'a' scans as military zone A (UTC+1), which is
/// why the whole expression lands 3600 s earlier (probed).
fn zone_word_minutes(word: &str) -> Option<i32> {
    if let Some(off) = named_zone_minutes(word) {
        return Some(off);
    }
    let b = word.as_bytes();
    if b.len() != 1 || !b[0].is_ascii_alphabetic() {
        return None;
    }
    match b[0].to_ascii_lowercase() {
        b'j' => local_bias_minutes(), // 'J' = the local zone
        b'z' => Some(0),
        l @ b'a'..=b'i' => Some((l - b'a') as i32 * 60 + 60),
        l @ b'k'..=b'm' => Some((l - b'a') as i32 * 60),
        l @ b'n'..=b'y' => Some(-((l - b'n') as i32 * 60 + 60)),
        _ => None,
    }
}

/// Local zone bias (local - utc) in minutes, from the running system —
/// the same conversion fs_win::to_local performs for timestamps.
fn local_bias_minutes() -> Option<i32> {
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .ok()?;
    let ft = FileTime::from_epoch(now.as_secs() as i64, now.subsec_nanos());
    let (_, min) = fs_win::to_local(ft.as_u64() as i64)?;
    Some(min)
}

/// Epoch seconds -> FILETIME clamped to the FILETIME domain. GNU lets
/// mktime produce any year and the filesystem pins the result into its
/// range (the WSL probes show the ext4 floor INT32_MIN for `-t
/// 000001010000`); NTFS's floor is FILETIME 0.
fn clamp_epoch_filetime(epoch_secs: i64, nanos: u32) -> Option<FileTime> {
    let ticks =
        epoch_secs as i128 * 10_000_000 + 116_444_736_000_000_000i128 + (nanos / 100) as i128;
    // Floor is 1, not 0: FILETIME 0 is SetFileTime's "leave unchanged"
    // sentinel (fs_win::set_file_times maps the omit case to it), so the
    // earliest writable stamp is 100ns past 1601-01-01 — the NTFS
    // analogue of the ext4 INT32_MIN floor GNU lands on.
    Some(FileTime::from_u64(ticks.clamp(1, u64::MAX as i128) as u64))
}

/// Naive local wall time -> UTC FILETIME for the -d/-t parsers. Years
/// inside SYSTEMTIME's range keep the DST-aware TzSpecific conversion;
/// GNU-accepted years outside it (posixtm mktime accepts year 0, GNU
/// probed `-t 000001010000` and `-d 0000-01-02` rc 0) fall back to civil
/// arithmetic against the current local bias and clamp into the
/// FILETIME domain — the NTFS analogue of the filesystem floor the
/// oracle lands on.
fn local_wall_to_filetime(
    y: i64,
    mo: u32,
    d: u32,
    h: u32,
    mi: u32,
    s: u32,
    nanos: u32,
) -> Option<FileTime> {
    if (1601..=30827).contains(&y) {
        let st = SystemTime {
            year: y as u16,
            month: mo as u16,
            day_of_week: 0,
            day: d as u16,
            hour: h as u16,
            minute: mi as u16,
            second: s as u16,
            milliseconds: (nanos / 1_000_000) as u16,
        };
        if let Some(ft) = fs_win::from_local(&st) {
            return Some(ft);
        }
        // Conversion failed (e.g. a 1601 wall time underflowing the
        // FILETIME epoch in an east-of-UTC zone) -> arithmetic + clamp.
    }
    let naive = days_from_civil(y, mo as i64, d as i64) * 86_400
        + h as i64 * 3600
        + mi as i64 * 60
        + s as i64;
    let epoch = naive - local_bias_minutes()? as i64 * 60;
    clamp_epoch_filetime(epoch, nanos)
}

/// Split "DATE TIME" / "TDATE" / date-only / time-only / basic "THHMM[SS]".
fn split_datetime(body: &str) -> Option<(&str, &str)> {
    if let Some(pos) = body.find(['T', 't']) {
        // 2004-02-29T16:21:42 (the T must sit between digits)
        let b = body.as_bytes();
        if pos > 0
            && b[pos - 1].is_ascii_digit()
            && pos + 1 < b.len()
            && b[pos + 1].is_ascii_digit()
        {
            return Some((&body[..pos], &body[pos + 1..]));
        }
    }
    if let Some(pos) = body.find(' ') {
        return Some((&body[..pos], body[pos + 1..].trim_start()));
    }
    // date-only (has '-' or is the compact all-digit form) or time-only
    if body.contains('-') || (!body.is_empty() && body.bytes().all(|b| b.is_ascii_digit())) {
        Some((body, ""))
    } else {
        Some(("", body))
    }
}

fn parse_ymd(s: &str) -> Option<(i64, u32, u32)> {
    // ISO "YYYY-MM-DD"; loose single-digit fields are GNU-accepted
    // ("2026-6-27"), so widths are variable. Slash dates ("2026/05/06",
    // probed rc 0) take the same shape on '/'.
    let (sep, alt): (char, char) = if s.contains('-') {
        ('-', '/')
    } else if s.contains('/') {
        ('/', '-')
    } else {
        return None;
    };
    let parts: Vec<&str> = s.split(sep).collect();
    if parts.len() != 3 || parts.iter().any(|p| p.contains(alt)) {
        return None;
    }
    if parts
        .iter()
        .any(|p| p.is_empty() || !p.bytes().all(|b| b.is_ascii_digit()))
    {
        return None;
    }
    let y: i64 = parts[0].parse().ok()?;
    let m: u32 = parts[1].parse().ok()?;
    let d: u32 = parts[2].parse().ok()?;
    // GNU mktime verifies the broken-down date: 2020-02-30 / month 13
    // are invalid (probed "invalid date format"), not normalized.
    if !(1..=12).contains(&m) || !(1..=31).contains(&d) || d > days_in_month(y, m) {
        return None;
    }
    Some((y, m, d))
}

fn parse_hms(s: &str) -> Option<(u32, u32, u32, u32)> {
    let (hms, frac) = match s.split_once('.') {
        Some((a, f)) => {
            if !f.bytes().all(|b| b.is_ascii_digit()) {
                return None;
            }
            let nanos = format!("{:0<9}", f);
            (a, nanos.parse::<u32>().ok()?)
        }
        None => (s, 0),
    };
    let parts: Vec<&str> = hms.split(':').collect();
    let (h, m, sec) = match parts.len() {
        // bare hour ("2026-05-06 07") is valid GNU parse-datetime:
        // probed coreutils 9.4 `touch -d '2026-05-06 07' f` -> rc 0 (07:00)
        1 => (parts[0], "0", "0"),
        2 => (parts[0], parts[1], "0"),
        3 => (parts[0], parts[1], parts[2]),
        _ => return None,
    };
    if parts
        .iter()
        .any(|p| p.is_empty() || !p.bytes().all(|b| b.is_ascii_digit()))
    {
        return None;
    }
    let h: u32 = h.parse().ok()?;
    let m: u32 = m.parse().ok()?;
    let sec: u32 = sec.parse().ok()?;
    // GNU rejects :60 here (probed `touch -d '2020-01-02 03:04:60'` rc
    // 1); the leap-second acceptance lives in the -t stamp path only
    // (posixtm.c:201-207).
    if h > 23 || m > 59 || sec > 59 {
        return None;
    }
    Some((h, m, sec, frac))
}

fn local_today() -> Option<(i64, u32, u32)> {
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .ok()?;
    let ft = FileTime::from_epoch(now.as_secs() as i64, now.subsec_nanos());
    let (local, _) = fs_win::to_local(ft.as_u64() as i64)?;
    Some((local.year as i64, local.month as u32, local.day as u32))
}

/// 00:00:00 of `ft`'s local date (the effective zone), DST aware.
fn midnight_of(ft: FileTime) -> Option<FileTime> {
    let (mut local, _) = fs_win::to_local(ft.as_u64() as i64)?;
    local.hour = 0;
    local.minute = 0;
    local.second = 0;
    local.milliseconds = 0;
    fs_win::from_local(&local)
}

fn days_in_month(y: i64, m: u32) -> u32 {
    match m {
        1 | 3 | 5 | 7 | 8 | 10 | 12 => 31,
        4 | 6 | 9 | 11 => 30,
        2 => {
            if (y % 4 == 0 && y % 100 != 0) || y % 400 == 0 {
                29
            } else {
                28
            }
        }
        _ => 0,
    }
}

/// Day of week (0 = Sunday) for a civil date.
fn civil_dow(y: i64, m: u32, d: u32) -> u32 {
    let days = days_from_civil(y, m as i64, d as i64);
    (((days + 4) % 7 + 7) % 7) as u32
}

/// [GNU] military timezone specs (parse-datetime.y military_table, ported
/// from the 1.x build): "<HH|HHMM><L>" or a lone "<L>" meaning today
/// 00:00 in that zone. A-I = UTC+1..+9, K-M = UTC+10..+12 (J is skipped
/// in the alphabet), N-Y = UTC-1..-12, Z = UTC, J = the local zone. 'T'
/// is not a zone. "<HH>:<MM>[:<SS>]<L>" is accepted too ("9:30j").
fn parse_military(s: &str) -> Option<FileTime> {
    let t = s.trim();
    let bytes = t.as_bytes();
    let (h, mi, sec, letter) = if bytes.len() >= 2
        && bytes[bytes.len() - 1].is_ascii_alphabetic()
        && bytes[..bytes.len() - 1]
            .iter()
            .all(|c| c.is_ascii_digit() || *c == b':')
    {
        // "9:30j" / "1430Z" / "16z"
        let letter = bytes[bytes.len() - 1] as char;
        let body = &t[..t.len() - 1];
        let parts: Vec<&str> = body.split(':').collect();
        match parts.len() {
            1 => {
                let digits = parts[0];
                if digits.is_empty() || !digits.bytes().all(|b| b.is_ascii_digit()) {
                    return None;
                }
                let (h, mi) = match digits.len() {
                    1 | 2 => (digits.parse::<u32>().ok()?, 0),
                    3 | 4 => (
                        digits[..digits.len() - 2].parse::<u32>().ok()?,
                        digits[digits.len() - 2..].parse::<u32>().ok()?,
                    ),
                    _ => return None,
                };
                (h, mi, 0u32, letter)
            }
            2 | 3 => {
                if !parts
                    .iter()
                    .all(|p| !p.is_empty() && p.bytes().all(|b| b.is_ascii_digit()))
                {
                    return None;
                }
                let h: u32 = parts[0].parse().ok()?;
                let mi: u32 = parts[1].parse().ok()?;
                let sec: u32 = if parts.len() == 3 {
                    parts[2].parse().ok()?
                } else {
                    0
                };
                (h, mi, sec, letter)
            }
            _ => return None,
        }
    } else if bytes.len() == 1 && bytes[0].is_ascii_alphabetic() {
        (0u32, 0u32, 0u32, bytes[0] as char)
    } else {
        return None;
    };
    let letter = letter.to_ascii_lowercase();
    if letter == 't' || h > 23 || mi > 59 || sec > 59 {
        return None;
    }
    let east: Option<i32> = match letter {
        'j' => None, // 'J' = the local zone
        'a'..='i' => Some(((letter as u8 - b'a') as i32 + 1) * 60),
        'k'..='m' => Some(((letter as u8 - b'a') as i32) * 60),
        'z' => Some(0),
        'n'..='y' => Some(-((letter as u8 - b'n') as i32 + 1) * 60),
        _ => return None,
    };
    let today = local_today()?;
    let st = SystemTime {
        year: today.0 as u16,
        month: today.1 as u16,
        day_of_week: 0,
        day: today.2 as u16,
        hour: h as u16,
        minute: mi as u16,
        second: sec as u16,
        milliseconds: 0,
    };
    match east {
        None => fs_win::from_local(&st),
        Some(min) => {
            let utc_ft = utc_wall_to_filetime(&st)?;
            let adjusted = utc_ft.as_u64() as i64 - (min as i64) * 600_000_000i64;
            Some(FileTime::from_u64(adjusted as u64))
        }
    }
}

fn weekday_number(name: &str) -> Option<u32> {
    let n = name.to_ascii_lowercase();
    Some(match n.as_str() {
        "sunday" | "sun" => 0,
        "monday" | "mon" => 1,
        "tuesday" | "tue" | "tues" => 2,
        "wednesday" | "wed" => 3,
        "thursday" | "thu" | "thur" | "thurs" => 4,
        "friday" | "fri" => 5,
        "saturday" | "sat" => 6,
        _ => return None,
    })
}

/// [GNU] Weekday items (issue #1382): "Monday", "next Monday", "last
/// Monday". A bare name is the nearest such day, today counting (+0..6);
/// "next" pushes a same-day match a full week out (+7), "last" mirrors it
/// backwards (-7..0 with same-day at -7). The result is 00:00:00 of the
/// target date in the effective zone.
fn parse_weekday(s: &str, base: FileTime) -> Option<FileTime> {
    let t = s.trim();
    let lower = t.to_ascii_lowercase();
    enum Mode {
        Bare,
        Next,
        Last,
    }
    let (mode, name) = if let Some(rest) = lower.strip_prefix("next ") {
        (Mode::Next, rest.trim())
    } else if let Some(rest) = lower.strip_prefix("last ") {
        (Mode::Last, rest.trim())
    } else {
        (Mode::Bare, lower.as_str())
    };
    let target = weekday_number(name.trim())?;
    let ord: i64 = match mode {
        Mode::Bare => 0,
        Mode::Next => 1,
        Mode::Last => -1,
    };
    shift_to_weekday(base, target, ord)
}

/// Weekday shift for a GNU `day` item anchored at `ft` (parse-datetime.y
/// day productions :791-808): ord 0 is the next such weekday with today
/// counting; ord >= 1 the ord-th occurrence strictly after today
/// ('next'/'first' = 1); ord < 0 the |ord|-th before today ('last' =
/// -1). The result lands on 00:00 of the target date in the effective
/// zone.
fn shift_to_weekday(ft: FileTime, target: u32, ord: i64) -> Option<FileTime> {
    let (local, _) = fs_win::to_local(ft.as_u64() as i64)?;
    let base_dow = civil_dow(local.year as i64, local.month as u32, local.day as u32);
    let delta = (target + 7 - base_dow) % 7;
    let offset: i64 = if ord == 0 {
        delta as i64
    } else if ord > 0 {
        (if delta == 0 { 7 } else { delta as i64 }) + (ord - 1) * 7
    } else {
        (if delta == 0 { -7 } else { delta as i64 - 7 }) - (-ord - 1) * 7
    };
    let shifted = FileTime::from_u64((ft.as_u64() as i64 + offset * 86_400_000_000i64) as u64);
    midnight_of(shifted)
}

/// GNU relative_time_table ordinal words (parse-datetime.y :1098-1115).
/// SECOND is commented out there, so 'second day' scans as +1s +1d
/// (probed), never as the ordinal.
fn ordinal_word(w: &str) -> Option<i64> {
    Some(match w {
        "last" => -1,
        "this" => 0,
        "next" | "first" => 1,
        "third" => 3,
        "fourth" => 4,
        "fifth" => 5,
        "sixth" => 6,
        "seventh" => 7,
        "eighth" => 8,
        "ninth" => 9,
        "tenth" => 10,
        "eleventh" => 11,
        "twelfth" => 12,
        _ => return None,
    })
}

fn is_number(w: &str) -> bool {
    !w.is_empty()
        && w.bytes()
            .all(|b| b.is_ascii_digit() || b == b'+' || b == b'-')
}

fn is_rel_unit(w: &str) -> bool {
    matches!(
        w,
        "fortnight"
            | "fortnights"
            | "second"
            | "seconds"
            | "sec"
            | "secs"
            | "minute"
            | "minutes"
            | "min"
            | "mins"
            | "hour"
            | "hours"
            | "day"
            | "days"
            | "week"
            | "weeks"
            | "month"
            | "months"
            | "year"
            | "years"
    )
}

/// Apply one relative item: month/year are calendar units, not fixed
/// second counts ("3 months ago" rolls the wall-clock month back three
/// positions, clamping the day into the target month).
fn apply_rel_unit(ft: FileTime, unit: &str, amount: i64) -> Option<FileTime> {
    match unit {
        "month" | "months" => shift_months(ft, amount),
        "year" | "years" => shift_months(ft, amount.saturating_mul(12)),
        "second" | "seconds" | "sec" | "secs" => add_seconds_ft(ft, amount),
        "minute" | "minutes" | "min" | "mins" => add_seconds_ft(ft, amount.saturating_mul(60)),
        "hour" | "hours" => add_seconds_ft(ft, amount.saturating_mul(3600)),
        "day" | "days" => add_seconds_ft(ft, amount.saturating_mul(86_400)),
        "week" | "weeks" => add_seconds_ft(ft, amount.saturating_mul(7 * 86_400)),
        // [GNU] a fortnight is fourteen days (issue #1382)
        "fortnight" | "fortnights" => add_seconds_ft(ft, amount.saturating_mul(14 * 86_400)),
        _ => None,
    }
}

/// [GNU] Relative items (parse-datetime.y relunit :930-970 +
/// lookup_word's plural/unit recognition :1405-1415): a SEQUENCE of
/// items, each `[amount] unit [ago|hence]`. The amount may be a signed
/// number, an ordinal word (next/last/this/first/third..twelfth —
/// probed "this day" = +0, "next fortnight" = +14d), or omitted = 1
/// (probed `touch -d fortnight`, `day ago`, `hours`, `mins ago` rc 0).
/// `ago`/`hence` apply to the item they follow, so items accumulate
/// ("2 days 3 hours" = 51h, "day day" = +2d, "a day ago" = -1d-3600s).
/// A zone WORD is a zone item that shifts the anchored result by its
/// offset — the 'a fortnight' quirk: 'a' scans as military zone A
/// (UTC+1) so the whole expression lands one hour earlier (probed).
fn parse_relative(s: &str, base: FileTime) -> Option<FileTime> {
    let t = s.trim().to_ascii_lowercase();
    match t.as_str() {
        // GNU dayshift words keep the time of day (probed: `touch -d
        // today` leaves the file at the invocation second, not 00:00).
        "now" | "today" => return Some(base),
        "yesterday" => return add_seconds_ft(base, -86_400),
        "tomorrow" => return add_seconds_ft(base, 86_400),
        _ => {}
    }
    let mut ft = base;
    let mut zone_min: Option<i32> = None;
    let mut items = 0usize;
    let mut toks = t.split_whitespace().peekable();
    while let Some(tok) = toks.next() {
        // zone item — last one wins
        if let Some(off) = zone_word_minutes(tok) {
            zone_min = Some(off);
            continue;
        }
        // day item: [tORDINAL] tDAY
        let mut amount: i64 = 1;
        let tok = if let Some(ord) = ordinal_word(tok) {
            if let Some(wd) = toks.peek().and_then(|w| weekday_number(w)) {
                let _ = toks.next();
                ft = shift_to_weekday(ft, wd, ord)?;
                items += 1;
                continue;
            }
            amount = ord;
            toks.next()?
        } else if let Some(wd) = weekday_number(tok) {
            ft = shift_to_weekday(ft, wd, 0)?;
            items += 1;
            continue;
        } else {
            tok
        };
        // dayshift item
        match tok {
            "tomorrow" => {
                ft = add_seconds_ft(ft, 86_400)?;
                items += 1;
                continue;
            }
            "yesterday" => {
                ft = add_seconds_ft(ft, -86_400)?;
                items += 1;
                continue;
            }
            "today" | "now" => {
                items += 1;
                continue;
            }
            _ => {}
        }
        // [number] unit
        if !is_rel_unit(tok) {
            if !is_number(tok) {
                return None;
            }
            amount = tok.parse().ok()?;
            let Some(u) = toks.next() else { return None };
            if !is_rel_unit(u) {
                return None;
            }
            let u2 = u;
            if let Some(&m) = toks.peek() {
                if m == "ago" {
                    amount = -amount;
                    let _ = toks.next();
                } else if m == "hence" {
                    let _ = toks.next();
                }
            }
            ft = apply_rel_unit(ft, u2, amount)?;
            items += 1;
            continue;
        }
        if let Some(&m) = toks.peek() {
            if m == "ago" {
                amount = -amount;
                let _ = toks.next();
            } else if m == "hence" {
                let _ = toks.next();
            }
        }
        ft = apply_rel_unit(ft, tok, amount)?;
        items += 1;
    }
    if items == 0 {
        return None;
    }
    // zone items shift the whole anchored result (probed 'a fortnight'
    // = +14d - 3600s, 'EST fortnight' = +14d + 5h)
    if let Some(off) = zone_min {
        ft = add_seconds_ft(ft, -(off as i64) * 60)?;
    }
    Some(ft)
}

fn add_seconds_ft(base: FileTime, secs: i64) -> Option<FileTime> {
    Some(FileTime::from_u64(
        (base.as_u64() as i64 + secs * 10_000_000i64) as u64,
    ))
}

/// [GNU] month/year are calendar units (1.x shift_months, coreutils
/// touch.c date relative handling): roll the wall-clock month by `months`
/// positions, clamping the day into the target month (Mar 31 - 1 month ->
/// Feb 28/29; Feb 29 - 1 year -> Feb 28).
fn shift_months(base: FileTime, months: i64) -> Option<FileTime> {
    let (mut st, _) = fs_win::to_local(base.as_u64() as i64)?;
    let total = st.year as i64 * 12 + (st.month as i64 - 1) + months;
    let mut year = total.div_euclid(12);
    let mut month = total.rem_euclid(12) + 1;
    if year < 1601 || year > 9999 {
        return None;
    }
    let mut day = st.day as i64;
    while day > days_in_month(year, month as u32) as i64 {
        day -= days_in_month(year, month as u32) as i64;
        month += 1;
        if month > 12 {
            month = 1;
            year += 1;
            if year > 9999 {
                return None;
            }
        }
    }
    st.year = year as u16;
    st.month = month as u16;
    st.day = day as u16;
    fs_win::from_local(&st)
}

/// [GNU] A date string may end with one relative item applied to a base
/// timestamp ("2026-03-31 1 month ago" -> 2026-03-03) or one weekday item
/// ("2020-01-02 Monday"; issue #1382). Split the trailing item off and
/// parse the remainder as the base (recursion bounded like the 1.x).
fn parse_combined(s: &str, base: FileTime, depth: u32) -> Option<FileTime> {
    if depth > 4 {
        return None;
    }
    let t = s.trim();
    if t.is_empty() {
        return None;
    }
    let tokens: Vec<&str> = t.split_whitespace().collect();
    // tail shapes: <head> <amount|next|last> <unit> [ago|hence]
    //              <head> [next|last] <weekday>
    if tokens.len() >= 3 {
        let last = tokens[tokens.len() - 1].to_ascii_lowercase();
        let unit_idx = if last == "ago" || last == "hence" {
            tokens.len() - 2
        } else {
            tokens.len() - 1
        };
        let unit = tokens[unit_idx].to_ascii_lowercase();
        let is_unit = matches!(
            unit.as_str(),
            "fortnight"
                | "fortnights"
                | "second"
                | "seconds"
                | "sec"
                | "secs"
                | "minute"
                | "minutes"
                | "min"
                | "mins"
                | "hour"
                | "hours"
                | "day"
                | "days"
                | "week"
                | "weeks"
                | "month"
                | "months"
                | "year"
                | "years"
        );
        if is_unit && unit_idx >= 1 {
            let amount_word = tokens[unit_idx - 1];
            let amount_l = amount_word.to_ascii_lowercase();
            // GNU's amount slot also takes ordinal words and zone words
            // ("2020-01-02 a fortnight" -> head date +14d -3600s).
            let is_amount = is_number(&amount_l)
                || ordinal_word(&amount_l).is_some()
                || zone_word_minutes(&amount_l).is_some();
            if is_amount {
                let head = tokens[..unit_idx - 1].join(" ");
                if head.is_empty() {
                    return None;
                }
                let base_ft = parse_head(&head, base, depth)?;
                let mut rel = format!("{amount_word} {unit}");
                if unit_idx + 1 < tokens.len() && unit_idx != tokens.len() - 1 {
                    rel.push(' ');
                    rel.push_str(tokens[tokens.len() - 1]);
                }
                return parse_relative(&rel, base_ft);
            }
        }
    }
    // weekday tail
    if tokens.len() >= 2 {
        let mut idx = tokens.len() - 1;
        let mut modifier = "";
        let last = tokens[idx].to_ascii_lowercase();
        if weekday_number(&last).is_some() {
            if idx >= 1 {
                let prev = tokens[idx - 1].to_ascii_lowercase();
                if prev == "next" || prev == "last" {
                    modifier = &tokens[idx - 1];
                    idx -= 1;
                }
            }
            let head = tokens[..idx].join(" ");
            if !head.is_empty() {
                let base_ft = parse_head(&head, base, depth)?;
                let item = if modifier.is_empty() {
                    last
                } else {
                    format!("{modifier} {last}")
                };
                return parse_weekday(&item, base_ft);
            }
        }
    }
    None
}

fn parse_head(head: &str, base: FileTime, depth: u32) -> Option<FileTime> {
    if let Some(ft) = parse_fixed(head) {
        return Some(ft);
    }
    if let Some(ft) = parse_combined(head, base, depth + 1) {
        return Some(ft);
    }
    parse_relative(head, base)
}

/// [GNU] Month names (parse-datetime.y month_and_day_table + the
/// lookup_word abbrev rule: a 3-letter word, or 3 letters + '.', matches
/// a name's first three; the literal 'SEPT' is a table entry).
fn month_number(w: &str) -> Option<u32> {
    const MONTHS: [&str; 12] = [
        "january",
        "february",
        "march",
        "april",
        "may",
        "june",
        "july",
        "august",
        "september",
        "october",
        "november",
        "december",
    ];
    let w = w.trim_end_matches('.');
    if w == "sept" {
        return Some(9);
    }
    for (i, m) in MONTHS.iter().enumerate() {
        if w == *m || (w.len() == 3 && m.starts_with(w)) {
            return Some(i as u32 + 1);
        }
    }
    None
}

/// [GNU] Month-name dates (parse-datetime.y date :865-906, the `number`
/// item digits_to_date_time :273-296, lookup_word :1372-1436). Shapes:
///   "Jan 2 2020" "January 2, 2020" "Dec 25"    (month day [year])
///   "2 Jan 2020" "17-JUN-1992" "JUN-17-1992"   (day month [year])
/// with a weekday (ignored once a definite date exists — probed "Sat
/// 2020-01-02" -> the ISO date unchanged and "mon jan 2 2020"), an
/// HH:MM[:SS] clock, am/pm meridian words and zone words in any order
/// (probed "utc jan 2 2020", "jan 2 z 2020", "jan 2 2020 3pm").
///
/// Number slots follow GNU's item rules: a number directly before the
/// month is the day and a following number is the year with the XPG4
/// two-digit adjust ("2 jan 70" -> 1970, to_tm_year :1295-1317). After
/// "month day" a comma-separated number is the year ("jan 2, 69" ->
/// 1969); a bare number of more than two digits is the year ("jan 2
/// 2020", "jan 2 555"); a 2-digit bare number is a 24-hour clock value
/// — "jan 2 20" = 20:00 and "jan 2 69" is INVALID (probed). A zone word
/// plus a bare number alone ("est 5") is today at that hour in the zone
/// (probed).
fn parse_wordy(s: &str, base: FileTime) -> Option<FileTime> {
    let _ = base;
    // Tokenize on whitespace and commas, remembering which tokens were
    // comma-separated — "jan 2, 69" binds the year, "dec 25, 03:04:05"
    // is invalid (the comma production needs a number).
    let mut toks: Vec<String> = Vec::new();
    let mut comma_before: Vec<bool> = Vec::new();
    let mut pending = false;
    for raw in s.split_whitespace() {
        let mut cb = pending;
        for (k, p) in raw.split(',').enumerate() {
            if p.is_empty() {
                cb = true;
                continue;
            }
            if k > 0 {
                cb = true;
            }
            toks.push(p.to_ascii_lowercase());
            comma_before.push(cb);
            cb = false;
        }
        pending = cb;
    }
    for (i, tok) in toks.iter().enumerate() {
        if comma_before[i] && !is_number(tok) {
            return None;
        }
    }

    let mut month: Option<u32> = None;
    let mut day: Option<u32> = None;
    let mut year: Option<i64> = None;
    let mut hour = 0u32;
    let mut minute = 0u32;
    let mut second = 0u32;
    let mut nanos = 0u32;
    let mut clock_seen = false;
    let mut meridian: Option<bool> = None; // true = pm
    let mut zone_min = i32::MIN;
    let mut month_idx = usize::MAX;
    let mut day_before_month = false;
    let mut numbers: Vec<(usize, i64, usize)> = Vec::new(); // (tok idx, value, digit count)
    let mut rels: Vec<(i64, String)> = Vec::new(); // ordinal-word rel items ("this year")

    let mut i = 0usize;
    while i < toks.len() {
        let raw = &toks[i];
        let mut tok = raw.as_str();
        i += 1;
        match tok {
            "am" | "a.m." => {
                meridian = Some(false);
                continue;
            }
            "pm" | "p.m." => {
                meridian = Some(true);
                continue;
            }
            _ => {}
        }
        // attached meridian: "3pm", "12am", "3:04pm"
        if let Some(core) = tok.strip_suffix("pm") {
            if !core.is_empty() && (is_number(core) || core.contains(':')) {
                meridian = Some(true);
                tok = core;
            }
        } else if let Some(core) = tok.strip_suffix("am") {
            if !core.is_empty() && (is_number(core) || core.contains(':')) {
                meridian = Some(false);
                tok = core;
            }
        }
        // weekday items — ignored next to a definite date (GNU probed);
        // a preceding ordinal word pairs with it and is skipped too.
        if weekday_number(tok).is_some() {
            continue;
        }
        if let Some(ord) = ordinal_word(tok) {
            let next = toks.get(i).map(|t| t.as_str());
            match next {
                Some(n) if weekday_number(n).is_some() => i += 1,
                Some(n) if is_rel_unit(n) => {
                    rels.push((ord, n.to_string()));
                    i += 1;
                }
                _ => return None,
            }
            continue;
        }
        if let Some(m) = month_number(tok) {
            if month.is_some() {
                return None;
            }
            month = Some(m);
            month_idx = i - 1;
            continue;
        }
        if let Some(off) = zone_word_minutes(tok) {
            zone_min = off;
            continue;
        }
        if tok.contains(':') {
            let (h, mi, se, ns) = parse_hms(tok)?;
            (hour, minute, second, nanos) = (h, mi, se, ns);
            clock_seen = true;
            continue;
        }
        if is_number(tok) && tok.parse::<i64>().is_ok() {
            let digits = tok.trim_start_matches(['+', '-']).len();
            numbers.push((i - 1, tok.parse().ok()?, digits));
            continue;
        }
        if tok.contains('-') {
            let parts: Vec<&str> = tok.split('-').collect();
            if parts.len() == 3 {
                if let (Ok(a), Ok(b), Ok(c)) = (
                    parts[0].parse::<i64>(),
                    parts[1].parse::<i64>(),
                    parts[2].parse::<i64>(),
                ) {
                    // ISO / US dash date "2020-01-02" / "01-02-2020":
                    // GNU reads the >=3-digit field as the year.
                    if month.is_some() || day.is_some() {
                        return None;
                    }
                    let (y, m, d) = if parts[0].len() >= 3 {
                        (a, b, c)
                    } else {
                        (c, a, b)
                    };
                    month = Some(u32::try_from(m).ok()?);
                    day = Some(u32::try_from(d).ok()?);
                    year = Some(y);
                    month_idx = i - 1;
                    continue;
                }
                if let (Ok(d), Some(m), Ok(y)) = (
                    parts[0].parse::<u32>(),
                    month_number(parts[1]),
                    parts[2].parse::<i64>(),
                ) {
                    // "17-JUN-1992" — GNU's tUNUMBER tMONTH tSNUMBER
                    if month.is_some() || day.is_some() {
                        return None;
                    }
                    day = Some(d);
                    month = Some(m);
                    month_idx = i - 1;
                    year = Some(y);
                    continue;
                }
                if let (Some(m), Ok(d), Ok(y)) = (
                    month_number(parts[0]),
                    parts[1].parse::<u32>(),
                    parts[2].parse::<i64>(),
                ) {
                    // "JUN-17-1992" — tMONTH tSNUMBER tSNUMBER
                    if month.is_some() || day.is_some() {
                        return None;
                    }
                    month = Some(m);
                    day = Some(d);
                    month_idx = i - 1;
                    year = Some(y);
                    continue;
                }
            }
            return None;
        }
        return None;
    }

    for &(idx, val, digits) in &numbers {
        if day.is_none() && month.is_some() {
            if idx + 1 == month_idx {
                day = Some(u32::try_from(val).ok()?);
                day_before_month = true;
                continue;
            }
            if idx == month_idx + 1 {
                day = Some(u32::try_from(val).ok()?);
                continue;
            }
        }
        if year.is_none()
            && (comma_before[idx] || digits > 2 || (day_before_month && idx == month_idx + 1))
        {
            // XPG4 two-digit year (to_tm_year): 00-68 -> 20xx,
            // 69-99 -> 19xx; other widths are literal ("jan 2, 5"
            // is the year 5 — probed to the fs floor).
            let y = if digits == 2 {
                val + if val <= 68 { 2000 } else { 1900 }
            } else {
                val
            };
            year = Some(y);
            continue;
        }
        if !clock_seen {
            if digits <= 2 {
                hour = u32::try_from(val).ok()?;
                if hour > 24 {
                    return None;
                }
                clock_seen = true;
                continue;
            }
            if (3..=4).contains(&digits) {
                hour = (val / 100) as u32;
                minute = (val % 100) as u32;
                if hour > 24 || minute > 59 {
                    return None;
                }
                clock_seen = true;
                continue;
            }
            return None;
        }
        return None;
    }

    if let Some(pm) = meridian {
        if hour > 12 {
            return None;
        }
        hour = (hour % 12) + if pm { 12 } else { 0 };
    }
    if month.is_none() {
        // zone + clock form ("est 5" = today 05:00 EST, probed)
        if day.is_none() && clock_seen {
            let (y, m, d) = local_today()?;
            return finish_civil(y, m, d, hour, minute, second, nanos, zone_min);
        }
        return None;
    }
    let d = day?;
    let y = match year {
        Some(y) => y,
        None => local_today()?.0,
    };
    if d == 0 || d > days_in_month(y, month.unwrap()) || hour > 24 || minute > 59 || second > 59 {
        return None;
    }
    let mut ft = finish_civil(y, month.unwrap(), d, hour, minute, second, nanos, zone_min)?;
    // ordinal-word rel items ("jan 2 this year" = +0y — probed rc 0)
    for (ord, unit) in &rels {
        ft = apply_rel_unit(ft, unit, *ord)?;
    }
    Some(ft)
}

/// -t STAMP: [[CC]YY]MMDDhhmm[.ss]
fn parse_stamp(s: &str) -> Option<FileTime> {
    let (digits, sec) = match s.split_once('.') {
        Some((d, ss)) => {
            // exactly two digits, 0..=60 (GNU probed .99 rejected)
            if ss.len() != 2 || !ss.bytes().all(|b| b.is_ascii_digit()) {
                return None;
            }
            let v: u32 = ss.parse().ok()?;
            if v > 60 {
                return None;
            }
            (d, v)
        }
        None => (s, 0),
    };
    if !digits.bytes().all(|b| b.is_ascii_digit()) {
        return None;
    }
    let (year, rest) = match digits.len() {
        12 => {
            let y: i64 = digits[..4].parse().ok()?;
            (y, &digits[4..])
        }
        10 => {
            let yy: i64 = digits[..2].parse().ok()?;
            let y = if (69..=99).contains(&yy) {
                1900 + yy
            } else {
                2000 + yy
            };
            (y, &digits[2..])
        }
        8 => {
            // current year is not knowable portably for a bare MMDDhhmm —
            // GNU uses the current year
            let today = local_today()?;
            (today.0, digits)
        }
        _ => return None,
    };
    if rest.len() != 8 {
        return None;
    }
    let month: u32 = rest[0..2].parse().ok()?;
    let day: u32 = rest[2..4].parse().ok()?;
    let hour: u32 = rest[4..6].parse().ok()?;
    let minute: u32 = rest[6..8].parse().ok()?;
    // [GNU] posixtm.c:179-194 verifies the broken-down stamp the way
    // mktime would — 2020-02-30 and month 13 fail (probed rc 1), they do
    // not normalize.
    if !(1..=12).contains(&month)
        || !(1..=31).contains(&day)
        || day > days_in_month(year, month)
        || hour > 23
        || minute > 59
    {
        return None;
    }
    // [GNU] posixtm.c:201-207: a .60 seconds field is retried as 59 plus
    // one second — `touch -t 201612312359.60` lands on 2017-01-01
    // 00:00:00 (probed). .61 stays invalid.
    let leap = sec == 60;
    let mut ft = local_wall_to_filetime(
        year,
        month,
        day,
        hour,
        minute,
        if leap { 59 } else { sec },
        0,
    )?;
    if leap {
        ft = add_seconds_ft(ft, 1)?;
    }
    Some(ft)
}

#[allow(unused)]
const _: &str = OPTS_HELP;

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str]) -> (u32, String, String) {
        let sc = ScratchCtx::new("touch");
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = touch_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    fn mtime_epoch(sc: &ScratchCtx, name: &str) -> i64 {
        let m = fs_win::file_meta(&sc.ctx(), name, true).unwrap();
        m.mtime.to_epoch().0
    }

    /// Scripted `touch -` host: records each retime call.
    #[derive(Default)]
    struct RecordingStdout {
        calls: Vec<(Option<u64>, Option<u64>)>,
        result: Option<u32>,
    }

    impl StdoutTimes for RecordingStdout {
        fn set_times(
            &mut self,
            atime: Option<FileTime>,
            mtime: Option<FileTime>,
        ) -> Result<(), u32> {
            self.calls
                .push((atime.map(|t| t.as_u64()), mtime.map(|t| t.as_u64())));
            match self.result {
                Some(e) => Err(e),
                None => Ok(()),
            }
        }
    }

    #[test]
    fn dash_operand_retimes_injected_stdout() {
        // GNU touch.c:127-128: `-` targets fd 1. The library asks the
        // injected host face, never GetStdHandle (the #1487 red line).
        let sc = ScratchCtx::new("touch-dash");
        let mut host = RecordingStdout::default();
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = ["-d", "@1000000000", "-m", "-"]
            .iter()
            .map(Into::into)
            .collect();
        let r = touch_with_stdout(&owned, &mut io.split(), &ctx, Some(&mut host)).unwrap();
        assert_eq!(r, 0);
        assert!(io.out().is_empty());
        assert!(io.err().is_empty());
        assert_eq!(host.calls.len(), 1);
        let (a, m) = host.calls[0];
        // -m selects only the modification time; the access field is
        // untouched (None maps to SetFileTime's NULL).
        assert_eq!(a, None);
        let want = FileTime::from_epoch(1_000_000_000, 0).as_u64();
        assert_eq!(m, Some(want));
        // -a is the mirror image.
        let mut host = RecordingStdout::default();
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["-d", "@1000000000", "-a", "-"]
            .iter()
            .map(Into::into)
            .collect();
        let r = touch_with_stdout(&owned, &mut io.split(), &ctx, Some(&mut host)).unwrap();
        assert_eq!(r, 0);
        assert_eq!(host.calls[0].0, Some(want));
        assert_eq!(host.calls[0].1, None);
    }

    #[test]
    fn dash_operand_without_host_stdout_is_einval() {
        // The ABI capture buffer has no settable times: GNU on a pipe
        // reports "setting times of '-': Invalid argument", rc 1.
        let (r, out, err) = run(&["-"]);
        assert_eq!(r, 1);
        assert!(out.is_empty());
        assert!(
            err.contains("touch: setting times of '-': Invalid argument"),
            "err={err}"
        );
    }

    #[test]
    fn dash_host_failure_propagates_and_later_operands_run() {
        let sc = ScratchCtx::new("touch-dash-err");
        let mut host = RecordingStdout {
            calls: Vec::new(),
            result: Some(5), // ERROR_ACCESS_DENIED -> "Permission denied"
        };
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = ["-", "later"].iter().map(Into::into).collect();
        let r = touch_with_stdout(&owned, &mut io.split(), &ctx, Some(&mut host)).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("setting times of '-': Permission denied"));
        assert_eq!(host.calls.len(), 1);
        // touch processes every operand (rc is the worst outcome).
        assert!(sc.path("later").exists());
    }

    #[test]
    fn creates_and_updates() {
        let sc = ScratchCtx::new("touch-basic");
        {
            let mut io = MemIo::new("");
            let ctx = sc.ctx();
            let owned: Vec<OsString> = ["new1"].iter().map(Into::into).collect();
            assert_eq!(touch_cmd(&owned, &mut io.split(), &ctx).unwrap(), 0);
        }
        assert!(sc.path("new1").exists());
        let e1 = mtime_epoch(&sc, "new1");
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_secs() as i64;
        assert!((e1 - now).abs() < 5);
        // -c does not create
        let (r, _, _) = run(&["-c", "never1"]);
        assert_eq!(r, 0);
        assert!(!sc.path("never1").exists());
        // missing operand
        let (r, _, e) = run(&[]);
        assert_eq!(r, 1);
        assert!(e.contains("missing file operand"));
    }

    #[test]
    fn date_forms() {
        let sc = ScratchCtx::new("touch-date");
        let set = |args: &[&str]| {
            let mut io = MemIo::new("");
            let ctx = sc.ctx();
            let owned: Vec<OsString> = args.iter().map(Into::into).collect();
            let r = touch_cmd(&owned, &mut io.split(), &ctx).unwrap();
            (r, io.err())
        };
        let (r, e) = set(&["-d", "2004-02-29 16:21:42", "reg"]);
        assert_eq!((r, e.as_str()), (0, ""));
        // local +0800 epoch (probed GNU: 1078042902); the wall time itself
        // is zone independent: 16:21:42 local
        let m = fs_win::file_meta(&sc.ctx(), "reg", true).unwrap();
        let (secs, _) = m.mtime.to_epoch();
        let local = fs_win::to_local(m.mtime.as_u64() as i64).unwrap().0;
        assert_eq!(
            (
                local.hour,
                local.minute,
                local.second,
                local.year,
                local.month,
                local.day
            ),
            (16, 21, 42, 2004, 2, 29)
        );
        let _ = secs;
        // explicit offset +0100: 16:21:42+01 == 15:21:42 UTC
        let (r, _) = set(&["-d", "2004-02-29 16:21:42 +0100", "reg"]);
        assert_eq!(r, 0);
        let m = fs_win::file_meta(&sc.ctx(), "reg", true).unwrap();
        let (secs, _) = m.mtime.to_epoch();
        assert_eq!(secs, 1078068102);
        // @epoch
        let (r, _) = set(&["-d", "@1000000000", "reg"]);
        assert_eq!(r, 0);
        assert_eq!(mtime_epoch(&sc, "reg"), 1000000000);
        // T separator
        let (r, _) = set(&["-d", "2004-02-29T16:21:42", "reg"]);
        assert_eq!(r, 0);
        let local = fs_win::to_local(
            fs_win::file_meta(&sc.ctx(), "reg", true)
                .unwrap()
                .mtime
                .as_u64() as i64,
        )
        .unwrap()
        .0;
        assert_eq!((local.hour, local.minute), (16, 21));
        // date-only
        let (r, _) = set(&["-d", "2004-02-29", "reg"]);
        assert_eq!(r, 0);
        let local = fs_win::to_local(
            fs_win::file_meta(&sc.ctx(), "reg", true)
                .unwrap()
                .mtime
                .as_u64() as i64,
        )
        .unwrap()
        .0;
        assert_eq!((local.year, local.month, local.day), (2004, 2, 29));
        // errors
        let (r, e) = set(&["-d", "garbage-date", "reg"]);
        assert_eq!(r, 1);
        assert!(e.contains("invalid date format 'garbage-date'"));
        // GNU probed: empty -d is today at local midnight, rc 0
        let (r, e) = set(&["-d", "", "reg"]);
        assert_eq!((r, e.as_str()), (0, ""));
        let local = fs_win::to_local(
            fs_win::file_meta(&sc.ctx(), "reg", true)
                .unwrap()
                .mtime
                .as_u64() as i64,
        )
        .unwrap()
        .0;
        let today = local_today().unwrap();
        assert_eq!(
            (
                local.year as i64,
                local.month as u32,
                local.day as u32,
                local.hour,
                local.minute
            ),
            (today.0, today.1, today.2, 0, 0)
        );
    }

    #[test]
    fn stamp_forms() {
        let sc = ScratchCtx::new("touch-stamp");
        let set = |args: &[&str]| {
            let mut io = MemIo::new("");
            let ctx = sc.ctx();
            let owned: Vec<OsString> = args.iter().map(Into::into).collect();
            let r = touch_cmd(&owned, &mut io.split(), &ctx).unwrap();
            (r, io.err())
        };
        let (r, e) = set(&["-t", "200402291621", "reg"]);
        assert_eq!((r, e.as_str()), (0, ""));
        let m = fs_win::file_meta(&sc.ctx(), "reg", true).unwrap();
        let (secs, _) = m.mtime.to_epoch();
        // probed GNU local +0800 value; wall-clock check is zone independent
        assert_eq!(secs % 60, 0);
        let local = fs_win::to_local(m.mtime.as_u64() as i64).unwrap().0;
        assert_eq!((local.hour, local.minute), (16, 21));
        let (r, _) = set(&["-t", "200402291621.30", "reg"]);
        assert_eq!(r, 0);
        let local = fs_win::to_local(
            fs_win::file_meta(&sc.ctx(), "reg", true)
                .unwrap()
                .mtime
                .as_u64() as i64,
        )
        .unwrap()
        .0;
        assert_eq!(local.second, 30);
        // errors: 6 digits, .99, 3-digit fraction
        let (r, e) = set(&["-t", "021621", "reg"]);
        assert_eq!(r, 1);
        assert!(e.contains("invalid date format '021621'"));
        let (r, e) = set(&["-t", "200402291621.99", "reg"]);
        assert_eq!(r, 1);
        assert!(e.contains("invalid date format"));
        let (r, _) = set(&["-t", "200402291621.100", "reg"]);
        assert_eq!(r, 1);
        // 1999 is not a leap year: Feb 29 is rejected like GNU (probed)
        let (r, e) = set(&["-t", "9902291621", "reg"]);
        assert_eq!(r, 1);
        assert!(e.contains("invalid date format '9902291621'"));
        // 10-digit short year: 69-99 -> 19xx
        let (r, _) = set(&["-t", "9904291621", "reg"]);
        assert_eq!(r, 0);
        let local = fs_win::to_local(
            fs_win::file_meta(&sc.ctx(), "reg", true)
                .unwrap()
                .mtime
                .as_u64() as i64,
        )
        .unwrap()
        .0;
        assert_eq!(local.year, 1999);
        // 12-digit is CCYY literal (GNU: 9904 stays 9904)
        let (r, _) = set(&["-t", "990402291621", "reg"]);
        assert_eq!(r, 0);
        let local = fs_win::to_local(
            fs_win::file_meta(&sc.ctx(), "reg", true)
                .unwrap()
                .mtime
                .as_u64() as i64,
        )
        .unwrap()
        .0;
        assert_eq!(local.year, 9904);
    }

    #[test]
    fn reference_and_select() {
        let sc = ScratchCtx::new("touch-ref");
        std::fs::write(sc.path("ref"), "x").unwrap();
        {
            let mut io = MemIo::new("");
            let ctx = sc.ctx();
            let owned: Vec<OsString> = ["-d", "@500000000", "ref"].iter().map(Into::into).collect();
            assert_eq!(touch_cmd(&owned, &mut io.split(), &ctx).unwrap(), 0);
        }
        {
            let mut io = MemIo::new("");
            let ctx = sc.ctx();
            let owned: Vec<OsString> = ["-r", "ref", "out1"].iter().map(Into::into).collect();
            assert_eq!(touch_cmd(&owned, &mut io.split(), &ctx).unwrap(), 0);
        }
        assert_eq!(mtime_epoch(&sc, "out1"), 500000000);
        // -r missing reference
        let (r, _, e) = run(&["-r", "missing3", "out1"]);
        assert_eq!(r, 1);
        assert!(e.contains("failed to get attributes of 'missing3'"));
        // --time=bogus
        let (r, _, e) = run(&["--time=bogus", "x"]);
        assert_eq!(r, 1);
        assert!(e.contains("invalid argument 'bogus' for '--time'"));
        assert!(e.contains("Valid arguments are:"));
        // unrecognized long option
        let (r, _, e) = run(&["--refile=reg"]);
        assert_eq!(r, 1);
        assert!(e.contains("unrecognized option '--refile=reg'"));
        assert_eq!(run(&["--help"]).0, 0);
        assert_eq!(run(&["--version"]).0, 0);
    }

    #[test]
    fn multi_operands_continue_on_error() {
        let sc = ScratchCtx::new("touch-multi");
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = ["nodir/f", "ok1"].iter().map(Into::into).collect();
        let r = touch_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(sc.path("ok1").exists());
        assert!(io.err().contains("cannot touch 'nodir/f'"));
    }

    #[test]
    fn gnu_grammar_extensions() {
        // compact YYYYMMDD[HHMM[SS]] (probed rc 0); loose fields; slash date
        assert_eq!(run(&["-d", "20250102030405", "compact"]).0, 0);
        assert_eq!(run(&["-d", "2026-6-7 8:30", "loose"]).0, 0);
        assert_eq!(run(&["-d", "2026/05/06 07:08", "slash"]).0, 0);
        assert_eq!(run(&["-d", "T1430", "basic"]).0, 0);
        // relative items: fortnight (issue #1382), ago, plus-sign
        assert_eq!(run(&["-d", "1 fortnight", "fort1"]).0, 0);
        assert_eq!(run(&["-d", "2 fortnights", "fort2"]).0, 0);
        assert_eq!(run(&["-d", "3 days ago", "ago3"]).0, 0);
        assert_eq!(run(&["-d", "+2 hours", "plus2"]).0, 0);
        // weekday items
        assert_eq!(run(&["-d", "next monday", "nmon"]).0, 0);
        assert_eq!(run(&["-d", "last friday", "lfri"]).0, 0);
        assert_eq!(run(&["-d", "monday", "bmon"]).0, 0);
        // combined: fixed base + relative item. [GNU] month arithmetic is
        // calendar-position arithmetic with forward day roll (1.x, probed
        // coreutils 9.4): Mar 31 minus one month = Mar 3, NOT Feb 28.
        let sc = ScratchCtx::new("touch-combined");
        {
            let mut io = MemIo::new("");
            let ctx = sc.ctx();
            let owned: Vec<OsString> = ["-d", "2026-03-31 1 month ago", "clamped"]
                .iter()
                .map(Into::into)
                .collect();
            let r = touch_cmd(&owned, &mut io.split(), &ctx).unwrap();
            assert_eq!(r, 0);
        }
        let local = fs_win::to_local(
            fs_win::file_meta(&sc.ctx(), "clamped", true)
                .unwrap()
                .mtime
                .as_u64() as i64,
        )
        .unwrap()
        .0;
        assert_eq!(
            (local.year, local.month, local.day),
            (2026, 3, 3),
            "Mar 31 minus one month rolls the overflow days forward (GNU)"
        );
    }

    #[test]
    fn reference_anchors_relative_date() {
        // [GNU] touch.c:354-360 (probed): -r + -d evaluates the string
        // against the reference atime/mtime per field, so "+1 day" lands a
        // day past each reference stamp.
        let sc = ScratchCtx::new("touch-refanchor");
        {
            let mut io = MemIo::new("");
            let ctx = sc.ctx();
            let owned: Vec<OsString> = ["-d", "@1500000000", "ref"]
                .iter()
                .map(Into::into)
                .collect();
            assert_eq!(touch_cmd(&owned, &mut io.split(), &ctx).unwrap(), 0);
        }
        {
            let mut io = MemIo::new("");
            let ctx = sc.ctx();
            let owned: Vec<OsString> = ["-r", "ref", "-d", "+1 day", "target"]
                .iter()
                .map(Into::into)
                .collect();
            assert_eq!(touch_cmd(&owned, &mut io.split(), &ctx).unwrap(), 0);
        }
        assert_eq!(mtime_epoch(&sc, "target"), 1500000000 + 86_400);
    }

    #[test]
    fn trailing_separator_and_glob() {
        let sc = ScratchCtx::new("touch-sep-glob");
        std::fs::write(sc.path("file.txt"), "payload").unwrap();
        std::fs::create_dir(sc.path("sub")).unwrap();
        std::fs::write(sc.path("aa1.txt"), "1").unwrap();
        std::fs::write(sc.path("aa2.txt"), "2").unwrap();
        let set = |args: &[&str]| {
            let mut io = MemIo::new("");
            let ctx = sc.ctx();
            let owned: Vec<OsString> = args.iter().map(Into::into).collect();
            let r = touch_cmd(&owned, &mut io.split(), &ctx).unwrap();
            (r, io.err())
        };
        // existing regular file + trailing separator: GNU probed wording
        let (r, e) = set(&["file.txt/"]);
        assert_eq!(r, 1);
        assert!(e.contains("setting times of 'file.txt/': Not a directory"));
        // missing path + separator: same wording, ENOENT reason
        let (r, e) = set(&["nope/"]);
        assert_eq!(r, 1);
        assert!(e.contains("setting times of 'nope/': No such file or directory"));
        // directory + separator succeeds
        let (r, e) = set(&["sub/"]);
        assert_eq!((r, e.as_str()), (0, ""));
        // glob expansion (direct-invocation contract): both matches touched
        let (r, e) = set(&["aa*.txt"]);
        assert_eq!((r, e.as_str()), (0, ""));
        assert!(sc.path("aa1.txt").exists());
        assert!(sc.path("aa2.txt").exists());
        // unmatched pattern stays literal (GNU glob default). On Windows a
        // literal '*'-name cannot be created (ERROR_INVALID_NAME), the same
        // rc 1 the 1.x build reports for `touch 'zz*.txt'`; -c stays silent.
        let (r, _) = set(&["-c", "zz*.txt"]);
        assert_eq!(r, 0);
        assert!(!sc.path("zz*.txt").exists());
        let (r, e) = set(&["zz*.txt"]);
        assert_eq!(r, 1);
        assert!(e.contains("cannot touch 'zz*.txt'"));
    }

    #[test]
    fn both_time_flags_update_both() {
        // #1704 / GNU touch.c:284-336: -a and -m accumulate into the
        // change_times mask; both set = both fields written (probed
        // `touch -a -m -d @0` -> atime AND mtime 0).
        let sc = ScratchCtx::new("touch-am");
        let mut host = RecordingStdout::default();
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        for argv in [
            vec!["-d", "@1000000000", "-a", "-m", "-"],
            vec!["-d", "@1000000000", "-m", "-a", "-"],
            vec!["-d", "@1000000000", "-am", "-"],
            vec!["-d", "@1000000000", "--time=atime", "--time=mtime", "-"],
        ] {
            let owned: Vec<OsString> = argv.iter().map(Into::into).collect();
            let r = touch_with_stdout(&owned, &mut io.split(), &ctx, Some(&mut host)).unwrap();
            assert_eq!(r, 0, "argv={argv:?}");
        }
        let want = FileTime::from_epoch(1_000_000_000, 0).as_u64();
        assert_eq!(host.calls.len(), 4);
        for (a, m) in &host.calls {
            assert_eq!((*a, *m), (Some(want), Some(want)));
        }
    }

    #[test]
    fn empty_operand_is_cannot_touch() {
        // #1706 / GNU touch.c:127-129,196 (probed `touch ''` ->
        // "cannot touch '': No such file or directory", rc 1): an
        // explicit empty operand is a FILE argument, not "no operand".
        let (r, out, err) = run(&[""]);
        assert_eq!(r, 1);
        assert!(out.is_empty());
        assert_eq!(err, "touch: cannot touch '': No such file or directory\n");
        // ...and it does not retime the cwd or swallow other operands.
        let sc = ScratchCtx::new("touch-empty-mix");
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = ["", "later"].iter().map(Into::into).collect();
        let r = touch_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(sc.path("later").exists());
        // still zero operands -> missing file operand, not cannot-touch
        let (r, _, e) = run(&[]);
        assert_eq!(r, 1);
        assert!(e.contains("missing file operand"));
        // ...and an empty OPTION value stays legal (-d '' = today 00:00)
        let (r, e) = {
            let mut io = MemIo::new("");
            let ctx = sc.ctx();
            let owned: Vec<OsString> = ["-d", "", "reg"].iter().map(Into::into).collect();
            (touch_cmd(&owned, &mut io.split(), &ctx).unwrap(), io.err())
        };
        assert_eq!((r, e.as_str()), (0, ""));
    }

    #[test]
    fn gnu_grammar_residuals_1705() {
        let sc = ScratchCtx::new("touch-1705");
        let set = |args: &[&str]| {
            let mut io = MemIo::new("");
            let ctx = sc.ctx();
            let owned: Vec<OsString> = args.iter().map(Into::into).collect();
            let r = touch_cmd(&owned, &mut io.split(), &ctx).unwrap();
            (r, io.err())
        };
        let delta = |name: &str| -> i64 {
            let m = fs_win::file_meta(&sc.ctx(), name, true).unwrap();
            let (secs, _) = m.mtime.to_epoch();
            let now = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_secs() as i64;
            secs - now
        };
        // bare unit = amount 1 (probed `touch -d fortnight` rc 0 = +14d)
        assert_eq!(set(&["-d", "fortnight", "f1"]).0, 0);
        assert!((delta("f1") - 14 * 86_400).abs() < 30);
        assert_eq!(set(&["-d", "day ago", "f2"]).0, 0);
        assert!((delta("f2") + 86_400).abs() < 30);
        assert_eq!(set(&["-d", "hours", "f3"]).0, 0);
        assert!((delta("f3") - 3600).abs() < 30);
        // 'a fortnight': 'a' is military zone A = UTC+1 -> +14d -3600s
        // (probed GNU quirk, parse-datetime.y military_table)
        assert_eq!(set(&["-d", "a fortnight", "f4"]).0, 0);
        assert!((delta("f4") - (14 * 86_400 - 3600)).abs() < 30);
        // 'x fortnight': X = UTC-11 -> +14d +39600s
        assert_eq!(set(&["-d", "x fortnight", "f5"]).0, 0);
        assert!((delta("f5") - (14 * 86_400 + 39600)).abs() < 30);
        // items accumulate; 'second day' = +1s +1d (SECOND is the unit,
        // not the ordinal — commented out in relative_time_table)
        assert_eq!(set(&["-d", "day day", "f6"]).0, 0);
        assert!((delta("f6") - 2 * 86_400).abs() < 30);
        assert_eq!(set(&["-d", "second day", "f7"]).0, 0);
        assert!((delta("f7") - 86_401).abs() < 30);
        // 'hence' is the forward 'ago'
        assert_eq!(set(&["-d", "1 fortnight hence", "f8"]).0, 0);
        assert!((delta("f8") - 14 * 86_400).abs() < 30);
        // neighbors GNU rejects (probed): an/the/one, bare ago/hence
        for bad in [
            "an fortnight",
            "the fortnight",
            "one fortnight",
            "ago",
            "ago fortnight",
            "hence fortnight",
            "pm fortnight",
        ] {
            let (r, e) = set(&["-d", bad, "bad"]);
            assert_eq!(r, 1, "spec={bad:?}");
            assert!(e.contains("invalid date format"), "spec={bad:?} err={e}");
        }
    }

    #[test]
    fn gnu_named_zones_1705() {
        // named zone table (parse-datetime.y :1133-1191), word boundary
        let sc = ScratchCtx::new("touch-zone");
        let set = |args: &[&str]| {
            let mut io = MemIo::new("");
            let ctx = sc.ctx();
            let owned: Vec<OsString> = args.iter().map(Into::into).collect();
            let r = touch_cmd(&owned, &mut io.split(), &ctx).unwrap();
            (r, io.err())
        };
        // trailing named zone: 03:04:05 EST = 08:04:05 UTC (EST = -300)
        let (r, _) = set(&["-d", "2020-01-02 03:04:05 EST", "z1"]);
        assert_eq!(r, 0);
        assert_eq!(mtime_epoch(&sc, "z1"), 1577934245 + 5 * 3600);
        // military letter trailing a clock ("a" = UTC+1)
        let (r, _) = set(&["-d", "2020-01-02 03:04:05 a", "z2"]);
        assert_eq!(r, 0);
        assert_eq!(mtime_epoch(&sc, "z2"), 1577934245 - 3600);
        // a bare named zone = today 00:00 in that zone (probed `touch -d
        // EST` rc 0); XXX/EST5EDT are not zones (probed invalid)
        assert_eq!(set(&["-d", "EST", "z3"]).0, 0);
        for bad in ["XXX", "EST5EDT"] {
            let (r, e) = set(&["-d", bad, "z4"]);
            assert_eq!(r, 1, "spec={bad:?}");
            assert!(e.contains("invalid date format"), "spec={bad:?} err={e}");
        }
        // GNU rejects a :60 seconds field in -d strings (probed); the -t
        // stamp path is the one that accepts the leap second.
        for bad in ["2020-01-02 03:04:60", "2020-01-02 23:59:60"] {
            assert_eq!(set(&["-d", bad, "z5"]).0, 1, "spec={bad:?}");
        }
    }

    #[test]
    fn gnu_stamp_leap_and_year0_1705() {
        // posixtm.c:201-207 leap-second normalization and mktime's
        // year-0 acceptance; the filesystem floor pins out-of-range
        // results (GNU's ext4 floor INT32_MIN, NTFS's earliest writable
        // FILETIME — FILETIME 0 is SetFileTime's no-change sentinel).
        let sc = ScratchCtx::new("touch-leap");
        let set = |args: &[&str]| {
            let mut io = MemIo::new("");
            let ctx = sc.ctx();
            let owned: Vec<OsString> = args.iter().map(Into::into).collect();
            let r = touch_cmd(&owned, &mut io.split(), &ctx).unwrap();
            (r, io.err())
        };
        // .60 rolls into the next minute (probed -t 201612312359.60 ->
        // 2017-01-01 00:00:00)
        let (r, _) = set(&["-t", "201612312359.60", "lp1"]);
        assert_eq!(r, 0);
        let m = fs_win::file_meta(&sc.ctx(), "lp1", true).unwrap();
        let local = fs_win::to_local(m.mtime.as_u64() as i64).unwrap().0;
        assert_eq!(
            (
                local.year,
                local.month,
                local.day,
                local.hour,
                local.minute,
                local.second
            ),
            (2017, 1, 1, 0, 0, 0)
        );
        // .61 stays invalid (probed)
        let (r, e) = set(&["-t", "202001010000.61", "lp2"]);
        assert_eq!(r, 1);
        assert!(e.contains("invalid date format"));
        // year 0000 (and the pre-1601 range generally) parses and clamps
        // to the earliest writable stamp (probed rc 0); invalid days are
        // still rejected (mktime verify, posixtm.c:179-194).
        assert_eq!(set(&["-t", "000001010000", "lp3"]).0, 0);
        let m = fs_win::file_meta(&sc.ctx(), "lp3", true).unwrap();
        let (secs, _) = m.mtime.to_epoch();
        assert_eq!(secs, -11644473600); // FILETIME 1 = the writable floor
        let (r, _) = set(&["-t", "202002302359", "lp4"]);
        assert_eq!(r, 1);
        let (r, _) = set(&["-t", "202013010000", "lp5"]);
        assert_eq!(r, 1);
    }

    #[test]
    fn gnu_wordy_dates_1705() {
        let sc = ScratchCtx::new("touch-wordy");
        let set = |args: &[&str]| {
            let mut io = MemIo::new("");
            let ctx = sc.ctx();
            let owned: Vec<OsString> = args.iter().map(Into::into).collect();
            let r = touch_cmd(&owned, &mut io.split(), &ctx).unwrap();
            (r, io.err())
        };
        let local_of = |name: &str| {
            fs_win::to_local(
                fs_win::file_meta(&sc.ctx(), name, true)
                    .unwrap()
                    .mtime
                    .as_u64() as i64,
            )
            .unwrap()
            .0
        };
        // the audit's headline form
        let (r, _) = set(&["-d", "Thu Jan 2 03:04:05 UTC 2020", "w1"]);
        assert_eq!(r, 0);
        assert_eq!(mtime_epoch(&sc, "w1"), 1577934245);
        // month-name shapes (all probed rc 0)
        for (spec, name) in [
            ("Jan 2 2020", "w2"),
            ("January 2, 2020", "w3"),
            ("jan 2, 69", "w4"),
            ("2 jan 70", "w5"),
            ("17-JUN-1992", "w6"),
            ("JUN-17-1992", "w7"),
            ("jan 2 2020 3pm", "w8"),
            ("mon jan 2 2020", "w9"),
        ] {
            assert_eq!(set(&["-d", spec, name]).0, 0, "spec={spec:?}");
        }
        assert_eq!(
            {
                let l = local_of("w4");
                (l.year, l.month, l.day)
            },
            (1969, 1, 2),
            "jan 2, 69 -> 1969 (comma binds the year, to_tm_year 69->19xx)"
        );
        assert_eq!(
            {
                let l = local_of("w5");
                (l.year, l.month, l.day)
            },
            (1970, 1, 2),
            "2 jan 70 -> 1970 (tUNUMBER tMONTH tUNUMBER)"
        );
        assert_eq!(
            {
                let l = local_of("w6");
                (l.year, l.month, l.day)
            },
            (1992, 6, 17)
        );
        assert_eq!((local_of("w8").hour, local_of("w8").minute), (15, 0));
        // 'jan 2 20' reads the 2-digit number as a clock hour (probed)
        let (r, _) = set(&["-d", "jan 2 20", "w10"]);
        assert_eq!(r, 0);
        assert_eq!(local_of("w10").hour, 20);
        // a weekday next to a definite date is ignored (probed "Sat
        // 2020-01-02" -> the ISO date unchanged)
        let (r, _) = set(&["-d", "Sat 2020-01-02", "w11"]);
        assert_eq!(r, 0);
        assert_eq!(
            {
                let l = local_of("w11");
                (l.year, l.month, l.day)
            },
            (2020, 1, 2)
        );
        // 'est 5' = today 05:00 EST (zone + bare number, probed)
        assert_eq!(set(&["-d", "est 5", "w12"]).0, 0);
        // neighbors GNU rejects (probed): no-comma 2-digit year slot,
        // month+number alone, comma before a non-number, '2nd', day>dim,
        // unabbreviated 2-letter 'se'.
        for bad in [
            "jan 2 69",
            "jan 2020",
            "dec 25, 03:04:05 2020",
            "2nd jan 2020",
            "feb 30 2020",
            "se 1 2020",
        ] {
            let (r, e) = set(&["-d", bad, "wbad"]);
            assert_eq!(r, 1, "spec={bad:?}");
            assert!(e.contains("invalid date format"), "spec={bad:?} err={e}");
        }
        // year-0 wordy/ISO forms clamp to the writable floor like the
        // -t stamp path (probed `-d 0000-01-02` rc 0 on GNU)
        assert_eq!(set(&["-d", "0000-01-02", "w13"]).0, 0);
        assert_eq!(set(&["-d", "jan 2, 5", "w14"]).0, 0);
    }
}
