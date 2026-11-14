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
//! does NOT name a literal file; a wildcard operand (`*?[`) expands over
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
                "access" | "atime" => {
                    opts.change_atime = true;
                    opts.change_mtime = false;
                }
                "mtime" | "modify" => {
                    opts.change_mtime = true;
                    opts.change_atime = false;
                }
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
                        "access" | "atime" | "use" => {
                            opts.change_atime = true;
                            opts.change_mtime = false;
                            opts.time_word = Some(v);
                        }
                        "modify" | "mtime" => {
                            opts.change_mtime = true;
                            opts.change_atime = false;
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
                'a' => {
                    opts.change_atime = true;
                    opts.change_mtime = false;
                }
                'm' => {
                    opts.change_mtime = true;
                    opts.change_atime = false;
                }
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
        let atime = match (opts.change_atime, opts.change_mtime) {
            (true, _) => want_atime,
            (false, true) => None,
            (_, _) => want_atime,
        };
        let mtime = match (opts.change_atime, opts.change_mtime) {
            (true, _) => None,
            (false, true) => want_mtime,
            (_, _) => want_mtime,
        };
        // [GNU] touch.c:127-128: the '-' operand retimes standard output
        // (POSIX) instead of naming a literal file. An untimable stdout
        // (pipe/console) reports the same setting-times failure GNU does.
        if op == "-" {
            if let Err(e) = fs_win::set_stdout_file_times(atime, mtime) {
                let _ = writeln!(
                    io.stderr,
                    "touch: setting times of '-': {}",
                    fs_win::win32_reason(e)
                );
                ok = false;
            }
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
fn parse_datetime(s: &str, base: FileTime) -> Option<FileTime> {
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
    parse_combined(s, base, 0)
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
        let st = SystemTime {
            year: today.0 as u16,
            month: today.1 as u16,
            day_of_week: 0,
            day: today.2 as u16,
            hour: h as u16,
            minute: mi as u16,
            second: ss as u16,
            milliseconds: 0,
        };
        return finish_fixed(&st, i32::MIN);
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
                if m == 0 || m > 12 || d == 0 || d > 31 || h > 23 || mi > 59 || s2 > 59 {
                    return None;
                }
                let st = SystemTime {
                    year: y as u16,
                    month: m as u16,
                    day_of_week: 0,
                    day: d as u16,
                    hour: h as u16,
                    minute: mi as u16,
                    second: s2 as u16,
                    milliseconds: 0,
                };
                return finish_fixed(&st, zone_min);
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
    let st = SystemTime {
        year: year as u16,
        month: month as u16,
        day_of_week: 0,
        day: day as u16,
        hour: hour as u16,
        minute: minute as u16,
        second: second as u16,
        milliseconds: (nanos / 1_000_000) as u16,
    };
    finish_fixed(&st, zone_min)
}

/// Zone application for a broken-down wall time (local when zone_min is
/// i32::MIN, else UTC shifted by the offset).
fn finish_fixed(st: &SystemTime, zone_min: i32) -> Option<FileTime> {
    if zone_min == i32::MIN {
        fs_win::from_local(st)
    } else {
        // interpret st as UTC wall clock and shift by the given offset
        // (local = utc+min; so utc = local - min)
        let _ = fs_win::from_local(st)?;
        let utc_ft = utc_wall_to_filetime(st)?;
        let adjusted = utc_ft.as_u64() as i64 - (zone_min as i64) * 600_000_000i64;
        Some(FileTime::from_u64(adjusted as u64))
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
    if !(1..=12).contains(&m) || !(1..=31).contains(&d) {
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
    if h > 23 || m > 59 || sec > 60 {
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
    let (local, _) = fs_win::to_local(base.as_u64() as i64)?;
    let base_dow = civil_dow(local.year as i64, local.month as u32, local.day as u32);
    let delta = (target + 7 - base_dow) % 7;
    let offset: i64 = match mode {
        Mode::Bare => delta as i64,
        Mode::Next => {
            if delta == 0 {
                7
            } else {
                delta as i64
            }
        }
        Mode::Last => {
            if delta == 0 {
                -7
            } else {
                delta as i64 - 7
            }
        }
    };
    let shifted = FileTime::from_u64((base.as_u64() as i64 + offset * 86_400_000_000i64) as u64);
    midnight_of(shifted)
}

/// [GNU] Relative items: now/today/yesterday/tomorrow; "<amount> <unit>[
/// ago]"; amount may carry a sign and next/last stand for +/-1. Units:
/// seconds/minutes/hours/days/weeks/fortnights plus month(s)/year(s) as
/// calendar shifts with month-end clamping (all probed coreutils 9.4 rc
/// 0: "3 days ago", "+2 hours", "1 fortnight", "2 fortnights").
fn parse_relative(s: &str, base: FileTime) -> Option<FileTime> {
    let t = s.trim().to_ascii_lowercase();
    match t.as_str() {
        "now" => return Some(base),
        "today" => return midnight_of(base),
        "yesterday" => {
            return Some(FileTime::from_u64(
                (base.as_u64() as i64 - 86_400_000_000i64) as u64,
            ))
        }
        "tomorrow" => {
            return Some(FileTime::from_u64(
                (base.as_u64() as i64 + 86_400_000_000i64) as u64,
            ))
        }
        _ => {}
    }
    let (body, ago) = match t.strip_suffix(" ago") {
        Some(b) => (b.trim_end(), true),
        None => (t.as_str(), false),
    };
    let mut tokens = body.split_whitespace();
    let amount_word = tokens.next()?;
    let unit = tokens.next()?;
    if tokens.next().is_some() {
        return None;
    }
    let mut amount: i64 = match amount_word {
        "next" => 1,
        "last" => -1,
        w => {
            if w.is_empty()
                || !w
                    .bytes()
                    .all(|b| b.is_ascii_digit() || b == b'+' || b == b'-')
            {
                return None;
            }
            w.parse::<i64>().ok()?
        }
    };
    if ago {
        amount = -amount;
    }
    // [GNU] month/year are calendar units, not fixed second counts:
    // "3 months ago" rolls the wall-clock month back three positions.
    match unit {
        "month" | "months" => shift_months(base, amount),
        "year" | "years" => shift_months(base, amount.saturating_mul(12)),
        "second" | "seconds" | "sec" | "secs" => add_seconds_ft(base, amount),
        "minute" | "minutes" | "min" | "mins" => add_seconds_ft(base, amount.saturating_mul(60)),
        "hour" | "hours" => add_seconds_ft(base, amount.saturating_mul(3600)),
        "day" | "days" => add_seconds_ft(base, amount.saturating_mul(86_400)),
        "week" | "weeks" => add_seconds_ft(base, amount.saturating_mul(7 * 86_400)),
        // [GNU] a fortnight is fourteen days (issue #1382)
        "fortnight" | "fortnights" => add_seconds_ft(base, amount.saturating_mul(14 * 86_400)),
        _ => None,
    }
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
            let is_amount = amount_word == "next"
                || amount_word == "last"
                || (!amount_word.is_empty()
                    && amount_word
                        .bytes()
                        .all(|b| b.is_ascii_digit() || b == b'+' || b == b'-'));
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
    if !(1..=12).contains(&month) || !(1..=31).contains(&day) || hour > 23 || minute > 59 {
        return None;
    }
    let st = SystemTime {
        year: year as u16,
        month: month as u16,
        day_of_week: 0,
        day: day as u16,
        hour: hour as u16,
        minute: minute as u16,
        second: sec as u16,
        milliseconds: 0,
    };
    fs_win::from_local(&st)
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
}
