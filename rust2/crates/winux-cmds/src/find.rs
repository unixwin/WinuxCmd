//! `find` — GNU findutils 4.9 semantics on the Windows value model
//! (#1488 rust2 lane; oracle: WSL `find (GNU findutils) 4.9.0`).
//!
//! Layout:
//! * [`parse`] — leading options (`-H -L -P -D -O`), start points, the
//!   expression (GNU's flat predicate list + tree.c precedence climbing,
//!   so every GNU diagnostic fires at the same token);
//! * [`expr`] — predicate evaluation (pred.c);
//! * [`printf`] — `-printf`/`-fprintf` (print.c) and `-ls` (listfile.c);
//! * [`exec`] — `-exec`/`-execdir`/`-ok`/`-okdir` through the shared
//!   childproc launch chain, `{} +` batching;
//! * [`entry`] / [`winfs`] — the lazily stat'ed entry over raw Win32 FFI.
//!
//! Traversal is lazy and iterative (one open enumeration handle per
//! directory level, no tree materialization): pre-order by default,
//! post-order under `-depth`/`-delete`, `-maxdepth`/`-mindepth`,
//! `-prune`, `-quit`, `-xdev`. Directory order is the volume's native
//! enumeration order (NTFS: case-insensitive B-tree order) — GNU prints
//! readdir order, which on ext4 is hash order, so order-dependent gate
//! rows compare sorted output (`sorted` mode). Symbolic links AND
//! junctions are links (`-type l`, not descended under -P, followed under
//! -L/-H with "File system loop detected" on a cycle).
//!
//! Windows value model (1.x C++ find.cpp parity): mode 0777 links, 0755
//! directories and `.exe/.com/.bat/.cmd/.ps1`, 0644 other files, the
//! read-only attribute clears the write bits; uid/gid via the shared SID
//! id formula; `%i` file index, `%D` volume serial, `%n` link count, `%b`
//! / `%k` the allocation size, `%F`/`-fstype` the volume filesystem name
//! (matched case-insensitively); `-newerB*`/`%B` use the creation time
//! (Windows has birth time; the GNU oracle build reports it unsupported).
//!
//! Documented deviations: warnings default OFF (GNU enables them when
//! stdin is a terminal; the injected-IO contract cannot see a tty — use
//! `-warn`); `-execdir`'s insecure-PATH check is skipped (Windows PATH
//! entries are drive-absolute, the POSIX `/`-prefix rule cannot apply);
//! `-context`/`%Z` behave like a GNU build with SELinux disabled.

pub mod entry;
pub mod exec;
pub mod expr;
pub mod fnm;
pub mod parse;
pub mod printf;
pub mod rx;
pub mod winfs;

#[cfg(test)]
mod tests;

use std::collections::HashMap;
use std::ffi::OsString;
use std::io::{BufWriter, Read, Write};

use winux_core::{CmdResult, Ctx, Io};

use entry::{Basic, Entry, Kind, Ts};
use expr::Node;
use parse::{Follow, Options, Parsed};

pub const HELP: &str = "Usage: find [-H] [-L] [-P] [-Olevel] [-D debugopts] [path...] [expression]

Default path is the current directory; default expression is -print.
Expression may consist of: operators, options, tests, and actions.

Operators (decreasing precedence; -and is implicit where no others are given):
      ( EXPR )   ! EXPR   -not EXPR   EXPR1 -a EXPR2   EXPR1 -and EXPR2
      EXPR1 -o EXPR2   EXPR1 -or EXPR2   EXPR1 , EXPR2

Positional options (always true):
      -daystart -follow -nowarn -regextype -warn

Normal options (always true, specified before other expressions):
      -depth -files0-from FILE -maxdepth LEVELS -mindepth LEVELS
       -mount -noleaf -xdev -ignore_readdir_race -noignore_readdir_race

Tests (N can be +N or -N or N):
      -amin N -anewer FILE -atime N -cmin N -cnewer FILE -context CONTEXT
      -ctime N -empty -false -fstype TYPE -gid N -group NAME -ilname PATTERN
      -iname PATTERN -inum N -iwholename PATTERN -iregex PATTERN
      -links N -lname PATTERN -mmin N -mtime N -name PATTERN -newer FILE
      -nouser -nogroup -path PATTERN -perm [-/]MODE -regex PATTERN
      -readable -writable -executable
      -wholename PATTERN -size N[bcwkMG] -true -type [bcdpflsD] -uid N
      -used N -user NAME -xtype [bcdpfls]

Actions:
      -delete -print0 -printf FORMAT -fprintf FILE FORMAT -print 
      -fprint0 FILE -fprint FILE -ls -fls FILE -prune -quit
      -exec COMMAND ; -exec COMMAND {} + -ok COMMAND ;
      -execdir COMMAND ; -execdir COMMAND {} + -okdir COMMAND ;

Other common options:
      --help                   display this help and exit
      --version                output version information and exit

Valid arguments for -D:
exec, opt, rates, search, stat, time, tree, all, help
Use '-D help' for a description of the options, or see find(1)

Please see also the documentation at https://www.gnu.org/software/findutils/.
You can report (and track progress on fixing) bugs in the \"find\"
program via the GNU findutils bug-reporting page at
https://savannah.gnu.org/bugs/?group=findutils or, if
you have no web access, by sending email to <bug-findutils@gnu.org>.
";

pub const VERSION: &str = "find (GNU findutils) 4.9.0
Copyright (C) 2022 Free Software Foundation, Inc.
License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>.
This is free software: you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.

Written by Eric B. Decker, James Youngman, and Kevin Dalley.
Features enabled: D_TYPE O_NOFOLLOW(enabled) LEAF_OPTIMISATION FTS(FTS_CWDFD) CBO(level=2) 
";

/// Where an output action writes.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Dest {
    Stdout,
    Stderr,
    File(usize),
}

/// A `-fprint`/`-fprintf`/`-fls` target (opened at parse time, shared
/// by name like GNU's sharefile).
pub struct OutFile {
    pub name: String,
    pub w: BufWriter<std::fs::File>,
}

/// Mutable run state shared by evaluation.
pub struct Run<'a> {
    pub out: BufWriter<&'a mut dyn Write>,
    pub err: &'a mut dyn Write,
    pub stdin: &'a mut dyn Read,
    pub ctx: &'a Ctx<'a>,
    pub status: u32,
    pub quit: bool,
    pub prune: bool,
    pub post_order: bool,
    pub files: Vec<OutFile>,
    pub ls_widths: [usize; 6],
    pub start: Ts,
    accounts: HashMap<Vec<u8>, (u64, Option<String>)>,
    fstypes: HashMap<u32, String>,
    zone: Option<crate::date::TimeZone>,
}

impl<'a> Run<'a> {
    /// Broken-down local time of `sec` (TZ env when set, else the
    /// DST-aware system zone).
    pub(crate) fn local(&self, sec: i64) -> crate::date::DateTime {
        run_local(self.zone.as_ref(), self.ctx, sec)
    }

    /// (id, name) of the owner (`owner`) or group of an entry.
    pub fn account(&mut self, e: &Entry, owner: bool) -> (u64, Option<String>) {
        let sid = match e.sids() {
            Some((o, g)) => {
                if owner {
                    o
                } else {
                    g
                }
            }
            None => return (0, None),
        };
        if let Some(v) = self.accounts.get(sid) {
            return v.clone();
        }
        let v = winfs::sid_account(sid);
        self.accounts.insert(sid.clone(), v.clone());
        v
    }

    /// Filesystem type name of the volume holding an entry.
    pub fn fstype(&mut self, e: &Entry) -> String {
        if e.device {
            return "devfs".to_string();
        }
        let dev = e.full().map(|h| h.dev);
        if let Some(d) = dev {
            if let Some(t) = self.fstypes.get(&d) {
                return t.clone();
            }
        }
        let t = winfs::fs_type(&e.native);
        if let Some(d) = dev {
            self.fstypes.insert(d, t.clone());
        }
        t
    }

    pub fn emit(&mut self, dest: Dest, b: &[u8]) {
        let _ = match dest {
            Dest::Stdout => self.out.write_all(b),
            Dest::Stderr => {
                let _ = self.out.flush();
                self.err.write_all(b)
            }
            Dest::File(i) => self.files[i].w.write_all(b),
        };
    }

    pub fn flush_dest(&mut self, dest: Dest) {
        let _ = match dest {
            Dest::Stdout => self.out.flush(),
            Dest::Stderr => self.err.flush(),
            Dest::File(i) => self.files[i].w.flush(),
        };
    }

    /// A non-fatal diagnostic: printed, exit status becomes 1.
    pub fn error(&mut self, msg: &str) {
        let _ = self.out.flush();
        let _ = self.err.write_all(msg.as_bytes());
        self.status = 1;
    }
}

/// Library entry point.
pub fn find_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let args: Vec<String> = args
        .iter()
        .map(|a| a.to_string_lossy().into_owned())
        .collect();
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default();
    let start = Ts {
        sec: now.as_secs() as i64,
        nsec: now.subsec_nanos(),
    };
    let zone = match ctx.env.get("TZ") {
        Some(v) if !v.is_empty() => Some(crate::date::TimeZone::parse(&v.to_string_lossy())),
        _ => None,
    };
    let Io {
        stdin,
        stdout,
        stderr,
    } = io;
    let mut run = Run {
        out: BufWriter::with_capacity(64 * 1024, &mut **stdout),
        err: &mut **stderr,
        stdin: &mut **stdin,
        ctx,
        status: 0,
        quit: false,
        prune: false,
        post_order: false,
        files: Vec::new(),
        ls_widths: [9, 6, 3, 8, 8, 8],
        start,
        accounts: HashMap::new(),
        fstypes: HashMap::new(),
        zone,
    };
    let midnight = |sec: i64| -> i64 {
        let dt = run_local(run.zone.as_ref(), ctx, sec);
        sec - (dt.hour * 3600 + dt.minute * 60 + dt.second)
    };
    let mut help_out: Vec<u8> = Vec::new();
    let parsed = parse::parse(&args, ctx, start, &mut help_out, run.err, &midnight);
    let _ = run.out.write_all(&help_out);
    let (opts, roots, mut expr, files) = match parsed {
        Ok(Parsed::Done) => {
            run.out.flush()?;
            return Ok(0);
        }
        Ok(Parsed::Run {
            opts,
            roots,
            expr,
            files,
        }) => (opts, roots, expr, files),
        Err(parse::Fail(lines)) => {
            let _ = run.out.flush();
            for l in lines {
                let _ = writeln!(run.err, "{l}");
            }
            return Ok(1);
        }
    };
    run.files = files;
    run.post_order = opts.post_order;
    let roots = match &opts.files0_from {
        Some(src) => match read_files0(&mut run, src) {
            Some(r) => r,
            None => {
                let _ = run.out.flush();
                return Ok(1);
            }
        },
        None if roots.is_empty() => vec![".".to_string()],
        None => roots,
    };
    for r in &roots {
        if run.quit {
            break;
        }
        walk_root(&mut run, &opts, &mut expr, r);
    }
    expr.flush_execs(&mut run);
    for f in &mut run.files {
        let _ = f.w.flush();
    }
    run.out.flush()?;
    Ok(run.status)
}

fn run_local(zone: Option<&crate::date::TimeZone>, ctx: &Ctx, sec: i64) -> crate::date::DateTime {
    if let Some(z) = zone {
        let (off, name) = z.at(sec);
        return crate::date::DateTime::from_epoch(sec, off, name);
    }
    let ticks = sec * 10_000_000 + 116_444_736_000_000_000;
    let off = crate::fs_win::imp::to_local(ticks)
        .map(|(_, m)| m as i64 * 60)
        .unwrap_or(0);
    let name = crate::date::effective_zone(false, ctx.env).at(sec).1;
    crate::date::DateTime::from_epoch(sec, off, name)
}

/// `-files0-from FILE` (`-` = stdin): NUL-separated start points.
fn read_files0(run: &mut Run, src: &str) -> Option<Vec<String>> {
    let mut data = Vec::new();
    if src == "-" {
        let _ = run.stdin.read_to_end(&mut data);
    } else {
        let path = crate::fs_win::imp::api_path(run.ctx, src);
        match std::fs::read(&path) {
            Ok(d) => data = d,
            Err(e) => {
                let msg = format!(
                    "find: cannot open '{src}' for reading: {}\n",
                    crate::fsutil::reason(&e)
                );
                run.error(&msg);
                return None;
            }
        }
    }
    let mut out = Vec::new();
    let mut parts: Vec<&[u8]> = data.split(|&b| b == 0).collect();
    if parts.last().is_some_and(|p| p.is_empty()) {
        parts.pop();
    }
    for p in parts {
        if p.is_empty() {
            run.error("find: invalid zero-length file name\n");
            continue;
        }
        out.push(String::from_utf8_lossy(p).into_owned());
    }
    Some(out)
}

/// Build the starting-point entry; `None` after reporting a failure.
fn root_entry(run: &mut Run, opts: &Options, r: &str) -> Option<Entry> {
    let display = r.replace('\\', "/");
    let root_len = display.len();
    if r.is_empty() {
        run.error("find: '': No such file or directory\n");
        return None;
    }
    if let Some(dev) = winux_core::path::resolve_pseudo_device(r) {
        return Some(Entry::device(
            display,
            dev.encode_utf16().collect(),
            root_len,
        ));
    }
    let native: Vec<u16> = collapse_dots(&crate::fs_win::imp::api_path(run.ctx, r))
        .encode_utf16()
        .collect();
    let follow = opts.follow != Follow::Never;
    let wild = r.contains(['*', '?']);
    let trimmed: Vec<u16> = {
        let mut n = native.clone();
        while n.len() > 7 && n.last() == Some(&(b'\\' as u16)) && n[n.len() - 2] != b':' as u16 {
            n.pop();
        }
        n
    };
    let lbasic = if wild {
        None
    } else {
        winfs::find_one(&trimmed)
            .ok()
            .map(|d| Basic::from_find(&d, &trimmed))
    };
    let mut handle = None;
    let lbasic = match lbasic {
        Some(b) => b,
        None => match winfs::handle_info(&trimmed, false) {
            Ok(h) => {
                let b = Basic::from_handle(&h, &trimmed);
                handle = Some(h);
                b
            }
            Err(code) => {
                let msg = format!("find: '{display}': {}\n", winfs::reason(code));
                run.error(&msg);
                return None;
            }
        },
    };
    let e = Entry::new(display.clone(), trimmed, 0, root_len, follow, lbasic);
    if let Some(h) = handle {
        e.set_lfull(h);
    }
    // a trailing slash demands a directory (GNU: ENOTDIR)
    if (r.ends_with('/') || r.ends_with('\\')) && e.kind() != Kind::Dir {
        let followed_dir =
            e.lbasic.kind == Kind::Lnk && e.followed().is_ok_and(|(b, _)| b.kind == Kind::Dir);
        if !followed_dir {
            let msg = format!("find: '{display}': Not a directory\n");
            run.error(&msg);
            return None;
        }
    }
    Some(e)
}

/// Lexically resolve `.` / `..` components of an extended (`\\?\`)
/// path: the `\\?\` namespace gets no normalization from the NT loader,
/// so `find ..` would otherwise reach CreateFileW as `...\x\..`.
fn collapse_dots(p: &str) -> String {
    let (prefix, rest) = if let Some(r) = p.strip_prefix(r"\\?\UNC\") {
        let mut it = r.splitn(3, '\\');
        let server = it.next().unwrap_or("");
        let share = it.next().unwrap_or("");
        (
            format!(r"\\?\UNC\{server}\{share}\"),
            it.next().unwrap_or("").to_string(),
        )
    } else if let Some(r) = p.strip_prefix(r"\\?\") {
        match r.find('\\') {
            Some(i) => (format!(r"\\?\{}\", &r[..i]), r[i + 1..].to_string()),
            None => return p.to_string(),
        }
    } else {
        return p.to_string();
    };
    if !rest.split('\\').any(|c| c == "." || c == "..") {
        return p.to_string();
    }
    let mut parts: Vec<&str> = Vec::new();
    for c in rest.split('\\') {
        match c {
            "" | "." => {}
            ".." => {
                parts.pop();
            }
            _ => parts.push(c),
        }
    }
    format!("{prefix}{}", parts.join("\\"))
}

struct Frame {
    iter: winfs::DirIter,
    entry: Entry,
    id: Option<(u32, u64)>,
}

/// Visit one entry: evaluate (pre-order) and decide whether to descend.
/// Returns the frame to push when the entry is a directory to enter.
fn visit(
    run: &mut Run,
    opts: &Options,
    expr: &mut Node,
    e: Entry,
    stack: &[Frame],
    root_dev: Option<u32>,
) -> Option<Frame> {
    let mut descend = e.kind() == Kind::Dir && opts.maxdepth.is_none_or(|m| e.depth < m);
    let mut id = None;
    if descend && e.follow && opts.follow != Follow::Never {
        if let Some(h) = e.full() {
            id = Some((h.dev, h.ino));
            if let Some(anc) = stack.iter().find(|f| f.id == id) {
                let msg = format!(
                    "find: File system loop detected; '{}' is part of the same file system loop as '{}'.\n",
                    e.path, anc.entry.path
                );
                run.error(&msg);
                return None;
            }
        }
    }
    let evaluate = e.depth >= opts.mindepth;
    if !opts.post_order && evaluate {
        run.prune = false;
        expr.eval(run, &e);
        if run.prune {
            descend = false;
        }
        if run.quit {
            return None;
        }
    }
    if descend && opts.xdev && e.depth > 0 {
        if let (Some(rd), Some(h)) = (root_dev, e.full()) {
            if h.dev != rd {
                descend = false;
            }
        }
    }
    if descend {
        match winfs::DirIter::open(&e.native) {
            Ok(iter) => return Some(Frame { iter, entry: e, id }),
            Err(code) => {
                let msg = format!("find: '{}': {}\n", e.path, winfs::reason(code));
                run.error(&msg);
            }
        }
    }
    if opts.post_order && evaluate {
        expr.eval(run, &e);
    }
    None
}

fn walk_root(run: &mut Run, opts: &Options, expr: &mut Node, r: &str) {
    let root = match root_entry(run, opts, r) {
        Some(e) => e,
        None => return,
    };
    let root_dev = if opts.xdev {
        root.full().map(|h| h.dev)
    } else {
        None
    };
    let mut stack: Vec<Frame> = Vec::new();
    if let Some(f) = visit(run, opts, expr, root, &stack, root_dev) {
        stack.push(f);
    }
    let follow_children = opts.follow == Follow::Always;
    while !stack.is_empty() {
        if run.quit {
            return;
        }
        let next = stack.last_mut().unwrap().iter.next();
        match next {
            Some(Ok(fd)) => {
                let top = &stack.last().unwrap().entry;
                let name = String::from_utf16_lossy(&fd.wname);
                let mut path = String::with_capacity(top.path.len() + name.len() + 1);
                path.push_str(&top.path);
                if !path.ends_with('/') {
                    path.push('/');
                }
                path.push_str(&name);
                let mut native = Vec::with_capacity(top.native.len() + fd.wname.len() + 1);
                native.extend_from_slice(&top.native);
                if native.last() != Some(&(b'\\' as u16)) {
                    native.push(b'\\' as u16);
                }
                native.extend_from_slice(&fd.wname);
                let lbasic = Basic::from_find(&fd, &native);
                let child = Entry::new(
                    path,
                    native,
                    top.depth + 1,
                    top.root_len,
                    follow_children,
                    lbasic,
                );
                if let Some(f) = visit(run, opts, expr, child, &stack, root_dev) {
                    stack.push(f);
                }
            }
            Some(Err(code)) => {
                let f = stack.pop().unwrap();
                let msg = format!("find: '{}': {}\n", f.entry.path, winfs::reason(code));
                run.error(&msg);
                if opts.post_order && f.entry.depth >= opts.mindepth {
                    expr.eval(run, &f.entry);
                }
            }
            None => {
                let f = stack.pop().unwrap();
                drop(f.iter);
                if opts.post_order && f.entry.depth >= opts.mindepth {
                    expr.eval(run, &f.entry);
                }
            }
        }
    }
}
