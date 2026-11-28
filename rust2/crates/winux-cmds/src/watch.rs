//! `watch` — execute a program periodically, showing output fullscreen (#1488).
//!
//! Port of `src/commands/watch.cpp` (procps 4.0.4 option surface + `--count`).
//! Explicit `Io`/`Ctx`, zero globals.

use std::ffi::OsString;
use std::io::Write;
use std::process::Command;
use std::thread::sleep;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use crate::argopt::{self, OptSpec};
use crate::childproc::{build_child_command, resolve_child, ChildResolve};
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('b'),
        long: "beep",
        takes_value: false,
    },
    OptSpec {
        short: Some('c'),
        long: "color",
        takes_value: false,
    },
    OptSpec {
        short: Some('C'),
        long: "no-color",
        takes_value: false,
    },
    OptSpec {
        short: Some('d'),
        long: "differences",
        takes_value: false,
    },
    OptSpec {
        short: Some('e'),
        long: "errexit",
        takes_value: false,
    },
    OptSpec {
        short: Some('g'),
        long: "chgexit",
        takes_value: false,
    },
    OptSpec {
        short: Some('n'),
        long: "interval",
        takes_value: true,
    },
    OptSpec {
        short: Some('p'),
        long: "precise",
        takes_value: false,
    },
    OptSpec {
        short: Some('q'),
        long: "equexit",
        takes_value: true,
    },
    OptSpec {
        short: Some('r'),
        long: "no-rerun",
        takes_value: false,
    },
    OptSpec {
        short: Some('t'),
        long: "no-title",
        takes_value: false,
    },
    OptSpec {
        short: Some('w'),
        long: "no-wrap",
        takes_value: false,
    },
    OptSpec {
        short: Some('x'),
        long: "exec",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "count",
        takes_value: true,
    },
];

struct Config {
    interval: f64,
    differences: bool,
    differences_permanent: bool,
    no_title: bool,
    beep: bool,
    _color: bool,
    no_color: bool,
    errexit: bool,
    chgexit: bool,
    precise: bool,
    exec: bool,
    no_wrap: bool,
    equexit: usize,
    count: usize,
    argv: Vec<String>,
    command_display: String,
}

fn parse_interval(text: &str) -> Option<f64> {
    let cleaned = text.replace(',', ".");
    if cleaned.is_empty() {
        return None;
    }
    let val: f64 = cleaned.parse().ok()?;
    if !val.is_finite() {
        return None;
    }
    Some(if val < 0.1 { 0.1 } else { val })
}

fn strip_ansi(text: &str) -> String {
    let bytes = text.as_bytes();
    let mut out = Vec::with_capacity(bytes.len());
    let mut i = 0usize;
    while i < bytes.len() {
        if bytes[i] == 0x1b && i + 1 < bytes.len() && bytes[i + 1] == b'[' {
            let mut j = i + 2;
            while j < bytes.len() && (0x20..=0x3f).contains(&bytes[j]) {
                j += 1;
            }
            if j < bytes.len() && (0x40..=0x7e).contains(&bytes[j]) {
                j += 1;
            }
            i = j;
            continue;
        }
        if bytes[i] == 0x1b && i + 1 < bytes.len() {
            i += 2;
            continue;
        }
        out.push(bytes[i]);
        i += 1;
    }
    String::from_utf8_lossy(&out).into_owned()
}

fn truncate_lines(text: &str, width: usize) -> String {
    let mut out = String::new();
    for (idx, line) in text.split('\n').enumerate() {
        if idx > 0 {
            out.push('\n');
        }
        let mut take = line.len().min(width);
        while take > 0 && !line.is_char_boundary(take) {
            take -= 1;
        }
        out.push_str(&line[..take]);
    }
    out
}

fn highlight_differences(current: &str, previous: &str) -> String {
    let cur_lines: Vec<&str> = current.lines().collect();
    let prev_lines: Vec<&str> = previous.lines().collect();
    let mut out = String::with_capacity(current.len() + cur_lines.len() * 16);
    for (i, line) in cur_lines.iter().enumerate() {
        let changed = i >= prev_lines.len() || *line != prev_lines[i];
        if changed {
            out.push_str("\x1b[1;4m");
            out.push_str(line);
            out.push_str("\x1b[0m");
        } else {
            out.push_str(line);
        }
        if i + 1 < cur_lines.len() {
            out.push('\n');
        }
    }
    out
}

fn header_text(cfg: &Config, ctx: &Ctx) -> String {
    let left = format!("Every {:.1}s: {}", cfg.interval, cfg.command_display);
    let host = winux_core::win32_env_get(ctx.env, "COMPUTERNAME")
        .or_else(|| winux_core::win32_env_get(ctx.env, "HOSTNAME"))
        .map(|v| v.to_string_lossy().into_owned())
        .filter(|s| !s.is_empty())
        .unwrap_or_else(|| "localhost".to_owned());
    let secs = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0);
    let right = format!("{host}: {secs}");
    let width = 80usize;
    if left.len() + 1 + right.len() <= width {
        format!("{left}{}{right}", " ".repeat(width - left.len() - right.len()))
    } else if right.len() + 1 < width {
        format!("{} {right}", &left[..width - right.len() - 1])
    } else {
        left.chars().take(width).collect()
    }
}

struct RunOnce {
    output: String,
    status: u32,
    management_error: bool,
}

fn run_via_shell(cfg: &Config) -> RunOnce {
    let mut cmd = if cfg!(windows) {
        let mut c = Command::new("cmd.exe");
        c.args(["/c", &cfg.command_display]);
        c
    } else {
        let mut c = Command::new("sh");
        c.args(["-c", &cfg.command_display]);
        c
    };
    match cmd.output() {
        Ok(out) => {
            let mut combined = String::from_utf8_lossy(&out.stdout).into_owned();
            if combined.is_empty() && !out.stderr.is_empty() {
                combined = String::from_utf8_lossy(&out.stderr).into_owned();
            }
            RunOnce {
                output: combined,
                status: out.status.code().unwrap_or(1) as u32,
                management_error: false,
            }
        }
        Err(_) => RunOnce {
            output: String::new(),
            status: 1,
            management_error: true,
        },
    }
}

fn run_via_exec(cfg: &Config, ctx: &Ctx) -> RunOnce {
    let cmd_name = &cfg.argv[0];
    let cmd_args: Vec<String> = cfg.argv[1..].to_vec();
    let mut cmd = match resolve_child(cmd_name, ctx) {
        ChildResolve::Found(launch) => build_child_command(&launch, &cmd_args, ctx),
        ChildResolve::NotFound => {
            return RunOnce {
                output: String::new(),
                status: 127,
                management_error: false,
            }
        }
        ChildResolve::BadFormat
        | ChildResolve::NotExecutable
        | ChildResolve::NotADirectory => {
            return RunOnce {
                output: String::new(),
                status: 126,
                management_error: false,
            }
        }
    };
    match cmd.output() {
        Ok(out) => {
            let mut combined = String::from_utf8_lossy(&out.stdout).into_owned();
            combined.push_str(&String::from_utf8_lossy(&out.stderr));
            RunOnce {
                output: combined,
                status: out.status.code().unwrap_or(1) as u32,
                management_error: false,
            }
        }
        Err(_) => RunOnce {
            output: String::new(),
            status: 126,
            management_error: false,
        },
    }
}

fn split_at_first_positional(args: &[OsString]) -> usize {
    let mut i = 0usize;
    while i < args.len() {
        let s = args[i].to_string_lossy();
        if s == "--" {
            return args.len();
        }
        if s.starts_with("--") {
            let takes_sep_val =
                !s.contains('=') && matches!(s.as_ref(), "--interval" | "--equexit" | "--count");
            i += if takes_sep_val { 2 } else { 1 };
            continue;
        }
        if s.starts_with('-') && s.len() > 1 {
            let mut consumes_next = false;
            let bytes = s.as_bytes();
            for idx in 1..bytes.len() {
                if matches!(bytes[idx], b'n' | b'q') {
                    if idx + 1 == bytes.len() {
                        consumes_next = true;
                    }
                    break;
                }
            }
            i += if consumes_next { 2 } else { 1 };
            continue;
        }
        return i;
    }
    args.len()
}

pub fn watch_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    // Normalize `--differences=X` / `-d=X` before argopt::parse
    let mut perm_diff = false;
    let mut cleaned: Vec<OsString> = Vec::with_capacity(args.len());
    let split = split_at_first_positional(args).min(args.len());
    for a in &args[..split] {
        let s = a.to_string_lossy();
        if let Some(rest) = s.strip_prefix("--differences=") {
            if !rest.is_empty() {
                perm_diff = true;
            }
            cleaned.push(OsString::from("--differences"));
        } else if let Some(rest) = s.strip_prefix("-d=") {
            if !rest.is_empty() {
                perm_diff = true;
            }
            cleaned.push(OsString::from("-d"));
        } else {
            cleaned.push(a.clone());
        }
    }

    let mut p = match argopt::parse("watch", &cleaned, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(rc) => return Ok(rc),
    };
    p.positionals.extend_from_slice(&args[split..]);

    if let Some(which) = p
        .flags
        .iter()
        .find(|f| f.as_str() == argopt::HELP || f.as_str() == argopt::VERSION)
    {
        if which == argopt::HELP {
            writeln!(
                io.stdout,
                "Usage: watch [OPTION]... COMMAND\nExecute a program periodically, showing output fullscreen."
            )?;
        } else {
            writeln!(io.stdout, "watch (WinuxCmd) 0.2.0")?;
        }
        return Ok(0);
    }

    let mut interval = 2.0f64;
    if let Some(env_val) = winux_core::win32_env_get(ctx.env, "WATCH_INTERVAL") {
        if let Some(v) = parse_interval(&env_val.to_string_lossy()) {
            interval = v;
        }
    }
    if let Some(raw) = p.value_of("interval") {
        let Some(v) = parse_interval(raw) else {
            writeln!(io.stderr, "watch: invalid interval: '{raw}'")?;
            return Ok(1);
        };
        interval = v;
    }

    let equexit = if let Some(raw) = p.value_of("equexit") {
        let Ok(c) = raw.parse::<usize>() else {
            writeln!(io.stderr, "watch: invalid equexit cycle count: '{raw}'")?;
            return Ok(1);
        };
        if c < 1 {
            writeln!(io.stderr, "watch: invalid equexit cycle count: '{raw}'")?;
            return Ok(1);
        }
        c
    } else {
        0
    };

    let count = if let Some(raw) = p.value_of("count") {
        let Ok(c) = raw.parse::<isize>() else {
            writeln!(io.stderr, "watch: invalid count: '{raw}'")?;
            return Ok(1);
        };
        if c < 0 {
            writeln!(io.stderr, "watch: count cannot be negative")?;
            return Ok(1);
        }
        c as usize
    } else {
        0
    };

    if p.positionals.is_empty() {
        writeln!(io.stderr, "watch: missing command to watch")?;
        return Ok(1);
    }

    let argv: Vec<String> = p
        .positionals
        .iter()
        .map(|a| a.to_string_lossy().into_owned())
        .collect();
    let command_display = argv.join(" ");

    let cfg = Config {
        interval,
        differences: p.has("differences") || perm_diff,
        differences_permanent: perm_diff,
        no_title: p.has("no-title"),
        beep: p.has("beep"),
        _color: p.has("color"),
        no_color: p.has("no-color"),
        errexit: p.has("errexit"),
        chgexit: p.has("chgexit"),
        precise: p.has("precise"),
        exec: p.has("exec"),
        no_wrap: p.has("no-wrap"),
        equexit,
        count,
        argv,
        command_display,
    };

    let mut last_output = String::new();
    let mut diff_baseline = String::new();
    let mut have_baseline = false;
    let mut unchanged_streak = 0usize;
    let mut iteration = 0usize;

    loop {
        let started = Instant::now();
        let once = if cfg.exec {
            run_via_exec(&cfg, ctx)
        } else {
            run_via_shell(&cfg)
        };
        iteration += 1;
        if once.management_error {
            return Ok(2);
        }

        let mut output = once.output.clone();
        if cfg.no_color {
            output = strip_ansi(&output);
        }
        if cfg.no_wrap {
            output = truncate_lines(&output, 80);
        }

        if !cfg.no_title {
            writeln!(io.stdout, "{}", header_text(&cfg, ctx))?;
            writeln!(io.stdout)?;
        }
        if cfg.differences && have_baseline {
            write!(io.stdout, "{}", highlight_differences(&output, &diff_baseline))?;
        } else {
            write!(io.stdout, "{output}")?;
        }
        if cfg.beep && once.status != 0 {
            write!(io.stdout, "\x07")?;
        }

        let changed = have_baseline && once.output != last_output;
        let unchanged = have_baseline && once.output == last_output;
        if cfg.errexit && once.status != 0 {
            return Ok(once.status);
        }
        if cfg.chgexit && changed {
            return Ok(0);
        }
        if cfg.equexit > 0 {
            if unchanged {
                unchanged_streak += 1;
                if unchanged_streak >= cfg.equexit {
                    return Ok(0);
                }
            } else {
                unchanged_streak = 0;
            }
        }

        last_output = once.output.clone();
        if !have_baseline || !cfg.differences_permanent {
            diff_baseline = once.output;
        }
        have_baseline = true;

        if cfg.count > 0 && iteration >= cfg.count {
            return Ok(0);
        }

        let wait_secs = if cfg.precise {
            (cfg.interval - started.elapsed().as_secs_f64()).max(0.0)
        } else {
            cfg.interval
        };
        sleep(Duration::from_secs_f64(wait_secs));
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn validates_options_and_helpers() {
        assert_eq!(parse_interval("0"), Some(0.1));
        assert_eq!(parse_interval("1,5"), Some(1.5));
        assert_eq!(strip_ansi("\x1b[31mhi\x1b[0m"), "hi");
        assert_eq!(truncate_lines("abcdef\nxy", 3), "abc\nxy");

        let sc = ScratchCtx::new("watch-err");
        let mut io = MemIo::new("");
        let rc = watch_cmd(&[], &mut io.split(), &sc.ctx()).unwrap();
        assert_eq!(rc, 1);
        assert!(io.err().contains("missing command to watch"));
    }
}
