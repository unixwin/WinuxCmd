//! `tail` follow mode — GNU coreutils 9.4 tail.c `tail_forever` /
//! `tail_forever_inotify` / `recheck` semantics, probed against WSL GNU
//! 9.4 (rust2/target/probe/p1-p3.sh; the live scenarios are
//! rust2/gate/wire/tail-follow.sh, results in tail-follow.out).
//!
//! GNU rules carried over:
//! * `-f`/`--follow[=descriptor]` follows the open handle (a renamed or
//!   deleted file keeps being read); `--follow=name` re-resolves the name
//!   and reopens on replacement; `-F` = `--follow=name --retry`; each
//!   -f/--follow/-F ASSIGNS the mode in argv order.
//! * Appended data is copied as it arrives; a size below the consumed
//!   offset prints `tail: X: file truncated` and restarts at 0 (GNU's
//!   heuristic — a truncate+regrow past the offset between two checks is
//!   missed exactly like GNU's).
//! * Headers (`==> X <==`, blank line before) are written when output
//!   SWITCHES files and only when data is written (the inotify path's
//!   dump_remainder(want_header)); the "previous file" starts as the LAST
//!   operand whether or not it printed (probed: `-F f g` with g missing
//!   prints g's first data without a header).
//! * Name-state diagnostics, GNU wording (double spaces included):
//!   `'X' has become inaccessible: <reason>` (with --retry),
//!   `X: <reason>` (without --retry),
//!   `'X' has appeared;  following new file`, `'X' has been replaced;
//!   following new file`, `'X' has become accessible`, `'X' has been
//!   replaced with an untailable file[; giving up on this name]`,
//!   `no files remaining`.
//! * `-f` ignores a pipe/FIFO stdin (POSIX; tail.c ignore_fifo_and_pipe):
//!   `printf x | tail -f` prints and exits 0. A regular-file stdin is
//!   followed by descriptor; a console stdin alone with no --pid is read
//!   blocking forever, like GNU's blocking tty follow.
//! * `--pid=PID`: after PID dies tail reads every file once more and
//!   exits. Exit status: GNU's inotify path exits 0 on writer death and 1
//!   on "no files remaining"; its polling fallback (chosen when stdin is
//!   followed, nothing regular is open, an operand is a symlink, or an
//!   open failed in descriptor mode) exits with the initial-open status
//!   for both. [`gnu_uses_inotify`] reproduces that choice so the exit
//!   status matches the oracle (probed, p1.sh).
//!
//! Windows mechanics:
//! * The loop sleeps in one WaitForMultipleObjects on
//!   FindFirstChangeNotificationW handles for the followed files' parent
//!   directories plus the --pid process handle (pid death wakes it at
//!   once). The timeout is the sleep interval capped at a 100 ms re-stat
//!   tick for up to 64 files ([`wake_tick`]): directory notifications
//!   fire when a writer closes the file, not on writes through a handle
//!   it keeps open, so the tick keeps such appends prompt (measured
//!   1-133 ms vs up to 931 ms at the default 1 s interval). A
//!   notification only wakes the loop early; every wake re-stats every
//!   file, so coalesced or missing notifications (network shares)
//!   degrade to polling. The wait is never 0 ms (`-s 0` waits 1 ms), and
//!   an idle follower measured 0 CPU over 5 s.
//! * Descriptor mode holds the handle (FILE_SHARE_READ|WRITE|DELETE):
//!   the file can be appended to, truncated, renamed away and deleted
//!   while followed, and is read on through the old handle like GNU's fd.
//!   A rename OVER the followed file still fails for the replacer
//!   (classic MoveFileEx refuses an open target; inherent to holding it).
//! * Name mode closes the handle between wakes ("parked") and reopens the
//!   name on every wake (the inotify path's IN_MOVE_SELF/IN_DELETE_SELF/
//!   IN_CREATE immediacy), comparing (volume serial, file index) with
//!   the followed file. Holding nothing open lets log rotation replace
//!   the name atomically (`mv new f`), which a held handle would block —
//!   and matches the 1.x C++ applet, which also reopened per poll.
//!   `--max-unchanged-stats` is accepted and validated but has nothing
//!   left to bound.
//!
//! Output goes through `io.stdout` and is flushed after every chunk; a
//! write error ends the run (broken pipe: `Err(CmdError::Io)`, which the
//! host maps to 141; GNU dies by SIGPIPE on the write).
//!
//! Liveness without --retry (tail.c any_live_files + the inotify hash):
//! a vanished name stays live on GNU's inotify path while its inode
//! exists — renamed away, or deleted but still held open by a writer —
//! and is dropped once the inode is freed (IN_DELETE_SELF). The port
//! asks the volume for the old (volume, index) identity with
//! OpenFileById, which answers exactly that question.
//!
//! Documented gaps (no GNU-observable difference in the harness): no
//! symlink-replacement diagnostic, no remote-file detection (network
//! shares are polled like local files and count as local for the exit
//! status choice), a console stdin among several inputs or with --pid
//! is not read after its EOF (GNU warns that this is ineffective too),
//! and a vanished stdout reader is noticed at the next write rather than
//! by polling stdout.

use std::fs::File;
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::PathBuf;

use super::sys::{self, StdinKind};
use super::{epipe_like, write_failure};
use crate::fsutil::{gnu_open_reason, quotef, reason};
use winux_core::{CmdResult, Ctx, Io};

const ENOENT: &str = "No such file or directory";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(super) enum FollowMode {
    Descriptor,
    Name,
}

pub(super) struct FollowOpts {
    pub mode: FollowMode,
    pub retry: bool,
    pub sleep_secs: f64,
    pub pid: Option<u32>,
    #[allow(dead_code)]
    pub max_unchanged: u64,
}

/// GNU's `f->errnum`: 0, an errno (kept as its strerror text), or -1
/// for "untailable file type".
#[derive(Clone, Debug, PartialEq, Eq)]
pub(super) enum Errnum {
    None,
    Err(String),
    Untailable,
}

/// One operand's follow state (tail.c `struct File_spec`).
pub(super) struct Spec {
    pub name: String,
    /// resolved path (None for stdin)
    pub path: Option<PathBuf>,
    /// the open handle (GNU's fd >= 0)
    pub fd: Option<File>,
    /// name mode: logically open (GNU's fd >= 0) but the handle is closed
    /// between wakes, see the module doc
    pub parked: bool,
    /// name mode: the name vanished but its file still exists (renamed
    /// away, or deleted while a writer holds it) — GNU's inotify watch
    /// on the inode is still in place, so the name counts as live there
    pub away: bool,
    /// Some(kind) for the `-` operand
    pub stdin: Option<StdinKind>,
    /// consumed offset (GNU's f->size)
    pub size: u64,
    pub id: (u32, u64),
    pub regular: bool,
    pub ignore: bool,
    pub tailable: bool,
    pub errnum: Errnum,
    pub symlink: bool,
    /// directory whose change notification wakes us for this file
    pub watch_dir: Option<PathBuf>,
}

impl Spec {
    pub fn new(name: &str) -> Spec {
        Spec {
            name: name.to_string(),
            path: None,
            fd: None,
            parked: false,
            away: false,
            stdin: None,
            size: 0,
            id: (0, 0),
            regular: false,
            ignore: false,
            tailable: true,
            errnum: Errnum::None,
            symlink: false,
            watch_dir: None,
        }
    }

    fn pretty(&self) -> &str {
        if self.name == "-" {
            "standard input"
        } else {
            &self.name
        }
    }

    /// stdin that holds a valid descriptor but is never read by the loop
    /// (console/NUL among several inputs): live, silent
    fn inert(&self) -> bool {
        matches!(self.stdin, Some(StdinKind::Console | StdinKind::CharDev)) && self.fd.is_none()
    }

    fn has_fd(&self) -> bool {
        self.fd.is_some() || self.parked || self.inert()
    }
}

/// GNU quoteaf (shell-escape-always) for the name diagnostics.
pub(super) fn quoteaf(name: &str) -> String {
    if name.contains('\'') {
        format!("\"{name}\"")
    } else {
        format!("'{name}'")
    }
}

/// tail.c write_header + the follow loop's "previous file" (`last` /
/// `prev_fspec`).
pub(super) struct Headers {
    enabled: bool,
    first: bool,
    last: usize,
}

impl Headers {
    pub fn new(enabled: bool, n_files: usize) -> Headers {
        Headers {
            enabled,
            first: true,
            last: n_files.saturating_sub(1),
        }
    }

    fn text(&mut self, name: &str) -> String {
        let s = format!("{}==> {name} <==\n", if self.first { "" } else { "\n" });
        self.first = false;
        s
    }

    /// Header of the initial dump (every input, when enabled).
    pub fn write_initial(&mut self, out: &mut dyn Write, name: &str) -> std::io::Result<()> {
        if self.enabled {
            let t = self.text(name);
            out.write_all(t.as_bytes())?;
        }
        Ok(())
    }

    /// Called before writing follow data of file `i`: the header text
    /// when output switches files, and the switch is recorded.
    pub fn switch_to(&mut self, i: usize, name: &str) -> Option<String> {
        if i == self.last {
            return None;
        }
        self.last = i;
        if self.enabled {
            Some(self.text(name))
        } else {
            None
        }
    }
}

/// What a check of the followed size says.
#[derive(Debug, PartialEq, Eq)]
pub(super) enum SizeChange {
    Truncated,
    Grown,
    Same,
}

pub(super) fn size_change(consumed: u64, now: u64) -> SizeChange {
    use std::cmp::Ordering::*;
    match now.cmp(&consumed) {
        Less => SizeChange::Truncated,
        Greater => SizeChange::Grown,
        Equal => SizeChange::Same,
    }
}

/// What reopening a followed name found.
#[derive(Clone, Debug, PartialEq, Eq)]
pub(super) enum Probe {
    /// open failed with this strerror
    Missing(String),
    /// opened something that is not tailable (a directory)
    Untailable,
    /// a regular file with this (volume, index) identity
    Regular((u32, u64)),
}

#[derive(Debug, PartialEq, Eq)]
pub(super) enum Verdict {
    /// same file: keep the current handle
    Keep,
    /// follow the newly opened file from offset 0
    NewFile,
    /// close the handle (the name is gone / untailable)
    Closed,
}

#[derive(Debug, PartialEq, Eq)]
pub(super) struct Recheck {
    pub msg: Option<String>,
    pub verdict: Verdict,
    pub tailable: bool,
    pub errnum: Errnum,
    pub ignore: Option<bool>,
}

/// tail.c recheck() as a pure state transition (the symlink and remote
/// legs are not modeled, see the module doc).
#[allow(clippy::too_many_arguments)]
pub(super) fn recheck_plan(
    had_fd: bool,
    id: (u32, u64),
    was_tailable: bool,
    prev: &Errnum,
    probe: &Probe,
    retry: bool,
    name_mode: bool,
    pretty: &str,
) -> Recheck {
    let mut tailable = !(retry && matches!(probe, Probe::Missing(_)));
    let mut msg = None;
    let mut ignore = None;
    let (ok, errnum) = match probe {
        Probe::Missing(r) => {
            let e = Errnum::Err(r.clone());
            if !tailable {
                if was_tailable {
                    msg = Some(format!(
                        "tail: {} has become inaccessible: {r}",
                        quoteaf(pretty)
                    ));
                }
            } else if *prev != e {
                msg = Some(format!("tail: {}: {r}", quotef(pretty)));
            }
            (false, e)
        }
        Probe::Untailable => {
            tailable = false;
            let give_up = !(retry && name_mode);
            ignore = Some(give_up);
            if was_tailable || *prev != Errnum::Untailable {
                msg = Some(format!(
                    "tail: {} has been replaced with an untailable file{}",
                    quoteaf(pretty),
                    if give_up {
                        "; giving up on this name"
                    } else {
                        ""
                    }
                ));
            }
            (false, Errnum::Untailable)
        }
        Probe::Regular(_) => (true, Errnum::None),
    };
    let verdict = if !ok {
        Verdict::Closed
    } else if match prev {
        Errnum::Untailable => true,
        Errnum::Err(r) => r != ENOENT,
        Errnum::None => false,
    } {
        msg = Some(format!("tail: {} has become accessible", quoteaf(pretty)));
        Verdict::NewFile
    } else if !had_fd {
        msg = Some(format!(
            "tail: {} has appeared;  following new file",
            quoteaf(pretty)
        ));
        Verdict::NewFile
    } else if matches!(probe, Probe::Regular(new) if *new != id) {
        msg = Some(format!(
            "tail: {} has been replaced;  following new file",
            quoteaf(pretty)
        ));
        Verdict::NewFile
    } else {
        Verdict::Keep
    };
    Recheck {
        msg,
        verdict,
        tailable,
        errnum,
        ignore,
    }
}

/// Inputs of GNU's inotify-vs-polling choice, per spec.
pub(super) struct ModeInput {
    pub stdin_followed: bool,
    pub open: bool,
    pub regular: bool,
    pub symlink: bool,
}

/// tail.c main: inotify is used unless stdin is followed, no file is
/// open, an operand is a symlink, an open input is not regular, or an
/// open failed while following by descriptor.
pub(super) fn gnu_uses_inotify(specs: &[ModeInput], ok: bool, mode: FollowMode) -> bool {
    !specs.iter().any(|s| s.stdin_followed)
        && specs.iter().any(|s| s.open)
        && !specs.iter().any(|s| s.symlink)
        && !specs.iter().any(|s| s.open && !s.regular)
        && !(!ok && mode == FollowMode::Descriptor)
}

/// Directory notifications fire when a writer CLOSES the file (or the
/// cache flushes), not on every write through a handle it keeps open —
/// measured: a held-open writer saw 45-931 ms latency at the default
/// 1 s interval. Small follow sets therefore also re-stat on a 100 ms
/// tick (one by-handle query per file: no spin), which keeps appended
/// data prompt like GNU's inotify IN_MODIFY; large sets fall back to
/// the plain interval.
pub(super) const FAST_TICK: std::time::Duration = std::time::Duration::from_millis(100);
pub(super) const FAST_TICK_MAX_FILES: usize = 64;

pub(super) fn wake_tick(
    interval: Option<std::time::Duration>,
    n_files: usize,
) -> Option<std::time::Duration> {
    if n_files > FAST_TICK_MAX_FILES {
        return interval;
    }
    Some(interval.map_or(FAST_TICK, |d| d.min(FAST_TICK)))
}

/// How the follow loop ended.
#[derive(Debug, PartialEq, Eq)]
pub(super) enum End {
    WriterDead,
    NoFiles,
}

pub(super) fn end_status(inotify: bool, ok: bool, end: End) -> u32 {
    match (inotify, end) {
        (true, End::WriterDead) => 0,
        (true, End::NoFiles) => 1,
        (false, _) => u32::from(!ok),
    }
}

/// tail.c any_live_files; on GNU's inotify path a name whose file
/// still exists elsewhere keeps its watch (hash entry) and stays live.
pub(super) fn any_live(specs: &[Spec], o: &FollowOpts, inotify: bool) -> bool {
    if o.retry && o.mode == FollowMode::Name {
        return true;
    }
    specs
        .iter()
        .any(|s| s.has_fd() || (!s.ignore && o.retry) || (inotify && s.away))
}

/// ignore_fifo_and_pipe: drop a pipe/FIFO stdin from the follow set and
/// count what is left (failed operands count: they still reach the
/// loop, which then reports "no files remaining").
pub(super) fn viable(specs: &mut [Spec]) -> usize {
    let mut n = 0;
    for s in specs.iter_mut() {
        if matches!(s.stdin, Some(StdinKind::Pipe | StdinKind::Unknown)) && !s.ignore {
            s.ignore = true;
            s.fd = None;
        } else if s.stdin.is_some() && s.ignore {
            // a failed stdin read
        } else {
            n += 1;
        }
    }
    n
}

/// Outcome of copying a file's new data.
enum Copy {
    Data(bool),
    /// tail.c dies on a follow read error: status 1
    ReadFailed,
}

pub(super) fn follow(
    specs: &mut [Spec],
    o: &FollowOpts,
    headers: &mut Headers,
    ok: bool,
    io: &mut Io,
    ctx: &Ctx,
) -> CmdResult {
    let modes: Vec<ModeInput> = specs
        .iter()
        .map(|s| ModeInput {
            stdin_followed: s.stdin.is_some() && !s.ignore,
            open: s.has_fd(),
            regular: s.fd.is_some() && s.regular,
            symlink: s.symlink,
        })
        .collect();
    let inotify = gnu_uses_inotify(&modes, ok, o.mode);
    let timeout = sys::interval(o.sleep_secs);
    let tick = wake_tick(timeout, specs.len());

    // GNU's blocking follow: a lone non-regular stdin, no --pid
    if specs.len() == 1 && specs[0].inert() && o.pid.is_none() && o.mode == FollowMode::Descriptor {
        return blocking_stdin(io, timeout);
    }

    let mut dirs: Vec<PathBuf> = Vec::new();
    for s in specs.iter() {
        if let Some(d) = &s.watch_dir {
            if !dirs.contains(d) {
                dirs.push(d.clone());
            }
        }
    }
    let mut waker = sys::Waker::new(&dirs);
    let mut pid = o.pid.map(sys::Pid::new);
    let mut writer_dead = false;
    let mut buf = vec![0u8; super::CHUNK];

    loop {
        let mut any_input = false;
        for i in 0..specs.len() {
            if specs[i].ignore || specs[i].inert() {
                continue;
            }
            if specs[i].path.is_some() && (specs[i].fd.is_none() || o.mode == FollowMode::Name) {
                // a closed name is reopened; name mode re-resolves the
                // name on every wake (the inotify path's IN_MOVE_SELF /
                // IN_DELETE_SELF / IN_CREATE immediacy)
                recheck(&mut specs[i], o, ctx, io);
            }
            if specs[i].fd.is_none() {
                continue;
            }
            let r = copy_new(specs, i, headers, io, &mut buf);
            if o.mode == FollowMode::Name && specs[i].path.is_some() && specs[i].fd.is_some() {
                specs[i].fd = None;
                specs[i].parked = true;
            }
            match r {
                Ok(Copy::Data(d)) => any_input |= d,
                Ok(Copy::ReadFailed) => return Ok(1),
                Err(e) => return write_failure(io, e),
            }
        }
        for s in specs.iter_mut().filter(|s| s.away) {
            // IN_DELETE_SELF: the renamed-away file itself is gone
            s.away = s
                .watch_dir
                .as_deref()
                .is_some_and(|d| sys::exists_by_id(d, s.id));
        }
        if !any_live(specs, o, inotify) {
            let _ = writeln!(io.stderr, "tail: no files remaining");
            return Ok(end_status(inotify, ok, End::NoFiles));
        }
        if !any_input {
            if writer_dead {
                return Ok(end_status(inotify, ok, End::WriterDead));
            }
            // once the writer is dead, read the files once more
            writer_dead = pid.as_mut().is_some_and(|p| p.dead());
            if !writer_dead {
                waker.wait(tick, pid.as_ref());
            }
        }
    }
}

/// check_fspec + dump_remainder for one open spec. `Err` is a stdout
/// write failure.
fn copy_new(
    specs: &mut [Spec],
    i: usize,
    headers: &mut Headers,
    io: &mut Io,
    buf: &mut [u8],
) -> std::io::Result<Copy> {
    let s = &mut specs[i];
    let pretty = s.pretty().to_string();
    let stat = match s.fd.as_ref() {
        Some(f) => sys::handle_stat(f),
        None => return Ok(Copy::Data(false)),
    };
    match stat {
        Ok(st) => {
            if st.regular && size_change(s.size, st.size) == SizeChange::Truncated {
                let _ = writeln!(io.stderr, "tail: {}: file truncated", quotef(&pretty));
                let rewound = s.fd.as_mut().map(|f| f.seek(SeekFrom::Start(0)));
                if !matches!(rewound, Some(Ok(_))) {
                    return Ok(Copy::ReadFailed);
                }
                s.size = 0;
            }
            if st.regular {
                // a reopened (parked) name resumes at the consumed offset
                let at = s.fd.as_mut().map(|f| f.seek(SeekFrom::Start(s.size)));
                if !matches!(at, Some(Ok(_))) {
                    return Ok(Copy::ReadFailed);
                }
            }
        }
        Err(e) => {
            // tail_forever: fstat failure drops the descriptor
            let _ = writeln!(io.stderr, "tail: {}: {}", quotef(&pretty), reason(&e));
            s.errnum = Errnum::Err(reason(&e).to_string());
            s.fd = None;
            return Ok(Copy::Data(false));
        }
    }
    let Some(f) = s.fd.as_mut() else {
        return Ok(Copy::Data(false));
    };
    let mut wrote = false;
    loop {
        let n = match f.read(buf) {
            Ok(0) => break,
            Ok(n) => n,
            Err(e) if e.kind() == std::io::ErrorKind::Interrupted => continue,
            Err(e) => {
                let _ = writeln!(
                    io.stderr,
                    "tail: error reading {}: {}",
                    quoteaf(&pretty),
                    reason(&e)
                );
                return Ok(Copy::ReadFailed);
            }
        };
        if !wrote {
            wrote = true;
            if let Some(h) = headers.switch_to(i, &pretty) {
                io.stdout.write_all(h.as_bytes())?;
            }
        }
        io.stdout.write_all(&buf[..n])?;
        s.size += n as u64;
    }
    if wrote {
        io.stdout.flush()?;
    }
    Ok(Copy::Data(wrote))
}

/// Reopen a followed name and apply tail.c recheck()'s transition.
fn recheck(s: &mut Spec, o: &FollowOpts, ctx: &Ctx, io: &mut Io) {
    let Some(path) = s.path.clone() else {
        return;
    };
    let mut opened: Option<(File, sys::Stat)> = None;
    let probe = match File::open(&path) {
        Ok(f) => match sys::handle_stat(&f) {
            Ok(st) if st.is_dir => Probe::Untailable,
            Ok(st) => {
                let id = st.id;
                opened = Some((f, st));
                Probe::Regular(id)
            }
            Err(e) => Probe::Missing(reason(&e).to_string()),
        },
        Err(e) => {
            if sys::path_stat(&path).is_ok_and(|st| st.is_dir) {
                Probe::Untailable
            } else {
                Probe::Missing(gnu_open_reason(ctx, &s.name, &e, false))
            }
        }
    };
    let had_fd = s.fd.is_some() || s.parked;
    // the common case: the name still holds the followed file
    if let (Probe::Regular(id), true) = (&probe, had_fd) {
        if *id == s.id && s.errnum == Errnum::None {
            if s.fd.is_none() {
                s.fd = opened.map(|(f, _)| f);
                s.parked = false;
            }
            return;
        }
    }
    let plan = recheck_plan(
        had_fd,
        s.id,
        s.tailable,
        &s.errnum,
        &probe,
        o.retry,
        o.mode == FollowMode::Name,
        s.pretty(),
    );
    if let Some(m) = &plan.msg {
        let _ = writeln!(io.stderr, "{m}");
    }
    s.tailable = plan.tailable;
    s.errnum = plan.errnum;
    if let Some(ig) = plan.ignore {
        s.ignore = ig;
    }
    match plan.verdict {
        Verdict::Keep => {
            if s.fd.is_none() {
                s.fd = opened.map(|(f, _)| f);
                s.parked = false;
            }
        }
        Verdict::Closed => {
            // the name stays in the set and is re-resolved on every wake
            // (silently while the error repeats); liveness decides
            // whether the run goes on
            if had_fd && o.mode == FollowMode::Name && matches!(probe, Probe::Missing(_)) {
                s.away = s
                    .watch_dir
                    .as_deref()
                    .is_some_and(|d| sys::exists_by_id(d, s.id));
            }
            s.fd = None;
            s.parked = false;
        }
        Verdict::NewFile => {
            if let Some((f, st)) = opened {
                s.fd = Some(f);
                s.parked = false;
                s.away = false;
                s.size = 0;
                s.id = st.id;
                s.regular = st.regular;
            }
        }
    }
}

/// GNU's blocking follow of a lone tty/char-device stdin: keep reading
/// it; an EOF (^Z/^D, or NUL's endless EOF) waits one interval.
fn blocking_stdin(io: &mut Io, timeout: Option<std::time::Duration>) -> CmdResult {
    let pause = timeout
        .unwrap_or(std::time::Duration::from_secs(3600))
        .max(std::time::Duration::from_millis(1));
    let mut buf = vec![0u8; 8192];
    loop {
        match io.stdin.read(&mut buf) {
            Ok(0) | Err(_) => {
                std::thread::sleep(pause);
            }
            Ok(n) => {
                if let Err(e) = io
                    .stdout
                    .write_all(&buf[..n])
                    .and_then(|()| io.stdout.flush())
                {
                    if epipe_like(&e) {
                        return Err(winux_core::CmdError::Io(e));
                    }
                    let _ = writeln!(io.stderr, "tail: write error: {}", reason(&e));
                    return Ok(1);
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn headers_switch_only_between_files() {
        // tail -f f g: the initial dump writes both headers, the loop's
        // "previous file" starts at the LAST operand
        let mut h = Headers::new(true, 2);
        let mut out = Vec::new();
        h.write_initial(&mut out, "f").unwrap();
        h.write_initial(&mut out, "g").unwrap();
        assert_eq!(out, b"==> f <==\n\n==> g <==\n");
        assert_eq!(h.switch_to(1, "g"), None); // g grew first: no header
        assert_eq!(h.switch_to(0, "f").as_deref(), Some("\n==> f <==\n"));
        assert_eq!(h.switch_to(0, "f"), None);
        assert_eq!(h.switch_to(1, "g").as_deref(), Some("\n==> g <==\n"));
    }

    #[test]
    fn headers_disabled_still_track_and_first_has_no_blank() {
        let mut h = Headers::new(false, 2);
        assert_eq!(h.switch_to(0, "f"), None);
        assert_eq!(h.last, 0);
        // every initial open failed: the first follow header has no blank
        let mut h = Headers::new(true, 2);
        assert_eq!(h.switch_to(0, "f").as_deref(), Some("==> f <==\n"));
    }

    #[test]
    fn truncation_detection() {
        assert_eq!(size_change(10, 3), SizeChange::Truncated);
        assert_eq!(size_change(10, 0), SizeChange::Truncated);
        assert_eq!(size_change(10, 10), SizeChange::Same);
        assert_eq!(size_change(10, 11), SizeChange::Grown);
        // -c +99 on a 3-byte file leaves the offset past EOF: GNU reports
        // a truncation at the first check
        assert_eq!(size_change(98, 3), SizeChange::Truncated);
    }

    fn plan(had_fd: bool, tailable: bool, prev: Errnum, probe: Probe, retry: bool) -> Recheck {
        recheck_plan(had_fd, (1, 7), tailable, &prev, &probe, retry, true, "f")
    }

    #[test]
    fn name_state_machine_retry() {
        // -F: the followed name disappears
        let r = plan(
            true,
            true,
            Errnum::None,
            Probe::Missing(ENOENT.into()),
            true,
        );
        assert_eq!(
            r.msg.as_deref(),
            Some("tail: 'f' has become inaccessible: No such file or directory")
        );
        assert_eq!(r.verdict, Verdict::Closed);
        assert!(!r.tailable);
        // still missing: silent
        let r = plan(
            false,
            false,
            Errnum::Err(ENOENT.into()),
            Probe::Missing(ENOENT.into()),
            true,
        );
        assert_eq!(r.msg, None);
        // it comes back (new inode or not): appeared, from offset 0
        let r = plan(
            false,
            false,
            Errnum::Err(ENOENT.into()),
            Probe::Regular((1, 9)),
            true,
        );
        assert_eq!(
            r.msg.as_deref(),
            Some("tail: 'f' has appeared;  following new file")
        );
        assert_eq!(r.verdict, Verdict::NewFile);
        assert!(r.tailable);
        assert_eq!(r.errnum, Errnum::None);
    }

    #[test]
    fn name_state_machine_replace_and_keep() {
        let r = plan(true, true, Errnum::None, Probe::Regular((1, 9)), true);
        assert_eq!(
            r.msg.as_deref(),
            Some("tail: 'f' has been replaced;  following new file")
        );
        assert_eq!(r.verdict, Verdict::NewFile);
        let r = plan(true, true, Errnum::None, Probe::Regular((1, 7)), true);
        assert_eq!(r.msg, None);
        assert_eq!(r.verdict, Verdict::Keep);
    }

    #[test]
    fn name_state_machine_without_retry() {
        // --follow=name: the bare strerror diagnostic, once
        let r = plan(
            true,
            true,
            Errnum::None,
            Probe::Missing(ENOENT.into()),
            false,
        );
        assert_eq!(r.msg.as_deref(), Some("tail: f: No such file or directory"));
        assert!(r.tailable);
        let r = plan(
            false,
            true,
            Errnum::Err(ENOENT.into()),
            Probe::Missing(ENOENT.into()),
            false,
        );
        assert_eq!(r.msg, None);
    }

    #[test]
    fn name_state_machine_untailable_and_accessible() {
        let r = plan(true, true, Errnum::None, Probe::Untailable, true);
        assert_eq!(
            r.msg.as_deref(),
            Some("tail: 'f' has been replaced with an untailable file")
        );
        assert_eq!(r.ignore, Some(false));
        let r = plan(true, true, Errnum::None, Probe::Untailable, false);
        assert_eq!(
            r.msg.as_deref(),
            Some("tail: 'f' has been replaced with an untailable file; giving up on this name")
        );
        assert_eq!(r.ignore, Some(true));
        // repeated untailable: silent
        let r = plan(false, false, Errnum::Untailable, Probe::Untailable, true);
        assert_eq!(r.msg, None);
        // a non-ENOENT failure that clears: "has become accessible"
        let r = plan(
            false,
            false,
            Errnum::Untailable,
            Probe::Regular((1, 9)),
            true,
        );
        assert_eq!(r.msg.as_deref(), Some("tail: 'f' has become accessible"));
        assert_eq!(r.verdict, Verdict::NewFile);
        let r = plan(
            false,
            true,
            Errnum::Err("Permission denied".into()),
            Probe::Regular((1, 9)),
            true,
        );
        assert_eq!(r.msg.as_deref(), Some("tail: 'f' has become accessible"));
    }

    #[test]
    fn quoting() {
        assert_eq!(quoteaf("f"), "'f'");
        assert_eq!(quoteaf("a b"), "'a b'");
        assert_eq!(quoteaf("it's"), "\"it's\"");
    }

    fn mi(stdin_followed: bool, open: bool, regular: bool, symlink: bool) -> ModeInput {
        ModeInput {
            stdin_followed,
            open,
            regular,
            symlink,
        }
    }

    #[test]
    fn inotify_choice_and_exit_status() {
        use FollowMode::*;
        let reg = mi(false, true, true, false);
        let missing = mi(false, false, false, false);
        assert!(gnu_uses_inotify(&[reg], true, Descriptor));
        // -f missing f: an open failed in descriptor mode -> polling
        let reg = mi(false, true, true, false);
        assert!(!gnu_uses_inotify(&[missing, reg], false, Descriptor));
        // -F missing f: inotify
        let (missing, reg) = (mi(false, false, false, false), mi(false, true, true, false));
        assert!(gnu_uses_inotify(&[missing, reg], false, Name));
        // nothing open, stdin followed, symlink, non-regular: polling
        assert!(!gnu_uses_inotify(
            &[mi(false, false, false, false)],
            false,
            Name
        ));
        assert!(!gnu_uses_inotify(
            &[mi(true, true, true, false)],
            true,
            Descriptor
        ));
        assert!(!gnu_uses_inotify(
            &[mi(false, true, true, true)],
            true,
            Descriptor
        ));
        assert!(!gnu_uses_inotify(
            &[mi(false, true, false, false)],
            true,
            Descriptor
        ));
        // probed (p1.sh): -F --pid=dead missing f -> 0; -f ... -> 1;
        // -F --pid=dead missing -> 1; --follow=name rm -> 1
        assert_eq!(end_status(true, false, End::WriterDead), 0);
        assert_eq!(end_status(false, false, End::WriterDead), 1);
        assert_eq!(end_status(false, true, End::WriterDead), 0);
        assert_eq!(end_status(true, true, End::NoFiles), 1);
        assert_eq!(end_status(false, true, End::NoFiles), 0);
    }

    fn opts(mode: FollowMode, retry: bool) -> FollowOpts {
        FollowOpts {
            mode,
            retry,
            sleep_secs: 1.0,
            pid: None,
            max_unchanged: 5,
        }
    }

    #[test]
    fn liveness_and_pipe_stdin() {
        let mut gone = Spec::new("f");
        gone.ignore = true;
        assert!(!any_live(
            &[gone],
            &opts(FollowMode::Descriptor, false),
            true
        ));
        let mut waiting = Spec::new("f");
        waiting.ignore = false;
        assert!(any_live(
            &[waiting],
            &opts(FollowMode::Descriptor, true),
            true
        ));
        let mut gone = Spec::new("f");
        gone.ignore = true;
        // -F is always live
        assert!(any_live(&[gone], &opts(FollowMode::Name, true), true));
        let mut parked = Spec::new("f");
        parked.parked = true;
        parked.ignore = true;
        assert!(any_live(&[parked], &opts(FollowMode::Name, false), true));
        let mut away = Spec::new("f");
        away.away = true;
        assert!(any_live(
            std::slice::from_ref(&away),
            &opts(FollowMode::Name, false),
            true
        ));
        // GNU's polling path has no watch to keep the name alive
        assert!(!any_live(&[away], &opts(FollowMode::Name, false), false));

        let mut pipe = Spec::new("-");
        pipe.stdin = Some(StdinKind::Pipe);
        let mut file = Spec::new("f");
        file.ignore = true; // a failed open still counts as viable
        let mut v = vec![pipe, file];
        assert_eq!(viable(&mut v), 1);
        assert!(v[0].ignore);
        let mut pipe = Spec::new("-");
        pipe.stdin = Some(StdinKind::Pipe);
        assert_eq!(viable(&mut [pipe]), 0);
        let mut con = Spec::new("-");
        con.stdin = Some(StdinKind::Console);
        assert!(con.inert() && con.has_fd());
        assert_eq!(viable(&mut [con]), 1);
    }

    #[test]
    fn wake_tick_caps_latency_but_never_spins() {
        use std::time::Duration;
        let s = |x: f64| Some(Duration::from_secs_f64(x));
        assert_eq!(wake_tick(s(1.0), 2), Some(FAST_TICK));
        assert_eq!(wake_tick(s(0.05), 2), s(0.05));
        assert_eq!(wake_tick(None, 2), Some(FAST_TICK));
        assert_eq!(wake_tick(s(1.0), 65), s(1.0));
    }
}
