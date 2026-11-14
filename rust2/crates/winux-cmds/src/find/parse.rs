//! Command-line parsing (findutils 4.9 util.c `process_leading_options`,
//! parser.c parse table, tree.c `build_expression_tree` / `get_expr` /
//! `scan_rest` port). The expression is first collected as GNU's flat
//! predicate list — implicit `-a` interposed by `get_new_pred_chk_op`,
//! global options inserted as always-true no-op primaries, the whole
//! thing wrapped in an artificial `( ... ) -print` when no action
//! suppresses the default print — and then folded into a tree by the same
//! precedence-climbing algorithm, so every GNU diagnostic fires at the
//! same token ("expected an expression after '-o'", "you have too many
//! ')'", "invalid expression; empty parentheses are not allowed.", ...).

use std::io::Write;

use winux_core::Ctx;
use winux_regex::dialect::{self, Dialect, DIALECT_NAMES};

use super::entry::Ts;
use super::exec::{ExecKind, ExecSpec};
use super::expr::{Cmp, Node, NumTest, PermKind, Prim, TField};
use super::printf;
use super::winfs;
use super::{Dest, OutFile};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Follow {
    Never,
    Args,
    Always,
}

/// Global options.
pub struct Options {
    pub follow: Follow,
    pub maxdepth: Option<usize>,
    pub mindepth: usize,
    pub post_order: bool,
    pub explicit_depth: bool,
    pub xdev: bool,
    pub files0_from: Option<String>,
    pub ignore_race: bool,
}

/// What parsing produced.
pub enum Parsed {
    Run {
        opts: Options,
        roots: Vec<String>,
        expr: Node,
        files: Vec<OutFile>,
    },
    /// `--help`, `--version`, `-D help`: text already printed, exit 0.
    Done,
}

/// A parse failure: lines printed verbatim to stderr, exit status 1.
pub struct Fail(pub Vec<String>);

fn fail(msg: impl Into<String>) -> Fail {
    Fail(vec![format!("find: {}", msg.into())])
}

const DAY_NS: i128 = 86_400_000_000_000;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum TT {
    Primary,
    Uni,
    Bi,
    Open,
    Close,
}

struct Tok {
    tt: TT,
    prec: u8,
    name: String,
    artificial: bool,
    prim: Option<Prim>,
    bin: u8, // b'a' / b'o' / b','
    no_default_print: bool,
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum ArgType {
    Option,
    PosOption,
    Other,
}

struct P<'x, 'e> {
    args: &'x [String],
    i: usize,
    toks: Vec<Tok>,
    opts: Options,
    files: Vec<OutFile>,
    first_nonoption: Option<String>,
    warnings: bool,
    dialect: Dialect,
    cur_day_start: i128,
    full_days: bool,
    start: Ts,
    roots: Vec<String>,
    ctx: &'x Ctx<'e>,
    has_delete: bool,
    has_prune: bool,
    pending_warn: Option<String>,
    local_midnight: &'x dyn Fn(i64) -> i64,
}

/// GNU tree.c looks_like_expression.
fn looks_like_expression(a: &str, leading: bool) -> bool {
    let b = a.as_bytes();
    match b.first() {
        Some(b'-') => b.len() > 1,
        Some(b')') | Some(b',') => b.len() == 1 && !leading,
        Some(b'!') | Some(b'(') => b.len() == 1,
        _ => false,
    }
}

pub const DEBUG_HELP: &str = "Valid arguments for -D:
exec       Show diagnostic information relating to -exec, -execdir, -ok and -okdir
opt        Show diagnostic information relating to optimisation
rates      Indicate how often each predicate succeeded
search     Navigate the directory tree verbosely
stat       Trace calls to stat(2) and lstat(2)
time       Show diagnostic information relating to time-of-day and timestamp comparisons
tree       Display the expression tree
all        Set all of the debug flags (but help)
help       Explain the various -D options
";

/// Parse the whole command line.
pub fn parse<'x>(
    args: &'x [String],
    ctx: &'x Ctx,
    start: Ts,
    out: &mut dyn Write,
    err: &mut dyn Write,
    local_midnight: &'x dyn Fn(i64) -> i64,
) -> Result<Parsed, Fail> {
    let mut opts = Options {
        follow: Follow::Never,
        maxdepth: None,
        mindepth: 0,
        post_order: false,
        explicit_depth: false,
        xdev: false,
        files0_from: None,
        ignore_race: false,
    };
    // ---- leading options (util.c process_leading_options) ----
    let mut i = 0;
    while i < args.len() {
        let a = args[i].as_str();
        match a {
            "-H" => opts.follow = Follow::Args,
            "-L" => opts.follow = Follow::Always,
            "-P" => opts.follow = Follow::Never,
            "--" => {
                i += 1;
                break;
            }
            "-D" => {
                if i + 1 >= args.len() {
                    return Err(Fail(vec![
                        "find: Missing argument after the -D option.".to_string(),
                        "Try 'find --help' for more information.".to_string(),
                    ]));
                }
                if debug_options(&args[i + 1], out, err) {
                    return Ok(Parsed::Done);
                }
                i += 1;
            }
            _ if a.starts_with("-O") => {
                let lvl = &a[2..];
                if lvl.is_empty() {
                    return Err(fail(
                        "The -O option must be immediately followed by a decimal integer",
                    ));
                }
                if !lvl.bytes().all(|b| b.is_ascii_digit()) {
                    return Err(fail("Please specify a decimal number immediately after -O"));
                }
            }
            _ => break,
        }
        i += 1;
    }
    // ---- starting points ----
    let mut roots = Vec::new();
    while i < args.len() && !looks_like_expression(&args[i], true) {
        roots.push(args[i].clone());
        i += 1;
    }
    let mut p = P {
        args,
        i,
        toks: Vec::new(),
        opts,
        files: Vec::new(),
        first_nonoption: None,
        warnings: false,
        dialect: Dialect::Emacs,
        cur_day_start: start.ns() - DAY_NS,
        full_days: false,
        start,
        roots,
        ctx,
        has_delete: false,
        has_prune: false,
        pending_warn: None,
        local_midnight,
    };
    // artificial "(" wrapping the user expression
    p.toks.push(Tok {
        tt: TT::Open,
        prec: 0,
        name: "(".into(),
        artificial: true,
        prim: None,
        bin: 0,
        no_default_print: false,
    });
    while p.i < args.len() {
        let a = args[p.i].clone();
        if !looks_like_expression(&a, false) {
            let mut lines = vec![format!("find: paths must precede expression: `{a}'")];
            let path = crate::fs_win::imp::api_path(ctx, &a);
            if winfs::attributes(&path.encode_utf16().collect::<Vec<_>>()).is_some() {
                let last = p.toks.last().map(|t| t.name.clone()).unwrap_or_default();
                lines.push(format!(
                    "find: possible unquoted pattern after predicate `{last}'?"
                ));
            }
            return Err(Fail(lines));
        }
        p.i += 1;
        if p.one(&a, out, err)? {
            return Ok(Parsed::Done);
        }
    }
    // ---- default print wrapping (tree.c) ----
    let user = p.toks.len() - 1;
    if user == 0 {
        p.toks.clear();
        p.push_prim(
            Prim::Print {
                dest: Dest::Stdout,
                nul: false,
            },
            "-print",
            true,
        );
    } else if p.toks.iter().any(|t| t.no_default_print) {
        p.toks.remove(0);
    } else {
        p.toks.push(Tok {
            tt: TT::Close,
            prec: 0,
            name: ")".into(),
            artificial: true,
            prim: None,
            bin: 0,
            no_default_print: false,
        });
        p.push_prim(
            Prim::Print {
                dest: Dest::Stdout,
                nul: false,
            },
            "-print",
            true,
        );
        p.toks.last_mut().unwrap().artificial = true;
    }
    if p.has_delete && p.has_prune && !p.opts.explicit_depth {
        return Err(fail(
            "The -delete action automatically turns on -depth, but -prune does nothing when -depth is in effect.  If you want to carry on anyway, just explicitly use the -depth option.",
        ));
    }
    let mut toks: Vec<Tok> = std::mem::take(&mut p.toks);
    let mut idx = 0usize;
    let tree = get_expr(&mut toks, &mut idx, 0, None)?;
    if idx < toks.len() {
        if toks[idx].tt == TT::Close {
            return Err(fail("you have too many ')'"));
        }
        return Err(fail(format!(
            "unexpected extra predicate '{}'",
            toks[idx].name
        )));
    }
    if p.opts.files0_from.is_some() && !p.roots.is_empty() {
        return Err(Fail(vec![
            format!("find: extra operand '{}'", p.roots[0]),
            "find: file operands cannot be combined with -files0-from".to_string(),
        ]));
    }
    Ok(Parsed::Run {
        opts: p.opts,
        roots: p.roots,
        expr: tree,
        files: p.files,
    })
}

/// `-D` list. Returns true when `help` asked to stop.
fn debug_options(list: &str, out: &mut dyn Write, err: &mut dyn Write) -> bool {
    let mut help = false;
    for t in list.split(',') {
        match t {
            "exec" | "opt" | "rates" | "search" | "stat" | "time" | "tree" | "all" => {}
            "help" => help = true,
            "" => {}
            other => {
                let _ = writeln!(err, "find: Ignoring unrecognised debug flag '{other}'");
            }
        }
    }
    if help {
        let _ = out.write_all(DEBUG_HELP.as_bytes());
    }
    help
}

/// tree.c get_expr.
fn get_expr(
    toks: &mut [Tok],
    idx: &mut usize,
    prev_prec: u8,
    prev: Option<usize>,
) -> Result<Node, Fail> {
    if *idx >= toks.len() {
        return Err(fail("invalid expression"));
    }
    let this = *idx;
    let node = match toks[this].tt {
        TT::Bi => {
            return Err(fail(format!(
                "invalid expression; you have used a binary operator '{}' with nothing before it.",
                toks[this].name
            )))
        }
        TT::Close => {
            let pv = match prev {
                None => {
                    return Err(fail(format!(
                        "invalid expression: expected expression before closing parentheses '{}'.",
                        toks[this].name
                    )))
                }
                Some(pv) => pv,
            };
            if matches!(toks[pv].tt, TT::Uni | TT::Bi) && !toks[this].artificial {
                return Err(fail(format!(
                    "expected an expression between '{}' and ')'",
                    toks[pv].name
                )));
            } else if toks[this].artificial {
                return Err(fail(format!(
                    "expected an expression after '{}'",
                    toks[pv].name
                )));
            } else {
                return Err(fail("invalid expression; you have too many ')'"));
            }
        }
        TT::Primary => {
            *idx += 1;
            Node::P(toks[this].prim.take().unwrap())
        }
        TT::Uni => {
            *idx += 1;
            let child = get_expr(toks, idx, 4, Some(this))?;
            Node::Not(Box::new(child))
        }
        TT::Open => {
            if this + 1 >= toks.len() || toks[this + 1].artificial {
                return Err(fail(format!(
                    "invalid expression; expected to find a ')' but didn't see one. Perhaps you need an extra predicate after '{}'",
                    toks[this].name
                )));
            }
            *idx += 1;
            if toks[*idx].tt == TT::Close {
                return Err(fail(
                    "invalid expression; empty parentheses are not allowed.",
                ));
            }
            let inner = get_expr(toks, idx, 0, Some(this))?;
            if *idx >= toks.len() || toks[*idx].tt != TT::Close {
                return Err(fail(
                    "invalid expression; I was expecting to find a ')' somewhere but did not see one.",
                ));
            }
            *idx += 1;
            inner
        }
    };
    if *idx >= toks.len() {
        return Ok(node);
    }
    if toks[*idx].prec > prev_prec {
        return scan_rest(toks, idx, node, prev_prec);
    }
    Ok(node)
}

/// tree.c scan_rest.
fn scan_rest(toks: &mut [Tok], idx: &mut usize, head: Node, prev_prec: u8) -> Result<Node, Fail> {
    if *idx >= toks.len() || toks[*idx].tt == TT::Close {
        return Err(fail("invalid expression"));
    }
    let mut tree = head;
    while *idx < toks.len() && toks[*idx].prec > prev_prec {
        match toks[*idx].tt {
            TT::Bi => {
                let op = *idx;
                *idx += 1;
                let right = get_expr(toks, idx, toks[op].prec, Some(op))?;
                tree = match toks[op].bin {
                    b'a' => Node::And(Box::new(tree), Box::new(right)),
                    b'o' => Node::Or(Box::new(tree), Box::new(right)),
                    _ => Node::Comma(Box::new(tree), Box::new(right)),
                };
            }
            TT::Close => return Ok(tree),
            _ => return Err(fail("invalid expression")),
        }
    }
    Ok(tree)
}

fn digits_only(s: &str) -> bool {
    !s.is_empty() && s.bytes().all(|b| b.is_ascii_digit())
}

/// get_num: optional +/- then an unsigned decimal.
fn get_num(s: &str) -> Option<NumTest> {
    let (cmp, rest) = match s.as_bytes().first() {
        Some(b'+') => (Cmp::Gt, &s[1..]),
        Some(b'-') => (Cmp::Lt, &s[1..]),
        _ => (Cmp::Eq, s),
    };
    let rest = rest.trim_start();
    if !digits_only(rest) {
        return None;
    }
    rest.parse::<u64>().ok().map(|n| NumTest { cmp, n })
}

impl<'x, 'e> P<'x, 'e> {
    fn chk_op(&mut self) {
        if let Some(last) = self.toks.last() {
            if matches!(last.tt, TT::Primary | TT::Close) {
                self.toks.push(Tok {
                    tt: TT::Bi,
                    prec: 3,
                    name: "-a".into(),
                    artificial: false,
                    prim: None,
                    bin: b'a',
                    no_default_print: false,
                });
            }
        }
    }

    fn push_prim(&mut self, prim: Prim, name: &str, no_default_print: bool) {
        self.chk_op();
        self.toks.push(Tok {
            tt: TT::Primary,
            prec: 0,
            name: name.to_string(),
            artificial: false,
            prim: Some(prim),
            bin: 0,
            no_default_print,
        });
    }

    fn push_op(&mut self, tt: TT, prec: u8, name: &str, bin: u8) {
        if matches!(tt, TT::Uni | TT::Open) {
            self.chk_op();
        }
        self.toks.push(Tok {
            tt,
            prec,
            name: name.to_string(),
            artificial: false,
            prim: None,
            bin,
            no_default_print: false,
        });
    }

    fn arg(&mut self, pred: &str) -> Result<String, Fail> {
        match self.args.get(self.i) {
            Some(a) => {
                self.i += 1;
                Ok(a.clone())
            }
            None => Err(fail(format!("missing argument to `{pred}'"))),
        }
    }

    fn invalid(&self, a: &str, pred: &str) -> Fail {
        fail(format!("invalid argument `{a}' to `{pred}'"))
    }

    fn found(&mut self, name: &str, ty: ArgType) {
        match ty {
            ArgType::PosOption => {}
            ArgType::Option => {
                if let Some(first) = &self.first_nonoption {
                    if self.warnings {
                        self.pending_warn = Some(format!(
                            "find: warning: you have specified the global option {name} after the argument {first}, but global options are not positional, i.e., {name} affects tests specified before it as well as those specified after it.  Please specify global options before other arguments."
                        ));
                    }
                }
            }
            ArgType::Other => {
                if self.first_nonoption.is_none() {
                    self.first_nonoption = Some(name.to_string());
                }
            }
        }
    }

    fn noop(&mut self) {
        self.push_prim(Prim::True, "--noop", false);
    }

    /// Reference time of a file for -newer & co (stat per follow mode).
    fn ref_stat(&self, f: &str) -> Result<winfs::HandleInfo, Fail> {
        let native: Vec<u16> = crate::fs_win::imp::api_path(self.ctx, f)
            .encode_utf16()
            .collect();
        let follow = self.opts.follow != Follow::Never;
        let r = winfs::handle_info(&native, follow).or_else(|e| {
            if follow {
                winfs::handle_info(&native, false)
            } else {
                Err(e)
            }
        });
        r.map_err(|e| fail(format!("'{f}': {}", winfs::reason(e))))
    }

    fn open_file(&mut self, f: &str) -> Result<Dest, Fail> {
        if f == "/dev/stdout" {
            return Ok(Dest::Stdout);
        }
        if f == "/dev/stderr" {
            return Ok(Dest::Stderr);
        }
        if let Some(i) = self.files.iter().position(|o| o.name == f) {
            return Ok(Dest::File(i));
        }
        let path = crate::fs_win::imp::api_path(self.ctx, f);
        match std::fs::File::create(&path) {
            Ok(file) => {
                self.files.push(OutFile {
                    name: f.to_string(),
                    w: std::io::BufWriter::new(file),
                });
                Ok(Dest::File(self.files.len() - 1))
            }
            Err(e) => Err(fail(format!("'{f}': {}", crate::fsutil::reason(&e)))),
        }
    }

    fn time_test(
        &mut self,
        pred: &str,
        field: TField,
        unit_secs: i128,
        minutes: bool,
    ) -> Result<(), Fail> {
        let a = self.arg(pred)?;
        let (user_cmp, num) = match a.as_bytes().first() {
            Some(b'+') => (Cmp::Gt, &a[1..]),
            Some(b'-') => (Cmp::Lt, &a[1..]),
            _ => (Cmp::Eq, &a[..]),
        };
        let v: f64 = match parse_strtod(num) {
            Some(v) => v,
            None => return Err(self.invalid(&a, pred)),
        };
        let mut origin = self.cur_day_start;
        if minutes {
            origin += DAY_NS;
        } else if user_cmp == Cmp::Lt {
            origin += DAY_NS - 1_000_000_000;
        }
        let reft = origin - (v * unit_secs as f64 * 1e9) as i128;
        let cmp = invert(user_cmp);
        let window = unit_secs * 1_000_000_000;
        self.found(pred, ArgType::Other);
        self.push_prim(
            Prim::Time {
                field,
                cmp,
                reft,
                window,
            },
            pred,
            false,
        );
        Ok(())
    }

    /// Handle one predicate token (already consumed). Returns Ok(true)
    /// when help/version text ended the run.
    fn one(&mut self, a: &str, out: &mut dyn Write, err: &mut dyn Write) -> Result<bool, Fail> {
        self.pending_warn = None;
        let r = self.one_inner(a, out, err);
        if let Some(w) = self.pending_warn.take() {
            let _ = writeln!(err, "{w}");
        }
        r
    }
}

/// strtod-style decimal (C locale): sign, digits, optional fraction and
/// exponent; the whole string must be consumed.
fn parse_strtod(s: &str) -> Option<f64> {
    let t = s.trim_start();
    if t.is_empty() {
        return None;
    }
    let ok = t
        .bytes()
        .all(|b| b.is_ascii_digit() || matches!(b, b'.' | b'e' | b'E' | b'+' | b'-'));
    if !ok || !t.bytes().any(|b| b.is_ascii_digit()) {
        return None;
    }
    t.parse::<f64>().ok()
}

fn invert(c: Cmp) -> Cmp {
    match c {
        Cmp::Gt => Cmp::Lt,
        Cmp::Lt => Cmp::Gt,
        Cmp::Eq => Cmp::Eq,
    }
}

fn type_index(c: u8) -> Option<usize> {
    b"bcdpflsD".iter().position(|&x| x == c)
}

impl<'x, 'e> P<'x, 'e> {
    fn one_inner(
        &mut self,
        a: &str,
        out: &mut dyn Write,
        err: &mut dyn Write,
    ) -> Result<bool, Fail> {
        // -newerXY special parse
        if a.len() == 8 && a.starts_with("-newer") {
            self.found(a, ArgType::Other);
            let x = a.as_bytes()[6];
            let y = a.as_bytes()[7];
            if x == b't' || !b"aBcm".contains(&x) || !b"aBcmt".contains(&y) {
                return Err(fail(format!("invalid predicate `{a}'")));
            }
            let v = match self.args.get(self.i) {
                Some(v) => v.clone(),
                None => return Err(fail(format!("The '{a}' test needs an argument"))),
            };
            self.i += 1;
            let field = match x {
                b'a' => TField::A,
                b'B' => TField::B,
                b'c' => TField::C,
                _ => TField::M,
            };
            let t = if y == b't' {
                let base = crate::fs_win::FileTime::from_epoch(self.start.sec, self.start.nsec);
                match crate::touch::parse_datetime(&v, base) {
                    Some(ft) => Ts::from_ticks(ft.as_u64() as i64).ns(),
                    None => {
                        return Err(fail(format!(
                            "I cannot figure out how to interpret '{v}' as a date or time"
                        )))
                    }
                }
            } else {
                let h = self.ref_stat(&v)?;
                Ts::from_ticks(match y {
                    b'a' => h.atime,
                    b'B' => h.btime,
                    b'c' => h.ctime,
                    _ => h.mtime,
                })
                .ns()
            };
            self.push_prim(Prim::Newer { field, t }, a, false);
            return Ok(false);
        }
        let name = a.strip_prefix('-').unwrap_or(a);
        match name {
            "!" | "not" => {
                self.found(a, ArgType::Other);
                self.push_op(TT::Uni, 4, a, 0);
            }
            "(" => {
                self.found(a, ArgType::Other);
                self.push_op(TT::Open, 0, a, 0);
            }
            ")" => {
                self.found(a, ArgType::Other);
                self.push_op(TT::Close, 0, a, 0);
            }
            "," => {
                self.found(a, ArgType::Other);
                self.push_op(TT::Bi, 1, a, b',');
            }
            "a" | "and" => {
                self.found(a, ArgType::Other);
                self.push_op(TT::Bi, 3, a, b'a');
            }
            "o" | "or" => {
                self.found(a, ArgType::Other);
                self.push_op(TT::Bi, 2, a, b'o');
            }
            "help" | "-help" => {
                let _ = out.write_all(super::HELP.as_bytes());
                return Ok(true);
            }
            "version" | "-version" => {
                let _ = out.write_all(super::VERSION.as_bytes());
                return Ok(true);
            }
            // ---- global options ----
            "d" | "depth" => {
                self.found(a, ArgType::Option);
                if name == "d" && self.warnings {
                    let _ = writeln!(
                        err,
                        "find: warning: the -d option is deprecated; please use -depth instead, because the latter is a POSIX-compliant feature."
                    );
                }
                self.opts.post_order = true;
                self.opts.explicit_depth = true;
                self.noop();
            }
            "files0-from" => {
                self.found(a, ArgType::Option);
                let f = self.arg(a)?;
                self.opts.files0_from = Some(f);
                self.noop();
            }
            "ignore_readdir_race" | "noignore_readdir_race" => {
                self.found(a, ArgType::Option);
                self.opts.ignore_race = name == "ignore_readdir_race";
                self.noop();
            }
            "maxdepth" | "mindepth" => {
                self.found(a, ArgType::Option);
                let v = self.arg(a)?;
                if !digits_only(&v) {
                    return Err(fail(format!(
                        "Expected a positive decimal integer argument to {a}, but got '{v}'"
                    )));
                }
                let n: usize = v.parse().unwrap_or(usize::MAX);
                if name == "maxdepth" {
                    self.opts.maxdepth = Some(n);
                } else {
                    self.opts.mindepth = n;
                }
                self.noop();
            }
            "mount" | "xdev" => {
                self.found(a, ArgType::Option);
                self.opts.xdev = true;
                self.noop();
            }
            "noleaf" => {
                self.found(a, ArgType::Option);
                self.noop();
            }
            // ---- positional options ----
            "daystart" => {
                self.found(a, ArgType::PosOption);
                if !self.full_days {
                    let t = self.cur_day_start + DAY_NS;
                    let sec = t.div_euclid(1_000_000_000) as i64;
                    let midnight = (self.local_midnight)(sec);
                    self.cur_day_start = midnight as i128 * 1_000_000_000;
                    self.full_days = true;
                }
                self.noop();
            }
            "follow" => {
                self.found(a, ArgType::PosOption);
                self.opts.follow = Follow::Always;
                self.noop();
            }
            "warn" | "nowarn" => {
                self.found(a, ArgType::PosOption);
                self.warnings = name == "warn";
                self.noop();
            }
            "regextype" => {
                self.found(a, ArgType::PosOption);
                let t = self.arg(a)?;
                match Dialect::from_name(&t) {
                    Some(d) => self.dialect = d,
                    None => {
                        let list: Vec<String> = DIALECT_NAMES
                            .iter()
                            .map(|(n, _)| format!("'{n}'"))
                            .collect();
                        return Err(fail(format!(
                            "Unknown regular expression type '{t}'; valid types are {}.",
                            list.join(", ")
                        )));
                    }
                }
                self.noop();
            }
            // ---- tests ----
            "true" | "false" => {
                self.found(a, ArgType::Other);
                self.push_prim(
                    if name == "true" {
                        Prim::True
                    } else {
                        Prim::False
                    },
                    a,
                    false,
                );
            }
            "name" | "iname" => {
                self.found(a, ArgType::Other);
                let pat = self.arg(a)?;
                if self.warnings && pat.contains('/') {
                    let alt = if name == "name" {
                        "-wholename"
                    } else {
                        "-iwholename"
                    };
                    let _ = writeln!(
                        err,
                        "find: warning: '{a}' matches against basenames only, but the given pattern contains a directory separator ('/'), thus the expression will evaluate to false all the time.  Did you mean '{alt}'?"
                    );
                }
                self.push_prim(
                    Prim::Name {
                        pat: pat.into_bytes(),
                        icase: name == "iname",
                    },
                    a,
                    false,
                );
            }
            "path" | "wholename" | "ipath" | "iwholename" => {
                self.found(a, ArgType::Other);
                let pat = self.arg(a)?;
                let icase = name.starts_with('i');
                if pat.ends_with('/')
                    && !self
                        .roots
                        .iter()
                        .any(|r| super::fnm::fnmatch(pat.as_bytes(), r.as_bytes(), icase))
                {
                    let _ = writeln!(
                        err,
                        "find: warning: -{name} {pat} will not match anything because it ends with /."
                    );
                }
                self.push_prim(
                    Prim::Path {
                        pat: pat.into_bytes(),
                        icase,
                    },
                    a,
                    false,
                );
            }
            "lname" | "ilname" => {
                self.found(a, ArgType::Other);
                let pat = self.arg(a)?;
                self.push_prim(
                    Prim::Lname {
                        pat: pat.into_bytes(),
                        icase: name == "ilname",
                    },
                    a,
                    false,
                );
            }
            "regex" | "iregex" => {
                self.found(a, ArgType::Other);
                let rx = self.arg(a)?;
                match dialect::compile(self.dialect, rx.as_bytes(), name == "iregex") {
                    Ok(re) => self.push_prim(Prim::Regex(Box::new(re)), a, false),
                    Err(e) => {
                        return Err(fail(format!(
                            "failed to compile regular expression '{rx}': {}",
                            e.glibc_message()
                        )))
                    }
                }
            }
            "type" | "xtype" => {
                self.found(a, ArgType::Other);
                let v = self.arg(a)?;
                let pred = if name == "xtype" { "-xtype" } else { "-type" };
                if v.is_empty() {
                    return Err(fail(format!(
                        "Arguments to {pred} should contain at least one letter"
                    )));
                }
                let mut set = [false; 8];
                let b = v.as_bytes();
                let mut k = 0;
                while k < b.len() {
                    let c = b[k];
                    let idx = match type_index(c) {
                        Some(7) => {
                            return Err(fail(format!(
                                "{pred} {} is not supported because Solaris doors are not supported on the platform find was compiled on.",
                                c as char
                            )))
                        }
                        Some(ix) => ix,
                        None => return Err(fail(format!("Unknown argument to {pred}: {}", c as char))),
                    };
                    if set[idx] {
                        return Err(fail(format!(
                            "Duplicate file type '{}' in the argument list to {pred}.",
                            c as char
                        )));
                    }
                    set[idx] = true;
                    k += 1;
                    if k < b.len() {
                        if b[k] != b',' {
                            return Err(fail(format!(
                                "Must separate multiple arguments to {pred} using: ','"
                            )));
                        }
                        k += 1;
                        if k >= b.len() {
                            return Err(fail(format!(
                                "Last file type in list argument to {pred} is missing, i.e., list is ending on: ','"
                            )));
                        }
                    }
                }
                self.push_prim(
                    Prim::Type {
                        set,
                        x: name == "xtype",
                    },
                    a,
                    false,
                );
            }
            "size" => {
                self.found(a, ArgType::Other);
                let v = self.arg(a)?;
                if v.is_empty() {
                    return Err(fail("invalid null argument to -size"));
                }
                let last = *v.as_bytes().last().unwrap();
                let (unit, body) = match last {
                    b'b' => (512u64, &v[..v.len() - 1]),
                    b'c' => (1, &v[..v.len() - 1]),
                    b'k' => (1024, &v[..v.len() - 1]),
                    b'M' => (1 << 20, &v[..v.len() - 1]),
                    b'G' => (1 << 30, &v[..v.len() - 1]),
                    b'w' => (2, &v[..v.len() - 1]),
                    b'0'..=b'9' => (512, &v[..]),
                    _ => return Err(fail(format!("invalid -size type `{}'", last as char))),
                };
                let nt = match get_num(body) {
                    Some(n) => n,
                    None => {
                        let tail = if last.is_ascii_digit() {
                            String::new()
                        } else {
                            (last as char).to_string()
                        };
                        return Err(fail(format!("Invalid argument `{body}{tail}' to -size")));
                    }
                };
                self.push_prim(Prim::Size { t: nt, unit }, a, false);
            }
            "empty" => {
                self.found(a, ArgType::Other);
                self.push_prim(Prim::Empty, a, false);
            }
            "mtime" | "atime" | "ctime" => {
                let f = match name {
                    "mtime" => TField::M,
                    "atime" => TField::A,
                    _ => TField::C,
                };
                self.time_test(a, f, 86_400, false)?;
            }
            "mmin" | "amin" | "cmin" => {
                let f = match name {
                    "mmin" => TField::M,
                    "amin" => TField::A,
                    _ => TField::C,
                };
                self.time_test(a, f, 60, true)?;
            }
            "used" => {
                self.found(a, ArgType::Other);
                let v = self.arg(a)?;
                let (user_cmp, num) = match v.as_bytes().first() {
                    Some(b'+') => (Cmp::Gt, &v[1..]),
                    Some(b'-') => (Cmp::Lt, &v[1..]),
                    _ => (Cmp::Eq, &v[..]),
                };
                let n = match parse_strtod(num) {
                    Some(n) => n,
                    None => return Err(fail(format!("Invalid argument {v} to -used"))),
                };
                let reft = -((n * 86_400.0 * 1e9) as i128);
                self.push_prim(
                    Prim::Used {
                        cmp: invert(user_cmp),
                        reft,
                    },
                    a,
                    false,
                );
            }
            "newer" | "anewer" | "cnewer" => {
                self.found(a, ArgType::Other);
                let f = self.arg(a)?;
                let h = self.ref_stat(&f)?;
                let field = match name {
                    "newer" => TField::M,
                    "anewer" => TField::A,
                    _ => TField::C,
                };
                self.push_prim(
                    Prim::Newer {
                        field,
                        t: Ts::from_ticks(h.mtime).ns(),
                    },
                    a,
                    false,
                );
            }
            "perm" => {
                self.found(a, ArgType::Other);
                let v = self.arg(a)?;
                let (kind, body) = match v.as_bytes().first() {
                    Some(b'-') => (PermKind::All, &v[1..]),
                    Some(b'/') => (PermKind::Any, &v[1..]),
                    _ => (PermKind::Exact, &v[..]),
                };
                let bad = || fail(format!("invalid mode '{v}'"));
                if v.starts_with('+')
                    && v.as_bytes()
                        .get(1)
                        .is_some_and(|c| (b'0'..b'8').contains(c))
                {
                    return Err(bad());
                }
                let spec = crate::modemap::parse_mode_spec(body).map_err(|_| bad())?;
                let val = [
                    crate::modemap::eval_spec(0, false, &spec, 0),
                    crate::modemap::eval_spec(0, true, &spec, 0),
                ];
                let mut kind = kind;
                if v.starts_with('/') && val == [0, 0] {
                    let _ = writeln!(
                        err,
                        "find: warning: you have specified a mode pattern {v} (which is equivalent to /000). The meaning of -perm /000 has now been changed to be consistent with -perm -000; that is, while it used to match no files, it now matches all files."
                    );
                    kind = PermKind::All;
                }
                self.push_prim(Prim::Perm { kind, val }, a, false);
            }
            "uid" | "gid" | "links" | "inum" => {
                self.found(a, ArgType::Other);
                let v = self.arg(a)?;
                let nt = get_num(&v).ok_or_else(|| self.invalid(&v, a))?;
                let prim = match name {
                    "uid" => Prim::Uid(nt),
                    "gid" => Prim::Gid(nt),
                    "links" => Prim::Links(nt),
                    _ => Prim::Inum(nt),
                };
                self.push_prim(prim, a, false);
            }
            "user" | "group" => {
                self.found(a, ArgType::Other);
                let v = self.arg(a)?;
                let id = if v.is_empty() {
                    return Err(fail(if name == "user" {
                        "The argument to -user should not be empty"
                    } else {
                        "argument to -group is empty, but should be a group name"
                    }));
                } else if let Some(id) =
                    crate::winsys::imp::account_id_from_name(&v).and_then(|s| s.parse::<u64>().ok())
                {
                    id
                } else {
                    let nd = v.bytes().take_while(|b| b.is_ascii_digit()).count();
                    if nd > 0 && nd == v.len() {
                        v.parse::<u64>().unwrap_or(u64::MAX)
                    } else if name == "user" {
                        if v.is_empty() {
                            return Err(fail("The argument to -user should not be empty"));
                        }
                        return Err(fail(format!("'{v}' is not the name of a known user")));
                    } else if v.is_empty() {
                        return Err(fail(
                            "argument to -group is empty, but should be a group name",
                        ));
                    } else if nd > 0 {
                        return Err(fail(format!(
                            "'{v}' is not the name of an existing group and it does not look like a numeric group ID because it has the unexpected suffix '{}'",
                            &v[nd..]
                        )));
                    } else {
                        return Err(fail(format!("'{v}' is not the name of an existing group")));
                    }
                };
                self.push_prim(
                    if name == "user" {
                        Prim::Uid(NumTest {
                            cmp: Cmp::Eq,
                            n: id,
                        })
                    } else {
                        Prim::Gid(NumTest {
                            cmp: Cmp::Eq,
                            n: id,
                        })
                    },
                    a,
                    false,
                );
            }
            "nouser" | "nogroup" => {
                self.found(a, ArgType::Other);
                self.push_prim(
                    if name == "nouser" {
                        Prim::NoUser
                    } else {
                        Prim::NoGroup
                    },
                    a,
                    false,
                );
            }
            "readable" | "writable" | "executable" => {
                self.found(a, ArgType::Other);
                let p = match name {
                    "readable" => Prim::Readable,
                    "writable" => Prim::Writable,
                    _ => Prim::Executable,
                };
                self.push_prim(p, a, false);
            }
            "samefile" => {
                self.found(a, ArgType::Other);
                let f = self.arg(a)?;
                let h = self.ref_stat(&f)?;
                self.push_prim(
                    Prim::SameFile {
                        dev: h.dev,
                        ino: h.ino,
                    },
                    a,
                    false,
                );
            }
            "fstype" => {
                self.found(a, ArgType::Other);
                let t = self.arg(a)?;
                self.push_prim(Prim::Fstype(t), a, false);
            }
            "context" => {
                self.found(a, ArgType::Other);
                let _ = self.arg(a)?;
                return Err(fail("invalid predicate -context: SELinux is not enabled."));
            }
            // ---- actions ----
            "print" | "print0" => {
                self.found(a, ArgType::Other);
                self.push_prim(
                    Prim::Print {
                        dest: Dest::Stdout,
                        nul: name == "print0",
                    },
                    a,
                    true,
                );
            }
            "fprint" | "fprint0" => {
                self.found(a, ArgType::Other);
                let f = self.arg(a)?;
                let dest = self.open_file(&f)?;
                self.push_prim(
                    Prim::Print {
                        dest,
                        nul: name == "fprint0",
                    },
                    a,
                    true,
                );
            }
            "printf" | "fprintf" => {
                self.found(a, ArgType::Other);
                let dest = if name == "fprintf" {
                    let f = self.arg(a)?;
                    if self.i >= self.args.len() {
                        return Err(fail(format!("missing argument to `{a}'")));
                    }
                    self.open_file(&f)?
                } else {
                    Dest::Stdout
                };
                let f = self.arg(a)?;
                let segs = printf::compile(f.as_bytes(), err).map_err(fail)?;
                self.push_prim(Prim::Printf { dest, segs }, a, true);
            }
            "ls" | "fls" => {
                self.found(a, ArgType::Other);
                let dest = if name == "fls" {
                    let f = self.arg(a)?;
                    self.open_file(&f)?
                } else {
                    Dest::Stdout
                };
                self.push_prim(Prim::Ls { dest }, a, true);
            }
            "prune" => {
                self.found(a, ArgType::Other);
                self.has_prune = true;
                self.push_prim(Prim::Prune, a, false);
            }
            "quit" => {
                self.found(a, ArgType::Other);
                self.push_prim(Prim::Quit, a, false);
            }
            "delete" => {
                self.found(a, ArgType::Other);
                self.has_delete = true;
                self.opts.post_order = true;
                self.push_prim(Prim::Delete, a, true);
            }
            "exec" | "execdir" | "ok" | "okdir" => {
                self.found(a, ArgType::Other);
                let kind = match name {
                    "exec" => ExecKind::Exec,
                    "execdir" => ExecKind::ExecDir,
                    "ok" => ExecKind::Ok,
                    _ => ExecKind::OkDir,
                };
                let allow_plus = !kind.asks();
                let start = self.i;
                let mut end = start;
                let mut saw_braces = false;
                let mut brace_count = 0;
                let mut brace_arg = String::new();
                let mut multi = false;
                while end < self.args.len() && self.args[end] != ";" {
                    let s = &self.args[end];
                    if allow_plus && s == "+" && saw_braces {
                        multi = true;
                        break;
                    }
                    saw_braces = false;
                    if s.contains("{}") {
                        saw_braces = true;
                        brace_count += 1;
                        brace_arg = s.clone();
                        // findutils 4.9 guards `{}` in the -execdir/-okdir
                        // utility name with `0 == end` (an absolute argv
                        // index), so the check never fires from a real
                        // command line: GNU runs `./<name>` (probed
                        // `find . -execdir {} \;` -> "'./.': Permission
                        // denied", rc 0). Kept GNU-identical.
                    }
                    end += 1;
                }
                if end == start || end >= self.args.len() {
                    return Err(fail(format!("missing argument to `{a}'")));
                }
                if multi {
                    let suffix = if kind == ExecKind::ExecDir { "dir" } else { "" };
                    if brace_count > 1 {
                        return Err(fail(format!(
                            "Only one instance of {{}} is supported with -exec{suffix} ... +"
                        )));
                    }
                    if brace_arg != "{}" {
                        return Err(fail(format!(
                            "In '-exec{suffix} ... {{}} +' the '{{}}' must appear by itself, but you specified '{brace_arg}'"
                        )));
                    }
                }
                let argv: Vec<String> = if multi {
                    self.args[start..end - 1].to_vec()
                } else {
                    self.args[start..end].to_vec()
                };
                self.i = end + 1;
                self.push_prim(
                    Prim::Exec(Box::new(ExecSpec::new(kind, argv, multi))),
                    a,
                    true,
                );
            }
            _ => return Err(fail(format!("unknown predicate `{a}'"))),
        }
        Ok(false)
    }
}
