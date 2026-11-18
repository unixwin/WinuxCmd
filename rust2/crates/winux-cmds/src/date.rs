//! `date` — GNU coreutils date.c (9.4) semantics, probed byte-for-byte
//! against WSL coreutils 9.4 (rust2/target/probe/w5-date.sh, w5-tz.sh).
//!
//! Deterministic core (everything the gate can compare):
//! * `-d DATE` parse subset — ISO `YYYY-MM-DD[ HH:MM[:SS]]`, the 'T'
//!   date/time separator, `@EPOCH` (negative allowed), explicit
//!   `UTC`/`GMT`/`Z` suffix; bare wall times are interpreted in the
//!   effective zone (TZ env / system local).
//! * Relative forms anchored on now (parse-datetime.y `rel`/`hybrid`):
//!   `+N unit`, `-N unit`, bare `N unit`, chained signed groups
//!   (`+1 day +2 hours`), `<N unit> ago` (negates only the group it
//!   follows), `now`, `today`, `tomorrow`, `yesterday`,
//!   `next|last|this UNIT`, and `<base> [+-] N unit` on an explicit date
//!   (both `+ 1 day` and glued `+1 day`). Month/year are calendar units —
//!   probed: Jan 31 + 1 month -> Mar 2, overflow rolls; Mar 1 - 1 day ->
//!   Feb 29 in 2020. Units: second/minute/hour/day/fortnight/week/month/
//!   year.
//! * Time-of-day-only operands (parse-datetime.y `digits_to_date_time` and
//!   `time`/`iso_8601_time`): `H`, `HH`, `HHMM`, `H:M[:S]`, optional
//!   `am`/`pm`, and a trailing military-zone letter (`T` = UTC-7, so probed
//!   `1024t` = 17:24 UTC; `Z` = UTC). The civil date comes from now.
//! * NOT part of the port's subset (documented boundary, tracked by the
//!   date-parse-datetime issue): weekday/month names (`monday`,
//!   `next monday`, `Jan 2 2020`), combined day+time operands
//!   (`tomorrow 10:00`), `TZ="..."` prefixes, nanoseconds in the input,
//!   ordinal/`YYYYMMDD` numeric date forms, and named zones beyond
//!   UTC/GMT/Z. Those stay `invalid date` rather than being approximated.
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
        short: None,
        long: "debug",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "universal",
        takes_value: false,
    },
    OptSpec {
        // obsolete alias of --utc (GNU accepts it; issue #1073)
        short: None,
        long: "uct",
        takes_value: false,
    },
    OptSpec {
        // obsolete alias of -R/--rfc-email (GNU accepts it; issue #1073)
        short: None,
        long: "rfc-822",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "resolution",
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
    /// sub-second nanoseconds (FILETIME carries 100 ns units; -d/-r
    /// operands usually land on 0)
    pub ns: u32,
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
            ns: 0,
        }
    }

    /// from_epoch with an explicit sub-second part (the --resolution
    /// instant and file mtimes carry real 100 ns ticks).
    pub fn from_epoch_ns(epoch: i64, offset: i64, tzname: String, ns: u32) -> Self {
        let mut dt = Self::from_epoch(epoch, offset, tzname);
        dt.ns = ns;
        dt
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
        // rc 2 = TIME_ZONE_ID_DAYLIGHT, rc 1 = TIME_ZONE_ID_STANDARD, 0 = UNKNOWN
        let (bias, zone_name) = if rc == 2 {
            (tzi.bias + tzi.daylight_bias, name(&tzi.daylight_name))
        } else if rc == 1 {
            (tzi.bias + tzi.standard_bias, name(&tzi.standard_name))
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
    ns: u32,
    /// @EPOCH (wall fields are UTC) or an explicit UTC/GMT/Z/numeric suffix
    force_utc: bool,
    /// The operand is anchored on `now` (`+N unit`, `tomorrow`, `now + …`)
    from_now: bool,
    rels: Vec<RelAmount>,
    /// Time-of-day-only operand (`HH`, `HHMM`, `HH:MM[:SS][am|pm]`): the
    /// civil date comes from `now` at resolve time.
    time_only: Option<(i64, i64, i64)>,
    /// Explicit zone offset in seconds east of UTC (e.g. military zone
    /// letter `T` = -7h, `Z` = 0, or `±hhmm`). `None` uses the effective
    /// zone (including military `J`).
    zone_override: Option<i64>,
}

impl ParsedDate {
    /// An operand whose base is the current instant (parse-datetime.y
    /// `rel` with no date part).
    fn anchored_on_now(rels: Vec<RelAmount>) -> Self {
        ParsedDate {
            y: 1970,
            m: 1,
            d: 1,
            hh: 0,
            mm: 0,
            ss: 0,
            ns: 0,
            force_utc: true,
            from_now: true,
            rels,
            time_only: None,
            zone_override: None,
        }
    }
}

/// Unit word -> (exact seconds, calendar kind); parse-datetime.y
/// time_units_table plus the `fortnight` alias.
fn rel_unit(word: &str) -> Option<(i64, CalUnit)> {
    let lower = word.to_ascii_lowercase();
    match lower.as_str() {
        "second" | "seconds" | "sec" | "secs" => Some((1, CalUnit::None)),
        "minute" | "minutes" | "min" | "mins" => Some((60, CalUnit::None)),
        "hour" | "hours" => Some((3600, CalUnit::None)),
        "day" | "days" => Some((86400, CalUnit::Day)),
        "fortnight" | "fortnights" => Some((14 * 86400, CalUnit::Day)),
        "week" | "weeks" => Some((7 * 86400, CalUnit::Week)),
        "month" | "months" => Some((0, CalUnit::Month)),
        "year" | "years" => Some((0, CalUnit::Year)),
        _ => None,
    }
}

/// Military time-zone letter (parse-datetime.y military_table).
/// `J` is the *local* military zone (`{ "J", 'J', 0 }` + `item: 'J'`),
/// added in gnulib commit 9cde39f8 / coreutils 9.2: returns `Some(None)`
/// so callers use the effective zone's offset, while A-I/K-Z return
/// `Some(Some(seconds_east))`.
fn military_offset(letter: u8) -> Option<Option<i64>> {
    let hours = match letter.to_ascii_uppercase() {
        b'A' => 1,
        b'B' => 2,
        b'C' => 3,
        b'D' => 4,
        b'E' => 5,
        b'F' => 6,
        b'G' => 7,
        b'H' => 8,
        b'I' => 9,
        b'J' => return Some(None),
        b'K' => 10,
        b'L' => 11,
        b'M' => 12,
        b'N' => -1,
        b'O' => -2,
        b'P' => -3,
        b'Q' => -4,
        b'R' => -5,
        b'S' => -6,
        b'T' => -7,
        b'U' => -8,
        b'V' => -9,
        b'W' => -10,
        b'X' => -11,
        b'Y' => -12,
        b'Z' => 0,
        _ => return None,
    };
    Some(Some(hours * 3600))
}

fn parse_rel_groups(s: &str) -> Option<Vec<RelAmount>> {
    let toks: Vec<&str> = s.split_whitespace().collect();
    let mut out = Vec::new();
    let mut i = 0usize;
    while i < toks.len() {
        // A sign may be glued to the number ("+1") or a separate token
        // ("+ 1"), matching parse-datetime.y's tSNUMBER rule.
        let mut sign = 1i64;
        let mut tok = toks[i];
        if tok == "+" || tok == "-" {
            if tok == "-" {
                sign = -1;
            }
            i += 1;
            if i >= toks.len() {
                return None;
            }
            tok = toks[i];
        }
        let n: i64 = tok.parse().ok()?;
        i += 1;
        if i >= toks.len() {
            return None;
        }
        let (unit_seconds, calendar) = rel_unit(toks[i])?;
        i += 1;
        let mut count = sign.checked_mul(n)?;
        if i < toks.len() {
            if toks[i].eq_ignore_ascii_case("ago") {
                count = count.checked_neg()?;
                i += 1;
            } else if toks[i].eq_ignore_ascii_case("hence") {
                i += 1;
            }
        }
        out.push(RelAmount {
            count,
            unit_seconds,
            calendar,
        });
    }
    if out.is_empty() {
        None
    } else {
        Some(out)
    }
}

/// Row of `parse-datetime.y` `relative_time_table` for today/tomorrow/
/// yesterday (all three keep the current time of day).
fn keyword_day(s: &str) -> Option<i64> {
    match s.to_ascii_lowercase().as_str() {
        "today" => Some(0),
        "tomorrow" => Some(1),
        "yesterday" => Some(-1),
        _ => None,
    }
}

/// `next UNIT` / `last UNIT` / `this UNIT` (parse-datetime.y
/// relative_time_table's tDAY_SHIFT arm).
fn parse_named_relative(s: &str) -> Option<Vec<RelAmount>> {
    let mut it = s.split_whitespace();
    let head = it.next()?;
    let unit = it.next()?;
    if it.next().is_some() {
        return None;
    }
    let count = match head.to_ascii_lowercase().as_str() {
        "next" => 1,
        "last" => -1,
        "this" => 0,
        _ => return None,
    };
    let (unit_seconds, calendar) = rel_unit(unit)?;
    Some(vec![RelAmount {
        count,
        unit_seconds,
        calendar,
    }])
}

/// Strip optional trailing meridian (`am`/`pm`/`a.m.`/`p.m.`, case-insensitive)
/// from a time expression. Returns `(rest, Some(is_pm))` if found, or
/// `(s, None)` otherwise.
fn strip_meridian(s: &str) -> (&str, Option<bool>) {
    let trimmed = s.trim_end();
    let lower = trimmed.to_ascii_lowercase();
    for (suffix, is_pm) in [("a.m.", false), ("p.m.", true), ("am", false), ("pm", true)] {
        if lower.len() > suffix.len() && lower.ends_with(suffix) {
            let before = trimmed[..trimmed.len() - suffix.len()].trim_end();
            // Meridian must follow a digit (e.g. `3pm`, `03:04:05 PM`, `03:04:05 P.M.`).
            if before.bytes().last().is_some_and(|b| b.is_ascii_digit()) {
                return (before, Some(is_pm));
            }
        }
    }
    (trimmed, None)
}

/// Parse a time expression (`H:M[:S[.frac]]`, or if `allow_bare_digits`
/// is true, `H`/`HH`/`HHMM`), with optional meridian (`am`/`pm`/`a.m.`/`p.m.`)
/// and optional trailing military-zone letter (`1024t`, `1024j`, `10:24:30 T`,
/// `03:04:05Z`).
/// Returns `((hh, mm, ss, ns), explicit_zone_seen, zone_override)`.
fn parse_time_expr(
    s: &str,
    allow_bare_digits: bool,
) -> Option<((i64, i64, i64, u32), bool, Option<i64>)> {
    let (mut body, meridian) = strip_meridian(s.trim());
    let mut zone_seen = false;
    let mut zone_override: Option<i64> = None;

    if meridian.is_none() && body.len() >= 2 {
        let last = body.as_bytes()[body.len() - 1];
        if last.is_ascii_alphabetic() {
            let before = body[..body.len() - 1].trim_end();
            if before.bytes().last().is_some_and(|b| b.is_ascii_digit()) {
                zone_override = military_offset(last)?;
                zone_seen = true;
                body = before;
            }
        }
    }

    if body.is_empty()
        || !body
            .bytes()
            .all(|b| b.is_ascii_digit() || b == b':' || b == b'.')
    {
        return None;
    }

    let (mut mm, mut ss, mut ns) = (0i64, 0i64, 0u32);
    let mut hh: i64;
    if body.contains(':') {
        let parts: Vec<&str> = body.split(':').collect();
        if parts.len() < 2 || parts.len() > 3 {
            return None;
        }
        if parts[0].is_empty() || !parts[0].bytes().all(|b| b.is_ascii_digit()) {
            return None;
        }
        if parts[1].is_empty() || !parts[1].bytes().all(|b| b.is_ascii_digit()) {
            return None;
        }
        hh = parts[0].parse().ok()?;
        mm = parts[1].parse().ok()?;
        if parts.len() == 3 {
            let sec_str = parts[2];
            if let Some((whole, frac)) = sec_str.split_once('.') {
                if whole.is_empty()
                    || !whole.bytes().all(|b| b.is_ascii_digit())
                    || frac.is_empty()
                    || !frac.bytes().all(|b| b.is_ascii_digit())
                {
                    return None;
                }
                ss = whole.parse().ok()?;
                let mut acc = 0u32;
                let fb = frac.as_bytes();
                for i in 0..9 {
                    let d = if i < fb.len() {
                        (fb[i] - b'0') as u32
                    } else {
                        0
                    };
                    acc = acc * 10 + d;
                }
                ns = acc;
            } else {
                if sec_str.is_empty() || !sec_str.bytes().all(|b| b.is_ascii_digit()) {
                    return None;
                }
                ss = sec_str.parse().ok()?;
            }
        }
    } else {
        if !allow_bare_digits || body.contains('.') || body.len() > 4 {
            return None;
        }
        let v: i64 = body.parse().ok()?;
        if body.len() <= 2 {
            hh = v;
        } else {
            hh = v / 100;
            mm = v % 100;
        }
    }

    if let Some(pm) = meridian {
        if !(1..=12).contains(&hh) {
            return None;
        }
        hh = match (pm, hh) {
            (false, 12) => 0,
            (true, 12) => 12,
            (false, h) => h,
            (true, h) => h + 12,
        };
    }
    if hh > 23 || mm > 59 || ss > 60 {
        return None;
    }
    Some(((hh, mm, ss, ns), zone_seen, zone_override))
}

/// Parse a `YYYY-M-D`, `YYYY/M/D`, or `YYYYMMDD` date token.
fn parse_date_token(tok: &str) -> Option<(i64, i64, i64)> {
    if tok.len() == 8 && tok.bytes().all(|b| b.is_ascii_digit()) {
        let y: i64 = tok[0..4].parse().ok()?;
        let m: i64 = tok[4..6].parse().ok()?;
        let d: i64 = tok[6..8].parse().ok()?;
        return Some((y, m, d));
    }
    let sep = if tok.contains('-') && !tok.contains('/') {
        '-'
    } else if tok.contains('/') && !tok.contains('-') {
        '/'
    } else {
        return None;
    };
    let mut parts = tok.split(sep);
    let ys = parts.next()?;
    let ms = parts.next()?;
    let ds = parts.next()?;
    if parts.next().is_some() {
        return None;
    }
    if ys.len() != 4 || !(1..=2).contains(&ms.len()) || !(1..=2).contains(&ds.len()) {
        return None;
    }
    if !ys.bytes().all(|b| b.is_ascii_digit())
        || !ms.bytes().all(|b| b.is_ascii_digit())
        || !ds.bytes().all(|b| b.is_ascii_digit())
    {
        return None;
    }
    let y: i64 = ys.parse().ok()?;
    let m: i64 = ms.parse().ok()?;
    let d: i64 = ds.parse().ok()?;
    Some((y, m, d))
}

/// Strip an optional trailing explicit zone (`UTC`, `GMT`, `UT`, `±hhmm`,
/// `±hh:mm`) from a base date+time string. Returns `(trimmed, Some(offset_sec))`
/// or `(s, None)`.
fn strip_trailing_zone(s: &str) -> (&str, Option<i64>) {
    let s = s.trim_end();
    let lower = s.to_ascii_lowercase();
    for suffix in [" utc", " gmt", " ut"] {
        if lower.ends_with(suffix) {
            return (s[..s.len() - suffix.len()].trim_end(), Some(0));
        }
    }
    let b = s.as_bytes();
    // ±hhmm
    if b.len() >= 6 {
        let pos = b.len() - 5;
        if (b[pos] == b'+' || b[pos] == b'-')
            && (b[pos - 1].is_ascii_digit() || b[pos - 1].is_ascii_whitespace())
            && b[pos + 1..].iter().all(|c| c.is_ascii_digit())
        {
            if let (Ok(hh), Ok(mm)) = (
                s[pos + 1..pos + 3].parse::<i64>(),
                s[pos + 3..].parse::<i64>(),
            ) {
                if hh <= 23 && mm <= 59 {
                    let sign = if b[pos] == b'-' { -1 } else { 1 };
                    return (s[..pos].trim_end(), Some(sign * (hh * 3600 + mm * 60)));
                }
            }
        }
    }
    // ±hh:mm
    if b.len() >= 7 {
        let pos = b.len() - 6;
        if (b[pos] == b'+' || b[pos] == b'-')
            && (b[pos - 1].is_ascii_digit() || b[pos - 1].is_ascii_whitespace())
            && b[pos + 3] == b':'
            && b[pos + 1..pos + 3].iter().all(|c| c.is_ascii_digit())
            && b[pos + 4..].iter().all(|c| c.is_ascii_digit())
        {
            if let (Ok(hh), Ok(mm)) = (
                s[pos + 1..pos + 3].parse::<i64>(),
                s[pos + 4..].parse::<i64>(),
            ) {
                if hh <= 23 && mm <= 59 {
                    let sign = if b[pos] == b'-' { -1 } else { 1 };
                    return (s[..pos].trim_end(), Some(sign * (hh * 3600 + mm * 60)));
                }
            }
        }
    }
    (s, None)
}

/// Split `<base> <relative groups>` at the first whitespace whose suffix is a
/// well-formed relative list. Used for hybrid forms like
/// `2020-01-02 03:04:05 UTC +1 day` (the sign may be glued to the number).
fn split_base_rels(s: &str) -> Option<(&str, Vec<RelAmount>)> {
    for (pos, ch) in s.char_indices() {
        if !ch.is_ascii_whitespace() {
            continue;
        }
        let head = s[..pos].trim_end();
        let tail = s[pos + 1..].trim_start();
        if head.is_empty() {
            continue;
        }
        if let Some(rels) = parse_rel_groups(tail) {
            return Some((head, rels));
        }
    }
    None
}

fn parse_date_string(s: &str) -> Option<ParsedDate> {
    let s = s.trim();
    // GNU: an empty -d operand is today at midnight (parse-datetime.y
    // accepts the empty grammar and 00:00 is the default time).
    if s.is_empty() {
        return Some(ParsedDate {
            y: 1970,
            m: 1,
            d: 1,
            hh: 0,
            mm: 0,
            ss: 0,
            ns: 0,
            force_utc: true,
            from_now: false,
            rels: Vec::new(),
            time_only: Some((0, 0, 0)),
            zone_override: None,
        });
    }
    // @EPOCH (probed @0/@1234567890/@-1)
    let parse_epoch = |expr: &str, rels: Vec<RelAmount>| -> Option<ParsedDate> {
        let rest = expr.strip_prefix('@')?.trim();
        let (epoch_str, ns) = if let Some((whole, frac)) = rest.split_once('.') {
            if frac.is_empty() || !frac.bytes().all(|b| b.is_ascii_digit()) {
                return None;
            }
            let mut acc = 0u32;
            let fb = frac.as_bytes();
            for i in 0..9 {
                let d = if i < fb.len() {
                    (fb[i] - b'0') as u32
                } else {
                    0
                };
                acc = acc * 10 + d;
            }
            (whole, acc)
        } else {
            (rest, 0u32)
        };
        let epoch: i64 = epoch_str.parse().ok()?;
        let days = epoch.div_euclid(86400);
        let secs = epoch.rem_euclid(86400);
        let (y, m, d) = civil_from_days(days);
        Some(ParsedDate {
            y,
            m,
            d,
            hh: secs / 3600,
            mm: (secs % 3600) / 60,
            ss: secs % 60,
            ns,
            force_utc: true,
            from_now: false,
            rels,
            time_only: None,
            zone_override: Some(0),
        })
    };
    if s.starts_with('@') {
        if let Some(pd) = parse_epoch(s, Vec::new()) {
            return Some(pd);
        }
    }
    if s.eq_ignore_ascii_case("now") {
        return Some(ParsedDate::anchored_on_now(Vec::new()));
    }
    // A lone relative expression: "+1 day", "1 day", "+1 day +2 hours", "1 day 2 hours ago".
    if let Some(rels) = parse_rel_groups(s) {
        return Some(ParsedDate::anchored_on_now(rels));
    }
    if let Some(named) = parse_named_relative(s) {
        return Some(ParsedDate::anchored_on_now(named));
    }
    if let Some(days) = keyword_day(s) {
        return Some(ParsedDate::anchored_on_now(vec![RelAmount {
            count: days,
            unit_seconds: 86400,
            calendar: CalUnit::Day,
        }]));
    }
    // time-of-day only (digits_to_date_time / iso time rules)
    if let Some(((hh, mm, ss, ns), zone_seen, zone_override)) = parse_time_expr(s, true) {
        return Some(ParsedDate {
            y: 1970,
            m: 1,
            d: 1,
            hh,
            mm,
            ss,
            ns,
            force_utc: zone_seen && zone_override.is_some(),
            from_now: false,
            rels: Vec::new(),
            time_only: Some((hh, mm, ss)),
            zone_override,
        });
    }
    // "<base> [+-] N unit[ ...]": the relative tail may be glued to its sign
    // ("… UTC +1 day") or separated ("… UTC + 1 day"), so split at the
    // first space whose suffix parses as relative groups (parse-datetime.y
    // hybrid: digits_to_date_time then relunit).
    let mut base = s;
    let mut rels: Vec<RelAmount> = Vec::new();
    if let Some((b, r)) = split_base_rels(s) {
        base = b;
        rels = r;
        if base.starts_with('@') {
            return parse_epoch(base, rels);
        }
        // base keywords share the now anchor with any tail groups
        if base.eq_ignore_ascii_case("now") {
            return Some(ParsedDate::anchored_on_now(rels));
        }
        if let Some(days) = keyword_day(base) {
            let mut all = vec![RelAmount {
                count: days,
                unit_seconds: 86400,
                calendar: CalUnit::Day,
            }];
            all.extend(rels);
            return Some(ParsedDate::anchored_on_now(all));
        }
        if let Some(named) = parse_named_relative(base) {
            let mut all = named;
            all.extend(rels);
            return Some(ParsedDate::anchored_on_now(all));
        }
        if let Some(((hh, mm, ss, ns), zone_seen, zone_override)) = parse_time_expr(base, true) {
            return Some(ParsedDate {
                y: 1970,
                m: 1,
                d: 1,
                hh,
                mm,
                ss,
                ns,
                force_utc: zone_seen && zone_override.is_some(),
                from_now: false,
                rels,
                time_only: Some((hh, mm, ss)),
                zone_override,
            });
        }
    }

    // Explicit trailing zone suffix ("... UTC", "... GMT", "... +08:00")
    let (base_no_zone, mut zone_override) = strip_trailing_zone(base);

    // Tokenize `base_no_zone`, splitting ISO `YYYY-M-D[T|t]HH...` per token so
    // any trailing zone or meridian stays intact, and accept the date token
    // in either first or last position ("2026-01-01 10:00 PM" and "10:00 PM 2026-01-01").
    let mut tokens: Vec<&str> = Vec::new();
    for raw_tok in base_no_zone.split_whitespace() {
        if let Some(idx) = raw_tok.find(|c| c == 'T' || c == 't') {
            let head = &raw_tok[..idx];
            let tail = &raw_tok[idx + 1..];
            if parse_date_token(head).is_some() && tail.starts_with(|c: char| c.is_ascii_digit()) {
                tokens.push(head);
                tokens.push(tail);
                continue;
            }
        }
        tokens.push(raw_tok);
    }

    let mut date_vals: Option<(i64, i64, i64)> = None;
    let mut time_tokens: Vec<&str> = Vec::new();
    for tok in tokens {
        if date_vals.is_none() {
            if let Some(d) = parse_date_token(tok) {
                date_vals = Some(d);
                continue;
            }
        }
        time_tokens.push(tok);
    }
    let (y, m, d) = date_vals?;
    if !(1..=12).contains(&m) || d < 1 || d > days_in_month(y, m) {
        return None;
    }

    let (mut hh, mut mm, mut ss, mut ns) = (0i64, 0i64, 0i64, 0u32);
    if !time_tokens.is_empty() {
        let time_str = time_tokens.join(" ");
        let ((th, tm, ts, tns), time_zone_seen, time_zone_off) = parse_time_expr(&time_str, false)?;
        hh = th;
        mm = tm;
        ss = ts;
        ns = tns;
        if time_zone_seen {
            if zone_override.is_some() {
                return None;
            }
            zone_override = time_zone_off;
        }
    }

    Some(ParsedDate {
        y,
        m,
        d,
        hh,
        mm,
        ss,
        ns,
        force_utc: zone_override.is_some(),
        from_now: false,
        rels,
        time_only: None,
        zone_override,
    })
}

/// Build the epoch of a parsed date under `zone` and apply the relative
/// adjustments.
fn resolve_parsed(p: &ParsedDate, zone: &TimeZone, now_epoch: i64) -> Option<DateTime> {
    if let Some((hh, mm, ss)) = p.time_only {
        // Time-of-day-only: take the civil date from `now`, then apply the
        // operand's own zone (military letter) before converting back to
        // the output zone (probed: TZ=UTC `-d 1024t` = 17:24:00).
        let (off_now, name_now) = zone.at(now_epoch);
        let now_dt = DateTime::from_epoch(now_epoch, off_now, name_now);
        let wall = days_from_civil(now_dt.year, now_dt.month, now_dt.day) * 86400
            + hh * 3600
            + mm * 60
            + ss;
        let parsed_off = p.zone_override.unwrap_or(off_now);
        let epoch = wall - parsed_off;
        let (off, name) = zone.at(epoch);
        return apply_rels(
            DateTime::from_epoch_ns(epoch, off, name, p.ns),
            &p.rels,
            zone,
        );
    }
    if p.from_now {
        let (off, name) = zone.at(now_epoch);
        return apply_rels(
            DateTime::from_epoch_ns(now_epoch, off, name, p.ns),
            &p.rels,
            zone,
        );
    }
    let epoch0 = if let Some(z_off) = p.zone_override {
        days_from_civil(p.y, p.m, p.d) * 86400 + p.hh * 3600 + p.mm * 60 + p.ss - z_off
    } else if p.force_utc {
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
    apply_rels(
        DateTime::from_epoch_ns(epoch0, off, name, p.ns),
        &p.rels,
        zone,
    )
}

fn apply_rels(dt: DateTime, rels: &[RelAmount], zone: &TimeZone) -> Option<DateTime> {
    if rels.is_empty() {
        return Some(dt);
    }
    let mut delta_months: i64 = 0;
    let mut delta_days: i64 = 0;
    let mut delta_secs: i64 = 0;
    for r in rels {
        match r.calendar {
            CalUnit::Month => {
                delta_months = delta_months.checked_add(r.count)?;
            }
            CalUnit::Year => {
                let m = r.count.checked_mul(12)?;
                delta_months = delta_months.checked_add(m)?;
            }
            CalUnit::Day | CalUnit::Week => {
                let factor = r.unit_seconds / 86400;
                let d = r.count.checked_mul(factor)?;
                delta_days = delta_days.checked_add(d)?;
            }
            CalUnit::None => {
                let s = r.count.checked_mul(r.unit_seconds)?;
                delta_secs = delta_secs.checked_add(s)?;
            }
        }
    }
    let mut dt = dt;
    // Sum all month/year items and apply once before day/second items
    // (gnulib parse-datetime.y: "2026-01-31 +1 month +1 month" = 2026-03-31,
    // "2024-02-29 +1 year" = 2025-03-01).
    if delta_months != 0 {
        let total = dt
            .year
            .checked_mul(12)?
            .checked_add(dt.month - 1)?
            .checked_add(delta_months)?;
        let mut y = total.div_euclid(12);
        let mut m = total.rem_euclid(12) + 1;
        if !(1..=9999).contains(&y) {
            return None;
        }
        let mut d = dt.day;
        while d > days_in_month(y, m) {
            d -= days_in_month(y, m);
            m += 1;
            if m > 12 {
                m = 1;
                y += 1;
                if y > 9999 {
                    return None;
                }
            }
        }
        dt.year = y;
        dt.month = m;
        dt.day = d;
        let wall = days_from_civil(dt.year, dt.month, dt.day).checked_mul(86400)?
            + dt.hour * 3600
            + dt.minute * 60
            + dt.second;
        let west = zone.std_offset_west();
        let (off, name) = zone.at(wall - west);
        dt.epoch = wall - off;
        dt.utc_offset = off;
        dt.tzname = name;
    }
    if delta_days != 0 {
        let nd = dt.days_since_epoch().checked_add(delta_days)?;
        let (y, m, d) = civil_from_days(nd);
        if !(1..=9999).contains(&y) {
            return None;
        }
        dt.year = y;
        dt.month = m;
        dt.day = d;
        let wall = nd.checked_mul(86400)? + dt.hour * 3600 + dt.minute * 60 + dt.second;
        let west = zone.std_offset_west();
        let (off, name) = zone.at(wall - west);
        dt.epoch = wall - off;
        dt.utc_offset = off;
        dt.tzname = name;
    }
    if delta_secs != 0 {
        let ns = dt.ns;
        let epoch = dt.epoch.checked_add(delta_secs)?;
        let (off, name) = zone.at(epoch);
        dt = DateTime::from_epoch_ns(epoch, off, name, ns);
    }
    Some(dt)
}

// ---------------------------------------------------------------------------
// LC_TIME locale database projection (Windows NLS + C fallback)
// ---------------------------------------------------------------------------

struct TimeLocale {
    day: [String; 7],
    abday: [String; 7],
    mon: [String; 12],
    abmon: [String; 12],
    am: String,
    pm: String,
    d_fmt: String,
    t_fmt: String,
    d_t_fmt: String,
    t_fmt_ampm: String,
    date_fmt: String,
}

impl TimeLocale {
    fn c_locale() -> Self {
        TimeLocale {
            day: WEEKDAYS.map(String::from),
            abday: WEEKDAYS_SHORT.map(String::from),
            mon: MONTHS.map(String::from),
            abmon: MONTHS_SHORT.map(String::from),
            am: "AM".to_string(),
            pm: "PM".to_string(),
            d_fmt: "%m/%d/%y".to_string(),
            t_fmt: "%H:%M:%S".to_string(),
            d_t_fmt: "%a %b %e %H:%M:%S %Y".to_string(),
            t_fmt_ampm: "%I:%M:%S %p".to_string(),
            date_fmt: "%a %b %e %H:%M:%S %Z %Y".to_string(),
        }
    }

    fn from_env(env: &dyn winux_core::EnvSource) -> Self {
        let mut selected = String::new();
        for key in ["LC_ALL", "LC_TIME", "LANG"] {
            if let Some(v) = env.get(key) {
                let s = v.to_string_lossy();
                if !s.is_empty() {
                    selected = s.into_owned();
                    break;
                }
            }
        }
        if selected.is_empty() {
            return Self::c_locale();
        }
        let no_mod = selected.split('@').next().unwrap_or("");
        let base = no_mod.split('.').next().unwrap_or("");
        let lower = base.to_ascii_lowercase();
        if base.is_empty() || lower == "c" || lower == "posix" {
            return Self::c_locale();
        }
        let win_name = base.replace('_', "-");
        query_windows_time_locale(&win_name).unwrap_or_else(Self::c_locale)
    }
}

fn picture_to_strftime(picture: &str) -> String {
    let bytes = picture.as_bytes();
    let mut out = String::with_capacity(picture.len() * 2);
    let mut literal = String::new();
    let flush = |lit: &mut String, dst: &mut String| {
        for ch in lit.chars() {
            if ch == '%' {
                dst.push_str("%%");
            } else {
                dst.push(ch);
            }
        }
        lit.clear();
    };
    let mut i = 0;
    let mut quoted = false;
    while i < bytes.len() {
        let ch = bytes[i] as char;
        if ch == '\'' {
            quoted = !quoted;
            if !quoted {
                flush(&mut literal, &mut out);
            }
            i += 1;
            continue;
        }
        if quoted {
            if ch == '\\' && i + 1 < bytes.len() {
                literal.push(bytes[i + 1] as char);
                i += 2;
            } else {
                let rest = &picture[i..];
                let c = rest.chars().next().unwrap();
                literal.push(c);
                i += c.len_utf8();
            }
            continue;
        }
        let mut run = 1;
        while i + run < bytes.len() && bytes[i + run] == bytes[i] {
            run += 1;
        }
        let mut take = |tok: &str| {
            flush(&mut literal, &mut out);
            out.push_str(tok);
            i += run;
        };
        match ch {
            'd' => take(if run >= 4 {
                "%A"
            } else if run == 3 {
                "%a"
            } else {
                "%d"
            }),
            'M' => take(if run >= 4 {
                "%B"
            } else if run == 3 {
                "%b"
            } else {
                "%m"
            }),
            'y' => take(if run >= 3 { "%Y" } else { "%y" }),
            'H' => take("%H"),
            'h' => take("%I"),
            'm' => take("%M"),
            's' => take("%S"),
            't' => take("%p"),
            'g' | 'G' => {
                flush(&mut literal, &mut out);
                i += run;
            }
            '/' | '-' | '.' | ',' | ' ' | ':' => {
                literal.push(ch);
                i += 1;
            }
            '\\' => {
                if i + 1 < bytes.len() {
                    let rest = &picture[i + 1..];
                    let c = rest.chars().next().unwrap();
                    literal.push(c);
                    i += 1 + c.len_utf8();
                } else {
                    i += 1;
                }
            }
            _ => {
                let rest = &picture[i..];
                let c = rest.chars().next().unwrap();
                literal.push(c);
                i += c.len_utf8();
            }
        }
    }
    flush(&mut literal, &mut out);
    out
}

fn picture_uses_meridiem(picture: &str) -> bool {
    picture.contains("tt") || (picture.contains('h') && !picture.contains('H'))
}

#[cfg(windows)]
fn query_windows_time_locale(win_name: &str) -> Option<TimeLocale> {
    #[link(name = "kernel32")]
    extern "system" {
        fn GetLocaleInfoEx(
            lpLocaleName: *const u16,
            LCType: u32,
            lpLCData: *mut u16,
            cchData: i32,
        ) -> i32;
    }
    let wide: Vec<u16> = win_name.encode_utf16().chain(std::iter::once(0)).collect();
    let query = |lctype: u32| -> Option<String> {
        let mut buf = vec![0u16; 256];
        let n = unsafe { GetLocaleInfoEx(wide.as_ptr(), lctype, buf.as_mut_ptr(), 256) };
        if n <= 0 {
            return None;
        }
        let len = (n as usize).saturating_sub(1);
        Some(String::from_utf16_lossy(&buf[..len]))
    };
    // Verify the locale resolves (LOCALE_SDAYNAME7 = 0x30)
    const DAY_TYPES: [u32; 7] = [0x30, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F];
    const ABDAY_TYPES: [u32; 7] = [0x37, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36];
    let mut loc = TimeLocale::c_locale();
    // Check if Windows knows this locale at all
    query(0x30)?;
    for i in 0..7 {
        if let Some(v) = query(DAY_TYPES[i]) {
            loc.day[i] = v;
        }
        if let Some(v) = query(ABDAY_TYPES[i]) {
            loc.abday[i] = v;
        }
    }
    for i in 0..12 {
        if let Some(v) = query(0x38 + i as u32) {
            loc.mon[i] = v;
        }
        if let Some(v) = query(0x44 + i as u32) {
            loc.abmon[i] = v;
        }
    }
    loc.am = query(0x28).unwrap_or_else(|| "AM".to_string());
    loc.pm = query(0x29).unwrap_or_else(|| "PM".to_string());
    if let Some(pic) = query(0x1F) {
        let converted = picture_to_strftime(&pic);
        if !converted.is_empty() {
            loc.d_fmt = converted;
        }
    }
    let time_pic = query(0x1003);
    if let Some(ref pic) = time_pic {
        let converted = picture_to_strftime(pic);
        if !converted.is_empty() {
            loc.t_fmt = converted;
        }
    }
    loc.d_t_fmt = format!("{} {}", loc.d_fmt, loc.t_fmt);
    let meridiem = time_pic.as_deref().is_some_and(picture_uses_meridiem);
    loc.t_fmt_ampm = if meridiem && (!loc.am.is_empty() || !loc.pm.is_empty()) {
        "%I:%M:%S %p".to_string()
    } else {
        String::new()
    };
    loc.date_fmt = format!("{} %Z", loc.d_t_fmt);
    Some(loc)
}

#[cfg(not(windows))]
fn query_windows_time_locale(_win_name: &str) -> Option<TimeLocale> {
    None
}

// ---------------------------------------------------------------------------
// +FORMAT strftime
// ---------------------------------------------------------------------------

fn iso_week(dt: &DateTime) -> (i64, i64) {
    // ISO 8601 week %V/%G
    let wd = (dt.weekday() as i64 + 6) % 7; // Monday=0
    let thursday = dt.days_since_epoch() - wd + 3;
    let (gy, _, _) = civil_from_days(thursday);
    let jan1 = days_from_civil(gy, 1, 1);
    let week = (thursday - jan1) / 7 + 1;
    (gy, week)
}

fn apply_case(text: String, to_uppcase: bool, to_lowcase: bool) -> String {
    if to_lowcase {
        text.to_lowercase()
    } else if to_uppcase {
        text.to_uppercase()
    } else {
        text
    }
}

fn format_zone_offset_colons(utc_offset: i64, colons: usize) -> String {
    let sign = if utc_offset < 0 { '-' } else { '+' };
    let total = utc_offset.abs();
    let oh = total / 3600;
    let om = (total % 3600) / 60;
    let os = total % 60;
    match colons {
        0 => format!("{sign}{oh:02}{om:02}"),
        1 => format!("{sign}{oh:02}:{om:02}"),
        2 => format!("{sign}{oh:02}:{om:02}:{os:02}"),
        _ => {
            if os != 0 {
                format!("{sign}{oh:02}:{om:02}:{os:02}")
            } else if om != 0 {
                format!("{sign}{oh:02}:{om:02}")
            } else {
                format!("{sign}{oh:02}")
            }
        }
    }
}

pub(crate) fn strftime(dt: &DateTime, fmt: &str) -> String {
    strftime_with_locale(dt, fmt, &TimeLocale::c_locale())
}

fn strftime_with_locale(dt: &DateTime, fmt: &str, loc: &TimeLocale) -> String {
    let mut out = String::with_capacity(fmt.len() * 2);
    let bytes = fmt.as_bytes();
    let mut i = 0usize;
    while i < bytes.len() {
        if bytes[i] != b'%' {
            let ch = fmt[i..].chars().next().unwrap();
            out.push(ch);
            i += ch.len_utf8();
            continue;
        }
        let spec_start = i;
        i += 1;
        let mut to_uppcase = false;
        let mut to_lowcase = false;
        let mut change_case = false;
        let mut colon_count = 0usize;
        let mut no_pad = false;
        let mut pad_char = '0';
        let mut plus_pad = false;
        let mut width = 0usize;
        let mut has_width = false;

        while i < bytes.len() {
            match bytes[i] {
                b'^' => to_uppcase = true,
                b'#' => change_case = true,
                b':' => colon_count += 1,
                b'-' => no_pad = true,
                b'_' => pad_char = ' ',
                b'0' => pad_char = '0',
                b'+' => plus_pad = true,
                b'E' | b'O' => {}
                b'1'..=b'9' => {
                    has_width = true;
                    while i < bytes.len() && bytes[i].is_ascii_digit() {
                        width = width
                            .saturating_mul(10)
                            .saturating_add((bytes[i] - b'0') as usize);
                        i += 1;
                    }
                    continue;
                }
                _ => break,
            }
            i += 1;
        }
        // Note: a trailing '0' before a specifier was treated as pad_char='0';
        // also consume any decimal digits following '0' as width (e.g. %02j).
        if i < bytes.len() && bytes[i].is_ascii_digit() {
            has_width = true;
            while i < bytes.len() && bytes[i].is_ascii_digit() {
                width = width
                    .saturating_mul(10)
                    .saturating_add((bytes[i] - b'0') as usize);
                i += 1;
            }
            // Also check for E/O modifier right after width
            while i < bytes.len() && (bytes[i] == b'E' || bytes[i] == b'O') {
                i += 1;
            }
        }
        if i >= bytes.len() {
            // Dangling '%' (optionally with flags): emit verbatim.
            out.push_str(&fmt[spec_start..]);
            break;
        }
        let c = fmt[i..].chars().next().unwrap();
        let char_end = i + c.len_utf8();
        i = char_end;

        if colon_count > 0 && c != 'z' {
            out.push_str(&fmt[spec_start..char_end]);
            continue;
        }

        let fmt_num = |val: i64, def_width: usize, def_pad: char| -> String {
            let digits = val.abs().to_string();
            let sign_prefix = if val < 0 { "-" } else { "" };
            let target_w = if no_pad {
                0
            } else if has_width {
                width
            } else {
                def_width
            };
            let fill = if has_width || no_pad {
                pad_char
            } else {
                def_pad
            };
            if digits.len() >= target_w {
                format!("{sign_prefix}{digits}")
            } else if fill == '0' {
                format!(
                    "{sign_prefix}{}{digits}",
                    "0".repeat(target_w - digits.len())
                )
            } else {
                format!(
                    "{}{sign_prefix}{digits}",
                    " ".repeat(target_w - digits.len() - sign_prefix.len())
                )
            }
        };

        match c {
            'Y' => {
                if plus_pad && dt.year > 9999 {
                    out.push('+');
                }
                out.push_str(&fmt_num(dt.year, 4, '0'));
            }
            'y' => out.push_str(&fmt_num(dt.year.rem_euclid(100), 2, '0')),
            'C' => out.push_str(&fmt_num(dt.year.div_euclid(100), 2, '0')),
            'm' => out.push_str(&fmt_num(dt.month, 2, '0')),
            'd' => out.push_str(&fmt_num(dt.day, 2, '0')),
            'e' => out.push_str(&fmt_num(dt.day, 2, ' ')),
            'H' => out.push_str(&fmt_num(dt.hour, 2, '0')),
            'k' => out.push_str(&fmt_num(dt.hour, 2, ' ')),
            'I' => out.push_str(&fmt_num(
                if dt.hour % 12 == 0 { 12 } else { dt.hour % 12 },
                2,
                '0',
            )),
            'l' => out.push_str(&fmt_num(
                if dt.hour % 12 == 0 { 12 } else { dt.hour % 12 },
                2,
                ' ',
            )),
            'M' => out.push_str(&fmt_num(dt.minute, 2, '0')),
            'S' => out.push_str(&fmt_num(dt.second, 2, '0')),
            'N' => {
                let nanos = format!("{:09}", dt.ns);
                if no_pad {
                    let trimmed = nanos.trim_start_matches('0');
                    out.push_str(if trimmed.is_empty() { "0" } else { trimmed });
                } else if has_width {
                    if width < nanos.len() {
                        out.push_str(&nanos[..width]);
                    } else {
                        out.push_str(&"0".repeat(width - nanos.len()));
                        out.push_str(&nanos);
                    }
                } else {
                    out.push_str(&nanos);
                }
            }
            'j' => out.push_str(&fmt_num(dt.yearday(), 3, '0')),
            'q' => out.push_str(&((dt.month - 1) / 3 + 1).to_string()),
            'p' => {
                let text = if dt.hour < 12 {
                    loc.am.clone()
                } else {
                    loc.pm.clone()
                };
                if change_case {
                    to_uppcase = false;
                    to_lowcase = true;
                }
                out.push_str(&apply_case(text, to_uppcase, to_lowcase));
            }
            'P' => {
                let text = if dt.hour < 12 {
                    loc.am.clone()
                } else {
                    loc.pm.clone()
                };
                out.push_str(&apply_case(text, false, true));
            }
            'A' => {
                if change_case {
                    to_uppcase = true;
                    to_lowcase = false;
                }
                out.push_str(&apply_case(
                    loc.day[dt.weekday()].clone(),
                    to_uppcase,
                    to_lowcase,
                ));
            }
            'a' => {
                if change_case {
                    to_uppcase = true;
                    to_lowcase = false;
                }
                out.push_str(&apply_case(
                    loc.abday[dt.weekday()].clone(),
                    to_uppcase,
                    to_lowcase,
                ));
            }
            'B' => {
                if change_case {
                    to_uppcase = true;
                    to_lowcase = false;
                }
                out.push_str(&apply_case(
                    loc.mon[(dt.month - 1) as usize].clone(),
                    to_uppcase,
                    to_lowcase,
                ));
            }
            'b' | 'h' => {
                if change_case {
                    to_uppcase = true;
                    to_lowcase = false;
                }
                out.push_str(&apply_case(
                    loc.abmon[(dt.month - 1) as usize].clone(),
                    to_uppcase,
                    to_lowcase,
                ));
            }
            'Z' => {
                if change_case {
                    to_uppcase = false;
                    to_lowcase = true;
                }
                out.push_str(&apply_case(dt.tzname.clone(), to_uppcase, to_lowcase));
            }
            'z' => out.push_str(&format_zone_offset_colons(dt.utc_offset, colon_count)),
            's' => out.push_str(&dt.epoch.to_string()),
            'F' => out.push_str(&apply_case(
                strftime_with_locale(dt, "%Y-%m-%d", loc),
                to_uppcase,
                to_lowcase,
            )),
            'D' => out.push_str(&apply_case(
                strftime_with_locale(dt, "%m/%d/%y", loc),
                to_uppcase,
                to_lowcase,
            )),
            'T' => out.push_str(&apply_case(
                strftime_with_locale(dt, "%H:%M:%S", loc),
                to_uppcase,
                to_lowcase,
            )),
            'R' => out.push_str(&apply_case(
                strftime_with_locale(dt, "%H:%M", loc),
                to_uppcase,
                to_lowcase,
            )),
            'r' => {
                let pat = if loc.t_fmt_ampm.is_empty() {
                    "%I:%M:%S %p"
                } else {
                    loc.t_fmt_ampm.as_str()
                };
                out.push_str(&apply_case(
                    strftime_with_locale(dt, pat, loc),
                    to_uppcase,
                    to_lowcase,
                ));
            }
            'c' => out.push_str(&apply_case(
                strftime_with_locale(dt, &loc.d_t_fmt, loc),
                to_uppcase,
                to_lowcase,
            )),
            'x' => out.push_str(&apply_case(
                strftime_with_locale(dt, &loc.d_fmt, loc),
                to_uppcase,
                to_lowcase,
            )),
            'X' => out.push_str(&apply_case(
                strftime_with_locale(dt, &loc.t_fmt, loc),
                to_uppcase,
                to_lowcase,
            )),
            'n' => out.push('\n'),
            't' => out.push('\t'),
            '%' => out.push('%'),
            'U' => {
                let jan1_wd = weekday_from_days(days_from_civil(dt.year, 1, 1));
                let yd = dt.yearday() - 1;
                out.push_str(&fmt_num((yd + jan1_wd) / 7, 2, '0'));
            }
            'W' => {
                let jan1_wd = (weekday_from_days(days_from_civil(dt.year, 1, 1)) + 6) % 7;
                let yd = dt.yearday() - 1;
                out.push_str(&fmt_num((yd + jan1_wd) / 7, 2, '0'));
            }
            'V' | 'G' => {
                let (gy, week) = iso_week(dt);
                if c == 'V' {
                    out.push_str(&fmt_num(week, 2, '0'));
                } else {
                    out.push_str(&fmt_num(gy, 4, '0'));
                }
            }
            'g' => {
                let (gy, _) = iso_week(dt);
                out.push_str(&fmt_num(gy.rem_euclid(100), 2, '0'));
            }
            'u' => out.push_str(&(((dt.weekday() as i64 + 6) % 7) + 1).to_string()),
            'w' => out.push_str(&(dt.weekday() as i64).to_string()),
            _ => {
                // GNU: an unknown conversion (e.g. %Q, %+) prints literally
                out.push_str(&fmt[spec_start..char_end]);
            }
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
fn split_iso_tokens(args: &[OsString]) -> (Vec<Option<String>>, Vec<OsString>) {
    let mut kinds: Vec<Option<String>> = Vec::new();
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
                    kinds.push(Some(glue.to_string()));
                    continue;
                }
            }
            if let Some(v) = text.strip_prefix("--iso-8601=") {
                kinds.push(Some(v.to_string()));
                continue;
            }
            if text == "--iso-8601" {
                kinds.push(None);
                continue;
            }
        }
        rest.push(a.clone());
    }
    (kinds, rest)
}

fn argmatch_timespec<'a>(spec: &str, candidates: &[&'a str]) -> Option<&'a str> {
    for &c in candidates {
        if spec == c {
            return Some(c);
        }
    }
    if spec.is_empty() {
        return None;
    }
    let mut found: Option<&'a str> = None;
    for &c in candidates {
        if c.starts_with(spec) {
            if found.is_some() {
                return None;
            }
            found = Some(c);
        }
    }
    found
}

fn parse_posix_clock(arg: &str) -> bool {
    let digits = match arg.split_once('.') {
        Some((head, ss)) => {
            if ss.len() != 2 || !ss.bytes().all(|b| b.is_ascii_digit()) {
                return false;
            }
            let sec: u32 = ss.parse().unwrap_or(99);
            if sec > 60 {
                return false;
            }
            head
        }
        None => arg,
    };
    if ![8, 10, 12].contains(&digits.len()) || !digits.bytes().all(|b| b.is_ascii_digit()) {
        return false;
    }
    let mon: u32 = digits[0..2].parse().unwrap_or(0);
    let day: u32 = digits[2..4].parse().unwrap_or(0);
    let hour: u32 = digits[4..6].parse().unwrap_or(99);
    let minute: u32 = digits[6..8].parse().unwrap_or(99);
    if digits.len() == 12 {
        let year: u32 = digits[8..12].parse().unwrap_or(0);
        let century = year / 100;
        if century != 19 && century != 20 {
            return false;
        }
    }
    (1..=12).contains(&mon) && (1..=31).contains(&day) && hour <= 23 && minute <= 59
}

fn now_epoch() -> i64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs() as i64)
        .unwrap_or(0)
}

fn system_time_to_epoch_ns(t: std::time::SystemTime) -> (i64, u32) {
    match t.duration_since(std::time::UNIX_EPOCH) {
        Ok(d) => (d.as_secs() as i64, d.subsec_nanos()),
        Err(e) => {
            let d = e.duration();
            let ns = d.subsec_nanos();
            if ns == 0 {
                (-(d.as_secs() as i64), 0)
            } else {
                (-(d.as_secs() as i64) - 1, 1_000_000_000 - ns)
            }
        }
    }
}

pub fn date_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let (mut iso_tokens, args) = split_iso_tokens(args);
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

    for ev in &p.events {
        if ev.key == "iso-8601" {
            iso_tokens.push(None);
        }
    }

    let has_date = p.has("date");
    let has_file = p.has("file");
    let has_ref = p.has("reference");
    let has_res = p.has("resolution");
    let specified_date = has_date as u8 + has_file as u8 + has_ref as u8 + has_res as u8;

    if specified_date > 1 {
        let _ = writeln!(
            io.stderr,
            "date: the options to specify dates for printing are mutually exclusive"
        );
        let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "date"));
        return Ok(1);
    }
    if p.has("set") && specified_date > 0 {
        let _ = writeln!(
            io.stderr,
            "date: the options to print and set the time may not be used together"
        );
        let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "date"));
        return Ok(1);
    }

    let use_utc = p.has("utc") || p.has("universal") || p.has("uct");
    let zone = effective_zone(use_utc, ctx.env);

    let mut iso_format: Option<&'static str> = None;
    if let Some(last_iso) = iso_tokens.last() {
        let spec = last_iso.as_deref().unwrap_or("date");
        const ISO_CANDS: [&str; 5] = ["hours", "minutes", "date", "seconds", "ns"];
        match argmatch_timespec(spec, &ISO_CANDS) {
            Some("date") => iso_format = Some("%Y-%m-%d"),
            Some("hours") => iso_format = Some("%Y-%m-%dT%H%:z"),
            Some("minutes") => iso_format = Some("%Y-%m-%dT%H:%M%:z"),
            Some("seconds") => iso_format = Some("%Y-%m-%dT%H:%M:%S%:z"),
            Some("ns") => iso_format = Some("%Y-%m-%dT%H:%M:%S,%N%:z"),
            _ => {
                let _ = writeln!(
                    io.stderr,
                    "date: invalid argument '{spec}' for '--iso-8601'"
                );
                let _ = writeln!(io.stderr, "Valid arguments are:");
                for c in ISO_CANDS {
                    let _ = writeln!(io.stderr, "  - '{c}'");
                }
                let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "date"));
                return Ok(1);
            }
        }
    }

    let rfc3339_count = p.events.iter().filter(|e| e.key == "rfc-3339").count();
    let mut rfc3339_format: Option<&'static str> = None;
    if rfc3339_count > 0 {
        let spec = p.value_of("rfc-3339").unwrap_or("");
        const RFC_CANDS: [&str; 3] = ["date", "seconds", "ns"];
        match argmatch_timespec(spec, &RFC_CANDS) {
            Some("date") => rfc3339_format = Some("%Y-%m-%d"),
            Some("seconds") => rfc3339_format = Some("%Y-%m-%d %H:%M:%S%:z"),
            Some("ns") => rfc3339_format = Some("%Y-%m-%d %H:%M:%S.%N%:z"),
            _ => {
                let _ = writeln!(
                    io.stderr,
                    "date: invalid argument '{spec}' for '--rfc-3339'"
                );
                let _ = writeln!(io.stderr, "Valid arguments are:");
                for c in RFC_CANDS {
                    let _ = writeln!(io.stderr, "  - '{c}'");
                }
                let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "date"));
                return Ok(1);
            }
        }
    }

    let rfc_email_count = p
        .events
        .iter()
        .filter(|e| e.key == "rfc-email" || e.key == "rfc-2822" || e.key == "rfc-822")
        .count();

    let format_options = iso_tokens.len() + rfc3339_count + rfc_email_count;
    if format_options > 1 {
        let _ = writeln!(io.stderr, "date: multiple output formats specified");
        return Ok(1);
    }

    let rfc_email = rfc_email_count > 0;
    let mut format_opt: Option<String> = if let Some(f) = iso_format {
        Some(f.to_string())
    } else if let Some(f) = rfc3339_format {
        Some(f.to_string())
    } else if rfc_email {
        Some("%a, %d %b %Y %H:%M:%S %z".to_string())
    } else {
        None
    };

    let mut posix_date_operand: Option<String> = None;
    if p.positionals.len() > 1 {
        let extra = p.positionals[1].to_string_lossy();
        let _ = writeln!(io.stderr, "date: extra operand '{extra}'");
        let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "date"));
        return Ok(1);
    } else if let Some(pos) = p.positionals.first() {
        let op = pos.to_string_lossy().into_owned();
        if let Some(fmt_body) = op.strip_prefix('+') {
            if format_opt.is_some() {
                let _ = writeln!(io.stderr, "date: multiple output formats specified");
                return Ok(1);
            }
            format_opt = Some(fmt_body.to_string());
        } else if specified_date > 0 || p.has("set") {
            let _ = writeln!(
                io.stderr,
                "date: the argument '{op}' lacks a leading '+';\nwhen using an option to specify date(s), any non-option\nargument must be a format string beginning with '+'",
            );
            let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "date"));
            return Ok(1);
        } else {
            posix_date_operand = Some(op);
        }
    }

    let loc = if rfc_email {
        TimeLocale::c_locale()
    } else {
        TimeLocale::from_env(ctx.env)
    };
    let out_fmt = format_opt.unwrap_or_else(|| {
        if has_res {
            "%s.%N".to_string()
        } else {
            loc.date_fmt.clone()
        }
    });

    if p.has("set") {
        // honest boundary: the port never sets the clock; GNU on the gate
        // host also fails (probed rc 1)
        let _ = writeln!(
            io.stderr,
            "date: setting the system clock is not supported on Windows"
        );
        return Ok(1);
    }

    if let Some(op) = posix_date_operand {
        if !parse_posix_clock(&op) {
            let _ = writeln!(io.stderr, "date: invalid date '{op}'");
            return Ok(1);
        }
        let _ = writeln!(
            io.stderr,
            "date: setting the system clock is not supported on Windows"
        );
        return Ok(1);
    }

    // -f DATEFILE: one date per line (GNU shape)
    if let Some(file) = p.value_of("file") {
        let text = if file == "-" {
            let mut buf = String::new();
            std::io::Read::read_to_string(&mut io.stdin, &mut buf)?;
            buf
        } else {
            let path = resolve(ctx, file);
            if path.is_dir() {
                let _ = writeln!(
                    io.stderr,
                    "date: {}: read error: Is a directory",
                    crate::fsutil::quotef(file)
                );
                return Ok(1);
            }
            match std::fs::read_to_string(&path) {
                Ok(t) => t,
                Err(e) => {
                    let reason = crate::fsutil::reason(&e);
                    let _ = writeln!(io.stderr, "date: {}: {reason}", crate::fsutil::quotef(file));
                    return Ok(1);
                }
            }
        };
        let mut failed = false;
        let trimmed_eof = text.strip_suffix('\n').unwrap_or(&text);
        if !(text.is_empty()) {
            for raw_line in trimmed_eof.split('\n') {
                let line = raw_line.strip_suffix('\r').unwrap_or(raw_line);
                let parsed =
                    parse_date_string(line).and_then(|pd| resolve_parsed(&pd, &zone, now_epoch()));
                match parsed {
                    Some(d) => {
                        let out = strftime_with_locale(&d, &out_fmt, &loc);
                        writeln!(io.stdout, "{out}")?;
                    }
                    None => {
                        let _ = writeln!(io.stderr, "date: invalid date '{line}'");
                        failed = true;
                    }
                }
            }
        }
        return Ok(if failed { 1 } else { 0 });
    }

    // resolve the timestamp: -d / -r / --resolution / now
    let dt = if let Some(d) = p.value_of("date") {
        match parse_date_string(d).and_then(|pd| resolve_parsed(&pd, &zone, now_epoch())) {
            Some(dt) => dt,
            None => {
                let _ = writeln!(io.stderr, "date: invalid date '{d}'");
                return Ok(1);
            }
        }
    } else if let Some(r) = p.value_of("reference") {
        let path = resolve(ctx, r);
        match path.metadata().and_then(|m| m.modified()) {
            Ok(t) => {
                let (epoch, ns) = system_time_to_epoch_ns(t);
                let (off, name) = zone.at(epoch);
                DateTime::from_epoch_ns(epoch, off, name, ns)
            }
            Err(e) => {
                let reason = crate::fsutil::reason(&e);
                let _ = writeln!(io.stderr, "date: {}: {reason}", crate::fsutil::quotef(r));
                return Ok(1);
            }
        }
    } else if has_res {
        let (off, name) = zone.at(0);
        DateTime::from_epoch_ns(0, off, name, 100)
    } else {
        let (epoch, ns) = system_time_to_epoch_ns(std::time::SystemTime::now());
        let (off, name) = zone.at(epoch);
        DateTime::from_epoch_ns(epoch, off, name, ns)
    };

    let line = strftime_with_locale(&dt, &out_fmt, &loc);
    writeln!(io.stdout, "{line}")?;
    Ok(0)
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

    /// Resolve `s` against a pinned now (2020-01-02 03:04:05 UTC) so the
    /// assertions are time-independent.
    fn fmt_at(s: &str) -> Option<String> {
        let p = parse_date_string(s)?;
        let dt = resolve_parsed(&p, &TimeZone::utc(), 1_577_934_245)?;
        Some(strftime(&dt, "%F_%T"))
    }

    #[test]
    fn relative_anchored_forms() {
        // GNU 9.4 probes 2026-11-17 (TZ=UTC), re-based on the pinned now.
        let cases = [
            ("now", "2020-01-02_03:04:05"),
            ("today", "2020-01-02_03:04:05"),
            ("tomorrow", "2020-01-03_03:04:05"),
            ("yesterday", "2020-01-01_03:04:05"),
            ("+1 day", "2020-01-03_03:04:05"),
            ("-1 day", "2020-01-01_03:04:05"),
            ("1 day", "2020-01-03_03:04:05"),
            ("+1 week", "2020-01-09_03:04:05"),
            ("+1 hour", "2020-01-02_04:04:05"),
            ("+1 minute", "2020-01-02_03:05:05"),
            ("+1 second", "2020-01-02_03:04:06"),
            ("next day", "2020-01-03_03:04:05"),
            ("next week", "2020-01-09_03:04:05"),
            ("last month", "2019-12-02_03:04:05"),
            ("this month", "2020-01-02_03:04:05"),
            ("+1 fortnight", "2020-01-16_03:04:05"),
            ("1 day ago", "2020-01-01_03:04:05"),
            // "ago" negates only the last group (GNU grammar relunit tAGO)
            ("1 day 2 hours ago", "2020-01-03_01:04:05"),
            ("2 days 3 hours ago", "2020-01-04_00:04:05"),
            // chained signed groups
            ("+1 day +2 hours", "2020-01-03_05:04:05"),
            ("+1 day -2 hours", "2020-01-03_01:04:05"),
            ("1 day 2 hours", "2020-01-03_05:04:05"),
            ("now + 1 day", "2020-01-03_03:04:05"),
            ("now - 2 hours", "2020-01-02_01:04:05"),
        ];
        for (input, want) in cases {
            assert_eq!(fmt_at(input).as_deref(), Some(want), "{input}");
        }
        // explicit base with glued or spaced tail (GNU probe)
        assert_eq!(
            fmt_at("2020-01-02 03:04:05 UTC +1 day").as_deref(),
            Some("2020-01-03_03:04:05")
        );
        assert_eq!(
            fmt_at("2020-01-02 03:04:05 UTC + 1 day").as_deref(),
            Some("2020-01-03_03:04:05")
        );
        assert_eq!(
            fmt_at("2020-01-02 1 day").as_deref(),
            Some("2020-01-03_00:00:00")
        );
        // documented unsupported subset: weekday names stay invalid
        assert_eq!(fmt_at("next monday"), None);
        assert_eq!(fmt_at("monday"), None);
    }

    #[test]
    fn time_only_and_military_zone() {
        // GNU 9.4 probes 2026-11-17 (TZ=UTC): `1024` = 10:24 today,
        // `1024t` = 10:24 in military zone T (UTC-7) = 17:24 UTC.
        let cases = [
            ("1024", "2020-01-02_10:24:00"),
            ("1024t", "2020-01-02_17:24:00"),
            ("1024T", "2020-01-02_17:24:00"),
            ("1024z", "2020-01-02_10:24:00"),
            ("1024n", "2020-01-02_11:24:00"),
            ("10", "2020-01-02_10:00:00"),
            ("3", "2020-01-02_03:00:00"),
            ("03:04", "2020-01-02_03:04:00"),
            ("03:04:05", "2020-01-02_03:04:05"),
            ("17:24:00", "2020-01-02_17:24:00"),
            ("3:04pm", "2020-01-02_15:04:00"),
            ("3:04:05pm", "2020-01-02_15:04:05"),
            ("12:00am", "2020-01-02_00:00:00"),
            ("12:00pm", "2020-01-02_12:00:00"),
            ("2020-01-02T03:04:05", "2020-01-02_03:04:05"),
            ("2020-01-02t03:04:05", "2020-01-02_03:04:05"),
        ];
        for (input, want) in cases {
            assert_eq!(fmt_at(input).as_deref(), Some(want), "{input}");
        }
        // six digits are a date-ish number, not a time (GNU: invalid date)
        assert_eq!(fmt_at("102400"), None);
        // world-readable form: the same shape through date_cmd under TZ=UTC
        let (r, out, _) = tz("UTC", &["-u", "-d", "1024t", "+%H:%M:%S"]);
        assert_eq!(r, 0);
        assert_eq!(out, "17:24:00\n");
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

    #[test]
    fn corpus_regressions_fixed() {
        // 12-hour AM/PM with and without space, midnight/noon, dotted P.M., invalid >12
        assert_eq!(
            tz("UTC", &["-d", "2026-01-02 03:04:05 PM", "+%F_%H:%M"]).1,
            "2026-01-02_15:04\n"
        );
        assert_eq!(
            tz("UTC", &["-d", "2026-01-02 03:04:05PM", "+%H:%M"]).1,
            "15:04\n"
        );
        assert_eq!(
            tz("UTC", &["-d", "2026-01-02 12:00:00 AM", "+%H:%M"]).1,
            "00:00\n"
        );
        assert_eq!(
            tz("UTC", &["-d", "2026-01-02 12:00:00 PM", "+%H:%M"]).1,
            "12:00\n"
        );
        assert_eq!(
            tz("UTC", &["-d", "2026-01-02 03:04:05 P.M.", "+%H:%M"]).1,
            "15:04\n"
        );
        assert_eq!(tz("UTC", &["-d", "2026-01-02 13:04:05 PM", "+%H:%M"]).0, 1);

        // slash date & word order
        assert_eq!(
            tz("UTC", &["-d", "2026/1/2 10:00", "+%F_%H:%M"]).1,
            "2026-01-02_10:00\n"
        );
        assert_eq!(
            tz("UTC", &["-d", "10:00 2026-01-01", "+%F_%H:%M"]).1,
            "2026-01-01_10:00\n"
        );
        assert_eq!(
            tz("UTC", &["-d", "10:00 PM 2026-01-01", "+%F_%H:%M"]).1,
            "2026-01-01_22:00\n"
        );

        // summed relative months and leap year rollover
        assert_eq!(
            tz("UTC", &["-d", "2026-01-31 +1 month +1 month", "+%F"]).1,
            "2026-03-31\n"
        );
        assert_eq!(
            tz("UTC", &["-d", "2024-02-29 +1 year", "+%F"]).1,
            "2025-03-01\n"
        );

        // military J/j (local zone)
        assert_eq!(tz("UTC", &["-u", "-d", "1024j", "+%H:%M"]).1, "10:24\n");
        assert_eq!(tz("UTC", &["-u", "-d", "1024J", "+%H:%M"]).1, "10:24\n");
        assert_eq!(
            tz("EST5EDT", &["-d", "1024j", "+%H:%M %Z"]).1,
            "10:24 EST\n"
        );

        // zone colons
        assert_eq!(
            tz("UTC", &["-u", "-d", "@0", "+%::z %:::z %:z"]).1,
            "+00:00:00 +00 +00:00\n"
        );
        assert_eq!(
            tz("XYZ+5:30", &["-d", "@0", "+%::z|%:::z|%:z"]).1,
            "-05:30:00|-05:30|-05:30\n"
        );

        // format conflict
        let (rc, _, err) = run(&["-R", "-Iseconds"]);
        assert_eq!(rc, 1);
        assert!(err.contains("multiple output formats specified"));
        let (rc, _, err) = run(&["-R", "+%Y"]);
        assert_eq!(rc, 1);
        assert!(err.contains("multiple output formats specified"));

        // resolution + format
        assert_eq!(run(&["--resolution"]).1, "0.000000100\n");
        assert_eq!(run(&["--resolution", "+%s"]).1, "0\n");

        // iso prefix & nanoseconds rejected & -Ins comma
        assert_eq!(
            tz("UTC", &["-u", "-d", "@0", "-Isec"]).1,
            "1970-01-01T00:00:00+00:00\n"
        );
        assert_eq!(
            tz("UTC", &["-u", "-d", "@0", "-Ins"]).1,
            "1970-01-01T00:00:00,000000000+00:00\n"
        );
        let (rc, _, err) = run(&["-Inanoseconds"]);
        assert_eq!(rc, 1);
        assert!(err.contains("invalid argument 'nanoseconds' for '--iso-8601'"));
        assert!(err.contains("  - 'hours'"));

        // missing reference file quotes as bare name
        let (rc, _, err) = run(&["-r", "nofile.txt", "+%s"]);
        assert_eq!(rc, 1);
        assert_eq!(err, "date: nofile.txt: No such file or directory\n");

        // case flags & E/O & aliases
        assert_eq!(
            tz(
                "UTC",
                &["-u", "-d", "@0", "+%a|%#a|%^a|%p|%#p|%^p|%Z|%#Z|%^Z"]
            )
            .1,
            "Thu|THU|THU|AM|am|AM|UTC|utc|UTC\n"
        );
        assert_eq!(
            tz("UTC", &["-u", "-d", "@0", "+%c|%#c|%^c|%r|%x|%q"]).1,
            "Thu Jan  1 00:00:00 1970|Thu Jan  1 00:00:00 1970|THU JAN  1 00:00:00 1970|12:00:00 AM|01/01/70|1\n"
        );
        assert_eq!(
            tz("UTC", &["-u", "-d", "@0", "+%EY|%Oy|%Ec"]).1,
            "1970|70|Thu Jan  1 00:00:00 1970\n"
        );
        assert_eq!(run(&["--uct", "-d", "@0", "+%s"]).1, "0\n");
        assert_eq!(
            run(&["--rfc-822", "-d", "@0"]).1,
            run(&["-R", "-d", "@0"]).1
        );
    }
}
