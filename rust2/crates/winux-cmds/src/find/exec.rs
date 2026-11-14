//! `-exec` / `-execdir` / `-ok` / `-okdir` (findutils exec.c + buildcmd
//! port). Children launch through the shared childproc chain
//! (`resolve_child` + `build_child_command`: PATH/PATHEXT search,
//! `.cmd/.bat` via COMSPEC, `#!` scripts) with inherited stdio; the
//! command's own buffered stdout is flushed before every spawn so the
//! child's output lands after everything find printed before it.
//!
//! `{} +` batches are bounded like GNU's bc_* argument limits, adapted to
//! the Windows command-line ceiling: a batch is launched before its
//! command line would exceed 32767 UTF-16 units minus GNU's 2048-unit
//! headroom. `-execdir {} +` batches per directory (a new directory
//! launches the pending batch first, the GNU behavior probed as
//! `./d` / `./f ./e` / `./g`).

use std::io::Write;
use std::path::PathBuf;

use winux_core::{tr, Ctx};

use super::entry::Entry;
use super::printf::base_name;
use super::Run;
use crate::childproc::{build_child_command, resolve_child, ChildResolve};

const CMDLINE_MAX: usize = 32767 - 2048;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ExecKind {
    Exec,
    ExecDir,
    Ok,
    OkDir,
}

impl ExecKind {
    pub fn in_dir(self) -> bool {
        matches!(self, ExecKind::ExecDir | ExecKind::OkDir)
    }
    pub fn asks(self) -> bool {
        matches!(self, ExecKind::Ok | ExecKind::OkDir)
    }
}

#[derive(Debug)]
pub struct ExecSpec {
    pub kind: ExecKind,
    /// The command template; for `{} +` the trailing `{}` is dropped.
    pub argv: Vec<String>,
    pub multi: bool,
    pending: Vec<String>,
    pending_len: usize,
    pending_dir: Option<PathBuf>,
}

impl ExecSpec {
    pub fn new(kind: ExecKind, argv: Vec<String>, multi: bool) -> ExecSpec {
        ExecSpec {
            kind,
            argv,
            multi,
            pending: Vec::new(),
            pending_len: 0,
            pending_dir: None,
        }
    }

    fn base_len(&self) -> usize {
        self.argv
            .iter()
            .map(|a| a.encode_utf16().count() + 3)
            .sum::<usize>()
            + 260
    }

    /// Evaluate the predicate for one entry.
    pub fn eval(&mut self, run: &mut Run, e: &Entry) -> bool {
        let (path, dir) = if self.kind.in_dir() {
            let base = base_name(&e.path);
            (format!("./{base}"), Some(exec_dir(run.ctx, e)))
        } else {
            (e.path.clone(), None)
        };
        if self.multi {
            if self.pending_dir != dir && !self.pending.is_empty() {
                self.flush(run);
            }
            let add = path.encode_utf16().count() + 3;
            if !self.pending.is_empty() && self.base_len() + self.pending_len + add > CMDLINE_MAX {
                self.flush(run);
            }
            self.pending_dir = dir;
            self.pending_len += add;
            self.pending.push(path);
            return true;
        }
        let args: Vec<String> = self.argv.iter().map(|a| a.replace("{}", &path)).collect();
        if self.kind.asks() && !confirm(run, &args[0], &e.path) {
            return false;
        }
        launch(run, &args, dir, self.kind.asks()) == Some(0)
    }

    /// Launch the pending `{} +` batch (end of walk, directory change,
    /// length limit, `-quit`). A failing batch makes find exit 1.
    pub fn flush(&mut self, run: &mut Run) {
        if self.pending.is_empty() {
            return;
        }
        let mut args = self.argv.clone();
        args.append(&mut self.pending);
        self.pending_len = 0;
        let dir = self.pending_dir.take();
        if launch(run, &args, dir, false) != Some(0) {
            run.status = 1;
        }
    }
}

/// The directory -execdir runs in: the entry's parent (for a starting
/// point, the dirname of the operand as given).
fn exec_dir(ctx: &Ctx, e: &Entry) -> PathBuf {
    let strip = |w: &[u16]| -> PathBuf {
        let s = String::from_utf16_lossy(w);
        let s = if let Some(r) = s.strip_prefix(r"\\?\UNC\") {
            format!(r"\\{r}")
        } else if let Some(r) = s.strip_prefix(r"\\?\") {
            r.to_string()
        } else {
            s
        };
        PathBuf::from(s)
    };
    if e.depth > 0 {
        let cut = e
            .native
            .iter()
            .rposition(|&c| c == b'\\' as u16)
            .unwrap_or(0);
        let mut parent = strip(&e.native[..cut]);
        if parent.as_os_str().to_string_lossy().ends_with(':') {
            parent.push("\\");
        }
        return parent;
    }
    let p = e.path.trim_end_matches('/');
    match p.rfind('/') {
        Some(0) => strip(
            &crate::fs_win::imp::api_path(ctx, "/")
                .encode_utf16()
                .collect::<Vec<_>>(),
        ),
        Some(i) => {
            let dir = &e.path[..i];
            let native = crate::fs_win::imp::api_path(ctx, dir);
            strip(&native.encode_utf16().collect::<Vec<_>>())
        }
        None => ctx.cwd.clone(),
    }
}

/// `-ok` prompt on stderr, reply line from stdin ("< cmd ... path > ? ").
fn confirm(run: &mut Run, cmd: &str, path: &str) -> bool {
    let _ = run.out.flush();
    let _ = write!(
        run.err,
        "{}",
        tr!(
            run.ctx,
            "command.find.prompt.ok",
            "< {command} ... {path} > ? ",
            command = cmd,
            path = path
        )
    );
    let _ = run.err.flush();
    let mut line = Vec::new();
    let mut b = [0u8; 1];
    loop {
        match run.stdin.read(&mut b) {
            Ok(1) => {
                if b[0] == b'\n' {
                    break;
                }
                line.push(b[0]);
            }
            _ => break,
        }
    }
    matches!(line.first(), Some(b'y') | Some(b'Y'))
}

/// Spawn and wait; `None` when the child could not be started (the
/// diagnostic is printed GNU-style).
fn launch(run: &mut Run, args: &[String], dir: Option<PathBuf>, close_stdin: bool) -> Option<i32> {
    let _ = run.out.flush();
    let child_ctx = Ctx::with_host(
        dir.unwrap_or_else(|| run.ctx.cwd.clone()),
        run.ctx.env,
        run.ctx.host,
    );
    let launch = match resolve_child(&args[0], &child_ctx) {
        ChildResolve::Found(l) => l,
        other => {
            let why = match other {
                ChildResolve::NotFound => tr!(
                    run.ctx,
                    "command.find.error.exec_no_such_file",
                    "No such file or directory"
                ),
                ChildResolve::NotADirectory => {
                    tr!(
                        run.ctx,
                        "command.find.error.exec_not_a_directory",
                        "Not a directory"
                    )
                }
                ChildResolve::BadFormat => {
                    tr!(
                        run.ctx,
                        "command.find.error.exec_bad_format",
                        "Exec format error"
                    )
                }
                _ => tr!(
                    run.ctx,
                    "command.find.error.exec_permission_denied",
                    "Permission denied"
                ),
            };
            // GNU: the forked child reports the exec failure and exits
            // nonzero; `;` only makes the test false (rc stays 0), a `{} +`
            // batch failure sets rc 1 in flush().
            let _ = writeln!(run.err, "find: '{}': {why}", args[0]);
            return None;
        }
    };
    let mut cmd = build_child_command(&launch, &args[1..], &child_ctx);
    if close_stdin {
        cmd.stdin(std::process::Stdio::null());
    }
    match cmd.spawn() {
        Ok(mut child) => match child.wait() {
            Ok(st) => Some(st.code().unwrap_or(1)),
            Err(_) => Some(1),
        },
        Err(err) => {
            let _ = writeln!(
                run.err,
                "{}",
                tr!(
                    run.ctx,
                    "command.find.error.exec_spawn",
                    "find: '{command}': {reason}",
                    command = args[0],
                    reason = crate::fsutil::reason(&err)
                )
            );
            None
        }
    }
}
