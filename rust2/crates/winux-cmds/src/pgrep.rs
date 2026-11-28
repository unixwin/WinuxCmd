//! `pgrep` — look up processes based on name and other attributes (#1488).

use std::ffi::OsString;
use std::io::Write;

use crate::argopt::{self, OptSpec};
use crate::procps_select::{self as sel, Criteria};
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec { short: Some('a'), long: "list-full", takes_value: false },
    OptSpec { short: Some('A'), long: "ignore-ancestors", takes_value: false },
    OptSpec { short: Some('c'), long: "count", takes_value: false },
    OptSpec { short: Some('d'), long: "delimiter", takes_value: true },
    OptSpec { short: Some('f'), long: "full", takes_value: false },
    OptSpec { short: Some('F'), long: "pidfile", takes_value: true },
    OptSpec { short: Some('G'), long: "group", takes_value: true },
    OptSpec { short: Some('i'), long: "ignore-case", takes_value: false },
    OptSpec { short: Some('l'), long: "list-name", takes_value: false },
    OptSpec { short: Some('L'), long: "logpidfile", takes_value: false },
    OptSpec { short: Some('n'), long: "newest", takes_value: false },
    OptSpec { short: Some('o'), long: "oldest", takes_value: false },
    OptSpec { short: Some('O'), long: "older", takes_value: true },
    OptSpec { short: Some('P'), long: "parent", takes_value: true },
    OptSpec { short: Some('s'), long: "session", takes_value: true },
    OptSpec { short: Some('u'), long: "euid", takes_value: true },
    OptSpec { short: Some('U'), long: "uid", takes_value: true },
    OptSpec { short: Some('v'), long: "inverse", takes_value: false },
    OptSpec { short: Some('w'), long: "lightweight", takes_value: false },
    OptSpec { short: Some('x'), long: "exact", takes_value: false },
    OptSpec { short: None, long: "signal", takes_value: true },
];

fn err_try(io: &mut Io, msg: &str, code: u32) -> CmdResult {
    writeln!(io.stderr, "{msg}")?;
    writeln!(io.stderr, "Try `pgrep --help' for more information.")?;
    Ok(code)
}

pub fn pgrep_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("pgrep", args, OPTS, io, ctx, 2) {
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
                "Usage: pgrep [OPTION]... PATTERN\nLook up processes by name or command line."
            )?;
        } else {
            writeln!(io.stdout, "pgrep (WinuxCmd) 0.2.0")?;
        }
        return Ok(0);
    }

    let nov_count = p
        .flags
        .iter()
        .filter(|f| matches!(f.as_str(), "newest" | "oldest" | "inverse"))
        .count();
    if nov_count > 1 {
        return err_try(io, "pgrep: -n, -o and -v are mutually exclusive", 2);
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
                None => return err_try(io, &format!("pgrep: invalid user list: '{v}'"), 2),
            },
            "uid" => match sel::split_list(v) {
                Some(items) => ruid_specs.extend(items),
                None => return err_try(io, &format!("pgrep: invalid user list: '{v}'"), 2),
            },
            "group" => match sel::split_list(v) {
                Some(items) => group_specs.extend(items),
                None => return err_try(io, &format!("pgrep: invalid group list: '{v}'"), 2),
            },
            "parent" => {
                if !sel::parse_u32_list(v, &mut ppids) {
                    return err_try(io, &format!("pgrep: invalid parent list: '{v}'"), 2);
                }
            }
            "session" => {
                if !sel::parse_u32_list(v, &mut sessions) {
                    return err_try(io, &format!("pgrep: invalid session list: '{v}'"), 2);
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
            "signal" => {
                if sel::signal_name_to_number(v).is_none() {
                    return err_try(io, &format!("pgrep: invalid signal: '{v}'"), 2);
                }
            }
            _ => {}
        }
    }

    if let Err(bad) = sel::validate_account_specs(&euid_specs) {
        return err_try(io, &format!("pgrep: invalid user name: {bad}"), 2);
    }
    if let Err(bad) = sel::validate_account_specs(&ruid_specs) {
        return err_try(io, &format!("pgrep: invalid user name: {bad}"), 2);
    }
    if let Err(bad) = sel::validate_account_specs(&group_specs) {
        return err_try(io, &format!("pgrep: invalid group name: {bad}"), 2);
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
        return err_try(io, "pgrep: only one pattern can be provided", 2);
    }

    let exact = p.has("exact");
    let ignore_case = p.has("ignore-case");
    let full = p.has("full");
    let list_name = p.has("list-name");
    let list_full = p.has("list-full");
    let count = p.has("count");
    let threads = p.has("lightweight");
    let logpidfile = p.has("logpidfile");
    let delimiter = p.value_of("delimiter").unwrap_or("\n");

    if logpidfile && pidfile_path.is_none() {
        return err_try(io, "pgrep: -L without -F makes no sense", 2);
    }

    let pidfile_pid = if let Some(ref pf) = pidfile_path {
        match sel::read_pidfile(pf, logpidfile, io, ctx) {
            Some(pid) => Some(pid),
            None => return err_try(io, "pgrep: pidfile not valid", 1),
        }
    } else {
        None
    };

    let (has_pattern, pattern_text, pattern) = if let Some(arg) = p.positionals.first() {
        let text = arg.to_string_lossy().into_owned();
        match sel::compile_pattern(&text, exact, ignore_case) {
            Ok(re) => (true, text, Some(re)),
            Err(_) => return err_try(io, "pgrep: invalid regular expression", 2),
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
        negate: p.has("inverse"),
    };

    if !crit.has_criteria() {
        return err_try(io, "pgrep: no matching criteria specified", 2);
    }

    let processes = sel::sys::enumerate_processes(crit.full || list_full);
    let selected = sel::select_processes(&crit, &processes, sel::sys::now_filetime());

    if selected.is_empty() && crit.has_pattern && !crit.full && sel::is_long_match(&crit.pattern_text) {
        writeln!(
            io.stderr,
            "pgrep: pattern that searches for process name longer than 15 characters will result in zero matches"
        )?;
        writeln!(
            io.stderr,
            "Try `pgrep -f' option to match against the complete command line."
        )?;
    }

    let matched = !selected.is_empty();
    if count {
        let total = if threads {
            selected
                .iter()
                .map(|s| sel::sys::process_thread_ids(s.proc_info.pid).len())
                .sum()
        } else {
            selected.len()
        };
        writeln!(io.stdout, "{total}")?;
        return Ok(if matched { 0 } else { 1 });
    }

    if threads {
        let mut rows = Vec::new();
        for s in &selected {
            for tid in sel::sys::process_thread_ids(s.proc_info.pid) {
                rows.push((tid, &s.proc_info));
            }
        }
        for (i, (tid, proc_info)) in rows.iter().enumerate() {
            let mut text = tid.to_string();
            if list_full {
                text.push(' ');
                text.push_str(&proc_info.command_line);
            } else if list_name {
                text.push(' ');
                text.push_str(&proc_info.name);
            }
            write!(
                io.stdout,
                "{text}{}",
                if i + 1 == rows.len() { "\n" } else { delimiter }
            )?;
        }
    } else {
        for (i, s) in selected.iter().enumerate() {
            let mut text = s.proc_info.pid.to_string();
            if list_full {
                text.push(' ');
                text.push_str(&s.proc_info.command_line);
            } else if list_name {
                text.push(' ');
                text.push_str(&s.proc_info.name);
            }
            write!(
                io.stdout,
                "{text}{}",
                if i + 1 == selected.len() { "\n" } else { delimiter }
            )?;
        }
    }

    Ok(if matched { 0 } else { 1 })
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn no_criteria_and_mutually_exclusive_flags() {
        let sc = ScratchCtx::new("pgrep-err");
        let mut io = MemIo::new("");
        let rc = pgrep_cmd(&[], &mut io.split(), &sc.ctx()).unwrap();
        assert_eq!(rc, 2);
        assert!(io.err().contains("no matching criteria"));

        let mut io2 = MemIo::new("");
        let rc2 = pgrep_cmd(
            &[OsString::from("-n"), OsString::from("-o"), OsString::from("foo")],
            &mut io2.split(),
            &sc.ctx(),
        )
        .unwrap();
        assert_eq!(rc2, 2);
        assert!(io2.err().contains("mutually exclusive"));
    }
}
