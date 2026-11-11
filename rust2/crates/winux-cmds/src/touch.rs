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
//!   9.4: ISO 8601 "YYYY-MM-DD[ T]HH[:MM[:SS[.frac]]]", date-only, time-only
//!   (today), "@SECONDS" epoch, optional trailing timezone
//!   (+HHMM / +HH:MM / Z / UTC / GMT). Relative forms ("next monday",
//!   "1 day ago") are NOT part of the subset and report GNU's
//!   `invalid date format 'X'` (documented divergence: no "Try" line, as
//!   the 1.x C++ build also printed).
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
//! `-` is a plain file name (POSIX, like GNU touch).

use std::ffi::OsString;

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
                        let _ = usage_error(io, "touch", "option '--date' requires an argument");
                        return Ok(1);
                    }
                },
                "reference" => match take_val(inline, args, &mut idx) {
                    Some(v) => opts.reference = Some(v),
                    None => {
                        let _ =
                            usage_error(io, "touch", "option '--reference' requires an argument");
                        return Ok(1);
                    }
                },
                "time" => {
                    let v = match take_val(inline, args, &mut idx) {
                        Some(v) => v,
                        None => {
                            let _ =
                                usage_error(io, "touch", "option '--time' requires an argument");
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
                                "touch: invalid argument '{v}' for '--time'\nValid arguments are:\n  - 'atime', 'access', 'use'\n  - 'mtime', 'modify'\nTry 'touch --help' for more information."
                            );
                            return Ok(1);
                        }
                    }
                }
                "help" | "version" => return Ok(0),
                _ => {
                    let _ = usage_error(io, "touch", &format!("unrecognized option '{arg}'"));
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
                                let _ =
                                    usage_error(io, "touch", "option requires an argument -- 'd'");
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
                                let _ =
                                    usage_error(io, "touch", "option requires an argument -- 't'");
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
                                let _ =
                                    usage_error(io, "touch", "option requires an argument -- 'r'");
                                return Ok(1);
                            }
                        }
                    };
                    opts.reference = Some(v);
                }
                c => {
                    let _ = usage_error(io, "touch", &format!("invalid option -- '{c}'"));
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
    let mut new_time: Option<FileTime> = None;
    // [GNU] -t combined with any other time source is rejected before the
    // reference stat (probed: `-r missing -t ok` reports "more than one
    // source", not the stat failure). -d + -r is legal; the -d time wins.
    if opts.stamp.is_some() && (opts.date.is_some() || opts.reference.is_some()) {
        let _ = writeln!(
            io.stderr,
            "touch: cannot specify times from more than one source\nTry 'touch --help' for more information."
        );
        return Ok(1);
    }
    // Reference stat (also validates -r): GNU stats the reference before
    // the -d parse and before the missing-operand check (probed:
    // `-r missing -d garbage` and `-r missing` with no operands both
    // report the stat failure).
    if let Some(r) = &opts.reference {
        match fs_win::file_meta(&ctx.cwd, r, true) {
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
        match parse_datetime(d) {
            Some(ft) => new_time = Some(ft),
            None => {
                let _ = writeln!(io.stderr, "touch: invalid date format '{d}'");
                return Ok(1);
            }
        }
    }
    if opts.stamp.is_some() {
        new_time = stamp_time;
    }
    // [GNU] the missing-operand check follows time-source resolution
    // (probed: `-d garbage` with no operands reports the date;
    // `-d ok` with no operands reports the operand).
    if operands.is_empty() {
        let _ = usage_error(io, "touch", "missing file operand");
        return Ok(1);
    }
    let mut ok = true;
    for op in &operands {
        // GNU: an explicit -d/-t time wins over -r (probed: -d + -r succeeds
        // with the -d time applied); -r alone copies BOTH times from the
        // reference; with neither, both get the current time. -a/-m then
        // select which of the two actually gets written.
        let (want_atime, want_mtime) = if let Some(t) = new_time {
            (Some(t), Some(t))
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
        // missing file: create unless -c
        let missing = fs_win::file_meta(&ctx.cwd, op, true).is_err();
        if missing && !opts.no_create {
            if let Err(e) = fs_win::create_file(&ctx.cwd, op) {
                let _ = writeln!(
                    io.stderr,
                    "touch: cannot touch '{op}': {}",
                    fs_win::win32_reason(e)
                );
                ok = false;
                continue;
            }
        }
        let is_dir = fs_win::file_meta(&ctx.cwd, op, true)
            .map(|m| m.is_dir)
            .unwrap_or(false);
        match fs_win::set_file_times(
            &ctx.cwd,
            op,
            None,
            atime,
            mtime,
            is_dir,
            opts.no_dereference,
        ) {
            Ok(()) => {}
            Err(e) => {
                if missing && opts.no_create {
                    // GNU -c on a missing file: silent success
                    continue;
                }
                let _ = writeln!(
                    io.stderr,
                    "touch: setting times of '{op}': {}",
                    fs_win::win32_reason(e)
                );
                ok = false;
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

/// parse_datetime(): the GNU parse-datetime subset (ISO 8601 / @epoch /
/// time-only / date-only / trailing zone). Returns a UTC FILETIME.
fn parse_datetime(s: &str) -> Option<FileTime> {
    let s = s.trim();
    // GNU probed: an empty date is today at local midnight (rc 0)
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
    // split date/time first, THEN a trailing timezone from the time part
    // (a whole-string zone scan would eat "-DD" out of a date)
    let (date_part, time_part0) = split_datetime(s)?;
    let (time_part, zone_min) = split_zone(time_part0)?;
    let _ = time_part0;
    // date_part: "" (time-only), "YYYY-MM-DD" or "YYYYMMDD"?
    // GNU accepts YYYY-MM-DD; time HH:MM[:SS[.frac]].
    let today = local_today()?;
    let (year, month, day) = if date_part.is_empty() {
        (today.0, today.1, today.2)
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
    if zone_min == i32::MIN {
        // local time
        fs_win::from_local(&st)
    } else {
        // interpret as UTC then shift by the given offset (local = utc+min;
        // so utc = local - min): build a wall time and subtract the offset
        // by converting through the epoch.
        let _ = fs_win::from_local(&st)?;
        // correct: treat st as UTC wall clock instead
        let utc_ft = utc_wall_to_filetime(&st)?;

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

/// Split "DATE TIME" / "TDATE" / date-only / time-only.
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
    // date-only (has '-') or time-only (has ':')
    if body.contains('-') {
        Some((body, ""))
    } else {
        Some(("", body))
    }
}

fn parse_ymd(s: &str) -> Option<(i64, u32, u32)> {
    let parts: Vec<&str> = s.split('-').collect();
    if parts.len() != 3 {
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
        let m = fs_win::file_meta(&sc.dir, name, true).unwrap();
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
        let m = fs_win::file_meta(&sc.dir, "reg", true).unwrap();
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
        let m = fs_win::file_meta(&sc.dir, "reg", true).unwrap();
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
            fs_win::file_meta(&sc.dir, "reg", true)
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
            fs_win::file_meta(&sc.dir, "reg", true)
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
            fs_win::file_meta(&sc.dir, "reg", true)
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
        let m = fs_win::file_meta(&sc.dir, "reg", true).unwrap();
        let (secs, _) = m.mtime.to_epoch();
        // probed GNU local +0800 value; wall-clock check is zone independent
        assert_eq!(secs % 60, 0);
        let local = fs_win::to_local(m.mtime.as_u64() as i64).unwrap().0;
        assert_eq!((local.hour, local.minute), (16, 21));
        let (r, _) = set(&["-t", "200402291621.30", "reg"]);
        assert_eq!(r, 0);
        let local = fs_win::to_local(
            fs_win::file_meta(&sc.dir, "reg", true)
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
            fs_win::file_meta(&sc.dir, "reg", true)
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
            fs_win::file_meta(&sc.dir, "reg", true)
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
}
