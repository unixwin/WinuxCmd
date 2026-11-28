//! `pkill` — signal processes based on name and other attributes (#1488).

use std::ffi::OsString;
use std::io::Write;

use crate::argopt::{self, OptSpec};
use crate::procps_select::{self as sel, Criteria};
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec { short: Some('A'), long: "ignore-ancestors", takes_value: false },
    OptSpec { short: Some('c'), long: "count", takes_value: false },
    OptSpec { short: Some('e'), long: "echo", takes_value: false },
    OptSpec { short: Some('f'), long: "full", takes_value: false },
    OptSpec { short: Some('F'), long: "pidfile", takes_value: true },
    OptSpec { short: Some('G'), long: "group", takes_value: true },
    OptSpec { short: Some('i'), long: "ignore-case", takes_value: false },
    OptSpec { short: Some('L'), long: "logpidfile", takes_value: false },
    OptSpec { short: Some('n'), long: "newest", takes_value: false },
    OptSpec { short: Some('o'), long: "oldest", takes_value: false },
    OptSpec { short: Some('O'), long: "older", takes_value: true },
    OptSpec { short: Some('P'), long: "parent", takes_value: true },
    OptSpec { short: Some('q'), long: "queue", takes_value: true },
    OptSpec { short: Some('s'), long: "session", takes_value: true },
    OptSpec { short: Some('u'), long: "euid", takes_value: true },
    OptSpec { short: Some('U'), long: "uid", takes_value: true },
    OptSpec { short: Some('x'), long: "exact", takes_value: false },
    OptSpec { short: None, long: "signal", takes_value: true },
];

fn err_try(io: &mut Io, msg: &str, code: u32) -> CmdResult {
    writeln!(io.stderr, "{msg}")?;
    writeln!(io.stderr, "Try `pkill --help' for more information.")?;
    Ok(code)
}

pub fn pkill_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut cleaned: Vec<OsString> = Vec::with_capacity(args.len());
    let mut pre_signals: Vec<i32> = Vec::new();
    let mut after_dd = false;
    for a in args {
        let s = a.to_string_lossy();
        if after_dd {
            cleaned.push(a.clone());
            continue;
        }
        if s == "--" {
            after_dd = true;
            cleaned.push(a.clone());
            continue;
        }
        if s.starts_with('-') && s.len() > 1 && !s.starts_with("--") {
            let rest = &s[1..];
            if rest.bytes().all(|b| b.is_ascii_digit()) {
                match sel::signal_name_to_number(rest) {
                    Some(n) => {
                        pre_signals.push(n);
                        continue;
                    }
                    None => return err_try(io, &format!("pkill: invalid signal: '{rest}'"), 2),
                }
            }
            if rest.len() > 1 {
                if let Some(n) = sel::signal_name_to_number(rest) {
                    pre_signals.push(n);
                    continue;
                }
            }
        }
        cleaned.push(a.clone());
    }

    let p = match argopt::parse("pkill", &cleaned, OPTS, io, ctx, 2) {
        Ok(p) => p,
        Err(rc) => return Ok(rc),
    };
    if let Some(which) = p
        .flags
        .iter()
        .find(|f| f.as_str() == argopt::HELP || f.as_str() == argopt::VERSION)
    {
        if which == argopt::HELP {
            writeln!(
                io.stdout,
                "Usage: pkill [OPTION]... PATTERN\nSignal processes selected by name or command line."
            )?;
        } else {
            writeln!(io.stdout, "pkill (WinuxCmd) 0.2.0")?;
        }
        return Ok(0);
    }

    if pre_signals.len() > 1 {
        return err_try(io, "pkill: only one signal may be specified", 2);
    }
    let signal = if let Some(sig) = p.value_of("signal") {
        match sel::signal_name_to_number(sig) {
            Some(n) => n,
            None => return err_try(io, &format!("pkill: invalid signal: '{sig}'"), 2),
        }
    } else if let Some(&n) = pre_signals.first() {
        n
    } else {
        15
    };

    let no_count = p
        .flags
        .iter()
        .filter(|f| matches!(f.as_str(), "newest" | "oldest"))
        .count();
    if no_count > 1 {
        return err_try(io, "pkill: -n and -o are mutually exclusive", 2);
    }

    let mut euid_specs = Vec::new();
    let mut ruid_specs = Vec::new();
    let mut group_specs = Vec::new();
    let mut ppids = Vec::new();
    let mut sessions = Vec::new();
    let mut older_than = None;
    let mut pidfile_path: Option<String> = None;

    for (k, v) in &p.values {
        match k.as_str() {
            "euid" => match sel::split_list(v) {
                Some(items) => euid_specs.extend(items),
                None => return err_try(io, &format!("pkill: invalid user list: '{v}'"), 2),
            },
            "uid" => match sel::split_list(v) {
                Some(items) => ruid_specs.extend(items),
                None => return err_try(io, &format!("pkill: invalid user list: '{v}'"), 2),
            },
            "group" => match sel::split_list(v) {
                Some(items) => group_specs.extend(items),
                None => return err_try(io, &format!("pkill: invalid group list: '{v}'"), 2),
            },
            "parent" => {
                if !sel::parse_u32_list(v, &mut ppids) {
                    return err_try(io, &format!("pkill: invalid parent list: '{v}'"), 2);
                }
            }
            "session" => {
                if !sel::parse_u32_list(v, &mut sessions) {
                    return err_try(io, &format!("pkill: invalid session list: '{v}'"), 2);
                }
            }
            "older" => {
                let digits: String = v
                    .chars()
                    .take_while(|c| c.is_ascii_digit() || *c == '-')
                    .collect();
                older_than = Some(digits.parse::<i64>().unwrap_or(0));
            }
            "pidfile" => pidfile_path = Some(v.clone()),
            _ => {}
        }
    }

    if let Err(bad) = sel::validate_account_specs(&euid_specs) {
        return err_try(io, &format!("pkill: invalid user name: {bad}"), 2);
    }
    if let Err(bad) = sel::validate_account_specs(&ruid_specs) {
        return err_try(io, &format!("pkill: invalid user name: {bad}"), 2);
    }
    if let Err(bad) = sel::validate_account_specs(&group_specs) {
        return err_try(io, &format!("pkill: invalid group name: {bad}"), 2);
    }

    if sessions.contains(&0) {
        if let Some(own) = sel::sys::own_session_id() {
            for s in &mut sessions {
                if *s == 0 {
                    *s = own;
                }
            }
        }
    }

    if p.positionals.len() > 1 {
        return err_try(io, "pkill: only one pattern can be provided", 2);
    }

    let exact = p.has("exact");
    let ignore_case = p.has("ignore-case");
    let full = p.has("full");
    let count = p.has("count");
    let echo = p.has("echo");
    let logpidfile = p.has("logpidfile");

    if logpidfile && pidfile_path.is_none() {
        return err_try(io, "pkill: -L without -F makes no sense", 2);
    }

    let pidfile_pid = if let Some(ref pf) = pidfile_path {
        match sel::read_pidfile(pf, logpidfile, io, ctx) {
            Some(pid) => Some(pid),
            None => return err_try(io, "pkill: pidfile not valid", 1),
        }
    } else {
        None
    };

    let (has_pattern, pattern_text, pattern) = if let Some(arg) = p.positionals.first() {
        let text = arg.to_string_lossy().into_owned();
        match sel::compile_pattern(&text, exact, ignore_case) {
            Ok(re) => (true, text, Some(re)),
            Err(_) => return err_try(io, "pkill: invalid regular expression", 2),
        }
    } else {
        (false, String::new(), None)
    };

    let crit = Criteria {
        has_pattern,
        pattern_text,
        pattern,
        exact,
        ignore_case,
        full,
        euid_specs,
        ruid_specs,
        group_specs,
        ppids,
        sessions,
        pidfile_pid,
        newest: p.has("newest"),
        oldest: p.has("oldest"),
        older_than,
        ignore_ancestors: p.has("ignore-ancestors"),
        negate: false,
    };

    if !crit.has_criteria() {
        return err_try(io, "pkill: no matching criteria specified", 2);
    }

    let processes = sel::sys::enumerate_processes(crit.full);
    let selected = sel::select_processes(&crit, &processes, sel::sys::now_filetime());

    if selected.is_empty() && crit.has_pattern && !crit.full && sel::is_long_match(&crit.pattern_text) {
        writeln!(
            io.stderr,
            "pkill: pattern that searches for process name longer than 15 characters will result in zero matches"
        )?;
        writeln!(
            io.stderr,
            "Try `pkill -f' option to match against the complete command line."
        )?;
    }

    let mut kill_count = 0usize;
    for s in &selected {
        let proc_info = &s.proc_info;
        let res = if signal == 0 {
            if sel::sys::check_process(proc_info.pid) {
                Ok(())
            } else {
                Err((1, "No such process".to_owned()))
            }
        } else {
            sel::sys::terminate_process(proc_info.pid)
        };
        match res {
            Ok(()) => {
                if echo {
                    writeln!(io.stdout, "{} killed (pid {})", proc_info.name, proc_info.pid)?;
                }
                kill_count += 1;
            }
            Err((_, msg)) => {
                writeln!(io.stderr, "pkill: killing pid {} failed: {msg}", proc_info.pid)?;
            }
        }
    }
    if count {
        writeln!(io.stdout, "{}", selected.len())?;
    }

    Ok(if kill_count > 0 { 0 } else { 1 })
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn no_criteria_and_invalid_signal() {
        let sc = ScratchCtx::new("pkill-err");
        let mut io = MemIo::new("");
        let rc = pkill_cmd(&[], &mut io.split(), &sc.ctx()).unwrap();
        assert_eq!(rc, 2);
        assert!(io.err().contains("no matching criteria"));

        let mut io2 = MemIo::new("");
        let rc2 = pkill_cmd(
            &[OsString::from("--signal=NOPE"), OsString::from("foo")],
            &mut io2.split(),
            &sc.ctx(),
        )
        .unwrap();
        assert_eq!(rc2, 2);
        assert!(io2.err().contains("invalid signal"));
    }
}
