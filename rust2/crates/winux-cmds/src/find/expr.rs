//! Expression tree and predicate evaluation (findutils pred.c port).

use winux_regex::Regex;

use super::entry::{Entry, Kind, Ts};
use super::exec::ExecSpec;
use super::fnm::fnmatch;
use super::printf::{self, base_name, Seg};
use super::winfs;
use super::{Dest, Run};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Cmp {
    Gt,
    Lt,
    Eq,
}

#[derive(Clone, Copy, Debug)]
pub struct NumTest {
    pub cmp: Cmp,
    pub n: u64,
}

impl NumTest {
    pub fn test(&self, v: u64) -> bool {
        match self.cmp {
            Cmp::Gt => v > self.n,
            Cmp::Lt => v < self.n,
            Cmp::Eq => v == self.n,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TField {
    A,
    B,
    C,
    M,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PermKind {
    Exact,
    All,
    Any,
}

pub enum Prim {
    True,
    False,
    Name {
        pat: Vec<u8>,
        icase: bool,
    },
    Path {
        pat: Vec<u8>,
        icase: bool,
    },
    Lname {
        pat: Vec<u8>,
        icase: bool,
    },
    Regex(Box<Regex>),
    /// letters `bcdpflsD` as a set; `x` = -xtype
    Type {
        set: [bool; 8],
        x: bool,
    },
    Size {
        t: NumTest,
        unit: u64,
    },
    Empty,
    /// -mtime/-mmin family: pred_timewindow against `reft` (ns).
    Time {
        field: TField,
        cmp: Cmp,
        reft: i128,
        window: i128,
    },
    /// -newer family: file time strictly after `t` (ns).
    Newer {
        field: TField,
        t: i128,
    },
    Used {
        cmp: Cmp,
        reft: i128,
    },
    Perm {
        kind: PermKind,
        val: [u32; 2],
    },
    Uid(NumTest),
    Gid(NumTest),
    NoUser,
    NoGroup,
    Readable,
    Writable,
    Executable,
    Links(NumTest),
    Inum(NumTest),
    SameFile {
        dev: u32,
        ino: u64,
    },
    Fstype(String),
    Print {
        dest: Dest,
        nul: bool,
    },
    Printf {
        dest: Dest,
        segs: Vec<Seg>,
    },
    Ls {
        dest: Dest,
    },
    Prune,
    Quit,
    Delete,
    Exec(Box<ExecSpec>),
}

pub enum Node {
    And(Box<Node>, Box<Node>),
    Or(Box<Node>, Box<Node>),
    Comma(Box<Node>, Box<Node>),
    Not(Box<Node>),
    P(Prim),
}

fn field_time(e: &Entry, f: TField) -> Ts {
    match f {
        TField::A => e.st().atime,
        TField::B => e.st().btime,
        TField::C => e.ctime(),
        TField::M => e.st().mtime,
    }
}

fn window(delta: i128, cmp: Cmp, win: i128) -> bool {
    match cmp {
        Cmp::Gt => delta > 0,
        Cmp::Lt => delta < 0,
        Cmp::Eq => delta >= 0 && delta < win,
    }
}

/// -name's base: base_name with trailing slashes stripped ("/" stays).
fn name_base(path: &str) -> &str {
    let b = base_name(path);
    let t = b.trim_end_matches('/');
    if t.is_empty() {
        &b[..b.len().min(1)]
    } else {
        t
    }
}

impl Node {
    pub fn eval(&mut self, run: &mut Run, e: &Entry) -> bool {
        match self {
            Node::And(l, r) => {
                if !l.eval(run, e) || run.quit {
                    return false;
                }
                r.eval(run, e)
            }
            Node::Or(l, r) => {
                let v = l.eval(run, e);
                if v || run.quit {
                    return v;
                }
                r.eval(run, e)
            }
            Node::Comma(l, r) => {
                l.eval(run, e);
                if run.quit {
                    return false;
                }
                r.eval(run, e)
            }
            Node::Not(c) => !c.eval(run, e),
            Node::P(p) => p.eval(run, e),
        }
    }

    /// Launch every pending `{} +` batch (end of walk / -quit).
    pub fn flush_execs(&mut self, run: &mut Run) {
        match self {
            Node::And(l, r) | Node::Or(l, r) | Node::Comma(l, r) => {
                l.flush_execs(run);
                r.flush_execs(run);
            }
            Node::Not(c) => c.flush_execs(run),
            Node::P(Prim::Exec(x)) => x.flush(run),
            Node::P(_) => {}
        }
    }
}

impl Prim {
    fn eval(&mut self, run: &mut Run, e: &Entry) -> bool {
        match self {
            Prim::True => true,
            Prim::False => false,
            Prim::Name { pat, icase } => fnmatch(pat, name_base(&e.path).as_bytes(), *icase),
            Prim::Path { pat, icase } => fnmatch(pat, e.path.as_bytes(), *icase),
            Prim::Lname { pat, icase } => {
                e.kind() == Kind::Lnk
                    && e.link_target()
                        .is_some_and(|t| fnmatch(pat, t.as_bytes(), *icase))
            }
            Prim::Regex(re) => re.matches_entire_bytes(e.path.as_bytes()),
            Prim::Type { set, x } => {
                let k = if *x {
                    if e.follow {
                        e.lbasic.kind
                    } else if e.lbasic.kind == Kind::Lnk {
                        e.followed().map(|(b, _)| b.kind).unwrap_or(Kind::Lnk)
                    } else {
                        e.lbasic.kind
                    }
                } else {
                    e.kind()
                };
                let idx = b"bcdpflsD".iter().position(|&c| c == k.letter()).unwrap();
                set[idx]
            }
            Prim::Size { t, unit } => {
                let s = e.size();
                t.test(s.div_ceil(*unit))
            }
            Prim::Empty => match e.kind() {
                Kind::Reg => e.size() == 0,
                Kind::Dir => e.dir_is_empty(),
                _ => false,
            },
            Prim::Time {
                field,
                cmp,
                reft,
                window: w,
            } => window(field_time(e, *field).ns() - *reft, *cmp, *w),
            Prim::Newer { field, t } => field_time(e, *field).ns() > *t,
            Prim::Used { cmp, reft } => {
                let d = e.st().atime.ns() - e.ctime().ns();
                window(d - *reft, *cmp, 86_400_000_000_000)
            }
            Prim::Perm { kind, val } => {
                let mode = e.mode() & 0o7777;
                let v = val[(e.kind() == Kind::Dir) as usize];
                match kind {
                    PermKind::Exact => mode == v,
                    PermKind::All => mode & v == v,
                    PermKind::Any => v == 0 || mode & v != 0,
                }
            }
            Prim::Uid(t) => t.test(run.account(e, true).0),
            Prim::Gid(t) => t.test(run.account(e, false).0),
            Prim::NoUser => run.account(e, true).1.is_none(),
            Prim::NoGroup => run.account(e, false).1.is_none(),
            Prim::Readable => e.device || winfs::readable(&e.native, e.kind() == Kind::Dir),
            Prim::Writable => {
                e.device || winfs::writable(&e.native, e.kind() == Kind::Dir, e.st().attrs)
            }
            Prim::Executable => match e.kind() {
                Kind::Dir => winfs::readable(&e.native, true),
                Kind::Reg => {
                    super::entry::exec_ext(name_base(&e.path).as_bytes())
                        && winfs::readable(&e.native, false)
                }
                _ => false,
            },
            Prim::Links(t) => t.test(e.full().map(|h| h.nlink).unwrap_or(1)),
            Prim::Inum(t) => t.test(e.full().map(|h| h.ino).unwrap_or(0)),
            Prim::SameFile { dev, ino } => e.full().is_some_and(|h| h.dev == *dev && h.ino == *ino),
            Prim::Fstype(t) => run.fstype(e).eq_ignore_ascii_case(t),
            Prim::Print { dest, nul } => {
                let mut b = Vec::with_capacity(e.path.len() + 1);
                b.extend_from_slice(e.path.as_bytes());
                b.push(if *nul { 0 } else { b'\n' });
                run.emit(*dest, &b);
                true
            }
            Prim::Printf { dest, segs } => {
                let mut b = Vec::with_capacity(64);
                let go_on = printf::render(run, segs, e, &mut b);
                run.emit(*dest, &b);
                if !go_on {
                    run.flush_dest(*dest);
                }
                true
            }
            Prim::Ls { dest } => {
                let mut b = Vec::with_capacity(128);
                printf::ls_line(run, e, &mut b);
                run.emit(*dest, &b);
                true
            }
            Prim::Prune => {
                if !run.post_order {
                    run.prune = true;
                }
                true
            }
            Prim::Quit => {
                run.quit = true;
                true
            }
            Prim::Delete => {
                if e.depth == 0 && name_base(&e.path) == "." {
                    return true;
                }
                let is_dir = matches!(e.lbasic.kind, Kind::Dir)
                    || (e.lbasic.kind == Kind::Lnk
                        && e.lbasic.attrs & winfs::FILE_ATTRIBUTE_DIRECTORY != 0);
                match winfs::delete(&e.native, is_dir) {
                    Ok(()) => true,
                    Err(code) => {
                        let msg = format!(
                            "find: cannot delete '{}': {}\n",
                            e.path,
                            winfs::reason(code)
                        );
                        run.error(&msg);
                        false
                    }
                }
            }
            Prim::Exec(x) => x.eval(run, e),
        }
    }
}
