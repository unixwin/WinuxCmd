//! `cal` — util-linux cal.c semantics, ported for WinuxCmd 2.0 (#1488).
//!
//! Pure std-only Gregorian calendar rendering with `-1`/`-3`/`-s`/`-m`/`-j`/
//! `-n`/`-S`/`-y`/`-Y`/`-V`/`-h`, explicit `Io`/`Ctx`, and zero globals.

use std::ffi::OsString;
use std::io::Write;

use crate::argopt;
use winux_core::{tr, usage_error, CmdResult, Ctx, Io};

const MONTH_NAMES: [&str; 13] = [
    "",
    "January",
    "February",
    "March",
    "April",
    "May",
    "June",
    "July",
    "August",
    "September",
    "October",
    "November",
    "December",
];
const DAY_NAMES_SHORT: [&str; 7] = ["Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"];
const DAY_NAMES_LONG: [&str; 7] = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];

const CAL_HELP: &str = "\
Usage: cal [OPTION] [[MONTH] YEAR]
Display a calendar.

  -1, --one         display only the requested month
  -3, --three       display previous, current and next month
  -s, --sunday      start week on Sunday
  -m, --monday      start week on Monday
  -j, --julian      display Julian days
  -n, --months=NUM  display NUM months
  -S, --span        span the months around the request
  -y, --year        display the whole year
  -Y, --twelve      display the next twelve months
";

#[cfg(windows)]
fn current_local_year_month() -> (i32, i32) {
    #[repr(C)]
    struct SystemTimeWin {
        year: u16,
        month: u16,
        day_of_week: u16,
        day: u16,
        hour: u16,
        minute: u16,
        second: u16,
        milliseconds: u16,
    }
    extern "system" {
        fn GetLocalTime(lpSystemTime: *mut SystemTimeWin);
    }
    let mut st = SystemTimeWin {
        year: 1970,
        month: 1,
        day_of_week: 0,
        day: 1,
        hour: 0,
        minute: 0,
        second: 0,
        milliseconds: 0,
    };
    unsafe {
        GetLocalTime(&mut st);
    }
    (st.year as i32, st.month as i32)
}

#[cfg(not(windows))]
fn current_local_year_month() -> (i32, i32) {
    let secs = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs() as i64)
        .unwrap_or(0);
    let z = secs.div_euclid(86_400) + 719_468;
    let era = if z >= 0 { z } else { z - 146_096 } / 146_097;
    let doe = z - era * 146_097;
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146_096) / 365;
    let y = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let m = if mp < 10 { mp + 3 } else { mp - 9 };
    let year = (y + if m <= 2 { 1 } else { 0 }) as i32;
    (year, m as i32)
}

fn is_leap_year(year: i32) -> bool {
    (year % 400 == 0) || (year % 100 != 0 && year % 4 == 0)
}

fn days_in_month(year: i32, month: i32) -> i32 {
    const DAYS: [i32; 13] = [0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
    if month == 2 && is_leap_year(year) {
        29
    } else {
        DAYS[month as usize]
    }
}

fn day_of_year(year: i32, month: i32, day: i32) -> i32 {
    let mut total = day;
    for m in 1..month {
        total += days_in_month(year, m);
    }
    total
}

fn weekday_sunday_zero(mut year: i32, month: i32, day: i32) -> i32 {
    const OFFSETS: [i32; 12] = [0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4];
    if month < 3 {
        year -= 1;
    }
    (year + year / 4 - year / 100 + year / 400 + OFFSETS[(month - 1) as usize] + day).rem_euclid(7)
}

fn center(text: &str, width: usize) -> String {
    let truncated: String = text.chars().take(width).collect();
    if truncated.len() >= width {
        return truncated;
    }
    let padding = width - truncated.len();
    let left = (padding + 1) / 2;
    let right = padding - left;
    format!("{}{}{}", " ".repeat(left), truncated, " ".repeat(right))
}

fn right_aligned(value: i32, width: usize) -> String {
    if value <= 0 {
        return " ".repeat(width);
    }
    let s = value.to_string();
    if s.len() >= width {
        s
    } else {
        format!("{}{}", " ".repeat(width - s.len()), s)
    }
}

#[derive(Clone, Copy)]
struct RenderStyle {
    weekstart: i32,
    julian: bool,
    name_only_titles: bool,
}

fn month_width(style: RenderStyle) -> usize {
    if style.julian {
        27
    } else {
        20
    }
}

fn month_lines(year: i32, month: i32, style: RenderStyle) -> [String; 8] {
    let width = month_width(style);
    let first_cell = if style.julian { 3 } else { 2 };
    let cell = if style.julian { 4 } else { 3 };
    let mut lines: [String; 8] = Default::default();

    lines[0] = if style.name_only_titles {
        center(MONTH_NAMES[month as usize], width)
    } else {
        center(&format!("{} {:04}", MONTH_NAMES[month as usize], year), width)
    };

    let mut header = String::new();
    for i in 0..7 {
        let dow = (style.weekstart + i) % 7;
        let name = if style.julian {
            DAY_NAMES_LONG[dow as usize]
        } else {
            DAY_NAMES_SHORT[dow as usize]
        };
        if i > 0 {
            header.push(' ');
        }
        if name.len() < first_cell {
            header.push_str(&" ".repeat(first_cell - name.len()));
        }
        header.push_str(name);
    }
    lines[1] = header;

    let start_col = (weekday_sunday_zero(year, month, 1) - style.weekstart + 7) % 7;
    let last = days_in_month(year, month);
    let mut day = 1;
    for week in 0..6 {
        let mut row = String::with_capacity(width);
        for col in 0..7 {
            let in_month = !(week == 0 && col < start_col) && day <= last;
            let shown = if in_month {
                let val = if style.julian {
                    day_of_year(year, month, day)
                } else {
                    day
                };
                day += 1;
                val
            } else {
                0
            };
            row.push_str(&right_aligned(shown, if col == 0 { first_cell } else { cell }));
        }
        lines[(week + 2) as usize] = row;
    }
    lines
}

fn print_block_row(io: &mut Io, months: &[(i32, i32)], style: RenderStyle, gutter: usize) -> std::io::Result<()> {
    let blocks: Vec<[String; 8]> = months
        .iter()
        .map(|&(y, m)| month_lines(y, m, style))
        .collect();
    let pad = " ".repeat(gutter);
    for line in 0..8 {
        for (col, blk) in blocks.iter().enumerate() {
            io.stdout.write_all(blk[line].as_bytes())?;
            if col + 1 < blocks.len() {
                io.stdout.write_all(pad.as_bytes())?;
            }
        }
        io.stdout.write_all(b"\n")?;
    }
    Ok(())
}

fn month_from_name(text: &str) -> Option<i32> {
    let lower = text.to_ascii_lowercase();
    for m in 1..=12 {
        let full = MONTH_NAMES[m].to_ascii_lowercase();
        if lower == full || lower == full[..3] {
            return Some(m as i32);
        }
    }
    None
}

fn is_digit_string(s: &str) -> bool {
    !s.is_empty() && s.bytes().all(|b| b.is_ascii_digit())
}

pub fn cal_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut style = RenderStyle {
        weekstart: 0,
        julian: false,
        name_only_titles: false,
    };
    let mut three = false;
    let mut one = false;
    let mut year_flag = false;
    let mut twelve = false;
    let mut span = false;
    let mut n_text: Option<String> = None;
    let mut positionals: Vec<String> = Vec::new();
    let mut no_more_opts = false;
    let mut i = 0;

    while i < args.len() {
        let s = args[i].to_string_lossy();
        if no_more_opts {
            positionals.push(s.into_owned());
            i += 1;
            continue;
        }
        if s == "--" {
            no_more_opts = true;
            i += 1;
            continue;
        }
        if s.starts_with("--") {
            if s == "--help" {
                io.stdout.write_all(CAL_HELP.as_bytes())?;
                return Ok(0);
            } else if s == "--version" {
                writeln!(io.stdout, "cal (WinuxCmd) 1.0")?;
                return Ok(0);
            } else if s == "--one" {
                one = true;
            } else if s == "--three" {
                three = true;
            } else if s == "--sunday" {
                style.weekstart = 0;
            } else if s == "--monday" {
                style.weekstart = 1;
            } else if s == "--julian" {
                style.julian = true;
            } else if s == "--span" {
                span = true;
            } else if s == "--year" {
                year_flag = true;
            } else if s == "--twelve" {
                twelve = true;
            } else if let Some(val) = s.strip_prefix("--months=") {
                n_text = Some(val.to_owned());
            } else if s == "--months" {
                i += 1;
                if i >= args.len() {
                    let _ = usage_error(ctx, io, "cal", &argopt::requires_arg(ctx, "--months"));
                    return Ok(1);
                }
                n_text = Some(args[i].to_string_lossy().into_owned());
            } else {
                let _ = usage_error(ctx, io, "cal", &argopt::unrecognized_option(ctx, &s));
                return Ok(1);
            }
            i += 1;
            continue;
        }
        if s.starts_with('-') && s.len() > 1 {
            let bytes = s.as_bytes();
            let mut j = 1;
            while j < bytes.len() {
                match bytes[j] {
                    b'h' => {
                        io.stdout.write_all(CAL_HELP.as_bytes())?;
                        return Ok(0);
                    }
                    b'V' => {
                        writeln!(io.stdout, "cal (WinuxCmd) 1.0")?;
                        return Ok(0);
                    }
                    b'1' => one = true,
                    b'3' => three = true,
                    b's' => style.weekstart = 0,
                    b'm' => style.weekstart = 1,
                    b'j' => style.julian = true,
                    b'S' => span = true,
                    b'y' => year_flag = true,
                    b'Y' => twelve = true,
                    b'n' => {
                        if j + 1 < bytes.len() {
                            n_text = Some(s[j + 1..].to_owned());
                        } else {
                            i += 1;
                            if i >= args.len() {
                                let _ = usage_error(ctx, io, "cal", &argopt::requires_short_arg(ctx, 'n'));
                                return Ok(1);
                            }
                            n_text = Some(args[i].to_string_lossy().into_owned());
                        }
                        break;
                    }
                    other => {
                        let ch = (other as char).to_string();
                        let _ = usage_error(ctx, io, "cal", &argopt::invalid_option(ctx, &ch));
                        return Ok(1);
                    }
                }
                j += 1;
            }
            i += 1;
            continue;
        }
        positionals.push(s.into_owned());
        i += 1;
    }

    let mut num_months: i32 = 0;
    if let Some(ref nt) = n_text {
        match nt.parse::<i32>() {
            Ok(v) if v >= 0 => num_months = v,
            _ => {
                writeln!(io.stderr, "cal: invalid month argument: '{nt}'")?;
                return Ok(1);
            }
        }
    }
    if three {
        num_months = 3;
        span = true;
    }
    if twelve {
        num_months = 12;
    }

    let (cur_year, cur_month) = current_local_year_month();
    let mut year = cur_year;
    let mut month = cur_month;
    let mut year_only = false;

    if positionals.len() > 3 {
        writeln!(io.stderr, "cal: bad usage")?;
        let try_msg = tr!(ctx, "common.try_help", "Try '{0} --help' for more information.");
        writeln!(io.stderr, "{}", try_msg.replace("{0}", "cal"))?;
        return Ok(1);
    }

    let mut idx = 0usize;
    let mut day: i32 = 0;
    if positionals.len() == 1 && !is_digit_string(&positionals[0]) {
        match month_from_name(&positionals[0]) {
            Some(m) => {
                month = m;
                year = cur_year;
            }
            None => {
                writeln!(
                    io.stderr,
                    "cal: failed to parse timestamp or unknown month name: {}",
                    positionals[0]
                )?;
                return Ok(1);
            }
        }
    } else if !positionals.is_empty() {
        if positionals.len() == 3 {
            day = match positionals[0].parse::<i32>() {
                Ok(d) => d,
                Err(_) => {
                    writeln!(io.stderr, "cal: illegal day value: '{}'", positionals[0])?;
                    return Ok(1);
                }
            };
            if !(1..=31).contains(&day) {
                writeln!(io.stderr, "cal: illegal day value: use 1-31")?;
                return Ok(1);
            }
            idx = 1;
        }
        if positionals.len() >= 2 {
            let month_text = &positionals[idx];
            if month_text.as_bytes().first().is_some_and(|b| b.is_ascii_digit()) {
                month = month_text.parse::<i32>().unwrap_or(0);
            } else {
                match month_from_name(month_text) {
                    Some(m) => month = m,
                    None => {
                        writeln!(io.stderr, "cal: unknown month name: {month_text}")?;
                        return Ok(1);
                    }
                }
            }
            if !(1..=12).contains(&month) {
                writeln!(io.stderr, "cal: illegal month value: use 1-12")?;
                return Ok(1);
            }
            idx += 1;
        }
        year = match positionals[idx].parse::<i32>() {
            Ok(y) => y,
            Err(_) => {
                writeln!(io.stderr, "cal: illegal year value: '{}'", positionals[idx])?;
                return Ok(1);
            }
        };
        if year < 1 {
            writeln!(io.stderr, "cal: illegal year value: use positive integer")?;
            return Ok(1);
        }
        if positionals.len() == 1 {
            year_only = true;
        }
        if day > 0 && day > days_in_month(year, month) {
            writeln!(
                io.stderr,
                "cal: illegal day value: use 1-{}",
                days_in_month(year, month)
            )?;
            return Ok(1);
        }
    }

    if year_flag || (year_only && num_months == 0 && !one) {
        let year_style = RenderStyle {
            name_only_titles: true,
            ..style
        };
        let width = 3 * month_width(style) + 6;
        writeln!(io.stdout, "{}", center(&format!("{year:04}"), width))?;
        writeln!(io.stdout)?;
        for first in (1..=12).step_by(3) {
            print_block_row(
                io,
                &[(year, first), (year, first + 1), (year, first + 2)],
                year_style,
                3,
            )?;
        }
        return Ok(0);
    }

    if num_months == 0 {
        num_months = 1;
    }
    let mut start_year = year;
    let mut start_month = month;
    if span {
        for _ in 0..(num_months / 2) {
            start_month -= 1;
            if start_month < 1 {
                start_month = 12;
                start_year -= 1;
            }
        }
    }

    let mut sequence = Vec::with_capacity(num_months as usize);
    let mut y = start_year;
    let mut m = start_month;
    for _ in 0..num_months {
        sequence.push((y, m));
        m += 1;
        if m > 12 {
            m = 1;
            y += 1;
        }
    }

    let gutter = if twelve { 3 } else { 2 };
    for chunk in sequence.chunks(3) {
        print_block_row(io, chunk, style, gutter)?;
    }
    Ok(0)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run_cal(args: &[&str]) -> (u32, String, String) {
        let sc = ScratchCtx::new("cal-test");
        let ctx = sc.ctx();
        let mut io = MemIo::new("");
        let os_args: Vec<OsString> = args.iter().map(OsString::from).collect();
        let rc = cal_cmd(&os_args, &mut io.split(), &ctx).unwrap();
        (rc, io.out(), io.err())
    }

    #[test]
    fn july_2026_matches_util_linux_fixed_width() {
        let (rc, out, err) = run_cal(&["7", "2026"]);
        assert_eq!(rc, 0);
        assert!(err.is_empty());
        let expected = concat!(
            "      July 2026     \n",
            "Su Mo Tu We Th Fr Sa\n",
            "          1  2  3  4\n",
            " 5  6  7  8  9 10 11\n",
            "12 13 14 15 16 17 18\n",
            "19 20 21 22 23 24 25\n",
            "26 27 28 29 30 31   \n",
            "                    \n",
        );
        assert_eq!(out, expected);
    }

    #[test]
    fn monday_first_march_2024() {
        let (rc, out, _) = run_cal(&["-m", "3", "2024"]);
        assert_eq!(rc, 0);
        let expected = concat!(
            "     March 2024     \n",
            "Mo Tu We Th Fr Sa Su\n",
            "             1  2  3\n",
            " 4  5  6  7  8  9 10\n",
            "11 12 13 14 15 16 17\n",
            "18 19 20 21 22 23 24\n",
            "25 26 27 28 29 30 31\n",
            "                    \n",
        );
        assert_eq!(out, expected);
    }

    #[test]
    fn year_view_and_error_cases() {
        let (rc, out, _) = run_cal(&["2026"]);
        assert_eq!(rc, 0);
        assert!(out.contains("2026"));
        assert!(out.contains("January"));
        assert!(out.contains("December"));

        let (rc, _, err) = run_cal(&["13", "2026"]);
        assert_eq!(rc, 1);
        assert!(err.contains("illegal month value"));

        let (rc, _, err) = run_cal(&["-n", "bogus"]);
        assert_eq!(rc, 1);
        assert!(err.contains("invalid month argument"));
    }
}
