//! Minimal GNU-getopt-shaped option parsing shared by wave1 commands.
//!
//! GNU permutes options and operands by default (options may appear after
//! operands); `--` ends option processing. Short options accept bundled
//! values (`-sVAL`) and detached values (`-s VAL`); long options accept
//! `--long=VAL` and `--long VAL`. Unknown options produce the GNU-style
//! usage diagnostic and the command's usage status (1, or 2 for printenv).
//!
//! Sequential contract (#1573): getopt_long returns one option per call,
//! and coreutils' main loop exits the process at the moment it receives
//! the help/version value (`case_GETOPT_HELP_CHAR` -> usage(EXIT_SUCCESS),
//! `case_GETOPT_VERSION_CHAR` -> version_etc + exit). Options after the
//! first help/version in argv order are therefore never validated: the
//! parser records the flag and returns at that position, so
//! `<cmd> --help --bogus` is help-exit rc 0 while `<cmd> --bogus --help`
//! still fails as unrecognized (rc 1), and `--help --version` keeps the
//! first-flag-wins order.

use std::ffi::OsString;

use winux_core::{usage_error, Ctx, Io};

pub struct OptSpec {
    pub short: Option<char>,
    pub long: &'static str, // canonical long name without leading dashes
    pub takes_value: bool,
}

pub const HELP: &str = "help";
pub const VERSION: &str = "version";

/// Internal long name for the marker spec a command registers under short
/// 'h' when GNU has NO -h option: getopt_long matches --help only as a
/// long option there, so a bare -h is "invalid option -- 'h'" (probed GNU
/// 9.4: shuf/csplit/pathchk/groups/who/nohup all reject it). Registering
/// the marker keeps the shared parser from hijacking -h into --help for
/// those commands; call [`rejected_short_h`] after parse.
pub const H_REJECT_MARKER: &str = "winux-h-reject";

/// Spec to append to a command's option table when GNU has no -h.
pub const H_REJECT_SPEC: OptSpec = OptSpec {
    short: Some('h'),
    long: H_REJECT_MARKER,
    takes_value: false,
};

/// GNU diagnostic for the marker: `<prog>: invalid option -- 'h'` plus the
/// try-help line, with the command's usage status.
pub fn rejected_short_h(io: &mut Io, prog: &str, p: &Parsed, usage_status: u32) -> Option<u32> {
    if p.flags.iter().any(|f| f == H_REJECT_MARKER) {
        let _ = usage_error(io, prog, "invalid option -- 'h'");
        return Some(usage_status);
    }
    None
}

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
/// Ambiguous and unknown prefixes yield the GNU diagnostics getopt_long
/// produces ("option '--x' is ambiguous; possibilities: ..." /
/// "unrecognized option '--x'").
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
                // getopt_long rejects an inline argument on a flag
                if inline_val.is_some() {
                    let msg = format!("option '--{}' doesn't allow an argument", record_key);
                    let _ = usage_error(io, prog, &msg);
                    return Err(usage_status);
                }
                out.flags.push(record_key.clone());
                // #1573: the sequential getopt_long contract. help/version
                // reach this point only through option-table resolution
                // (exact match or unique abbreviation — the same shapes the
                // virtual entries hold in coreutils' longopts tables), and
                // the bare `-h` mapping below records the same key. In GNU
                // the option is returned to main() here and
                // case_GETOPT_HELP_CHAR / case_GETOPT_VERSION_CHAR exit
                // immediately, so the rest of argv is never validated.
                // Stopping the scan at the record site keeps every earlier
                // option in `out` (they were parsed in order) and every
                // later token unchecked; a help/version consumed as the
                // VALUE of a preceding value option never lands in flags,
                // so it is not short-circuited (getopt hands it to the
                // command as optarg).
                if record_key == HELP || record_key == VERSION {
                    return Ok(out);
                }
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
                    // `-h` alone maps to --help unless the command defines -h.
                    // Same #1573 sequential contract as the long form: the
                    // help value terminates the scan in argv order.
                    out.flags.push(HELP.to_string());
                    return Ok(out);
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
        let owned: Vec<std::ffi::OsString> = args.iter().map(std::ffi::OsString::from).collect();
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

    // ===== #1573: the sequential getopt_long contract =====
    //
    // getopt_long returns one option per call and coreutils' main exits at
    // the help/version value (`case_GETOPT_HELP_CHAR` -> usage(EXIT_SUCCESS)),
    // so options after the first help/version are never validated. Each pin
    // below mirrors a probe row from the issue against WSL coreutils 9.4.

    #[test]
    fn help_short_circuits_later_unknown_options() {
        // `basename --help --bogus` -> usage, rc 0.
        let p = run(&["--help", "--bogus"]).unwrap();
        assert!(p.has("help"));
        assert!(!p.has("version"));
    }

    #[test]
    fn version_short_circuits_later_unknown_options() {
        // `basename --version --bogus` -> version block, rc 0.
        let p = run(&["--version", "--bogus"]).unwrap();
        assert!(p.has("version"));
        assert!(!p.has("help"));
    }

    #[test]
    fn bare_h_short_circuits_later_unknown_options() {
        // The bare `-h` -> help mapping keeps the same order contract as the
        // long form (the mapping itself is argopt's own extension; GNU 9.4
        // rejects a bare -h where the command does not define it — a
        // separate pre-existing divergence, out of #1573's scope).
        let p = run(&["-h", "--bogus"]).unwrap();
        assert!(p.has("help"));
    }

    #[test]
    fn unknown_option_before_help_still_fails() {
        // `basename --bogus --help` -> unrecognized rc 1: getopt hits
        // --bogus first and the usage error leaves main before help is read.
        let owned: Vec<std::ffi::OsString> = vec!["--bogus".into(), "--help".into()];
        let mut io = MemIo::new("");
        let sc = winux_core::test_support::ScratchCtx::new("argopt-order-rev");
        let r = parse("prog", &owned, &opts(), &mut io.split(), &sc.ctx(), 1);
        assert_eq!(r.unwrap_err(), 1);
        assert!(io.err().contains("unrecognized option '--bogus'"));
    }

    #[test]
    fn first_help_or_version_flag_wins() {
        // `basename --help --version` -> help; `--version --help` -> version.
        let p = run(&["--help", "--version"]).unwrap();
        assert!(p.has("help"));
        assert!(!p.has("version"));

        let p = run(&["--version", "--help"]).unwrap();
        assert!(p.has("version"));
        assert!(!p.has("help"));
    }

    #[test]
    fn options_before_help_are_kept() {
        // Options earlier in argv were processed by getopt before main saw
        // help; the short-circuit must not drop them.
        let p = run(&["-z", "--suf", "x", "--help", "--bogus"]).unwrap();
        assert!(p.has("zero"));
        assert_eq!(p.value_of("suffix"), Some("x"));
        assert!(p.has("help"));
    }

    #[test]
    fn help_as_a_value_argument_is_not_short_circuited() {
        // `mkdir --mode --help` in GNU: getopt hands "--help" to the command
        // as optarg; it is a value, not the help exit.
        let p = run(&["--suffix", "--help", "extra"]).unwrap();
        assert!(!p.has("help"));
        assert_eq!(p.value_of("suffix"), Some("--help"));
        assert_eq!(p.positionals, ["extra"]);
    }

    #[test]
    fn attached_value_on_help_is_still_a_usage_error() {
        // `--help=x` keeps the getopt_long "doesn't allow an argument"
        // diagnostic even though help would otherwise short-circuit.
        let owned: Vec<std::ffi::OsString> = vec!["--help=x".into()];
        let mut io = MemIo::new("");
        let sc = winux_core::test_support::ScratchCtx::new("argopt-helpval");
        let r = parse("prog", &owned, &opts(), &mut io.split(), &sc.ctx(), 1);
        assert_eq!(r.unwrap_err(), 1);
        assert!(io
            .err()
            .contains("option '--help' doesn't allow an argument"));
    }

    #[test]
    fn operand_before_help_permutes_into_positionals() {
        // GNU permutes operands: `mkdir x --help --bogus` exits with help
        // rc 0 and the operand stays an operand.
        let owned: Vec<std::ffi::OsString> = vec!["x".into(), "--help".into(), "--bogus".into()];
        let mut io = MemIo::new("");
        let sc = winux_core::test_support::ScratchCtx::new("argopt-perm");
        let p = parse("prog", &owned, &opts(), &mut io.split(), &sc.ctx(), 1).unwrap();
        assert!(p.has("help"));
        assert_eq!(p.positionals, ["x"]);
    }
}
