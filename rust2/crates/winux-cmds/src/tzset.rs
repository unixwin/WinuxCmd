//! `tzset` — Cygwin/MSYS2 tzset semantics, ported for WinuxCmd 2.0 (#1488).
//!
//! Prints `TZ=<posix-id>` from the current Windows timezone via
//! `GetDynamicTimeZoneInformation` (with `ctx.env.get("TZ")` fallback on
//! non-Windows hosts) or validates a requested POSIX timezone ID.

use std::ffi::OsString;
use std::io::Write;

use crate::argopt;
use crate::gnuhelp;
use winux_core::{usage_error, CmdResult, Ctx, Io};

const TZSET_HELP: &str = "\
Usage: tzset [OPTION]

Print POSIX-compatible timezone ID from current Windows timezone setting

Options:
      --help               output usage information and exit.
  -V, --version            output version information and exit.

Use tzset to set your TZ variable. In POSIX-compatible shells like bash,
dash, mksh, or zsh:

      export TZ=$(tzset)

In csh-compatible shells like tcsh:

      setenv TZ `tzset`
";

const KNOWN_POSIX_ZONES: [&str; 11] = [
    "Asia/Shanghai",
    "UTC",
    "Europe/London",
    "Europe/Berlin",
    "Europe/Budapest",
    "America/New_York",
    "America/Chicago",
    "America/Denver",
    "America/Los_Angeles",
    "Asia/Tokyo",
    "Asia/Seoul",
];

fn key_to_posix(key: &str) -> Option<&'static str> {
    const MAP: [(&str, &str); 11] = [
        ("China Standard Time", "Asia/Shanghai"),
        ("UTC", "UTC"),
        ("GMT Standard Time", "Europe/London"),
        ("W. Europe Standard Time", "Europe/Berlin"),
        ("Central Europe Standard Time", "Europe/Budapest"),
        ("Eastern Standard Time", "America/New_York"),
        ("Central Standard Time", "America/Chicago"),
        ("Mountain Standard Time", "America/Denver"),
        ("Pacific Standard Time", "America/Los_Angeles"),
        ("Tokyo Standard Time", "Asia/Tokyo"),
        ("Korea Standard Time", "Asia/Seoul"),
    ];
    for (win_key, posix) in MAP {
        if key.eq_ignore_ascii_case(win_key) {
            return Some(posix);
        }
    }
    None
}

fn utf16_z_to_string(buf: &[u16]) -> String {
    let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
    String::from_utf16_lossy(&buf[..len])
}

#[cfg(windows)]
fn current_posix_timezone(_ctx: &Ctx) -> Option<String> {
    #[repr(C)]
    struct SystemTimeWin {
        _fields: [u16; 8],
    }
    #[repr(C)]
    struct DynamicTimeZoneInformation {
        bias: i32,
        standard_name: [u16; 32],
        standard_date: SystemTimeWin,
        standard_bias: i32,
        daylight_name: [u16; 32],
        daylight_date: SystemTimeWin,
        daylight_bias: i32,
        time_zone_key_name: [u16; 128],
        dynamic_daylight_time_disabled: u8,
    }
    extern "system" {
        fn GetDynamicTimeZoneInformation(
            pTimeZoneInformation: *mut DynamicTimeZoneInformation,
        ) -> u32;
    }
    const TIME_ZONE_ID_INVALID: u32 = 0xFFFF_FFFF;

    let mut tz = DynamicTimeZoneInformation {
        bias: 0,
        standard_name: [0; 32],
        standard_date: SystemTimeWin { _fields: [0; 8] },
        standard_bias: 0,
        daylight_name: [0; 32],
        daylight_date: SystemTimeWin { _fields: [0; 8] },
        daylight_bias: 0,
        time_zone_key_name: [0; 128],
        dynamic_daylight_time_disabled: 0,
    };
    if unsafe { GetDynamicTimeZoneInformation(&mut tz) } == TIME_ZONE_ID_INVALID {
        return None;
    }

    let key = utf16_z_to_string(&tz.time_zone_key_name);
    if let Some(mapped) = key_to_posix(&key) {
        return Some(mapped.to_owned());
    }

    let bias = tz.bias + tz.standard_bias;
    let hours = bias.abs() / 60;
    let std_name = utf16_z_to_string(&tz.standard_name);
    let mut abbr = String::new();
    for ch in std_name.bytes() {
        if ch.is_ascii_alphabetic() {
            abbr.push((ch as char).to_ascii_uppercase());
            if abbr.len() == 3 {
                break;
            }
        }
    }
    if abbr.is_empty() {
        abbr.push_str("TZ");
    }
    let sign = if bias <= 0 { '-' } else { '+' };
    Some(format!("{abbr}{sign}{hours}"))
}

#[cfg(not(windows))]
fn current_posix_timezone(ctx: &Ctx) -> Option<String> {
    let _ = utf16_z_to_string(&[]);
    Some(
        ctx.env
            .get("TZ")
            .map(|v| v.to_string_lossy().into_owned())
            .filter(|s| !s.is_empty())
            .unwrap_or_else(|| "UTC".to_owned()),
    )
}

pub fn tzset_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut positionals: Vec<String> = Vec::new();
    let mut no_more_opts = false;

    for a in args {
        let s = a.to_string_lossy();
        if !no_more_opts {
            if s == "--" {
                no_more_opts = true;
                continue;
            }
            if s == "--help" || s == "-h" {
                io.stdout.write_all(TZSET_HELP.as_bytes())?;
                return Ok(0);
            }
            if s == "-V" || s == "--version" {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("tzset", "WinuxCmd")
                )?;
                return Ok(0);
            }
            if s.starts_with('-') && s.len() > 1 {
                let msg = if let Some(rest) = s.strip_prefix("--") {
                    argopt::unrecognized_option(ctx, &format!("--{rest}"))
                } else {
                    let c = s.chars().nth(1).unwrap();
                    argopt::invalid_option(ctx, &c.to_string())
                };
                let _ = usage_error(ctx, io, "tzset", &msg);
                return Ok(1);
            }
        }
        positionals.push(s.into_owned());
    }

    if let Some(requested) = positionals.first() {
        if !KNOWN_POSIX_ZONES.contains(&requested.as_str()) {
            writeln!(io.stderr, "Usage: tzset [OPTION]")?;
            writeln!(io.stderr)?;
            writeln!(
                io.stderr,
                "Print POSIX-compatible timezone ID from current Windows timezone setting"
            )?;
            return Ok(1);
        }
        writeln!(io.stdout, "TZ={requested}")?;
        return Ok(0);
    }

    match current_posix_timezone(ctx) {
        Some(tz) => {
            writeln!(io.stdout, "TZ={tz}")?;
            Ok(0)
        }
        None => Ok(1),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run_tzset(args: &[&str]) -> (u32, String, String) {
        let sc = ScratchCtx::new("tzset-test");
        let ctx = sc.ctx();
        let mut io = MemIo::new("");
        let os_args: Vec<OsString> = args.iter().map(OsString::from).collect();
        let rc = tzset_cmd(&os_args, &mut io.split(), &ctx).unwrap();
        (rc, io.out(), io.err())
    }

    #[test]
    fn current_and_known_zones() {
        let (rc, out, err) = run_tzset(&[]);
        assert_eq!(rc, 0);
        assert!(err.is_empty());
        assert!(out.starts_with("TZ="));

        let (rc, out, _) = run_tzset(&["UTC"]);
        assert_eq!(rc, 0);
        assert_eq!(out, "TZ=UTC\n");

        let (rc, out, _) = run_tzset(&["Asia/Shanghai"]);
        assert_eq!(rc, 0);
        assert_eq!(out, "TZ=Asia/Shanghai\n");
    }

    #[test]
    fn unknown_zone_and_help() {
        let (rc, _, err) = run_tzset(&["Mars/Olympus"]);
        assert_eq!(rc, 1);
        assert!(err.contains("Usage: tzset [OPTION]"));

        let (rc, out, _) = run_tzset(&["--help"]);
        assert_eq!(rc, 0);
        assert!(out.contains("export TZ=$(tzset)"));

        assert_eq!(key_to_posix("china standard time"), Some("Asia/Shanghai"));
    }
}
