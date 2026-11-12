//! `pathchk` — GNU coreutils pathchk semantics on the Windows value model
//! (probe base: coreutils 9.4, target/probe/pk1.sh; C++ value model:
//! `src/commands/pathchk.cpp` on main).
//!
//! GNU contract (pathchk.c validate_check_name / validate_file_name):
//! * `-p` selects the POSIX-minimum checks (portable character set,
//!   _POSIX_PATH_MAX 256 / _POSIX_NAME_MAX 14 length limits); `-P` adds
//!   the empty-name and leading-dash checks; `--portability` is "equivalent
//!   to -p -P" (pathchk.c:93,120-133). Order of diagnostics per name:
//!   leading dash (-P) first, then empty name (-p/-P), then either the
//!   portable-character scan (-p) or the lstat probe (default mode), then
//!   total length, then per-component length.
//! * Default mode lstats the name: a missing name is fine; any other
//!   failure is reported with the errno text (probed: `regfile/file` →
//!   "Not a directory", `''` → "No such file or directory"). Length checks
//!   in default mode only apply to names that do not exist (256-component
//!   case: GNU's lstat answers ENAMETOOLONG with its own wording — same
//!   rc, different message text, `exit`-classified in the gate).
//! * rc 1 on the first offending operand is GNU behavior for the *run*:
//!   every operand is still checked (probed: two bad operands print two
//!   diagnostics) and the run exits 1 if any failed; stdout is always
//!   empty, so gate rows compare rc against GNU (`strict`).
//!
//! Windows value model (documented deviations):
//! * The default/"current filesystem" checks are the Windows filesystem's
//!   rules, not ext4's: reserved device names (CON, PRN, AUX, NUL,
//!   COM1-9, LPT1-9) and the invalid characters `< > : " | ? *` fail with
//!   "invalid Windows filename" where GNU-on-ext4 accepts them (probed:
//!   `pathchk ok.txt 'a*b'` → rc 0 on ext4). Those rows gate `win` against
//!   the C++ Debug exe, which carries the same rule.
//! * The lstat probe walks the component chain to pin the POSIX ENOTDIR
//!   verdict, because Win32 surfaces ERROR_DIRECTORY ("The directory name
//!   is invalid.") where POSIX lstat answers ENOTDIR (C++ differential
//!   pathchk/01 finding).

use std::ffi::OsString;
use std::path::{Path, PathBuf};

use crate::argopt::{self, OptSpec};
use crate::gnuhelp;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('p'),
        long: "posix-portable",
        takes_value: false,
    },
    OptSpec {
        short: Some('P'),
        long: "extra-portability",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "portability",
        takes_value: false,
    },
];

const PATHCHK_HELP_BODY: &str = r#"Usage: pathchk [OPTION]... NAME...
Diagnose invalid or non-portable file names.

  -p                  check for most POSIX systems
  -P                  check for empty names and leading "-"
      --portability   check for all POSIX systems (equivalent to -p -P)
      --help        display this help and exit
      --version     output version information and exit

"#;

const POSIX_PATH_MAX: usize = 256;
const POSIX_NAME_MAX: usize = 14;
const WINDOWS_COMPONENT_MAX: usize = 255;
// Non-portability length gate for the current filesystem: the
// PATH_MAX_MINIMUM analogue GNU asks pathconf(_PC_PATH_MAX) for.
const HOST_PATH_MAX: usize = 4096;

const WINDOWS_RESERVED: [&str; 22] = [
    "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8",
    "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
];

/// GNU pathchk.c no_leading_hyphen: a '-' at the start of the name or
/// immediately after a '/'.
fn has_leading_dash_component(path: &str) -> bool {
    let b = path.as_bytes();
    for (i, &c) in b.iter().enumerate() {
        if c == b'-' && (i == 0 || b[i - 1] == b'/') {
            return true;
        }
    }
    false
}

/// Split on '/' (POSIX mode) or on both '/' and '\\' (Windows mode).
fn split_components(path: &str, windows_separators: bool) -> Vec<&str> {
    let mut out = Vec::new();
    let mut start = 0usize;
    for (i, c) in path.char_indices() {
        let sep = c == '/' || (windows_separators && c == '\\');
        if sep {
            out.push(&path[start..i]);
            start = i + c.len_utf8();
        }
    }
    out.push(&path[start..]);
    out
}

/// Subject-side "current filesystem" rules: Windows reserved names and
/// invalid characters (pathchk.cpp windows_path_error).
fn windows_path_error(path: &str) -> Option<&'static str> {
    let has_drive_prefix =
        path.len() >= 2 && path.as_bytes()[0].is_ascii_alphabetic() && path.as_bytes()[1] == b':';
    for (i, c) in path.char_indices() {
        if has_drive_prefix && i == 1 {
            continue;
        }
        if matches!(c, '<' | '>' | ':' | '"' | '|' | '?' | '*') {
            return Some("invalid Windows filename");
        }
    }
    for component in split_components(path, true) {
        if component.is_empty() {
            continue;
        }
        if component.len() > WINDOWS_COMPONENT_MAX {
            return Some("file name too long");
        }
        let mut base = component;
        while base.ends_with('.') || base.ends_with(' ') {
            base = &base[..base.len() - 1];
        }
        if let Some(dot) = base.find('.') {
            base = &base[..dot];
        }
        if base.len() <= 9 {
            let upper = base.to_ascii_uppercase();
            if WINDOWS_RESERVED.contains(&upper.as_str()) {
                return Some("invalid Windows filename");
            }
        }
    }
    None
}

enum ProbeOutcome {
    Exists,
    NotThere,
    Error(String),
}

/// POSIX lstat answers ENOTDIR, not ENOENT, when a non-final component
/// exists as a file; Win32 disagrees about which error crosses that shape,
/// so the not-there verdict is confirmed by walking the component chain.
/// The probe goes through the `\\?\` API path (fs_win::api_path) so DOS
/// device names stay literal, exactly like the C++ make_api_path_operand
/// probe — `CON` under the scratch dir must reach the subject-side
/// reserved-name check, not the console device.
fn probe_existence(ctx: &Ctx, name: &str) -> ProbeOutcome {
    // Whole-operand DOS device (NUL, CON, ...): attribute APIs reject
    // device names outright (fs_win note), so the device-open probe
    // answers existence directly. The subject-side reserved-name check
    // still fires on the same name afterwards, exactly like the C++
    // probe whose \\?\ path makes the name merely literal-not-found.
    let api = crate::fs_win::api_path(&ctx.cwd, name);
    if crate::fs_win::probe_dos_device(&api) {
        return ProbeOutcome::Exists;
    }
    // \\?\-prefixed paths get no loader normalization: trailing separators
    // and "." / ".." components must be collapsed lexically before the
    // probe (normalize_lex absolutizes against ctx.cwd and drops them).
    let lex = if Path::new(name).is_absolute() {
        api
    } else {
        format!(
            r"\\?\{}",
            crate::fsutil::normalize_lex(ctx, name).replace('/', "\\")
        )
    };
    let api_path = PathBuf::from(lex);
    match std::fs::symlink_metadata(&api_path) {
        Ok(_) => return ProbeOutcome::Exists,
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => {
            if file_component_in_chain(&api_path) {
                return ProbeOutcome::Error("Not a directory".to_string());
            }
            return ProbeOutcome::NotThere;
        }
        Err(e) => {
            let text = match e.kind() {
                std::io::ErrorKind::NotADirectory => "Not a directory".to_string(),
                _ => e.to_string(),
            };
            return ProbeOutcome::Error(text);
        }
    }
}

/// True when some existing prefix of `path` is a regular file (the ENOTDIR
/// pin for a not-found final component).
fn file_component_in_chain(path: &Path) -> bool {
    let mut prefixes: Vec<PathBuf> = Vec::new();
    let mut cur = path.to_path_buf();
    loop {
        match cur.parent() {
            Some(p) if p != cur => {
                prefixes.push(p.to_path_buf());
                cur = p.to_path_buf();
            }
            _ => break,
        }
    }
    for prefix in prefixes {
        if prefix.as_os_str().is_empty() {
            continue;
        }
        if let Ok(meta) = std::fs::symlink_metadata(&prefix) {
            if !meta.is_dir() {
                return true;
            }
        }
    }
    false
}

pub fn pathchk_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("pathchk", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if let Some(which) = p
        .flags
        .iter()
        .find(|f| f.as_str() == argopt::HELP || f.as_str() == argopt::VERSION)
    {
        return Ok(match which.as_str() {
            "help" => {
                write!(
                    io.stdout,
                    "{}{}",
                    PATHCHK_HELP_BODY,
                    gnuhelp::footer("pathchk", "pathchk invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version(
                        "pathchk",
                        "Paul Eggert, David MacKenzie, and Jim Meyering"
                    )
                )?;
                0
            }
        });
    }

    let check_basic = p.has("posix-portable") || p.has("portability");
    let check_extra = p.has("extra-portability") || p.has("portability");

    if p.positionals.is_empty() {
        let _ = writeln!(io.stderr, "pathchk: missing operand");
        let _ = writeln!(io.stderr, "Try 'pathchk --help' for more information.");
        return Ok(1);
    }

    let mut exit_code = 0;
    for operand in &p.positionals {
        let name = operand.to_string_lossy().into_owned();
        if let Some(message) = check_name(&name, check_basic, check_extra, ctx) {
            let _ = writeln!(io.stderr, "{message}");
            exit_code = 1;
        }
    }
    Ok(exit_code)
}

/// All checks for one NAME; returns the diagnostic line when it fails.
fn check_name(name: &str, check_basic: bool, check_extra: bool, ctx: &Ctx) -> Option<String> {
    // GNU pathchk.c:270-272: the leading-dash check runs first.
    if check_extra && has_leading_dash_component(name) {
        return Some(format!(
            "pathchk: leading '-' in a component of file name '{name}'"
        ));
    }
    // pathchk.c:274-285: portable modes reject the empty name.
    if (check_basic || check_extra) && name.is_empty() {
        return Some("pathchk: empty file name".to_string());
    }

    let mut file_exists = false;
    if check_basic {
        // pathchk.c portable_chars_only: the POSIX portable character set,
        // first offender named.
        for c in name.chars() {
            let portable =
                c.is_ascii_alphanumeric() || c == '.' || c == '_' || c == '-' || c == '/';
            if !portable {
                return Some(format!(
                    "pathchk: non-portable character '{}' in file name '{name}'",
                    c
                ));
            }
        }
    } else {
        // pathchk.c:292-306: lstat the full name. A missing name is fine;
        // any other failure is reported with the errno text.
        let probe = if name.is_empty() {
            // Windows metadata calls reject the empty path outright; POSIX
            // lstat("") answers ENOENT and GNU reports it (probed).
            ProbeOutcome::NotThere
        } else {
            probe_existence(ctx, name)
        };
        match probe {
            ProbeOutcome::Exists => file_exists = true,
            ProbeOutcome::Error(text) => {
                return Some(format!("pathchk: '{name}': {text}"));
            }
            ProbeOutcome::NotThere if name.is_empty() => {
                return Some(format!("pathchk: '{name}': No such file or directory"));
            }
            ProbeOutcome::NotThere => {}
        }
    }

    // pathchk.c:308-341: total length.
    if check_basic {
        let limit = POSIX_PATH_MAX - 1;
        if name.len() > limit {
            return Some(format!(
                "pathchk: limit {limit} exceeded by length {} of file name '{name}'",
                name.len()
            ));
        }
    } else if !file_exists && name.len() >= HOST_PATH_MAX {
        let limit = HOST_PATH_MAX - 1;
        return Some(format!(
            "pathchk: limit {limit} exceeded by length {} of file name '{name}'",
            name.len()
        ));
    }

    // pathchk.c:343-415: component lengths.
    let component_limit = if check_basic {
        Some(POSIX_NAME_MAX)
    } else if !file_exists {
        let over = split_components(name, false)
            .iter()
            .any(|comp| comp.len() > WINDOWS_COMPONENT_MAX);
        over.then_some(WINDOWS_COMPONENT_MAX)
    } else {
        None
    };
    if let Some(limit) = component_limit {
        for comp in split_components(name, false) {
            if comp.len() > limit {
                return Some(format!(
                    "pathchk: limit {limit} exceeded by length {} of file name component '{comp}'",
                    comp.len()
                ));
            }
        }
    }

    // Subject-side "current filesystem" rules (Windows reserved names and
    // characters). Like GNU's filesystem checks these belong to the default
    // and -P modes; -p / --portability is pure POSIX and must not see them
    // (probed: `pathchk -p CON` passes).
    if !check_basic {
        if let Some(reason) = windows_path_error(name) {
            return Some(format!("pathchk: '{name}': {reason}"));
        }
    }
    None
}

#[cfg(test)]
mod tests {
    use std::ffi::OsString;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run_in(sc: &ScratchCtx, args: &[&str]) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::pathchk_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    fn run(args: &[&str]) -> (u32, String, String) {
        let sc = ScratchCtx::new("pathchk");
        run_in(&sc, args)
    }

    #[test]
    fn good_names_pass() {
        let (r, _, err) = run(&["ok.txt"]);
        assert_eq!(r, 0, "err={err}");
        assert_eq!(run(&["nosuch/name.txt"]).0, 0);
        assert_eq!(run(&["a", "b", "c"]).0, 0);
        assert_eq!(run(&["a/b/"]).0, 0);
        assert_eq!(run(&["."]).0, 0);
        assert_eq!(run(&[".."]).0, 0);
    }

    #[test]
    fn enotdir_vs_missing_dir() {
        let sc = ScratchCtx::new("pathchk-enotdir");
        std::fs::write(sc.path("regfile"), "x").unwrap();
        std::fs::create_dir_all(sc.path("missingdir/inner")).unwrap();
        // existing file as a non-final component: ENOTDIR like GNU lstat
        let (r, _, err) = run_in(&sc, &["regfile/file"]);
        assert_eq!(r, 1);
        assert!(err.contains("Not a directory"));
        // missing directory chain: fine (GNU probed rc 0)
        assert_eq!(run_in(&sc, &["missingdir/inner/x"]).0, 0);
    }

    #[test]
    fn empty_name_checks() {
        // default mode: lstat("") -> ENOENT wording (GNU probed rc 1)
        let (r, _, err) = run(&[""]);
        assert_eq!(r, 1);
        assert!(err.contains("'': No such file or directory"));
        let (r, _, err) = run(&["-p", ""]);
        assert_eq!(r, 1);
        assert!(err.contains("empty file name"));
        let (r, _, err) = run(&["-P", ""]);
        assert_eq!(r, 1);
        assert!(err.contains("empty file name"));
    }

    #[test]
    fn leading_dash_checks() {
        let (r, _, err) = run(&["-P", "--", "-x"]);
        assert_eq!(r, 1);
        assert!(err.contains("leading '-' in a component of file name '-x'"));
        let (r, _, err) = run(&["-P", "a/-b"]);
        assert_eq!(r, 1);
        assert!(err.contains("leading '-'"));
        // -P alone: the dash in the middle of a component (not after /)
        assert_eq!(run(&["-P", "a-b"]).0, 0);
        // without -P a leading dash is not checked
        assert_eq!(run(&["-x"]).0, 1); // it is an option there; use --
        assert_eq!(run(&["--", "-x"]).0, 0);
    }

    #[test]
    fn portable_length_checks() {
        let long255 = "a".repeat(255);
        let long256 = "a".repeat(256);
        // A single long component already fails -p's NAME_MAX check first
        // (GNU probed: "-p long 255" -> limit 14 wording).
        let (r, _, err) = run(&["-p", &long255]);
        assert_eq!(r, 1);
        assert!(err.contains("limit 14 exceeded by length 255"));
        let (r, _, err) = run(&["-p", &long256]);
        assert_eq!(r, 1);
        // GNU default-mode lstat wording differs (File name too long);
        // -p mode gives the limit wording (probed).
        assert!(err.contains("limit 255 exceeded by length 256"));

        let comp15 = "a".repeat(15);
        let comp14 = "a".repeat(14);
        let (r, _, err) = run(&["-p", &comp15]);
        assert_eq!(r, 1);
        assert!(err.contains("limit 14 exceeded by length 15"));
        assert_eq!(run(&["-p", &comp14]).0, 0);

        let total256 = format!("d/{long254}", long254 = "a".repeat(254));
        let (r, _, err) = run(&["-p", &total256]);
        assert_eq!(r, 1);
        assert!(err.contains("limit 255 exceeded by length 256 of file name 'd/"));
    }

    #[test]
    fn portable_character_checks() {
        let (r, _, err) = run(&["-p", "a b"]);
        assert_eq!(r, 1);
        assert!(err.contains("non-portable character ' ' in file name 'a b'"));
        let (r, _, err) = run(&["-p", "a:b"]);
        assert_eq!(r, 1);
        assert!(err.contains("non-portable character ':'"));
        assert_eq!(run(&["-p", "a9._-b/c"]).0, 0);
        // --portability is -p -P combined
        let (r, _, err) = run(&["--portability", "x?", ""]);
        assert_eq!(r, 1);
        assert!(err.contains("non-portable character '?'"));
        assert!(err.contains("empty file name"));
    }

    #[test]
    fn windows_subject_rules() {
        // Reserved device names fail in default mode (C++ value model),
        // while -p mode is pure POSIX and accepts them (GNU probed rc 0).
        let (r, _, err) = run(&["CON"]);
        assert_eq!(r, 1, "err={err}");
        assert!(err.contains("invalid Windows filename"), "err={err}");
        assert_eq!(run(&["-p", "CON"]).0, 0);
        let (r, _, _) = run(&["a*b"]);
        assert_eq!(r, 1); // '*' invalid on Windows (GNU ext4 accepts: win row)
                          // stem-before-dot rule, like the C++ windows_path_error: con.txt is
                          // reserved too (GNU ext4 accepts it: win-classified gate row)
        assert_eq!(run(&["con.txt"]).0, 1);
        assert_eq!(run(&["Con"]).0, 1); // case-insensitive
        assert_eq!(run(&["NUL "]).0, 1); // trailing-space strip -> reserved
    }

    #[test]
    fn missing_operand_and_help() {
        let (r, _, err) = run(&[]);
        assert_eq!(r, 1);
        assert!(err.contains("pathchk: missing operand"));
        let (r, out, _) = run(&["--help"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("Usage: pathchk [OPTION]... NAME...\n"));
        assert!(out.ends_with("info '(coreutils) pathchk invocation'\n"));
        let (r, out, _) = run(&["--version"]);
        assert_eq!(r, 0);
        assert!(out.ends_with("Written by Paul Eggert, David MacKenzie, and Jim Meyering.\n"));
    }

    #[test]
    fn two_operands_one_bad() {
        // every operand is checked; rc 1 if any failed (GNU probed)
        let sc = ScratchCtx::new("pathchk-two");
        std::fs::write(sc.path("regfile"), "x").unwrap();
        let (r, _, err) = run_in(&sc, &["okname", "regfile/file"]);
        assert_eq!(r, 1);
        assert!(err.contains("Not a directory"));
    }
}
