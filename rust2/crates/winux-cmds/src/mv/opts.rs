//! `mv` option grammar — a sequential GNU-getopt port (mv.c:336-429).
//!
//! `argopt::parse` records presence and values but not argv order; mv
//! needs order for the last-wins family (`-f`/`-i`/`-n` set ONE
//! `interactive` field, and `--update=WHEN` both rewrites it and is
//! skipped entirely while `-n` is in effect — `-n` takes precedence,
//! mv.c:382 `x.interactive != I_ALWAYS_NO`), so the switch is applied
//! as each token is seen, exactly like mv.c's getopt loop.
//!
//! Grammar details taken from mv.c:337 (`getopt_long` optstring
//! `"bfint:uvS:TZ"`) and mv.c:55-75 (`long_options`):
//! * optional-argument long options take a value ONLY as `--opt=VALUE`
//!   (`--backup`, `--update`); `mv --update all a b` leaves `all`, `a`,
//!   `b` operands and reports `target 'b': Not a directory` (probed).
//! * `-b`/`-u`/`-Z` never take an argument; `-t`/`-S` require one,
//!   attached (`-tdir`) or detached.
//! * `--context` is `no_argument` in 9.4 (unlike cp's optional form):
//!   `mv --context=x` is `option '--context' doesn't allow an argument`.
//! * a second `-t` is the parse-time `multiple target directories
//!   specified` diagnostic (mv.c:377 — `error (EXIT_FAILURE)`, no
//!   `Try 'mv --help'` tail, like `cannot combine --target-directory
//!   (-t) and --no-target-directory (-T)` at mv.c:441 and the
//!   `target directory`/`target` diagnostics).
//! * `--update=CONTROL` runs argmatch INSIDE the getopt loop, but only
//!   when `-n` is not already in effect (mv.c:382); the backup
//!   CONTROL's xget_version runs much later in mv.c:501, after the
//!   operand grammar — `mv --backup=bogus` alone reports
//!   `missing file operand`, not an argmatch error (probed).
//! * `--help`/`--version` terminate the scan where they appear (argopt's
//!   #1573 sequential contract).
//! * long options match by exact name or unambiguous prefix; ambiguous
//!   and unknown tokens get getopt_long's diagnostics (probed:
//!   `mv --no` → `option '--no' is ambiguous; possibilities:
//!   '--no-clobber' '--no-copy' '--no-target-directory'`).
//!
//! Values: `--update` goes through GNU argmatch (unique prefix accepted,
//! empty/ambiguous rejected with the value list — `mv --update=o` →
//! older, `mv --update=` → ambiguous, probed 9.4-3ubuntu6.2).

use std::ffi::OsString;

use winux_core::{tr, Ctx};

/// GNU `copy.h` interactive states (mv.c uses all five).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Interactive {
    Unspecified,
    /// `-f` — never prompt.
    AlwaysYes,
    /// `-i` — prompt before overwrite.
    Ask,
    /// `-n` — never overwrite; mv's variant returns 1 and prints
    /// `not replacing 'dst'` (I_ALWAYS_NO, not cp's silent skip).
    AlwaysNo,
    /// `--update=none` — never overwrite; silent skip, rc 0, `--debug`
    /// prints `skipped 'dst'`.
    AlwaysSkip,
}

/// GNU `lib/backupfile.h` version-control kinds.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Backup {
    None,
    Simple,
    Numbered,
    Existing,
}

#[derive(Clone, Debug)]
pub struct Options {
    pub interactive: Interactive,
    /// `-u` / `--update=older` (cleared when `-n` is active, mv.c:491).
    pub update: bool,
    /// `--debug`: emits `skipped 'dst'` on the quiet paths; sets verbose.
    pub debug: bool,
    pub verbose: bool,
    /// `--no-copy`: refuse the copy+remove fallback when rename fails.
    pub no_copy: bool,
    pub strip_trailing_slashes: bool,
    /// `-b`/`--backup` or `-S` were given (mv.c:344 `make_backups`).
    pub make_backups: bool,
    /// The `--backup=CONTROL` value; `None` selects xget_version's
    /// default (`existing` — simple unless numbered backups are present).
    pub version_control: Option<String>,
    /// `-S SUFFIX`; `None`/`""` selects the `~` default (probed:
    /// `mv -S '' -b a b` creates `b~`).
    pub backup_suffix: Option<String>,
    /// The backup control resolved by the post-grammar xget_version.
    pub backup: Backup,
    pub target_directory: Option<String>,
    pub no_target_directory: bool,
    pub help: bool,
    pub version: bool,
}

impl Default for Options {
    fn default() -> Self {
        Options {
            interactive: Interactive::Unspecified,
            update: false,
            debug: false,
            verbose: false,
            no_copy: false,
            strip_trailing_slashes: false,
            make_backups: false,
            version_control: None,
            backup_suffix: None,
            backup: Backup::None,
            target_directory: None,
            no_target_directory: false,
            help: false,
            version: false,
        }
    }
}

/// A usage-level failure already rendered. `try_help` carries the
/// `Try 'mv --help' for more information.` second line: GNU emits it for
/// the operand/option grammar errors (`usage (EXIT_FAILURE)`) but not
/// for the `error (EXIT_FAILURE, ...)` diagnostics — `-t` twice, `-t`
/// and `-T` combined, `target`/`target directory`.
pub struct UsageError(pub String, pub bool);

pub fn usage_block(msg: String) -> UsageError {
    UsageError(
        format!("{msg}\nTry 'mv --help' for more information."),
        true,
    )
}

fn fatal(msg: String) -> UsageError {
    UsageError(msg, false)
}

/// XARGMATCH (lib/argmatch.c) over a value table: exact, or a unique
/// prefix; `""` and multiple candidates are ambiguous.
fn argmatch(
    ctx: &Ctx,
    option: &str,
    value: &str,
    table: &[(&str, &str)],
) -> Result<usize, UsageError> {
    if let Some(i) = table.iter().position(|(name, _)| *name == value) {
        return Ok(i);
    }
    let hits: Vec<usize> = table
        .iter()
        .enumerate()
        .filter(|(_, (name, _))| name.starts_with(value))
        .map(|(i, _)| i)
        .collect();
    if hits.len() == 1 {
        return Ok(hits[0]);
    }
    let mut list = String::new();
    for (name, alias) in table {
        let _ = alias;
        list.push_str(&format!("  - '{name}'\n"));
    }
    let list = list.trim_end_matches('\n').to_string();
    let msg = if hits.is_empty() {
        tr!(
            ctx,
            "command.mv.error.invalid_argument",
            "invalid argument '{value}' for '{option}'\nValid arguments are:\n{values}",
            value = value,
            option = option,
            values = list
        )
    } else {
        tr!(
            ctx,
            "command.mv.error.ambiguous_argument",
            "ambiguous argument '{value}' for '{option}'\nValid arguments are:\n{values}",
            value = value,
            option = option,
            values = list
        )
    };
    Err(usage_block(msg))
}

fn update_table() -> [(&'static str, &'static str); 3] {
    [("all", ""), ("none", ""), ("older", "")]
}

/// xget_version (mv.c:501) over the backupfile.c type table: the four
/// controls carry aliases, and a `""` control is xget_version's default
/// (`existing`, probed `mv --backup= a b` → rc 0).
pub fn backup_value(ctx: &Ctx, control: &str) -> Result<Backup, UsageError> {
    match control {
        "" | "existing" | "nil" => Ok(Backup::Existing),
        "none" | "off" => Ok(Backup::None),
        "simple" | "never" => Ok(Backup::Simple),
        "numbered" | "t" => Ok(Backup::Numbered),
        other => {
            // Unique prefixes of the four controls (probed: --backup=n is
            // ambiguous — 'none'/'nil'/'never'/'numbered' give two distinct
            // controls; --backup=num is numbered).
            let aliases = [
                ("none", Backup::None),
                ("off", Backup::None),
                ("simple", Backup::Simple),
                ("never", Backup::Simple),
                ("existing", Backup::Existing),
                ("nil", Backup::Existing),
                ("numbered", Backup::Numbered),
                ("t", Backup::Numbered),
            ];
            let hits: Vec<usize> = aliases
                .iter()
                .enumerate()
                .filter(|(_, (name, _))| name.starts_with(other))
                .map(|(i, _)| i)
                .collect();
            let mut distinct: Vec<Backup> = Vec::new();
            for i in &hits {
                if !distinct.contains(&aliases[*i].1) {
                    distinct.push(aliases[*i].1);
                }
            }
            if distinct.len() == 1 {
                return Ok(distinct[0]);
            }
            let list = tr!(
                ctx,
                "command.mv.error.backup_valid_arguments",
                "Valid arguments are:\n  - 'none', 'off'\n  - 'simple', 'never'\n  - 'existing', 'nil'\n  - 'numbered', 't'"
            );
            let msg = if distinct.is_empty() {
                tr!(
                    ctx,
                    "command.mv.error.invalid_backup_argument",
                    "invalid argument '{value}' for 'backup type'\n{values}",
                    value = other,
                    values = list
                )
            } else {
                tr!(
                    ctx,
                    "command.mv.error.ambiguous_backup_argument",
                    "ambiguous argument '{value}' for 'backup type'\n{values}",
                    value = other,
                    values = list
                )
            };
            Err(usage_block(msg))
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Arg {
    None,
    Optional,
    Required,
}

struct Spec {
    short: Option<char>,
    long: &'static str,
    arg: Arg,
    id: Id,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Id {
    Backup,
    Context,
    Debug,
    Force,
    Interactive,
    NoClobber,
    NoCopy,
    NoTargetDirectory,
    StripTrailingSlashes,
    Suffix,
    TargetDirectory,
    Update,
    Verbose,
    Help,
    Version,
}

/// mv.c:55-75 long_options order (the ambiguous-possibilities list
/// follows declaration order, probed `mv --no`).
const SPECS: &[Spec] = &[
    Spec {
        short: Some('b'),
        long: "backup",
        arg: Arg::Optional,
        id: Id::Backup,
    },
    Spec {
        short: Some('Z'),
        long: "context",
        arg: Arg::None,
        id: Id::Context,
    },
    Spec {
        short: None,
        long: "debug",
        arg: Arg::None,
        id: Id::Debug,
    },
    Spec {
        short: Some('f'),
        long: "force",
        arg: Arg::None,
        id: Id::Force,
    },
    Spec {
        short: Some('i'),
        long: "interactive",
        arg: Arg::None,
        id: Id::Interactive,
    },
    Spec {
        short: Some('n'),
        long: "no-clobber",
        arg: Arg::None,
        id: Id::NoClobber,
    },
    Spec {
        short: None,
        long: "no-copy",
        arg: Arg::None,
        id: Id::NoCopy,
    },
    Spec {
        short: Some('T'),
        long: "no-target-directory",
        arg: Arg::None,
        id: Id::NoTargetDirectory,
    },
    Spec {
        short: None,
        long: "strip-trailing-slashes",
        arg: Arg::None,
        id: Id::StripTrailingSlashes,
    },
    Spec {
        short: Some('S'),
        long: "suffix",
        arg: Arg::Required,
        id: Id::Suffix,
    },
    Spec {
        short: Some('t'),
        long: "target-directory",
        arg: Arg::Required,
        id: Id::TargetDirectory,
    },
    Spec {
        short: Some('u'),
        long: "update",
        arg: Arg::Optional,
        id: Id::Update,
    },
    Spec {
        short: Some('v'),
        long: "verbose",
        arg: Arg::None,
        id: Id::Verbose,
    },
    Spec {
        short: None,
        long: "help",
        arg: Arg::None,
        id: Id::Help,
    },
    Spec {
        short: None,
        long: "version",
        arg: Arg::None,
        id: Id::Version,
    },
];

fn multiple_target_directories(ctx: &Ctx) -> UsageError {
    fatal(tr!(
        ctx,
        "command.mv.error.multiple_target_directories",
        "multiple target directories specified"
    ))
}

/// Parse argv into options + operands, applying the mv.c switch in order.
pub fn parse(args: &[OsString], ctx: &Ctx) -> Result<(Options, Vec<String>), UsageError> {
    let mut x = Options::default();
    let mut operands: Vec<String> = Vec::new();
    let mut i = 0usize;
    let mut only_operands = false;

    while i < args.len() {
        let arg = args[i].to_string_lossy().into_owned();
        if only_operands {
            operands.push(arg);
            i += 1;
            continue;
        }
        if arg == "--" {
            only_operands = true;
            i += 1;
            continue;
        }
        if let Some(rest) = arg.strip_prefix("--") {
            let (name, inline) = match rest.split_once('=') {
                Some((n, v)) => (n.to_string(), Some(v.to_string())),
                None => (rest.to_string(), None),
            };
            // exact match first, then unique prefix (getopt_long)
            let exact = SPECS.iter().position(|s| s.long == name);
            let spec = match exact {
                Some(i) => &SPECS[i],
                None => {
                    let hits: Vec<&Spec> = SPECS
                        .iter()
                        .filter(|s| !s.long.is_empty() && s.long.starts_with(&name))
                        .collect();
                    if hits.len() == 1 {
                        hits[0]
                    } else if hits.len() > 1 {
                        let mut cands: Vec<String> = Vec::new();
                        for s in &hits {
                            let c = format!("'--{}'", s.long);
                            if !cands.contains(&c) {
                                cands.push(c);
                            }
                        }
                        let msg = tr!(
                            ctx,
                            "command.mv.error.ambiguous_option",
                            "option '--{option}' is ambiguous; possibilities: {possibilities}",
                            option = name,
                            possibilities = cands.join(" ")
                        );
                        return Err(usage_block(msg));
                    } else {
                        let msg = tr!(
                            ctx,
                            "command.mv.error.unrecognized_option",
                            "unrecognized option '--{option}'",
                            option = name
                        );
                        return Err(usage_block(msg));
                    }
                }
            };
            let value = |inline: Option<String>, i: &mut usize| -> Result<String, UsageError> {
                match inline {
                    Some(v) => Ok(v),
                    None => match args.get(*i + 1) {
                        Some(v) => {
                            *i += 1;
                            Ok(v.to_string_lossy().into_owned())
                        }
                        None => Err(usage_block(tr!(
                            ctx,
                            "command.mv.error.option_requires_argument",
                            "option '--{option}' requires an argument",
                            option = spec.long
                        ))),
                    },
                }
            };
            match spec.arg {
                Arg::None => {
                    if inline.is_some() {
                        let msg = tr!(
                            ctx,
                            "command.mv.error.option_no_argument",
                            "option '--{option}' doesn't allow an argument",
                            option = spec.long
                        );
                        return Err(usage_block(msg));
                    }
                }
                Arg::Optional => {}
                Arg::Required => {
                    if inline.is_none() && args.get(i + 1).is_none() {
                        let msg = tr!(
                            ctx,
                            "command.mv.error.option_requires_argument",
                            "option '--{option}' requires an argument",
                            option = spec.long
                        );
                        return Err(usage_block(msg));
                    }
                }
            }
            // Values are consumed before the switch so a bare optional
            // option never eats the next argv word.
            let val = match spec.arg {
                Arg::None => None,
                Arg::Optional => inline.clone(),
                Arg::Required => Some(value(inline.clone(), &mut i)?),
            };
            match spec.id {
                Id::Backup => {
                    x.make_backups = true;
                    if let Some(v) = val {
                        x.version_control = Some(v);
                    }
                }
                Id::Context => {
                    // -Z/--context: no SELinux on Windows; mv.c:415's
                    // enhancement skips the selabel_open entirely when
                    // SELinux is disabled, so the option is a no-op.
                }
                Id::Debug => {
                    x.debug = true;
                    x.verbose = true;
                }
                Id::Force => x.interactive = Interactive::AlwaysYes,
                Id::Interactive => x.interactive = Interactive::Ask,
                Id::NoClobber => x.interactive = Interactive::AlwaysNo,
                Id::NoCopy => x.no_copy = true,
                Id::NoTargetDirectory => x.no_target_directory = true,
                Id::StripTrailingSlashes => x.strip_trailing_slashes = true,
                Id::Suffix => {
                    x.make_backups = true;
                    x.backup_suffix = Some(val.unwrap_or_default());
                }
                Id::TargetDirectory => {
                    if x.target_directory.is_some() {
                        return Err(multiple_target_directories(ctx));
                    }
                    x.target_directory = Some(val.unwrap_or_default());
                }
                Id::Update => match val.as_deref() {
                    None => x.update = true,
                    Some(v) => {
                        // mv.c:382: -n takes precedence over an explicit
                        // --update=WHEN (no argmatch while -n is active —
                        // probed `mv -n --update=bogus` → `not replacing`).
                        if x.interactive != Interactive::AlwaysNo {
                            let t = update_table();
                            let idx = argmatch(ctx, "--update", v, &t)?;
                            match idx {
                                // all
                                0 => {
                                    x.update = false;
                                    x.interactive = Interactive::Unspecified;
                                }
                                // none
                                1 => {
                                    x.update = false;
                                    x.interactive = Interactive::AlwaysSkip;
                                }
                                // older
                                _ => {
                                    x.update = true;
                                    x.interactive = Interactive::Unspecified;
                                }
                            }
                        }
                    }
                },
                Id::Verbose => x.verbose = true,
                Id::Help => {
                    x.help = true;
                    return Ok((x, operands));
                }
                Id::Version => {
                    x.version = true;
                    return Ok((x, operands));
                }
            }
            i += 1;
            continue;
        }
        if arg.len() > 1 && arg.starts_with('-') {
            let chars: Vec<char> = arg.chars().skip(1).collect();
            let mut ci = 0usize;
            while ci < chars.len() {
                let c = chars[ci];
                let spec = match SPECS.iter().find(|s| s.short == Some(c)) {
                    Some(s) => s,
                    None => {
                        let msg = tr!(
                            ctx,
                            "command.mv.error.invalid_short_option",
                            "invalid option -- '{option}'",
                            option = c
                        );
                        return Err(usage_block(msg));
                    }
                };
                match spec.arg {
                    Arg::None => {
                        apply_short(spec.id, &mut x);
                        ci += 1;
                    }
                    Arg::Required => {
                        let rest: String = chars[ci + 1..].iter().collect();
                        let v = if !rest.is_empty() {
                            rest
                        } else {
                            i += 1;
                            match args.get(i) {
                                Some(v) => v.to_string_lossy().into_owned(),
                                None => {
                                    let msg = tr!(
                                        ctx,
                                        "command.mv.error.short_requires_argument",
                                        "option requires an argument -- '{option}'",
                                        option = c
                                    );
                                    return Err(usage_block(msg));
                                }
                            }
                        };
                        apply_short_value(ctx, spec.id, v, &mut x)?;
                        ci = chars.len();
                    }
                    Arg::Optional => {
                        // Short optional arguments never consume anything
                        // (GNU's "u" has no optstring colon).
                        apply_short(spec.id, &mut x);
                        ci += 1;
                    }
                }
            }
            i += 1;
            continue;
        }
        operands.push(arg);
        i += 1;
    }

    // mv.c:491: -n clears -u.
    if x.interactive == Interactive::AlwaysNo {
        x.update = false;
    }
    Ok((x, operands))
}

fn apply_short(id: Id, x: &mut Options) {
    match id {
        Id::Backup => x.make_backups = true,
        Id::Context => {}
        Id::Debug => {
            x.debug = true;
            x.verbose = true;
        }
        Id::Force => x.interactive = Interactive::AlwaysYes,
        Id::Interactive => x.interactive = Interactive::Ask,
        Id::NoClobber => x.interactive = Interactive::AlwaysNo,
        Id::NoCopy => x.no_copy = true,
        Id::NoTargetDirectory => x.no_target_directory = true,
        Id::StripTrailingSlashes => x.strip_trailing_slashes = true,
        Id::Update => x.update = true,
        Id::Verbose => x.verbose = true,
        _ => unreachable!(),
    }
}

fn apply_short_value(ctx: &Ctx, id: Id, v: String, x: &mut Options) -> Result<(), UsageError> {
    match id {
        Id::Suffix => {
            x.make_backups = true;
            x.backup_suffix = Some(v);
        }
        Id::TargetDirectory => {
            if x.target_directory.is_some() {
                return Err(multiple_target_directories(ctx));
            }
            x.target_directory = Some(v);
        }
        _ => unreachable!(),
    }
    Ok(())
}

/// GNU quotes operands with quoteaf — quotearg's
/// shell_escape_always_quoting_style (always quoted; control bytes leave
/// the quotes as $'\ooo' splices, a lone apostrophe restyles the whole
/// name as "..." when every char is C+shell safe, else the '\'' splice —
/// #374, probed coreutils 9.4: `cannot stat 'x'$'\001''y'`).
pub fn q(name: &str) -> String {
    crate::ls::shell_escape_quote_display_name(name, true)
}
