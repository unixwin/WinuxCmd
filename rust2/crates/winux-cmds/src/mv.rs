//! `mv` — GNU coreutils 9.4 semantics on the Windows value model
//! (#1488 rust2 lane, WP-J; oracle: WSL `mv (GNU coreutils) 9.4`,
//! 9.4-3ubuntu6.2).
//!
//! Layout:
//! * [`opts`] — sequential getopt_long port (mv.c:336-429): last-wins
//!   `-f`/`-i`/`-n`, `--update=WHEN` rewriting interactive state under
//!   `-n` precedence, `-t`/`-T` grammar, `--backup`/`-S` bookkeeping.
//! * this file — the operand grammar (`main`, mv.c:313-524), `do_move`
//!   (mv.c:167), and the move-mode `copy_internal` ladder (copy.c:2197)
//!   with the cross-device copy+`rm` fallback (remove.c:604 `rm`).
//! * `tests` — filesystem-level unit tests over a scratch root.
//!
//! GNU owners (file:line in coreutils 9.4):
//! * `rm_option_init` mv.c:87 — recursive, never-interactive rm options.
//! * `cp_option_init` mv.c:119 — move_mode, DEREF_NEVER, preserve
//!   ownership/links/mode/timestamps/xattr, recursive, SPARSE_AUTO.
//! * `do_move` mv.c:167 — copy() then rm() for non-renames;
//!   `main` mv.c:311 — option scan, operand grammar, the 2-operand
//!   RENAME_NOREPLACE probe and dir detection, dest_info init, the
//!   per-source loop.
//! * `copy` copy.c:3405 — top-level src/dst names for diagnostics.
//! * `copy_internal` copy.c:2197 — the decision ladder: rename probe,
//!   dst stat, same-file, `--update`, `abandon_move`, type refusals,
//!   just-created destination, backup, real rename, the EINVAL
//!   subdirectory-of-itself path, EXDEV copy fallback.
//! * `same_file_ok` copy.c:1741 — link/identity rules.
//! * `overwrite_ok` copy.c:1990 and `writable_destination` copy.c:1981 —
//!   the `-i` prompt shapes.
//! * `abandon_move` copy.c:2064 — `-n`/skip/decline handling.
//! * `emit_verbose` copy.c:2084 — `'src' -> 'dst' (backup: 'dst~')`.
//! * `source_is_dst_backup` copy.c:2163 — the `a~` onto `a` refusal.
//! * `copy_dir` copy.c:824 — recursive descent for the EXDEV fallback.
//! * `create_hard_link` copy.c:2124 — hard-link group preservation.
//! * `is_ancestor` copy.c:688 — cyclic link detection.
//! * `excise` remove.c:392 — `removed 'x'` / `removed directory 'x'`
//!   verbose lines; `rm` remove.c:604 — the post-order removal walk.
//!
//! Windows decisions (cp.rs precedent):
//! * rename is `MoveFileExW`: `flags=0` is RENAME_NOREPLACE,
//!   `MOVEFILE_REPLACE_EXISTING` is renameat — cross-volume fails
//!   `ERROR_NOT_SAME_DEVICE`, the EXDEV analog that drives the
//!   copy+remove fallback (MOVEFILE_COPY_ALLOWED is deliberately NOT
//!   used so the fallback ladder runs);
//! * identity is `(volume serial, file index)` (winfs `Meta`); junctions
//!   and the WSL/LX tag count as symlinks, so `mv` moves a link as a
//!   link and a followed link to a directory opens as a directory;
//! * `is a directory` tests use the lstat view (a junction is not a
//!   directory for the type refusals), matching DEREF_NEVER;
//! * POSIX rename ignores a read-only destination bit; Win32 refuses,
//!   so an `ACCESS_DENIED` rename over a READONLY destination clears the
//!   attribute and retries (the cp.cpp precedent);
//! * `mv s s/sub` reports GNU's EINVAL text `cannot move 's' to a
//!   subdirectory of itself` when the destination's deepest existing
//!   ancestor is the source inode (Windows returns ACCESS_DENIED for
//!   the same relation — the relation check is the kernel contract);
//! * renaming `.`/`..` reports `Device or resource busy` (EBUSY), the
//!   probed GNU errno for a dot operand, where Win32 reports
//!   ACCESS_DENIED;
//! * `mv File.txt file.txt` performs a case-only rename — the
//!   same-name check compares basenames byte-exactly (the
//!   case-insensitive-enumeration / case-sensitive-matching model);
//! * mode is the read-only attribute (the `-i` unwritable prompt says
//!   `replace 'b', overriding mode 0444 (r--r--r--)? `), timestamps are
//!   FILETIMEs preserved through the fallback, ownership/context/xattr
//!   are accepted no-ops.
//!
//! 1.x C++ behaviors that are intentionally corrected here:
//! * #1649 — a directory source onto a non-directory destination is
//!   refused with `cannot overwrite non-directory 'f' with directory
//!   'd'`, rc 1, target intact (the C++ silently replaced the file);
//! * the wording of the into-itself diagnostic is GNU's
//!   `cannot move 'src' to a subdirectory of itself, 'dst'` (the C++
//!   says `cannot copy a directory ... into itself`);
//! * `-n` refuses with `not replacing 'dst'` and rc 1, and `-i`
//!   refusal also returns 1;
//! * `-f`/`-i`/`-n` are last-wins in argv order (#1651 family);
//! * `--exchange` does not exist in GNU 9.4 — the 1.x C++ accepted a
//!   9.5-style `--exchange`; this port reports `unrecognized option`.

pub mod opts;

#[cfg(test)]
mod tests;

use std::collections::HashMap;
use std::ffi::OsString;
use std::io::Write;

use winux_core::{tr, CmdResult, Ctx, Io, StdioMeta};

use crate::cp::winfs::{self, Meta};
use crate::fsutil;
use crate::modemap;

use opts::{q, Backup, Interactive, Options, UsageError};

/// `mv` entry point. The driver's stdin is never a terminal, so the
/// library face composes with `StdioMeta::pipe()` (the test_cmd.rs
/// precedent); `mv_with_meta` is the seam the wired dispatcher uses.
pub fn mv_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    run(args, io, ctx, &StdioMeta::pipe())
}

pub fn mv_with_meta(args: &[OsString], io: &mut Io, ctx: &Ctx, stdio: &StdioMeta) -> CmdResult {
    run(args, io, ctx, stdio)
}

/// Win32/GNU errno classes the move ladder distinguishes. `reason` is
/// GNU's C-locale strerror spelling, the inner `error()` wording.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum MErr {
    NoEnt,
    Exists,
    Perm,
    NotDir,
    NotEmpty,
    Busy,
    Inval,
    NoSpace,
    XDev,
    Loop,
    NameTooLong,
    OpNotPerm,
}

impl MErr {
    fn reason(self) -> &'static str {
        match self {
            MErr::NoEnt => "No such file or directory",
            MErr::Exists => "File exists",
            MErr::Perm => "Permission denied",
            MErr::NotDir => "Not a directory",
            MErr::NotEmpty => "Directory not empty",
            MErr::Busy => "Device or resource busy",
            MErr::Inval => "Invalid argument",
            MErr::NoSpace => "No space left on device",
            MErr::XDev => "Invalid cross-device link",
            MErr::Loop => "Too many levels of symbolic links",
            MErr::NameTooLong => "File name too long",
            MErr::OpNotPerm => "Operation not permitted",
        }
    }

    /// The Win32 code the class came from (the probe returns raw errnos
    /// to the ladder, so a synthesized class has to name one).
    fn code(self) -> u32 {
        match self {
            MErr::NoEnt => 2,
            MErr::Exists => 80,
            MErr::Perm => 5,
            MErr::NotDir => 267,
            MErr::NotEmpty => 145,
            MErr::Busy => 32,
            MErr::Inval => 87,
            MErr::NoSpace => 112,
            MErr::XDev => 17,
            MErr::Loop => 1921,
            MErr::NameTooLong => 206,
            MErr::OpNotPerm => 1314,
        }
    }
}

/// Win32 error -> the errno class the ladder branches on (the
/// winfs::reason superset mv needs).
fn classify(err: u32) -> MErr {
    match err {
        2 | 3 | 1920 | 2062 | 161 | 123 => MErr::NoEnt,
        5 | 32 | 33 => MErr::Perm,
        80 | 183 => MErr::Exists,
        267 => MErr::NotDir,
        145 => MErr::NotEmpty,
        17 => MErr::XDev,
        112 => MErr::NoSpace,
        206 => MErr::NameTooLong,
        1921 => MErr::Loop,
        1314 | 1 => MErr::OpNotPerm,
        _ => MErr::Inval,
    }
}

fn run(args: &[OsString], io: &mut Io, ctx: &Ctx, stdio: &StdioMeta) -> CmdResult {
    let (mut opts, mut files) = match opts::parse(args, ctx) {
        Ok(v) => v,
        Err(UsageError(msg, _)) => {
            let _ = writeln!(io.stderr, "mv: {msg}");
            return Ok(1);
        }
    };
    if opts.help || opts.version {
        // The product dispatcher owns the branded help/version surface;
        // the standalone driver only has to exit 0 (cp precedent).
        return Ok(0);
    }

    // ---- operand grammar (mv.c:429-487) ----
    // `n_files <= !target_directory`.
    if files.len() <= usize::from(opts.target_directory.is_none()) {
        let msg = if files.is_empty() {
            tr!(
                ctx,
                "command.mv.error.missing_file_operand",
                "missing file operand"
            )
        } else {
            tr!(
                ctx,
                "command.mv.error.missing_destination",
                "missing destination file operand after {operand}",
                operand = q(&files[0])
            )
        };
        let _ = writeln!(io.stderr, "mv: {msg}");
        let _ = writeln!(io.stderr, "Try 'mv --help' for more information.");
        return Ok(1);
    }

    let mut target_directory: Option<String> = None;
    let mut probe_errno: i32 = -1;

    if opts.no_target_directory {
        // mv.c:439: error (EXIT_FAILURE), no usage tail.
        if opts.target_directory.is_some() {
            let msg = tr!(
                ctx,
                "command.mv.error.target_dir_and_no_target_dir",
                "cannot combine --target-directory (-t) and --no-target-directory (-T)"
            );
            let _ = writeln!(io.stderr, "mv: {msg}");
            return Ok(1);
        }
        if files.len() > 2 {
            let msg = tr!(
                ctx,
                "command.mv.error.extra_operand",
                "extra operand {operand}",
                operand = q(&files[2])
            );
            let _ = writeln!(io.stderr, "mv: {msg}");
            let _ = writeln!(io.stderr, "Try 'mv --help' for more information.");
            return Ok(1);
        }
    } else if let Some(td) = opts.target_directory.clone() {
        // target_directory_operand (mv.c:452): the error carries the
        // open errno and no usage tail.
        let m = Mover::new(ctx, &opts, io, stdio);
        match m.is_dir_follow(&td) {
            Ok(true) => target_directory = Some(td),
            Ok(false) => {
                let msg = tr!(
                    ctx,
                    "command.mv.error.target_directory",
                    "target directory {dir}: {reason}",
                    dir = q(&td),
                    reason = "Not a directory"
                );
                let _ = writeln!(io.stderr, "mv: {msg}");
                return Ok(1);
            }
            Err(e) => {
                let msg = tr!(
                    ctx,
                    "command.mv.error.target_directory",
                    "target directory {dir}: {reason}",
                    dir = q(&td),
                    reason = e.reason()
                );
                let _ = writeln!(io.stderr, "mv: {msg}");
                return Ok(1);
            }
        }
    } else {
        let lastfile = files.last().unwrap().clone();
        if files.len() == 2 {
            // mv.c:459: the RENAME_NOREPLACE probe — on success the file
            // is already moved and copy() reports it.
            let m = Mover::new(ctx, &opts, io, stdio);
            probe_errno = match m.probe_noreplace(&files[0], &lastfile) {
                Ok(()) => 0,
                Err(e) => e as i32,
            };
        }
        if probe_errno != 0 {
            let m = Mover::new(ctx, &opts, io, stdio);
            if m.is_dir_follow(&lastfile).unwrap_or(false) {
                // dir mode: the last operand is the target directory.
                probe_errno = -1;
                target_directory = Some(lastfile);
                files.pop();
            } else if files.len() > 2 {
                let reason = m.dir_error(&lastfile);
                let msg = tr!(
                    ctx,
                    "command.mv.error.target",
                    "target {operand}: {reason}",
                    operand = q(&lastfile),
                    reason = reason
                );
                let _ = writeln!(io.stderr, "mv: {msg}");
                return Ok(1);
            }
        }
    }

    // mv.c:487: --strip-trailing-slashes rewrites every remaining
    // file[] operand — in the two-operand form that includes the
    // destination spelling (probed `mv --strip a nodir/` reports
    // 'nodir').
    if opts.strip_trailing_slashes {
        for f in files.iter_mut() {
            let stripped = f.trim_end_matches(['/', '\\']);
            if !stripped.is_empty() {
                *f = stripped.to_string();
            }
        }
    }

    // mv.c:491/494 — already applied -n -> update in opts::parse; the
    // mutual exclusion is a usage() error.
    if opts.make_backups && opts.interactive == Interactive::AlwaysNo {
        let msg = tr!(
            ctx,
            "command.mv.error.backup_and_no_clobber",
            "options --backup and --no-clobber are mutually exclusive"
        );
        let _ = writeln!(io.stderr, "mv: {msg}");
        let _ = writeln!(io.stderr, "Try 'mv --help' for more information.");
        return Ok(1);
    }

    // mv.c:501 xget_version ("backup type") — after the operand grammar.
    if opts.make_backups {
        match &opts.version_control {
            None => opts.backup = Backup::Existing,
            Some(v) => match opts::backup_value(ctx, v) {
                Ok(b) => opts.backup = b,
                Err(UsageError(msg, _)) => {
                    let _ = writeln!(io.stderr, "mv: {msg}");
                    return Ok(1);
                }
            },
        }
        if let Some(s) = &opts.backup_suffix {
            if s.is_empty() {
                // probed `mv -S ''` -> the `~` default.
                opts.backup_suffix = None;
            }
        }
    }

    let mut mover = Mover::new(ctx, &opts, io, stdio);
    // mv.c:507: dest_info exists only for a 2+-operand directory target.
    mover.dest_info = target_directory.is_some() && files.len() >= 2;

    let ok = match &target_directory {
        Some(td) => {
            let mut ok = true;
            for (i, src) in files.iter().enumerate() {
                let last_file = i + 1 == files.len();
                // file_name_concat keeps the basename's own trailing
                // slashes in dest; only dest_relname is stripped.
                let base_raw = gnu_last_component(src);
                let relname = base_raw.trim_end_matches(['/', '\\']).to_string();
                let relname = if relname.is_empty() {
                    base_raw.clone()
                } else {
                    relname
                };
                let dest = join(td, &base_raw);
                ok &= mover.do_move(src, &dest, &relname, probe_errno, last_file, true);
            }
            ok
        }
        // mv.c:554: the two-operand form is a single do_move with
        // last_file always set.
        None => mover.do_move(&files[0], &files[1], &files[1], probe_errno, true, true),
    };
    Ok(if ok { 0 } else { 1 })
}

/// gnulib `last_component`: the last component, keeping its trailing
/// slashes ("a/b/" -> "b/"). mv.c feeds this into file_name_concat.
fn gnu_last_component(path: &str) -> String {
    let bytes = path.as_bytes();
    let sep = |b: u8| b == b'/' || b == b'\\';
    let mut start = 0usize;
    for (i, &b) in bytes.iter().enumerate() {
        if !sep(b) && (i == 0 || sep(bytes[i - 1])) {
            start = i;
        }
    }
    if path[start..].chars().all(|c| c == '/' || c == '\\') && start > 0 {
        return path.to_string();
    }
    path[start..].to_string()
}

/// GNU `last_component`: the final component with trailing slashes
/// removed (".." collapses to "." like cp's copy::last_component).
fn last_component(path: &str) -> String {
    let trimmed = path.trim_end_matches(['/', '\\']);
    if trimmed.is_empty() {
        return path.to_string();
    }
    match trimmed.rfind(['/', '\\']) {
        Some(i) => {
            let base = &trimmed[i + 1..];
            if base == ".." {
                ".".to_string()
            } else {
                base.to_string()
            }
        }
        None => trimmed.to_string(),
    }
}

/// file_name_concat: join a target directory and a basename with the `/`
/// display separator (Win32 accepts forward slashes).
fn join(base: &str, name: &str) -> String {
    if base.is_empty() {
        return name.to_string();
    }
    if base.ends_with(['/', '\\']) {
        format!("{base}{name}")
    } else {
        format!("{base}/{name}")
    }
}

#[derive(Default)]
struct Outcome {
    ok: bool,
    into_self: bool,
    renamed: bool,
}

struct Mover<'a, 'b, 'c> {
    ctx: &'a Ctx<'c>,
    opts: &'a Options,
    io: &'a mut Io<'b>,
    stdio: &'a StdioMeta,
    /// `src_info`'s counterpart for destinations: remember_copied /
    /// src_to_dest_lookup (inode -> first destination relname).
    src_to_dest: HashMap<(u32, u64), String>,
    /// `dest_info`: destination names this run created (relname -> id).
    created: HashMap<String, (u32, u64)>,
    dest_info: bool,
}

impl<'a, 'b, 'c> Mover<'a, 'b, 'c> {
    fn new(ctx: &'a Ctx<'c>, opts: &'a Options, io: &'a mut Io<'b>, stdio: &'a StdioMeta) -> Self {
        Mover {
            ctx,
            opts,
            io,
            stdio,
            src_to_dest: HashMap::new(),
            created: HashMap::new(),
            dest_info: false,
        }
    }

    fn say(&mut self, msg: &str) {
        let _ = writeln!(self.io.stderr, "mv: {msg}");
    }

    fn fail(&mut self, msg: &str) {
        self.say(msg);
    }

    // ------------------------------------------------------------------
    // path / metadata helpers
    // ------------------------------------------------------------------

    /// Absolute native path of an operand (cp::copy::api_for — POSIX
    /// spellings, /mnt folding, DOS devices). The empty operand stays
    /// empty so it never folds onto the cwd (#1650 rule).
    fn api(&self, name: &str) -> String {
        if name.is_empty() {
            return String::new();
        }
        if let Some(dev) = winux_core::path::resolve_pseudo_device(name) {
            return dev.to_string();
        }
        winfs::extended_path(&fsutil::resolve(self.ctx, name).to_string_lossy())
    }

    /// Destination api path: file_name_concat can leave a trailing
    /// slash on dest ("t/d/"), which Win32 renames refuse — POSIX
    /// resolves it to the directory name, so the api path is stripped.
    fn api_dst(&self, name: &str) -> String {
        if name.len() > 1 && (name.ends_with('/') || name.ends_with('\\')) {
            let s = name.trim_end_matches(['/', '\\']);
            if !s.is_empty() {
                return self.api(s);
            }
        }
        self.api(name)
    }

    /// lstat view of a spelled operand. A trailing separator keeps the
    /// POSIX contract: `'d/'` on a directory stats the directory,
    /// `'a/'` on a file is ENOTDIR, and a missing prefix is ENOENT.
    /// Win32 tolerates the separator (`GetFileAttributesW("a\\")` is the
    /// file), so the spelling is enforced here before the handle open.
    fn lstat(&self, name: &str) -> Result<Meta, MErr> {
        if has_trailing_sep(name) {
            return self.trailing_sep_lookup(name);
        }
        let api = self.api(name);
        winfs::meta(&winfs::expand_lx_parent(self.ctx, &api), false).map_err(classify)
    }

    /// stat view (links followed through the LX reader).
    fn stat(&self, name: &str) -> Result<Meta, MErr> {
        if has_trailing_sep(name) {
            return self.trailing_sep_lookup(name);
        }
        let api = self.api(name);
        winfs::meta_follow(self.ctx, &api).map_err(classify)
    }

    /// POSIX resolves a trailing separator through the directory entry:
    /// missing is ENOENT, and anything but a directory (link included —
    /// the trailing slash forces the follow) is ENOTDIR.
    fn trailing_sep_lookup(&self, name: &str) -> Result<Meta, MErr> {
        let stripped = name.trim_end_matches(['/', '\\']);
        if stripped.is_empty() {
            return Err(MErr::NoEnt);
        }
        match winfs::meta_follow(self.ctx, &self.api(stripped)) {
            Ok(m) if m.is_dir() => Ok(m),
            Ok(_) => Err(MErr::NotDir),
            Err(e) => Err(classify(e)),
        }
    }

    /// `open(O_DIRECTORY)` for target_directory_operand: a followed
    /// directory link counts as a directory.
    fn is_dir_follow(&self, name: &str) -> Result<bool, MErr> {
        if name.is_empty() {
            return Err(MErr::NoEnt);
        }
        match self.stat(name) {
            Ok(m) => Ok(m.is_dir()),
            Err(e) => Err(e),
        }
    }

    /// The `target %s` reason: ENOENT when the name does not resolve at
    /// all, ENOTDIR when it exists as a non-directory.
    fn dir_error(&self, name: &str) -> &'static str {
        match self.stat(name) {
            Ok(_) => "Not a directory",
            Err(e) => e.reason(),
        }
    }

    /// A source API path: `d/` on a real directory resolves to `d`
    /// (GNU's rename honors a source trailing slash on directories);
    /// `a/` on a file keeps the slash so the operation fails ENOTDIR.
    fn api_src(&self, name: &str) -> String {
        if name.len() > 1 && (name.ends_with('/') || name.ends_with('\\')) {
            let stripped = name.trim_end_matches(['/', '\\']);
            if self.stat(stripped).map(|m| m.is_dir()).unwrap_or(false) {
                return self.api(stripped);
            }
        }
        self.api(name)
    }

    fn lexists(&self, name: &str) -> bool {
        winfs::attributes(&self.api(name)).is_some()
    }

    /// renameat2(RENAME_NOREPLACE) with the POSIX spelling rules Win32
    /// does not implement.
    ///
    /// * An existing destination is EEXIST — even when it is the very
    ///   same path: `MoveFileExW(a, a, 0)` reports success, which is how
    ///   `mv a a` (and a hard-linked pair) slipped through the ladder
    ///   without the same-file diagnostic.
    /// * A trailing separator resolves through the directory entry:
    ///   ENOTDIR for a non-directory, ENOENT when nothing is there. Win32
    ///   silently strips it (`MoveFileExW(a, "b\\", 0)` renames), so the
    ///   probe has to model the POSIX lookup itself.
    fn probe_noreplace(&self, src: &str, dst: &str) -> Result<(), u32> {
        // renameat2 on a dot operand is EBUSY (probed `mv . x`: "cannot
        // move '.' to 'x': Device or resource busy"); never touch it.
        if is_dot_operand(src) || is_dot_operand(dst) {
            return Err(MErr::Busy.code());
        }
        if has_trailing_sep(src) {
            if let Err(e) = self.trailing_sep_lookup(src) {
                return Err(e.code());
            }
        }
        if has_trailing_sep(dst) {
            match self.lstat(dst) {
                Ok(m) if m.is_dir() => {}
                Ok(_) => return Err(MErr::NotDir.code()),
                // rename("dir", "new/") is legal — the trailing separator
                // only promises the result is a directory — while a
                // non-directory source is ENOTDIR.
                Err(MErr::NoEnt) => {
                    if !self.stat(src).map(|m| m.is_dir()).unwrap_or(false) {
                        return Err(MErr::NotDir.code());
                    }
                }
                Err(e) => return Err(e.code()),
            }
        }
        if self.lexists(dst) {
            return Err(ERROR_FILE_EXISTS as u32);
        }
        winfs::rename_noreplace(&self.api_src(src), &self.api_dst(dst))
    }

    /// same_nameat: identical parent directory and an equal basename
    /// spelling. The basename comparison is byte-exact (the
    /// case-insensitive-enumeration / case-sensitive-matching rule, so
    /// `mv File.txt file.txt` is a real case rename).
    fn same_entry(&self, a: &str, b: &str) -> bool {
        let pa = self.api(a);
        let pb = self.api(b);
        let (dir_a, base_a) = split_dir_base(&pa);
        let (dir_b, base_b) = split_dir_base(&pb);
        if base_a != base_b {
            return false;
        }
        if dir_a.eq_ignore_ascii_case(&dir_b) {
            return true;
        }
        match (
            winfs::meta_follow(self.ctx, &dir_a),
            winfs::meta_follow(self.ctx, &dir_b),
        ) {
            (Ok(ma), Ok(mb)) => ma.id() == mb.id(),
            _ => false,
        }
    }

    /// Two spellings of one directory entry that differ only in ASCII
    /// case (NTFS is case-insensitive for lookup, case-preserving for
    /// display): `same_entry` is byte-exact, this is the rename case.
    fn same_dir_case_only(&self, a: &str, b: &str) -> bool {
        let pa = self.api(a);
        let pb = self.api(b);
        let (dir_a, base_a) = split_dir_base(&pa);
        let (dir_b, base_b) = split_dir_base(&pb);
        if base_a == base_b || !base_a.eq_ignore_ascii_case(&base_b) {
            return false;
        }
        if dir_a.eq_ignore_ascii_case(&dir_b) {
            return true;
        }
        match (
            winfs::meta_follow(self.ctx, &dir_a),
            winfs::meta_follow(self.ctx, &dir_b),
        ) {
            (Ok(ma), Ok(mb)) => ma.id() == mb.id(),
            _ => false,
        }
    }

    // ------------------------------------------------------------------
    // do_move (mv.c:167) and copy_internal (copy.c:2197)
    // ------------------------------------------------------------------

    fn do_move(
        &mut self,
        src: &str,
        dst: &str,
        relname: &str,
        probe_errno: i32,
        last_file: bool,
        cmdline: bool,
    ) -> bool {
        let mut first_dir_created = false;
        let mut ancestors = Vec::new();
        let o = self.copy_internal(
            src,
            dst,
            relname,
            0,
            None,
            &mut ancestors,
            cmdline,
            &mut first_dir_created,
            probe_errno,
            last_file,
            src,
            dst,
        );
        if !o.ok {
            return false;
        }
        if o.into_self {
            return false;
        }
        if o.renamed {
            return true;
        }
        // mv.c:220: the copy+remove fallback — rm the source tree.
        self.rm_source(src)
    }

    #[allow(clippy::too_many_arguments)]
    fn copy_internal(
        &mut self,
        src_name: &str,
        dst_name: &str,
        dst_relname: &str,
        nonexistent_dst: i32,
        _parent: Option<&Meta>,
        ancestors: &mut Vec<(u32, u64)>,
        cmdline: bool,
        first_dir_created: &mut bool,
        probe_errno: i32,
        last_file: bool,
        top_src: &str,
        top_dst: &str,
    ) -> Outcome {
        let mut out = Outcome {
            ok: false,
            into_self: false,
            renamed: false,
        };
        let drelname = if dst_relname.is_empty() {
            "."
        } else {
            dst_relname
        };
        // drelname "." (the empty destination operand) addresses the cwd.
        let dst_api_name = if drelname == "." && dst_name.is_empty() {
            "."
        } else {
            dst_name
        };
        let mut dst_backup: Option<String> = None;

        let mut rename_errno = probe_errno;
        if rename_errno < 0 {
            rename_errno = match self.probe_noreplace(src_name, dst_api_name) {
                Ok(()) => 0,
                Err(e) => e as i32,
            };
        }
        // move_mode: the probe (or the caller's probe) already renamed the
        // file. GNU leaves copy_internal at this point — the destination
        // name now holds the source inode, so re-stat'ing it as a
        // collision would run the overwrite ladder against the move's own
        // result and fail with ENOENT on the vanished source.
        if rename_errno == 0 {
            if self.opts.verbose {
                self.emit_verbose("renamed ", src_name, dst_name, None);
            }
            if cmdline && !last_file && self.dest_info {
                if let Ok(dm) = self.lstat(dst_api_name) {
                    self.created.insert(drelname.to_string(), dm.id());
                }
            }
            out.renamed = true;
            out.ok = true;
            return out;
        }
        let mut new_dst = matches!(classify(rename_errno as u32), MErr::NoEnt);

        // ---- source stat (copy.c:2243) ----
        // Unconditional: `mv -n nosuch b` still reports `cannot stat`,
        // and `-n`/`--update=none` refuse only after the source is known.
        let src_sb = match self.lstat(src_name) {
            Ok(m) => Some(m),
            Err(e) => {
                let m = tr!(
                    self.ctx,
                    "command.mv.error.cannot_stat",
                    "cannot stat {src}: {reason}",
                    src = q(src_name),
                    reason = e.reason()
                );
                self.fail(&m);
                return out;
            }
        };
        let src_is_dir = src_sb.as_ref().map(|m| m.is_dir()).unwrap_or(false);

        // ---- destination stat (copy.c:2300) ----
        let mut dst_sb: Option<Meta> = None;
        if nonexistent_dst <= 0 {
            match self.lstat(dst_api_name) {
                Ok(m) => {
                    dst_sb = Some(m);
                    rename_errno = ERROR_FILE_EXISTS;
                }
                Err(MErr::NoEnt) => new_dst = true,
                Err(e) => {
                    let m = tr!(
                        self.ctx,
                        "command.mv.error.cannot_stat",
                        "cannot stat {src}: {reason}",
                        src = q(dst_name),
                        reason = e.reason()
                    );
                    self.fail(&m);
                    return out;
                }
            }

            if let Some(dst_m) = dst_sb.clone() {
                let src_m = src_sb.clone().unwrap_or_default();

                // ---- same_file_ok (copy.c:1741) ----
                if !matches!(
                    self.opts.interactive,
                    Interactive::AlwaysNo | Interactive::AlwaysSkip
                ) && !self.same_file_ok(src_name, &src_m, dst_api_name, &dst_m)
                {
                    let m = tr!(
                        self.ctx,
                        "command.mv.error.same_file",
                        "{src} and {dst} are the same file",
                        src = q(src_name),
                        dst = q(dst_name)
                    );
                    self.fail(&m);
                    return out;
                }

                let mut skipped = false;
                let mut return_val = true;

                // ---- --update (copy.c:2352) ----
                if self.opts.update && !src_is_dir {
                    if dst_m.mtime >= src_m.mtime {
                        out.renamed = true; // pretend rename succeeded
                        if let Some(earlier) = self.remember_copied(drelname, src_m.id()) {
                            if !self.create_hard_link(&earlier, dst_name, drelname) {
                                return self.un_backup(
                                    out,
                                    dst_name,
                                    drelname,
                                    &mut dst_backup,
                                    src_m.id(),
                                );
                            }
                        }
                        skipped = true;
                    }
                }

                // ---- abandon_move (copy.c:2064) ----
                if !skipped && self.abandon_move(dst_name, &dst_m) {
                    out.renamed = true;
                    skipped = true;
                    return_val = self.opts.interactive == Interactive::AlwaysSkip;
                }

                if skipped {
                    if self.opts.interactive == Interactive::AlwaysNo {
                        let m = tr!(
                            self.ctx,
                            "command.mv.error.not_replacing",
                            "not replacing {dst}",
                            dst = q(dst_name)
                        );
                        self.fail(&m);
                    } else if self.opts.debug {
                        let m = tr!(
                            self.ctx,
                            "command.mv.debug.skipped",
                            "skipped {dst}",
                            dst = q(dst_name)
                        );
                        let _ = writeln!(self.io.stdout, "{m}");
                    }
                    out.ok = return_val;
                    return out;
                }

                let dst_is_dir = dst_m.is_dir() && !dst_m.is_link();

                // ---- type refusals (copy.c:2438-2503) ----
                if !dst_is_dir && src_is_dir && self.opts.backup == Backup::None {
                    // #1649: never let a directory move flatten a
                    // non-directory destination.
                    let m = tr!(
                        self.ctx,
                        "command.mv.error.overwrite_nondir_with_dir",
                        "cannot overwrite non-directory {dst} with directory {src}",
                        dst = q(dst_name),
                        src = q(src_name)
                    );
                    self.fail(&m);
                    return out;
                }
                if !src_is_dir && dst_is_dir {
                    if self.opts.backup == Backup::None {
                        let m = tr!(
                            self.ctx,
                            "command.mv.error.overwrite_dir_with_nondir",
                            "cannot overwrite directory {dst} with non-directory",
                            dst = q(dst_name)
                        );
                        self.fail(&m);
                        return out;
                    }
                }
                // ---- just-created destination (copy.c:2470-2485) ----
                // The second `mv a/f b/f d` operand must not replace the
                // destination the first operand just created.
                if cmdline
                    && self.opts.backup != Backup::Numbered
                    && self.dest_info
                    && self.created.get(drelname) == Some(&dst_m.id())
                {
                    let m = tr!(
                        self.ctx,
                        "command.mv.error.will_not_overwrite_just_created",
                        "will not overwrite just-created {dst} with {src}",
                        dst = q(dst_name),
                        src = q(src_name)
                    );
                    self.fail(&m);
                    return out;
                }

                // ---- backup (copy.c:2507) ----
                // mv is always move_mode, so a directory destination is
                // backed up too: `mv -bT f d` renames `d` to `d~` and moves
                // `f` onto `d` (probed GNU 9.4).
                let srcbase = last_component(src_name);
                if self.opts.backup != Backup::None && srcbase != "." && srcbase != ".." {
                    if self.opts.backup != Backup::Numbered
                        && self.source_is_dst_backup(&srcbase, &src_m, dst_name, drelname)
                    {
                        let m = tr!(
                            self.ctx,
                            "command.mv.error.backing_up_might_destroy",
                            "backing up {dst} might destroy source;  {src} not moved",
                            dst = q(dst_name),
                            src = q(src_name)
                        );
                        self.fail(&m);
                        return out;
                    }
                    match self.rename_backup(dst_name) {
                        Ok(Some(name)) => {
                            dst_backup = Some(name);
                            new_dst = true;
                        }
                        Ok(None) => {}
                        Err(e) => {
                            let m = tr!(
                                self.ctx,
                                "command.mv.error.cannot_backup",
                                "cannot backup {dst}: {reason}",
                                dst = q(dst_name),
                                reason = e.reason()
                            );
                            self.fail(&m);
                            return out;
                        }
                    }
                }
            }
        }

        // ---- dest dev/ino bookkeeping (copy.c:2660) ----
        let mut earlier_file: Option<String> = None;
        if rename_errno != 0 {
            if src_is_dir {
                earlier_file = if cmdline {
                    self.remember_copied(drelname, src_m_or(&src_sb).id())
                } else {
                    self.src_to_dest.get(&src_m_or(&src_sb).id()).cloned()
                };
            } else {
                let id = src_m_or(&src_sb).id();
                if src_m_or(&src_sb).links == 1 {
                    earlier_file = self.src_to_dest.get(&id).cloned();
                } else {
                    earlier_file = self.remember_copied(drelname, id);
                }
            }
        }

        if let Some(earlier) = earlier_file.clone() {
            if src_is_dir {
                let earlier_full = join(&self.dest_dir_prefix(dst_name, drelname), &earlier);
                if self.same_entry(src_name, &earlier_full) {
                    let m = tr!(
                        self.ctx,
                        "command.mv.error.copy_into_itself",
                        "cannot copy a directory, {src}, into itself, {dst}",
                        src = q(top_src),
                        dst = q(top_dst)
                    );
                    self.fail(&m);
                    out.into_self = true;
                    return self.un_backup(
                        out,
                        dst_name,
                        drelname,
                        &mut dst_backup,
                        src_m_or(&src_sb).id(),
                    );
                } else if self.same_entry(dst_api_name, &earlier_full) {
                    let m = tr!(
                        self.ctx,
                        "command.mv.warning.source_dir_twice",
                        "warning: source directory {src} specified more than once",
                        src = q(top_src)
                    );
                    self.say(&m);
                    out.renamed = true;
                    out.ok = true;
                    return out;
                } else {
                    let m = tr!(
                        self.ctx,
                        "command.mv.error.hard_link_to_dir",
                        "will not create hard link {dst} to directory {src}",
                        dst = q(dst_name),
                        src = q(&earlier_full)
                    );
                    self.fail(&m);
                    return self.un_backup(
                        out,
                        dst_name,
                        drelname,
                        &mut dst_backup,
                        src_m_or(&src_sb).id(),
                    );
                }
            } else {
                if !self.create_hard_link(&earlier, dst_name, drelname) {
                    return self.un_backup(
                        out,
                        dst_name,
                        drelname,
                        &mut dst_backup,
                        src_m_or(&src_sb).id(),
                    );
                }
                out.ok = true;
                return out;
            }
        }

        // ---- the real rename (copy.c:2755) ----
        // A destination that is a subdirectory of the source is the
        // kernel's EINVAL. Checked before the rename so the empty-dir
        // replacement below can never delete part of the source.
        if src_is_dir && !is_dot_operand(src_name) && self.dst_inside_src(src_name, dst_api_name) {
            let m = tr!(
                self.ctx,
                "command.mv.error.move_into_itself",
                "cannot move {src} to a subdirectory of itself, {dst}",
                src = q(top_src),
                dst = q(top_dst)
            );
            self.fail(&m);
            out.into_self = true;
            out.ok = true;
            return out;
        }
        // POSIX rename(2) replaces an existing empty directory; Win32
        // refuses every directory destination with ACCESS_DENIED, so the
        // empty destination is removed first (a non-empty one stays to
        // fail the rename as ENOTEMPTY). Only an existing destination is
        // replaced: when the noreplace probe failed and nothing is there,
        // GNU does not retry the rename — the probe's errno is the
        // operation's outcome (`mv --strip a nodir/` reports ENOTDIR
        // instead of quietly succeeding on the stripped name).
        if dst_sb.is_some() {
            if src_is_dir {
                if let Some(dm) = dst_sb.as_ref() {
                    if dm.is_dir() && !dm.is_link() && self.dir_is_empty(dst_api_name) {
                        let _ = winfs::delete(&self.api_dst(dst_api_name), true);
                    }
                }
            }
            rename_errno = match self.rename_replace(src_name, dst_api_name) {
                Ok(()) => 0,
                Err(e) => e as i32,
            };
            if rename_errno == 0 {
                if self.opts.verbose {
                    self.emit_verbose("renamed ", src_name, dst_name, dst_backup.as_deref());
                }
                out.renamed = true;
                if cmdline && !last_file && self.dest_info {
                    self.created
                        .insert(drelname.to_string(), src_m_or(&src_sb).id());
                }
                out.ok = true;
                return out;
            }
        }

        // ---- subdirectory-of-itself (copy.c:2797, kernel EINVAL) ----
        // (checked before the rename: see above)

        let mut eclass = self.rename_err_class(rename_errno, src_name, dst_api_name);
        // Win32 reports ACCESS_DENIED/ALREADY_EXISTS for a directory
        // destination; POSIX rename(2) says ENOTEMPTY.
        if src_is_dir
            && dst_sb
                .as_ref()
                .map(|m| m.is_dir() && !m.is_link())
                .unwrap_or(false)
            && matches!(eclass, MErr::Exists | MErr::Perm | MErr::NotEmpty)
        {
            eclass = MErr::NotEmpty;
        }
        if eclass != MErr::XDev || self.opts.no_copy {
            let dst_problem = matches!(eclass, MErr::Exists | MErr::NoSpace | MErr::NotEmpty);
            let m = if dst_problem {
                tr!(
                    self.ctx,
                    "command.mv.error.cannot_overwrite",
                    "cannot overwrite {dst}: {reason}",
                    dst = q(dst_name),
                    reason = eclass.reason()
                )
            } else {
                tr!(
                    self.ctx,
                    "command.mv.error.cannot_move",
                    "cannot move {src} to {dst}: {reason}",
                    src = q(src_name),
                    dst = q(dst_name),
                    reason = eclass.reason()
                )
            };
            self.fail(&m);
            return out;
        }

        // ---- EXDEV fallback (copy.c:2836) ----
        {
            let is_dir = src_is_dir;
            if self.lexists(dst_api_name) {
                if let Err(e) = winfs::delete(&self.api_dst(dst_api_name), is_dir) {
                    if classify(e) != MErr::NoEnt {
                        let m = tr!(
                            self.ctx,
                            "command.mv.error.interdevice_move_failed",
                            "inter-device move failed: {src} to {dst}; unable to remove target",
                            src = q(src_name),
                            dst = q(dst_name)
                        );
                        self.fail(&m);
                        return out;
                    }
                }
            }
            if self.opts.verbose && !src_is_dir {
                self.emit_verbose("copied ", src_name, dst_name, dst_backup.as_deref());
            }
            new_dst = true;
        }

        // ---- the copy machinery (copy.c:2920+) ----
        let src_m = src_m_or(&src_sb);
        let mut delayed_ok = true;
        if src_is_dir {
            if ancestors.contains(&src_m.id()) {
                let m = tr!(
                    self.ctx,
                    "command.mv.error.cyclic_link",
                    "cannot copy cyclic symbolic link {src}",
                    src = q(src_name)
                );
                self.fail(&m);
                return self.un_backup(out, dst_name, drelname, &mut dst_backup, src_m.id());
            }
            ancestors.push(src_m.id());

            if new_dst || !dst_sb.as_ref().map(|m| m.is_dir()).unwrap_or(false) {
                if let Err(e) = winfs::ensure_dir(&self.api_dst(dst_api_name)) {
                    let m = tr!(
                        self.ctx,
                        "command.mv.error.cannot_create_directory",
                        "cannot create directory {dst}: {reason}",
                        dst = q(dst_name),
                        reason = classify(e).reason()
                    );
                    self.fail(&m);
                    ancestors.pop();
                    return self.un_backup(out, dst_name, drelname, &mut dst_backup, src_m.id());
                }
                if !*first_dir_created {
                    // remember_copied records the just-created
                    // destination directory's own inode (copy.c:3007).
                    if let Ok(dm) = self.lstat(dst_api_name) {
                        self.src_to_dest.insert(dm.id(), drelname.to_string());
                    }
                    *first_dir_created = true;
                }
                if self.opts.verbose {
                    let m = tr!(
                        self.ctx,
                        "command.mv.verbose.created_directory",
                        "created directory {dst}",
                        dst = q(dst_name)
                    );
                    let _ = writeln!(self.io.stdout, "{m}");
                }
            }

            delayed_ok = self.copy_dir(
                src_name,
                dst_name,
                drelname,
                new_dst,
                &src_m,
                ancestors,
                first_dir_created,
                top_src,
                top_dst,
            );
            ancestors.pop();
        } else if src_m.is_link() {
            // S_ISLNK source under DEREF_NEVER: recreate the link.
            match winfs::link_target(&self.api_src(src_name)) {
                Err(e) => {
                    let m = tr!(
                        self.ctx,
                        "command.mv.error.cannot_read_symlink",
                        "cannot read symbolic link {src}: {reason}",
                        src = q(src_name),
                        reason = classify(e).reason()
                    );
                    self.fail(&m);
                    return self.un_backup(out, dst_name, drelname, &mut dst_backup, src_m.id());
                }
                Ok((target, is_dir)) => {
                    let tag = src_m.tag;
                    let r = if tag == winfs::IO_REPARSE_TAG_MOUNT_POINT {
                        winfs::create_junction(&self.api_dst(dst_api_name), &self.api(&target))
                    } else {
                        winfs::create_symlink(&self.api_dst(dst_api_name), &target, is_dir)
                    };
                    if let Err(e) = r {
                        let m = tr!(
                            self.ctx,
                            "command.mv.error.cannot_create_symlink_dst",
                            "cannot create symbolic link {dst}: {reason}",
                            dst = q(dst_name),
                            reason = classify(e).reason()
                        );
                        self.fail(&m);
                        return self.un_backup(
                            out,
                            dst_name,
                            drelname,
                            &mut dst_backup,
                            src_m.id(),
                        );
                    }
                }
            }
        } else {
            match self.copy_bytes(src_name, dst_api_name) {
                Err(CopyError::OpenRead(e)) => {
                    let m = tr!(
                        self.ctx,
                        "command.mv.error.cannot_open_read",
                        "cannot open {src} for reading: {reason}",
                        src = q(src_name),
                        reason = classify(e).reason()
                    );
                    self.fail(&m);
                    return self.un_backup(out, dst_name, drelname, &mut dst_backup, src_m.id());
                }
                Err(CopyError::Read) => {
                    let m = tr!(
                        self.ctx,
                        "command.mv.error.read",
                        "error reading {src}",
                        src = q(src_name)
                    );
                    self.fail(&m);
                    return self.un_backup(out, dst_name, drelname, &mut dst_backup, src_m.id());
                }
                Err(CopyError::Create(e)) => {
                    let reason = if dst_name.is_empty() {
                        MErr::NoEnt.reason()
                    } else {
                        classify(e).reason()
                    };
                    let m = tr!(
                        self.ctx,
                        "command.mv.error.cannot_create_regular",
                        "cannot create regular file {dst}: {reason}",
                        dst = q(dst_name),
                        reason = reason
                    );
                    self.fail(&m);
                    return self.un_backup(out, dst_name, drelname, &mut dst_backup, src_m.id());
                }
                Err(CopyError::Write) => {
                    let m = tr!(
                        self.ctx,
                        "command.mv.error.write",
                        "error writing {dst}",
                        dst = q(dst_name)
                    );
                    self.fail(&m);
                    return self.un_backup(out, dst_name, drelname, &mut dst_backup, src_m.id());
                }
                Ok(()) => {}
            }
        }

        // record the now-created destination (copy.c:3213).
        if cmdline && self.dest_info {
            if let Ok(dm) = self.lstat(dst_api_name) {
                self.created.insert(drelname.to_string(), dm.id());
            }
        }

        // ---- preserve (copy.c:3234+): times -> FILETIMEs, mode ->
        // read-only attribute; ownership/context/xattr are no-ops and
        // failures are non-fatal (require_preserve=false for mv). ----
        let _ = winfs::set_times(
            &self.api_dst(dst_api_name),
            src_m.btime,
            src_m.atime,
            src_m.mtime,
        );
        let _ = winfs::set_read_only(&self.api_dst(dst_api_name), src_m.is_read_only());

        out.ok = delayed_ok;
        out
    }

    /// copy_dir (copy.c:824): enumerate the source directory and recurse
    /// into copy_internal for each child with command_line_arg=false.
    #[allow(clippy::too_many_arguments)]
    fn copy_dir(
        &mut self,
        src_name_in: &str,
        dst_name_in: &str,
        drelname_in: &str,
        new_dst: bool,
        src_sb: &Meta,
        ancestors: &mut Vec<(u32, u64)>,
        first_dir_created: &mut bool,
        top_src: &str,
        top_dst: &str,
    ) -> bool {
        let entries = match winfs::read_dir(&self.api_src(src_name_in)) {
            Ok(v) => v,
            Err(e) => {
                let m = tr!(
                    self.ctx,
                    "command.mv.error.cannot_access",
                    "cannot access {src}: {reason}",
                    src = q(src_name_in),
                    reason = classify(e).reason()
                );
                self.fail(&m);
                return false;
            }
        };
        let mut ok = true;
        for (name, _attrs, _tag, _size) in entries {
            let child_src = join(src_name_in, &name);
            let child_dst = join(dst_name_in, &name);
            let child_rel = join(drelname_in, &name);
            let o = self.copy_internal(
                &child_src,
                &child_dst,
                &child_rel,
                i32::from(new_dst),
                Some(src_sb),
                ancestors,
                false,
                first_dir_created,
                -1,
                true,
                top_src,
                top_dst,
            );
            ok &= o.ok;
            if o.into_self {
                break;
            }
        }
        ok
    }

    /// The un_backup tail (copy.c:3352): forget the src dev/ino entry and
    /// restore any destination backup made before the failure.
    fn un_backup(
        &mut self,
        mut out: Outcome,
        dst_name: &str,
        drelname: &str,
        dst_backup: &mut Option<String>,
        src_id: (u32, u64),
    ) -> Outcome {
        self.forget_created(src_id);
        if let Some(b) = dst_backup.take() {
            let prefix = self.dest_dir_prefix(dst_name, drelname);
            let backup_full = join(&prefix, &b);
            match self.rename_replace(&b, dst_name) {
                Ok(()) => {
                    if self.opts.verbose {
                        let _ = writeln!(
                            self.io.stdout,
                            "{} -> {} (unbackup)",
                            q(&backup_full),
                            q(dst_name)
                        );
                    }
                }
                Err(e) => {
                    let m = tr!(
                        self.ctx,
                        "command.mv.error.cannot_unbackup",
                        "cannot un-backup {dst}: {reason}",
                        dst = q(dst_name),
                        reason = classify(e).reason()
                    );
                    self.fail(&m);
                }
            }
        }
        out.ok = false;
        out
    }

    // ------------------------------------------------------------------
    // same_file_ok (copy.c:1741), mv specialization: DEREF_NEVER,
    // move_mode, no hard_link/symbolic_link/unlink_dest.
    // ------------------------------------------------------------------

    fn same_file_ok(&mut self, src_name: &str, src: &Meta, dst_name: &str, dst: &Meta) -> bool {
        let same = src.id() == dst.id();

        if src.is_link() && dst.is_link() {
            if self.same_entry(src_name, dst_name) {
                return false;
            }
            if self.opts.backup != Backup::None {
                return true;
            }
            // two links, distinct entries: fine unless they are the same
            // inode (a hard-linked link pair, which mv cannot replace).
            return !same;
        }

        if self.opts.backup != Backup::None {
            if !same {
                return true;
            }
            return !self.same_entry(src_name, dst_name);
        }

        // move_mode: a destination link is always replaceable.
        if dst.is_link() {
            return true;
        }

        // Distinct hard links to one file would lose data.
        if same && dst.links > 1 && !self.same_entry(src_name, dst_name) {
            return false;
        }

        // A case-only rename of the same directory entry (`mv File.txt
        // file.txt`) is a real rename on a case-insensitive volume: the
        // two spellings are one entry, so it is not the same-file error.
        if same && self.same_dir_case_only(src_name, dst_name) {
            return true;
        }

        if !src.is_link() && !dst.is_link() && !same {
            return true;
        }

        // DEREF_NEVER tail (copy.c:1944): resolve both sides and compare.
        let src_id = if src.is_link() {
            self.stat(src_name).map(|m| m.id()).unwrap_or(src.id())
        } else {
            src.id()
        };
        let dst_id = if dst.is_link() {
            self.stat(dst_name).map(|m| m.id()).unwrap_or(dst.id())
        } else {
            dst.id()
        };
        src_id != dst_id
    }

    // ------------------------------------------------------------------
    // abandon_move / overwrite_ok (copy.c:2064/1990)
    // ------------------------------------------------------------------

    /// writable_destination (copy.c:1981): a link is always writable;
    /// the read-only attribute is the Windows carrier for the mode's
    /// missing owner-write bit.
    fn writable_destination(&self, dst: &Meta) -> bool {
        if dst.is_link() {
            return true;
        }
        !dst.is_read_only()
    }

    fn abandon_move(&mut self, dst_name: &str, dst: &Meta) -> bool {
        match self.opts.interactive {
            Interactive::AlwaysNo | Interactive::AlwaysSkip => true,
            Interactive::Ask => !self.overwrite_ok(dst_name, dst),
            Interactive::Unspecified | Interactive::AlwaysYes => {
                self.stdio.is_tty(0)
                    && !self.writable_destination(dst)
                    && !self.overwrite_ok(dst_name, dst)
            }
        }
    }

    fn overwrite_ok(&mut self, dst_name: &str, dst: &Meta) -> bool {
        if self.writable_destination(dst) {
            let m = tr!(
                self.ctx,
                "command.mv.prompt.overwrite",
                "overwrite {dst}?",
                dst = q(dst_name)
            );
            let _ = write!(self.io.stderr, "mv: {m} ");
        } else {
            let mode = modemap::current_mode(std::path::Path::new(&self.api(dst_name)));
            let m = tr!(
                self.ctx,
                "command.mv.prompt.replace_overriding",
                "replace {dst}, overriding mode {mode}?",
                dst = q(dst_name),
                mode = modemap::mode_string(mode)
            );
            let _ = write!(self.io.stderr, "mv: {m} ");
        }
        let _ = self.io.stderr.flush();
        let mut line = Vec::new();
        let mut byte = [0u8; 1];
        loop {
            match std::io::Read::read(&mut *self.io.stdin, &mut byte) {
                Ok(0) => break,
                Ok(_) => {
                    if byte[0] == b'\n' {
                        break;
                    }
                    line.push(byte[0]);
                }
                Err(_) => break,
            }
        }
        matches!(line.first(), Some(b'y') | Some(b'Y'))
    }

    // ------------------------------------------------------------------
    // rename wrappers and errno classification
    // ------------------------------------------------------------------

    /// renameat-with-replace plus the Windows value-model retry: a
    /// READONLY destination refuses MoveFileExW where POSIX ignores the
    /// file mode for rename.
    ///
    /// The POSIX spelling rules Win32 drops are applied first: a trailing
    /// separator on the source must name a directory (else ENOTDIR), and
    /// on the destination it is ENOTDIR unless it names one (Win32 strips
    /// `"b\\"` and renames anyway, which turned `mv a nodir/` into a
    /// success).
    fn rename_replace(&self, src: &str, dst: &str) -> Result<(), u32> {
        if is_dot_operand(src) || is_dot_operand(dst) {
            return Err(MErr::Busy.code());
        }
        if has_trailing_sep(src) {
            if let Err(e) = self.trailing_sep_lookup(src) {
                return Err(e.code());
            }
        }
        if has_trailing_sep(dst) {
            match self.lstat(dst) {
                Ok(m) if m.is_dir() => {}
                Ok(_) => return Err(MErr::NotDir.code()),
                Err(MErr::NoEnt) => {
                    // rename("dir", "new/") is legal, rename("file", "new/")
                    // is ENOTDIR (the trailing separator promises a
                    // directory result).
                    if !self.stat(src).map(|m| m.is_dir()).unwrap_or(false) {
                        return Err(MErr::NotDir.code());
                    }
                }
                Err(e) => return Err(e.code()),
            }
        }
        let sa = self.api_src(src);
        let da = self.api_dst(dst);
        match winfs::rename(&sa, &da) {
            Ok(()) => Ok(()),
            Err(e) if e == 5 => {
                let ro = winfs::attributes(&da)
                    .map(|a| a & winfs::FILE_ATTRIBUTE_READONLY != 0)
                    .unwrap_or(false);
                if ro {
                    let _ = winfs::set_read_only(&da, false);
                    match winfs::rename(&sa, &da) {
                        Ok(()) => Ok(()),
                        Err(e2) => {
                            let _ = winfs::set_read_only(&da, true);
                            Err(e2)
                        }
                    }
                } else {
                    Err(e)
                }
            }
            Err(e) => Err(e),
        }
    }

    /// The class the error switch (copy.c:2844) branches on. Spelled
    /// operands decide the cases the Win32 code cannot express: a dot
    /// operand is EBUSY and a trailing-separator destination whose
    /// prefix is a non-directory is ENOTDIR.
    fn rename_err_class(&self, code: i32, src_name: &str, dst_name: &str) -> MErr {
        let base = last_component(src_name);
        if base == "." || base == ".." {
            return MErr::Busy;
        }
        let dbase = last_component(dst_name);
        if dbase == "." || dbase == ".." {
            return MErr::Busy;
        }
        let e = classify(code as u32);
        if e != MErr::NotDir && (dst_name.ends_with('/') || dst_name.ends_with('\\')) {
            return MErr::NotDir;
        }
        e
    }

    /// Is DST physically inside SRC? Every existing ancestor of the
    /// destination is resolved through links and compared with the source
    /// inode — the kernel's EINVAL for `mv d d/sub/d` (copy.c:2797),
    /// where the deepest existing ancestor (`d/sub`) is a *different*
    /// inode but a farther one (`d`) is the source. Also covers the
    /// `mv real l` case where `l` is a link into `real` (the
    /// copy::destination_inside_ancestors rule).
    fn dst_inside_src(&self, src_name: &str, dst_name: &str) -> bool {
        let src_id = match self.stat(src_name) {
            Ok(m) => m.id(),
            Err(_) => return false,
        };
        let mut current = std::path::PathBuf::from(self.api(dst_name));
        for _ in 0..256 {
            let shown = current.to_string_lossy().into_owned();
            match winfs::meta_follow(self.ctx, &shown) {
                Ok(m) if m.id() == src_id => return true,
                _ => {}
            }
            match current.parent() {
                Some(parent) if parent != current => current = parent.to_path_buf(),
                _ => return false,
            }
        }
        false
    }

    /// Is the directory empty (POSIX rename replaces an empty one)?
    fn dir_is_empty(&self, name: &str) -> bool {
        match winfs::read_dir(&self.api_dst(name)) {
            Ok(v) => v.is_empty(),
            Err(_) => false,
        }
    }

    // ------------------------------------------------------------------
    // backup machinery (find_backup_file_name / backup_file_rename)
    // ------------------------------------------------------------------

    fn suffix(&self) -> &str {
        self.opts.backup_suffix.as_deref().unwrap_or("~")
    }

    fn select_backup(&self, dst: &str) -> String {
        match self.opts.backup {
            Backup::Simple => format!("{dst}{}", self.suffix()),
            Backup::Numbered => self.numbered_backup(dst),
            Backup::Existing => {
                if self.lexists(&format!("{dst}.~1~")) {
                    self.numbered_backup(dst)
                } else {
                    format!("{dst}{}", self.suffix())
                }
            }
            Backup::None => dst.to_string(),
        }
    }

    fn numbered_backup(&self, dst: &str) -> String {
        let mut n = 1u64;
        loop {
            let cand = format!("{dst}.~{n}~");
            if !self.lexists(&cand) {
                return cand;
            }
            n += 1;
        }
    }

    /// backup_file_rename (copy.c:2539): move the destination aside.
    /// `Ok(None)` is the ENOENT case GNU ignores.
    fn rename_backup(&mut self, dst_name: &str) -> Result<Option<String>, MErr> {
        let backup = self.select_backup(dst_name);
        match self.rename_replace(dst_name, &backup) {
            Ok(()) => Ok(Some(backup)),
            Err(e) => {
                let c = classify(e);
                if c == MErr::NoEnt {
                    Ok(None)
                } else {
                    Err(c)
                }
            }
        }
    }

    /// source_is_dst_backup (copy.c:2163): `mv -b a~ a` would rename the
    /// source as its own backup.
    fn source_is_dst_backup(
        &self,
        srcbase: &str,
        src: &Meta,
        dst_name: &str,
        drelname: &str,
    ) -> bool {
        let dstbase = last_component(drelname);
        let suffix = self.suffix();
        if !(srcbase.len() == dstbase.len() + suffix.len()
            && srcbase.starts_with(&dstbase)
            && srcbase[dstbase.len()..] == *suffix)
        {
            return false;
        }
        let prefix = self.dest_dir_prefix(dst_name, drelname);
        let back = join(&prefix, &format!("{dstbase}{suffix}"));
        match self.lstat(&back) {
            Ok(m) => m.id() == src.id(),
            Err(_) => false,
        }
    }

    /// The directory part of dst_name that drelname lives in (the
    /// target dir for -t moves, the operand's own parent otherwise —
    /// subst_suffix's dirlen prefix).
    fn dest_dir_prefix(&self, dst_name: &str, drelname: &str) -> String {
        if drelname == "." || dst_name.len() < drelname.len() {
            return String::new();
        }
        dst_name[..dst_name.len() - drelname.len()].to_string()
    }

    // ------------------------------------------------------------------
    // dev/ino bookkeeping (cp-hash remember_copied / forget_created)
    // ------------------------------------------------------------------

    /// remember_copied: record relname -> src id and return any earlier
    /// destination recorded for the same inode.
    fn remember_copied(&mut self, relname: &str, id: (u32, u64)) -> Option<String> {
        self.src_to_dest.insert(id, relname.to_string())
    }

    fn forget_created(&mut self, id: (u32, u64)) {
        self.src_to_dest.remove(&id);
        self.created.retain(|_, v| *v != id);
    }

    /// create_hard_link (copy.c:2124): link EARLIER (a relname in the
    /// destination directory) onto DST, replacing it.
    fn create_hard_link(&mut self, earlier: &str, dst_name: &str, drelname: &str) -> bool {
        let prefix = self.dest_dir_prefix(dst_name, drelname);
        let earlier_full = join(&prefix, earlier);
        if self.lexists(dst_name) {
            let dir = self
                .lstat(dst_name)
                .map(|m| m.is_dir() && !m.is_link())
                .unwrap_or(false);
            let _ = winfs::delete(&self.api_dst(dst_name), dir);
        }
        match winfs::hard_link(&self.api(&earlier_full), &self.api_dst(dst_name)) {
            Ok(()) => true,
            Err(e) => {
                let m = tr!(
                    self.ctx,
                    "command.mv.error.cannot_create_hard_link",
                    "cannot create hard link {dst} to {src}: {reason}",
                    dst = q(dst_name),
                    src = q(&earlier_full),
                    reason = classify(e).reason()
                );
                self.fail(&m);
                false
            }
        }
    }

    // ------------------------------------------------------------------
    // the EXDEV byte copy and the rm fallback
    // ------------------------------------------------------------------

    fn copy_bytes(&mut self, src: &str, dst: &str) -> Result<(), CopyError> {
        let mut infile = winfs::open_read(&self.api_src(src)).map_err(CopyError::OpenRead)?;
        let mut outfile = winfs::open_write(&self.api_dst(dst)).map_err(CopyError::Create)?;
        let mut buf = vec![0u8; 256 * 1024];
        loop {
            let n = winfs::read_chunk(&mut infile, &mut buf).map_err(|_| CopyError::Read)?;
            if n == 0 {
                break;
            }
            winfs::write_all(&mut outfile, &buf[..n]).map_err(|_| CopyError::Write)?;
        }
        let _ = outfile.flush();
        Ok(())
    }

    /// rm(3) with mv's options (remove.c:604): recursive, never
    /// interactive, verbose from -v — `removed 'x'` /
    /// `removed directory 'x'` in post-order.
    fn rm_source(&mut self, src: &str) -> bool {
        match self.lstat(src) {
            Ok(m) if m.is_dir() && !m.is_link() => self.rm_tree(src),
            Ok(_) => match winfs::delete(&self.api(src), false) {
                Ok(()) => {
                    if self.opts.verbose {
                        let m = tr!(
                            self.ctx,
                            "command.mv.verbose.removed",
                            "removed {src}",
                            src = q(src)
                        );
                        let _ = writeln!(self.io.stdout, "{m}");
                    }
                    true
                }
                Err(e) => {
                    let m = tr!(
                        self.ctx,
                        "command.mv.error.cannot_remove",
                        "cannot remove {src}: {reason}",
                        src = q(src),
                        reason = classify(e).reason()
                    );
                    self.fail(&m);
                    false
                }
            },
            Err(e) => {
                let m = tr!(
                    self.ctx,
                    "command.mv.error.cannot_remove",
                    "cannot remove {src}: {reason}",
                    src = q(src),
                    reason = e.reason()
                );
                self.fail(&m);
                false
            }
        }
    }

    fn rm_tree(&mut self, dir: &str) -> bool {
        let entries = match winfs::read_dir(&self.api_src(dir)) {
            Ok(v) => v,
            Err(e) => {
                let m = tr!(
                    self.ctx,
                    "command.mv.error.cannot_remove",
                    "cannot remove {src}: {reason}",
                    src = q(dir),
                    reason = classify(e).reason()
                );
                self.fail(&m);
                return false;
            }
        };
        let mut ok = true;
        for (name, attrs, tag, _size) in entries {
            let child = join(dir, &name);
            let is_link =
                attrs & winfs::FILE_ATTRIBUTE_REPARSE_POINT != 0 && winfs::is_link_tag(tag);
            if attrs & winfs::FILE_ATTRIBUTE_DIRECTORY != 0 && !is_link {
                ok &= self.rm_tree(&child);
            } else {
                match winfs::delete(&self.api(&child), false) {
                    Ok(()) => {
                        if self.opts.verbose {
                            let m = tr!(
                                self.ctx,
                                "command.mv.verbose.removed",
                                "removed {src}",
                                src = q(&child)
                            );
                            let _ = writeln!(self.io.stdout, "{m}");
                        }
                    }
                    Err(e) => {
                        let m = tr!(
                            self.ctx,
                            "command.mv.error.cannot_remove",
                            "cannot remove {src}: {reason}",
                            src = q(&child),
                            reason = classify(e).reason()
                        );
                        self.fail(&m);
                        ok = false;
                    }
                }
            }
        }
        match winfs::delete(&self.api(dir), true) {
            Ok(()) => {
                if self.opts.verbose {
                    let m = tr!(
                        self.ctx,
                        "command.mv.verbose.removed_directory",
                        "removed directory {src}",
                        src = q(dir)
                    );
                    let _ = writeln!(self.io.stdout, "{m}");
                }
            }
            Err(e) => {
                let m = tr!(
                    self.ctx,
                    "command.mv.error.cannot_remove",
                    "cannot remove {src}: {reason}",
                    src = q(dir),
                    reason = classify(e).reason()
                );
                self.fail(&m);
                ok = false;
            }
        }
        ok
    }

    /// emit_verbose (copy.c:2084): `<verb>'src' -> 'dst' (backup: 'b~')`.
    fn emit_verbose(&mut self, verb: &str, src: &str, dst: &str, backup: Option<&str>) {
        let mut line = format!("{verb}{} -> {}", q(src), q(dst));
        if let Some(b) = backup {
            line.push_str(&format!(" (backup: {})", q(b)));
        }
        let _ = writeln!(self.io.stdout, "{line}");
    }
}

fn src_m_or(m: &Option<Meta>) -> Meta {
    m.clone().unwrap_or_default()
}

/// Win32 ERROR_FILE_EXISTS, the raw `rename_errno` value the ladder uses
/// once the destination stat confirms a collision.
const ERROR_FILE_EXISTS: i32 = 80;

/// A POSIX trailing separator (a lone root marker is not one).
fn has_trailing_sep(name: &str) -> bool {
    name.len() > 1 && (name.ends_with('/') || name.ends_with('\\'))
}

/// A `.`/`..` operand: rename(2) reports EBUSY for these, so mv must not
/// hand them to MoveFileExW (which would try to rename the cwd/dot entry).
fn is_dot_operand(name: &str) -> bool {
    let trimmed = name.trim_end_matches(['/', '\\']);
    let base = match trimmed.rfind(['/', '\\']) {
        Some(i) => &trimmed[i + 1..],
        None => trimmed,
    };
    base == "." || base == ".."
}

/// Split a native absolute path into (dir, basename).
fn split_dir_base(path: &str) -> (String, String) {
    let trimmed = path.trim_end_matches(['\\', '/']);
    match trimmed.rfind(['\\', '/']) {
        Some(i) => (trimmed[..i].to_string(), trimmed[i + 1..].to_string()),
        None => (String::new(), trimmed.to_string()),
    }
}

enum CopyError {
    OpenRead(u32),
    Read,
    Create(u32),
    Write,
}
