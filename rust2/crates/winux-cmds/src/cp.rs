//! `cp` — GNU coreutils 9.4 semantics on the Windows value model
//! (#1488 rust2 lane, WP-J; oracle: WSL `cp (GNU coreutils) 9.4`,
//! 9.4-3ubuntu6.2).
//!
//! Layout:
//! * [`opts`] — sequential getopt_long port (cp.c:968-1245): last-wins
//!   `-i`/`-n`, `-H`/`-L`/`-P`/`-d`/`-a`, `--update=WHEN` rewriting the
//!   interactive field, argmatch for `--sparse`/`--reflink`/`--update`/
//!   `--backup`/`--preserve`, `-t`/`-T`/operand grammar decisions.
//! * [`copy`] — the copy_internal / copy_reg / copy_dir ladder, backup,
//!   prompt, preserve, verbose and `--preserve=links`.
//! * [`winfs`] — raw kernel32 FFI (identity, reparse tags/targets, junction
//!   creation, times, attributes, enumeration) in the find/winfs family.
//! * `tests` — filesystem-level unit tests over a scratch root.
//!
//! GNU owners are cited per function in [`copy`]; the operand planner is
//! cp.c:608 `do_copy`, the option state cp.c:968 `main`, `-p`/`-a`/
//! `--preserve` decoding cp.c:876 `decode_preserve_arg`.
//!
//! Documented platform / 1.x divergences:
//! * file symlinks need Developer Mode; `cp -d` of a *file* symlink reports
//!   GNU's `Operation not permitted` (the #1647 fix is about links being
//!   COPIED at all: junction sources — the unprivileged directory-link form
//!   — are recreated and dereferenced correctly, and `cp -r` no longer
//!   aborts on a link child);
//! * junctions count as symbolic links (1.x #1064 rule);
//! * `--preserve=ownership/context/xattr` are accepted list arithmetic with
//!   no Windows effect; mode is the read-only attribute, timestamps are
//!   FILETIMEs;
//! * `--reflink=always` is clone-or-fail with ENOTSUP on non-ReFS volumes
//!   (same rc/stdout as the ext4 oracle); `--sparse` skips all-zero 64 KiB
//!   blocks;
//! * `-S` does not imply `-b` (probed 9.4-3ubuntu6.2 oracle; the upstream
//!   9.4 tarball sets make_backups, recorded as an oracle divergence);
//! * `--update=none-fail` does not exist in 9.4 (the 1.x C++ accepted it —
//!   that extension retires with the wiring, matching the oracle's
//!   argmatch error and rc 1);
//! * `-n` behaves as `--update=none` (silent skip, rc 0, no `-b` conflict,
//!   deprecation warning) — the Ubuntu 9.4 oracle's patched behavior;
//! * `--parents` grafts an absolute Windows source below the root without
//!   its drive designator (`C:\a\b` -> `DEST/a/b`; GNU keeps `/a/b` minus
//!   the slash), and UNC becomes `server/share/...`.
//!
//! 1.x C++ behaviors that are intentionally corrected here:
//! * #1647 — every symlink/junction source failed with `cannot open ...
//!   Invalid argument`; the Rust path opens links by tag, recreates them
//!   under `-d`/`-a`/plain `-r` and follows them under `-L`/`-H`;
//! * #1648 — `-i` refusal returned 0; GNU returns 1 and so does this port;
//! * the cp face of #1649 — a directory source onto a non-directory
//!   destination is refused with `cannot overwrite non-directory ...`
//!   (the C++ shared path silently replaced the file);
//! * the cp face of #1650 — an explicit empty operand is a path, so
//!   `cp '' out` reports `cannot stat '': No such file or directory`;
//! * the cp face of #1651 — `-i`/`-f`/`-n` are last-wins in argv order;
//! * `-v --attributes-only` prints GNU's plain `'a' -> 'b'` (the C++ added
//!   a `(attributes only)` suffix);
//! * `-v` with `-b` prints `(backup: 'b~')` (the C++ omitted it).

pub mod copy;
pub mod opts;
pub mod winfs;

#[cfg(test)]
mod tests;

use std::ffi::OsString;

use winux_core::{tr, CmdResult, Ctx, Io};

use copy::Copier;
use opts::UsageError;

/// `cp` entry point.
pub fn cp_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let (opts, operands) = match opts::parse(args, ctx) {
        Ok(v) => v,
        Err(UsageError(msg)) => {
            let _ = writeln!(io.stderr, "cp: {msg}");
            return Ok(1);
        }
    };
    if opts.help || opts.version {
        // The product dispatcher owns the branded help/version surface;
        // the standalone driver only has to exit 0 (ln/stat precedent).
        return Ok(0);
    }
    if let Err(UsageError(msg)) = opts::validate(ctx, &opts) {
        let _ = writeln!(io.stderr, "cp: {msg}");
        return Ok(1);
    }
    // Ubuntu 9.4-3ubuntu6.2 oracle: -n announces its deprecation once.
    if opts.no_clobber_seen {
        let m = tr!(
            ctx,
            "command.cp.warning.no_clobber_deprecated",
            "warning: behavior of -n is non-portable and may change in future; use --update=none instead"
        );
        let _ = writeln!(io.stderr, "cp: {m}");
    }

    let plan = match plan(&opts, &operands, io, ctx) {
        Ok(p) => p,
        Err(UsageError(msg)) => {
            let _ = writeln!(io.stderr, "cp: {msg}");
            return Ok(1);
        }
    };

    let mut copier = Copier::new(ctx, &opts, io);
    for src in &plan.sources {
        let dst = if plan.dest_is_dir {
            if opts.parents {
                let graft = copy::graft_relative(src);
                make_parents(&mut copier, &plan.dest, src, &graft);
                copy::join(&plan.dest, &graft)
            } else {
                copy::join(&plan.dest, &copy::last_component(src))
            }
        } else {
            plan.dest.clone()
        };
        process_source(&mut copier, src, &dst);
    }
    Ok(if copier.failed { 1 } else { 0 })
}

struct Plan {
    sources: Vec<String>,
    dest: String,
    dest_is_dir: bool,
}

/// Operand grammar (cp.c:608 do_copy) — diagnostics carry GNU's
/// "Try 'cp --help'" second line.
fn plan(
    opts: &opts::Options,
    operands: &[String],
    io: &mut Io,
    ctx: &Ctx,
) -> Result<Plan, UsageError> {
    let _ = io;
    let usage_tail =
        |msg: String| UsageError(format!("{msg}\nTry 'cp --help' for more information."));

    if let Some(target) = &opts.target_directory {
        if opts.no_target_directory {
            let msg = tr!(
                ctx,
                "command.cp.error.target_dir_and_no_target_dir",
                "cannot combine --target-directory (-t) and --no-target-directory (-T)"
            );
            return Err(usage_tail(msg));
        }
        // cp.c:613: the operand-count check precedes target validation.
        if operands.is_empty() {
            let msg = tr!(
                ctx,
                "command.cp.error.missing_file_operand",
                "missing file operand"
            );
            return Err(usage_tail(msg));
        }
        let m = winfs::meta(&copy::api_for(ctx, target), true);
        match m {
            Err(e) => {
                let msg = tr!(
                    ctx,
                    "command.cp.error.target_directory",
                    "target directory {dir}: {reason}",
                    dir = opts::q(target),
                    reason = winfs::reason(e)
                );
                return Err(UsageError(msg));
            }
            Ok(meta) if !meta.is_dir() => {
                let msg = tr!(
                    ctx,
                    "command.cp.error.target_directory",
                    "target directory {dir}: {reason}",
                    dir = opts::q(target),
                    reason = "Not a directory"
                );
                return Err(UsageError(msg));
            }
            Ok(_) => {}
        }
        return Ok(Plan {
            sources: strip_all(opts, operands),
            dest: target.clone(),
            dest_is_dir: true,
        });
    }

    if opts.no_target_directory {
        if operands.len() > 2 {
            let msg = tr!(
                ctx,
                "command.cp.error.extra_operand",
                "extra operand {operand}",
                operand = opts::q(&operands[2])
            );
            return Err(usage_tail(msg));
        }
    }

    if operands.is_empty() {
        let msg = tr!(
            ctx,
            "command.cp.error.missing_file_operand",
            "missing file operand"
        );
        return Err(usage_tail(msg));
    }
    if operands.len() == 1 {
        let msg = tr!(
            ctx,
            "command.cp.error.missing_destination",
            "missing destination file operand after {operand}",
            operand = opts::q(&operands[0])
        );
        return Err(usage_tail(msg));
    }

    let dest_idx = operands.len() - 1;
    let dest = operands[dest_idx].clone();
    // target_directory_operand open(O_DIRECTORY): follows a link to a dir.
    let dest_is_dir = !opts.no_target_directory
        && !dest.is_empty()
        && winfs::meta(&copy::api_for(ctx, &dest), true)
            .map(|m| m.is_dir())
            .unwrap_or(false);

    if !dest_is_dir && operands.len() > 2 {
        let reason = if winfs::attributes(&copy::api_for(ctx, &dest)).is_some() {
            "Not a directory"
        } else {
            "No such file or directory"
        };
        let msg = tr!(
            ctx,
            "command.cp.error.target",
            "target {operand}: {reason}",
            operand = opts::q(&dest),
            reason = reason
        );
        return Err(usage_tail(msg));
    }

    if opts.parents && !dest_is_dir {
        let msg = tr!(
            ctx,
            "command.cp.error.parents_needs_directory",
            "with --parents, the destination must be a directory"
        );
        return Err(usage_tail(msg));
    }

    Ok(Plan {
        sources: if dest_is_dir {
            strip_all(opts, &operands[..dest_idx])
        } else {
            operands[..dest_idx].to_vec()
        },
        dest,
        dest_is_dir,
    })
}

fn strip_all(opts: &opts::Options, list: &[String]) -> Vec<String> {
    list.iter()
        .map(|s| {
            if opts.strip_trailing_slashes {
                s.trim_end_matches('/').to_string()
            } else {
                s.clone()
            }
        })
        .collect()
}

/// One SOURCE operand (cp.c:1935 process_source_paths).
fn process_source(c: &mut Copier, src: &str, dst: &str) {
    if src.ends_with(['/', '\\']) {
        // stat("a/") on a regular file is ENOTDIR (GNU probes the spelling
        // as typed; --strip-trailing-slashes only applies in the
        // target-directory form, cp.c:655).
        let mapped = copy::api_for(c.ctx, src);
        let is_dir = winfs::meta(&mapped, true)
            .map(|m| m.is_dir())
            .unwrap_or(false);
        if !is_dir {
            let m = tr!(
                c.ctx,
                "command.cp.error.cannot_stat",
                "cannot stat {src}: {reason}",
                src = opts::q(src),
                reason = "Not a directory"
            );
            c.fail(&m);
            return;
        }
    }
    let deref = c.deref_here(true);
    // A dangling link still "exists" when the link itself is copied.
    let exists = if deref {
        c.meta(src, true).is_ok()
    } else {
        c.lexists(src)
    };
    if !exists {
        let m = tr!(
            c.ctx,
            "command.cp.error.cannot_stat",
            "cannot stat {src}: {reason}",
            src = opts::q(src),
            reason = "No such file or directory"
        );
        c.fail(&m);
        return;
    }
    let is_dir = c
        .meta(src, deref)
        .map(|m| m.is_dir() && !(m.is_link() && !deref))
        .unwrap_or(false);
    if is_dir {
        if !c.opts.recursive {
            c.omit_directory(src);
            return;
        }
        let mut ancestors = Vec::new();
        c.copy_dir(src, dst, &mut ancestors);
    } else {
        c.copy_file(src, dst, true);
    }
}

/// Create the `--parents` intermediate directories, with GNU's verbose
/// directory lines (`p -> d/p`, unquoted — `make_dir_parents_private`).
fn make_parents(c: &mut Copier, target: &str, _src: &str, graft: &str) {
    let comps: Vec<&str> = graft.split('/').filter(|c| !c.is_empty()).collect();
    if comps.len() <= 1 {
        return;
    }
    for k in 1..comps.len() {
        let prefix = comps[..k].join("/");
        let dir = copy::join(target, &prefix);
        if c.lexists(&dir) {
            continue;
        }
        if crate::cp::winfs::ensure_dir(&c.api(&dir)).is_ok() {
            if c.opts.verbose {
                let _ = writeln!(c.io.stdout, "{prefix} -> {dir}");
            }
        }
    }
}
