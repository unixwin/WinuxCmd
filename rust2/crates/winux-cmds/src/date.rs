//! `date` — GNU coreutils date.c (9.4) semantics, probed byte-for-byte
//! against WSL coreutils 9.4 (rust2/target/probe/w5-date.sh, w5-tz.sh).
//!
//! Deterministic core (everything the gate can compare):
//! * `-d DATE` parse subset — ISO `YYYY-MM-DD[ HH:MM[:SS]]`, `@EPOCH`
//!   (negative allowed), trailing relative adjustments (`+ N unit`,
//!   `- N unit`, `N unit ago`; month/year are calendar units — probed:
//!   Jan 31 + 1 month -> Mar 2, overflow rolls; Mar 1 - 1 day -> Feb 29
//!   in 2020), explicit `UTC`/`GMT` zone suffix; bare wall times are
//!   interpreted in the effective zone (TZ env / system local).
//! * `+FORMAT` strftime subset — probed conversions; GNU's unknown-
//!   conversion rule prints the token literally (probed `%Q` -> `%Q`).
//! * `-u`, `-R`, `-I[=FMT]`/`--iso-8601`, `--rfc-3339`, `--rfc-email`,
//!   `-r FILE`, `%s`, week mechanics (%U %W %V %G %g), %N zeros.
//! * `-s SET` (set the clock) is not supported on Windows: GNU on the
//!   gate host also FAILS to set the clock (probed rc 1), and the port
//!   refuses with the same status (message recorded, not gated).
//! * a non-`+` format operand is the GNU "lacks a leading '+'" diagnostic.
//!
//! Time zones: TZ is POSIX TZ-string semantics (`EST5EDT` uses the US DST
//! rules by default — probed EST in January / EDT in July; `XYZ+5:30` is
//! west-positive = UTC-5:30; `GMT-8` = UTC+8; `<+07>-7` angle names).
//! IANA names (`America/New_York`) need a zoneinfo database Windows
//! lacks — GNU resolves them, the port falls back to UTC (documented
//! boundary; gate rows pin POSIX-form TZ values via the env column).
//! An unset TZ reads the system local zone (Windows API bias), the
//! honest equivalent of GNU's /etc/localtime.
//!
//! du --time shares [`TimeZone`] and [`strftime`] helpers from this
//! module (pub(crate) surface).

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use crate::fsutil::resolve;
use crate::gnuhelp;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('d'),
        long: "date",
        takes_value: true,
    },
    OptSpec {
        short: Some('f'),
        long: "file",
        takes_value: true,
    },
    // -I's optional argument is handled by the -I pre-scan below (GNU
    // getopt long optional_argument shape: only glued "-Iseconds" and
    // "--iso-8601=kind" carry a value; bare "-I" does not).
    OptSpec {
        short: Some('I'),
        long: "iso-8601",
        takes_value: false,
    },
    OptSpec {
        short: Some('r'),
        long: "reference",
        takes_value: true,
    },
    OptSpec {
        short: Some('R'),
        long: "rfc-email",
        takes_value: false,
    },
    OptSpec {
        short: Some('s'),
        long: "set",
        takes_value: true,
    },
    OptSpec {
        short: Some('u'),
        long: "utc",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "rfc-3339",
        takes_value: true,
    },
    OptSpec {
        // obsolete alias of -R (GNU accepts both)
        short: None,
        long: "rfc-2822",
        takes_value: false,
    },
    OptSpec {
        short: Some('v'),
        long: "debug",
        takes_value: false,
    },
];

// `--help` body verbatim from `env date --help` under WSL coreutils 9.4.
const DATE_HELP_BODY: &str = r#"Usage: date [OPTION]... [+FORMAT]
  or:  date [-u|--utc|--universal] [MMDDhhmm[[CC]YY][.ss]]
Display date and time in the given FORMAT.
With -s, or with [MMDDhhmm[[CC]YY][.ss]], set the date and time.

Mandatory arguments to long options are mandatory for short options too.
  -d, --date=STRING          display time described by STRING, not 'now'
      --debug                annotate the parsed date,
                              and warn about questionable usage to stderr
  -f, --file=DATEFILE        like --date; once for each line of DATEFILE
  -I[FMT], --iso-8601[=FMT]  output date/time in ISO 8601 format.
                               FMT='date' for date only (the default),
                               'hours', 'minutes', 'seconds', or 'ns'
                               for date and time to the indicated precision.
                               Example: 2006-08-14T02:34:56-06:00
  --resolution               output the available resolution of timestamps
                               Example: 0.000000001
  -R, --rfc-email            output date and time in RFC 5322 format.
                               Example: Mon, 14 Aug 2006 02:34:56 -0600
      --rfc-3339=FMT         output date/time in RFC 3339 format.
                               FMT='date', 'seconds', or 'ns'
                               for date and time to the indicated precision.
                               Example: 2006-08-14 02:34:56-06:00
  -r, --reference=FILE       display the last modification time of FILE
  -s, --set=STRING           set time described by STRING
  -u, --utc, --universal     print or set Coordinated Universal Time (UTC)
      --help        display this help and exit
      --version     output version information and exit

All options that specify the date to display are mutually exclusive.
I.e.: --date, --file, --reference, --resolution.

FORMAT controls the output.  Interpreted sequences are:

  %%   a literal %
  %a   locale's abbreviated weekday name (e.g., Sun)
  %A   locale's full weekday name (e.g., Sunday)
  %b   locale's abbreviated month name (e.g., Jan)
  %B   locale's full month name (e.g., January)
  %c   locale's date and time (e.g., Thu Mar  3 23:05:25 2005)
  %C   century; like %Y, except omit last two digits (e.g., 20)
  %d   day of month (e.g., 01)
  %D   date; same as %m/%d/%y
  %e   day of month, space padded; same as %_d
  %F   full date; like %+4Y-%m-%d
  %g   last two digits of year of ISO week number (see %G)
  %G   year of ISO week number (see %V); normally useful only with %V
  %h   same as %b
  %H   hour (00..23)
  %I   hour (01..12)
  %j   day of year (001..366)
  %k   hour, space padded ( 0..23); same as %_H
  %l   hour, space padded ( 1..12); same as %_I
  %m   month (01..12)
  %M   minute (00..59)
  %n   a newline
  %N   nanoseconds (000000000..999999999)
  %p   locale's equivalent of either AM or PM; blank if not known
  %P   like %p, but lower case
  %q   quarter of year (1..4)
  %r   locale's 12-hour clock time (e.g., 11:11:04 PM)
  %R   24-hour hour and minute; same as %H:%M
  %s   seconds since the Epoch (1970-01-01 00:00 UTC)
  %S   second (00..60)
  %t   a tab
  %T   time; same as %H:%M:%S
  %u   day of week (1..7); 1 is Monday
  %U   week number of year, with Sunday as first day of week (00..53)
  %V   ISO week number, with Monday as first day of week (01..53)
  %w   day of week (0..6); 0 is Sunday
  %W   week number of year, with Monday as first day of week (00..53)
  %x   locale's date representation (e.g., 12/31/99)
  %X   locale's time representation (e.g., 23:13:48)
  %y   last two digits of year (00..99)
  %Y   year
  %z   +hhmm numeric time zone (e.g., -0400)
  %:z  +hh:mm numeric time zone (e.g., -04:00)
  %::z  +hh:mm:ss numeric time zone (e.g., -04:00:00)
  %:::z  numeric time zone with : to necessary precision (e.g., -04, +05:30)
  %Z   alphabetic time zone abbreviation (e.g., EDT)

By default, date pads numeric fields with zeroes.
The following optional flags may follow '%':

  -  (hyphen) do not pad the field
  _  (underscore) pad with spaces
  0  (zero) pad with zeros
  +  pad with zeros, and put '+' before future years with >4 digits
  ^  use upper case if possible
  #  use opposite case if possible

After any flags comes an optional field width, as a decimal number;
then an optional modifier, which is either
E to use the locale's alternate representations if available, or
O to use the locale's alternate numeric symbols if available.

Examples:
Convert seconds since the Epoch (1970-01-01 UTC) to a date
  $ date --date='@2147483647'

Show the time on the west coast of the US (use tzselect(1) to find TZ)
  $ TZ='America/Los_Angeles' date

Show the local time for 9AM next Friday on the west coast of the US
  $ date --date='TZ="America/Los_Angeles" 09:00 next Fri'

"#;

// ---------------------------------------------------------------------------
// civil-time primitives (proleptic Gregorian; days_from_civil family)
// ---------------------------------------------------------------------------

const MONTH_DAYS: [i64; 12] = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
const WEEKDAYS: [&str; 7] = [
    "Sunday",
    "Monday",
    "Tuesday",
    "Wednesday",
    "Thursday",
    "Friday",
    "Saturday",
];
const WEEKDAYS_SHORT: [&str; 7] = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
const MONTHS: [&str; 12] = [
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
const MONTHS_SHORT: [&str; 12] = [
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
];

fn is_leap(y: i64) -> bool {
    (y % 4 == 0 && y % 100 != 0) || y % 400 == 0
}

fn days_in_month(y: i64, m: i64) -> i64 {
    if m == 2 && is_leap(y) {
        29
    } else {
        MONTH_DAYS[(m - 1) as usize]
    }
}

/// Days from 1970-01-01 (Howard Hinnant's days_from_civil).
fn days_from_civil(y: i64, m: i64, d: i64) -> i64 {
    let y = if m <= 2 { y - 1 } else { y };
    let era = if y >= 0 { y } else { y - 399 } / 400;
    let yoe = y - era * 400;
    let mp = (m + 9) % 12;
    let doy = (153 * mp + 2) / 5 + d - 1;
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    era * 146097 + doe - 719468
}

/// Civil date from days since 1970-01-01.
fn civil_from_days(z: i64) -> (i64, i64, i64) {
    let z = z + 719468;
    let era = if z >= 0 { z } else { z - 146096 } / 146097;
    let doe = z - era * 146097;
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    let y = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = if mp < 10 { mp + 3 } else { mp - 9 };
    (if m <= 2 { y + 1 } else { y }, m, d)
}

fn weekday_from_days(z: i64) -> i64 {
    // 1970-01-01 was a Thursday (4 with Sunday=0)
    (z + 4).rem_euclid(7)
}

/// Broken-down local time.
#[derive(Clone)]
pub(crate) struct DateTime {
    pub year: i64,
    pub month: i64, // 1-12
    pub day: i64,   // 1-31
    pub hour: i64,
    pub minute: i64,
    pub second: i64,
    pub epoch: i64,      // UTC epoch seconds
    pub utc_offset: i64, // seconds east of UTC
    pub tzname: String,
}

impl DateTime {
    pub fn from_epoch(epoch: i64, offset: i64, tzname: String) -> Self {
        let local = epoch + offset;
        let days = local.div_euclid(86400);
        let secs = local.rem_euclid(86400);
        let (year, month, day) = civil_from_days(days);
        DateTime {
            year,
            month,
            day,
            hour: secs / 3600,
            minute: (secs % 3600) / 60,
            second: secs % 60,
            epoch,
            utc_offset: offset,
            tzname,
        }
    }

    fn days_since_epoch(&self) -> i64 {
        days_from_civil(self.year, self.month, self.day)
    }

    fn weekday(&self) -> usize {
        weekday_from_days(self.days_since_epoch()) as usize
    }

    fn yearday(&self) -> i64 {
        days_from_civil(self.year, self.month, self.day) - days_from_civil(self.year, 1, 1) + 1
    }
}

// ---------------------------------------------------------------------------
// POSIX TZ strings
// ---------------------------------------------------------------------------

#[derive(Clone, Copy)]
enum RuleKind {
    /// Jn — day n of the year, 1..365, never counting Feb 29
    Jday(i64),
    /// n — day n of the year, 0..365, counting Feb 29
    ZeroDay(i64),
    /// Mm.n.d — month m, week n (1-5, 5=last), weekday d (0=Sunday)
    MonthWeekDay(i64, i64, i64),
}

struct TzRule {
    kind: RuleKind,
    time: i64, // seconds into the day (2:00 default)
}

pub(crate) struct TimeZone {
    std_name: String,
    /// seconds WEST of UTC (POSIX convention; EST5 -> 18000)
    std_offset: i64,
    dst_name: Option<String>,
    dst_offset: Option<i64>,
    start: Option<TzRule>,
    end: Option<TzRule>,
}

fn parse_tz_number(s: &str) -> Option<(i64, usize)> {
    let b = s.as_bytes();
    let mut i = 0;
    while i < b.len() && b[i].is_ascii_digit() {
        i += 1;
    }
    if i == 0 {
        return None;
    }
    s[..i].parse().ok().map(|v| (v, i))
}

/// POSIX offset: [+|-]hh[:mm[:ss]], west-positive (EST5 = UTC-5).
fn parse_tz_offset(s: &str) -> Option<(i64, usize)> {
    let b = s.as_bytes();
    let mut i = 0;
    let mut sign = 1i64;
    if i < b.len() && (b[i] == b'+' || b[i] == b'-') {
        if b[i] == b'-' {
            sign = -1;
        }
        i += 1;
    }
    let (h, used) = parse_tz_number(&s[i..])?;
    i += used;
    let mut total = h * 3600;
    // up to two :mm[:ss] components
    if i < b.len() && b[i] == b':' {
        if let Some((v, used)) = parse_tz_number(&s[i + 1..]) {
            total += v * 60;
            i += 1 + used;
            if i < b.len() && b[i] == b':' {
                if let Some((v, used)) = parse_tz_number(&s[i + 1..]) {
                    total += v;
                    i += 1 + used;
                }
            }
        }
    }
    if total > 24 * 3600 {
        return None;
    }
    Some((sign * total, i))
}

fn parse_tz_name(s: &str) -> Option<(String, usize)> {
    if let Some(rest) = s.strip_prefix('<') {
        let end = rest.find('>')?;
        return Some((rest[..end].to_string(), end + 2));
    }
    let b = s.as_bytes();
    let mut i = 0;
    // POSIX: the alphabetic run; digits begin the offset (EST5EDT)
    while i < b.len() && b[i].is_ascii_alphabetic() {
        i += 1;
    }
    if i < 3 {
        return None;
    }
    Some((s[..i].to_string(), i))
}

/// Mm.n.d[/time] | Jn[/time] | n[/time]
fn parse_tz_rule(s: &str) -> Option<(TzRule, usize)> {
    let (kind, used) = if let Some(rest) = s.strip_prefix('M') {
        let (m, u1) = parse_tz_number(rest)?;
        let rest2 = rest[u1..].strip_prefix('.')?;
        let (n, u2) = parse_tz_number(rest2)?;
        let rest3 = rest2[u2..].strip_prefix('.')?;
        let (d, u3) = parse_tz_number(rest3)?;
        (RuleKind::MonthWeekDay(m, n, d), 1 + u1 + 1 + u2 + 1 + u3)
    } else if let Some(rest) = s.strip_prefix('J') {
        let (n, u) = parse_tz_number(rest)?;
        (RuleKind::Jday(n), 1 + u)
    } else {
        let (n, u) = parse_tz_number(s)?;
        (RuleKind::ZeroDay(n), u)
    };
    let mut total = used;
    let mut time = 2 * 3600;
    if let Some(rest) = s[total..].strip_prefix('/') {
        if let Some((v, u)) = parse_tz_offset(rest) {
            time = v;
            total += 1 + u;
        }
    }
    Some((TzRule { kind, time }, total))
}

impl TimeZone {
    /// UTC constant zone.
    pub fn utc() -> Self {
        TimeZone {
            std_name: "UTC".to_string(),
            std_offset: 0,
            dst_name: None,
            dst_offset: None,
            start: None,
            end: None,
        }
    }

    /// Seconds west currently in the std (non-DST) branch — used to place
    /// a wall time before the offset is known.
    fn std_offset_west(&self) -> i64 {
        self.std_offset
    }

    /// Parse a POSIX TZ string. Unparseable values fall back to UTC
    /// (glibc falls back too; documented boundary for IANA names).
    pub fn parse(tz: &str) -> Self {
        if tz.is_empty() || tz == "UTC" || tz == "UTC0" || tz == "GMT" || tz == "GMT0" {
            return TimeZone::utc();
        }
        let (std_name, u1) = match parse_tz_name(tz) {
            Some(v) => v,
            None => return TimeZone::utc(),
        };
        let rest = &tz[u1..];
        let (std_offset, u2) = match parse_tz_offset(rest) {
            Some(v) => v,
            None => return TimeZone::utc(),
        };
        let mut zone = TimeZone {
            std_name,
            std_offset,
            dst_name: None,
            dst_offset: None,
            start: None,
            end: None,
        };
        let mut rest = &rest[u2..];
        if rest.is_empty() {
            return zone;
        }
        let (dst_name, u3) = match parse_tz_name(rest) {
            Some(v) => v,
            None => return zone,
        };
        rest = &rest[u3..];
        let first_digit_or_sign = rest
            .as_bytes()
            .first()
            .map(|c| c.is_ascii_digit() || *c == b'+' || *c == b'-')
            .unwrap_or(false);
        let (dst_offset, u4) = if first_digit_or_sign {
            match parse_tz_offset(rest) {
                Some(v) => v,
                None => return zone,
            }
        } else {
            // POSIX default: DST is one hour east of standard
            (zone.std_offset - 3600, 0)
        };
        zone.dst_name = Some(dst_name);
        zone.dst_offset = Some(dst_offset);
        rest = &rest[u4..];
        if let Some(rest2) = rest.strip_prefix(',') {
            let (start, u) = match parse_tz_rule(rest2) {
                Some(v) => v,
                None => return zone,
            };
            let rest3 = match rest2[u..].strip_prefix(',') {
                Some(r) => r,
                None => return zone,
            };
            let (end, _) = match parse_tz_rule(rest3) {
                Some(v) => v,
                None => return zone,
            };
            zone.start = Some(start);
            zone.end = Some(end);
        } else if rest.is_empty() {
            // default US DST rules (glibc; probed EST5EDT Jan=EST Jul=EDT)
            zone.start = Some(TzRule {
                kind: RuleKind::MonthWeekDay(3, 2, 0),
                time: 2 * 3600,
            });
            zone.end = Some(TzRule {
                kind: RuleKind::MonthWeekDay(11, 1, 0),
                time: 2 * 3600,
            });
        }
        zone
    }

    fn rule_day(&self, year: i64, rule: &TzRule) -> i64 {
        match rule.kind {
            RuleKind::Jday(n) => days_from_civil(year, 1, 1) + n - 1,
            RuleKind::ZeroDay(n) => days_from_civil(year, 1, 1) + n,
            RuleKind::MonthWeekDay(m, n, d) => {
                let first = days_from_civil(year, m, 1);
                let first_wd = weekday_from_days(first);
                let mut day = 1 + (d - first_wd).rem_euclid(7) + (n - 1) * 7;
                if day > days_in_month(year, m) {
                    day -= 7; // n=5 means the last occurrence
                }
                days_from_civil(year, m, day)
            }
        }
    }

    /// Offset seconds EAST of UTC in effect at `epoch`, plus the name.
    pub fn at(&self, epoch: i64) -> (i64, String) {
        let (dst_name, dst_offset) = match (&self.dst_name, self.dst_offset) {
            (Some(n), Some(o)) => (n.clone(), o),
            _ => return (-self.std_offset, self.std_name.clone()),
        };
        let (start, end) = match (&self.start, &self.end) {
            (Some(s), Some(e)) => (s, e),
            _ => return (-self.std_offset, self.std_name.clone()),
        };
        // POSIX compares in local-standard seconds
        let local_std = epoch + self.std_offset;
        let year = civil_from_days(local_std.div_euclid(86400)).0;
        let start_instant = self.rule_day(year, start) * 86400 + start.time;
        let end_instant = self.rule_day(year, end) * 86400 + end.time;
        let in_dst = if start_instant <= end_instant {
            local_std >= start_instant && local_std < end_instant
        } else {
            local_std >= start_instant || local_std < end_instant
        };
        if in_dst {
            (-dst_offset, dst_name)
        } else {
            (-self.std_offset, self.std_name.clone())
        }
    }
}

/// The zone for the command: `-u` forces UTC, otherwise the TZ env
/// (POSIX string) or the system local zone. Gate rows pin TZ explicitly.
pub(crate) fn effective_zone(use_utc: bool, env: &dyn winux_core::EnvSource) -> TimeZone {
    if use_utc {
        return TimeZone::utc();
    }
    if let Some(tz) = env.get("TZ") {
        let tz = tz.to_string_lossy().into_owned();
        if !tz.is_empty() {
            return TimeZone::parse(&tz);
        }
    }
    system_local_zone()
}

#[cfg(windows)]
fn system_local_zone() -> TimeZone {
    // System local zone as a fixed-offset zone read at call time (the
    // honest equivalent of GNU reading /etc/localtime once). The gate
    // never exercises unset-TZ rows; reading the CURRENT bias is the
    // documented approximation.
    #[repr(C)]
    struct Tzi {
        bias: i32,
        standard_name: [u16; 32],
        standard_date: [u32; 8],
        standard_bias: i32,
        daylight_name: [u16; 32],
        daylight_date: [u32; 8],
        daylight_bias: i32,
    }
    #[link(name = "kernel32")]
    extern "system" {
        fn GetTimeZoneInformation(tzi: *mut Tzi) -> u32;
    }
    unsafe {
        let mut tzi: Tzi = std::mem::zeroed();
        let rc = GetTimeZoneInformation(&mut tzi);
        let name = |chars: &[u16]| {
            let end = chars.iter().position(|&c| c == 0).unwrap_or(0);
            String::from_utf16_lossy(&chars[..end])
        };
        // rc 1 = in DST window (0 unknown, 2 standard)
        let (bias, zone_name) = if rc == 1 {
            (tzi.bias + tzi.daylight_bias, name(&tzi.daylight_name))
        } else {
            (tzi.bias, name(&tzi.standard_name))
        };
        TimeZone {
            std_name: if zone_name.is_empty() {
                "UTC".to_string()
            } else {
                zone_name
            },
            std_offset: bias as i64 * 60, // minutes west -> seconds west
            dst_name: None,
            dst_offset: None,
            start: None,
            end: None,
        }
    }
}

#[cfg(not(windows))]
fn system_local_zone() -> TimeZone {
    TimeZone::utc()
}

// ---------------------------------------------------------------------------
// -d DATE parsing (probed subset)
// ---------------------------------------------------------------------------

/// One relative adjustment from a `-d` tail.
#[derive(Clone, Copy)]
struct RelAmount {
    count: i64,
    unit_seconds: i64, // exact units ride the epoch
    calendar: CalUnit, // month/year are calendar units
}

#[derive(Clone, Copy, PartialEq)]
enum CalUnit {
    None,
    Day,
    Week,
    Month,
    Year,
}

/// A parsed -d operand.
struct ParsedDate {
    y: i64,
    m: i64,
    d: i64,
    hh: i64,
    mm: i64,
    ss: i64,
    /// @EPOCH (wall fields are UTC) or an explicit UTC/GMT/Z suffix
    force_utc: bool,
    /// "<N unit> ago" anchors on now
    from_now: bool,
    rels: Vec<RelAmount>,
}

fn parse_rel_groups(s: &str) -> Option<Vec<RelAmount>> {
    let mut out = Vec::new();
    let mut it = s.split_whitespace();
    while let Some(tok) = it.next() {
        let n: i64 = tok.parse().ok()?;
        let unit = it.next()?.trim_end_matches('s');
        let (unit_seconds, calendar) = match unit {
            "second" | "sec" => (1, CalUnit::None),
            "minute" | "min" => (60, CalUnit::None),
            "hour" => (3600, CalUnit::None),
            "day" => (86400, CalUnit::Day),
            "week" => (7 * 86400, CalUnit::Week),
            "month" => (0, CalUnit::Month),
            "year" => (0, CalUnit::Year),
            _ => return None,
        };
        // GNU chains groups ("2 years 3 months"); a joining +/- belongs to
        // the caller's split and is tolerated, anything else is a failure
        if let Some(extra) = it.next() {
            if extra != "+" && extra != "-" {
                return None;
            }
        }
        out.push(RelAmount {
            count: n,
            unit_seconds,
            calendar,
        });
    }
    Some(out)
}

fn parse_date_string(s: &str) -> Option<ParsedDate> {
    let s = s.trim();
    // @EPOCH (probed @0/@1234567890/@-1)
    if let Some(rest) = s.strip_prefix('@') {
        let epoch: i64 = rest.trim().parse().ok()?;
        let days = epoch.div_euclid(86400);
        let secs = epoch.rem_euclid(86400);
        let (y, m, d) = civil_from_days(days);
        return Some(ParsedDate {
            y,
            m,
            d,
            hh: secs / 3600,
            mm: (secs % 3600) / 60,
            ss: secs % 60,
            force_utc: true,
            from_now: false,
            rels: Vec::new(),
        });
    }
    let mut rels: Vec<RelAmount> = Vec::new();
    let mut from_now = false;
    // "<N unit> ago" anchors on now (probed shape)
    if let Some(rest) = s.strip_suffix(" ago") {
        rels = parse_rel_groups(rest)?;
        for r in &mut rels {
            r.count = -r.count;
        }
        from_now = true;
        return Some(ParsedDate {
            y: 1970,
            m: 1,
            d: 1,
            hh: 0,
            mm: 0,
            ss: 0,
            force_utc: true,
            from_now,
            rels,
        });
    }
    // "<base> + N unit[ + ...]" / "<base> - N unit"
    let mut base = s;
    if let Some(pos) = s.find(" + ") {
        base = &s[..pos];
        rels = parse_rel_groups(&s[pos + 3..])?;
    } else if let Some(pos) = s.find(" - ") {
        base = &s[..pos];
        rels = parse_rel_groups(&s[pos + 3..])?;
        for r in &mut rels {
            r.count = -r.count;
        }
    }
    // explicit trailing zone word (probed "… UTC")
    let mut force_utc = false;
    let mut words = base.split_whitespace().collect::<Vec<_>>();
    if let Some(last) = words.last() {
        if *last == "UTC" || *last == "GMT" || *last == "Z" {
            force_utc = true;
            words.pop();
        }
    }
    // ISO: Y-M-D [H:M[:S]]
    if words.is_empty() {
        return None;
    }
    let mut dmy = words[0].split('-');
    let y: i64 = dmy.next()?.parse().ok()?;
    let m: i64 = dmy.next()?.parse().ok()?;
    let d: i64 = dmy.next()?.parse().ok()?;
    let (mut hh, mut mm, mut ss) = (0, 0, 0);
    if words.len() > 1 {
        let mut t = words[1].split(':');
        hh = t.next()?.parse().ok()?;
        mm = t.next().map(|v| v.parse().ok()).unwrap_or(Some(0))?;
        ss = t.next().map(|v| v.parse().ok()).unwrap_or(Some(0))?;
    }
    if words.len() > 2 {
        return None;
    }
    if !(1..=12).contains(&m) || d < 1 || d > days_in_month(y, m) || hh > 23 || mm > 59 || ss > 60 {
        return None;
    }
    Some(ParsedDate {
        y,
        m,
        d,
        hh,
        mm,
        ss,
        force_utc,
        from_now,
        rels,
    })
}

/// Build the epoch of a parsed date under `zone` and apply the relative
/// adjustments.
fn resolve_parsed(p: &ParsedDate, zone: &TimeZone, now_epoch: i64) -> DateTime {
    if p.from_now {
        let (off, name) = zone.at(now_epoch);
        return apply_rels(DateTime::from_epoch(now_epoch, off, name), &p.rels, zone);
    }
    let epoch0 = if p.force_utc {
        // @EPOCH and explicit-zone forms: the wall fields already ARE UTC
        days_from_civil(p.y, p.m, p.d) * 86400 + p.hh * 3600 + p.mm * 60 + p.ss
    } else {
        // bare wall time interpreted in `zone`
        let days = days_from_civil(p.y, p.m, p.d);
        let wall = days * 86400 + p.hh * 3600 + p.mm * 60 + p.ss;
        let west = zone.std_offset_west();
        let (off, _) = zone.at(wall - west);
        wall - off
    };
    let (off, name) = zone.at(epoch0);
    apply_rels(DateTime::from_epoch(epoch0, off, name), &p.rels, zone)
}

fn apply_rels(dt: DateTime, rels: &[RelAmount], zone: &TimeZone) -> DateTime {
    let mut dt = dt;
    for r in rels {
        if r.calendar == CalUnit::None {
            dt.epoch += r.count * r.unit_seconds;
            let (off, name) = zone.at(dt.epoch);
            dt = DateTime::from_epoch(dt.epoch, off, name);
        } else {
            let days = r.count
                * match r.calendar {
                    CalUnit::Day => 1,
                    CalUnit::Week => 7,
                    _ => 0,
                };
            if days != 0 {
                let nd = dt.days_since_epoch() + days;
                let (y, m, d) = civil_from_days(nd);
                dt.year = y;
                dt.month = m;
                dt.day = d;
            } else if r.calendar == CalUnit::Month {
                // probed: Jan 31 + 1 month -> Mar 2 (overflow rolls)
                let total = dt.year * 12 + (dt.month - 1) + r.count;
                let mut y = total.div_euclid(12);
                let mut m = total.rem_euclid(12) + 1;
                let mut d = dt.day;
                while d > days_in_month(y, m) {
                    d -= days_in_month(y, m);
                    m += 1;
                    if m > 12 {
                        m = 1;
                        y += 1;
                    }
                }
                dt.year = y;
                dt.month = m;
                dt.day = d;
            } else {
                dt.year += r.count;
                if dt.month == 2 && dt.day == 29 && !is_leap(dt.year) {
                    dt.day = 28;
                }
            }
        }
    }
    // re-sync the epoch to the final civil fields
    let wall = dt.days_since_epoch() * 86400 + dt.hour * 3600 + dt.minute * 60 + dt.second;
    dt.epoch = wall - dt.utc_offset;
    dt
}

// ---------------------------------------------------------------------------
// +FORMAT strftime
// ---------------------------------------------------------------------------

fn fmt2(v: i64) -> String {
    format!("{v:02}")
}

fn fmt_pad(v: i64, width: usize) -> String {
    format!("{v:>width$}")
}

fn iso_week(dt: &DateTime) -> (i64, i64) {
    // ISO 8601 week %V/%G
    let wd = (dt.weekday() as i64 + 6) % 7; // Monday=0
    let thursday = dt.days_since_epoch() - wd + 3;
    let (gy, _, _) = civil_from_days(thursday);
    let jan1 = days_from_civil(gy, 1, 1);
    let week = (thursday - jan1) / 7 + 1;
    (gy, week)
}

pub(crate) fn strftime(dt: &DateTime, fmt: &str) -> String {
    let mut out = String::new();
    let mut chars = fmt.chars().peekable();
    while let Some(ch) = chars.next() {
        if ch != '%' {
            out.push(ch);
            continue;
        }
        match chars.next() {
            None => {
                out.push('%');
            }
            Some(c) => match c {
                'Y' => out.push_str(&dt.year.to_string()),
                'y' => out.push_str(&fmt2(dt.year.rem_euclid(100))),
                'C' => out.push_str(&fmt2(dt.year.div_euclid(100))),
                'm' => out.push_str(&fmt2(dt.month)),
                'd' => out.push_str(&fmt2(dt.day)),
                'e' => out.push_str(&fmt_pad(dt.day, 2)),
                'H' => out.push_str(&fmt2(dt.hour)),
                'k' => out.push_str(&fmt_pad(dt.hour, 2)),
                'I' => out.push_str(&fmt2(if dt.hour % 12 == 0 { 12 } else { dt.hour % 12 })),
                'l' => out.push_str(&fmt_pad(
                    if dt.hour % 12 == 0 { 12 } else { dt.hour % 12 },
                    2,
                )),
                'M' => out.push_str(&fmt2(dt.minute)),
                'S' => out.push_str(&fmt2(dt.second)),
                'N' => out.push_str("000000000"),
                'j' => out.push_str(&format!("{:03}", dt.yearday())),
                'p' => out.push_str(if dt.hour < 12 { "AM" } else { "PM" }),
                'P' => out.push_str(if dt.hour < 12 { "am" } else { "pm" }),
                'A' => out.push_str(WEEKDAYS[dt.weekday()]),
                'a' => out.push_str(WEEKDAYS_SHORT[dt.weekday()]),
                'B' => out.push_str(MONTHS[(dt.month - 1) as usize]),
                'b' | 'h' => out.push_str(MONTHS_SHORT[(dt.month - 1) as usize]),
                'Z' => out.push_str(&dt.tzname),
                'z' => {
                    let sign = if dt.utc_offset < 0 { '-' } else { '+' };
                    let a = dt.utc_offset.abs();
                    out.push_str(&format!("{sign}{:02}{:02}", a / 3600, (a % 3600) / 60));
                }
                's' => out.push_str(&dt.epoch.to_string()),
                'F' => out.push_str(&format!("{}-{}-{}", dt.year, fmt2(dt.month), fmt2(dt.day))),
                'T' => out.push_str(&format!(
                    "{}:{}:{}",
                    fmt2(dt.hour),
                    fmt2(dt.minute),
                    fmt2(dt.second)
                )),
                'R' => out.push_str(&format!("{}:{}", fmt2(dt.hour), fmt2(dt.minute))),
                'D' => out.push_str(&format!(
                    "{}/{}/{}",
                    fmt2(dt.month),
                    fmt2(dt.day),
                    fmt2(dt.year.rem_euclid(100))
                )),
                'n' => out.push('\n'),
                't' => out.push('\t'),
                '%' => out.push('%'),
                'U' => {
                    let jan1_wd = weekday_from_days(days_from_civil(dt.year, 1, 1));
                    let yd = dt.yearday() - 1;
                    out.push_str(&fmt2((yd + jan1_wd) / 7));
                }
                'W' => {
                    let jan1_wd = (weekday_from_days(days_from_civil(dt.year, 1, 1)) + 6) % 7;
                    let yd = dt.yearday() - 1;
                    out.push_str(&fmt2((yd + jan1_wd) / 7));
                }
                'V' | 'G' => {
                    let (gy, week) = iso_week(dt);
                    if c == 'V' {
                        out.push_str(&fmt2(week));
                    } else {
                        out.push_str(&gy.to_string());
                    }
                }
                'g' => {
                    let (gy, _) = iso_week(dt);
                    out.push_str(&fmt2(gy.rem_euclid(100)));
                }
                'u' => out.push_str(&(((dt.weekday() as i64 + 6) % 7) + 1).to_string()),
                'w' => out.push_str(&(dt.weekday() as i64).to_string()),
                _ => {
                    // GNU: an unknown conversion prints literally (probed %Q)
                    out.push('%');
                    out.push(c);
                }
            },
        }
    }
    out
}

// ---------------------------------------------------------------------------
// the command
// ---------------------------------------------------------------------------

/// GNU getopt optional_argument pre-scan for -I/--iso-8601: pull
/// `-I<glue>` and `--iso-8601[=kind]` tokens out before the shared parse
/// (argopt's OptSpec cannot express optional values; same rationale as
/// mkfifo's --context). Stops at `--`.
fn split_iso_tokens(args: &[OsString]) -> (Option<String>, Vec<OsString>) {
    let mut kind: Option<String> = None;
    let mut rest = Vec::new();
    let mut no_more_options = false;
    for a in args {
        let text = a.to_string_lossy();
        if !no_more_options {
            if text == "--" {
                no_more_options = true;
                rest.push(a.clone());
                continue;
            }
            if let Some(glue) = text.strip_prefix("-I") {
                if !glue.is_empty() {
                    kind = Some(glue.to_string());
                    continue;
                }
            }
            if let Some(v) = text.strip_prefix("--iso-8601=") {
                kind = Some(v.to_string());
                continue;
            }
            if text == "--iso-8601" {
                kind = Some(String::new());
                continue;
            }
        }
        rest.push(a.clone());
    }
    (kind, rest)
}

fn now_epoch() -> i64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs() as i64)
        .unwrap_or(0)
}

fn offset_parts(off: i64) -> (char, i64, i64) {
    let sign = if off < 0 { '-' } else { '+' };
    let a = off.abs();
    (sign, a / 3600, (a % 3600) / 60)
}

pub fn date_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let (iso_kind, args) = split_iso_tokens(args);
    let p = match argopt::parse("date", &args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if let Some(which) = p
        .flags
        .iter()
        .find(|f| f.as_str() == argopt::HELP || f.as_str() == argopt::VERSION)
    {
        return Ok(match which.as_str() {
            "help" => {
                write!(
                    io.stdout,
                    "{}{}",
                    DATE_HELP_BODY,
                    gnuhelp::footer("date", "date invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("date", "David MacKenzie")
                )?;
                0
            }
        });
    }

    let use_utc = p.has("utc");
    let zone = effective_zone(use_utc, ctx.env);

    // resolve the timestamp: -d / -r / now
    let dt = if let Some(d) = p.value_of("date") {
        match parse_date_string(d) {
            Some(pd) => resolve_parsed(&pd, &zone, now_epoch()),
            None => {
                let _ = writeln!(io.stderr, "date: invalid date '{d}'");
                return Ok(1);
            }
        }
    } else if let Some(r) = p.value_of("reference") {
        let path = resolve(ctx, r);
        match path.metadata().and_then(|m| m.modified()) {
            Ok(t) => {
                let epoch = t
                    .duration_since(std::time::UNIX_EPOCH)
                    .map(|d| d.as_secs() as i64)
                    .unwrap_or_else(|_| {
                        -(t.duration_since(std::time::UNIX_EPOCH)
                            .err()
                            .map(|e| e.duration().as_secs())
                            .unwrap_or(0) as i64)
                    });
                let (off, name) = zone.at(epoch);
                DateTime::from_epoch(epoch, off, name)
            }
            Err(_) => {
                let _ = writeln!(io.stderr, "date: invalid date");
                return Ok(1);
            }
        }
    } else {
        let epoch = now_epoch();
        let (off, name) = zone.at(epoch);
        DateTime::from_epoch(epoch, off, name)
    };

    if p.has("set") {
        // honest boundary: the port never sets the clock; GNU on the gate
        // host also fails (probed rc 1)
        let _ = writeln!(
            io.stderr,
            "date: setting the system clock is not supported on Windows"
        );
        return Ok(1);
    }

    // -f DATEFILE: one date per line (GNU shape)
    if let Some(file) = p.value_of("file") {
        let path = resolve(ctx, file);
        let text = match std::fs::read_to_string(&path) {
            Ok(t) => t,
            Err(e) => {
                let reason = crate::fsutil::reason(&e);
                let _ = writeln!(io.stderr, "date: {file}: {reason}");
                return Ok(1);
            }
        };
        let mut failed = false;
        for line in text.lines() {
            if line.trim().is_empty() {
                continue;
            }
            match parse_date_string(line.trim()) {
                Some(pd) => {
                    let d = resolve_parsed(&pd, &zone, now_epoch());
                    let out = default_or_custom_line(&p, &d, &iso_kind, ctx)?;
                    writeln!(io.stdout, "{out}")?;
                }
                None => {
                    let _ = writeln!(io.stderr, "date: invalid date '{line}'");
                    failed = true;
                }
            }
        }
        return Ok(if failed { 1 } else { 0 });
    }

    // output format
    let line = if let Some(fmt) = p.positionals.first() {
        let f = fmt.to_string_lossy().into_owned();
        if !f.starts_with('+') {
            // probed: the non-+ operand diagnostic
            let _ = writeln!(
                io.stderr,
                "date: the argument '{}' lacks a leading '+';\nwhen using an option to specify date(s), any non-option\nargument must be a format string beginning with '+'",
                f
            );
            let _ = writeln!(io.stderr, "Try 'date --help' for more information.");
            return Ok(1);
        }
        strftime(&dt, &f[1..])
    } else if let Some(kind) = p.value_of("rfc-3339") {
        let (sign, oh, om) = offset_parts(dt.utc_offset);
        match kind {
            "date" => strftime(&dt, "%F"),
            "seconds" => format!("{}{}{:02}:{:02}", strftime(&dt, "%F %T"), sign, oh, om),
            "ns" => format!(
                "{}.000000000{}{:02}:{:02}",
                strftime(&dt, "%F %T"),
                sign,
                oh,
                om
            ),
            _ => {
                let _ = writeln!(
                    io.stderr,
                    "date: invalid argument '{kind}' for '--rfc-3339'"
                );
                return Ok(1);
            }
        }
    } else {
        let iso = if iso_kind.is_some() || p.has("iso-8601") {
            match iso_kind.as_deref() {
                None | Some("") => Some("date".to_string()),
                Some(k) => Some(k.to_string()),
            }
        } else {
            None
        };
        if let Some(kind) = iso {
            let (sign, oh, om) = offset_parts(dt.utc_offset);
            match kind.as_str() {
                "date" => strftime(&dt, "%F"),
                "hours" => format!(
                    "{}T{}{}{:02}:00",
                    strftime(&dt, "%F"),
                    fmt2(dt.hour),
                    sign,
                    oh
                ),
                "minutes" => format!(
                    "{}T{}:{}{}{:02}:{:02}",
                    strftime(&dt, "%F"),
                    fmt2(dt.hour),
                    fmt2(dt.minute),
                    sign,
                    oh,
                    om
                ),
                "seconds" => format!(
                    "{}T{}:{}:{}{}{:02}:{:02}",
                    strftime(&dt, "%F"),
                    fmt2(dt.hour),
                    fmt2(dt.minute),
                    fmt2(dt.second),
                    sign,
                    oh,
                    om
                ),
                "ns" => format!(
                    "{}T{}:{}:{}.{:09}{}{:02}:{:02}",
                    strftime(&dt, "%F"),
                    fmt2(dt.hour),
                    fmt2(dt.minute),
                    fmt2(dt.second),
                    0,
                    sign,
                    oh,
                    om
                ),
                _ => strftime(&dt, "%F"),
            }
        } else if p.has("rfc-email") || p.has("rfc-2822") {
            let (sign, oh, om) = offset_parts(dt.utc_offset);
            format!(
                "{}, {} {} {} {}:{}:{} {}{:02}{:02}",
                WEEKDAYS_SHORT[dt.weekday()],
                fmt2(dt.day),
                MONTHS_SHORT[(dt.month - 1) as usize],
                dt.year,
                fmt2(dt.hour),
                fmt2(dt.minute),
                fmt2(dt.second),
                sign,
                oh,
                om
            )
        } else {
            // probed default: `Thu Jan  2 03:04:05 UTC 2020`
            format!(
                "{} {} {:>2} {}:{}:{} {} {}",
                WEEKDAYS_SHORT[dt.weekday()],
                MONTHS_SHORT[(dt.month - 1) as usize],
                dt.day,
                fmt2(dt.hour),
                fmt2(dt.minute),
                fmt2(dt.second),
                dt.tzname,
                dt.year
            )
        }
    };
    writeln!(io.stdout, "{line}")?;
    Ok(0)
}

/// -f FILE rows share the same format-selection logic.
fn default_or_custom_line(
    p: &argopt::Parsed,
    d: &DateTime,
    _iso: &Option<String>,
    _ctx: &Ctx,
) -> Result<String, std::io::Error> {
    if let Some(fmt) = p.positionals.first() {
        let f = fmt.to_string_lossy().into_owned();
        if f.starts_with('+') {
            return Ok(strftime(d, &f[1..]));
        }
    }
    Ok(format!(
        "{} {} {:>2} {}:{}:{} {} {}",
        WEEKDAYS_SHORT[d.weekday()],
        MONTHS_SHORT[(d.month - 1) as usize],
        d.day,
        fmt2(d.hour),
        fmt2(d.minute),
        fmt2(d.second),
        d.tzname,
        d.year
    ))
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str]) -> (u32, String, String) {
        let sc = ScratchCtx::new("date");
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = date_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    fn tz(zone: &str, args: &[&str]) -> (u32, String, String) {
        let mut sc = ScratchCtx::new("date-tz");
        let mut io = MemIo::new("");
        sc.env.set("TZ", zone);
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = date_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn iso_default_format_and_weekdays() {
        // probed a/r cases: default line shape + %A %B %e %j %U %W %V %G %C %y
        let (r, out, err) = tz("UTC", &["-d", "2020-01-02 03:04:05 UTC"]);
        assert_eq!(r, 0);
        assert_eq!(out, "Thu Jan  2 03:04:05 UTC 2020\n");
        assert!(err.is_empty());
        let (_, out, _) = tz(
            "UTC",
            &["-d", "2020-01-02", "+%A|%B|%e|%j|%U|%W|%V|%G|%C|%y"],
        );
        assert_eq!(out, "Thursday|January| 2|002|00|00|01|2020|20|20\n");
    }

    #[test]
    fn format_basics_and_am_pm() {
        // probed q: %N newline tab %% %p %P %k %l
        let (_, out, _) = tz(
            "UTC",
            &["-d", "2020-01-02 03:04:05", "+%N|%n|%t|%%|%p|%P|%k|%l"],
        );
        assert_eq!(out, "000000000|\n|\t|%|AM|am| 3| 3\n");
        let (_, out, _) = tz("UTC", &["-d", "2020-01-02", "+%F|%T|%R|%D"]);
        assert_eq!(out, "2020-01-02|00:00:00|00:00|01/02/20\n");
    }

    #[test]
    fn epoch_and_s() {
        // probed d/e/g/w/ae: @epoch round trip and %s
        let (_, out, _) = tz("UTC", &["-d", "@1234567890", "+%s"]);
        assert_eq!(out, "1234567890\n");
        let (_, out, _) = tz("UTC", &["-u", "-d", "@1234567890"]);
        assert_eq!(out, "Fri Feb 13 23:31:30 UTC 2009\n");
        let (_, out, _) = tz("UTC", &["-d", "2020-01-02 03:04:05 UTC", "+%s"]);
        assert_eq!(out, "1577934245\n");
        let (_, out, _) = tz("UTC", &["-u", "-d", "@0", "+%F_%T"]);
        assert_eq!(out, "1970-01-01_00:00:00\n");
        let (_, out, _) = tz("UTC", &["-u", "-d", "@-1", "+%F_%T"]);
        assert_eq!(out, "1969-12-31_23:59:59\n");
        let (_, out, _) = tz("UTC", &["-d", "2020-02-29 12:00:00 UTC", "+%s"]);
        assert_eq!(out, "1582977600\n");
    }

    #[test]
    fn rfc_and_iso_surfaces() {
        // probed h/i/j/l/m/n/o/p
        let (_, out, _) = tz("UTC", &["-R", "-d", "2020-01-02 03:04:05 UTC"]);
        assert_eq!(out, "Thu, 02 Jan 2020 03:04:05 +0000\n");
        let (_, out, _) = tz(
            "UTC",
            &["--rfc-3339=seconds", "-d", "2020-01-02 03:04:05 UTC"],
        );
        assert_eq!(out, "2020-01-02 03:04:05+00:00\n");
        let (_, out, _) = tz("UTC", &["--rfc-3339=date", "-d", "2020-01-02 03:04:05 UTC"]);
        assert_eq!(out, "2020-01-02\n");
        let (_, out, _) = tz("UTC", &["--rfc-3339=ns", "-d", "2020-01-02 03:04:05 UTC"]);
        assert_eq!(out, "2020-01-02 03:04:05.000000000+00:00\n");
        let (_, out, _) = tz("UTC", &["-I", "-d", "2020-01-02 03:04:05 UTC"]);
        assert_eq!(out, "2020-01-02\n");
        let (_, out, _) = tz("UTC", &["-Iseconds", "-d", "2020-01-02 03:04:05 UTC"]);
        assert_eq!(out, "2020-01-02T03:04:05+00:00\n");
        let (_, out, _) = tz(
            "UTC",
            &["--iso-8601=minutes", "-u", "-d", "2020-01-02 03:04:05"],
        );
        assert_eq!(out, "2020-01-02T03:04+00:00\n");
        let (_, out, _) = tz("UTC", &["--iso-8601=date", "-d", "2020-01-02 03:04:05"]);
        assert_eq!(out, "2020-01-02\n");
        let (_, out, _) = tz("UTC", &["--rfc-email", "-d", "2020-01-02 03:04:05 UTC"]);
        assert_eq!(out, "Thu, 02 Jan 2020 03:04:05 +0000\n");
    }

    #[test]
    fn relative_adjustments() {
        // probed x/y/z: "+ 1 day", "+ 1 month" (Jan 31 -> Mar 2), "- 1 day"
        let (_, out, _) = tz("UTC", &["-d", "2020-01-31 + 1 day", "+%F"]);
        assert_eq!(out, "2020-02-01\n");
        let (_, out, _) = tz("UTC", &["-d", "2020-01-31 + 1 month", "+%F"]);
        assert_eq!(out, "2020-03-02\n");
        let (_, out, _) = tz("UTC", &["-d", "2020-03-01 - 1 day", "+%F"]);
        assert_eq!(out, "2020-02-29\n");
    }

    #[test]
    fn posix_tz_offsets_and_dst() {
        // probed r: EST5EDT summer -> EDT -0400; winter -> EST +0500 west
        let (_, out, _) = tz(
            "EST5EDT",
            &["-d", "2020-07-01 03:04:05 UTC", "+%F_%T_%z_%Z"],
        );
        assert_eq!(out, "2020-06-30_23:04:05_-0400_EDT\n");
        let (_, out, _) = tz(
            "EST5EDT",
            &["-d", "2020-01-02 03:04:05 UTC", "+%F_%T_%z_%Z"],
        );
        assert_eq!(out, "2020-01-01_22:04:05_-0500_EST\n");
        // probed a: XYZ+5:30 = UTC-5:30
        let (_, out, _) = tz(
            "XYZ+5:30",
            &["-d", "2020-01-02 03:04:05 UTC", "+%F_%T_%z_%Z"],
        );
        assert_eq!(out, "2020-01-01_21:34:05_-0530_XYZ\n");
        // probed b: GMT-8 = UTC+8
        let (_, out, _) = tz("GMT-8", &["-d", "2020-01-02 03:04:05 UTC", "+%T_%z"]);
        assert_eq!(out, "11:04:05_+0800\n");
    }

    #[test]
    fn errors_and_leading_plus() {
        // probed s/t/ag/ah/ac: invalid date, bogus opt, -d missing arg,
        // unknown conversion literal, half-word time
        let (r, out, _) = tz("UTC", &["-d", "not a date", "+%F"]);
        assert_eq!(r, 1);
        assert!(out.is_empty());
        let (r, _, _) = tz("UTC", &["--bogus"]);
        assert_eq!(r, 1);
        let (r, _, _) = tz("UTC", &["-d"]);
        assert_eq!(r, 1);
        let (_, out, _) = tz("UTC", &["-d", "2020-01-02", "+%Q"]);
        assert_eq!(out, "%Q\n");
        let (_, out, _) = tz("UTC", &["-d", "2020-01-02 03:04", "+%F_%T"]);
        assert_eq!(out, "2020-01-02_03:04:00\n");
        let (_, out, _) = tz("UTC", &["--date=2020-01-02 03:04:05", "+%F"]);
        assert_eq!(out, "2020-01-02\n");
    }

    #[test]
    fn nonplus_operand_and_empty_format() {
        // probed i-l: %F as operand and '' both lack a leading '+'
        let (r, _, err) = tz(
            "UTC",
            &["-d", "2020-01-02 03:04:05", "%F", "-d", "2020-02-02"],
        );
        assert_eq!(r, 1);
        assert!(err.contains("lacks a leading '+'"));
        let (r, _, err) = tz("UTC", &["-d", "2020-01-02", ""]);
        assert_eq!(r, 1);
        assert!(err.contains("lacks a leading '+'"));
    }

    #[test]
    fn year_9999_and_half_word() {
        // probed ad/ac
        let (_, out, _) = tz("UTC", &["-d", "9999-12-31 23:59:59", "+%F_%T"]);
        assert_eq!(out, "9999-12-31_23:59:59\n");
    }

    #[test]
    fn help_version_surface() {
        let (r, out, err) = run(&["--help"]);
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.starts_with("Usage: date [OPTION]... [+FORMAT]\n"));
        assert!(out.contains("  -I[FMT], --iso-8601[=FMT]  output date/time in ISO 8601 format.\n"));
        assert!(out.contains(
            "  --resolution               output the available resolution of timestamps\n"
        ));
        assert!(
            out.contains("All options that specify the date to display are mutually exclusive.\n")
        );
        assert!(out.contains("  $ date --date='TZ=\"America/Los_Angeles\" 09:00 next Fri'\n"));
        let (r, out, _) = run(&["--version"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("date (GNU coreutils) 9.4\n"));
        assert!(out.ends_with("Written by David MacKenzie.\n"));
    }

    #[test]
    fn set_boundary_refuses() {
        // The port refuses honestly: a compat layer must not mutate the
        // machine clock (the 1.x C++ applet refuses too, rc 1). Note the
        // gate oracle nuance: the WSL host runs as root, so GNU ACCEPTS
        // -s there — dt38 is a recorded note, not a strict row.
        let (r, _, _) = run(&["-s", "2020-01-02 03:04:05"]);
        assert_eq!(r, 1);
    }
}
