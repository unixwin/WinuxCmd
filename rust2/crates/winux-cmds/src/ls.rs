//! `ls` — GNU coreutils ls (9.4) on the WinuxCmd Windows value model.
//!
//! Owning GNU functions (coreutils-9.4, clean-room port — no GNU source is
//! copied; references record where each behavior's contract lives):
//!   ls.c:1644      main              operand gobble/sort/extract/print flow
//!   ls.c:1902      decode_switches   option table + positional application
//!   ls.c:899-951   format_args / sort_args / time_args / when_args tables
//!   ls.c:2965      queue_directory   pending dir queue (realname for -R links)
//!   ls.c:2981      print_dir        enumerate / ignore / sort / total / print
//!   ls.c:3152      file_ignored     -a/-A/-B/--hide/-I pattern filtering
//!   ls.c:3370      gobble_file      per-entry stat/deref/scontext collection
//!   ls.c:3714      get_link_name    readlink -> ` -> target` display
//!   ls.c:3741      extract_dirs_from_files  files first, then each dir
//!   ls.c:3796      xstrcoll         locale collation (C.UTF-8 == strcmp)
//!   ls.c:3856-3981 cmp_{ctime,mtime,atime,btime,size,name,extension,width}
//!                    + filevercmp (gnulib/lib/filevercmp.c:134)
//!   ls.c:4086      sort_files       sort + -r reverse + dirs-first partition
//!   ls.c:4128      print_current_files  format dispatch
//!   ls.c:4304      print_long_format    long line field order/widths
//!   ls.c:4818      print_name_with_quoting  quote + color + indicator
//!   ls.c:4871      print_file_name_and_frills  inode/blocks/scontext prefix
//!   ls.c:5136      print_many_per_line    -C grid (column-major)
//!   ls.c:5172      print_horizontal       -x grid (row-major)
//!   ls.c:5213      print_with_separator   -m commas / unbounded -C/-x
//!   ls.c:5254      indent                 tab-aware padding
//!   ls.c:5366      calculate_columns      incremental column-width pass
//!   gnulib/lib/human.c:154   human_readable  -h/--si ceiling-tenth scale
//!   gnulib/lib/quotearg.c:248  quotearg_buffer_restyled  quoting styles
//!   gnulib/lib/argmatch.c      argmatch prefix match + die (rc 1)
//!
//! Windows value model (1.x ls.cpp parity, issues #1639/#1643/#1644/
//! #1645/#1646 and #1504/#1536/#1145/#1572): reparse points carry
//! SYMLINK/MOUNT_POINT/LX(0xA000001D) tags and are read through
//! FSCTL_GET_REPARSE_POINT; a junction stays a reparse `l` entry; the
//! permission string follows the shared #1777 Windows mode model
//! (regular 0644, executable extensions and directories 0755, character
//! devices 0666, links 0777, READONLY clears the write bits); -Z prints the
//! '?'; --si scales by 1000 with lowercase k; enumeration is
//! case-insensitive on disk but all matching is case-sensitive; DOS device
//! names (NUL, /dev/null, ...) are character devices; a bare-name operand
//! retries as name.exe (#1572).

use std::cmp::Ordering;
use std::ffi::OsString;

use crate::{fs_win, gnuhelp};
use winux_core::{tr, usage_error, CmdResult, Ctx, Io};

// ======================================================================
// Option table (ls.c:847-896 long_options + optstring
// "abcdfghiklmnopqrstuvw:xABCDFGHI:LNQRST:UXZ1").  Order matters: glibc
// getopt lists ambiguous-prefix candidates in table order.
// ======================================================================

#[derive(Clone, Copy, PartialEq)]
enum Arg {
    No,
    Required,
    /// GNU optional_argument: only `--long=VAL` carries a value; the short
    /// form never takes one (`-Falways` parses as -F -a -l ...).
    Optional,
}

struct Spec {
    short: Option<char>,
    long: &'static str,
    /// The key emitted into Occ (long name for long options, the char
    /// string for short-only options).
    key: &'static str,
    arg: Arg,
}

const fn s(short: Option<char>, long: &'static str, arg: Arg) -> Spec {
    Spec {
        short,
        long,
        key: long,
        arg,
    }
}

/// Short-only option (no GNU long spelling — `--l` must NOT parse).
const fn so(short: char) -> Spec {
    Spec {
        short: Some(short),
        long: "",
        key: match short {
            'c' => "c",
            'f' => "f",
            'g' => "g",
            'l' => "l",
            'm' => "m",
            'o' => "o",
            'p' => "p",
            't' => "t",
            'u' => "u",
            'v' => "v",
            'x' => "x",
            'C' => "C",
            'S' => "S",
            'U' => "U",
            'X' => "X",
            '1' => "1",
            _ => "",
        },
        arg: Arg::No,
    }
}

const SPECS: &[Spec] = &[
    s(Some('a'), "all", Arg::No),
    s(Some('b'), "escape", Arg::No),
    s(Some('d'), "directory", Arg::No),
    s(Some('D'), "dired", Arg::No),
    s(None, "full-time", Arg::No),
    s(None, "group-directories-first", Arg::No),
    s(Some('h'), "human-readable", Arg::No),
    s(Some('i'), "inode", Arg::No),
    s(Some('k'), "kibibytes", Arg::No),
    s(Some('n'), "numeric-uid-gid", Arg::No),
    s(Some('G'), "no-group", Arg::No),
    s(Some('q'), "hide-control-chars", Arg::No),
    s(Some('r'), "reverse", Arg::No),
    s(Some('s'), "size", Arg::No),
    s(Some('w'), "width", Arg::Required),
    s(Some('A'), "almost-all", Arg::No),
    s(Some('B'), "ignore-backups", Arg::No),
    s(Some('F'), "classify", Arg::Optional),
    s(None, "file-type", Arg::No),
    s(None, "si", Arg::No),
    s(Some('H'), "dereference-command-line", Arg::No),
    s(None, "dereference-command-line-symlink-to-dir", Arg::No),
    s(None, "hide", Arg::Required),
    s(Some('I'), "ignore", Arg::Required),
    s(None, "indicator-style", Arg::Required),
    s(Some('L'), "dereference", Arg::No),
    s(Some('N'), "literal", Arg::No),
    s(Some('Q'), "quote-name", Arg::No),
    s(None, "quoting-style", Arg::Required),
    s(Some('R'), "recursive", Arg::No),
    s(None, "format", Arg::Required),
    s(None, "show-control-chars", Arg::No),
    s(None, "sort", Arg::Required),
    s(Some('T'), "tabsize", Arg::Required),
    s(None, "time", Arg::Required),
    s(None, "time-style", Arg::Required),
    s(None, "zero", Arg::No),
    s(None, "color", Arg::Optional),
    s(None, "hyperlink", Arg::Optional),
    s(None, "block-size", Arg::Required),
    s(Some('Z'), "context", Arg::No),
    s(None, "author", Arg::No),
    // short-only letters (optstring-only in GNU: c f g l m o p t u v x 1)
    so('c'),
    so('f'),
    so('g'),
    so('l'),
    so('m'),
    so('o'),
    so('p'),
    so('t'),
    so('u'),
    so('v'),
    so('x'),
    so('C'),
    so('S'),
    so('U'),
    so('X'),
    so('1'),
];

fn long_by_name(name: &str) -> Option<&'static Spec> {
    if name.is_empty() {
        return None;
    }
    SPECS.iter().find(|o| !o.long.is_empty() && o.long == name)
}

fn short_by_char(c: char) -> Option<&'static Spec> {
    SPECS.iter().find(|o| o.short == Some(c))
}

/// One decoded getopt event in argv order (GNU getopt_long returns one
/// option per call; decode_switches applies each immediately).
enum Occ {
    Flag(&'static str),
    Val(&'static str, String),
    /// Bare optional-arg long (`--color`, `--hyperlink`, `--classify`).
    Bare(&'static str),
    /// getopt diagnostic; scanning stops here (GNU dies at first error).
    Err(String),
    Help,
    Version,
}

/// GNU getopt_long scan of `args` (argv[0] already stripped). Records one
/// Occ per option in argv order, permutes operands like GNU (options may
/// follow operands), honors `--` and bare `-`, expands short bundles, and
/// resolves unambiguous long-option abbreviations. Diagnostics use the
/// glibc/getopt wording argopt shares (`invalid option -- 'z'`,
/// `unrecognized option '--x'`, `option '--x' is ambiguous; possibilities:
/// ...`, `option '--x' requires an argument`, `option '--x' doesn't allow
/// an argument`, `option requires an argument -- 'w'`).
fn scan(args: &[OsString], ctx: &Ctx) -> (Vec<Occ>, Vec<OsString>) {
    let mut occs = Vec::new();
    let mut operands: Vec<OsString> = Vec::new();
    let mut i = 0usize;
    let mut no_more = false;
    while i < args.len() {
        let arg = args[i].to_string_lossy().into_owned();
        if no_more {
            operands.push(args[i].clone());
            i += 1;
            continue;
        }
        if arg == "--" {
            no_more = true;
            i += 1;
            continue;
        }
        if let Some(rest) = arg.strip_prefix("--") {
            let (name, inline) = match rest.split_once('=') {
                Some((n, v)) => (n, Some(v.to_string())),
                None => (rest, None),
            };
            // resolve: exact match wins; else unique prefix over the spec
            // longs plus the virtual help/version entries (ls.c longopts).
            let matched: Option<&'static Spec> = if let Some(spec) = long_by_name(name) {
                Some(spec)
            } else if name == "help" {
                return_help(&mut occs);
                i += 1;
                continue;
            } else if name == "version" {
                occs.push(Occ::Version);
                i += 1;
                continue;
            } else {
                let mut cand: Vec<&str> = SPECS
                    .iter()
                    .filter(|o| !o.long.is_empty() && o.long.starts_with(name))
                    .map(|o| o.long)
                    .collect();
                if "help".starts_with(name) {
                    cand.push("help");
                }
                if "version".starts_with(name) {
                    cand.push("version");
                }
                if cand.len() == 1 {
                    if cand[0] == "help" {
                        return_help(&mut occs);
                        i += 1;
                        continue;
                    }
                    if cand[0] == "version" {
                        occs.push(Occ::Version);
                        i += 1;
                        continue;
                    }
                    Some(long_by_name(cand[0]).unwrap())
                } else if name.is_empty() || cand.is_empty() {
                    occs.push(Occ::Err(tr!(
                        ctx,
                        "command.ls.error.unrecognized_option",
                        "unrecognized option '--{opt}'",
                        opt = rest
                    )));
                    i += 1;
                    continue;
                } else {
                    let list: Vec<String> = cand.iter().map(|c| format!("'--{c}'")).collect();
                    occs.push(Occ::Err(tr!(
                        ctx,
                        "command.ls.error.ambiguous_option",
                        "option '--{opt}' is ambiguous; possibilities: {list}",
                        opt = rest,
                        list = list.join(" ")
                    )));
                    i += 1;
                    continue;
                }
            };
            let spec = matched.unwrap();
            let long = spec.long;
            match spec.arg {
                Arg::No => {
                    if inline.is_some() {
                        occs.push(Occ::Err(tr!(
                            ctx,
                            "command.ls.error.option_no_arg",
                            "option '--{opt}' doesn't allow an argument",
                            opt = long
                        )));
                    } else {
                        occs.push(Occ::Flag(spec.key));
                    }
                }
                Arg::Required => match inline {
                    Some(v) => occs.push(Occ::Val(spec.key, v)),
                    None => {
                        i += 1;
                        match args.get(i) {
                            Some(v) => {
                                occs.push(Occ::Val(spec.key, v.to_string_lossy().into_owned()))
                            }
                            None => occs.push(Occ::Err(tr!(
                                ctx,
                                "command.ls.error.option_requires_arg",
                                "option '--{opt}' requires an argument",
                                opt = long
                            ))),
                        }
                    }
                },
                Arg::Optional => match inline {
                    Some(v) => occs.push(Occ::Val(spec.key, v)),
                    None => occs.push(Occ::Bare(spec.key)),
                },
            }
            i += 1;
            continue;
        }
        if arg.len() > 1 && arg.starts_with('-') {
            let chars: Vec<char> = arg.chars().skip(1).collect();
            let mut ci = 0usize;
            while ci < chars.len() {
                let c = chars[ci];
                match short_by_char(c) {
                    None => {
                        occs.push(Occ::Err(tr!(
                            ctx,
                            "command.ls.error.invalid_option",
                            "invalid option -- '{opt}'",
                            opt = c
                        )));
                        break;
                    }
                    Some(spec) if spec.arg == Arg::Required => {
                        let rest: String = chars[ci + 1..].iter().collect();
                        if !rest.is_empty() {
                            occs.push(Occ::Val(spec.key, rest));
                        } else {
                            i += 1;
                            match args.get(i) {
                                Some(v) => {
                                    occs.push(Occ::Val(spec.key, v.to_string_lossy().into_owned()))
                                }
                                None => occs.push(Occ::Err(tr!(
                                    ctx,
                                    "command.ls.error.short_requires_arg",
                                    "option requires an argument -- '{opt}'",
                                    opt = c
                                ))),
                            }
                        }
                        break;
                    }
                    // Optional-arg shorts (-F) take no argument in the
                    // optstring: '-Ffoo' is the bundle -F -f -o -o.
                    Some(spec) => {
                        occs.push(Occ::Flag(spec.key));
                        ci += 1;
                    }
                }
            }
            i += 1;
            continue;
        }
        operands.push(args[i].clone());
        i += 1;
    }
    (occs, operands)
}

fn return_help(occs: &mut Vec<Occ>) {
    occs.push(Occ::Help);
}

// ======================================================================
// Decoded configuration (decode_switches state, ls.c:1902-2480).
// ======================================================================

#[derive(Clone, Copy, PartialEq, Debug)]
enum Ignore {
    Default,
    Minimal, // -a / -f
    DotDot,  // -A
}

#[derive(Clone, Copy, PartialEq, Debug)]
enum Format {
    Long,
    Commas,
    OnePerLine,
    Columns, // many_per_line (-C)
    Across,  // horizontal (-x)
}

#[derive(Clone, Copy, PartialEq, Debug)]
enum Sort {
    Name,
    Time,
    Size,
    Version,
    Extension,
    Width,
    None,
}

#[derive(Clone, Copy, PartialEq, Debug)]
enum TimeType {
    Mtime,
    Atime,
    Ctime,
    Btime,
}

#[derive(Clone, Copy, PartialEq, Debug)]
enum TimeStyle {
    Default,
    Locale,
    FullIso,
    LongIso,
    Iso,
    Custom,
}

#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Debug)]
enum Indicator {
    None,
    Slash,
    FileType,
    Classify,
}

#[derive(Clone, Copy, PartialEq, Debug)]
enum Deref {
    Never,
    CmdlineAll,          // -H
    CmdlineSymlinkToDir, // default resolution / --dereference-command-line-symlink-to-dir
    All,                 // -L
}

#[derive(Clone, Copy, PartialEq, Debug)]
enum Quoting {
    Literal,
    Escape,
    C,
    CMaybe,
    Shell,
    ShellAlways,
    ShellEscape,
    ShellEscapeAlways,
    Locale,
    Clocale,
}

#[derive(Clone, Copy, PartialEq, Debug)]
enum SizeMode {
    Bytes,
    Blocks,
    Human, // -h: 1024 base, K M G T P E
    Si,    // --si: 1000 base, k M G T P E
}

#[derive(Clone, Copy)]
struct SizeCfg {
    file_mode: SizeMode,
    file_block_size: u64,
    block_mode: SizeMode,
    block_size: u64,
}

impl Default for SizeCfg {
    fn default() -> Self {
        SizeCfg {
            file_mode: SizeMode::Bytes,
            file_block_size: 1,
            block_mode: SizeMode::Blocks,
            block_size: 1024,
        }
    }
}

#[derive(Clone, Copy, PartialEq)]
enum When {
    Always,
    Never,
    IfTty,
}

// when_args (ls.c:938-951): synonyms collapse to the same value, which is
// what makes a multi-candidate prefix like "n" unambiguous for argmatch.
const WHEN_NAMES: &[(&str, When)] = &[
    ("always", When::Always),
    ("yes", When::Always),
    ("force", When::Always),
    ("never", When::Never),
    ("no", When::Never),
    ("none", When::Never),
    ("auto", When::IfTty),
    ("tty", When::IfTty),
    ("if-tty", When::IfTty),
];

/// gnulib argmatch: exact or unique-prefix match. With a value list the
/// ambiguity is resolved when every candidate maps to the same value
/// (argmatch.c: "vallist resolve ambiguities limited to synonyms").
/// Returns Ok(index) / Err(true = ambiguous, false = invalid).
fn argmatch<'a, T: PartialEq>(value: &str, table: &[(&'a str, T)]) -> Result<usize, bool> {
    if let Some((i, _)) = table.iter().enumerate().find(|(_, (n, _))| *n == value) {
        return Ok(i);
    }
    let mut hit: Option<usize> = None;
    let mut ambiguous = false;
    for (i, (name, v)) in table.iter().enumerate() {
        if !value.is_empty() && name.starts_with(value) {
            match hit {
                None => hit = Some(i),
                Some(h) if table[h].1 == *v => hit = Some(h),
                _ => ambiguous = true,
            }
        }
    }
    if ambiguous {
        return Err(true);
    }
    match hit {
        Some(i) => Ok(i),
        None => Err(false),
    }
}

/// The `invalid|ambiguous argument 'v' for 'opt'` + "Valid arguments are:"
/// body. GNU prints grouped synonyms for the when table and bare names
/// elsewhere; `groups` carries the already-rendered lines.
fn argmatch_text(ctx: &Ctx, value: &str, opt: &str, ambiguous: bool, groups: &[&str]) -> String {
    let mut t = if ambiguous {
        tr!(
            ctx,
            "command.ls.error.ambiguous_argument",
            "ambiguous argument '{value}' for '{option}'",
            value = value,
            option = opt
        )
    } else {
        tr!(
            ctx,
            "command.ls.error.invalid_argument",
            "invalid argument '{value}' for '{option}'",
            value = value,
            option = opt
        )
    };
    t.push('\n');
    t.push_str(&tr!(
        ctx,
        "command.ls.error.valid_arguments",
        "Valid arguments are:"
    ));
    for g in groups {
        t.push_str("\n  - ");
        t.push_str(g);
    }
    t.push('\n');
    t.push_str(&tr!(
        ctx,
        "command.ls.error.try_help",
        "Try '{prog} --help' for more information.",
        prog = "ls"
    ));
    t
}

const WHEN_GROUPS: &[&str] = &[
    "'always', 'yes', 'force'",
    "'never', 'no', 'none'",
    "'auto', 'tty', 'if-tty'",
];

const SORT_TABLE: &[(&str, Sort)] = &[
    ("none", Sort::None),
    ("time", Sort::Time),
    ("size", Sort::Size),
    ("extension", Sort::Extension),
    ("version", Sort::Version),
    ("width", Sort::Width),
];
const SORT_GROUPS: &[&str] = &[
    "'none'",
    "'time'",
    "'size'",
    "'extension'",
    "'version'",
    "'width'",
];

const TIME_TABLE: &[(&str, TimeType)] = &[
    ("atime", TimeType::Atime),
    ("access", TimeType::Atime),
    ("use", TimeType::Atime),
    ("ctime", TimeType::Ctime),
    ("status", TimeType::Ctime),
    ("mtime", TimeType::Mtime),
    ("modification", TimeType::Mtime),
    ("birth", TimeType::Btime),
    ("creation", TimeType::Btime),
];

const FORMAT_TABLE: &[(&str, Format)] = &[
    ("verbose", Format::Long),
    ("long", Format::Long),
    ("commas", Format::Commas),
    ("horizontal", Format::Across),
    ("across", Format::Across),
    ("vertical", Format::Columns),
    ("single-column", Format::OnePerLine),
];

const INDICATOR_TABLE: &[(&str, Indicator)] = &[
    ("none", Indicator::None),
    ("slash", Indicator::Slash),
    ("file-type", Indicator::FileType),
    ("classify", Indicator::Classify),
];

const QUOTING_TABLE: &[(&str, Quoting)] = &[
    ("literal", Quoting::Literal),
    ("shell", Quoting::Shell),
    ("shell-always", Quoting::ShellAlways),
    ("shell-escape", Quoting::ShellEscape),
    ("shell-escape-always", Quoting::ShellEscapeAlways),
    ("c", Quoting::C),
    ("c-maybe", Quoting::CMaybe),
    ("escape", Quoting::Escape),
    ("locale", Quoting::Locale),
    ("clocale", Quoting::Clocale),
];

struct Cfg {
    ignore: Ignore,
    format_opt: Option<Format>,
    sort_opt: Option<Sort>,
    time_type: TimeType,
    time_style_arg: Option<String>,
    quoting: Option<Quoting>,
    hide_control: Option<bool>,
    indicator: Indicator,
    deref: Option<Deref>,
    print_inode: bool,
    print_block_size: bool,
    print_scontext: bool,
    print_owner: bool,
    print_group: bool,
    print_author: bool,
    numeric_ids: bool,
    immediate_dirs: bool,
    recursive: bool,
    dirs_first: bool,
    reverse: bool,
    ignore_pats: Vec<String>,
    hide_pats: Vec<String>,
    with_color: bool,
    with_hyperlink: bool,
    width_opt: Option<u64>, // decoded; 0 = no limit
    tabsize_opt: Option<u64>,
    size: SizeCfg,
    explicit_size: bool, // -h/--si/--block-size seen (blocks LS_BLOCK_SIZE + -k path)
    kibibytes: bool,
    eol: u8,
    dired: bool,
}

impl Default for Cfg {
    fn default() -> Self {
        Cfg {
            ignore: Ignore::Default,
            format_opt: None,
            sort_opt: None,
            time_type: TimeType::Mtime,
            time_style_arg: None,
            quoting: None,
            hide_control: None,
            indicator: Indicator::None,
            deref: None,
            print_inode: false,
            print_block_size: false,
            print_scontext: false,
            print_owner: true,
            print_group: true,
            print_author: false,
            numeric_ids: false,
            immediate_dirs: false,
            recursive: false,
            dirs_first: false,
            reverse: false,
            ignore_pats: Vec::new(),
            hide_pats: Vec::new(),
            with_color: false,
            with_hyperlink: false,
            width_opt: None,
            tabsize_opt: None,
            size: SizeCfg::default(),
            explicit_size: false,
            kibibytes: false,
            eol: b'\n',
            dired: false,
        }
    }
}

/// The interpreter's terminal conditions: getopt error (2), argmatch
/// failure (1, ls.c argmatch_die -> exit_failure stays 1 because the
/// argmatch exit happens through gnulib's own exit path — probed 9.4:
/// `ls --sort=bogus` -> rc 1, `ls --bogus` -> rc 2), or an early exit
/// (--help/--version 0, usage errors 2).
struct Early(u32);

/// decode_line_length (ls.c:1867-1886): xstrtoumax base-0 — leading
/// whitespace and '+' allowed, 0x hex, leading-0 octal; 0 or a value that
/// overflows ptrdiff means "no limit" (return 0); trailing junk, a bare
/// sign, or no digits -> -1.
fn decode_line_length(spec: &str) -> i64 {
    let b = spec.as_bytes();
    let mut i = 0usize;
    while i < b.len() && b[i].is_ascii_whitespace() {
        i += 1;
    }
    if i < b.len() && b[i] == b'+' {
        i += 1;
    }
    let mut base = 10u64;
    if i + 1 < b.len() && b[i] == b'0' && (b[i + 1] == b'x' || b[i + 1] == b'X') {
        base = 16;
        i += 2;
    } else if i < b.len() && b[i] == b'0' {
        base = 8;
    }
    const PTRDIFF_MAX: u64 = 0x7fff_ffff_ffff_ffff;
    let mut value: u64 = 0;
    let mut overflow = false;
    let start = i;
    while i < b.len() {
        let c = b[i];
        let digit = match c {
            b'0'..=b'9' => (c - b'0') as u64,
            b'a'..=b'f' => 10 + (c - b'a') as u64,
            b'A'..=b'F' => 10 + (c - b'A') as u64,
            _ => break,
        };
        if digit >= base {
            break;
        }
        if value > (PTRDIFF_MAX - digit) / base {
            overflow = true;
        } else {
            value = value * base + digit;
        }
        i += 1;
    }
    if i == start || i != b.len() {
        return -1;
    }
    if overflow || value == 0 || value > i32::MAX as u64 {
        return 0; // "no limit"
    }
    value as i64
}

/// strtoumax base-0 with full-consumption (the -T / TABSIZE decoder).
fn parse_base0(s: &str) -> Option<u64> {
    let b = s.as_bytes();
    let mut i = 0usize;
    if i < b.len() && (b[i] == b'+') {
        i += 1;
    }
    let mut base = 10u64;
    if i + 1 < b.len() && b[i] == b'0' && (b[i + 1] == b'x' || b[i + 1] == b'X') {
        base = 16;
        i += 2;
    } else if i < b.len() && b[i] == b'0' {
        base = 8;
    }
    let start = i;
    let mut v: u64 = 0;
    while i < b.len() {
        let c = b[i];
        let d = match c {
            b'0'..=b'9' => (c - b'0') as u64,
            b'a'..=b'f' => 10 + (c - b'a') as u64,
            b'A'..=b'F' => 10 + (c - b'A') as u64,
            _ => break,
        };
        if d >= base {
            break;
        }
        v = v.checked_mul(base)?.checked_add(d)?;
        i += 1;
    }
    if i == start || i != b.len() {
        return None;
    }
    Some(v)
}

fn when_match(ctx: &Ctx, value: &str, opt: &str) -> Result<When, String> {
    match argmatch(value, WHEN_NAMES) {
        Ok(i) => Ok(WHEN_NAMES[i].1),
        Err(amb) => Err(argmatch_text(ctx, value, opt, amb, WHEN_GROUPS)),
    }
}

fn tty_stdout() -> bool {
    plat::stdout_is_console()
}

/// Apply every Occ in argv order — the positional contract of
/// decode_switches (`ls -f -l` != `ls -l -f`; argmatch failures die at
/// their argv position with rc 1; -w/-T value errors die with rc 2;
/// getopt errors recorded by scan() die with rc 2 at their position).
fn interpret(occs: &[Occ], io: &mut Io, ctx: &Ctx) -> Result<Cfg, Early> {
    let mut c = Cfg::default();
    for occ in occs {
        match occ {
            Occ::Help => {
                let _ = write!(
                    io.stdout,
                    "{}{}",
                    LS_HELP_BODY,
                    gnuhelp::footer("ls", "ls invocation")
                );
                return Err(Early(0));
            }
            Occ::Version => {
                let _ = write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("ls", "Richard M. Stallman and David MacKenzie")
                );
                return Err(Early(0));
            }
            Occ::Err(msg) => {
                let _ = usage_error(ctx, io, "ls", msg);
                return Err(Early(2));
            }
            _ => {}
        }
        let (key, val): (&'static str, Option<&str>) = match occ {
            Occ::Flag(k) => (*k, None),
            Occ::Bare(k) => (*k, None),
            Occ::Val(k, v) => (*k, Some(v)),
            _ => unreachable!(),
        };
        match key {
            "all" => c.ignore = Ignore::Minimal,
            "almost-all" => c.ignore = Ignore::DotDot,
            "escape" => c.quoting = Some(Quoting::Escape),
            "directory" => c.immediate_dirs = true,
            "dired" => c.dired = true,
            "full-time" => {
                c.format_opt = Some(Format::Long);
                c.time_style_arg = Some("full-iso".to_string());
            }
            "group-directories-first" => c.dirs_first = true,
            "human-readable" => {
                c.size.file_mode = SizeMode::Human;
                c.size.file_block_size = 1;
                c.size.block_mode = SizeMode::Human;
                c.size.block_size = 1;
                c.explicit_size = true;
            }
            "inode" => c.print_inode = true,
            "kibibytes" => c.kibibytes = true,
            "numeric-uid-gid" => {
                c.numeric_ids = true;
                c.format_opt = Some(Format::Long);
            }
            "no-group" => c.print_group = false,
            "hide-control-chars" => c.hide_control = Some(true),
            "reverse" => c.reverse = true,
            "size" => c.print_block_size = true,
            "width" => {
                let v = val.unwrap_or("");
                let d = decode_line_length(v);
                if d < 0 {
                    ls_error(
                        io,
                        &tr!(
                            ctx,
                            "command.ls.error.invalid_line_width",
                            "invalid line width: \u{2018}{value}\u{2019}",
                            value = v
                        ),
                    );
                    return Err(Early(2));
                }
                c.width_opt = Some(d as u64);
            }
            "ignore-backups" => {
                c.ignore_pats.push("*~".to_string());
                c.ignore_pats.push(".*~".to_string());
            }
            "classify" => {
                let when = match val {
                    None => When::Always,
                    Some(v) => match when_match(ctx, v, "--classify") {
                        Ok(w) => w,
                        Err(msg) => {
                            let _ = writeln!(io.stderr, "ls: {msg}");
                            return Err(Early(1));
                        }
                    },
                };
                if when == When::Always || (when == When::IfTty && tty_stdout()) {
                    c.indicator = Indicator::Classify;
                }
            }
            "file-type" => c.indicator = Indicator::FileType,
            "si" => {
                c.size.file_mode = SizeMode::Si;
                c.size.file_block_size = 1;
                c.size.block_mode = SizeMode::Si;
                c.size.block_size = 1;
                c.explicit_size = true;
            }
            "dereference-command-line" => c.deref = Some(Deref::CmdlineAll),
            "dereference-command-line-symlink-to-dir" => c.deref = Some(Deref::CmdlineSymlinkToDir),
            "hide" => c.hide_pats.push(val.unwrap_or_default().to_string()),
            "ignore" => c.ignore_pats.push(val.unwrap_or_default().to_string()),
            "indicator-style" => {
                let v = val.unwrap_or("");
                match argmatch(v, INDICATOR_TABLE) {
                    Ok(i) => c.indicator = INDICATOR_TABLE[i].1,
                    Err(amb) => {
                        let _ = writeln!(
                            io.stderr,
                            "ls: {}",
                            argmatch_text(
                                ctx,
                                v,
                                "--indicator-style",
                                amb,
                                &["'none'", "'slash'", "'file-type'", "'classify'"]
                            )
                        );
                        return Err(Early(1));
                    }
                }
            }
            "dereference" => c.deref = Some(Deref::All),
            "literal" => c.quoting = Some(Quoting::Literal),
            "quote-name" => c.quoting = Some(Quoting::C),
            "quoting-style" => {
                let v = val.unwrap_or("");
                match argmatch(v, QUOTING_TABLE) {
                    Ok(i) => c.quoting = Some(QUOTING_TABLE[i].1),
                    Err(amb) => {
                        let _ = writeln!(
                            io.stderr,
                            "ls: {}",
                            argmatch_text(
                                ctx,
                                v,
                                "--quoting-style",
                                amb,
                                &[
                                    "'literal'",
                                    "'shell'",
                                    "'shell-always'",
                                    "'shell-escape'",
                                    "'shell-escape-always'",
                                    "'c'",
                                    "'c-maybe'",
                                    "'escape'",
                                    "'locale'",
                                    "'clocale'",
                                ]
                            )
                        );
                        return Err(Early(1));
                    }
                }
            }
            "recursive" => c.recursive = true,
            "format" => {
                let v = val.unwrap_or("");
                match argmatch(v, FORMAT_TABLE) {
                    Ok(i) => c.format_opt = Some(FORMAT_TABLE[i].1),
                    Err(amb) => {
                        let _ = writeln!(
                            io.stderr,
                            "ls: {}",
                            argmatch_text(
                                ctx,
                                v,
                                "--format",
                                amb,
                                &[
                                    "'verbose'",
                                    "'long'",
                                    "'commas'",
                                    "'horizontal'",
                                    "'across'",
                                    "'vertical'",
                                    "'single-column'",
                                ]
                            )
                        );
                        return Err(Early(1));
                    }
                }
            }
            "show-control-chars" => c.hide_control = Some(false),
            "sort" => {
                let v = val.unwrap_or("");
                match argmatch(v, SORT_TABLE) {
                    Ok(i) => c.sort_opt = Some(SORT_TABLE[i].1),
                    Err(amb) => {
                        let _ = writeln!(
                            io.stderr,
                            "ls: {}",
                            argmatch_text(ctx, v, "--sort", amb, SORT_GROUPS)
                        );
                        return Err(Early(1));
                    }
                }
            }
            "tabsize" => {
                let v = val.unwrap_or("");
                match parse_base0(v) {
                    Some(n) if n <= i32::MAX as u64 => c.tabsize_opt = Some(n),
                    _ => {
                        ls_error(
                            io,
                            &tr!(
                                ctx,
                                "command.ls.error.invalid_tab_size",
                                "invalid tab size: '{value}'",
                                value = v
                            ),
                        );
                        return Err(Early(2));
                    }
                }
            }
            "time" => {
                let v = val.unwrap_or("");
                match argmatch(v, TIME_TABLE) {
                    Ok(i) => c.time_type = TIME_TABLE[i].1,
                    Err(amb) => {
                        let _ = writeln!(
                            io.stderr,
                            "ls: {}",
                            argmatch_text(
                                ctx,
                                v,
                                "--time",
                                amb,
                                &[
                                    "'atime', 'access', 'use'",
                                    "'ctime', 'status'",
                                    "'mtime', 'modification'",
                                    "'birth', 'creation'",
                                ]
                            )
                        );
                        return Err(Early(1));
                    }
                }
            }
            "time-style" => c.time_style_arg = Some(val.unwrap_or("").to_string()),
            "zero" => {
                c.eol = 0;
                c.hide_control = Some(false);
                if c.format_opt != Some(Format::Long) {
                    c.format_opt = Some(Format::OnePerLine);
                }
                c.with_color = false;
                c.quoting = Some(Quoting::Literal);
            }
            "color" => {
                let when = match val {
                    None => When::Always,
                    Some(v) => match when_match(ctx, v, "--color") {
                        Ok(w) => w,
                        Err(msg) => {
                            let _ = writeln!(io.stderr, "ls: {msg}");
                            return Err(Early(1));
                        }
                    },
                };
                c.with_color = when == When::Always || (when == When::IfTty && tty_stdout());
            }
            "hyperlink" => {
                let when = match val {
                    None => When::Always,
                    Some(v) => match when_match(ctx, v, "--hyperlink") {
                        Ok(w) => w,
                        Err(msg) => {
                            let _ = writeln!(io.stderr, "ls: {msg}");
                            return Err(Early(1));
                        }
                    },
                };
                c.with_hyperlink = when == When::Always || (when == When::IfTty && tty_stdout());
            }
            "block-size" => {
                let v = val.unwrap_or("");
                if v == "human-readable" {
                    c.size.file_mode = SizeMode::Human;
                    c.size.file_block_size = 1;
                    c.size.block_mode = SizeMode::Human;
                    c.size.block_size = 1;
                    c.explicit_size = true;
                } else if v == "si" {
                    c.size.file_mode = SizeMode::Si;
                    c.size.file_block_size = 1;
                    c.size.block_mode = SizeMode::Si;
                    c.size.block_size = 1;
                    c.explicit_size = true;
                } else {
                    match parse_block_size(v) {
                        Some(n) => {
                            c.size.file_mode = SizeMode::Blocks;
                            c.size.file_block_size = n;
                            c.size.block_mode = SizeMode::Blocks;
                            c.size.block_size = n;
                            c.explicit_size = true;
                        }
                        None => {
                            ls_error(io, &format!("invalid --block-size argument '{v}'"));
                            return Err(Early(2));
                        }
                    }
                }
            }
            "context" => c.print_scontext = true,
            "author" => c.print_author = true,
            // short-only options (no long spelling in GNU)
            "c" => c.time_type = TimeType::Ctime,
            "f" => {
                // ls.c case 'f': -a -U -1 --color=none --hyperlink=none and
                // print_block_size off; cancels an EARLIER -l only.
                c.ignore = Ignore::Minimal;
                c.sort_opt = Some(Sort::None);
                if c.format_opt == Some(Format::Long) {
                    c.format_opt = None;
                }
                c.with_color = false;
                c.with_hyperlink = false;
                c.print_block_size = false;
            }
            "g" => {
                c.format_opt = Some(Format::Long);
                c.print_owner = false;
            }
            "l" => c.format_opt = Some(Format::Long),
            "m" => c.format_opt = Some(Format::Commas),
            "o" => {
                c.format_opt = Some(Format::Long);
                c.print_group = false;
            }
            "p" => c.indicator = Indicator::Slash,
            "t" => c.sort_opt = Some(Sort::Time),
            "u" => c.time_type = TimeType::Atime,
            "v" => c.sort_opt = Some(Sort::Version),
            "x" => c.format_opt = Some(Format::Across),
            "C" => c.format_opt = Some(Format::Columns),
            "1" => {
                if c.format_opt != Some(Format::Long) {
                    c.format_opt = Some(Format::OnePerLine);
                }
            }
            "S" => c.sort_opt = Some(Sort::Size),
            "U" => c.sort_opt = Some(Sort::None),
            "X" => c.sort_opt = Some(Sort::Extension),
            _ => {}
        }
    }

    // LS_BLOCK_SIZE / BLOCK_SIZE env: only when no explicit size option
    // set output_block_size (ls.c:2229-2241); -k then wins inside the same
    // branch.
    if !c.explicit_size {
        if let Some(v) = env(ctx, "LS_BLOCK_SIZE").or_else(|| env(ctx, "BLOCK_SIZE")) {
            if v == "human-readable" {
                c.size.file_mode = SizeMode::Human;
                c.size.file_block_size = 1;
                c.size.block_mode = SizeMode::Human;
                c.size.block_size = 1;
            } else if v == "si" {
                c.size.file_mode = SizeMode::Si;
                c.size.file_block_size = 1;
                c.size.block_mode = SizeMode::Si;
                c.size.block_size = 1;
            } else if let Some(n) = parse_block_size(&v) {
                c.size.file_mode = SizeMode::Blocks;
                c.size.file_block_size = n;
                c.size.block_mode = SizeMode::Blocks;
                c.size.block_size = n;
            }
            if c.kibibytes {
                c.size.file_mode = SizeMode::Blocks;
                c.size.file_block_size = 1024;
                c.size.block_mode = SizeMode::Blocks;
                c.size.block_size = 1024;
            }
        } else if c.kibibytes {
            c.size.file_mode = SizeMode::Blocks;
            c.size.file_block_size = 1024;
            c.size.block_mode = SizeMode::Blocks;
            c.size.block_size = 1024;
        }
    }
    Ok(c)
}

/// Case-insensitive Win32 env lookup (the CRT getenv the 1.x applets use).
fn env(ctx: &Ctx, key: &str) -> Option<String> {
    winux_core::win32_env_get(ctx.env, key).map(|v| v.to_string_lossy().into_owned())
}

/// --block-size SIZE parser: strtoumax digits + unit suffix (human.c
/// human_options table; K/M/... are 1024-based, k lowercase is only the
/// --si suffix letter and is NOT accepted as a SIZE unit there — GNU's
/// --block-size accepts SI letters K M G T P E plus k which strtoumax
/// reads as 1024 anyway; 'B' = 1, 'b' = 512, KiB/MiB/... IEC forms, and
/// KB/MB/... decimal forms).
fn parse_block_size(text: &str) -> Option<u64> {
    if text.is_empty() {
        return None;
    }
    const UNITS: &[(&str, u64)] = &[
        ("KiB", 1024),
        ("MiB", 1024 * 1024),
        ("GiB", 1024 * 1024 * 1024),
        ("TiB", 1024 * 1024 * 1024 * 1024),
        ("PiB", 1024 * 1024 * 1024 * 1024 * 1024),
        ("EiB", 1024 * 1024 * 1024 * 1024 * 1024 * 1024),
        ("KB", 1_000),
        ("MB", 1_000_000),
        ("GB", 1_000_000_000),
        ("TB", 1_000_000_000_000),
        ("PB", 1_000_000_000_000_000),
        ("EB", 1_000_000_000_000_000_000),
        ("K", 1024),
        ("M", 1024 * 1024),
        ("G", 1024 * 1024 * 1024),
        ("T", 1024 * 1024 * 1024 * 1024),
        ("P", 1024 * 1024 * 1024 * 1024 * 1024),
        ("E", 1024 * 1024 * 1024 * 1024 * 1024 * 1024),
        ("k", 1024),
        ("m", 1024 * 1024),
        ("g", 1024 * 1024 * 1024),
        ("t", 1024 * 1024 * 1024 * 1024),
        ("p", 1024 * 1024 * 1024 * 1024 * 1024),
        ("e", 1024 * 1024 * 1024 * 1024 * 1024 * 1024),
        ("b", 512),
        ("B", 1),
    ];
    let mut multiplier = 1u64;
    let mut number = text;
    let mut matched = false;
    for (suffix, mult) in UNITS {
        if text.len() >= suffix.len() && text.ends_with(suffix) {
            multiplier = *mult;
            number = &text[..text.len() - suffix.len()];
            matched = true;
            break;
        }
    }
    let mut value = 1u64;
    if !number.is_empty() {
        value = number.parse::<u64>().ok()?;
        if !number.chars().all(|c| c.is_ascii_digit()) {
            return None;
        }
    } else if !matched {
        return None;
    }
    if value == 0 || value > u64::MAX / multiplier {
        return None;
    }
    Some(value * multiplier)
}

// ======================================================================
// Platform layer — the 1.x ls.cpp value model over Win32.
// ======================================================================

pub const ATTR_READONLY: u32 = 0x1;
pub const ATTR_HIDDEN: u32 = 0x2;
pub const ATTR_SYSTEM: u32 = 0x4;
pub const ATTR_DIRECTORY: u32 = 0x10;
pub const ATTR_NORMAL: u32 = 0x80;
pub const ATTR_DEVICE: u32 = 0x40;
pub const ATTR_REPARSE: u32 = 0x400;

pub const TAG_SYMLINK: u32 = 0xA000_000C;
pub const TAG_MOUNT_POINT: u32 = 0xA000_0003;
/// WSL stores its links with this private tag (ls.cpp kReparseTagLxSymlink).
pub const TAG_LX_SYMLINK: u32 = 0xA000_001D;

/// WIN32_FIND_DATAW's fields ls uses, in FILETIME ticks (i64).
#[derive(Clone, Copy, Default)]
pub struct Fd {
    pub attrs: u32,
    pub tag: u32,
    pub size: u64,
    pub ctime: i64,
    pub atime: i64,
    pub mtime: i64,
}

impl Fd {
    pub fn is_dir(&self) -> bool {
        self.attrs & ATTR_DIRECTORY != 0
    }
    pub fn is_reparse(&self) -> bool {
        self.attrs & ATTR_REPARSE != 0
    }
    /// Symbolic reparse = SYMLINK or LX tag (#1639: MOUNT_POINT keeps
    /// junction identity — it is a link for display but not a symbolic
    /// link for size semantics).
    pub fn is_symbolic_link(&self) -> bool {
        self.is_reparse() && (self.tag == TAG_SYMLINK || self.tag == TAG_LX_SYMLINK)
    }
    pub fn is_plain_dir(&self) -> bool {
        self.is_dir() && !self.is_reparse()
    }
    pub fn is_chardev(&self) -> bool {
        self.attrs & ATTR_DEVICE != 0
    }
    pub fn read_only(&self) -> bool {
        self.attrs & ATTR_READONLY != 0
    }
    pub fn hidden(&self) -> bool {
        self.attrs & ATTR_HIDDEN != 0
    }
    pub fn system(&self) -> bool {
        self.attrs & ATTR_SYSTEM != 0
    }
}

/// A listed entry: display name, metadata probe path, find_data.
#[derive(Clone)]
struct Entry {
    name: String,
    full_path: String,
    fd: Fd,
    command_line: bool,
    /// GNU filetype == arg_directory: a directory operand that will be
    /// extracted into the pending-dir queue (never set with -d).
    arg_dir: bool,
    /// -L/-H stat failure inside a listing: '?' placeholder fields.
    stat_failed: bool,
}

#[cfg(windows)]
mod plat {
    use super::Fd;
    use std::ffi::OsStr;
    use std::os::windows::ffi::OsStrExt;

    /// FILETIME is a pair of DWORDs — 4-byte aligned inside these Win32
    /// structs, not 8.
    #[repr(C)]
    #[derive(Clone, Copy, Default)]
    struct Ft {
        lo: u32,
        hi: u32,
    }

    impl Ft {
        fn ticks(&self) -> u64 {
            (self.lo as u64) | ((self.hi as u64) << 32)
        }
    }

    #[allow(non_snake_case)]
    #[repr(C)]
    struct Wfd {
        attrs: u32,
        ctime: Ft,
        atime: Ft,
        mtime: Ft,
        size_hi: u32,
        size_lo: u32,
        reserved0: u32,
        reserved1: u32,
        name: [u16; 260],
        alt: [u16; 14],
    }

    type Handle = *mut core::ffi::c_void;
    const INVALID_HANDLE: Handle = -1isize as Handle;

    #[allow(clashing_extern_declarations)]
    #[link(name = "kernel32")]
    extern "system" {
        fn FindFirstFileW(path: *const u16, data: *mut Wfd) -> Handle;
        fn FindNextFileW(h: Handle, data: *mut Wfd) -> i32;
        fn FindClose(h: Handle) -> i32;
        fn GetLastError() -> u32;
        fn GetFileAttributesW(path: *const u16) -> u32;
        fn GetFileAttributesExW(path: *const u16, level: i32, data: *mut u8) -> i32;
        fn CreateFileW(
            path: *const u16,
            access: u32,
            share: u32,
            sa: *mut core::ffi::c_void,
            disp: u32,
            flags: u32,
            tmpl: Handle,
        ) -> Handle;
        fn CloseHandle(h: Handle) -> i32;
        fn GetFileInformationByHandle(h: Handle, info: *mut ByHandle) -> i32;
        fn GetFileInformationByHandleEx(
            h: Handle,
            class: i32,
            info: *mut StdInfo,
            size: u32,
        ) -> i32;
        fn GetCompressedFileSizeW(path: *const u16, hi: *mut u32) -> u32;
        fn DeviceIoControl(
            h: Handle,
            code: u32,
            inbuf: *const u8,
            insize: u32,
            outbuf: *mut u8,
            outsize: u32,
            ret: *mut u32,
            overlapped: *mut core::ffi::c_void,
        ) -> i32;
        fn GetStdHandle(which: u32) -> Handle;
        fn GetConsoleMode(h: Handle, mode: *mut u32) -> i32;
        fn GetConsoleScreenBufferInfo(h: Handle, info: *mut Csbi) -> i32;
        fn GetComputerNameA(buf: *mut u8, size: *mut u32) -> i32;
        fn GetSystemTimeAsFileTime(ft: *mut u64);
        fn FileTimeToLocalFileTime(utc: *const u64, local: *mut u64) -> i32;
        fn FileTimeToSystemTime(ft: *const u64, st: *mut Syst) -> i32;
    }

    #[repr(C)]
    #[derive(Default)]
    struct ByHandle {
        attrs: u32,
        ctime: Ft,
        atime: Ft,
        mtime: Ft,
        vol: u32,
        size_hi: u32,
        size_lo: u32,
        links: u32,
        idx_hi: u32,
        idx_lo: u32,
    }

    /// FILE_STANDARD_INFO: AllocationSize/EndOfFile i64, NumberOfLinks
    /// u32, then two BOOLEANs.
    #[repr(C)]
    #[derive(Default)]
    struct StdInfo {
        alloc: i64,
        eof: i64,
        links: u32,
        delete_pending: u8,
        directory: u8,
    }

    #[repr(C)]
    struct Csbi {
        size: [i16; 2],
        cursor: [i16; 2],
        attrs: u16,
        window: [i16; 4],
        max: [i16; 2],
    }

    #[repr(C)]
    #[derive(Default)]
    struct Syst {
        year: u16,
        month: u16,
        dow: u16,
        day: u16,
        hour: u16,
        minute: u16,
        second: u16,
        millis: u16,
    }

    pub fn wide(s: &str) -> Vec<u16> {
        OsStr::new(s)
            .encode_wide()
            .chain(std::iter::once(0))
            .collect()
    }

    fn wfd_to_fd(d: &Wfd) -> Fd {
        Fd {
            attrs: d.attrs,
            tag: d.reserved0,
            size: d.size_lo as u64 | ((d.size_hi as u64) << 32),
            ctime: d.ctime.ticks() as i64,
            atime: d.atime.ticks() as i64,
            mtime: d.mtime.ticks() as i64,
        }
    }

    fn wfd_name(d: &Wfd) -> String {
        let len = d.name.iter().position(|&c| c == 0).unwrap_or(d.name.len());
        String::from_utf16_lossy(&d.name[..len])
    }

    /// FindFirstFileW probe — returns the WIN32_FIND_DATAW-shaped Fd.
    pub fn find_first(extended: &str) -> Option<Fd> {
        let w = wide(extended);
        let mut d = Wfd {
            name: [0; 260],
            alt: [0; 14],
            ..unsafe { std::mem::zeroed() }
        };
        let h = unsafe { FindFirstFileW(w.as_ptr(), &mut d) };
        if h == INVALID_HANDLE || h.is_null() {
            return None;
        }
        unsafe { FindClose(h) };
        Some(wfd_to_fd(&d))
    }

    /// Enumerate `extended\*` -> (name, Fd) pairs, or the Win32 error.
    pub fn enumerate(extended: &str) -> Result<Vec<(String, Fd)>, u32> {
        let w = wide(&format!("{extended}\\*"));
        let mut d: Wfd = unsafe { std::mem::zeroed() };
        let h = unsafe { FindFirstFileW(w.as_ptr(), &mut d) };
        if h == INVALID_HANDLE || h.is_null() {
            return Err(unsafe { GetLastError() });
        }
        let mut out = Vec::new();
        loop {
            out.push((wfd_name(&d), wfd_to_fd(&d)));
            d = unsafe { std::mem::zeroed() };
            if unsafe { FindNextFileW(h, &mut d) } == 0 {
                break;
            }
        }
        unsafe { FindClose(h) };
        Ok(out)
    }

    /// GetFileAttributesW probe (no handle).
    pub fn attributes(extended: &str) -> Option<u32> {
        let w = wide(extended);
        match unsafe { GetFileAttributesW(w.as_ptr()) } {
            0xFFFF_FFFF => None,
            a => Some(a),
        }
    }

    /// GetFileAttributesExW probe (post-canonicalization deref path).
    pub fn attributes_ex(extended: &str) -> Option<Fd> {
        #[repr(C)]
        struct AttrEx {
            attrs: u32,
            ctime: Ft,
            atime: Ft,
            mtime: Ft,
            size_hi: u32,
            size_lo: u32,
        }
        let w = wide(extended);
        let mut d: AttrEx = unsafe { std::mem::zeroed() };
        if unsafe { GetFileAttributesExW(w.as_ptr(), 0, (&mut d as *mut AttrEx).cast()) } == 0 {
            return None;
        }
        Some(Fd {
            attrs: d.attrs,
            tag: 0,
            size: d.size_lo as u64 | ((d.size_hi as u64) << 32),
            ctime: d.ctime.ticks() as i64,
            atime: d.atime.ticks() as i64,
            mtime: d.mtime.ticks() as i64,
        })
    }

    const SHARE_ALL: u32 = 0x7;
    const OPEN_EXISTING: u32 = 3;
    const FILE_READ_ATTRIBUTES: u32 = 0x80;
    const FILE_READ_EA: u32 = 0x8;
    const FLAG_BACKUP_SEMANTICS: u32 = 0x0200_0000;
    const FLAG_OPEN_REPARSE: u32 = 0x0020_0000;

    /// CreateFileW + GetFileInformationByHandle -> (links, index). Flags
    /// follow ls.cpp get_link_count/get_file_index_string: dirs open with
    /// BACKUP_SEMANTICS, un-dereferenced reparse points with
    /// OPEN_REPARSE_POINT.
    pub fn handle_info(extended: &str, fd: &Fd, dereference: bool) -> Option<(u32, u64)> {
        let mut flags = 0u32;
        if fd.attrs & super::ATTR_DIRECTORY != 0 {
            flags |= FLAG_BACKUP_SEMANTICS;
        }
        if fd.attrs & super::ATTR_REPARSE != 0 && !dereference {
            flags |= FLAG_OPEN_REPARSE;
        }
        let w = wide(extended);
        let h = unsafe {
            CreateFileW(
                w.as_ptr(),
                FILE_READ_ATTRIBUTES,
                SHARE_ALL,
                std::ptr::null_mut(),
                OPEN_EXISTING,
                flags,
                std::ptr::null_mut(),
            )
        };
        if h == INVALID_HANDLE || h.is_null() {
            return None;
        }
        let mut info: ByHandle = unsafe { std::mem::zeroed() };
        let ok = unsafe { GetFileInformationByHandle(h, &mut info) } != 0;
        unsafe { CloseHandle(h) };
        if !ok {
            return None;
        }
        Some((
            info.links.max(1),
            ((info.idx_hi as u64) << 32) | info.idx_lo as u64,
        ))
    }

    /// FILE_STANDARD_INFO EndOfFile/AllocationSize for a directory
    /// (ls.cpp query_directory_standard_size_bytes).
    pub fn dir_standard_size(extended: &str, allocation: bool) -> Option<u64> {
        let w = wide(extended);
        let h = unsafe {
            CreateFileW(
                w.as_ptr(),
                FILE_READ_ATTRIBUTES,
                SHARE_ALL,
                std::ptr::null_mut(),
                OPEN_EXISTING,
                FLAG_BACKUP_SEMANTICS,
                std::ptr::null_mut(),
            )
        };
        if h == INVALID_HANDLE || h.is_null() {
            return None;
        }
        let mut info: StdInfo = unsafe { std::mem::zeroed() };
        let ok = unsafe {
            GetFileInformationByHandleEx(h, 1, &mut info, std::mem::size_of::<StdInfo>() as u32)
        } != 0;
        unsafe { CloseHandle(h) };
        if !ok {
            return None;
        }
        let v = if allocation { info.alloc } else { info.eof };
        (v >= 0).then_some(v as u64)
    }

    /// GetCompressedFileSizeW — the allocated size; None on API failure
    /// (the C++ falls back to the logical nFileSize then).
    pub fn compressed_size(extended: &str) -> Option<u64> {
        let w = wide(extended);
        let mut hi: u32 = 0;
        let lo = unsafe { GetCompressedFileSizeW(w.as_ptr(), &mut hi) };
        if lo == u32::MAX && unsafe { GetLastError() } != 0 {
            return None;
        }
        Some(((hi as u64) << 32) | lo as u64)
    }

    const FSCTL_GET_REPARSE_POINT: u32 = 0x0009_00A8;

    /// FSCTL_GET_REPARSE_POINT read of the link target (#1639 port):
    /// SYMLINK print-name (falling back to substitute-name), LX UTF-8
    /// payload, MOUNT_POINT substitute-name — each with the NT namespace
    /// prefix stripped to a display spelling.
    pub fn read_reparse_target(extended: &str) -> Option<String> {
        let w = wide(extended);
        let h = unsafe {
            CreateFileW(
                w.as_ptr(),
                FILE_READ_EA,
                SHARE_ALL,
                std::ptr::null_mut(),
                OPEN_EXISTING,
                FLAG_OPEN_REPARSE | FLAG_BACKUP_SEMANTICS,
                std::ptr::null_mut(),
            )
        };
        if h == INVALID_HANDLE || h.is_null() {
            return None;
        }
        let mut buf = vec![0u8; 16 * 1024];
        let mut ret: u32 = 0;
        let ok = unsafe {
            DeviceIoControl(
                h,
                FSCTL_GET_REPARSE_POINT,
                std::ptr::null(),
                0,
                buf.as_mut_ptr(),
                buf.len() as u32,
                &mut ret,
                std::ptr::null_mut(),
            )
        } != 0;
        unsafe { CloseHandle(h) };
        if !ok || ret < 8 {
            return None;
        }
        let buf = &buf[..ret as usize];
        let tag = u32::from_le_bytes(buf[0..4].try_into().unwrap());
        let payload = &buf[8..];
        let read_u16 = |o: usize| -> u16 {
            u16::from_le_bytes(payload[o..o + 2].try_into().unwrap_or([0, 0]))
        };
        let name_at = |off: usize, len: usize| -> String {
            if off + len > payload.len() || len % 2 != 0 {
                return String::new();
            }
            let units: Vec<u16> = payload[off..off + len]
                .chunks_exact(2)
                .map(|c| u16::from_le_bytes([c[0], c[1]]))
                .collect();
            let mut s = String::from_utf16_lossy(&units);
            while s.ends_with('\0') {
                s.pop();
            }
            s
        };
        let strip = |mut v: String| -> String {
            for p in [r"\\?\UNC\", r"\??\UNC\"] {
                if let Some(rest) = v.strip_prefix(p) {
                    return format!(r"\\{rest}");
                }
            }
            for p in [r"\\?\", r"\??\"] {
                if let Some(rest) = v.strip_prefix(p) {
                    v = rest.to_string();
                }
            }
            v
        };
        match tag {
            super::TAG_SYMLINK if payload.len() >= 20 => {
                let sub_off = read_u16(0) as usize;
                let sub_len = read_u16(2) as usize;
                let prt_off = read_u16(4) as usize;
                let prt_len = read_u16(6) as usize;
                let p = name_at(12 + prt_off, prt_len);
                if !p.is_empty() {
                    return Some(strip(p));
                }
                let n = name_at(12 + sub_off, sub_len);
                if !n.is_empty() {
                    return Some(strip(n));
                }
                None
            }
            super::TAG_LX_SYMLINK if payload.len() >= 4 => {
                // version u32 + UTF-8 target without terminator.
                let mut bytes = &payload[4..];
                while bytes.last() == Some(&0) {
                    bytes = &bytes[..bytes.len() - 1];
                }
                Some(String::from_utf8_lossy(bytes).into_owned())
            }
            super::TAG_MOUNT_POINT if payload.len() >= 16 => {
                let sub_off = read_u16(0) as usize;
                let sub_len = read_u16(2) as usize;
                let n = name_at(8 + sub_off, sub_len);
                if !n.is_empty() {
                    return Some(strip(n));
                }
                None
            }
            _ => None,
        }
    }

    pub fn stdout_is_console() -> bool {
        const STD_OUTPUT_HANDLE: u32 = 0xFFFF_FFF5;
        let mut mode: u32 = 0;
        unsafe { GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mut mode) != 0 }
    }

    /// GetConsoleScreenBufferInfo window columns (0 on non-console).
    pub fn console_width() -> i32 {
        const STD_OUTPUT_HANDLE: u32 = 0xFFFF_FFF5;
        let h = unsafe { GetStdHandle(STD_OUTPUT_HANDLE) };
        let mut csbi: Csbi = unsafe { std::mem::zeroed() };
        if unsafe { GetConsoleScreenBufferInfo(h, &mut csbi) } == 0 {
            return 0;
        }
        let cols = (csbi.window[2] - csbi.window[0] + 1) as i32;
        if cols > 0 {
            cols
        } else {
            0
        }
    }

    pub fn computer_name() -> String {
        let mut buf = [0u8; 64];
        let mut len = buf.len() as u32;
        if unsafe { GetComputerNameA(buf.as_mut_ptr(), &mut len) } != 0 {
            String::from_utf8_lossy(&buf[..len as usize]).into_owned()
        } else {
            "localhost".to_string()
        }
    }

    pub fn now_filetime() -> i64 {
        let mut ft: u64 = 0;
        unsafe { GetSystemTimeAsFileTime(&mut ft) };
        ft as i64
    }

    /// FILETIME (UTC) -> local FILETIME via the system timezone
    /// (ls.cpp FileTimeToLocalFileTime; %z offset derives from the delta).
    pub fn to_local_filetime(ft: i64) -> Option<(i64, i32)> {
        let u = ft as u64;
        let mut l: u64 = 0;
        if unsafe { FileTimeToLocalFileTime(&u, &mut l) } == 0 {
            return None;
        }
        let delta = l as i64 - ft;
        let mins = ((delta + 30_000_000) / 600_000_000) as i32;
        Some((l as i64, mins))
    }

    /// FILETIME -> broken-down UTC fields.
    pub fn to_systemtime(ft: i64) -> Option<(u16, u16, u16, u16, u16, u16, u16)> {
        let u = ft as u64;
        let mut st: Syst = unsafe { std::mem::zeroed() };
        if unsafe { FileTimeToSystemTime(&u, &mut st) } == 0 {
            return None;
        }
        Some((
            st.year, st.month, st.day, st.hour, st.minute, st.second, st.millis,
        ))
    }
}

#[cfg(not(windows))]
mod plat {
    //! Non-Windows build support (crate compiles/tests on Unix hosts):
    //! std::fs metadata in the same Fd shape; times converted to FILETIME
    //! ticks. This path is never exercised by the Windows gate.
    use super::Fd;

    const TICKS_PER_SEC: i64 = 10_000_000;
    const EPOCH_DELTA: i64 = 11_644_473_600;

    fn to_ft(t: std::time::SystemTime) -> i64 {
        match t.duration_since(std::time::UNIX_EPOCH) {
            Ok(d) => {
                (d.as_secs() as i64 + EPOCH_DELTA) * TICKS_PER_SEC + d.subsec_nanos() as i64 / 100
            }
            Err(_) => 0,
        }
    }

    fn meta_to_fd(m: &std::fs::Metadata, path: &std::path::Path) -> Fd {
        use std::os::unix::fs::FileTypeExt;
        let ft = m.file_type();
        let mut fd = Fd {
            attrs: 0,
            tag: 0,
            size: m.len(),
            ctime: m.created().map(to_ft).unwrap_or(0),
            atime: m.accessed().map(to_ft).unwrap_or(0),
            mtime: m.modified().map(to_ft).unwrap_or(0),
            ..Fd::default()
        };
        if ft.is_dir() {
            fd.attrs |= super::ATTR_DIRECTORY;
        }
        if ft.is_symlink() {
            fd.attrs |= super::ATTR_REPARSE;
            fd.tag = super::TAG_SYMLINK;
        }
        if ft.is_char_device() || ft.is_block_device() {
            fd.attrs |= super::ATTR_DEVICE;
        }
        if ft.is_fifo() {
            fd.attrs |= super::ATTR_SYSTEM; // reused as the fifo flag here
        }
        if m.permissions().readonly() {
            fd.attrs |= super::ATTR_READONLY;
        }
        let _ = path;
        fd
    }

    pub fn find_first(path: &str) -> Option<Fd> {
        std::fs::symlink_metadata(path)
            .ok()
            .map(|m| meta_to_fd(&m, std::path::Path::new(path)))
    }

    pub fn enumerate(path: &str) -> Result<Vec<(String, Fd)>, u32> {
        let mut out = Vec::new();
        let rd = std::fs::read_dir(path).map_err(|e| e.raw_os_error().unwrap_or(5) as u32)?;
        for e in rd.flatten() {
            let name = e.file_name().to_string_lossy().into_owned();
            if let Ok(m) = e
                .metadata()
                .or_else(|_| std::fs::symlink_metadata(e.path()))
            {
                out.push((name, meta_to_fd(&m, &e.path())));
            }
        }
        Ok(out)
    }

    pub fn attributes(path: &str) -> Option<u32> {
        std::fs::symlink_metadata(path)
            .ok()
            .map(|m| meta_to_fd(&m, std::path::Path::new(path)).attrs)
    }

    pub fn attributes_ex(path: &str) -> Option<Fd> {
        std::fs::metadata(path)
            .ok()
            .map(|m| meta_to_fd(&m, std::path::Path::new(path)))
    }

    pub fn handle_info(path: &str, _fd: &Fd, _deref: bool) -> Option<(u32, u64)> {
        use std::os::unix::fs::MetadataExt;
        std::fs::metadata(path)
            .ok()
            .map(|m| (m.nlink() as u32, m.ino()))
    }

    pub fn dir_standard_size(path: &str, _alloc: bool) -> Option<u64> {
        std::fs::metadata(path).ok().map(|m| m.len())
    }

    pub fn compressed_size(path: &str) -> Option<u64> {
        use std::os::unix::fs::MetadataExt;
        std::fs::metadata(path).ok().map(|m| m.blocks() * 512)
    }

    pub fn read_reparse_target(path: &str) -> Option<String> {
        std::fs::read_link(path)
            .ok()
            .map(|p| p.to_string_lossy().into_owned())
    }

    pub fn stdout_is_console() -> bool {
        unsafe { libc_isatty(1) != 0 }
    }

    extern "C" {
        fn isatty(fd: i32) -> i32;
    }
    unsafe fn libc_isatty(fd: i32) -> i32 {
        isatty(fd)
    }

    pub fn console_width() -> i32 {
        0
    }
    pub fn computer_name() -> String {
        "localhost".to_string()
    }
    pub fn now_filetime() -> i64 {
        to_ft(std::time::SystemTime::now())
    }
    pub fn to_local_filetime(ft: i64) -> Option<(i64, i32)> {
        Some((ft, 0))
    }
    pub fn to_systemtime(ft: i64) -> Option<(u16, u16, u16, u16, u16, u16, u16)> {
        // FILETIME ticks -> civil date (UTC).
        let secs = ft / 10_000_000 - 11_644_473_600;
        let days = secs.div_euclid(86_400);
        let rem = secs.rem_euclid(86_400);
        let (hour, minute, second) = (
            (rem / 3600) as u16,
            ((rem % 3600) / 60) as u16,
            (rem % 60) as u16,
        );
        // Howard Hinnant's civil-from-days.
        let z = days + 719_468;
        let era = z.div_euclid(146_097);
        let doe = z.rem_euclid(146_097);
        let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
        let y = yoe + era * 400;
        let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        let mp = (5 * doy + 2) / 153;
        let d = doy - (153 * mp + 2) / 5 + 1;
        let m = if mp < 10 { mp + 3 } else { mp - 9 };
        let year = if m <= 2 { y + 1 } else { y };
        Some((
            year as u16,
            m as u16,
            d as u16,
            hour,
            minute,
            second,
            (ft % 10_000_000 / 10_000) as u16,
        ))
    }
}

// ======================================================================
// Operand boundary helpers (native_path.cppm / ls.cpp probe layer).
// ======================================================================

/// Extended \\?\ spelling for API calls (fs_win::imp::api_path), with
/// "." and ".." components collapsed afterwards: the \\?\ prefix skips
/// the NT loader's normalization, so "lstest\.." would reach
/// FindFirstFileW / GetNamedSecurityInfoW literally and fail (the 1.x
/// `ls -la` shows real metadata for `.`/`..` entries).
fn api_path(ctx: &Ctx, operand: &str) -> String {
    #[cfg(windows)]
    {
        lex_collapse(&fs_win::imp::api_path(ctx, operand))
    }
    #[cfg(not(windows))]
    {
        let _ = ctx;
        operand.to_string()
    }
}

/// Lexically collapse "." / ".." inside an ALREADY-ABSOLUTE extended
/// path. Preserves the \\?\ / \\?\UNC\ / \\ prefixes and never pops a
/// drive root or UNC server+share.
#[cfg(windows)]
fn lex_collapse(path: &str) -> String {
    let p = path.replace('/', "\\");
    let (prefix, rest): (String, &str) = if let Some(r) = p.strip_prefix(r"\\?\UNC\") {
        (r"\\?\UNC\".to_string(), r)
    } else if let Some(r) = p.strip_prefix(r"\\?\") {
        (r"\\?\".to_string(), r)
    } else if let Some(r) = p.strip_prefix(r"\\") {
        (r"\\".to_string(), r)
    } else {
        (String::new(), p.as_str())
    };
    // Components that can never be popped: UNC keeps server+share, a
    // drive path keeps the drive letter.
    let min_comps = if prefix.ends_with(r"\UNC\") || prefix == r"\\" {
        2
    } else if prefix.is_empty() {
        0
    } else {
        1
    };
    let absolute = !prefix.is_empty() || rest.split('\\').next().is_some_and(|c| c.ends_with(':'));
    let min_comps = if !absolute { 0 } else { min_comps.max(1) };
    let mut stack: Vec<&str> = Vec::new();
    for part in rest.split('\\') {
        match part {
            "" | "." => {
                if stack.is_empty() && !absolute {
                    stack.push(part);
                }
            }
            ".." => {
                if stack.len() > min_comps {
                    stack.pop();
                } else if !absolute {
                    stack.push(part);
                }
            }
            _ => stack.push(part),
        }
    }
    format!("{}{}", prefix, stack.join("\\"))
}

/// ls.cpp normalize_lookup_path: an all-separator operand is the root;
/// trailing separators strip to the root-length minimum.
fn normalize_lookup_path(path: &str) -> String {
    if path.is_empty() {
        return path.to_string();
    }
    if path.chars().all(|c| c == '/' || c == '\\') {
        return "/".to_string();
    }
    // root_length: drive root name + root directory
    let bytes = path.as_bytes();
    let mut root_len = 0usize;
    if bytes.len() >= 2 && bytes[1] == b':' && bytes[0].is_ascii_alphabetic() {
        root_len = 2;
        if bytes.len() >= 3 && (bytes[2] == b'/' || bytes[2] == b'\\') {
            root_len = 3;
        }
    } else if path.starts_with("\\\\") {
        // \\server\share\ : root covers UNC share when present
        if let Some(prefix) = winux_core::path::unc_root_length(path) {
            root_len = prefix;
        }
        if root_len < path.len()
            && (path.as_bytes()[root_len] == b'/' || path.as_bytes()[root_len] == b'\\')
        {
            root_len += 1;
        }
    } else if bytes[0] == b'/' || bytes[0] == b'\\' {
        root_len = 1;
    }
    let mut end = path.len();
    while end > root_len && matches!(path.as_bytes()[end - 1], b'/' | b'\\') {
        end -= 1;
    }
    path[..end].to_string()
}

/// #1572: a bare-name operand retries as `name.exe` (no explicit
/// extension, not a trailing-separator path, not a pseudo-device).
fn resolve_bare_exe(ctx: &Ctx, path: &str) -> String {
    if path.is_empty() || path.ends_with('/') || path.ends_with('\\') {
        return path.to_string();
    }
    if winux_core::path::resolve_pseudo_device(path).is_some() {
        return path.to_string();
    }
    let slash = path.rfind(['/', '\\']);
    if let Some(dot) = path.rfind('.') {
        if slash.is_none_or(|s| dot > s) {
            return path.to_string();
        }
    }
    let exists = |p: &str| {
        plat::attributes(&api_path(ctx, p)).is_some() || find_first_probe(ctx, p).is_some()
    };
    if exists(path) {
        return path.to_string();
    }
    let cand = format!("{path}.exe");
    if exists(&cand) {
        return cand;
    }
    path.to_string()
}

fn find_first_probe(ctx: &Ctx, operand: &str) -> Option<Fd> {
    plat::find_first(&api_path(ctx, operand))
}

/// ls.cpp probe_path: attributes + FindFirstFileW + the pseudo-device
/// synthesis (NUL /dev/null ... are real character devices with no
/// directory entry).
struct Probe {
    fd: Fd,
    found: bool,
    attrs_valid: bool,
}

fn probe_path(ctx: &Ctx, path: &str) -> Probe {
    let lookup = resolve_bare_exe(ctx, &normalize_lookup_path(path));
    // POSIX pseudo-devices have no directory entry in the Win32 namespace
    // (`api_path` folds /dev/null onto NUL, whose FindFirstFileW record is
    // a regular 0-byte file). Answer them at the operand boundary and keep
    // the operand spelling, the way `stat` already does (#1779, #1643):
    // /dev/null plus the kernel-synthesized /dev/zero|random|urandom|full
    // are character devices (`crw-rw-rw-`).
    if winux_core::path::resolve_pseudo_device(&lookup).is_some()
        || winux_core::path::synthesized_device(&lookup).is_some()
    {
        return Probe {
            fd: Fd {
                attrs: ATTR_NORMAL | ATTR_DEVICE,
                ..Fd::default()
            },
            found: true,
            attrs_valid: true,
        };
    }
    let ext = api_path(ctx, &lookup);
    if let Some(fd) = plat::find_first(&ext) {
        return Probe {
            fd,
            found: true,
            attrs_valid: true,
        };
    }
    if let Some(a) = plat::attributes(&ext) {
        return Probe {
            fd: Fd {
                attrs: a,
                ..Fd::default()
            },
            found: false,
            attrs_valid: true,
        };
    }
    Probe {
        fd: Fd::default(),
        found: false,
        attrs_valid: false,
    }
}

/// ls.cpp contains_unmatchable_wildcard: a leftover * or ? in an operand
/// can never name a real file on Windows (they are illegal characters);
/// the operand is reported ENOENT rather than probed as a pattern.
fn has_unmatchable_wildcard(path: &str) -> bool {
    path.contains('*') || path.contains('?')
}

/// ls.cpp operand_requires_directory: trailing '/' or a final "." / ".."
/// component forces traversal through the named file as a directory.
fn operand_requires_directory(path: &str) -> bool {
    let mut end = path.len();
    let b = path.as_bytes();
    while end > 0 && (b[end - 1] == b'/' || b[end - 1] == b'\\') {
        end -= 1;
    }
    if end != b.len() {
        return true;
    }
    if end == 0 || b[end - 1] != b'.' {
        return false;
    }
    if end == 1 {
        return true; // "."
    }
    let prev = b[end - 2];
    if prev == b'/' || prev == b'\\' {
        return true; // "/."
    }
    if prev == b'.' {
        if end == 2 {
            return true; // ".."
        }
        let prev2 = b[end - 3];
        if prev2 == b'/' || prev2 == b'\\' {
            return true; // "/.."
        }
    }
    false
}

fn resolved_is_directory(ctx: &Ctx, path: &str) -> bool {
    match plat::attributes(&api_path(ctx, path)) {
        Some(a) => a & ATTR_DIRECTORY != 0,
        None => false,
    }
}

/// ls.cpp expand_path_operands: glob expansion (the Windows runtime owns
/// wildcard expansion), then per-operand existence and the ENOTDIR rule.
/// Returns (paths, success, operand_count) — the count tracks argv
/// operands (GNU n_files), not expanded glob hits.
fn expand_operands(ctx: &Ctx, operands: &[OsString], io: &mut Io) -> (Vec<String>, bool, usize) {
    let mut expanded: Vec<String> = Vec::new();
    let mut success = true;
    let mut logical = 0usize;
    for os in operands {
        let path = os.to_string_lossy().into_owned();
        logical += 1;
        if path.is_empty() {
            ls_error(
                io,
                &tr!(
                    ctx,
                    "command.ls.error.cannot_access",
                    "cannot access '{name}': {reason}",
                    name = path,
                    reason = "No such file or directory"
                ),
            );
            success = false;
            continue;
        }
        // 1.x shell_owns_glob_expansion: the surrounding shell expands
        // globs; operands stay literal unless the native fallback is
        // opted into via WINUX_SHELL_GLOB=native.
        let native_glob = env(ctx, "WINUX_SHELL_GLOB")
            .map(|v| v == "native")
            .unwrap_or(false);
        if native_glob && crate::glob::contains_wildcard(&path) {
            if let Some(files) = crate::glob::glob_expand_in(ctx, &path) {
                if !files.is_empty() {
                    expanded.extend(files);
                    continue;
                }
            }
        }
        if crate::glob::contains_wildcard(&path) && has_unmatchable_wildcard(&path) {
            // '*'/'?' can never be a literal Windows name; a literal probe
            // would let FindFirstFileW wildcard semantics match unrelated
            // files instead of reporting ENOENT.
            ls_error(
                io,
                &tr!(
                    ctx,
                    "command.ls.error.cannot_access",
                    "cannot access '{name}': {reason}",
                    name = path,
                    reason = "No such file or directory"
                ),
            );
            success = false;
            continue;
        }
        // bracket-class-only operand: '[' is a legal Windows filename
        // char — fall through and probe it literally.
        let probe = probe_path(ctx, &path);
        if probe.attrs_valid || probe.found {
            let resolved = resolve_bare_exe(ctx, &normalize_lookup_path(&path));
            if operand_requires_directory(&path) && !resolved_is_directory(ctx, &resolved) {
                ls_error(
                    io,
                    &tr!(
                        ctx,
                        "command.ls.error.cannot_access",
                        "cannot access '{name}': {reason}",
                        name = path,
                        reason = "Not a directory"
                    ),
                );
                success = false;
                continue;
            }
            expanded.push(path);
        } else {
            ls_error(
                io,
                &tr!(
                    ctx,
                    "command.ls.error.cannot_access",
                    "cannot access '{name}': {reason}",
                    name = path,
                    reason = "No such file or directory"
                ),
            );
            success = false;
        }
    }
    (expanded, success, logical)
}

fn ls_error(io: &mut Io, msg: &str) {
    let _ = writeln!(io.stderr, "ls: {msg}");
}

// ======================================================================
// Quoting (gnulib quotearg.c:248 quotearg_buffer_restyled; ls.cpp
// apply_quoting_mode, extended with the UTF-8 printability of the
// C.UTF-8 oracle — iswprint holds for é/CJK/emoji, so those bytes stay
// literal where the C locale would emit \ooo).
// ======================================================================

/// Is `c` printable under the C.UTF-8 oracle? ASCII graphics plus every
/// assigned non-format character; C0/C1 controls, format chars (Cf),
/// line/paragraph separators, and noncharacters are not.
fn is_print(c: char) -> bool {
    let u = c as u32;
    if (0x20..0x7f).contains(&u) {
        return true;
    }
    if u < 0x20 || (0x7f..0xa0).contains(&u) {
        return false;
    }
    // Cf format chars, Zl/Zp, noncharacters, unassigned private areas stay
    // escaped; NBSP and everything assigned prints.
    !matches!(u,
        0x200b..=0x200f | 0x202a..=0x202e | 0x2060..=0x2064 | 0xfeff
        | 0x2028 | 0x2029
        | 0xfdd0..=0xfdef)
        && !(u & 0xfffe == 0xfffe)
        && !(0xd800..=0xdfff).contains(&u)
}

fn append_octal(out: &mut String, byte: u8) {
    const D: &[u8; 8] = b"01234567";
    out.push('\\');
    out.push(D[(byte >> 6) as usize & 7] as char);
    out.push(D[(byte >> 3) as usize & 7] as char);
    out.push(D[(byte & 7) as usize] as char);
}

fn push_utf8_octal(out: &mut String, c: char) {
    let mut b = [0u8; 4];
    for byte in c.encode_utf8(&mut b).as_bytes() {
        append_octal(out, *byte);
    }
}

fn c_escape_into(out: &mut String, c: char) {
    match c {
        '\u{7}' => out.push_str("\\a"),
        '\u{8}' => out.push_str("\\b"),
        '\t' => out.push_str("\\t"),
        '\n' => out.push_str("\\n"),
        '\u{b}' => out.push_str("\\v"),
        '\u{c}' => out.push_str("\\f"),
        '\r' => out.push_str("\\r"),
        '\\' => out.push_str("\\\\"),
        '"' => out.push_str("\\\""),
        _ => {
            if is_print(c) {
                out.push(c);
            } else if (c as u32) < 0x80 {
                append_octal(out, c as u8);
            } else {
                push_utf8_octal(out, c);
            }
        }
    }
}

/// escape_display_name: the inner text of c_quoting_style (escapes `"` and
/// `\`, named controls, octal for non-printables).
fn escape_display_name(name: &str) -> String {
    let mut out = String::with_capacity(name.len() * 2);
    for c in name.chars() {
        c_escape_into(&mut out, c);
    }
    out
}

/// escape_quoting_style (-b): same escapes except `"` stays literal and
/// the space (plus indicator chars registered by ls.c:2364-2369 when
/// indicator_style >= file_type) is backslash-quoted.
fn escape_style_display_name(name: &str, extra_chars: &str) -> String {
    let mut out = String::with_capacity(name.len() * 2);
    for c in name.chars() {
        if c == ' ' || extra_chars.contains(c) {
            out.push('\\');
            out.push(c);
            continue;
        }
        match c {
            '"' => out.push('"'),
            _ => c_escape_into(&mut out, c),
        }
    }
    out
}

/// locale/clocale quoting under the UTF-8 oracle: ‘...’ marks, named
/// escapes and octal for non-printables, `'` literal (probed 9.4:
/// ‘it's.txt’ keeps the apostrophe).
fn locale_quote_display_name(name: &str) -> String {
    let mut out = String::from("\u{2018}");
    for c in name.chars() {
        match c {
            '"' => out.push('"'),
            '\'' => out.push('\''),
            _ => c_escape_into(&mut out, c),
        }
    }
    out.push('\u{2019}');
    out
}

fn shell_special_at(c: char, index: usize, len: usize) -> bool {
    if c == '#' || c == '~' {
        return index == 0;
    }
    if c == '{' || c == '}' {
        return len == 1;
    }
    matches!(
        c,
        ' ' | '\t'
            | '\n'
            | '\u{b}'
            | '\u{c}'
            | '\r'
            | '\''
            | '"'
            | '$'
            | '&'
            | '('
            | ')'
            | '*'
            | ';'
            | '<'
            | '='
            | '>'
            | '['
            | '^'
            | '`'
            | '|'
            | '!'
            | '\\'
    ) || !is_print(c)
}

/// quotearg's shell/C compatibility test: the apostrophe form is only
/// eligible for the shorter "C" re-render when no other character would
/// need escaping inside "...".
fn shell_and_c_compatible(c: char) -> bool {
    !matches!(
        c,
        '!' | '"'
            | '$'
            | '&'
            | '('
            | ')'
            | '*'
            | ';'
            | '<'
            | '='
            | '>'
            | '['
            | '^'
            | '`'
            | '|'
            | '\\'
    ) && is_print(c)
}

/// shell_quoting_style / shell-always: single quotes with the '\'' splice
/// for apostrophes; a name containing ' where every char is C+shell safe
/// re-renders as the C string "..." (quotearg.c encountered_single_quote).
fn shell_quote_display_name(name: &str, always: bool) -> String {
    let mut quote = always;
    if !quote {
        for (i, c) in name.chars().enumerate() {
            if shell_special_at(c, i, name.chars().count()) {
                quote = true;
                break;
            }
        }
    }
    if !quote {
        return name.to_string();
    }
    if name.contains('\'') && name.chars().all(shell_and_c_compatible) {
        return format!("\"{name}\"");
    }
    let mut out = String::from("'");
    for c in name.chars() {
        if c == '\'' {
            out.push_str("'\\''");
        } else {
            out.push(c);
        }
    }
    out.push('\'');
    out
}

/// shell_escape_quoting_style: like shell quoting but non-printable
/// characters leave the quotes and render as $'\ooo' splices (probed 9.4:
/// 'tab'$'\t''name'). `always` is GNU's shell_escape_always_quoting_style —
/// the quoteaf() shape mv/cp diagnostics use for operands (#374).
pub(crate) fn shell_escape_quote_display_name(name: &str, always: bool) -> String {
    let mut quote = always;
    if !quote {
        for (i, c) in name.chars().enumerate() {
            if shell_special_at(c, i, name.chars().count()) {
                quote = true;
                break;
            }
        }
    }
    if !quote {
        return name.to_string();
    }
    if name.contains('\'') && name.chars().all(shell_and_c_compatible) {
        return format!("\"{name}\"");
    }
    let mut out = String::from("'");
    for c in name.chars() {
        if c == '\'' {
            out.push_str("'\\''");
        } else if is_print(c) || c == '\u{7f}' {
            out.push(c);
        } else {
            // close quote, emit $'<escape>', reopen
            let named = match c {
                '\u{7}' => Some("\\a"),
                '\u{8}' => Some("\\b"),
                '\t' => Some("\\t"),
                '\n' => Some("\\n"),
                '\u{b}' => Some("\\v"),
                '\u{c}' => Some("\\f"),
                '\r' => Some("\\r"),
                _ => None,
            };
            out.push('\'');
            out.push('$');
            out.push('\'');
            if let Some(e) = named {
                out.push_str(e);
            } else if (c as u32) < 0x80 {
                append_octal(&mut out, c as u8);
            } else {
                push_utf8_octal(&mut out, c);
            }
            out.push('\'');
            out.push('\'');
        }
    }
    out.push('\'');
    out
}

fn apply_quoting(name: &str, q: Quoting, indicator_extra: &str) -> String {
    match q {
        Quoting::Literal => name.to_string(),
        Quoting::Escape => escape_style_display_name(name, indicator_extra),
        Quoting::C => format!("\"{}\"", escape_display_name(name)),
        Quoting::CMaybe => {
            let esc = escape_display_name(name);
            if esc != name {
                format!("\"{esc}\"")
            } else {
                esc
            }
        }
        Quoting::Shell => shell_quote_display_name(name, false),
        Quoting::ShellAlways => shell_quote_display_name(name, true),
        Quoting::ShellEscape => shell_escape_quote_display_name(name, false),
        Quoting::ShellEscapeAlways => shell_escape_quote_display_name(name, true),
        Quoting::Locale | Quoting::Clocale => locale_quote_display_name(name),
    }
}

/// Display width in terminal columns (wcwidth approximation: combining
/// marks 0, wide CJK/emoji 2, everything else 1).
fn display_width(s: &str) -> usize {
    s.chars()
        .map(|c| {
            let u = c as u32;
            if u < 0x20 || (0x7f..0xa0).contains(&u) {
                0
            } else if (0x300..=0x36f).contains(&u) {
                0
            } else if u >= 0x1100
                && (u <= 0x115f
                    || (0x2e80..=0xa4cf).contains(&u)
                    || (0xac00..=0xd7a3).contains(&u)
                    || (0xf900..=0xfaff).contains(&u)
                    || (0xfe30..=0xfe6f).contains(&u)
                    || (0xff00..=0xff60).contains(&u)
                    || (0xffe0..=0xffe6).contains(&u)
                    || (0x1f300..=0x1f64f).contains(&u)
                    || (0x20000..=0x2fffd).contains(&u)
                    || (0x30000..=0x3fffd).contains(&u))
            {
                2
            } else {
                1
            }
        })
        .sum()
}

// ======================================================================
// filevercmp (gnulib/lib/filevercmp.c:134; ls.cpp compare_version_strings
// port — same algorithm as sort.rs's -V comparator).
// ======================================================================

fn version_order(c: Option<char>) -> i64 {
    match c {
        None => -1,
        Some('~') => -2,
        Some(c) if c.is_ascii_digit() => 0,
        Some(c) if c.is_ascii_alphabetic() => c as i64,
        Some(c) => c as i64 + 256,
    }
}

fn verrevcmp(a: &[char], b: &[char]) -> i64 {
    let mut i = 0usize;
    let mut j = 0usize;
    while i < a.len() || j < b.len() {
        let mut first_diff = 0i64;
        while (i < a.len() && !a[i].is_ascii_digit()) || (j < b.len() && !b[j].is_ascii_digit()) {
            let ac = version_order(a.get(i).copied());
            let bc = version_order(b.get(j).copied());
            if ac != bc {
                return if ac < bc { -1 } else { 1 };
            }
            i += 1;
            j += 1;
        }
        while i < a.len() && a[i] == '0' {
            i += 1;
        }
        while j < b.len() && b[j] == '0' {
            j += 1;
        }
        while i < a.len() && j < b.len() && a[i].is_ascii_digit() && b[j].is_ascii_digit() {
            if first_diff == 0 {
                first_diff = a[i] as i64 - b[j] as i64;
            }
            i += 1;
            j += 1;
        }
        if i < a.len() && a[i].is_ascii_digit() {
            return 1;
        }
        if j < b.len() && b[j].is_ascii_digit() {
            return -1;
        }
        if first_diff != 0 {
            return if first_diff < 0 { -1 } else { 1 };
        }
    }
    0
}

fn file_prefixlen(s: &[char]) -> usize {
    let mut cut = s.len();
    while cut > 1 {
        let mut q = cut;
        while q > 0 && (s[q - 1].is_ascii_alphanumeric() || s[q - 1] == '~') {
            q -= 1;
        }
        if q == 0 || q == cut || s[q - 1] != '.' {
            break;
        }
        if !s[q].is_ascii_alphabetic() && s[q] != '~' {
            break;
        }
        if q - 1 == 0 {
            break;
        }
        cut = q - 1;
    }
    cut
}

fn compare_version(a: &str, b: &str) -> Ordering {
    if a.is_empty() || b.is_empty() {
        return if a.is_empty() && b.is_empty() {
            Ordering::Equal
        } else if a.is_empty() {
            Ordering::Less
        } else {
            Ordering::Greater
        };
    }
    if a == "." {
        return if b == "." {
            Ordering::Equal
        } else {
            Ordering::Less
        };
    }
    if b == "." {
        return Ordering::Greater;
    }
    if a == ".." {
        return if b == ".." {
            Ordering::Equal
        } else {
            Ordering::Less
        };
    }
    if b == ".." {
        return Ordering::Greater;
    }
    let a_dot = a.starts_with('.');
    let b_dot = b.starts_with('.');
    if a_dot != b_dot {
        return if a_dot {
            Ordering::Less
        } else {
            Ordering::Greater
        };
    }
    let av: Vec<char> = a.chars().collect();
    let bv: Vec<char> = b.chars().collect();
    let ap = file_prefixlen(&av);
    let bp = file_prefixlen(&bv);
    let cmp = verrevcmp(&av[..ap], &bv[..bp]);
    if cmp != 0 {
        return cmp.cmp(&0);
    }
    if ap < av.len() || bp < bv.len() {
        return verrevcmp(&av, &bv).cmp(&0);
    }
    Ordering::Equal
}

// ======================================================================
// Sizes (gnulib/lib/human.c:154 human_readable with human_ceiling;
// ls.cpp format_human_size / format_scaled_size / SizeConfig).
// ======================================================================

/// Ceiling-tenth autoscale: 1024 base ("K M G T P E") or 1000 base
/// ("k M G T P E", #1644).
fn human_size(bytes: u64, decimal: bool) -> String {
    let units: &[char] = if decimal {
        &['B', 'k', 'M', 'G', 'T', 'P', 'E']
    } else {
        &['B', 'K', 'M', 'G', 'T', 'P', 'E']
    };
    let radix: u64 = if decimal { 1000 } else { 1024 };
    if bytes < radix {
        return bytes.to_string();
    }
    let mut idx = 0usize;
    let mut unit = radix;
    while idx < 6 && bytes / unit >= radix {
        unit = unit.saturating_mul(radix);
        idx += 1;
    }
    idx += 1;
    let whole = bytes / unit;
    let rest = bytes % unit;
    let mut tenths = whole * 10 + (rest * 10 + unit - 1) / unit;
    while tenths >= radix * 10 && idx < 6 {
        tenths = (tenths + radix - 1) / radix;
        idx += 1;
    }
    if tenths < 100 {
        format!("{}.{}{}", tenths / 10, tenths % 10, units[idx])
    } else {
        format!("{}{}", (tenths + 9) / 10, units[idx])
    }
}

fn format_scaled_size(size: u64, mode: SizeMode, block: u64) -> String {
    match mode {
        SizeMode::Bytes => size.to_string(),
        SizeMode::Blocks => {
            if block == 0 {
                size.to_string()
            } else {
                ((size + block - 1) / block).to_string()
            }
        }
        SizeMode::Human => human_size(size, false),
        SizeMode::Si => human_size(size, true),
    }
}

fn ceil_div(v: u64, d: u64) -> u64 {
    if v == 0 || d == 0 {
        0
    } else {
        1 + (v - 1) / d
    }
}

/// Allocated size: directories take FILE_STANDARD_INFO.AllocationSize,
/// files GetCompressedFileSizeW (falling back to the logical size).
fn allocated_size_bytes(ctx: &Ctx, path: &str, fd: &Fd) -> u64 {
    if fd.is_plain_dir() {
        if let Some(v) = plat::dir_standard_size(&api_path(ctx, &abs_probe_path(ctx, path)), true) {
            return v;
        }
    }
    plat::compressed_size(&api_path(ctx, path)).unwrap_or(fd.size)
}

/// ls.cpp get_allocated_usage_bytes: fifo -> 0; allocated rounded UP to
/// the 1 KiB boundary first (matching the C++ block accounting).
fn allocated_usage_bytes(ctx: &Ctx, path: &str, fd: &Fd) -> u64 {
    if is_winux_fifo(ctx, path) {
        return 0;
    }
    let a = allocated_size_bytes(ctx, path, fd);
    if a == 0 {
        return 0;
    }
    ceil_div(a, 1024).saturating_mul(1024)
}

fn allocated_block_count(ctx: &Ctx, path: &str, fd: &Fd, cfg: &SizeCfg) -> u64 {
    ceil_div(allocated_usage_bytes(ctx, path, fd), cfg.block_size.max(1))
}

fn allocated_usage_value(ctx: &Ctx, path: &str, fd: &Fd, cfg: &SizeCfg) -> u64 {
    let usage = allocated_usage_bytes(ctx, path, fd);
    match cfg.block_mode {
        SizeMode::Bytes => usage,
        SizeMode::Blocks => ceil_div(usage, cfg.block_size.max(1)),
        SizeMode::Human | SizeMode::Si => usage,
    }
}

fn allocated_blocks_string(ctx: &Ctx, path: &str, fd: &Fd, cfg: &SizeCfg) -> String {
    let v = allocated_usage_value(ctx, path, fd, cfg);
    if cfg.block_mode == SizeMode::Blocks {
        v.to_string()
    } else {
        format_scaled_size(v, cfg.block_mode, cfg.block_size.max(1))
    }
}

/// normalize_metadata_probe_path (ls.cpp): resolve the operand to a
/// native absolute spelling for metadata probes that bypass the dialect
/// boundary (dir size query).
fn abs_probe_path(ctx: &Ctx, path: &str) -> String {
    let p = std::path::Path::new(path);
    if p.is_absolute() {
        path.to_string()
    } else {
        ctx.cwd.join(path).to_string_lossy().replace('/', "\\")
    }
}

// ======================================================================
// Time formatting (ls.cpp get_time_string / format_strftime_like;
// ls.c:4440-4470 recent-vs-old split, 31556952/2 second window).
// ======================================================================

const MONTHS: [&str; 12] = [
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
];

/// Six-month recency window (ls.c:4458-4466).
fn is_recent(ft: i64) -> bool {
    let now = plat::now_filetime();
    const WINDOW: i64 = 15_778_476 * 10_000_000;
    ft < now && (now - ft) < WINDOW
}

/// Broken-down local time for a FILETIME: (year, month, day, hour,
/// minute, second, sub-second-ticks).
fn local_parts(ft: i64) -> Option<(u16, u16, u16, u16, u16, u16, i64, i32)> {
    let (local_ft, offset_min) = plat::to_local_filetime(ft)?;
    let (y, mo, d, h, mi, s, _ms) = plat::to_systemtime(local_ft)?;
    let sub = (ft % 10_000_000) * 100; // nanoseconds
    Some((y, mo, d, h, mi, s, sub, offset_min))
}

/// Minimal strftime for --time-style=+FORMAT (GNU date conversion set:
/// padding flags -, _, 0 and the ls-relevant directives).
fn strftime_lite(format: &str, ft: i64) -> String {
    let Some((y, mo, d, h, mi, s, ns, off)) = local_parts(ft) else {
        return String::new();
    };
    let mut out = String::new();
    let chars: Vec<char> = format.chars().collect();
    let mut i = 0usize;
    let weekday = weekday_of(y, mo, d);
    while i < chars.len() {
        if chars[i] != '%' {
            out.push(chars[i]);
            i += 1;
            continue;
        }
        i += 1;
        let mut pad: Option<char> = None;
        let mut upper = false;
        while i < chars.len() && matches!(chars[i], '-' | '_' | '0' | '^' | '#' | ':') {
            match chars[i] {
                '-' | '_' | '0' => pad = Some(chars[i]),
                '^' => upper = true,
                _ => {}
            }
            i += 1;
        }
        if i >= chars.len() {
            out.push('%');
            break;
        }
        let c = chars[i];
        i += 1;
        let num = |v: u32, width: usize| -> String {
            let s = v.to_string();
            let fill = match pad {
                Some('-') => return s,
                Some('_') => ' ',
                _ => '0',
            };
            if s.len() >= width {
                s
            } else {
                format!("{}{}", fill.to_string().repeat(width - s.len()), s)
            }
        };
        let text = match c {
            'Y' => num(y as u32, 4),
            'y' => num(y as u32 % 100, 2),
            'm' => num(mo as u32, 2),
            'd' => num(d as u32, 2),
            'e' => {
                let s = d.to_string();
                if s.len() < 2 {
                    format!(" {s}")
                } else {
                    s
                }
            }
            'H' => num(h as u32, 2),
            'I' => num(((h + 11) % 12 + 1) as u32, 2),
            'M' => num(mi as u32, 2),
            'S' => num(s as u32, 2),
            'N' => format!("{ns:09}"),
            'z' => {
                let sign = if off < 0 { '-' } else { '+' };
                let m = off.abs();
                format!("{sign}{:02}{:02}", m / 60, m % 60)
            }
            'Z' => tz_name(off),
            'b' | 'h' => MONTHS[(mo - 1) as usize].to_string(),
            'B' => MONTH_FULL[(mo - 1) as usize].to_string(),
            'a' => WEEK_ABBR[weekday as usize].to_string(),
            'A' => WEEK_FULL[weekday as usize].to_string(),
            'p' => if h < 12 { "AM" } else { "PM" }.to_string(),
            'P' => if h < 12 { "am" } else { "pm" }.to_string(),
            'T' => format!("{:02}:{:02}:{:02}", h, mi, s),
            'R' => format!("{:02}:{:02}", h, mi),
            'D' => format!("{:02}/{:02}/{:02}", mo, d, y % 100),
            'F' => format!("{:04}-{:02}-{:02}", y, mo, d),
            's' => (ft / 10_000_000 - 11_644_473_600).to_string(),
            'j' => num(day_of_year(y, mo, d), 3),
            'u' => num(if weekday == 0 { 7 } else { weekday } as u32, 1),
            'w' => num(weekday as u32, 1),
            'c' => format!(
                "{} {} {:2} {:02}:{:02}:{:02} {}",
                WEEK_ABBR[weekday as usize],
                MONTHS[(mo - 1) as usize],
                d,
                h,
                mi,
                s,
                y
            ),
            'x' => format!("{:02}/{:02}/{:04}", mo, d, y),
            'X' => format!("{:02}:{:02}:{:02}", h, mi, s),
            'n' => "\n".to_string(),
            't' => "\t".to_string(),
            '%' => "%".to_string(),
            other => {
                out.push('%');
                out.push(other);
                continue;
            }
        };
        out.push_str(&if upper { text.to_uppercase() } else { text });
    }
    out
}

const MONTH_FULL: [&str; 12] = [
    "January",
    "February",
    "March",
    "April",
    "May",
    "June",
    "July",
    "August",
    "September",
    "October",
    "November",
    "December",
];
const WEEK_ABBR: [&str; 7] = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
const WEEK_FULL: [&str; 7] = [
    "Sunday",
    "Monday",
    "Tuesday",
    "Wednesday",
    "Thursday",
    "Friday",
    "Saturday",
];

fn weekday_of(y: u16, m: u16, d: u16) -> u32 {
    // Zeller: 0 = Sunday
    let (y, m) = if m <= 2 {
        (y as i64 - 1, m as i64 + 12)
    } else {
        (y as i64, m as i64)
    };
    let d = d as i64;
    let h = (d + (13 * (m + 1)) / 5 + y + y / 4 - y / 100 + y / 400).rem_euclid(7);
    ((h + 6) % 7) as u32
}

fn day_of_year(y: u16, m: u16, d: u16) -> u32 {
    let leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    const DAYS: [u32; 12] = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
    let mut total = d as u32;
    for i in 0..(m as usize - 1) {
        total += DAYS[i];
    }
    if leap && m > 2 {
        total += 1;
    }
    total
}

fn tz_name(off: i32) -> String {
    // Windows has no Olson name in the FILETIME path; emit the numeric
    // zone the way the 1.x applets surface it.
    if off == 0 {
        "UTC".to_string()
    } else {
        let sign = if off < 0 { '-' } else { '+' };
        let m = off.abs();
        format!("UTC{sign}{:02}{:02}", m / 60, m % 60)
    }
}

/// The ls.c time renderer (ls.cpp get_time_string port): style-specific
/// formats; the Default style splits on the six-month recency window like
/// locale.
fn get_time_string(fd: &Fd, time_type: TimeType, style: TimeStyle, custom: &str) -> String {
    let ft = match time_type {
        TimeType::Mtime => fd.mtime,
        TimeType::Atime => fd.atime,
        TimeType::Ctime | TimeType::Btime => fd.ctime,
    };
    let Some((y, mo, d, h, mi, s, ns, off)) = local_parts(ft) else {
        return "?".to_string();
    };
    match style {
        TimeStyle::FullIso => {
            let sign = if off < 0 { '-' } else { '+' };
            let m = off.abs();
            format!(
                "{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:09} {}{:02}{:02}",
                y,
                mo,
                d,
                h,
                mi,
                s,
                ns,
                sign,
                m / 60,
                m % 60
            )
        }
        TimeStyle::LongIso => format!("{:04}-{:02}-{:02} {:02}:{:02}", y, mo, d, h, mi),
        TimeStyle::Iso => {
            if is_recent(ft) {
                format!("{:02}-{:02} {:02}:{:02}", mo, d, h, mi)
            } else {
                format!("{:04}-{:02}-{:02} ", y, mo, d)
            }
        }
        TimeStyle::Custom => {
            // A '\n' in +FORMAT selects the recent/old pair (ls.c:2404-2416).
            if let Some(pos) = custom.find('\n') {
                let (old, recent) = custom.split_at(pos);
                if is_recent(ft) {
                    strftime_lite(&recent[1..], ft)
                } else {
                    strftime_lite(old, ft)
                }
            } else {
                strftime_lite(custom, ft)
            }
        }
        TimeStyle::Locale | TimeStyle::Default => {
            if is_recent(ft) {
                format!("{} {:2} {:02}:{:02}", MONTHS[(mo - 1) as usize], d, h, mi)
            } else {
                format!("{} {:2}  {:04}", MONTHS[(mo - 1) as usize], d, y)
            }
        }
    }
}

// ======================================================================
// Colors (ls.cpp get_color_for_entry + LS_COLORS table; GNU dircolors
// defaults for the unset case).
// ======================================================================

const COLOR_RESET: &str = "\u{1b}[0m";
const COLOR_DIR: &str = "\u{1b}[01;34m";
const COLOR_EXEC: &str = "\u{1b}[01;32m";
const COLOR_LINK: &str = "\u{1b}[01;36m";
const COLOR_ARCHIVE: &str = "\u{1b}[01;31m";
const COLOR_SCRIPT: &str = "\u{1b}[01;33m";
const COLOR_SOURCE: &str = "\u{1b}[01;36m";
const COLOR_MEDIA: &str = "\u{1b}[01;35m";

const COMPRESSED_EXTS: &[&str] = &[
    "zip", "rar", "7z", "tar", "gz", "bz2", "xz", "iso", "cab", "arc",
];
const SCRIPT_EXTS: &[&str] = &[
    "sh", "bat", "cmd", "py", "pl", "lua", "js", "php", "rb", "ps1",
];
const SOURCE_EXTS: &[&str] = &["cpp", "c", "h", "hpp", "rs", "ts", "java", "go"];
const MEDIA_EXTS: &[&str] = &[
    "jpg", "jpeg", "png", "gif", "bmp", "mp4", "mp3", "wav", "avi", "mkv", "webp",
];

fn ext_lower(name: &str) -> String {
    match name.rfind('.') {
        Some(d) if d + 1 < name.len() => name[d + 1..].to_lowercase(),
        _ => String::new(),
    }
}

/// Extension after the LAST dot, verbatim (cmp_extension compares raw).
fn ext_raw(name: &str) -> &str {
    match name.rfind('.') {
        Some(d) if d + 1 < name.len() => &name[d + 1..],
        _ => "",
    }
}

fn is_executable_name(name: &str) -> bool {
    matches!(
        ext_lower(name).as_str(),
        "exe" | "com" | "bat" | "cmd" | "ps1"
    )
}

struct LsColors {
    active: bool,
    keys: std::collections::HashMap<String, String>,
    exts: Vec<(String, String)>,
}

fn parse_ls_colors(ctx: &Ctx) -> LsColors {
    let mut t = LsColors {
        active: false,
        keys: std::collections::HashMap::new(),
        exts: Vec::new(),
    };
    let Some(v) = env(ctx, "LS_COLORS") else {
        return t;
    };
    if v.is_empty() {
        return t;
    }
    t.active = true;
    for item in v.split(':') {
        if let Some((k, val)) = item.split_once('=') {
            if k.is_empty() {
                continue;
            }
            let seq = format!("\u{1b}[{val}m");
            if k.starts_with('*') {
                t.exts.push((k.to_string(), seq));
            } else {
                t.keys.insert(k.to_string(), seq);
            }
        }
    }
    t
}

fn colors_lookup(colors: &LsColors, name: &str, fd: &Fd) -> String {
    let keyed = |k: &str| colors.keys.get(k).cloned().unwrap_or_default();
    if fd.is_reparse() {
        return keyed("ln");
    }
    if fd.is_plain_dir() {
        return keyed("di");
    }
    for (pat, seq) in &colors.exts {
        if wildcard_match(pat, name) {
            return seq.clone();
        }
    }
    if is_executable_name(name) {
        let s = keyed("ex");
        if !s.is_empty() {
            return s;
        }
    }
    keyed("fi")
}

fn entry_color(colors: &LsColors, name: &str, fd: &Fd) -> String {
    if colors.active {
        return colors_lookup(colors, name, fd);
    }
    if fd.is_reparse() {
        return COLOR_LINK.to_string();
    }
    if fd.is_plain_dir() {
        return COLOR_DIR.to_string();
    }
    let e = ext_lower(name);
    if COMPRESSED_EXTS.contains(&e.as_str()) {
        return COLOR_ARCHIVE.to_string();
    }
    if SCRIPT_EXTS.contains(&e.as_str()) {
        return COLOR_SCRIPT.to_string();
    }
    if is_executable_name(name) {
        return COLOR_EXEC.to_string();
    }
    if SOURCE_EXTS.contains(&e.as_str()) {
        return COLOR_SOURCE.to_string();
    }
    if MEDIA_EXTS.contains(&e.as_str()) {
        return COLOR_MEDIA.to_string();
    }
    String::new()
}

/// One leading reset before the first colored name of a run
/// (ls.cpp color_prefix_sequence / ls.c used_color).
struct ColorState {
    used: bool,
}

impl ColorState {
    fn prefix(&mut self) -> &'static str {
        if self.used {
            ""
        } else {
            self.used = true;
            COLOR_RESET
        }
    }
}

// ======================================================================
// Indicators (ls.c:4909 get_type_indicator; ls.cpp get_indicator_suffix).
// ======================================================================

fn indicator_suffix(style: Indicator, name: &str, fd: &Fd, is_fifo: bool) -> &'static str {
    let file_type = || {
        if fd.is_reparse() {
            "@"
        } else if fd.is_plain_dir() {
            "/"
        } else if is_fifo {
            "|"
        } else if fd.is_chardev() {
            ""
        } else {
            ""
        }
    };
    match style {
        Indicator::None => "",
        Indicator::Slash => {
            if fd.is_plain_dir() {
                "/"
            } else {
                ""
            }
        }
        Indicator::FileType => file_type(),
        Indicator::Classify => {
            let s = file_type();
            if !s.is_empty() {
                s
            } else if is_executable_name(name) {
                "*"
            } else {
                ""
            }
        }
    }
}

// ======================================================================
// Symlink display target (ls.cpp read_symlink_display_target): reparse
// payload, relative targets resolved against the link's directory,
// absolute in-tree targets shortened to the relative spelling.
// ======================================================================

struct LinkTarget {
    display: String,
    resolved: String,
}

fn read_link_display(ctx: &Ctx, full_path: &str, fd: &Fd) -> Option<LinkTarget> {
    let ext = api_path(ctx, full_path);
    let target = plat::read_reparse_target(&ext).or_else(|| {
        std::fs::read_link(&ext)
            .ok()
            .map(|p| p.to_string_lossy().into_owned())
    })?;

    // resolve to an absolute spelling for the target-type probe
    let resolved = {
        let tp = std::path::Path::new(&target);
        if tp.is_absolute() {
            normalize_slashes(&target)
        } else {
            let parent = std::path::Path::new(full_path)
                .parent()
                .map(|p| p.to_string_lossy().into_owned())
                .unwrap_or_default();
            normalize_slashes(&join_path(&parent, &target))
        }
    };

    let mut display = target.clone();
    for p in [r"\??\", r"\\?\"] {
        if let Some(rest) = display.strip_prefix(p) {
            display = rest.to_string();
        }
    }

    // Absolute symbolic targets inside the link's own directory shorten
    // to the relative spelling (ls.cpp normalize_windows_path compare).
    if std::path::Path::new(&target).is_absolute() && fd.is_symbolic_link() {
        let norm = |v: &str| v.replace('/', "\\").to_lowercase();
        let parent = std::path::Path::new(full_path)
            .parent()
            .map(|p| {
                let s = p.to_string_lossy().into_owned();
                if s.is_empty() {
                    ".".to_string()
                } else {
                    s
                }
            })
            .unwrap_or_else(|| ".".to_string());
        let parent_abs = abs_probe_path(ctx, &parent);
        let np = norm(&parent_abs);
        let nr = norm(&resolved);
        if !np.is_empty()
            && nr.len() > np.len()
            && nr.starts_with(&np)
            && nr.as_bytes().get(np.len()) == Some(&b'\\')
        {
            display = resolved[parent_abs.len() + 1..].to_string();
        }
    }
    Some(LinkTarget { display, resolved })
}

fn normalize_slashes(p: &str) -> String {
    let mut out = String::with_capacity(p.len());
    let parts: Vec<&str> = p.split(['/', '\\']).collect();
    // lexically normalize "." / ".." components, keep leading root
    let mut stack: Vec<&str> = Vec::new();
    for part in &parts {
        match *part {
            "." => {}
            ".." => {
                if matches!(stack.last(), Some(&x) if x != ".." && !x.is_empty() && x != ":") {
                    stack.pop();
                } else if !stack.is_empty() || !std::path::Path::new(p).is_absolute() {
                    stack.push(part);
                }
            }
            _ => stack.push(part),
        }
    }
    let joined = stack.join("\\");
    let _ = &mut out;
    joined
}

fn join_path(dir: &str, name: &str) -> String {
    if dir.is_empty() {
        return name.to_string();
    }
    if dir.ends_with('/') || dir.ends_with('\\') {
        format!("{dir}{name}")
    } else {
        format!("{dir}\\{name}")
    }
}

fn is_winux_fifo(ctx: &Ctx, path: &str) -> bool {
    #[cfg(windows)]
    {
        fs_win::imp::is_winux_fifo(&api_path(ctx, path))
    }
    #[cfg(not(windows))]
    {
        let _ = ctx;
        let _ = path;
        false
    }
}

// ======================================================================
// Wildcard match (the ls.cpp wildcard_match shape: '*' '?' '[..]' with
// case-sensitive matching for -I/--hide/LS_COLORS patterns).
// ======================================================================

/// fnmatch with FNM_PERIOD semantics (ignore_patterns / hide_patterns /
/// LS_COLORS): a leading '.' in the name matches only a literal '.' in
/// the pattern, never a wildcard.
fn wildcard_match(pattern: &str, name: &str) -> bool {
    if name.starts_with('.') && !pattern.starts_with('.') {
        return false;
    }
    fn inner(p: &[u8], n: &[u8]) -> bool {
        if p.is_empty() {
            return n.is_empty();
        }
        match p[0] {
            b'*' => {
                let mut i = 0;
                loop {
                    if inner(&p[1..], &n[i..]) {
                        return true;
                    }
                    if i >= n.len() {
                        return false;
                    }
                    i += 1;
                }
            }
            b'?' => !n.is_empty() && inner(&p[1..], &n[1..]),
            b'[' => {
                if n.is_empty() {
                    return false;
                }
                let mut j = 1;
                let neg = p.get(1) == Some(&b'!') || p.get(1) == Some(&b'^');
                if neg {
                    j += 1;
                }
                let mut hit = false;
                let mut first = true;
                while j < p.len() && (p[j] != b']' || first) {
                    first = false;
                    if j + 2 < p.len() && p[j + 1] == b'-' && p[j + 2] != b']' {
                        if p[j] <= n[0] && n[0] <= p[j + 2] {
                            hit = true;
                        }
                        j += 3;
                    } else {
                        if p[j] == n[0] {
                            hit = true;
                        }
                        j += 1;
                    }
                }
                if j >= p.len() {
                    return false; // unterminated class — literal
                }
                (hit != neg) && inner(&p[j + 1..], &n[1..])
            }
            c => !n.is_empty() && n[0] == c && inner(&p[1..], &n[1..]),
        }
    }
    inner(pattern.as_bytes(), name.as_bytes())
}

// ======================================================================
// Entry model + permission string + display name assembly.
// ======================================================================

/// get_permissions_string (#1777 shared Windows mode model): the
/// synthesized 10-char POSIX form — 'c' for character devices, 'l' for
/// reparse links, 'd' for plain dirs, 'p' for the fifo marker, '-'
/// otherwise. The bits are the same model `stat -c %a` and
/// `find -printf %m` answer: character devices 0666, directories and
/// `modemap::EXEC_EXTS` executables 0755, other regulars 0644, links
/// 0777; FILE_ATTRIBUTE_READONLY clears the write bits (directories
/// included).
fn permissions_string(fd: &Fd, is_fifo: bool, name: &str) -> String {
    let (type_char, bits) = if is_fifo {
        // GNU lstat() shape: S_IFIFO | 0644 (0444 when read-only).
        ('p', 0o644u32)
    } else if fd.is_chardev() {
        ('c', 0o666)
    } else if fd.is_reparse() {
        ('l', 0o777)
    } else if fd.is_plain_dir() {
        ('d', 0o755)
    } else if is_exec_mode_name(name) {
        ('-', 0o755)
    } else {
        ('-', 0o644)
    };
    let bits = if fd.read_only() && !fd.is_reparse() && !fd.is_chardev() {
        bits & !0o222
    } else {
        bits
    };
    let mut p = String::with_capacity(10);
    p.push(type_char);
    for shift in [6, 3, 0] {
        let v = (bits >> shift) & 0o7;
        p.push(if v & 4 != 0 { 'r' } else { '-' });
        p.push(if v & 2 != 0 { 'w' } else { '-' });
        p.push(if v & 1 != 0 { 'x' } else { '-' });
    }
    p
}

/// The mode model's executable class (`modemap::EXEC_EXTS`), so `ls -l`
/// agrees with `stat -c %a`, `find -perm -u+x` and `test -x` (#1777).
/// This is narrower than the classify `*` heuristic (`is_executable_name`
/// also stars .ps1, which the shared model deliberately does not).
fn is_exec_mode_name(name: &str) -> bool {
    let e = ext_lower(name);
    crate::modemap::EXEC_EXTS.iter().any(|x| e == *x)
}

/// The effective dereference decision for one entry
/// (ls.cpp should_dereference_entry_metadata / ls.c gobble_file deref
/// switch): -L everything, -H command line, --dereference-command-line-
/// symlink-to-dir / resolved-default command-line dirs only.
fn should_deref(deref: Deref, fd: &Fd, command_line: bool) -> bool {
    if !fd.is_reparse() {
        return false;
    }
    match deref {
        Deref::All => true,
        Deref::CmdlineAll => command_line,
        Deref::CmdlineSymlinkToDir => command_line && fd.is_dir(),
        Deref::Never => false,
    }
}

/// Canonicalize through the filesystem (the deref probe): returns the
/// resolved Fd for the TARGET, or None when the link dangles.
fn dereferenced_fd(ctx: &Ctx, path: &str, orig: &Fd) -> Option<(Fd, String)> {
    let resolved = std::fs::canonicalize(api_path(ctx, path))
        .ok()
        .map(|p| p.to_string_lossy().into_owned())?;
    let ext = if resolved.starts_with(r"\\?\") {
        resolved.clone()
    } else {
        api_path(ctx, &resolved)
    };
    let fd = plat::attributes_ex(&ext).or_else(|| plat::find_first(&ext))?;
    let name = std::path::Path::new(&resolved)
        .file_name()
        .map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_default();
    let _ = orig;
    Some((fd, name))
}

/// Effective display metadata: the lstat'd find_data unless the deref
/// mode replaces it with the target's attributes (ls.cpp
/// resolve_display_entry).
fn display_fd(ctx: &Ctx, entry: &Entry, deref: Deref) -> (Fd, bool) {
    if should_deref(deref, &entry.fd, entry.command_line) {
        if let Some((fd, _)) = dereferenced_fd(ctx, &entry.full_path, &entry.fd) {
            return (fd, true);
        }
        return (entry.fd, false); // dangling: caller reports
    }
    (entry.fd, false)
}

/// The display name: quoting + indicator + ` -> target` (long format).
struct DisplayName {
    rendered: String,
    target: Option<(String, String)>, // (display, resolved_path)
    name_width: usize,
}

fn build_display_name(
    ctx: &Ctx,
    name: &str,
    full_path: &str,
    fd: &Fd,
    cfg: &Resolved,
    long_format: bool,
    command_line: bool,
) -> DisplayName {
    let extra = if cfg.indicator >= Indicator::FileType {
        "*=>@|"
    } else {
        ""
    };
    let mut rendered = apply_quoting(name, cfg.quoting, extra);
    // -q / non-tty default off, tty default on (qmark_funny_chars):
    // whatever control characters survived quoting render as '?'.
    let qmark = |s: &str| -> String {
        if cfg.hide_control {
            s.chars()
                .map(|c| if is_print(c) || c == ' ' { c } else { '?' })
                .collect()
        } else {
            s.to_string()
        }
    };
    rendered = qmark(&rendered);
    let mut target = None;

    // ` -> target`: long format, still a reparse link after the deref
    // decision, and the reparse read answers (#1639).
    if long_format && fd.is_reparse() && !should_deref(cfg.deref, fd, command_line) {
        if let Some(t) = read_link_display(ctx, full_path, fd) {
            let qd = qmark(&apply_quoting(&t.display, cfg.quoting, ""));
            target = Some((qd, t.resolved));
        }
    }
    if cfg.with_hyperlink {
        rendered = hyperlink_wrap(&rendered, full_path);
        if let Some((d, r)) = &target {
            target = Some((hyperlink_wrap(d, r), r.clone()));
        }
    }
    let indicator_extra: String = match &target {
        None => {
            let is_fifo = fd.system() && is_winux_fifo(ctx, full_path);
            indicator_suffix(cfg.indicator, name, fd, is_fifo).to_string()
        }
        Some((_, resolved)) => {
            // The indicator reflects the TARGET's type (GNU prints it
            // after the target in `name -> target/`).
            let p = probe_path(ctx, resolved);
            if p.found || p.attrs_valid {
                let tf = p.fd;
                let is_fifo = tf.system() && is_winux_fifo(ctx, resolved);
                let leaf = std::path::Path::new(resolved)
                    .file_name()
                    .map(|n| n.to_string_lossy().into_owned())
                    .unwrap_or_else(|| resolved.clone());
                indicator_suffix(cfg.indicator, &leaf, &tf, is_fifo).to_string()
            } else {
                String::new()
            }
        }
    };
    let name_width = display_width(&rendered) + display_width(&indicator_extra);
    rendered.push_str(&indicator_extra);
    DisplayName {
        rendered,
        target,
        name_width,
    }
}

/// render the name with color + hyperlink for output; returns the full
/// text including ` -> target`.
fn colored_name_text(
    ctx: &Ctx,
    dn: &DisplayName,
    name: &str,
    fd: &Fd,
    cfg: &Resolved,
    colors: &LsColors,
    state: &mut ColorState,
) -> String {
    let mut text = String::new();
    let color = if cfg.with_color {
        entry_color(colors, name, fd)
    } else {
        String::new()
    };
    if !color.is_empty() {
        text.push_str(state.prefix());
        text.push_str(&color);
    }
    text.push_str(&dn.rendered);
    if !color.is_empty() {
        text.push_str(COLOR_RESET);
    }
    if let Some((disp, resolved)) = &dn.target {
        text.push_str(" -> ");
        let tc = if cfg.with_color {
            let p = probe_path(ctx, resolved);
            if p.found {
                let leaf = std::path::Path::new(resolved)
                    .file_name()
                    .map(|n| n.to_string_lossy().into_owned())
                    .unwrap_or_default();
                entry_color(colors, &leaf, &p.fd)
            } else if p.attrs_valid {
                if p.fd.is_reparse() {
                    COLOR_LINK.to_string()
                } else if p.fd.is_dir() {
                    COLOR_DIR.to_string()
                } else {
                    String::new()
                }
            } else {
                String::new()
            }
        } else {
            String::new()
        };
        if !tc.is_empty() {
            text.push_str(&tc);
        }
        text.push_str(disp);
        if !tc.is_empty() {
            text.push_str(COLOR_RESET);
        }
    }
    text
}

fn hyperlink_wrap(display: &str, full_path: &str) -> String {
    // RFC 8089 file URI (ls.cpp hyperlink_wrap).
    let abs = {
        let p = std::path::Path::new(full_path);
        if p.is_absolute() {
            p.to_string_lossy().into_owned()
        } else {
            // resolve against the process cwd the way filesystem::absolute
            // does — the operand boundary already absolutized the probe.
            std::env::current_dir()
                .unwrap_or_default()
                .join(p)
                .to_string_lossy()
                .into_owned()
        }
    };
    let fwd = abs.replace('\\', "/");
    let mut uri = String::from("file://");
    uri.push_str(&plat::computer_name());
    uri.push('/');
    for b in fwd.bytes() {
        if b.is_ascii_alphanumeric() || b"-._~/@:".contains(&b) {
            uri.push(b as char);
        } else {
            uri.push_str(&format!("%{:02X}", b));
        }
    }
    format!("\u{1b}]8;;{uri}\u{7}{display}\u{1b}]8;;\u{7}")
}

// ======================================================================
// Owner/group + inode + link count (ls.cpp get_file_owner_and_group /
// get_file_index_string / get_link_count; fs_win::file_accounts).
// ======================================================================

fn owner_group(ctx: &Ctx, path: &str, numeric: bool) -> (String, String) {
    #[cfg(windows)]
    {
        // file_accounts yields (id, name) per side (account_from_sid);
        // its internal api_path does not collapse "..", so normalize the
        // operand first (the ".." entry in ls -la probes the parent).
        let normalized = lex_collapse(&abs_probe_path(ctx, path));
        let ((oid, oname), (gid, gname)) = fs_win::imp::file_accounts(ctx, &normalized);
        if numeric {
            (
                if oid.is_empty() { "0".into() } else { oid },
                if gid.is_empty() { "0".into() } else { gid },
            )
        } else {
            (
                if oname.is_empty() {
                    "UNKNOWN".into()
                } else {
                    oname
                },
                if gname.is_empty() {
                    "UNKNOWN".into()
                } else {
                    gname
                },
            )
        }
    }
    #[cfg(not(windows))]
    {
        use std::os::unix::fs::MetadataExt;
        let (u, g) = std::fs::metadata(path)
            .map(|m| (m.uid(), m.gid()))
            .unwrap_or((0, 0));
        let _ = ctx;
        (u.to_string(), g.to_string())
    }
}

fn inode_string(ctx: &Ctx, path: &str, fd: &Fd, deref: Deref, command_line: bool) -> String {
    let deref_it = should_deref(deref, fd, command_line);
    plat::handle_info(&api_path(ctx, path), fd, deref_it)
        .map(|(_, idx)| idx.to_string())
        .unwrap_or_default()
}

fn link_count(ctx: &Ctx, path: &str, fd: &Fd, deref: Deref, command_line: bool) -> String {
    let deref_it = should_deref(deref, fd, command_line);
    plat::handle_info(&api_path(ctx, path), fd, deref_it)
        .map(|(links, _)| links.to_string())
        .unwrap_or_else(|| "1".to_string())
}

/// get_file_size_string (ls.cpp): symbolic link (non-deref) size is the
/// target-name byte length (#1639); plain dirs take FILE_STANDARD_INFO
/// EndOfFile; everything else the find_data size.
fn file_size_string(
    ctx: &Ctx,
    entry: &Entry,
    display: &Fd,
    cfg: &Resolved,
    long_format: bool,
) -> String {
    let mut size = 0u64;
    if !is_winux_fifo(ctx, &entry.full_path)
        && entry.fd.is_symbolic_link()
        && !should_deref(cfg.deref, &entry.fd, entry.command_line)
    {
        if let Some(t) = read_link_display(ctx, &entry.full_path, &entry.fd) {
            size = t.display.len() as u64;
        }
    }
    if size == 0 {
        if display.is_plain_dir() {
            if let Some(v) = plat::dir_standard_size(
                &api_path(ctx, &abs_probe_path(ctx, &entry.full_path)),
                false,
            ) {
                size = v;
            }
        }
    }
    if size == 0 {
        size = display.size;
    }
    let _ = long_format;
    format_scaled_size(size, cfg.size.file_mode, cfg.size.file_block_size.max(1))
}

// ======================================================================
// Resolved runtime view (post-decode state, ls.c:2218-2460).
// ======================================================================

struct Resolved {
    format: Format,
    sort: Sort,
    time_type: TimeType,
    time_style: TimeStyle,
    time_custom: String,
    quoting: Quoting,
    hide_control: bool,
    indicator: Indicator,
    deref: Deref,
    line_length: u64, // 0 = no limit
    tabsize: usize,
    size: SizeCfg,
    with_color: bool,
    with_hyperlink: bool,
    eol: u8,
}

fn time_style_error(ctx: &Ctx, v: &str, ambiguous: bool) -> String {
    tr!(
        ctx,
        "command.ls.error.time_style",
        "{kind} argument '{value}' for 'time style'\nValid arguments are:\n  - [posix-]full-iso\n  - [posix-]long-iso\n  - [posix-]iso\n  - [posix-]locale\n  - +FORMAT (e.g., +%H:%M) for a 'date'-style format",
        kind = if ambiguous { "ambiguous" } else { "invalid" },
        value = v
    )
}

/// Resolve the post-decode configuration (ls.c:2218-2480): format
/// default, widths, tabsize, time-style validation (long format only —
/// probed: TIME_STYLE=bogus without -l is ignored), dereference default,
/// quoting default, --dired/--zero conflict.
fn resolve(c: &Cfg, ctx: &Ctx, io: &mut Io) -> Result<Resolved, u32> {
    let format = c.format_opt.unwrap_or(if tty_stdout() {
        Format::Columns
    } else {
        Format::OnePerLine
    });

    // line_length (ls.c:2282-2307, #1504): -w always wins (0 = no
    // limit); otherwise console cols on a tty, then COLUMNS, default 80.
    // The tty/COLUMNS probes run only for column formats or --color.
    let mut line_length: Option<u64> = c.width_opt;
    if line_length.is_none()
        && (matches!(format, Format::Columns | Format::Across | Format::Commas) || c.with_color)
    {
        if tty_stdout() {
            let cols = plat::console_width();
            if cols > 0 {
                line_length = Some(cols as u64);
            }
        }
        if line_length.is_none() {
            if let Some(v) = env(ctx, "COLUMNS") {
                if !v.is_empty() {
                    let d = decode_line_length(&v);
                    if d < 0 {
                        ls_error(
                            io,
                            &tr!(
                                ctx,
                                "command.ls.error.columns_width",
                                "ignoring invalid width in environment variable COLUMNS: \u{2018}{value}\u{2019}",
                                value = v
                            ),
                        );
                    } else {
                        line_length = Some(d as u64);
                    }
                }
            }
        }
    }
    let line_length = line_length.unwrap_or(80);

    // tabsize only matters to column formats (ls.c:2309-2324).
    let mut tabsize = 8usize;
    if matches!(format, Format::Columns | Format::Across | Format::Commas) {
        if let Some(t) = c.tabsize_opt {
            tabsize = t as usize;
        } else if let Some(v) = env(ctx, "TABSIZE") {
            match parse_base0(&v) {
                Some(n) if n <= i32::MAX as u64 => tabsize = n as usize,
                _ => {
                    ls_error(
                        io,
                        &tr!(
                            ctx,
                            "command.ls.error.tabsize_env",
                            "ignoring invalid tab size in environment variable TABSIZE: \u{2018}{value}\u{2019}",
                            value = v
                        ),
                    );
                }
            }
        }
    }

    // -q: non-tty default false, tty default true (ls.c:2327-2330).
    let hide_control = c.hide_control.unwrap_or_else(tty_stdout);

    // quoting default: tty -> shell-escape, otherwise literal
    // (ls.c:2391-2397 + QUOTING_STYLE env).
    let quoting = c.quoting.unwrap_or_else(|| {
        if let Some(v) = env(ctx, "QUOTING_STYLE") {
            if let Ok(i) = argmatch(&v, QUOTING_TABLE) {
                return QUOTING_TABLE[i].1;
            }
        }
        if tty_stdout() {
            Quoting::ShellEscape
        } else {
            Quoting::Literal
        }
    });

    // dired is meaningful only in long format and sans --hyperlink
    // (ls.c:2372-2377); --zero then conflicts fatally (error(), no
    // try-help).
    if c.eol == 0 && c.dired && format == Format::Long && !c.with_hyperlink {
        ls_error(
            io,
            &tr!(
                ctx,
                "command.ls.error.dired_zero",
                "--dired and --zero are incompatible"
            ),
        );
        return Err(2);
    }

    // sort_type default: a non-mtime time field implies time sorting only
    // without -l (ls.c:2387-2391).
    let sort = c.sort_opt.unwrap_or(
        if format != Format::Long
            && matches!(
                c.time_type,
                TimeType::Ctime | TimeType::Atime | TimeType::Btime
            )
        {
            Sort::Time
        } else {
            Sort::Name
        },
    );

    // time style: validated only in long format (ls.c:2395-2473); the
    // posix- prefix is honored, +FORMAT splits on '\n'.
    let mut time_style = TimeStyle::Default;
    let mut time_custom = String::new();
    if format == Format::Long {
        let style = c
            .time_style_arg
            .clone()
            .or_else(|| env(ctx, "TIME_STYLE"))
            .unwrap_or_else(|| "locale".to_string());
        let mut st = style.as_str();
        while let Some(rest) = st.strip_prefix("posix-") {
            st = rest;
        }
        if let Some(custom) = st.strip_prefix('+') {
            // more than one embedded newline is invalid (ls.c:2409-2416)
            let rest = custom;
            let parts: Vec<&str> = rest.split('\n').collect();
            if parts.len() > 2 {
                ls_error(
                    io,
                    &tr!(
                        ctx,
                        "command.ls.error.invalid_time_style_format",
                        "invalid time style format '{value}'",
                        value = rest
                    ),
                );
                return Err(2);
            }
            time_style = TimeStyle::Custom;
            time_custom = custom.to_string();
        } else {
            const TS: &[(&str, TimeStyle)] = &[
                ("full-iso", TimeStyle::FullIso),
                ("long-iso", TimeStyle::LongIso),
                ("iso", TimeStyle::Iso),
                ("locale", TimeStyle::Locale),
            ];
            match argmatch(st, TS) {
                Ok(i) => time_style = TS[i].1,
                Err(amb) => {
                    ls_error(io, &time_style_error(ctx, st, amb));
                    let _ = writeln!(
                        io.stderr,
                        "{}",
                        tr!(
                            ctx,
                            "command.ls.error.try_help",
                            "Try '{prog} --help' for more information.",
                            prog = "ls"
                        )
                    );
                    return Err(2);
                }
            }
        }
    }

    // dereference default (ls.c:1696-1700): -d / classify / long format
    // keep links; otherwise command-line symlink-to-dirs are followed.
    let deref = c.deref.unwrap_or(
        if c.immediate_dirs || c.indicator == Indicator::Classify || format == Format::Long {
            Deref::Never
        } else {
            Deref::CmdlineSymlinkToDir
        },
    );

    Ok(Resolved {
        format,
        sort,
        time_type: c.time_type,
        time_style,
        time_custom,
        quoting,
        hide_control,
        indicator: c.indicator,
        deref,
        line_length,
        tabsize,
        size: c.size,
        with_color: c.with_color,
        with_hyperlink: c.with_hyperlink,
        eol: c.eol,
    })
}

// ======================================================================
// Listing engine.
// ======================================================================

/// Out writes through io.stdout, tracking byte position for --dired.
struct Out<'a, 'b> {
    io: &'a mut Io<'b>,
    pos: u64,
    dired_offsets: Vec<(u64, u64)>,
    subdired_offsets: Vec<(u64, u64)>,
    dired: bool,
    err: bool,
}

impl<'a, 'b> Out<'a, 'b> {
    fn p(&mut self, s: &str) {
        if self.err {
            return;
        }
        if self.io.stdout.write_all(s.as_bytes()).is_err() {
            self.err = true;
            return;
        }
        self.pos += s.len() as u64;
    }
    fn note_name(&mut self, offset_in_text: usize, len: usize) {
        if self.dired {
            let begin = self.pos + offset_in_text as u64;
            self.dired_offsets.push((begin, begin + len as u64));
        }
    }
}

/// should_show_entry (ls.cpp / ls.c:3152 file_ignored + the Windows
/// HIDDEN attribute rule).
fn show_entry(name: &str, fd: &Fd, c: &Cfg) -> bool {
    let show_all = c.ignore == Ignore::Minimal;
    let almost = show_all || c.ignore == Ignore::DotDot;
    if name == "." || name == ".." {
        return show_all;
    }
    if name.starts_with('.') && !almost {
        return false;
    }
    if fd.hidden() && !almost {
        return false;
    }
    if !almost && c.hide_pats.iter().any(|p| wildcard_match(p, name)) {
        return false;
    }
    if c.ignore_pats.iter().any(|p| wildcard_match(p, name)) {
        return false;
    }
    true
}

/// sort comparator for the resolved sort key (cmp_* + name tiebreak).
fn entry_cmp(a: &Entry, b: &Entry, cfg: &Resolved) -> Ordering {
    let ord = match cfg.sort {
        Sort::Name => a.name.cmp(&b.name),
        Sort::Time => {
            let (ta, tb) = (entry_time(a, cfg), entry_time(b, cfg));
            tb.cmp(&ta).then_with(|| a.name.cmp(&b.name))
        }
        Sort::Size => b.fd.size.cmp(&a.fd.size).then_with(|| a.name.cmp(&b.name)),
        Sort::Version => compare_version(&a.name, &b.name).then_with(|| a.name.cmp(&b.name)),
        Sort::Extension => ext_raw(&a.name)
            .cmp(ext_raw(&b.name))
            .then_with(|| a.name.cmp(&b.name)),
        Sort::Width => {
            let wa = display_width(&apply_quoting(&a.name, cfg.quoting, ""));
            let wb = display_width(&apply_quoting(&b.name, cfg.quoting, ""));
            wa.cmp(&wb).then_with(|| a.name.cmp(&b.name))
        }
        Sort::None => Ordering::Equal,
    };
    ord
}

fn entry_time(e: &Entry, cfg: &Resolved) -> i64 {
    match cfg.time_type {
        TimeType::Mtime => e.fd.mtime,
        TimeType::Atime => e.fd.atime,
        TimeType::Ctime | TimeType::Btime => e.fd.ctime,
    }
}

/// sort + -r + --group-directories-first (sort_files, ls.c:4080-4111):
/// sort_none returns before the mpsort call, so -r and
/// --group-directories-first are inert with -U/--sort=none/-f.
/// directories_first is a partition on the REVERSED order — inside each
/// group the reversal holds.
fn sort_entries(entries: &mut Vec<Entry>, cfg: &Resolved, c: &Cfg) {
    if cfg.sort == Sort::None {
        return;
    }
    entries.sort_by(|a, b| entry_cmp(a, b, cfg));
    if c.reverse {
        entries.reverse();
    }
    if c.dirs_first {
        let (dirs, rest): (Vec<Entry>, Vec<Entry>) = entries
            .drain(..)
            .partition(|e| e.fd.attrs & ATTR_DIRECTORY != 0);
        entries.extend(dirs);
        entries.extend(rest);
    }
}

/// A rendered non-long entry (text + visible width + name span for
/// --dired).
struct Rendered {
    text: String,
    width: usize,
    name_off: usize,
    name_len: usize,
}

/// build_listing_prefix (ls.cpp): inode blocks scontext, each followed
/// by a space. GNU pads inode/blocks to the column width in non-comma
/// formats (print_file_name_and_frills ls.c:4871).
fn listing_prefix(
    ctx: &Ctx,
    entry: &Entry,
    display: &Fd,
    cfg: &Resolved,
    c: &Cfg,
    inode_w: usize,
    blocks_w: usize,
) -> String {
    let mut p = String::new();
    if c.print_inode {
        let v = if entry.stat_failed {
            "?".to_string()
        } else {
            inode_string(
                ctx,
                &entry.full_path,
                display,
                cfg.deref,
                entry.command_line,
            )
        };
        let w = if cfg.format == Format::Commas {
            0
        } else {
            inode_w
        };
        p.push_str(&format!("{v:>w$} ", w = w));
    }
    if c.print_block_size {
        let v = if entry.stat_failed {
            "?".to_string()
        } else {
            allocated_blocks_string(ctx, &entry.full_path, display, &cfg.size)
        };
        let w = if cfg.format == Format::Commas {
            0
        } else {
            blocks_w
        };
        p.push_str(&format!("{v:>w$} ", w = w));
    }
    if c.print_scontext {
        p.push_str("? ");
    }
    p
}

fn render_entry(
    ctx: &Ctx,
    entry: &Entry,
    display: &Fd,
    cfg: &Resolved,
    c: &Cfg,
    colors: &LsColors,
    cs: &mut ColorState,
    inode_w: usize,
    blocks_w: usize,
) -> Rendered {
    let prefix = listing_prefix(ctx, entry, display, cfg, c, inode_w, blocks_w);
    let dn = build_display_name(
        ctx,
        &entry.name,
        &entry.full_path,
        display,
        cfg,
        false,
        entry.command_line,
    );
    let mut text = prefix.clone();
    let name_off = text.len();
    let name_text = colored_name_text(ctx, &dn, &entry.name, display, cfg, colors, cs);
    text.push_str(&name_text);
    // apply hide-control on the raw byte level for controls that survived
    // (quoting styles already handled them; -q turns them to '?').
    let _ = cfg.hide_control;
    Rendered {
        width: prefix.len() + dn.name_width,
        name_off,
        name_len: dn.rendered.len(),
        text,
    }
}

/// print_with_separator (ls.c:5213): sep + ' ' while it fits, else
/// sep + eol.
fn print_with_separator(out: &mut Out, entries: &[Rendered], sep: char, cfg: &Resolved) {
    let mut pos: usize = 0;
    for (i, e) in entries.iter().enumerate() {
        let len = if cfg.line_length > 0 { e.width } else { 0 };
        if i != 0 {
            let separator = if cfg.line_length == 0 || (pos + len + 2 < cfg.line_length as usize) {
                pos += 2;
                ' '
            } else {
                pos = 0;
                cfg.eol as char
            };
            out.p(&sep.to_string());
            out.p(&separator.to_string());
        }
        out.note_name(e.name_off, e.name_len);
        out.p(&e.text);
        pos += len;
    }
    out.p(&(cfg.eol as char).to_string());
}

/// indent (ls.c:5254): TAB when it lands strictly inside the pad run.
fn indent(out: &mut Out, from: usize, to: usize, tabsize: usize) {
    let mut from = from;
    while from < to {
        if tabsize != 0 && to / tabsize > (from + 1) / tabsize {
            out.p("\t");
            from += tabsize - from % tabsize;
        } else {
            out.p(" ");
            from += 1;
        }
    }
}

/// calculate_columns + print_many_per_line/print_horizontal
/// (ls.c:5136-5210, 5366-5412). by_columns selects the -C (column-major)
/// vs -x (row-major) index arithmetic.
fn print_grid(out: &mut Out, entries: &[Rendered], cfg: &Resolved, across: bool) {
    if entries.is_empty() {
        return;
    }
    if cfg.line_length == 0 {
        // No line limit: one line, "  " gutters (print_with_separator(' ')).
        for (i, e) in entries.iter().enumerate() {
            if i > 0 {
                out.p("  ");
            }
            out.note_name(e.name_off, e.name_len);
            out.p(&e.text);
        }
        out.p("\n");
        return;
    }
    const MIN_COL: usize = 3;
    let line_length = cfg.line_length as usize;
    let mut max_idx = line_length / MIN_COL;
    if line_length % MIN_COL != 0 {
        max_idx += 1;
    }
    let max_cols = if max_idx > 0 && max_idx < entries.len() {
        max_idx
    } else {
        entries.len()
    };
    // init_column_info + incremental validity (ls.c:5366-5412): each
    // candidate col-count starts at baseline MIN_COL per column and
    // grows; a candidate stays valid while line_len < line_length.
    let n = entries.len();
    let mut valid = vec![true; max_cols];
    let mut line_len: Vec<usize> = (1..=max_cols).map(|i| i * MIN_COL).collect();
    let mut col_arr: Vec<Vec<usize>> = (0..max_cols).map(|_| Vec::new()).collect();
    for i in 0..max_cols {
        col_arr[i] = vec![MIN_COL; i + 1];
    }
    for (filesno, e) in entries.iter().enumerate() {
        for i in 0..max_cols {
            if !valid[i] {
                continue;
            }
            let idx = if across {
                filesno % (i + 1)
            } else {
                filesno / ((n + i) / (i + 1))
            };
            let real = e.width + if idx == i { 0 } else { 2 };
            if col_arr[i][idx] < real {
                line_len[i] += real - col_arr[i][idx];
                col_arr[i][idx] = real;
                valid[i] = line_len[i] < line_length;
            }
        }
    }
    let mut cols = max_cols;
    while cols > 1 && !valid[cols - 1] {
        cols -= 1;
    }
    let col_widths = &col_arr[cols - 1];
    let rows = n / cols + usize::from(n % cols != 0);

    if across {
        // print_horizontal (ls.c:5172): first entry, then entries with
        // indent to each column's max width; wrap at col 0.
        let mut pos = 0usize;
        for i in 0..n {
            let col = i % cols;
            if i > 0 {
                if col == 0 {
                    out.p(&(cfg.eol as char).to_string());
                    pos = 0;
                } else {
                    let prev = &entries[i - 1];
                    indent(
                        out,
                        pos + prev.width,
                        pos + col_widths[col - 1],
                        cfg.tabsize,
                    );
                    pos += col_widths[col - 1];
                }
            }
            let e = &entries[i];
            out.note_name(e.name_off, e.name_len);
            out.p(&e.text);
            let _ = pos;
        }
        out.p(&(cfg.eol as char).to_string());
        return;
    }

    // print_many_per_line (ls.c:5136): rows x cols, column-major.
    for row in 0..rows {
        let mut col = 0usize;
        let mut filesno = row;
        let mut pos = 0usize;
        loop {
            let e = &entries[filesno];
            let max_name = col_widths[col];
            col += 1;
            out.note_name(e.name_off, e.name_len);
            out.p(&e.text);
            filesno += rows;
            if filesno >= n {
                break;
            }
            indent(out, pos + e.width, pos + max_name, cfg.tabsize);
            pos += max_name;
        }
        out.p(&(cfg.eol as char).to_string());
    }
}

// ======================================================================
// Directory listing (print_dir ls.c:2981 / ls.cpp list_directory).
// ======================================================================

fn print_total_line(out: &mut Out, ctx: &Ctx, entries: &[Entry], cfg: &Resolved, c: &Cfg) {
    let mut total_blocks = 0u64;
    let mut total_usage = 0u64;
    for e in entries {
        let (disp, _) = display_fd(ctx, e, cfg.deref);
        total_blocks += allocated_block_count(ctx, &e.full_path, &disp, &cfg.size);
        total_usage += allocated_usage_value(ctx, &e.full_path, &disp, &cfg.size);
    }
    out.p(&tr!(ctx, "command.ls.total", "total "));
    if cfg.size.block_mode == SizeMode::Blocks {
        out.p(&total_blocks.to_string());
    } else {
        out.p(&format_scaled_size(
            total_usage,
            cfg.size.block_mode,
            cfg.size.block_size.max(1),
        ));
    }
    out.p(&(c.eol as char).to_string());
}

struct DirResult {
    ok: bool,
    minor: bool, // entry-level failures (GNU exit 1)
}

/// Enumerate + filter + gobble one directory (ls.c:2981 print_dir's
/// read loop + ls.c:3370 gobble_file). A -L/-H stat failure is reported
/// at gobble time (GNU order: during enumeration, before the sorted
/// listing) and marks the entry stat_failed — GNU keeps it in the list
/// with '?' fields and minor exit status. `display_name` is the operand
/// spelling used in error text; `lookup` is the probe path.
fn collect_entries(
    ctx: &Ctx,
    c: &Cfg,
    cfg: &Resolved,
    io: &mut Io,
    display_name: &str,
    lookup: &str,
    res: &mut DirResult,
) -> Option<Vec<Entry>> {
    let lookup_w = resolve_bare_exe(ctx, &normalize_lookup_path(lookup));
    let ext = api_path(ctx, &lookup_w);
    let found = match plat::enumerate(&ext) {
        Ok(v) => v,
        Err(e) => {
            // ls.cpp: ERROR_FILE_NOT_FOUND on a real dir is just empty.
            if e == 2
                && plat::attributes(&ext)
                    .map(|a| a & ATTR_DIRECTORY != 0)
                    .unwrap_or(false)
            {
                return Some(Vec::new());
            }
            let reason = if e == 5 {
                "Permission denied"
            } else {
                "No such file or directory"
            };
            ls_error(
                io,
                &tr!(
                    ctx,
                    "command.ls.error.cannot_open_dir",
                    "cannot open directory '{name}': {reason}",
                    name = display_name,
                    reason = reason
                ),
            );
            res.ok = false;
            return None;
        }
    };
    let mut entries: Vec<Entry> = Vec::new();
    for (name, fd) in found {
        if !show_entry(&name, &fd, c) {
            continue;
        }
        let full_path = join_path(&lookup_w, &name);
        let mut stat_failed = false;
        if should_deref(cfg.deref, &fd, false) && dereferenced_fd(ctx, &full_path, &fd).is_none() {
            let joined = if display_name.is_empty() {
                full_path.clone()
            } else {
                format!(
                    "{}{}{}",
                    display_name,
                    if display_name.ends_with('/') || display_name.ends_with('\\') {
                        ""
                    } else {
                        "/"
                    },
                    name
                )
            };
            ls_error(
                io,
                &tr!(
                    ctx,
                    "command.ls.error.cannot_access",
                    "cannot access '{name}': {reason}",
                    name = joined,
                    reason = "No such file or directory"
                ),
            );
            res.minor = true;
            stat_failed = true;
        }
        entries.push(Entry {
            full_path,
            name,
            fd,
            command_line: false,
            arg_dir: false,
            stat_failed,
        });
    }
    Some(entries)
}

/// print_dir: enumerate + filter + sort + total + format dispatch.
fn list_directory(
    out: &mut Out,
    ctx: &Ctx,
    c: &Cfg,
    cfg: &Resolved,
    colors: &LsColors,
    cs: &mut ColorState,
    display_name: &str,
    lookup: &str,
) -> DirResult {
    let mut res = DirResult {
        ok: true,
        minor: false,
    };
    let Some(mut entries) = collect_entries(ctx, c, cfg, out.io, display_name, lookup, &mut res)
    else {
        return res;
    };
    sort_entries(&mut entries, cfg, c);
    // GNU prints the "total" line for every directory listing in long
    // format or with -s — including "total 0" for an empty directory.
    if cfg.format == Format::Long || c.print_block_size {
        print_total_line(out, ctx, &entries, cfg, c);
    }
    print_entries(out, ctx, c, cfg, colors, cs, &entries);
    res
}

/// Format dispatch for one listing body (print_current_files ls.c:4128).
/// The "total" line is printed by the caller (list_directory), never for
/// command-line file operands.
fn print_entries(
    out: &mut Out,
    ctx: &Ctx,
    c: &Cfg,
    cfg: &Resolved,
    colors: &LsColors,
    cs: &mut ColorState,
    entries: &[Entry],
) {
    if entries.is_empty() {
        return;
    }
    if cfg.format == Format::Long {
        print_long_entries(out, ctx, c, cfg, colors, cs, entries);
        return;
    }

    // widths for -i/-s in non-comma formats (GNU pads to column width).
    let mut inode_w = 0usize;
    let mut blocks_w = 0usize;
    if c.print_inode || c.print_block_size {
        for e in entries {
            if e.stat_failed {
                continue;
            }
            let (disp, _) = display_fd(ctx, e, cfg.deref);
            if c.print_inode {
                inode_w = inode_w
                    .max(inode_string(ctx, &e.full_path, &disp, cfg.deref, e.command_line).len());
            }
            if c.print_block_size {
                blocks_w = blocks_w
                    .max(allocated_blocks_string(ctx, &e.full_path, &disp, &cfg.size).len());
            }
        }
    }
    let mut rendered: Vec<Rendered> = Vec::with_capacity(entries.len());
    for e in entries {
        let (disp, _) = display_fd(ctx, e, cfg.deref);
        rendered.push(render_entry(
            ctx, e, &disp, cfg, c, colors, cs, inode_w, blocks_w,
        ));
    }
    match cfg.format {
        Format::OnePerLine => {
            for r in &rendered {
                out.note_name(r.name_off, r.name_len);
                out.p(&r.text);
                out.p(&(c.eol as char).to_string());
            }
        }
        Format::Commas => {
            print_with_separator(out, &rendered, ',', cfg);
        }
        Format::Columns => print_grid(out, &rendered, cfg, false),
        Format::Across => print_grid(out, &rendered, cfg, true),
        Format::Long => unreachable!(),
    }
}

/// print_long_format (ls.c:4304 / ls.cpp list_directory long branch):
/// per-entry field widths across the whole listing, then each line.
fn print_long_entries(
    out: &mut Out,
    ctx: &Ctx,
    c: &Cfg,
    cfg: &Resolved,
    colors: &LsColors,
    cs: &mut ColorState,
    entries: &[Entry],
) {
    struct Row {
        perms: String,
        inode: String,
        blocks: String,
        links: String,
        size: String,
        mtime: String,
        owner: String,
        author: String,
        group: String,
        display: Option<DisplayName>,
        fd: Fd,
        name: String,
    }
    let mut rows: Vec<Row> = Vec::with_capacity(entries.len());
    for e in entries {
        if e.stat_failed {
            // GNU print_long_format with stat_ok == false: '?' fields.
            let display =
                build_display_name(ctx, &e.name, &e.full_path, &e.fd, cfg, true, e.command_line);
            rows.push(Row {
                perms: "l?????????".to_string(),
                inode: "?".into(),
                blocks: "?".into(),
                links: "?".into(),
                size: "?".into(),
                mtime: "?".into(),
                owner: "?".into(),
                author: "?".into(),
                group: "?".into(),
                display: Some(display),
                fd: e.fd,
                name: e.name.clone(),
            });
            continue;
        }
        let (disp, _) = display_fd(ctx, e, cfg.deref);
        let is_fifo = disp.system() && is_winux_fifo(ctx, &e.full_path);
        let perms = permissions_string(&disp, is_fifo, &e.name);
        let inode = if c.print_inode {
            inode_string(ctx, &e.full_path, &e.fd, cfg.deref, e.command_line)
        } else {
            String::new()
        };
        let blocks = if c.print_block_size {
            allocated_blocks_string(ctx, &e.full_path, &disp, &cfg.size)
        } else {
            String::new()
        };
        let links = link_count(ctx, &e.full_path, &e.fd, cfg.deref, e.command_line);
        let size = file_size_string(ctx, e, &disp, cfg, true);
        let mtime = get_time_string(&disp, cfg.time_type, cfg.time_style, &cfg.time_custom);
        let (owner, group) = owner_group(ctx, &e.full_path, c.numeric_ids);
        let display =
            build_display_name(ctx, &e.name, &e.full_path, &disp, cfg, true, e.command_line);
        rows.push(Row {
            perms,
            inode,
            blocks,
            links,
            size,
            mtime,
            author: owner.clone(),
            owner,
            group,
            display: Some(display),
            fd: disp,
            name: e.name.clone(),
        });
    }
    let mut w_inode = 0usize;
    let mut w_blocks = 0usize;
    let mut w_links = 0usize;
    let mut w_size = 0usize;
    let mut w_owner = 0usize;
    let mut w_author = 0usize;
    let mut w_group = 0usize;
    for r in &rows {
        w_inode = w_inode.max(r.inode.len());
        w_blocks = w_blocks.max(r.blocks.len());
        w_links = w_links.max(r.links.len());
        w_size = w_size.max(r.size.len());
        w_owner = w_owner.max(r.owner.len());
        w_author = w_author.max(r.author.len());
        w_group = w_group.max(r.group.len());
    }
    if c.print_owner && w_owner == 0 {
        w_owner = 1;
    }
    if c.print_author && w_author == 0 {
        w_author = 1;
    }
    if c.print_group && w_group == 0 {
        w_group = 1;
    }
    if c.print_inode && w_inode == 0 {
        w_inode = 1;
    }
    if c.print_block_size && w_blocks == 0 {
        w_blocks = 1;
    }
    if w_links == 0 {
        w_links = 1;
    }
    if w_size == 0 {
        w_size = 1;
    }
    for r in &rows {
        let mut line = String::new();
        if c.print_inode {
            line.push_str(&format!("{:>w$} ", r.inode, w = w_inode));
        }
        if c.print_block_size {
            line.push_str(&format!("{:>w$} ", r.blocks, w = w_blocks));
        }
        line.push_str(&r.perms);
        line.push(' ');
        line.push_str(&format!("{:>w$} ", r.links, w = w_links));
        if c.print_owner {
            line.push_str(&format!("{:<w$} ", r.owner, w = w_owner));
        }
        if c.print_author {
            line.push_str(&format!("{:<w$} ", r.author, w = w_author));
        }
        if c.print_group {
            line.push_str(&format!("{:<w$} ", r.group, w = w_group));
        }
        if c.print_scontext {
            line.push_str("? ");
        }
        line.push_str(&format!("{:>w$} ", r.size, w = w_size));
        line.push_str(&r.mtime);
        line.push(' ');
        let name_off = line.len();
        out.note_name(
            name_off,
            r.display.as_ref().map(|d| d.rendered.len()).unwrap_or(0),
        );
        line.push_str(&colored_name_text(
            ctx,
            r.display.as_ref().unwrap(),
            &r.name,
            &r.fd,
            cfg,
            colors,
            cs,
        ));
        out.p(&line);
        out.p(&(c.eol as char).to_string());
    }
}

// ======================================================================
// Recursive listing (list_directory_recursive): visit-set loop guard,
/// "dir/sub:" headers, blank-line separators.
// ======================================================================

fn list_recursive(
    out: &mut Out,
    ctx: &Ctx,
    c: &Cfg,
    cfg: &Resolved,
    colors: &LsColors,
    cs: &mut ColorState,
    visited: &mut std::collections::BTreeSet<String>,
    path: &str,
    lookup: &str,
    header: bool,
    res: &mut DirResult,
) {
    let norm = path.replace('\\', "/");
    if visited.contains(&norm) {
        return;
    }
    visited.insert(norm.clone());
    if header {
        if c.dired {
            let begin = out.pos;
            out.subdired_offsets
                .push((begin, begin + norm.len() as u64));
        }
        out.p(&norm);
        out.p(":\n");
    }
    let Some(mut entries) = collect_entries(ctx, c, cfg, out.io, &norm, lookup, res) else {
        res.ok = false;
        return;
    };
    sort_entries(&mut entries, cfg, c);
    if cfg.format == Format::Long || c.print_block_size {
        print_total_line(out, ctx, &entries, cfg, c);
    }
    print_entries(out, ctx, c, cfg, colors, cs, &entries);

    // recurse through subdirs in the SORTED listing order (GNU walks
    // cwd_file, which is already sorted); -L follows links to dirs.
    // "." and ".." are listed under -a but never entered (ls.c:3185).
    let follow_links = cfg.deref == Deref::All;
    for e in &entries {
        if e.name == "." || e.name == ".." {
            continue;
        }
        let recurse = e.fd.is_plain_dir() || (follow_links && e.fd.is_dir() && e.fd.is_reparse());
        if !recurse {
            continue;
        }
        let sub_display = format!("{}/{}", norm.trim_end_matches('/'), e.name);
        if visited.contains(&sub_display) {
            continue;
        }
        out.p("\n");
        list_recursive(
            out,
            ctx,
            c,
            cfg,
            colors,
            cs,
            visited,
            &sub_display,
            &e.full_path.clone(),
            true,
            res,
        );
    }
}

// ======================================================================
// Operand processing (GNU main flow ls.c:1750-1840).
// ======================================================================

fn make_generic_display(path: &str) -> String {
    path.replace('\\', "/")
}

/// gobble one operand: probe (exe-retry + pseudo-device), apply the
/// operand-level deref for classification (arg_dir), return the entry or
/// report ENOENT.
fn gobble_operand(ctx: &Ctx, path: &str, c: &Cfg, cfg: &Resolved, io: &mut Io) -> Option<Entry> {
    let probe = probe_path(ctx, path);
    if !probe.attrs_valid && !probe.found {
        ls_error(
            io,
            &tr!(
                ctx,
                "command.ls.error.cannot_access",
                "cannot access '{name}': {reason}",
                name = path,
                reason = "No such file or directory"
            ),
        );
        return None;
    }
    let fd = probe.fd;
    let lookup = resolve_bare_exe(ctx, &normalize_lookup_path(path));
    // operand-level deref for classification: the effective stat decides
    // dir-vs-file the way gobble_file's do_stat does (ls.c:1696-1700).
    let mut classify_fd = fd;
    if should_deref(cfg.deref, &fd, true) {
        match dereferenced_fd(ctx, &lookup, &fd) {
            Some((d, _)) => classify_fd = d,
            None => {
                if cfg.deref == Deref::All || cfg.deref == Deref::CmdlineAll {
                    // -L/-H on a dangling operand: stat() failed (GNU
                    // reports the operand, exit 2 — ls.cpp list_file).
                    ls_error(
                        io,
                        &tr!(
                            ctx,
                            "command.ls.error.cannot_access",
                            "cannot access '{name}': {reason}",
                            name = path,
                            reason = "No such file or directory"
                        ),
                    );
                    return None;
                }
                // CmdlineSymlinkToDir: GNU need_lstat()ed, the lstat
                // succeeds — the operand stays a plain link entry.
            }
        }
    }
    let arg_dir = classify_fd.is_dir() && !c.immediate_dirs;
    Some(Entry {
        name: path.to_string(),
        full_path: lookup,
        fd,
        command_line: true,
        arg_dir,
        stat_failed: false,
    })
}

/// The GNU main flow: gobble operands, sort, extract dirs, print files,
/// then print each pending dir (header when >1 or when files printed;
/// -R recurses). Returns the process exit status component.
fn process_paths(
    out: &mut Out,
    ctx: &Ctx,
    c: &Cfg,
    cfg: &Resolved,
    colors: &LsColors,
    cs: &mut ColorState,
    operands: &[OsString],
) -> (bool, bool) {
    let (expanded, mut success, logical_count) = expand_operands(ctx, operands, out.io);
    let mut minor = false;
    let mut files: Vec<Entry> = Vec::new();
    let mut dirs: Vec<Entry> = Vec::new();
    for path in &expanded {
        match gobble_operand(ctx, path, c, cfg, out.io) {
            Some(e) => {
                if e.arg_dir {
                    dirs.push(e);
                } else {
                    files.push(e);
                }
            }
            None => success = false,
        }
    }
    // sort all operands together, then partition (extract_dirs_from_files
    // order: sort first over the whole operand set, dirs keep order).
    let mut all: Vec<Entry> = files.iter().chain(dirs.iter()).cloned().collect();
    sort_entries(&mut all, cfg, c);
    files.clear();
    dirs.clear();
    for e in all {
        if e.arg_dir {
            dirs.push(e);
        } else {
            files.push(e);
        }
    }

    let single_dir_no_header = files.is_empty() && dirs.len() == 1 && logical_count <= 1;
    let mut printed_any = false;
    if !files.is_empty() {
        // GNU print_current_files for file operands — no "total" line.
        print_entries(out, ctx, c, cfg, colors, cs, &files);
        printed_any = true;
    }
    let mut visited: std::collections::BTreeSet<String> = Default::default();
    let mut first_dir = true;
    for d in &dirs {
        if printed_any || !first_dir {
            out.p("\n");
        }
        if c.recursive {
            let mut res = DirResult {
                ok: true,
                minor: false,
            };
            let disp = make_generic_display(&d.name);
            list_recursive(
                out,
                ctx,
                c,
                cfg,
                colors,
                cs,
                &mut visited,
                &disp,
                &d.full_path,
                true,
                &mut res,
            );
            success &= res.ok;
            minor |= res.minor;
        } else {
            if !single_dir_no_header {
                let disp = make_generic_display(&d.name);
                if c.dired {
                    let begin = out.pos;
                    out.subdired_offsets
                        .push((begin, begin + disp.len() as u64));
                }
                out.p(&disp);
                out.p(":\n");
            }
            let r = list_directory(out, ctx, c, cfg, colors, cs, &d.name, &d.full_path);
            success &= r.ok;
            minor |= r.minor;
        }
        printed_any = true;
        first_dir = false;
    }
    (success, minor)
}

// ======================================================================
// Entry point.
// ======================================================================

const LS_HELP_BODY: &str = r#"Usage: ls [OPTION]... [FILE]...
List information about the FILEs (the current directory by default).
Sort entries alphabetically if none of -cftuvSUX nor --sort is specified.

Mandatory arguments to long options are mandatory for short options too.
  -a, --all                  do not ignore entries starting with .
  -A, --almost-all           do not list implied . and ..
      --author               with -l, print the author of each file
  -b, --escape               print C-style escapes for nongraphic characters
      --block-size=SIZE      with -l, scale sizes by SIZE when printing them;
                             e.g., '--block-size=M'; see SIZE format below

  -B, --ignore-backups       do not list implied entries ending with ~
  -c                         with -lt: sort by, and show, ctime (time of last
                             change of file status information);
                             with -l: show ctime and sort by name;
                             otherwise: sort by ctime, newest first

  -C                         list entries by columns
      --color[=WHEN]         color the output WHEN; more info below
  -d, --directory            list directories themselves, not their contents
  -D, --dired                generate output designed for Emacs' dired mode
  -f                         list all entries in directory order
  -F, --classify[=WHEN]      append indicator (one of */=>@|) to entries WHEN
      --file-type            likewise, except do not append '*'
      --format=WORD          across -x, commas -m, horizontal -x, long -l,
                             single-column -1, verbose -l, vertical -C

      --full-time            like -l --time-style=full-iso
  -g                         like -l, but do not list owner
      --group-directories-first
                             group directories before files;
                             can be augmented with a --sort option, but any
                             use of --sort=none (-U) disables grouping

  -G, --no-group             in a long listing, don't print group names
  -h, --human-readable       with -l and -s, print sizes like 1K 234M 2G etc.
      --si                   likewise, but use powers of 1000 not 1024
  -H, --dereference-command-line
                             follow symbolic links listed on the command line
      --dereference-command-line-symlink-to-dir
                             follow each command line symbolic link
                             that points to a directory

      --hide=PATTERN         do not list implied entries matching shell PATTERN
                             (overridden by -a or -A)

      --hyperlink[=WHEN]     hyperlink file names WHEN
      --indicator-style=WORD
                             append indicator with style WORD to entry names:
                             none (default), slash (-p),
                             file-type (--file-type), classify (-F)

  -i, --inode                print the index number of each file
  -I, --ignore=PATTERN       do not list implied entries matching shell PATTERN
  -k, --kibibytes            default to 1024-byte blocks for file system usage;
                             used only with -s and per directory totals

  -l                         use a long listing format
  -L, --dereference          when showing file information for a symbolic
                             link, show information for the file the link
                             references rather than for the link itself

  -m                         fill width with a comma separated list of entries
  -n, --numeric-uid-gid      like -l, but list numeric user and group IDs
  -N, --literal              print entry names without quoting
  -o                         like -l, but do not list group information
  -p, --indicator-style=slash
                             append / indicator to directories
  -q, --hide-control-chars   print ? instead of nongraphic characters
      --show-control-chars   show nongraphic characters as-is (the default,
                             unless program is 'ls' and output is a terminal)

  -Q, --quote-name           enclose entry names in double quotes
      --quoting-style=WORD   use quoting style WORD for entry names:
                             literal, locale, shell, shell-always,
                             shell-escape, shell-escape-always, c, escape
                             (overrides QUOTING_STYLE environment variable)

  -r, --reverse              reverse order while sorting
  -R, --recursive            list subdirectories recursively
  -s, --size                 print the allocated size of each file, in blocks
  -S                         sort by file size, largest first
      --sort=WORD            sort by WORD instead of name: none (-U), size (-S),
                             time (-t), version (-v), extension (-X), width

      --time=WORD            select which timestamp used to display or sort;
                               access time (-u): atime, access, use;
                               metadata change time (-c): ctime, status;
                               modified time (default): mtime, modification;
                               birth time: birth, creation;
                             with -l, WORD determines which time to show;
                             with --sort=time, sort by WORD (newest first)

      --time-style=TIME_STYLE
                             time/date format with -l; see TIME_STYLE below
  -t                         sort by time, newest first; see --time
  -T, --tabsize=COLS         assume tab stops at each COLS instead of 8
  -u                         with -lt: sort by, and show, access time;
                             with -l: show access time and sort by name;
                             otherwise: sort by access time, newest first

  -U                         do not sort; list entries in directory order
  -v                         natural sort of (version) numbers within text
  -w, --width=COLS           set output width to COLS.  0 means no limit
  -x                         list entries by lines instead of by columns
  -X                         sort alphabetically by entry extension
  -Z, --context              print any security context of each file
      --zero                 end each output line with NUL, not newline
  -1                         list one file per line
      --help        display this help and exit
      --version     output version information and exit

The SIZE argument is an integer and optional unit (example: 10K is 10*1024).
Units are K,M,G,T,P,E,Z,Y,R,Q (powers of 1024) or KB,MB,... (powers of 1000).
Binary prefixes can be used, too: KiB=K, MiB=M, and so on.

The TIME_STYLE argument can be full-iso, long-iso, iso, locale, or +FORMAT.
FORMAT is interpreted like in date(1).  If FORMAT is FORMAT1<newline>FORMAT2,
then FORMAT1 applies to non-recent files and FORMAT2 to recent files.
TIME_STYLE prefixed with 'posix-' takes effect only outside the POSIX locale.
Also the TIME_STYLE environment variable sets the default style to use.

The WHEN argument defaults to 'always' and can also be 'auto' or 'never'.

Using color to distinguish file types is disabled both by default and
with --color=never.  With --color=auto, ls emits color codes only when
standard output is connected to a terminal.  The LS_COLORS environment
variable can change the settings.  Use the dircolors(1) command to set it.

Exit status:
 0  if OK,
 1  if minor problems (e.g., cannot access subdirectory),
 2  if serious trouble (e.g., cannot access command-line argument).

"#;

pub fn ls_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let (occs, mut operands) = scan(args, ctx);
    let c = match interpret(&occs, io, ctx) {
        Ok(c) => c,
        Err(Early(status)) => return Ok(status),
    };
    let mut cfg = match resolve(&c, ctx, io) {
        Ok(r) => r,
        Err(status) => return Ok(status),
    };
    if operands.is_empty() {
        operands.push(OsString::from("."));
    }
    // --dired stays a no-op outside long format (ls.c:2372-2374).
    let dired = c.dired && cfg.format == Format::Long && !cfg.with_hyperlink;
    let mut out = Out {
        io,
        pos: 0,
        dired_offsets: Vec::new(),
        subdired_offsets: Vec::new(),
        dired,
        err: false,
    };
    let colors = parse_ls_colors(ctx);
    let mut cs = ColorState { used: false };
    let (success, minor) = process_paths(&mut out, ctx, &c, &mut cfg, &colors, &mut cs, &operands);

    if dired {
        // //DIRED// name ranges, //SUBDIRED// dir headers, options line.
        let mut trailer = String::from("//DIRED//");
        for (b, e) in &out.dired_offsets {
            trailer.push_str(&format!(" {b} {e}"));
        }
        trailer.push('\n');
        if !out.subdired_offsets.is_empty() {
            trailer.push_str("//SUBDIRED//");
            for (b, e) in &out.subdired_offsets {
                trailer.push_str(&format!(" {b} {e}"));
            }
            trailer.push('\n');
        }
        trailer.push_str("//DIRED-OPTIONS// --quoting-style=literal\n");
        out.p(&trailer);
    }

    if out.err {
        // close_stdout write failure -> "ls: write error" + rc 2
        // (ls.c:1661 closeout.c, #1368).
        let _ = writeln!(
            out.io.stderr,
            "ls: {}",
            tr!(ctx, "command.ls.error.write_error", "write error")
        );
        return Ok(2);
    }
    if !success {
        return Ok(2);
    }
    Ok(if minor { 1 } else { 0 })
}
