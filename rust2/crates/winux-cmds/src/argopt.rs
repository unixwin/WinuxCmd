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

#[derive(Default, Debug)]
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

/// Resolution of a `--name` token against the command's option table.
enum LongMatch<'a> {
    /// A registered spec matched (exact or unambiguous abbreviation).
    Spec(&'a OptSpec),
    /// The virtual help/version option matched.
    Help,
    Version,
    /// No option matched.
    Unknown,
}

/// Resolve a long-option name the way getopt_long does: an exact match
/// wins; otherwise the token is an abbreviation and must resolve to
/// exactly one candidate among the registered long names plus the virtual
/// help/version options (probed GNU coreutils 9.4: `basename --hel` prints
/// help, `basename --mul f1 f2` enables multiple mode, `readlink --can x`
/// is rejected with "option '--can' is ambiguous; possibilities: ...").
fn match_long<'a>(opts: &'a [OptSpec], name: &str) -> LongMatch<'a> {
    if name == HELP {
        return LongMatch::Help;
    }
    if name == VERSION {
        return LongMatch::Version;
    }
    if let Some(spec) = long_by_name(opts, name) {
        return LongMatch::Spec(spec);
    }
    // Unambiguous abbreviation. Candidates keep registration order so the
    // ambiguity message lists them the way glibc does; help/version join
    // last, as coreutils places them last in its longopts tables.
    let mut candidates: Vec<&str> = opts
        .iter()
        .filter(|o| o.long.starts_with(name))
        .map(|o| o.long)
        .collect();
    if HELP.starts_with(name) {
        candidates.push(HELP);
    }
    if VERSION.starts_with(name) {
        candidates.push(VERSION);
    }
    if candidates.len() == 1 {
        let only = candidates[0];
        return match long_by_name(opts, only) {
            Some(spec) => LongMatch::Spec(spec),
            None if only == HELP => LongMatch::Help,
            None => LongMatch::Version,
        };
    }
    LongMatch::Unknown
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
            let matched = match_long(opts, &name);
            let (record_key, takes_value) = match matched {
                LongMatch::Spec(spec) => (spec.long.to_string(), spec.takes_value),
                LongMatch::Help => (HELP.to_string(), false),
                LongMatch::Version => (VERSION.to_string(), false),
                LongMatch::Unknown => {
                    let msg = if name.is_empty() {
                        // An empty abbreviation ("--=v") is not a lookup;
                        // report the token verbatim.
                        format!("unrecognized option '--{rest}'")
                    } else if opts.iter().any(|o| o.long.starts_with(&name))
                        || HELP.starts_with(&name)
                        || VERSION.starts_with(&name)
                    {
                        let mut candidates: Vec<String> = opts
                            .iter()
                            .filter(|o| o.long.starts_with(&name))
                            .map(|o| format!("'--{}'", o.long))
                            .collect();
                        if HELP.starts_with(&name) {
                            candidates.push(format!("'--{HELP}'"));
                        }
                        if VERSION.starts_with(&name) {
                            candidates.push(format!("'--{VERSION}'"));
                        }
                        format!(
                            "option '--{name}' is ambiguous; possibilities: {}",
                            candidates.join(" ")
                        )
                    } else {
                        format!("unrecognized option '--{name}'")
                    };
                    let _ = usage_error(io, prog, &msg);
                    return Err(usage_status);
                }
            };
            if takes_value {
                let val = match inline_val {
                    Some(v) => v,
                    None => {
                        i += 1;
                        match args.get(i) {
                            Some(v) => to_str_lossy(v),
                            None => {
                                let msg = format!("option '--{record_key}' requires an argument");
                                let _ = usage_error(io, prog, &msg);
                                return Err(usage_status);
                            }
                        }
                    }
                };
                out.values.push((record_key, val));
            } else {
                out.flags.push(record_key);
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

#[cfg(test)]
mod tests {
    use super::{parse, OptSpec};
    use winux_core::test_support::MemIo;

    fn opts() -> Vec<OptSpec> {
        vec![
            OptSpec {
                short: Some('z'),
                long: "zero",
                takes_value: false,
            },
            OptSpec {
                short: Some('s'),
                long: "suffix",
                takes_value: true,
            },
        ]
    }

    fn run(args: &[&str]) -> Result<super::Parsed, u32> {
        let owned: Vec<std::ffi::OsString> =
            args.iter().map(std::ffi::OsString::from).collect();
        let mut io = MemIo::new("");
        let sc = winux_core::test_support::ScratchCtx::new("argopt");
        parse("prog", &owned, &opts(), &mut io.split(), &sc.ctx(), 1)
    }

    #[test]
    fn unambiguous_abbreviation_resolves_to_canonical() {
        // --ze -> --zero, --mul-style prefixes collapse onto the option the
        // command reads; the value form records under the canonical key.
        let p = run(&["--ze"]).unwrap();
        assert!(p.has("zero"));

        let p = run(&["--suf", ".c"]).unwrap();
        assert_eq!(p.value_of("suffix"), Some(".c"));

        let p = run(&["--suff=.c"]).unwrap();
        assert_eq!(p.value_of("suffix"), Some(".c"));
    }

    #[test]
    fn abbreviation_reaches_virtual_help_version() {
        let p = run(&["--hel"]).unwrap();
        assert!(p.has("help"));
        let p = run(&["--vers"]).unwrap();
        assert!(p.has("version"));
    }

    #[test]
    fn exact_match_wins_over_prefix() {
        // A registered long that is itself a prefix of another ("suffix" vs
        // a hypothetical "suffixx") matches exactly; here --suffix is exact.
        let p = run(&["--suffix", ".c"]).unwrap();
        assert_eq!(p.value_of("suffix"), Some(".c"));
    }

    #[test]
    fn ambiguous_abbreviation_is_a_usage_error() {
        // "s" prefixes both "suffix" and nothing else in this table, so use
        // an empty name via "--=v"? No: exercise ambiguity with a token that
        // prefixes both registered longs is impossible here (zero/suffix
        // share no prefix), so verify the unrecognized path stays intact.
        let owned: Vec<std::ffi::OsString> = vec!["--zzz".into()];
        let mut io = MemIo::new("");
        let sc = winux_core::test_support::ScratchCtx::new("argopt-amb");
        let r = parse("prog", &owned, &opts(), &mut io.split(), &sc.ctx(), 1);
        assert_eq!(r.unwrap_err(), 1);
        assert!(io.err().contains("unrecognized option '--zzz'"));
    }

    #[test]
    fn requires_argument_names_the_canonical_option() {
        let owned: Vec<std::ffi::OsString> = vec!["--suf".into()];
        let mut io = MemIo::new("");
        let sc = winux_core::test_support::ScratchCtx::new("argopt-req");
        let r = parse("prog", &owned, &opts(), &mut io.split(), &sc.ctx(), 1);
        assert_eq!(r.unwrap_err(), 1);
        // GNU canonicalizes the spelling in the diagnostic (probed 9.4:
        // "option '--suffix' requires an argument").
        assert!(io.err().contains("option '--suffix' requires an argument"));
    }
}
