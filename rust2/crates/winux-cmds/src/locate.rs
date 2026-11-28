//! `locate` — find files by name in a filename database (#1488).
//!
//! Port of `src/commands/locate.cpp` (GNU findutils 4.10 option set +
//! plain-line database format). Explicit `Io`/`Ctx`, zero globals.

use std::ffi::OsString;
use std::io::{Read, Write};
use std::path::Path;

use crate::argopt::{self, OptSpec};
use crate::bre;
use crate::fsutil;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('0'),
        long: "null",
        takes_value: false,
    },
    OptSpec {
        short: Some('A'),
        long: "all",
        takes_value: false,
    },
    OptSpec {
        short: Some('b'),
        long: "basename",
        takes_value: false,
    },
    OptSpec {
        short: Some('c'),
        long: "count",
        takes_value: false,
    },
    OptSpec {
        short: Some('d'),
        long: "database",
        takes_value: true,
    },
    OptSpec {
        short: Some('e'),
        long: "existing",
        takes_value: false,
    },
    OptSpec {
        short: Some('E'),
        long: "non-existing",
        takes_value: false,
    },
    OptSpec {
        short: Some('i'),
        long: "ignore-case",
        takes_value: false,
    },
    OptSpec {
        short: Some('l'),
        long: "limit",
        takes_value: true,
    },
    OptSpec {
        short: Some('L'),
        long: "follow",
        takes_value: false,
    },
    OptSpec {
        short: Some('m'),
        long: "mmap",
        takes_value: false,
    },
    OptSpec {
        short: Some('P'),
        long: "nofollow",
        takes_value: false,
    },
    OptSpec {
        short: Some('p'),
        long: "print",
        takes_value: false,
    },
    OptSpec {
        short: Some('r'),
        long: "regex",
        takes_value: false,
    },
    OptSpec {
        short: Some('S'),
        long: "statistics",
        takes_value: false,
    },
    OptSpec {
        short: Some('s'),
        long: "stdio",
        takes_value: false,
    },
    OptSpec {
        short: Some('w'),
        long: "wholename",
        takes_value: false,
    },
];

fn default_database(ctx: &Ctx) -> String {
    if let Some(val) = ctx.env.get("LOCALAPPDATA") {
        let s = val.to_string_lossy();
        if !s.is_empty() {
            return format!("{s}/WinuxCmd/locate.db");
        }
    }
    "locate.db".to_owned()
}

fn split_db_lines(data: &str) -> Vec<String> {
    data.split('\n')
        .map(|l| l.strip_suffix('\r').unwrap_or(l))
        .filter(|l| !l.is_empty())
        .map(str::to_owned)
        .collect()
}

fn basename_of(path: &str) -> &str {
    path.rsplit(['/', '\\']).next().unwrap_or(path)
}

fn contains_metacharacter(s: &str) -> bool {
    s.bytes()
        .any(|b| matches!(b, b'*' | b'?' | b'[' | b']' | b'\\'))
}

fn fnmatch_no_pathname(pat: &[u8], text: &[u8], case_sensitive: bool) -> bool {
    if pat.is_empty() {
        return text.is_empty();
    }
    match pat[0] {
        b'*' => {
            for skip in 0..=text.len() {
                if fnmatch_no_pathname(&pat[1..], &text[skip..], case_sensitive) {
                    return true;
                }
            }
            false
        }
        b'?' => !text.is_empty() && fnmatch_no_pathname(&pat[1..], &text[1..], case_sensitive),
        b'\\' if pat.len() > 1 => {
            if text.is_empty() {
                return false;
            }
            let eq = if case_sensitive {
                pat[1] == text[0]
            } else {
                pat[1].eq_ignore_ascii_case(&text[0])
            };
            eq && fnmatch_no_pathname(&pat[2..], &text[1..], case_sensitive)
        }
        ch => {
            if text.is_empty() {
                return false;
            }
            let eq = if case_sensitive {
                ch == text[0]
            } else {
                ch.eq_ignore_ascii_case(&text[0])
            };
            eq && fnmatch_no_pathname(&pat[1..], &text[1..], case_sensitive)
        }
    }
}

pub fn locate_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("locate", args, OPTS, io, ctx, 1) {
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
                "Usage: locate [OPTION]... PATTERN...\nSearch a database of file paths for matching names."
            )?;
        } else {
            writeln!(io.stdout, "locate (WinuxCmd) 0.2.0")?;
        }
        return Ok(0);
    }

    let null_sep = p.has("null");
    let all = p.has("all");
    let count = p.has("count");
    let existing = p.has("existing");
    let non_existing = p.has("non-existing");
    let ignore_case = p.has("ignore-case");
    let use_regex = p.has("regex");
    let statistics = p.has("statistics");
    let basename = p.flag_last(&["basename", "wholename"]) == Some("basename");

    let limit: Option<u64> = if let Some(raw) = p.value_of("limit") {
        if raw.starts_with('-') {
            writeln!(io.stderr, "locate: invalid negative limit")?;
            return Ok(1);
        }
        match raw.parse::<u64>() {
            Ok(v) => Some(v),
            Err(_) => {
                writeln!(io.stderr, "locate: invalid argument '{raw}' for '--limit'")?;
                return Ok(1);
            }
        }
    } else {
        None
    };

    let patterns: Vec<String> = p
        .positionals
        .iter()
        .map(|a| a.to_string_lossy().into_owned())
        .collect();

    if patterns.is_empty() && !statistics {
        writeln!(io.stderr, "locate: missing operand")?;
        writeln!(io.stderr, "Try 'locate --help' for more information.")?;
        return Ok(1);
    }

    let mut regexes = Vec::new();
    if use_regex {
        for pat in &patterns {
            let compiled_pat = if ignore_case {
                pat.to_ascii_lowercase()
            } else {
                pat.clone()
            };
            match bre::Regex::compile(&compiled_pat) {
                Ok(re) => regexes.push(re),
                Err(_) => {
                    writeln!(io.stderr, "locate: invalid regular expression")?;
                    return Ok(1);
                }
            }
        }
    }

    if limit == Some(0) {
        if count {
            writeln!(io.stdout, "0")?;
        }
        return Ok(0);
    }

    let sep = if cfg!(windows) { ';' } else { ':' };
    let (user_selected, elements): (bool, Vec<String>) = if let Some(db) = p.value_of("database") {
        (
            true,
            db.split(sep)
                .map(|e| {
                    if e.is_empty() || e == "." {
                        default_database(ctx)
                    } else {
                        e.to_owned()
                    }
                })
                .collect(),
        )
    } else if let Some(env) = ctx.env.get("LOCATE_PATH") {
        let spec = env.to_string_lossy();
        (
            true,
            spec.split(sep)
                .map(|e| {
                    if e.is_empty() || e == "." {
                        default_database(ctx)
                    } else {
                        e.to_owned()
                    }
                })
                .collect(),
        )
    } else {
        (false, vec![default_database(ctx)])
    };

    let mut accepted = 0u64;
    let mut did_stdin = false;
    let mut limit_reached = false;
    let mut output = String::new();

    for element in elements {
        if limit_reached {
            break;
        }
        let db_lines = if element == "-" {
            if did_stdin {
                writeln!(
                    io.stderr,
                    "locate: warning: the locate database can only be read from stdin once."
                )?;
                return Ok(0);
            }
            did_stdin = true;
            let mut buf = String::new();
            let _ = io.stdin.read_to_string(&mut buf);
            split_db_lines(&buf)
        } else {
            let resolved = fsutil::resolve(ctx, Path::new(&element));
            match std::fs::read_to_string(&resolved) {
                Ok(data) => split_db_lines(&data),
                Err(e) => {
                    if user_selected {
                        writeln!(io.stderr, "locate: '{element}': {}", fsutil::reason(&e))?;
                        return Ok(0);
                    }
                    break;
                }
            }
        };

        if statistics {
            let label = if element == "-" {
                "<stdin>"
            } else {
                element.as_str()
            };
            writeln!(io.stdout, "Database: {label}")?;
            writeln!(io.stdout, "Entries: {}", db_lines.len())?;
        }

        for line in &db_lines {
            let target = if basename {
                basename_of(line)
            } else {
                line.as_str()
            };
            let matched = if patterns.is_empty() {
                true
            } else {
                let mut ok = all;
                for (idx, pat) in patterns.iter().enumerate() {
                    let m = if use_regex {
                        if ignore_case {
                            regexes[idx].matches_anywhere(target.to_ascii_lowercase().as_bytes())
                        } else {
                            regexes[idx].matches_anywhere(target.as_bytes())
                        }
                    } else if contains_metacharacter(pat) {
                        fnmatch_no_pathname(pat.as_bytes(), target.as_bytes(), !ignore_case)
                    } else if ignore_case {
                        target
                            .to_ascii_lowercase()
                            .contains(&pat.to_ascii_lowercase())
                    } else {
                        target.contains(pat.as_str())
                    };
                    if m && !all {
                        ok = true;
                        break;
                    }
                    if !m && all {
                        ok = false;
                        break;
                    }
                }
                ok
            };

            if !matched {
                continue;
            }
            if existing && !fsutil::resolve(ctx, Path::new(line)).exists() {
                continue;
            }
            if non_existing && fsutil::resolve(ctx, Path::new(line)).exists() {
                continue;
            }
            accepted += 1;
            if !count && !statistics {
                output.push_str(line);
                output.push(if null_sep { '\0' } else { '\n' });
            }
            if let Some(lim) = limit {
                if accepted >= lim {
                    limit_reached = true;
                    break;
                }
            }
        }
    }

    if count {
        writeln!(io.stdout, "{accepted}")?;
    } else if !statistics {
        write!(io.stdout, "{output}")?;
    }

    if statistics || accepted > 0 {
        Ok(0)
    } else {
        Ok(1)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn locates_substring_glob_and_count() {
        let sc = ScratchCtx::new("locate-test");
        std::fs::write(
            sc.path("test.db"),
            "C:/proj/src/main.rs\nC:/proj/docs/README.md\nC:/proj/tests/a.rs\n",
        )
        .unwrap();

        let mut io = MemIo::new("");
        let rc = locate_cmd(
            &[
                OsString::from("-d"),
                OsString::from("test.db"),
                OsString::from("*.rs"),
            ],
            &mut io.split(),
            &sc.ctx(),
        )
        .unwrap();
        assert_eq!(rc, 0);
        assert!(io.out().contains("main.rs"));
        assert!(io.out().contains("a.rs"));
        assert!(!io.out().contains("README.md"));

        let mut io_c = MemIo::new("");
        let rc_c = locate_cmd(
            &[
                OsString::from("-d"),
                OsString::from("test.db"),
                OsString::from("-i"),
                OsString::from("-c"),
                OsString::from("readme"),
            ],
            &mut io_c.split(),
            &sc.ctx(),
        )
        .unwrap();
        assert_eq!(rc_c, 0);
        assert_eq!(io_c.out(), "1\n");
    }
}
