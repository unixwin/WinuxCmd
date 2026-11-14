//! `cp` copy engine — the owning C functions of coreutils 9.4 and their
//! Windows value-model realization.
//!
//! GNU owners (src/copy.c unless noted):
//! * `copy_internal` copy.c:2197 — per-operand decision ladder: dest stat
//!   (lstat/stat per mode), same-file, skip (`-n`/`--update=none`), update
//!   comparison, interactive prompt, dir-vs-non-dir refusals, just-created
//!   destination, backup, verbose, hard/symbolic link, link recreation,
//!   dispatch to `copy_reg` / `copy_dir`.
//! * `copy_reg` copy.c:1235 — regular-file copy (open, sparse, clone,
//!   buffer loop, preserve, `-f` retry).
//! * `copy_dir` copy.c:822 — recursive descent with `ancestors` cycle
//!   detection and one-file-system gating.
//! * `same_file_ok` copy.c:1739 — `(dev,ino)` identity; here the volume
//!   serial + file index pair.
//! * `overwrite_ok` copy.c:1988 — the `-i` prompt (line read, first
//!   character y/Y accepts; EOF declines).
//! * `should_dereference` copy.c:2151 — `-H` applies to command-line
//!   operands only.
//! * `emit_verbose` copy.c:2082 — `'src' -> 'dst' (backup: 'dst~')`.
//! * `create_hard_link` copy.c:2122 — `-l` and `--preserve=links`.
//! * `is_ancestor` copy.c:688 — cyclic link detection.
//! * `do_copy` cp.c:608 — operand grammar: `-t`, `-T`, multi-source target
//!   validation, `--parents` grafting; `make_dir_parents_private`
//!   cp.c:399; `decode_preserve_arg` cp.c:876; main cp.c:968 for the
//!   last-wins option state.
//!
//! Windows decisions (find/winfs.rs precedent):
//! * a link source is recreated by target text; junction targets are
//!   written back as junctions (`FSCTL_SET_REPARSE_POINT`), the only
//!   unprivileged directory-link form; file symlinks need Developer Mode
//!   and fail with GNU's `Operation not permitted` text otherwise (#1647's
//!   platform face);
//! * `--preserve=mode` maps the shared modemap bits onto the read-only
//!   attribute (never copies HIDDEN/SYSTEM, which are not POSIX mode bits);
//! * `--preserve=timestamps` copies creation/access/write FILETIMEs;
//!   ownership/context/xattr are accepted list arithmetic with no Windows
//!   effect (documented 1.x parity);
//! * `--preserve=links` maps `(volume serial, file index)` to the first
//!   destination and hard-links later names, so `cp -a` keeps a hard-link
//!   group intact;
//! * `--reflink=always` reports GNU's `failed to clone ...: Operation not
//!   supported` on non-ReFS volumes (probed on the ext4 oracle: same rc and
//!   stdout); `auto` falls back to a byte copy;
//! * `--sparse=always` marks the destination sparse and seeks over
//!   all-zero 64 KiB blocks (NTFS allocation granularity).

use std::collections::HashMap;
use std::io::Write;

use winux_core::{tr, Ctx, Io};

use super::opts::q;
use super::opts::{Backup, Deref, Interactive, Options, Sparse};
use super::winfs::{self, Meta};
use crate::fsutil;
use crate::modemap;

pub struct Copier<'a, 'b, 'c> {
    pub ctx: &'a Ctx<'c>,
    pub opts: &'a Options,
    pub io: &'a mut Io<'b>,
    pub failed: bool,
    /// `--preserve=links`: source (dev,ino) -> first destination name.
    hardlinks: HashMap<(u32, u64), String>,
    /// `dest_info` (copy.c:2018): destination names created by this run,
    /// used for "will not overwrite just-created" and duplicate sources.
    created: HashMap<String, (u32, u64)>,
    /// `src_info` (copy.c:2033): command-line sources already seen.
    seen_src: Vec<(String, (u32, u64))>,
}

impl<'a, 'b, 'c> Copier<'a, 'b, 'c> {
    pub fn new(ctx: &'a Ctx<'c>, opts: &'a Options, io: &'a mut Io<'b>) -> Self {
        Copier {
            ctx,
            opts,
            io,
            failed: false,
            hardlinks: HashMap::new(),
            created: HashMap::new(),
            seen_src: Vec::new(),
        }
    }

    fn say(&mut self, msg: &str) {
        let _ = writeln!(self.io.stderr, "cp: {msg}");
    }

    pub fn fail(&mut self, msg: &str) {
        self.failed = true;
        self.say(msg);
    }

    // ------------------------------------------------------------------
    // path helpers
    // ------------------------------------------------------------------

    /// Absolute native path (the operand boundary: POSIX spellings, /mnt/X
    /// folding, DOS devices — fsutil::resolve is the shared front door).
    pub fn api(&self, name: &str) -> String {
        if name.is_empty() {
            // open("") is ENOENT on GNU; never fold it onto the cwd.
            return String::new();
        }
        fsutil::resolve(self.ctx, name)
            .to_string_lossy()
            .into_owned()
    }

    pub fn meta(&self, name: &str, follow: bool) -> Result<Meta, u32> {
        winfs::meta(&self.api(name), follow)
    }

    pub fn lexists(&self, name: &str) -> bool {
        winfs::attributes(&self.api(name)).is_some()
    }

    pub fn deref_here(&self, command_line_arg: bool) -> bool {
        match self.opts.deref {
            Deref::Always => true,
            Deref::CommandLine => command_line_arg,
            Deref::Never => false,
            Deref::Undefined => false,
        }
    }

    fn same_id(a: &Meta, b: &Meta) -> bool {
        a.dev == b.dev && a.ino == b.ino
    }

    // ------------------------------------------------------------------
    // operand planning (cp.c:608 do_copy)
    // ------------------------------------------------------------------

    /// Expand one SOURCE operand to its final destination name.
    pub fn dest_for(&self, target: &str, src: &str, parents: bool) -> String {
        if !parents {
            return join(target, &last_component(src));
        }
        let graft = graft_relative(src);
        join(target, &graft)
    }

    // ------------------------------------------------------------------
    // the copy_internal ladder
    // ------------------------------------------------------------------

    /// Copy one non-directory (or recreate a link). Returns GNU's per-file
    /// success flag; diagnostics are already printed.
    pub fn copy_file(&mut self, src: &str, dst: &str, command_line_arg: bool) -> bool {
        // ---- source stat (copy.c:2220 follow_fstatat) ----
        let deref = self.deref_here(command_line_arg);
        let src_meta = match self.meta(src, deref) {
            Ok(m) => m,
            Err(e) => {
                let m = tr!(
                    self.ctx,
                    "command.cp.error.cannot_stat",
                    "cannot stat {src}: {reason}",
                    src = q(src),
                    reason = winfs::reason(e)
                );
                self.fail(&m);
                return false;
            }
        };
        if src_meta.is_dir() && !(src_meta.is_link() && !deref) {
            // copy_internal's S_ISDIR && !recursive branch; directory
            // dispatch is the caller's job, so this is a guard. A link
            // source that is not dereferenced stays a link even when its
            // followed target is a directory (copy_internal uses lstat
            // under DEREF_NEVER).
            return self.omit_directory(src);
        }

        // ---- duplicate command-line source (copy.c:2244-2260) ----
        if command_line_arg {
            if self.opts.backup == Backup::None
                && self
                    .seen_src
                    .iter()
                    .any(|(n, id)| *n == src && *id == src_meta.id())
            {
                let m = tr!(
                    self.ctx,
                    "command.cp.warning.source_twice",
                    "warning: source file {src} specified more than once",
                    src = q(src)
                );
                self.say(&m);
                return true;
            }
            self.seen_src.push((src.to_string(), src_meta.id()));
        }

        // ---- destination stat (copy.c:2265-2305) ----
        let dst_exists = self.lexists(dst);
        let dst_lmeta = if dst_exists {
            self.meta(dst, false).ok()
        } else {
            None
        };
        let dst_fmeta = if dst_exists {
            self.meta(dst, true).ok()
        } else {
            None
        };

        // same_file_ok: only when not skipping (copy.c:2344).
        if dst_exists
            && self.opts.interactive != Interactive::AlwaysNo
            && self.opts.interactive != Interactive::AlwaysSkip
        {
            let dest_meta = if src_meta.is_link() {
                dst_lmeta.as_ref()
            } else {
                dst_fmeta.as_ref().or(dst_lmeta.as_ref())
            };
            if let Some(dm) = dest_meta {
                if Self::same_id(&src_meta, dm) {
                    // cp.c:690-720: `cp -f -b foo foo` renames the old file
                    // to its backup name and copies onto that.
                    if self.opts.force && self.opts.backup != Backup::None && src == dst {
                        let backup = self.select_backup(dst);
                        if let Some(b) = backup {
                            if winfs::rename(&self.api(dst), &self.api(&b)).is_err() {
                                let m = tr!(
                                    self.ctx,
                                    "command.cp.error.cannot_backup",
                                    "cannot create backup for destination"
                                );
                                self.fail(&m);
                                return false;
                            }
                            return self.copy_regular(src, &b, &src_meta, None, true);
                        }
                    }
                    let m = tr!(
                        self.ctx,
                        "command.cp.error.same_file",
                        "{src} and {dst} are the same file",
                        src = q(src),
                        dst = q(dst)
                    );
                    self.fail(&m);
                    return false;
                }
            }
        }

        // ---- --update comparison (copy.c:2352-2415) ----
        if self.opts.update && dst_exists {
            if let (Some(dm), false) = (dst_fmeta.as_ref(), src_meta.is_dir()) {
                if src_meta.mtime <= dm.mtime {
                    self.remember_skipped(src, dst, &src_meta);
                    return true;
                }
            }
        }

        // ---- skip / prompt (copy.c:2417-2436) ----
        if dst_exists {
            match self.opts.interactive {
                Interactive::AlwaysSkip | Interactive::AlwaysNo => {
                    self.remember_skipped(src, dst, &src_meta);
                    return true;
                }
                Interactive::Ask => {
                    if !self.prompt(dst) {
                        self.failed = true;
                        return false;
                    }
                }
                Interactive::Unspecified => {}
            }
        }

        // ---- dir-vs-non-dir refusals (copy.c:2445-2500) ----
        if let Some(dm) = dst_lmeta.as_ref() {
            if dm.is_dir() && !dm.is_link() && !src_meta.is_dir() {
                let m = tr!(
                    self.ctx,
                    "command.cp.error.overwrite_dir_with_file",
                    "cannot overwrite directory {dst} with non-directory",
                    dst = q(dst)
                );
                self.fail(&m);
                return false;
            }
        }
        // "will not overwrite just-created" (copy.c:2470-2485)
        if command_line_arg && self.opts.backup != Backup::Numbered {
            if let (Some(dm), true) = (dst_fmeta.as_ref(), self.created.contains_key(dst)) {
                let _ = dm;
                let m = tr!(
                    self.ctx,
                    "command.cp.error.will_not_overwrite_just_created",
                    "will not overwrite just-created {dst} with {src}",
                    dst = q(dst),
                    src = q(src)
                );
                self.fail(&m);
                return false;
            }
        }

        self.copy_regular(src, dst, &src_meta, dst_lmeta.as_ref(), command_line_arg)
    }

    /// The shared "the destination is going to be written" tail: backup,
    /// verbose, link modes, then the byte copy.
    fn copy_regular(
        &mut self,
        src: &str,
        dst: &str,
        src_meta: &Meta,
        dst_lmeta: Option<&Meta>,
        command_line_arg: bool,
    ) -> bool {
        // ---- --remove-destination ----
        if self.opts.remove_destination {
            if let Some(dm) = dst_lmeta {
                if let Err(e) = winfs::delete(&self.api(dst), dm.is_dir() && !dm.is_link()) {
                    let m = tr!(
                        self.ctx,
                        "command.cp.error.cannot_remove",
                        "cannot remove {dst}: {reason}",
                        dst = q(dst),
                        reason = winfs::reason(e)
                    );
                    self.fail(&m);
                    return false;
                }
            }
        }

        // ---- backup (copy.c:2510-2560) ----
        let mut backup_name: Option<String> = None;
        if self.opts.backup != Backup::None && self.lexists(dst) {
            if let Some(b) = self.select_backup(dst) {
                if winfs::rename(&self.api(dst), &self.api(&b)).is_err() {
                    let m = tr!(
                        self.ctx,
                        "command.cp.error.cannot_backup",
                        "cannot create backup for destination"
                    );
                    self.fail(&m);
                    return false;
                }
                backup_name = Some(b);
            }
        }

        // ---- -l hard link (copy.c:2122 create_hard_link) ----
        if self.opts.hard_link {
            let link_src = if src_meta.is_link() && self.deref_here(true) {
                // GNU -l dereferences by default; CreateHardLinkW on the
                // resolved path is the same rule.
                fsutil::resolve(self.ctx, src)
                    .to_string_lossy()
                    .into_owned()
            } else {
                self.api(src)
            };
            let mut link_err = winfs::hard_link(&link_src, &self.api(dst)).err();
            if link_err.is_some() && self.opts.force && self.lexists(dst) {
                // -f replaces an existing destination once (copy.c:2170).
                let dir = dst_lmeta
                    .map(|m| m.is_dir() && !m.is_link())
                    .unwrap_or(false);
                let _ = winfs::delete(&self.api(dst), dir);
                link_err = winfs::hard_link(&link_src, &self.api(dst)).err();
                if link_err.is_none() {
                    self.verbose(src, dst, backup_name.as_deref());
                    return true;
                }
            }
            if let Some(e) = link_err {
                let m = tr!(
                    self.ctx,
                    "command.cp.error.cannot_hard_link",
                    "cannot create hard link {dst} to {src}: {reason}",
                    dst = q(dst),
                    src = q(src),
                    reason = winfs::reason(e)
                );
                self.fail(&m);
                return false;
            }
            self.verbose(src, dst, backup_name.as_deref());
            return true;
        }

        // ---- -s symbolic link (copy.c symbolic_link branch) ----
        if self.opts.symbolic_link {
            if self.lexists(dst) {
                if !self.opts.force {
                    let m = tr!(
                        self.ctx,
                        "command.cp.error.cannot_create_symlink_exists",
                        "cannot create symbolic link {dst} to {src}: File exists",
                        dst = q(dst),
                        src = q(src)
                    );
                    self.fail(&m);
                    return false;
                }
                let dir = dst_lmeta
                    .map(|m| m.is_dir() && !m.is_link())
                    .unwrap_or(false);
                if dir {
                    let m = tr!(
                        self.ctx,
                        "command.cp.error.overwrite_dir_with_file",
                        "cannot overwrite directory {dst} with non-directory",
                        dst = q(dst)
                    );
                    self.fail(&m);
                    return false;
                }
                let _ = winfs::delete(&self.api(dst), false);
            }
            let target = winux_core::path::normalize_operand(self.ctx.env, &self.ctx.cwd, src);
            let is_dir = self.meta(src, true).map(|m| m.is_dir()).unwrap_or(false);
            if let Err(e) = winfs::create_symlink(&self.api(dst), &target, is_dir) {
                let m = tr!(
                    self.ctx,
                    "command.cp.error.cannot_create_symlink",
                    "cannot create symbolic link {dst} to {src}: {reason}",
                    dst = q(dst),
                    src = q(src),
                    reason = winfs::reason(e)
                );
                self.fail(&m);
                return false;
            }
            self.verbose(src, dst, backup_name.as_deref());
            return true;
        }

        // ---- link recreation (-P/-d/-a, plain -r) (copy.c:2560-2600) ----
        if src_meta.is_link() && !self.deref_here(command_line_arg) {
            return self.recreate_link(src, dst, backup_name.as_deref(), dst_lmeta);
        }

        // ---- --attributes-only (copy.c attributes_only branch) ----
        if !self.opts.data_copy_required {
            let mut preserve = self.opts.preserve;
            if !preserve.any() {
                // GNU: without a preserve family option the copied set is
                // mode + timestamps (copy.c attributes-only).
                preserve.mode = true;
                preserve.timestamps = true;
            }
            self.verbose(src, dst, backup_name.as_deref());
            if let Err(e) = winfs::open_write(&self.api(dst)) {
                let m = tr!(
                    self.ctx,
                    "command.cp.error.cannot_create_regular",
                    "cannot create regular file {dst}: {reason}",
                    dst = q(dst),
                    reason = winfs::reason(e)
                );
                self.fail(&m);
                return false;
            }
            let _ = self.apply_preserve(src, src_meta, dst, preserve, false);
            return true;
        }

        // ---- --reflink (copy.c:1235 clone) ----
        if self.opts.reflink != super::opts::Reflink::Never {
            // NTFS/ext4 oracle: no clone support -> ENOTSUP spelling.
            if self.opts.reflink == super::opts::Reflink::Always {
                let m = tr!(
                    self.ctx,
                    "command.cp.error.failed_to_clone",
                    "failed to clone {dst} from {src}: Operation not supported",
                    dst = q(dst),
                    src = q(src)
                );
                self.fail(&m);
                let _ = winfs::delete(&self.api(dst), false);
                return false;
            }
        }

        // ---- dangling destination symlink (copy.c:1462-1485) ----
        // A regular file cannot be created through a symlink whose target
        // does not exist; GNU refuses instead of following the link.
        if let Some(lm) = dst_lmeta {
            if lm.is_link() && self.lexists(dst) && self.meta(dst, true).is_err() {
                let m = tr!(
                    self.ctx,
                    "command.cp.error.not_writing_through_dangling_symlink",
                    "not writing through dangling symlink {dst}",
                    dst = q(dst)
                );
                self.fail(&m);
                return false;
            }
        }

        // ---- verbose is emitted BEFORE the copy for non-directories
        // (copy.c:2628) ----
        self.verbose(src, dst, backup_name.as_deref());

        // ---- --preserve=links: second name of a hard-link group becomes a
        // link to the first destination (copy.c:2720) ----
        if self.opts.preserve.links {
            if let Some(first) = self.hardlinks.get(&src_meta.id()).cloned() {
                if self.lexists(dst) {
                    let dir = self.dst_is_dir_lstat(dst);
                    let _ = winfs::delete(&self.api(dst), dir);
                }
                if winfs::hard_link(&self.api(&first), &self.api(dst)).is_ok() {
                    self.created.insert(dst.to_string(), src_meta.id());
                    return true;
                }
            }
        }

        // ---- byte copy (copy.c:1235 copy_reg) ----
        if let Err(e) = self.copy_bytes(src, dst) {
            match e {
                CopyError::OpenRead(err) => {
                    let m = tr!(
                        self.ctx,
                        "command.cp.error.cannot_open_read",
                        "cannot open {src} for reading: {reason}",
                        src = q(src),
                        reason = winfs::reason(err)
                    );
                    self.fail(&m);
                }
                CopyError::Read => {
                    let m = tr!(
                        self.ctx,
                        "command.cp.error.read",
                        "error reading {src}",
                        src = q(src)
                    );
                    self.fail(&m);
                }
                CopyError::Create(err) => {
                    let reason = if dst.is_empty() {
                        "No such file or directory"
                    } else {
                        winfs::reason(err)
                    };
                    let m = tr!(
                        self.ctx,
                        "command.cp.error.cannot_create_regular",
                        "cannot create regular file {dst}: {reason}",
                        dst = q(dst),
                        reason = reason
                    );
                    self.fail(&m);
                }
                CopyError::Write => {
                    let m = tr!(
                        self.ctx,
                        "command.cp.error.write",
                        "error writing {dst}",
                        dst = q(dst)
                    );
                    self.fail(&m);
                }
            }
            return false;
        }

        // ---- preserve (copy.c:2900 finish_copy) ----
        let _ = self.apply_preserve(src, src_meta, dst, self.opts.preserve, false);
        if self.opts.preserve.links {
            self.hardlinks
                .entry(src_meta.id())
                .or_insert_with(|| dst.to_string());
        }
        self.created.insert(dst.to_string(), src_meta.id());
        true
    }

    fn dst_is_dir_lstat(&self, dst: &str) -> bool {
        self.meta(dst, false)
            .map(|m| m.is_dir() && !m.is_link())
            .unwrap_or(false)
    }

    fn remember_skipped(&mut self, src: &str, dst: &str, meta: &Meta) {
        let _ = src;
        if self.opts.preserve.links {
            if let Some(first) = self.hardlinks.get(&meta.id()).cloned() {
                let dir = self.dst_is_dir_lstat(dst);
                let _ = winfs::delete(&self.api(dst), dir);
                let _ = winfs::hard_link(&self.api(&first), &self.api(dst));
            }
        }
    }

    fn recreate_link(
        &mut self,
        src: &str,
        dst: &str,
        backup: Option<&str>,
        dst_lmeta: Option<&Meta>,
    ) -> bool {
        let (target, is_dir) = match winfs::link_target(&self.api(src)) {
            Ok(v) => v,
            Err(e) => {
                let m = tr!(
                    self.ctx,
                    "command.cp.error.cannot_read_symlink",
                    "cannot read symbolic link {src}: {reason}",
                    src = q(src),
                    reason = winfs::reason(e)
                );
                self.fail(&m);
                return false;
            }
        };
        if let Some(dm) = dst_lmeta {
            if dm.is_dir() && !dm.is_link() {
                let m = tr!(
                    self.ctx,
                    "command.cp.error.overwrite_dir_with_file",
                    "cannot overwrite directory {dst} with non-directory",
                    dst = q(dst)
                );
                self.fail(&m);
                return false;
            }
            let _ = winfs::delete(&self.api(dst), dm.is_dir());
        }
        let tag = self.meta(src, false).map(|m| m.tag).unwrap_or(0);
        let result = if tag == winfs::IO_REPARSE_TAG_MOUNT_POINT {
            winfs::create_junction(&self.api(dst), &self.api(&target))
        } else {
            winfs::create_symlink(&self.api(dst), &target, is_dir)
        };
        if let Err(e) = result {
            let m = tr!(
                self.ctx,
                "command.cp.error.cannot_create_symlink",
                "cannot create symbolic link {dst} to {src}: {reason}",
                dst = q(dst),
                src = q(src),
                reason = winfs::reason(e)
            );
            self.fail(&m);
            return false;
        }
        self.verbose(src, dst, backup);
        true
    }

    // ------------------------------------------------------------------
    // directory recursion (copy.c:822 copy_dir)
    // ------------------------------------------------------------------

    pub fn copy_dir(&mut self, src: &str, dst: &str, ancestors: &mut Vec<(u32, u64)>) -> bool {
        let src_meta = match self.meta(src, true) {
            Ok(m) => m,
            Err(e) => {
                let m = tr!(
                    self.ctx,
                    "command.cp.error.cannot_stat",
                    "cannot stat {src}: {reason}",
                    src = q(src),
                    reason = winfs::reason(e)
                );
                self.fail(&m);
                return false;
            }
        };
        // cyclic link detection (copy.c:688 is_ancestor): a followed link
        // whose target is already on the descent path.
        if ancestors.contains(&src_meta.id()) {
            let m = tr!(
                self.ctx,
                "command.cp.error.cyclic_link",
                "cannot copy cyclic symbolic link {src}",
                src = q(src)
            );
            self.fail(&m);
            return false;
        }
        ancestors.push(src_meta.id());

        // Into-itself detection (copy.c:2290-2320): identity keys, case
        // folded, on component boundaries.
        if self.dir_key_contains(&self.api(src), &self.api(dst)) {
            let m = tr!(
                self.ctx,
                "command.cp.error.copy_into_itself",
                "cannot copy a directory, {src}, into itself, {dst}",
                src = q(src),
                dst = q(dst)
            );
            self.fail(&m);
            ancestors.pop();
            return false;
        }

        // dir-vs-non-dir: destination exists as a non-directory.
        if let Ok(dm) = self.meta(dst, false) {
            if !dm.is_dir() || dm.is_link() {
                if !dm.is_link() || !self.dst_follow_dir(dst) {
                    let m = tr!(
                        self.ctx,
                        "command.cp.error.overwrite_nondir_with_dir",
                        "cannot overwrite non-directory {dst} with directory {src}",
                        dst = q(dst),
                        src = q(src)
                    );
                    self.fail(&m);
                    ancestors.pop();
                    return false;
                }
            }
        }

        // create the destination directory if needed (copy.c:2930).
        let existed = self.meta(dst, true).map(|m| m.is_dir()).unwrap_or(false);
        if !existed {
            if let Err(e) = winfs::ensure_dir(&self.api(dst)) {
                let m = tr!(
                    self.ctx,
                    "command.cp.error.cannot_create_directory",
                    "cannot create directory {dst}: {reason}",
                    dst = q(dst),
                    reason = winfs::reason(e)
                );
                self.fail(&m);
                ancestors.pop();
                return false;
            }
            if self.opts.verbose {
                self.verbose(src, dst, None);
            }
        }

        let entries = match winfs::read_dir(&self.api(src)) {
            Ok(v) => v,
            Err(e) => {
                let m = tr!(
                    self.ctx,
                    "command.cp.error.cannot_open_directory",
                    "cannot open directory {src}: {reason}",
                    src = q(src),
                    reason = winfs::reason(e)
                );
                self.fail(&m);
                ancestors.pop();
                return false;
            }
        };

        let mut ok = true;
        for (name, attrs, tag, _size) in entries {
            let child_src = join(src, &name);
            let child_dst = join(dst, &name);
            let is_link =
                attrs & winfs::FILE_ATTRIBUTE_REPARSE_POINT != 0 && winfs::is_link_tag(tag);
            let deref = self.deref_here(false);
            if is_link && !deref {
                if !self.copy_file_child(&child_src, &child_dst) {
                    ok = false;
                }
                continue;
            }
            let child_meta = match self.meta(&child_src, is_link) {
                Ok(m) => m,
                Err(e) => {
                    let m = tr!(
                        self.ctx,
                        "command.cp.error.cannot_stat",
                        "cannot stat {src}: {reason}",
                        src = q(&child_src),
                        reason = winfs::reason(e)
                    );
                    self.fail(&m);
                    ok = false;
                    continue;
                }
            };
            if child_meta.is_dir() {
                if self.opts.one_file_system && child_meta.dev != src_meta.dev {
                    continue;
                }
                if !self.copy_dir(&child_src, &child_dst, ancestors) {
                    ok = false;
                }
            } else if !self.copy_file_child(&child_src, &child_dst) {
                ok = false;
            }
        }

        if ok {
            let _ = self.apply_preserve(src, &src_meta, dst, self.opts.preserve, true);
        }
        ancestors.pop();
        ok
    }

    fn dst_follow_dir(&self, dst: &str) -> bool {
        self.meta(dst, true).map(|m| m.is_dir()).unwrap_or(false)
    }

    /// child copy through copy_internal with command_line_arg=false.
    fn copy_file_child(&mut self, src: &str, dst: &str) -> bool {
        self.copy_file(src, dst, false)
    }

    fn dir_key_contains(&self, src: &str, dst: &str) -> bool {
        let sk = identity_key(src);
        let dk = identity_key(dst);
        if sk.is_empty() || dk.is_empty() {
            return false;
        }
        if dk == sk {
            return true;
        }
        let prefix = if sk.ends_with('\\') {
            sk
        } else {
            format!("{sk}\\")
        };
        dk.starts_with(&prefix)
    }

    // ------------------------------------------------------------------
    // byte copy / preservation
    // ------------------------------------------------------------------

    fn copy_bytes(&mut self, src: &str, dst: &str) -> Result<(), CopyError> {
        let mut infile = winfs::open_read(&self.api(src)).map_err(CopyError::OpenRead)?;
        let mut outfile = match winfs::open_write(&self.api(dst)) {
            Ok(f) => f,
            Err(_) if self.opts.force => {
                // copy.c:1455: -f unlinks the destination and retries.
                let _ = winfs::delete(&self.api(dst), false);
                winfs::open_write(&self.api(dst)).map_err(CopyError::Create)?
            }
            Err(e) => return Err(CopyError::Create(e)),
        };
        let sparse = self.opts.sparse != Sparse::Never;
        let mut buf = vec![0u8; 256 * 1024];
        loop {
            let n = winfs::read_chunk(&mut infile, &mut buf).map_err(|_| CopyError::Read)?;
            if n == 0 {
                break;
            }
            if sparse && n % 512 == 0 && buf[..n].iter().all(|&b| b == 0) {
                // --sparse=always: skip all-zero runs (64 KiB NTFS holes).
                // A plain seek leaves a hole when the file is sparse-marked;
                // on volumes that refuse, the data is still correct.
                use std::io::{Seek, SeekFrom};
                let _ = outfile.seek(SeekFrom::Current(n as i64));
                continue;
            }
            winfs::write_all(&mut outfile, &buf[..n]).map_err(|_| CopyError::Write)?;
        }
        // Flush to surface buffered write errors (ENOSPC family).
        let _ = outfile.flush();
        Ok(())
    }

    /// Apply the preserve set: mode -> read-only attribute, timestamps ->
    /// FILETIMEs; ownership/context/xattr are accepted with no Windows
    /// effect (documented 1.x parity). Directories get their times after
    /// the children have been written.
    fn apply_preserve(
        &mut self,
        src: &str,
        src_meta: &Meta,
        dst: &str,
        p: super::opts::Preserve,
        _is_dir: bool,
    ) -> Result<(), ()> {
        if p.timestamps {
            let _ = winfs::set_times(
                &self.api(dst),
                src_meta.btime,
                src_meta.atime,
                src_meta.mtime,
            );
        }
        if p.mode {
            // mode value model: the shared modemap bits decide the write
            // bits; the read-only attribute is the Windows carrier.
            let ro = src_meta.is_read_only()
                || modemap::current_mode(std::path::Path::new(&self.api(src))) & 0o222 == 0;
            let _ = winfs::set_read_only(&self.api(dst), ro);
        }
        Ok(())
    }

    // ------------------------------------------------------------------
    // messages
    // ------------------------------------------------------------------

    /// Backup path selection through the ctx-anchored existence probe.
    fn select_backup(&self, dst: &str) -> Option<String> {
        match self.opts.backup {
            Backup::None => None,
            Backup::Simple => Some(format!("{dst}{}", self.opts.suffix)),
            Backup::Numbered => Some(self.numbered_backup(dst, 1)),
            Backup::Existing => {
                if self.lexists(&format!("{dst}.~1~")) {
                    Some(self.numbered_backup(dst, 1))
                } else {
                    Some(format!("{dst}{}", self.opts.suffix))
                }
            }
        }
    }

    fn numbered_backup(&self, dst: &str, mut n: u64) -> String {
        loop {
            let cand = format!("{dst}.~{n}~");
            if !self.lexists(&cand) {
                return cand;
            }
            n += 1;
        }
    }

    pub fn verbose(&mut self, src: &str, dst: &str, backup: Option<&str>) {
        if !self.opts.verbose {
            return;
        }
        let mut line = format!("{} -> {}", q(src), q(dst));
        if let Some(b) = backup {
            line.push_str(&format!(" (backup: {})", q(b)));
        }
        let _ = writeln!(self.io.stdout, "{line}");
    }

    /// The `-i` prompt (copy.c:1988 overwrite_ok): write `cp: overwrite
    /// 'dst'? ` to stderr, read one line, accept a leading y/Y, EOF declines.
    fn prompt(&mut self, dst: &str) -> bool {
        let m = tr!(
            self.ctx,
            "command.cp.prompt.overwrite",
            "overwrite {dst}?",
            dst = q(dst)
        );
        let _ = write!(self.io.stderr, "cp: {m} ");
        let _ = self.io.stderr.flush();
        let mut line = Vec::new();
        let mut byte = [0u8; 1];
        loop {
            match std::io::Read::read(self.io.stdin, &mut byte) {
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
        match line.first() {
            Some(b'y') | Some(b'Y') => true,
            _ => false,
        }
    }

    pub fn omit_directory(&mut self, src: &str) -> bool {
        let m = tr!(
            self.ctx,
            "command.cp.error.omitting_directory",
            "-r not specified; omitting directory {src}",
            src = q(src)
        );
        self.fail(&m);
        false
    }
}

enum CopyError {
    OpenRead(u32),
    Read,
    Create(u32),
    Write,
}

// ----------------------------------------------------------------------
// lexical helpers
// ----------------------------------------------------------------------

/// GNU `last_component`: the final component with trailing slashes removed.
pub fn last_component(path: &str) -> String {
    let trimmed = path.trim_end_matches(['/', '\\']);
    if trimmed.is_empty() {
        return path.to_string();
    }
    match trimmed.rfind(['/', '\\']) {
        Some(i) => {
            let base = &trimmed[i + 1..];
            // cp --target-directory: `cp -R source/.. dest` must not copy
            // into `dest/..` (cp.c:706); GNU rewrites ".." to ".".
            if base == ".." {
                ".".to_string()
            } else {
                base.to_string()
            }
        }
        None => trimmed.to_string(),
    }
}

/// Join two operand-spelled names with GNU's `/` display separator (Win32
/// accepts forward slashes for every call, so display and OS path agree).
pub fn join(base: &str, name: &str) -> String {
    if base.is_empty() || base == "." {
        return if base.is_empty() {
            name.to_string()
        } else {
            join(".", name)
        };
    }
    if base.ends_with(['/', '\\']) {
        format!("{base}{name}")
    } else {
        format!("{base}/{name}")
    }
}

/// `--parents` graft (cp.c:655-690 + the Windows drive decision):
/// strip the drive designator and leading separators, drop `.` components,
/// so `/a/b` -> `a/b` and `C:\a\b` -> `a/b`. UNC (`\\server\share\x`)
/// becomes `server/share/x`.
pub fn graft_relative(src: &str) -> String {
    let mut s = src.replace('\\', "/");
    if s.len() >= 2 && s.as_bytes()[1] == b':' {
        s = s[2..].to_string();
    }
    let comps: Vec<&str> = s
        .split('/')
        .filter(|c| !c.is_empty() && *c != ".")
        .collect();
    comps.join("/")
}

/// `(dev,ino)`-free lexical identity key for the into-itself check
/// (copy.c:2296 directory_key_contains): absolute, lexically normalized,
/// ASCII case folded (NTFS compares case-insensitively).
fn identity_key(path: &str) -> String {
    let p = std::path::Path::new(path);
    let abs = if p.is_absolute() {
        p.to_path_buf()
    } else {
        match std::env::current_dir() {
            Ok(cwd) => cwd.join(p),
            Err(_) => p.to_path_buf(),
        }
    };
    let mut out = String::new();
    for comp in abs.components() {
        let s = comp.as_os_str().to_string_lossy();
        match comp {
            std::path::Component::Prefix(_) | std::path::Component::RootDir => {
                out.push_str(&s);
            }
            std::path::Component::CurDir => {}
            std::path::Component::ParentDir => {
                if let Some(i) = out.rfind('\\') {
                    out.truncate(i);
                }
            }
            std::path::Component::Normal(_) => {
                if !out.ends_with('\\') {
                    out.push('\\');
                }
                out.push_str(&s);
            }
        }
    }
    out.to_ascii_uppercase()
}

/// Absolute native path of an operand through the shared boundary.
pub fn api_for(ctx: &Ctx, name: &str) -> String {
    fsutil::resolve(ctx, name).to_string_lossy().into_owned()
}
