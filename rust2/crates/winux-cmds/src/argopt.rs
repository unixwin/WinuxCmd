//! Minimal GNU-getopt-shaped option parsing shared by wave1 commands.
//!
//! GNU permutes options and operands by default (options may appear after
//! operands); `--` ends option processing. Short options accept bundled
//! values (`-sVAL`) and detached values (`-s VAL`); long options accept
//! `--long=VAL` and `--long VAL`. Unknown options produce the GNU-style
//! usage diagnostic and the command's usage status (1, or 2 for printenv).

use std::ffi::OsString;

use winux_core::{usage_error, Ctx, Io};

pub struct OptSpec {
    pub short: Option<char>,
    pub long: &'static str, // canonical long name without leading dashes
    pub takes_value: bool,
}

pub const HELP: &str = "help";
pub const VERSION: &str = "version";

#[derive(Default)]
pub struct Parsed {
    pub flags: Vec<String>,
    pub values: Vec<(String, String)>,
    pub positionals: Vec<OsString>,
}

impl Parsed {
    /// True when the option appeared, as a flag or with a value.
    pub fn has(&self, long: &str) -> bool {
        self.flags.iter().any(|f| f == long) || self.values.iter().any(|(k, _)| k == long)
    }

    pub fn value_of(&self, long: &str) -> Option<&str> {
        self.values
            .iter()
            .rev()
            .find(|(k, _)| k == long)
            .map(|(_, v)| v.as_str())
    }
}

fn long_by_name<'a>(opts: &'a [OptSpec], name: &str) -> Option<&'a OptSpec> {
    opts.iter().find(|o| o.long == name)
}

/// getopt_long unambiguous-abbreviation resolution: an exact match wins;
/// otherwise the unique long name starting with `name` is canonical.
/// Ambiguous and unknown prefixes yield the GNU diagnostics getopt_long
/// produces ("option '--x' is ambiguous" / "unrecognized option '--x'").
fn resolve_long<'a>(opts: &'a [OptSpec], name: &str) -> Result<&'a OptSpec, String> {
    if let Some(spec) = long_by_name(opts, name) {
        return Ok(spec);
    }
    let matches: Vec<&OptSpec> = opts.iter().filter(|o| o.long.starts_with(name)).collect();
    match matches.len() {
        1 => Ok(matches[0]),
        0 => Err(format!("unrecognized option '--{name}'")),
        _ => Err(format!("option '--{name}' is ambiguous")),
    }
}

fn short_by_char<'a>(opts: &'a [OptSpec], c: char) -> Option<&'a OptSpec> {
    opts.iter().find(|o| o.short == Some(c))
}

fn to_str_lossy(s: &OsString) -> String {
    s.to_string_lossy().into_owned()
}

/// Parse `args` (already stripped of argv[0]) against `opts`.
///
/// On unknown options, writes the GNU usage diagnostic to stderr and returns
/// `Err(status)`. `usage_status` is 1 for most coreutils, 2 for printenv.
/// `prog` is used verbatim in diagnostics (`basename`, `[`, ...).
pub fn parse(
    prog: &str,
    args: &[OsString],
    opts: &[OptSpec],
    io: &mut Io,
    _ctx: &Ctx,
    usage_status: u32,
) -> Result<Parsed, u32> {
    let mut out = Parsed::default();
    let mut i = 0usize;
    let mut no_more_options = false;

    while i < args.len() {
        let arg = to_str_lossy(&args[i]);
        if no_more_options {
            out.positionals.push(args[i].clone());
            i += 1;
            continue;
        }
        if arg == "--" {
            no_more_options = true;
            i += 1;
            continue;
        }
        if let Some(rest) = arg.strip_prefix("--") {
            let (name, inline_val) = match rest.split_once('=') {
                Some((n, v)) => (n.to_string(), Some(v.to_string())),
                None => (rest.to_string(), None),
            };
            let special = name == HELP || name == VERSION;
            let canonical = if special {
                name.clone()
            } else {
                match resolve_long(opts, &name) {
                    Ok(o) => o.long.to_string(),
                    Err(msg) => {
                        let _ = usage_error(io, prog, &msg);
                        return Err(usage_status);
                    }
                }
            };
            let takes_value = if special {
                false
            } else {
                long_by_name(opts, &canonical)
                    .map(|s| s.takes_value)
                    .unwrap_or(false)
            };
            if takes_value {
                let val = match inline_val {
                    Some(v) => v,
                    None => {
                        i += 1;
                        match args.get(i) {
                            Some(v) => to_str_lossy(v),
                            None => {
                                let msg = format!("option '--{}' requires an argument", canonical);
                                let _ = usage_error(io, prog, &msg);
                                return Err(usage_status);
                            }
                        }
                    }
                };
                out.values.push((canonical, val));
            } else {
                // getopt_long rejects an inline argument on a flag
                if inline_val.is_some() {
                    let msg = format!("option '--{}' doesn't allow an argument", canonical);
                    let _ = usage_error(io, prog, &msg);
                    return Err(usage_status);
                }
                out.flags.push(canonical);
            }
            i += 1;
            continue;
        }
        if arg.len() > 1 && arg.starts_with('-') {
            // short option cluster
            let chars: Vec<char> = arg.chars().skip(1).collect();
            let mut ci = 0usize;
            let mut consumed_next = false;
            while ci < chars.len() {
                let c = chars[ci];
                if c == 'h' && !short_by_char(opts, 'h').is_some() && chars.len() == 1 {
                    // `-h` alone maps to --help unless the command defines -h
                    out.flags.push(HELP.to_string());
                    ci += 1;
                    continue;
                }
                match short_by_char(opts, c) {
                    Some(spec) if spec.takes_value => {
                        let rest: String = chars[ci + 1..].iter().collect();
                        let val = if !rest.is_empty() {
                            rest
                        } else {
                            i += 1;
                            consumed_next = true;
                            match args.get(i) {
                                Some(v) => to_str_lossy(v),
                                None => {
                                    let msg = format!("option '-{}' requires an argument", c);
                                    let _ = usage_error(io, prog, &msg);
                                    return Err(usage_status);
                                }
                            }
                        };
                        out.values.push((spec.long.to_string(), val));
                        ci = chars.len();
                    }
                    Some(spec) => {
                        out.flags.push(spec.long.to_string());
                        ci += 1;
                    }
                    None => {
                        let msg = format!("invalid option -- '{c}'");
                        let _ = usage_error(io, prog, &msg);
                        return Err(usage_status);
                    }
                }
            }
            i += if consumed_next { 1 } else { 1 };
            continue;
        }
        // operand (GNU permutes: options may still follow)
        out.positionals.push(args[i].clone());
        i += 1;
    }
    Ok(out)
}
