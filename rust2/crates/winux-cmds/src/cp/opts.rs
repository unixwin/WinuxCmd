//! `cp` option grammar — a sequential GNU-getopt port (cp.c:968-1245).
//!
//! `argopt::parse` records presence and values but not argv order; cp needs
//! order for the last-wins family (`-i`/`-n` set ONE `interactive` field,
//! `-H`/`-L`/`-P`/`-d`/`-a` set ONE `dereference` field, `--update=WHEN`
//! rewrites `interactive`, `--preserve`/`--no-preserve` accumulate), so the
//! switch is applied as each token is seen, exactly like cp.c's getopt loop.
//!
//! Grammar details taken from cp.c:1006 (`getopt_long` optstring
//! `"abdfHilLnprst:uvxPRS:TZ"`), cp.c:115-142 (`long_opts`):
//! * optional-argument long options take a value ONLY as `--opt=VALUE`
//!   (`--backup`, `--preserve`, `--reflink`, `--update`, `--context`);
//!   `--backup numbered` leaves `numbered` an operand (probed: `cp -b
//!   numbered a b` → `target 'b': Not a directory`).
//! * `-b`/`-u`/`-Z` never take an argument; `-t`/`-S` require one, attached
//!   (`-tdir`) or detached.
//! * `--help`/`--version` terminate the scan where they appear (argopt's
//!   #1573 sequential contract).
//! * long options match by exact name or unambiguous prefix; ambiguous and
//!   unknown tokens get getopt_long's diagnostics.
//!
//! Values: `--sparse`, `--reflink`, `--update`, `--backup`, `--preserve`
//! and `--no-preserve` go through GNU argmatch (unique prefix accepted,
//! empty/ambiguous rejected with the value list), matching the probe:
//! `--update=o` → older, `--preserve=o` → ownership, `--update=` → ambiguous.
//!
//! Ubuntu's coreutils 9.4-3ubuntu6.2 oracle (WSL) also prints
//! `warning: behavior of -n is non-portable...` for `-n`, and does NOT
//! reject `-n -b`; both follow the oracle (see the module head of cp.rs for
//! the divergence note).

use std::ffi::OsString;

use winux_core::{tr, Ctx};

/// GNU `cp.c:104-112` update types (9.4 has no `none-fail`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Update {
    All,
    None,
    Older,
}

/// GNU `cp.c:823-855` interactive states.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Interactive {
    Unspecified,
    Ask,
    AlwaysNo,
    AlwaysSkip,
}

/// GNU `copy.h` dereference modes.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Deref {
    Undefined,
    Never,
    CommandLine,
    Always,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Sparse {
    Never,
    Auto,
    Always,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Reflink {
    Never,
    Auto,
    Always,
}

/// GNU `lib/backupfile.h` version-control kinds.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Backup {
    None,
    Simple,
    Numbered,
    Existing,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Preserve {
    pub mode: bool,
    pub timestamps: bool,
    pub ownership: bool,
    pub links: bool,
    pub context: bool,
    pub xattr: bool,
}

impl Preserve {
    pub fn all() -> Self {
        Preserve {
            mode: true,
            timestamps: true,
            ownership: true,
            links: true,
            context: true,
            xattr: true,
        }
    }
    pub fn set_all(&mut self, on: bool) {
        *self = if on {
            Preserve::all()
        } else {
            Preserve::default()
        };
    }
    pub fn any(&self) -> bool {
        self.mode || self.timestamps || self.ownership || self.links || self.context || self.xattr
    }
}

#[derive(Clone, Debug)]
pub struct Options {
    pub recursive: bool,
    pub interactive: Interactive,
    /// `-u` / `--update=older` (cleared when `-n` is active, cp.c:1217).
    pub update: bool,
    pub deref: Deref,
    pub preserve: Preserve,
    pub hard_link: bool,
    pub symbolic_link: bool,
    /// `-f`: unlink the destination when the open fails, then retry.
    pub force: bool,
    pub remove_destination: bool,
    pub no_target_directory: bool,
    pub parents: bool,
    pub verbose: bool,
    pub one_file_system: bool,
    /// `--attributes-only` clears this (copy metadata, not data).
    pub data_copy_required: bool,
    pub backup: Backup,
    pub suffix: String,
    pub sparse: Sparse,
    pub reflink: Reflink,
    pub strip_trailing_slashes: bool,
    pub target_directory: Option<String>,
    pub copy_contents: bool,
    /// `-n` was given (the oracle prints its deprecation warning even when
    /// a later `--update=all`/`-i` overrides the mode).
    pub no_clobber_seen: bool,
    /// `--keep-directory-symlink` (accepted; on Windows the destination
    /// directory link is followed by the default open path already).
    pub keep_directory_symlink: bool,
    pub help: bool,
    pub version: bool,
}

impl Default for Options {
    fn default() -> Self {
        Options {
            recursive: false,
            interactive: Interactive::Unspecified,
            update: false,
            deref: Deref::Undefined,
            preserve: Preserve::default(),
            hard_link: false,
            symbolic_link: false,
            force: false,
            remove_destination: false,
            no_target_directory: false,
            parents: false,
            verbose: false,
            one_file_system: false,
            data_copy_required: true,
            backup: Backup::None,
            suffix: "~".to_string(),
            sparse: Sparse::Never,
            reflink: Reflink::Never,
            strip_trailing_slashes: false,
            target_directory: None,
            copy_contents: false,
            no_clobber_seen: false,
            keep_directory_symlink: false,
            help: false,
            version: false,
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
    Archive,
    AttributesOnly,
    Backup,
    CopyContents,
    Debug,
    Dereference,
    Force,
    Interactive,
    HardLink,
    NoClobber,
    NoDereference,
    NoPreserve,
    NoTargetDirectory,
    OneFileSystem,
    Parents,
    Preserve,
    PreserveShort,
    DerefShort,
    DerefCommandLine,
    Recursive,
    RemoveDestination,
    Sparse,
    Reflink,
    StripTrailingSlashes,
    Suffix,
    SymbolicLink,
    TargetDirectory,
    Update,
    Verbose,
    Context,
    KeepDirectorySymlink,
    Help,
    Version,
}

/// cp.c:115-142 order, with the deprecated `--path` alias (cp.c:128).
const SPECS: &[Spec] = &[
    Spec {
        short: Some('a'),
        long: "archive",
        arg: Arg::None,
        id: Id::Archive,
    },
    Spec {
        short: None,
        long: "attributes-only",
        arg: Arg::None,
        id: Id::AttributesOnly,
    },
    Spec {
        short: Some('b'),
        long: "backup",
        arg: Arg::Optional,
        id: Id::Backup,
    },
    Spec {
        short: None,
        long: "copy-contents",
        arg: Arg::None,
        id: Id::CopyContents,
    },
    Spec {
        short: None,
        long: "debug",
        arg: Arg::None,
        id: Id::Debug,
    },
    Spec {
        short: Some('L'),
        long: "dereference",
        arg: Arg::None,
        id: Id::Dereference,
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
        short: Some('l'),
        long: "link",
        arg: Arg::None,
        id: Id::HardLink,
    },
    Spec {
        short: Some('n'),
        long: "no-clobber",
        arg: Arg::None,
        id: Id::NoClobber,
    },
    Spec {
        short: Some('P'),
        long: "no-dereference",
        arg: Arg::None,
        id: Id::NoDereference,
    },
    Spec {
        short: None,
        long: "no-preserve",
        arg: Arg::Required,
        id: Id::NoPreserve,
    },
    Spec {
        short: Some('T'),
        long: "no-target-directory",
        arg: Arg::None,
        id: Id::NoTargetDirectory,
    },
    Spec {
        short: Some('x'),
        long: "one-file-system",
        arg: Arg::None,
        id: Id::OneFileSystem,
    },
    Spec {
        short: None,
        long: "parents",
        arg: Arg::None,
        id: Id::Parents,
    },
    Spec {
        short: None,
        long: "path",
        arg: Arg::None,
        id: Id::Parents,
    },
    Spec {
        short: None,
        long: "preserve",
        arg: Arg::Optional,
        id: Id::Preserve,
    },
    // -p has no long form (cp.c:202: mode, ownership, timestamps).
    Spec {
        short: Some('p'),
        long: "",
        arg: Arg::None,
        id: Id::PreserveShort,
    },
    // -d has no long form: --no-dereference + --preserve=links (cp.c:1049).
    Spec {
        short: Some('d'),
        long: "",
        arg: Arg::None,
        id: Id::DerefShort,
    },
    // -H has no long form: follow command-line symlinks only (cp.c:1059).
    Spec {
        short: Some('H'),
        long: "",
        arg: Arg::None,
        id: Id::DerefCommandLine,
    },
    Spec {
        short: Some('r'),
        long: "recursive",
        arg: Arg::None,
        id: Id::Recursive,
    },
    Spec {
        short: Some('R'),
        long: "recursive",
        arg: Arg::None,
        id: Id::Recursive,
    },
    Spec {
        short: None,
        long: "remove-destination",
        arg: Arg::None,
        id: Id::RemoveDestination,
    },
    Spec {
        short: None,
        long: "sparse",
        arg: Arg::Required,
        id: Id::Sparse,
    },
    Spec {
        short: None,
        long: "reflink",
        arg: Arg::Optional,
        id: Id::Reflink,
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
        short: Some('s'),
        long: "symbolic-link",
        arg: Arg::None,
        id: Id::SymbolicLink,
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
        short: Some('Z'),
        long: "context",
        arg: Arg::Optional,
        id: Id::Context,
    },
    Spec {
        short: None,
        long: "keep-directory-symlink",
        arg: Arg::None,
        id: Id::KeepDirectorySymlink,
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

/// A usage-level failure already rendered (GNU writes the second line too).
pub struct UsageError(pub String);

fn usage_block(msg: String) -> UsageError {
    UsageError(format!("{msg}\nTry 'cp --help' for more information."))
}

/// XARGMATCH (lib/argmatch.c) over a value table: exact, or a unique prefix;
/// `""` and multiple candidates are ambiguous.
fn argmatch<'a>(
    ctx: &Ctx,
    option: &str,
    value: &str,
    table: &'a [(&'a str, &'a str)],
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
            "command.cp.error.invalid_argument",
            "invalid argument '{value}' for '{option}'\nValid arguments are:\n{values}",
            value = value,
            option = option,
            values = list
        )
    } else {
        tr!(
            ctx,
            "command.cp.error.ambiguous_argument",
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

fn preserve_table() -> [(&'static str, &'static str); 7] {
    [
        ("mode", ""),
        ("timestamps", ""),
        ("ownership", ""),
        ("links", ""),
        ("context", ""),
        ("xattr", ""),
        ("all", ""),
    ]
}

fn sparse_table() -> [(&'static str, &'static str); 3] {
    [("never", ""), ("auto", ""), ("always", "")]
}

fn reflink_table() -> [(&'static str, &'static str); 3] {
    [("auto", ""), ("always", ""), ("never", "")]
}

fn backup_value(ctx: &Ctx, control: &str) -> Result<Backup, UsageError> {
    match control {
        // `--backup=` selects the simple backup (probed: b~ created, rc 0).
        "" => Ok(Backup::Simple),
        "none" | "off" => Ok(Backup::None),
        "simple" | "never" => Ok(Backup::Simple),
        "existing" | "nil" => Ok(Backup::Existing),
        "numbered" | "t" => Ok(Backup::Numbered),
        other => {
            // Unique prefixes of the four aliases (probed: --backup=n is
            // ambiguous; --backup=num is numbered).
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
                "command.cp.error.backup_valid_arguments",
                "Valid arguments are:\n  - 'none', 'off'\n  - 'simple', 'never'\n  - 'existing', 'nil'\n  - 'numbered', 't'"
            );
            let msg = if distinct.is_empty() {
                tr!(
                    ctx,
                    "command.cp.error.invalid_backup_argument",
                    "invalid argument '{value}' for 'backup type'\n{values}",
                    value = other,
                    values = list
                )
            } else {
                tr!(
                    ctx,
                    "command.cp.error.ambiguous_backup_argument",
                    "ambiguous argument '{value}' for 'backup type'\n{values}",
                    value = other,
                    values = list
                )
            };
            Err(usage_block(msg))
        }
    }
}

fn apply_preserve_list(
    ctx: &Ctx,
    list: &str,
    on: bool,
    opt: &str,
    x: &mut Options,
) -> Result<(), UsageError> {
    let table = preserve_table();
    for part in list.split(',') {
        let i = argmatch(ctx, opt, part, &table)?;
        let p = &mut x.preserve;
        match table[i].0 {
            "mode" => p.mode = on,
            "timestamps" => p.timestamps = on,
            "ownership" => p.ownership = on,
            "links" => p.links = on,
            "context" => p.context = on,
            "xattr" => p.xattr = on,
            "all" => p.set_all(on),
            _ => unreachable!(),
        }
    }
    Ok(())
}

/// Parse argv into options + operands, applying the cp.c switch in order.
pub fn parse(args: &[OsString], ctx: &Ctx) -> Result<(Options, Vec<String>), UsageError> {
    let _ = ctx;
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
                            "command.cp.error.ambiguous_option",
                            "option '--{option}' is ambiguous; possibilities: {possibilities}",
                            option = name,
                            possibilities = cands.join(" ")
                        );
                        return Err(usage_block(msg));
                    } else {
                        let msg = tr!(
                            ctx,
                            "command.cp.error.unrecognized_option",
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
                            "command.cp.error.option_requires_argument",
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
                            "command.cp.error.option_no_argument",
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
                            "command.cp.error.option_requires_argument",
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
                Id::Archive => {
                    x.deref = Deref::Never;
                    x.preserve.links = true;
                    x.preserve.ownership = true;
                    x.preserve.mode = true;
                    x.preserve.timestamps = true;
                    x.preserve.xattr = true;
                    x.recursive = true;
                }
                Id::AttributesOnly => x.data_copy_required = false,
                Id::Backup => match val.as_deref() {
                    None => x.backup = Backup::Existing,
                    Some(v) => x.backup = backup_value(ctx, v)?,
                },
                Id::CopyContents => x.copy_contents = true,
                Id::Debug => x.verbose = true,
                Id::Dereference => x.deref = Deref::Always,
                Id::Force => x.force = true,
                Id::Interactive => x.interactive = Interactive::Ask,
                Id::HardLink => x.hard_link = true,
                Id::NoClobber => {
                    // The Ubuntu 9.4 oracle's -n behaves as --update=none:
                    // silent skip, rc 0, no -b conflict. Keeping it out of
                    // AlwaysNo also lets --update=all undo it (probed).
                    x.interactive = Interactive::AlwaysSkip;
                    x.no_clobber_seen = true;
                }
                Id::NoDereference => x.deref = Deref::Never,
                Id::NoPreserve => {
                    let v = val.unwrap_or_default();
                    apply_preserve_list(ctx, &v, false, "--no-preserve", &mut x)?;
                }
                Id::NoTargetDirectory => x.no_target_directory = true,
                Id::OneFileSystem => x.one_file_system = true,
                Id::Parents => x.parents = true,
                Id::PreserveShort => {
                    x.preserve.mode = true;
                    x.preserve.ownership = true;
                    x.preserve.timestamps = true;
                }
                Id::DerefShort => {
                    x.preserve.links = true;
                    x.deref = Deref::Never;
                }
                Id::DerefCommandLine => x.deref = Deref::CommandLine,
                Id::Preserve => match val.as_deref() {
                    None => x.preserve.set_all(true),
                    Some(v) => apply_preserve_list(ctx, v, true, "--preserve", &mut x)?,
                },
                Id::Recursive => x.recursive = true,
                Id::RemoveDestination => x.remove_destination = true,
                Id::Sparse => {
                    let t = sparse_table();
                    let idx = argmatch(ctx, "--sparse", &val.unwrap_or_default(), &t)?;
                    x.sparse = [Sparse::Never, Sparse::Auto, Sparse::Always][idx];
                }
                Id::Reflink => match val.as_deref() {
                    None => x.reflink = Reflink::Always,
                    Some(v) => {
                        let t = reflink_table();
                        let idx = argmatch(ctx, "--reflink", v, &t)?;
                        x.reflink = [Reflink::Auto, Reflink::Always, Reflink::Never][idx];
                    }
                },
                Id::StripTrailingSlashes => x.strip_trailing_slashes = true,
                Id::Suffix => {
                    // Ubuntu coreutils 9.4 oracle: -S no longer implies -b
                    // (probed: `cp -S .bak a b` creates no backup).
                    x.suffix = val.unwrap_or_default();
                }
                Id::SymbolicLink => x.symbolic_link = true,
                Id::TargetDirectory => {
                    let v = val.unwrap_or_default();
                    if x.target_directory.is_some() {
                        let msg = tr!(
                            ctx,
                            "command.cp.error.multiple_target_directories",
                            "multiple target directories specified"
                        );
                        return Err(usage_block(msg));
                    }
                    x.target_directory = Some(v);
                }
                Id::Update => match val.as_deref() {
                    // cp.c:1129-1157: -n takes precedence over an explicit
                    // --update=WHEN, and `all` resets the interactive state.
                    None => x.update = true,
                    Some(v) => {
                        if x.interactive != Interactive::AlwaysNo {
                            let t = update_table();
                            let idx = argmatch(ctx, "--update", v, &t)?;
                            match [Update::All, Update::None, Update::Older][idx] {
                                Update::All => {
                                    x.update = false;
                                    x.interactive = Interactive::Unspecified;
                                }
                                Update::None => {
                                    x.update = false;
                                    x.interactive = Interactive::AlwaysSkip;
                                }
                                Update::Older => {
                                    x.update = true;
                                    x.interactive = Interactive::Unspecified;
                                }
                            }
                        }
                    }
                },
                Id::Verbose => x.verbose = true,
                // -Z/--context: no SELinux on Windows; accepted no-op
                // (cp.c:1178 politely declines when SELinux is disabled).
                Id::Context => {}
                Id::KeepDirectorySymlink => x.keep_directory_symlink = true,
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
                            "command.cp.error.invalid_short_option",
                            "invalid option -- '{option}'",
                            option = c
                        );
                        return Err(usage_block(msg));
                    }
                };
                match spec.arg {
                    Arg::None => {
                        apply_short(spec.id, &mut x)?;
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
                                        "command.cp.error.short_requires_argument",
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
                        // (GNU's "-u" has no optstring colon).
                        apply_short(spec.id, &mut x)?;
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

    finalize(&mut x);
    Ok((x, operands))
}

fn apply_short(id: Id, x: &mut Options) -> Result<(), UsageError> {
    match id {
        Id::Archive => {
            x.deref = Deref::Never;
            x.preserve = Preserve::all();
            x.recursive = true;
        }
        Id::Backup => x.backup = Backup::Existing,
        Id::Dereference => x.deref = Deref::Always,
        Id::Force => x.force = true,
        Id::Interactive => x.interactive = Interactive::Ask,
        Id::HardLink => x.hard_link = true,
        Id::NoClobber => {
            x.interactive = Interactive::AlwaysSkip;
            x.no_clobber_seen = true;
        }
        Id::NoDereference => x.deref = Deref::Never,
        Id::PreserveShort => {
            x.preserve.mode = true;
            x.preserve.ownership = true;
            x.preserve.timestamps = true;
        }
        Id::DerefShort => {
            x.preserve.links = true;
            x.deref = Deref::Never;
        }
        Id::DerefCommandLine => x.deref = Deref::CommandLine,
        Id::Recursive => x.recursive = true,
        Id::NoTargetDirectory => x.no_target_directory = true,
        Id::OneFileSystem => x.one_file_system = true,
        Id::RemoveDestination => x.remove_destination = true,
        Id::SymbolicLink => x.symbolic_link = true,
        Id::Update => x.update = true,
        Id::Verbose => x.verbose = true,
        Id::Context => {}
        _ => unreachable!(),
    }
    Ok(())
}

fn apply_short_value(ctx: &Ctx, id: Id, v: String, x: &mut Options) -> Result<(), UsageError> {
    match id {
        Id::Suffix => x.suffix = v,
        Id::TargetDirectory => {
            if x.target_directory.is_some() {
                let msg = tr!(
                    ctx,
                    "command.cp.error.multiple_target_directories",
                    "multiple target directories specified"
                );
                return Err(usage_block(msg));
            }
            x.target_directory = Some(v);
        }
        _ => unreachable!(),
    }
    Ok(())
}

/// cp.c:1195-1245 post-getopt normalization.
fn finalize(x: &mut Options) {
    // --sparse=never disables reflinking (cp.c:1195-1199).
    if x.reflink == Reflink::Auto && x.sparse == Sparse::Never {
        x.reflink = Reflink::Never;
    }
    // -n clears -u (cp.c:1217).
    if x.interactive == Interactive::AlwaysNo {
        x.update = false;
    }
    // cp.c:1210-1214 dereference default.
    if x.deref == Deref::Undefined {
        x.deref = if x.recursive && !x.hard_link {
            Deref::Never
        } else {
            Deref::Always
        };
    }
}

/// GNU quotes operands with quoteaf (always quoted for these diagnostics,
/// probed: `cp: cannot stat 'nosuch'`).
pub fn q(name: &str) -> String {
    format!("'{name}'")
}

/// Validate the cross-option conflicts cp.c settles before copying.
pub fn validate(ctx: &Ctx, x: &Options) -> Result<(), UsageError> {
    if x.hard_link && x.symbolic_link {
        let msg = tr!(
            ctx,
            "command.cp.error.hard_and_symbolic_links",
            "cannot make both hard and symbolic links"
        );
        return Err(usage_block(msg));
    }
    if x.reflink == Reflink::Always && x.sparse != Sparse::Auto {
        let msg = tr!(
            ctx,
            "command.cp.error.reflink_needs_sparse_auto",
            "--reflink can be used only with --sparse=auto"
        );
        return Err(usage_block(msg));
    }
    Ok(())
}
