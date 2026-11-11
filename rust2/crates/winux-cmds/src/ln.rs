//! `ln` — GNU coreutils ln semantics on the Windows link APIs.
//!
//! Hard links go through CreateHardLinkW; -s symlinks through
//! CreateSymbolicLinkW with the ALLOW_UNPRIVILEGED_CREATE bit (outside
//! Developer Mode the call fails with ERROR_PRIVILEGE_NOT_HELD, reported
//! with GNU's error shape and errno text `Operation not permitted`, rc 1 —
//! the documented 1.x C++ behavior this port inherits).
//!
//! Operand grammar (ln.c): 2 operands = link src -> dst; more than two =
//! the last operand must be an existing directory (`target 'X': No such
//! file or directory` otherwise); 1 operand + no -t = link into `.`.
//! -t DIR / -T pin the destination. -f first removes a non-directory dst
//! (checking symlink_status so a symlink-to-dir is still removed);
//! -i prompts `ln: replace 'X'? ` on stdin (any leading y replaces);
//! -b/--backup[=CONTROL] and -S rename the old dst first (simple `~`,
//! numbered `.~N~`, existing = numbered-if-present-else-simple).
//! -v reports on stdout: hard `'dst' => 'src'`, symlink `'dst' -> 'src'`
//! (GNU probed). `-` is a plain name (POSIX). Directory hard links are
//! refused with `X: hard link not allowed for directory` (rc 1) — the
//! non-superuser message, which on Windows is every user.
//!
//! A missing SOURCE operand reports `failed to access 'X': ...` and the
//! remaining operands are still processed; with -s the symlink is created
//! dangling instead (GNU creates it without touching SOURCE). rc is 1 if
//! anything failed.

use std::ffi::OsString;

use crate::fs_win;
use winux_core::{usage_error, CmdResult, Ctx, Io};

struct LnOpts {
    force: bool,
    interactive: bool,
    symbolic: bool,
    verbose: bool,
    no_dereference: bool,
    directory: bool,
    relative: bool,
    target_dir: Option<String>,
    no_target_dir: bool,
    backup: bool,
    backup_control: Option<String>,
    suffix: String,
}

enum BackupKind {
    None,
    Simple,
    Numbered,
    Existing,
}

fn backup_kind(opts: &LnOpts) -> BackupKind {
    match &opts.backup_control {
        Some(c) => match c.to_ascii_lowercase().as_str() {
            "none" | "off" => BackupKind::None,
            "numbered" | "n" => BackupKind::Numbered,
            "simple" | "never" => BackupKind::Simple,
            "existing" | "nil" => BackupKind::Existing,
            _ => BackupKind::Simple, // GNU would reject; keep simple
        },
        None => {
            if opts.backup {
                BackupKind::Simple
            } else {
                BackupKind::None
            }
        }
    }
}

pub fn ln_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut opts = LnOpts {
        force: false,
        interactive: false,
        symbolic: false,
        verbose: false,
        no_dereference: false,
        directory: false,
        relative: false,
        target_dir: None,
        no_target_dir: false,
        backup: false,
        backup_control: None,
        suffix: "~".to_string(),
    };
    let mut operands: Vec<String> = Vec::new();
    let mut idx = 0usize;
    while idx < args.len() {
        let arg = args[idx].to_string_lossy().into_owned();
        if arg == "--" {
            idx += 1;
            for a in &args[idx..] {
                operands.push(a.to_string_lossy().into_owned());
            }
            break;
        }
        if arg == "-" || !arg.starts_with('-') {
            operands.push(arg);
            idx += 1;
            continue;
        }
        if let Some(long) = arg.strip_prefix("--") {
            let (name, inline) = match long.split_once('=') {
                Some((n, v)) => (n.to_string(), Some(v.to_string())),
                None => (long.to_string(), None),
            };
            match name.as_str() {
                "force" => opts.force = true,
                "interactive" => opts.interactive = true,
                "symbolic" => opts.symbolic = true,
                "verbose" => opts.verbose = true,
                "no-dereference" => opts.no_dereference = true,
                "directory" => opts.directory = true,
                "logical" | "physical" => { /* -L/-P: accepted, no-op for -s */ }
                "backup" => {
                    opts.backup = true;
                    if let Some(v) = inline {
                        opts.backup_control = Some(v);
                    }
                }
                "suffix" => match inline {
                    Some(v) => opts.suffix = v,
                    // bare --suffix has no GNU long form; keep the default
                    None => {}
                },
                "target-directory" => match inline {
                    Some(v) => opts.target_dir = Some(v),
                    None => {
                        idx += 1;
                        match args.get(idx) {
                            Some(v) => opts.target_dir = Some(v.to_string_lossy().into_owned()),
                            None => {
                                let _ = usage_error(
                                    io,
                                    "ln",
                                    "option '--target-directory' requires an argument",
                                );
                                return Ok(1);
                            }
                        }
                    }
                },
                "no-target-directory" => opts.no_target_dir = true,
                "relative" => opts.relative = true,
                "help" | "version" => return Ok(0),
                _ => {
                    let _ = usage_error(io, "ln", &format!("unrecognized option '{arg}'"));
                    return Ok(1);
                }
            }
            idx += 1;
            continue;
        }
        let chars: Vec<char> = arg.chars().skip(1).collect();
        let mut ci = 0usize;
        let mut consumed_next = false;
        while ci < chars.len() {
            let c = chars[ci];
            match c {
                'f' => opts.force = true,
                'i' => opts.interactive = true,
                's' => opts.symbolic = true,
                'v' => opts.verbose = true,
                'n' => opts.no_dereference = true,
                'd' | 'F' => opts.directory = true,
                'L' | 'P' => { /* accepted, no-op for -s */ }
                'b' => opts.backup = true,
                'S' => {
                    let rest: String = chars[ci + 1..].iter().collect();
                    if !rest.is_empty() {
                        opts.suffix = rest;
                        ci = chars.len();
                    } else {
                        match args.get(idx + 1) {
                            Some(v) => {
                                opts.suffix = v.to_string_lossy().into_owned();
                                consumed_next = true;
                                ci = chars.len();
                            }
                            None => {
                                let _ = usage_error(io, "ln", "option requires an argument -- 'S'");
                                return Ok(1);
                            }
                        }
                    }
                }
                't' => {
                    let rest: String = chars[ci + 1..].iter().collect();
                    let v = if !rest.is_empty() {
                        ci = chars.len();
                        rest
                    } else {
                        match args.get(idx + 1) {
                            Some(v) => {
                                consumed_next = true;
                                ci = chars.len();
                                v.to_string_lossy().into_owned()
                            }
                            None => {
                                let _ = usage_error(io, "ln", "option requires an argument -- 't'");
                                return Ok(1);
                            }
                        }
                    };
                    opts.target_dir = Some(v);
                }
                'T' => opts.no_target_dir = true,
                'r' => opts.relative = true,
                c => {
                    let _ = usage_error(io, "ln", &format!("invalid option -- '{c}'"));
                    return Ok(1);
                }
            }
            ci += 1;
        }
        idx += if consumed_next { 2 } else { 1 };
    }
    // [GNU] --relative requires --symbolic; checked right after the option
    // walk, before the missing-operand validation (probed in the
    // differential harness against coreutils 9.4: the command fails with
    // the bare `ln: cannot do --relative without --symbolic` line — no
    // "Try" hint — creating nothing; issue #1462 audit, ln.c:571-572, and
    // the 1.x C++ build prints the same shape, which the corpus case
    // corpus/ln/36-relative-without-symbolic pins).
    if opts.relative && !opts.symbolic {
        let _ = writeln!(io.stderr, "ln: cannot do --relative without --symbolic");
        return Ok(1);
    }
    // resolve the destination directory
    let dir_mode: Option<String> = if opts.no_target_dir {
        None
    } else if let Some(t) = &opts.target_dir {
        Some(t.clone())
    } else {
        None
    };
    let mut ok = true;
    if let Some(dir) = &dir_mode {
        // all operands are sources, links go into DIR
        if operands.is_empty() {
            let _ = usage_error(io, "ln", "missing file operand");
            return Ok(1);
        }
        if !std::path::Path::new(&resolve(ctx, dir)).is_dir() {
            let _ = writeln!(
                io.stderr,
                "ln: target '{dir}': {}",
                reason_path(&resolve(ctx, dir))
            );
            return Ok(1);
        }
        for src in &operands {
            let name = basename_of(src);
            if !link_one(io, ctx, &opts, src, &join_lex(&dir, &name), true, &mut ok) {
                ok = false;
            }
        }
        return Ok(if ok { 0 } else { 1 });
    }
    match operands.len() {
        0 => {
            let _ = usage_error(io, "ln", "missing file operand");
            Ok(1)
        }
        1 => {
            if opts.no_target_dir {
                let _ = usage_error(
                    io,
                    "ln",
                    &format!("missing destination file operand after '{}'", operands[0]),
                );
                return Ok(1);
            }
            // link into ".": GNU joins the "." directory with the
            // basename; the 1.x C++ build joined natively and byte-parity
            // keeps that shape ("\\aln" in the EEXIST diagnostic).
            let name = basename_of(&operands[0]);
            if !link_one(
                io,
                ctx,
                &opts,
                &operands[0],
                &format!(".\\{name}"),
                true,
                &mut ok,
            ) {
                ok = false;
            }
            Ok(if ok { 0 } else { 1 })
        }
        2 => {
            if opts.no_target_dir || !is_existing_dir(ctx, &operands[1]) {
                let dst = &operands[1];
                if !link_one(io, ctx, &opts, &operands[0], dst, false, &mut ok) {
                    ok = false;
                }
                Ok(if ok { 0 } else { 1 })
            } else {
                // dst is a directory: link the source into it
                let name = basename_of(&operands[0]);
                if !link_one(
                    io,
                    ctx,
                    &opts,
                    &operands[0],
                    &join_lex(&operands[1], &name),
                    true,
                    &mut ok,
                ) {
                    ok = false;
                }
                Ok(if ok { 0 } else { 1 })
            }
        }
        _ => {
            // 3+ operands: the last must be an existing directory
            let last = &operands[operands.len() - 1];
            if opts.no_target_dir {
                let _ = usage_error(io, "ln", &format!("extra operand '{}'", operands[1]));
                return Ok(1);
            }
            if !is_existing_dir(ctx, last) {
                let _ = writeln!(
                    io.stderr,
                    "ln: target '{last}': {}",
                    reason_path(&resolve(ctx, last))
                );
                return Ok(1);
            }
            for src in &operands[..operands.len() - 1] {
                let name = basename_of(src);
                if !link_one(io, ctx, &opts, src, &join_lex(last, &name), true, &mut ok) {
                    ok = false;
                }
            }
            Ok(if ok { 0 } else { 1 })
        }
    }
}

fn resolve(ctx: &Ctx, p: &str) -> std::path::PathBuf {
    // Route every operand through the one boundary (fs_win::api_path):
    // POSIX drive dialect folds, the #1283 carrier-colon fold, the
    // dot-segment collapse and the \\?\ extension all happen there, so
    // "C:/...", "/c/...", "C:name" and relative spellings all reach the
    // OS as the same file the 1.x build resolved (the diagnostic layer
    // keeps the user's spelling; this layer keeps the resolution).
    std::path::PathBuf::from(fs_win::api_path(&ctx.cwd, p))
}

fn is_existing_dir(ctx: &Ctx, p: &str) -> bool {
    let full = resolve(ctx, p);
    // symlink-to-dir counts as a directory destination only when not -n
    full.is_dir()
        && !std::fs::symlink_metadata(&full)
            .map(|m| m.file_type().is_symlink())
            .unwrap_or(false)
}

fn basename_of(p: &str) -> String {
    // Windows operands arrive in both spellings ("sub/source.txt" and
    // "sub\\source.txt"); the basename splits on both separators and the
    // trailing-separator strip covers both too (the 1.x operand boundary
    // normalized before this split).
    let t = p.trim_end_matches(['/', '\\']);
    match t.rsplit_once(['/', '\\']) {
        Some((_, b)) => b.to_string(),
        None => t.to_string(),
    }
}

fn join_lex(dir: &str, name: &str) -> String {
    let d = dir.trim_end_matches(['/', '\\']);
    if d.is_empty() {
        format!("/{name}")
    } else {
        // native separator shape: the verbose line and diagnostics spell
        // derived destinations like the 1.x build ("dest\\a.txt", the
        // byte-parity shape the corpus pins)
        format!("{d}\\{name}")
    }
}

fn reason_path(p: &std::path::Path) -> &'static str {
    if !p.parent().map(|x| x.exists()).unwrap_or(false) || !p.exists() {
        "No such file or directory"
    } else {
        "Not a directory"
    }
}

/// The -i prompt: `ln: replace 'X'? ` on STDERR (GNU probed), answer read
/// from the injected stdin.
fn prompt_replace(io: &mut Io, dst: &str) -> std::io::Result<bool> {
    write!(io.stderr, "ln: replace '{dst}'? ")?;
    io.stderr.flush()?;
    let mut buf = [0u8; 1];
    let mut answer = Vec::new();
    while let Ok(n) = io.stdin.read(&mut buf) {
        if n == 0 || buf[0] == b'\n' {
            break;
        }
        answer.push(buf[0]);
        if answer.len() > 64 {
            break;
        }
    }
    Ok(answer
        .first()
        .map(|c| c.to_ascii_lowercase() == b'y')
        .unwrap_or(false))
}

/// Make the backup of an existing dst; returns the backup path made.
fn make_backup(ctx: &Ctx, opts: &LnOpts, dst: &str) -> Option<String> {
    let kind = backup_kind(opts);
    match kind {
        BackupKind::None => None,
        BackupKind::Simple => {
            let b = format!("{dst}{}", opts.suffix);
            rename(ctx, dst, &b);
            Some(b)
        }
        BackupKind::Numbered => {
            for n in 1..100u32 {
                let b = format!("{dst}.~{n}~");
                if !resolve(ctx, &b).exists() {
                    rename(ctx, dst, &b);
                    return Some(b);
                }
            }
            None
        }
        BackupKind::Existing => {
            for n in 1..100u32 {
                if resolve(ctx, &format!("{dst}.~{n}~")).exists() {
                    let b = format!("{dst}.~{n}~");
                    rename(ctx, dst, &b);
                    return Some(b);
                }
            }
            let b = format!("{dst}{}", opts.suffix);
            rename(ctx, dst, &b);
            Some(b)
        }
    }
}

fn rename(ctx: &Ctx, from: &str, to: &str) {
    let _ = std::fs::rename(resolve(ctx, from), resolve(ctx, to));
}

enum RemoveOutcome {
    Gone,
    Declined,
    Refused,
    /// -f/-i confirmed the removal but the OS refused (readonly dest):
    /// carries the GNU reason text for "failed to remove 'X': <reason>".
    RemoveFailed(String),
}

/// Remove an existing destination per -f/-i. GNU: an interactive 'n'
/// declines SILENTLY (rc stays 0); a plain existing dst without -f is the
/// File-exists error.
fn remove_existing(io: &mut Io, ctx: &Ctx, opts: &LnOpts, dst: &str) -> RemoveOutcome {
    let full = resolve(ctx, dst);
    if std::fs::symlink_metadata(&full).is_err() {
        return RemoveOutcome::Gone; // nothing there
    }
    if opts.interactive {
        match prompt_replace(io, dst) {
            Ok(true) => {}
            Ok(false) => return RemoveOutcome::Declined,
            Err(_) => return RemoveOutcome::Declined,
        }
    } else if !opts.force {
        return RemoveOutcome::Refused;
    }
    // DeleteFileW, not std::fs::remove_file: the std helper clears the
    // read-only attribute before deleting, which would silently unlink
    // the exact case the corpus pins (readonly dest -> "failed to remove
    // 'link.txt': Permission denied", the GNU-on-Windows/MSYS shape)
    if fs_win::delete_file(&ctx.cwd, dst).is_ok() {
        RemoveOutcome::Gone
    } else {
        // a remove that was ATTEMPTED and failed (readonly destination ->
        // ERROR_ACCESS_DENIED) reports the removal failure, not File
        // exists (GNU ln -f on an unremovable dest: "failed to remove
        // 'X': <reason>"; probed coreutils 9.4 + the 1.x corpus test)
        let reason = fs_win::delete_file(&ctx.cwd, dst)
            .err()
            .map(fs_win::win32_reason)
            .unwrap_or("Permission denied");
        RemoveOutcome::RemoveFailed(reason.to_string())
    }
}

/// Link one source to one destination. `dst_inferred` marks destinations
/// GNU derives by joining a directory (dir target, -t, or the one-operand
/// "." rule): GNU attempts the link there and reports the OS failure
/// instead of running its same-file pre-check (probed coreutils 9.4:
/// `ln f` reports "failed to create hard link '.\\f': File exists", while
/// `ln f f` reports "'f' and 'f' are the same file").
fn link_one(
    io: &mut Io,
    ctx: &Ctx,
    opts: &LnOpts,
    src: &str,
    dst: &str,
    dst_inferred: bool,
    ok: &mut bool,
) -> bool {
    // source must exist for hard links (GNU: failed to access 'X'); a
    // symbolic link may dangle — GNU creates it without touching SOURCE
    // (probed: `ln -s missing sl` succeeds silently, rc 0)
    let src_meta = std::fs::symlink_metadata(resolve(ctx, src));
    if src_meta.is_err() {
        if opts.symbolic {
            // dangling symlink: fall through to creation
        } else {
            let _ = writeln!(
                io.stderr,
                "ln: failed to access '{src}': No such file or directory"
            );
            *ok = false;
            return false;
        }
    }
    let src_is_dir = src_meta.map(|m| m.is_dir()).unwrap_or(false);
    if opts.symbolic && src_is_dir {
        // -s to a directory operand: the symlink points at the directory
    }
    if !opts.symbolic && src_is_dir {
        // hard link to a directory: refused (Windows never allows it)
        let _ = writeln!(io.stderr, "ln: {src}: hard link not allowed for directory");
        *ok = false;
        return false;
    }
    let dst_full = resolve(ctx, dst);
    let dst_exists = std::fs::symlink_metadata(&dst_full).is_ok();
    let dst_is_dir = dst_exists
        && dst_full.is_dir()
        && !std::fs::symlink_metadata(&dst_full)
            .map(|m| m.file_type().is_symlink())
            .unwrap_or(false);
    if dst_exists {
        if dst_is_dir && !opts.no_dereference {
            let _ = writeln!(io.stderr, "ln: {dst}: cannot overwrite directory");
            *ok = false;
            return false;
        }
        let same = !dst_inferred
            && dst_full.exists()
            && src_full_exists(ctx, src)
            && same_file(ctx, src, dst);
        if same {
            let _ = writeln!(io.stderr, "ln: '{src}' and '{dst}' are the same file");
            *ok = false;
            return false;
        }
        if opts.backup {
            make_backup(ctx, opts, dst);
        } else {
            match remove_existing(io, ctx, opts, dst) {
                RemoveOutcome::Gone => {}
                // GNU: answering 'n' declines the link and FAILS the
                // command (rc 1), silently
                RemoveOutcome::Declined => {
                    *ok = false;
                    return false;
                }
                RemoveOutcome::Refused => {
                    let _ = writeln!(
                        io.stderr,
                        "ln: failed to create {} '{dst}': File exists",
                        if opts.symbolic {
                            "symbolic link"
                        } else {
                            "hard link"
                        }
                    );
                    *ok = false;
                    return false;
                }
                RemoveOutcome::RemoveFailed(reason) => {
                    let _ = writeln!(io.stderr, "ln: failed to remove '{dst}': {reason}");
                    *ok = false;
                    return false;
                }
            }
        }
    }
    let rc = if opts.symbolic {
        let dir_flag = src_is_dir;
        let link_target = if opts.relative {
            relative_target(ctx, src, dst)
        } else {
            src.to_string()
        };
        fs_win::create_symlink(&ctx.cwd, &link_target, dst, dir_flag)
    } else {
        fs_win::hard_link(&ctx.cwd, src, dst)
    };
    match rc {
        Ok(()) => {
            if opts.verbose {
                let arrow = if opts.symbolic { "->" } else { "=>" };
                let shown = if opts.symbolic && opts.relative {
                    relative_target(ctx, src, dst)
                } else {
                    src.to_string()
                };
                let _ = writeln!(io.stdout, "'{dst}' {arrow} '{shown}'");
            }
            true
        }
        Err(e) => {
            // [#1491] opt-in copy downgrade for hosts without the symlink
            // privilege (the 1.x build's contract): WINUX_LN_FALLBACK=copy
            // downgrades a failed -s creation to a silent plain-file copy.
            // Only a regular-file source can be downgraded; a dangling or
            // directory source keeps the GNU-style refusal (the 1.x
            // checked the same attribute classes). The copy is SILENT:
            // bats-core asserts byte-exact TAP streams, so a notice here
            // breaks the ecosystem the opt-in exists to unblock.
            if opts.symbolic
                && (e == 1314 || e == 1)
                && std::env::var_os("WINUX_LN_FALLBACK")
                    .map(|v| v.to_string_lossy().eq_ignore_ascii_case("copy"))
                    .unwrap_or(false)
            {
                if let Ok(m) = fs_win::file_meta(&ctx.cwd, src, true) {
                    if !m.is_dir && !m.is_symlink {
                        let from = resolve(ctx, src);
                        let to = resolve(ctx, dst);
                        if std::fs::copy(&from, &to).is_ok() {
                            return true;
                        }
                    }
                }
            }
            let kind = if opts.symbolic {
                "symbolic link"
            } else {
                "hard link"
            };
            let _ = writeln!(
                io.stderr,
                "ln: failed to create {kind} '{dst}': {}",
                fs_win::win32_reason(e)
            );
            *ok = false;
            false
        }
    }
}

fn src_full_exists(ctx: &Ctx, src: &str) -> bool {
    resolve(ctx, src).exists()
}

/// [GNU] -r/--relative rewrites the symlink target into a path relative to
/// the destination's directory (`ln -rs a.txt b.txt` links b.txt -> a.txt;
/// cross-directory sources get the `../` walk). Lexical, no symlink
/// resolution; differing roots fall back to the absolute source.
fn relative_target(ctx: &Ctx, src: &str, dst: &str) -> String {
    fn components(p: &std::path::Path) -> Vec<String> {
        p.components()
            .map(|c| c.as_os_str().to_string_lossy().into_owned())
            .collect()
    }
    let src_abs = resolve(ctx, src);
    let dst_dir = resolve(ctx, dst)
        .parent()
        .map(|p| p.to_path_buf())
        .unwrap_or_else(|| resolve(ctx, dst));
    let mut sc = components(&src_abs);
    let mut dc = components(&dst_dir);
    if sc.first() != dc.first() {
        // different roots (drive letters): GNU cannot make it relative either
        return src_abs.to_string_lossy().into_owned();
    }
    let mut i = 0usize;
    while i < sc.len() && i < dc.len() && sc[i] == dc[i] {
        i += 1;
    }
    let mut out: Vec<String> = Vec::new();
    for _ in i..dc.len() {
        out.push("..".to_string());
    }
    out.extend(sc[i..].to_vec());
    if out.is_empty() {
        ".".to_string()
    } else {
        out.join("\\")
    }
}

fn same_file(ctx: &Ctx, a: &str, b: &str) -> bool {
    match (
        fs_win::file_meta(&ctx.cwd, a, false),
        fs_win::file_meta(&ctx.cwd, b, false),
    ) {
        (Ok(x), Ok(y)) => x.index == y.index && x.volume_serial == y.volume_serial,
        _ => false,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run_sc(sc: &ScratchCtx, args: &[&str], stdin: &str) -> (u32, String, String) {
        let mut io = MemIo::new(stdin);
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = ln_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    fn setup(tag: &str) -> ScratchCtx {
        let sc = ScratchCtx::new(tag);
        std::fs::write(sc.path("tgt"), "A1").unwrap();
        std::fs::write(sc.path("a"), "aaa").unwrap();
        std::fs::write(sc.path("b"), "bbb").unwrap();
        std::fs::create_dir(sc.path("dd")).unwrap();
        sc
    }

    fn run(args: &[&str]) -> (u32, String, String) {
        let sc = setup("ln");
        run_sc(&sc, args, "")
    }

    #[test]
    fn hard_link_basics() {
        let sc = setup("ln-basic");
        let (r, out, err) = run_sc(&sc, &["tgt", "lnk"], "");
        assert_eq!((r, out.as_str(), err.as_str()), (0, "", ""));
        let m1 = std::fs::metadata(sc.path("tgt")).unwrap();
        let m2 = std::fs::metadata(sc.path("lnk")).unwrap();
        assert_eq!(m1.len(), 2);
        assert_eq!(m2.len(), 2);
        // content matches
        assert_eq!(std::fs::read_to_string(sc.path("lnk")).unwrap(), "A1");
        // existing dst without -f
        let (r, _, e) = run_sc(&sc, &["a", "b"], "");
        assert_eq!(r, 1);
        assert!(e.contains("failed to create hard link 'b': File exists"));
        assert_eq!(std::fs::read_to_string(sc.path("b")).unwrap(), "bbb");
        // -f replaces
        let (r, _, _) = run_sc(&sc, &["-f", "a", "b"], "");
        assert_eq!(r, 0);
        assert_eq!(std::fs::read_to_string(sc.path("b")).unwrap(), "aaa");
        // directory hard link refused
        let (r, _, e) = run_sc(&sc, &["dd", "dl"], "");
        assert_eq!(r, 1);
        assert!(e.contains("hard link not allowed for directory"));
        // missing source
        let (r, _, e) = run_sc(&sc, &["nosuch", "lnk2"], "");
        assert_eq!(r, 1);
        assert!(e.contains("failed to access 'nosuch': No such file or directory"));
    }

    #[test]
    fn link_into_directory() {
        let sc = setup("ln-dir");
        let (r, _, _) = run_sc(&sc, &["tgt", "dd"], "");
        assert_eq!(r, 0);
        assert!(sc.path("dd/tgt").exists());
        // 3+ operands, last is a directory
        let (r, _, _) = run_sc(&sc, &["a", "b", "dd"], "");
        assert_eq!(r, 0);
        assert!(sc.path("dd/a").exists());
        assert!(sc.path("dd/b").exists());
        // 3 operands with non-directory target
        let (r, _, e) = run_sc(&sc, &["a", "b", "c"], "");
        assert_eq!(r, 1);
        assert!(e.contains("target 'c': No such file or directory"));
        // -T forbids directory interpretation (rc 1, GNU)
        let (r, _, e) = run_sc(&sc, &["-T", "a", "dd"], "");
        assert_eq!(r, 1);
        assert!(e.contains("cannot overwrite directory"));
        // 1 operand links into .: GNU attempts the link and reports the OS
        // failure with the joined path (probed: `ln solo` -> "failed to
        // create hard link '.\solo': File exists", rc 1); byte-parity keeps
        // the 1.x native-separator shape
        let sc2 = ScratchCtx::new("ln-one");
        std::fs::write(sc2.path("solo"), "x").unwrap();
        let (r, _, e) = run_sc(&sc2, &["solo"], "");
        assert_eq!(r, 1);
        assert!(e.contains(r"failed to create hard link '.\solo': File exists"));
    }

    #[test]
    fn symlink_mode() {
        // symlink creation may fail without Developer Mode; skip softly
        let sc = setup("ln-sym");
        let (r, _, _) = run_sc(&sc, &["-s", "tgt", "sl"], "");
        if sc.path("sl").symlink_metadata().is_err() {
            // privilege expected on non-developer-mode hosts
            return;
        }
        assert_eq!(r, 0);
        assert_eq!(
            std::fs::read_link(sc.path("sl")).unwrap().to_string_lossy(),
            "tgt"
        );
        // -sf replaces an existing symlink
        let (r, _, _) = run_sc(&sc, &["-sf", "a", "sl"], "");
        assert_eq!(r, 0);
        assert_eq!(
            std::fs::read_link(sc.path("sl")).unwrap().to_string_lossy(),
            "a"
        );
        // -s existing name without -f
        let (r, _, e) = run_sc(&sc, &["-s", "tgt", "sl"], "");
        assert_eq!(r, 1);
        assert!(e.contains("failed to create symbolic link 'sl': File exists"));
        // dangling symlinks are allowed
        let (r, _, _) = run_sc(&sc, &["-s", "nowhere", "dang"], "");
        assert_eq!(r, 0);
        // symlink into a directory operand
        let (r, _, _) = run_sc(&sc, &["-s", "tgt", "dd"], "");
        if r == 0 {
            assert!(sc.path("dd/tgt").symlink_metadata().is_ok());
        }
    }

    #[test]
    fn verbose_output() {
        let sc = setup("ln-v");
        let (r, out, _) = run_sc(&sc, &["-v", "tgt", "lv"], "");
        assert_eq!(r, 0);
        assert_eq!(out, "'lv' => 'tgt'\n");
        // symlink verbose (when privileged)
        let (r, out, _) = run_sc(&sc, &["-sv", "tgt", "slv"], "");
        if r == 0 {
            assert_eq!(out, "'slv' -> 'tgt'\n");
        }
    }

    #[test]
    fn interactive_prompt() {
        let sc = setup("ln-i");
        // GNU: 'n' declines on STDERR and fails rc 1
        let (r, out, e) = run_sc(&sc, &["-i", "a", "b"], "n");
        assert_eq!(r, 1);
        assert!(out.is_empty());
        assert!(e.contains("ln: replace 'b'? "));
        assert_eq!(std::fs::read_to_string(sc.path("b")).unwrap(), "bbb");
        // answer 'y': replaced
        let (r, _, e) = run_sc(&sc, &["-i", "a", "b"], "y\n");
        assert_eq!(r, 0);
        assert_eq!(std::fs::read_to_string(sc.path("b")).unwrap(), "aaa");
        // -i with no existing dst: no prompt needed
        let (r, out, _) = run_sc(&sc, &["-i", "a", "fresh"], "");
        assert_eq!(r, 0);
        assert!(out.is_empty());
        assert!(sc.path("fresh").exists());
    }

    #[test]
    fn backups() {
        let sc = setup("ln-b");
        // -b keeps the old dst as b~
        let (r, _, _) = run_sc(&sc, &["-b", "a", "b"], "");
        assert_eq!(r, 0);
        assert_eq!(std::fs::read_to_string(sc.path("b")).unwrap(), "aaa");
        assert_eq!(std::fs::read_to_string(sc.path("b~")).unwrap(), "bbb");
        // --backup=numbered makes .~1~
        let sc2 = setup("ln-b2");
        let (r, _, _) = run_sc(&sc2, &["--backup=numbered", "a", "b"], "");
        assert_eq!(r, 0);
        assert_eq!(std::fs::read_to_string(sc2.path("b")).unwrap(), "aaa");
        assert_eq!(std::fs::read_to_string(sc2.path("b.~1~")).unwrap(), "bbb");
        // -S suffix
        let sc3 = setup("ln-b3");
        let (r, _, _) = run_sc(&sc3, &["-b", "-S", ".bak", "a", "b"], "");
        assert_eq!(r, 0);
        assert_eq!(std::fs::read_to_string(sc3.path("b.bak")).unwrap(), "bbb");
        // no dst: no backup file created
        let sc4 = setup("ln-b4");
        let (r, _, _) = run_sc(&sc4, &["-b", "-s", "tgt", "bt"], "");
        if r == 0 {
            assert!(sc4.path("bt~").symlink_metadata().is_err());
        }
    }

    #[test]
    fn target_directory_option() {
        let sc = setup("ln-t");
        let (r, _, _) = run_sc(&sc, &["-t", "dd", "tgt"], "");
        assert_eq!(r, 0);
        assert!(sc.path("dd/tgt").exists());
        // -t with a non-directory
        let (r, _, e) = run_sc(&sc, &["-t", "tgt", "a"], "");
        assert_eq!(r, 1);
        assert!(e.contains("target 'tgt'"));
        // errors
        assert_eq!(run(&[]).0, 1);
        let (r, _, e) = run(&[]);
        assert!(e.contains("missing file operand"));
        assert_eq!(run(&["--bogus"]).0, 1);
        assert_eq!(run(&["--help"]).0, 0);
        assert_eq!(run(&["--version"]).0, 0);
        // -- ends options
        let sc2 = setup("ln-dd");
        let (r, _, _) = run_sc(&sc2, &["--", "tgt", "afterdash"], "");
        assert_eq!(r, 0);
        assert!(sc2.path("afterdash").exists());
    }

    #[test]
    fn relative_requires_symbolic() {
        let sc = setup("ln-rel");
        // -r without -s: bare GNU error line (no "Try" hint), rc 1, no link
        let (r, _, e) = run_sc(&sc, &["-r", "tgt", "nope"], "");
        assert_eq!(r, 1);
        assert!(e.contains("cannot do --relative without --symbolic"));
        assert!(!e.contains("Try"));
        assert!(!sc.path("nope").exists());
        // --relative long form behaves the same
        let (r, _, e) = run_sc(&sc, &["--relative", "tgt", "nope2"], "");
        assert_eq!(r, 1);
        assert!(e.contains("cannot do --relative without --symbolic"));
        // the check precedes the missing-operand validation (GNU order)
        let (r, _, e) = run_sc(&sc, &["-r"], "");
        assert_eq!(r, 1);
        assert!(e.contains("cannot do --relative without --symbolic"));
        // -rs creates a symlink whose target is relative to the dst dir.
        // Like symlink_mode: symlink creation needs Developer Mode; skip
        // softly where the privilege is absent (rc 1 + the OS reason).
        let (r, out, _) = run_sc(&sc, &["-rs", "-v", "tgt", "rel1"], "");
        if r == 0 {
            assert!(sc.path("rel1").exists());
            assert!(out.contains("-> 'tgt'"));
        } else {
            assert!(out.is_empty());
            assert!(
                !sc.path("rel1").exists(),
                "a failed -rs must not leave a link"
            );
        }
    }
}
