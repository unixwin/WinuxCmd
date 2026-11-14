//! `mkfifo` — GNU coreutils mkfifo.c (9.4) semantics, probed byte-for-byte
//! against WSL coreutils 9.4 (rust2/target/probe/w5-mkfifo-mknod.sh).
//!
//! Windows has no filesystem FIFO node type, so the fifo is the 1.x C++
//! on-disk marker (#1038, `src/utils/native_path.cppm:1084`): a regular
//! file whose first bytes are `!<fifo>` (kFifoMarker) carrying
//! FILE_ATTRIBUTE_SYSTEM|ARCHIVE (+READONLY for a mode with no write
//! bits). Reader/writer opens in the 1.x file_io layer bridge that marker
//! to a deterministic `\\.\pipe\winuxcmd-fifo-<fnv1a>` endpoint, so
//! WinuxCmd processes get blocking POSIX open() semantics between
//! themselves; the marker is byte/attr-compatible with that bridge.
//!
//! Honest boundary (documented, not hidden): this wave5 module only
//! CREATES markers. The Rust head/cat do not bridge markers to named
//! pipes yet (#1442 covers the C++ side), so a marker read by the Rust
//! side sees the 7-byte marker text where C++ sees a pipe rendezvous.
//! Gate rows therefore never read a fifo operand; GNU's blocking-open
//! behavior is the oracle's, not ours.
//!
//! `-m MODE` is mkfifo.c:151-165's mode_compile on a 0666 base: octal
//! (values above 0777 fatal) or symbolic clauses; any resulting bit
//! outside S_IRWXUGO is fatal ("mode must specify only file permission
//! bits", probed with 1777/4755). `-Z` bare is silently ignored,
//! `-Z CTX` warns to stderr and still creates (mkfifo.c:118-122,
//! non-SELinux kernel; probed rc 0).
//!
//! Errors, probed: existing name -> `mkfifo: cannot create fifo 'X':
//! File exists` rc 1 (also for a regular file); missing parent ->
//! `...: No such file or directory` rc 1; no operand -> `mkfifo: missing
//! operand` + try-help rc 1.

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use crate::fsutil::resolve;
use crate::gnuhelp;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('m'),
        long: "mode",
        takes_value: true,
    },
    // mkfifo.c:344-345: -Z pairs with --context[=CTX] (optstring "m:Z" —
    // the short form never takes an argument, so `-Z ctx` makes ctx a
    // fifo NAME; probed: both created, silent, rc 0). --context is GNU
    // getopt optional_argument: only `--context=X` carries a value, bare
    // `--context` does not consume the next token. The shared argopt
    // OptSpec cannot express optional values without breaking every
    // command's literal, so mkfifo pre-scans those tokens itself (see
    // split_context_tokens) and argopt never sees a value-carrying form.
    OptSpec {
        short: Some('Z'),
        long: "context",
        takes_value: false,
    },
];

// `--help` body verbatim from `env mkfifo --help` under WSL coreutils 9.4.
const MKFIFO_HELP_BODY: &str = r#"Usage: mkfifo [OPTION]... NAME...
Create named pipes (FIFOs) with the given NAMEs.

Mandatory arguments to long options are mandatory for short options too.
  -m, --mode=MODE    set file permission bits to MODE, not a=rw - umask
  -Z                   set the SELinux security context to default type
      --context[=CTX]  like -Z, or if CTX is specified then set the SELinux
                         or SMACK security context to CTX
      --help        display this help and exit
      --version     output version information and exit

"#;

/// The on-disk marker text (native_path.cppm kFifoMarker).
pub const FIFO_MARKER: &[u8] = b"!<fifo>";

/// FILE_ATTRIBUTE_SYSTEM (the marker's identifying attribute).
const FILE_ATTRIBUTE_SYSTEM: u32 = 0x4;
/// FILE_ATTRIBUTE_ARCHIVE
const FILE_ATTRIBUTE_ARCHIVE: u32 = 0x20;
/// FILE_ATTRIBUTE_READONLY
const FILE_ATTRIBUTE_READONLY: u32 = 0x1;
/// CREATE_NEW disposition
const CREATE_NEW: u32 = 1;
/// GENERIC_WRITE
const GENERIC_WRITE: u32 = 0x4000_0000;
/// INVALID_FILE_ATTRIBUTES
const INVALID_FILE_ATTRIBUTES: u32 = 0xFFFF_FFFF;

/// mkfifo.c:151-165 mode_compile: apply the mode to a 0666 base; reject
/// any resulting bit outside S_IRWXUGO. Octal and symbolic forms.
/// Probed: `-m ug=r` on the 0666 base -> 446; `-m 1777`/`-m 4755` fatal.
pub fn evaluate_mode(mode: &str) -> Result<u32, &'static str> {
    if mode.is_empty() {
        return Err("invalid mode");
    }
    if mode.bytes().all(|c| (b'0'..=b'7').contains(&c)) {
        let mut value: u64 = 0;
        for c in mode.bytes() {
            value = value * 8 + u64::from(c - b'0');
        }
        if value > 0o777 {
            return Err("mode must specify only file permission bits");
        }
        return Ok(value as u32);
    }
    let mut cur: u32 = 0o666;
    for clause in mode.split(',') {
        if clause.is_empty() {
            return Err("invalid mode");
        }
        let b = clause.as_bytes();
        let mut j = 0usize;
        let mut who = [false; 3]; // u, g, o
        while j < b.len() && matches!(b[j], b'u' | b'g' | b'o' | b'a') {
            if b[j] == b'u' || b[j] == b'a' {
                who[0] = true;
            }
            if b[j] == b'g' || b[j] == b'a' {
                who[1] = true;
            }
            if b[j] == b'o' || b[j] == b'a' {
                who[2] = true;
            }
            j += 1;
        }
        if j == 0 {
            who = [true, true, true];
        }
        let mut any_op = false;
        while j < b.len() {
            let op = b[j];
            if op != b'+' && op != b'-' && op != b'=' {
                return Err("invalid mode");
            }
            j += 1;
            let perm_begin = j;
            while j < b.len() && b"rwxXstugo".contains(&b[j]) {
                j += 1;
            }
            let perms = &clause[perm_begin..j];
            any_op = true;
            let class_bits = |cur: u32| -> u32 {
                let mut bits = 0u32;
                for c in perms.bytes() {
                    match c {
                        b'r' => bits |= 4,
                        b'w' => bits |= 2,
                        b'x' => bits |= 1,
                        b'X' => bits |= u32::from(cur & 0o111 != 0),
                        b'u' => bits |= (cur >> 6) & 7,
                        b'g' => bits |= (cur >> 3) & 7,
                        b'o' => bits |= cur & 7,
                        _ => {} // s/t handled below
                    }
                }
                bits
            };
            for (cls, &is) in who.iter().enumerate() {
                if !is {
                    continue;
                }
                let shift = 6 - 3 * cls;
                let bits = class_bits(cur);
                cur = match op {
                    b'+' => cur | (bits << shift),
                    b'-' => cur & !(bits << shift),
                    _ => (cur & !(7 << shift)) | (bits << shift),
                };
            }
            let mut special = 0u32;
            for c in perms.bytes() {
                match c {
                    b's' => {
                        if who[0] {
                            special |= 0o4000;
                        }
                        if who[1] {
                            special |= 0o2000;
                        }
                    }
                    b't' => special |= 0o1000,
                    _ => {}
                }
            }
            cur = if op == b'-' {
                cur & !special
            } else {
                cur | special
            };
        }
        if !any_op {
            return Err("invalid mode");
        }
    }
    if cur & !0o777 != 0 {
        return Err("mode must specify only file permission bits");
    }
    Ok(cur)
}

#[cfg(windows)]
pub(crate) mod imp {
    use super::*;

    fn wide(s: &str) -> Vec<u16> {
        use std::os::windows::ffi::OsStrExt;
        std::ffi::OsStr::new(s)
            .encode_wide()
            .chain(Some(0))
            .collect()
    }

    #[link(name = "kernel32")]
    extern "system" {
        fn CreateFileW(
            name: *const u16,
            access: u32,
            share: u32,
            security: *mut core::ffi::c_void,
            disposition: u32,
            flags: u32,
            template: *mut core::ffi::c_void,
        ) -> *mut core::ffi::c_void;
        fn WriteFile(
            handle: *mut core::ffi::c_void,
            buffer: *const u8,
            to_write: u32,
            written: *mut u32,
            overlapped: *mut core::ffi::c_void,
        ) -> i32;
        fn CloseHandle(handle: *mut core::ffi::c_void) -> i32;
        fn GetFileAttributesW(name: *const u16) -> u32;
        fn SetFileAttributesW(name: *const u16, attrs: u32) -> i32;
        fn DeleteFileW(name: *const u16) -> i32;
    }

    fn to_extended(path: &str) -> String {
        if path.starts_with(r"\\?\") {
            return path.to_string();
        }
        if let Some(rest) = path.strip_prefix(r"\\") {
            return format!(r"\\?\UNC\{rest}");
        }
        format!(r"\\?\{path}")
    }

    /// Attributes of the native path (native_path.cppm attributes_w);
    /// verbatim spelling is enough below MAX_PATH, matching the C++
    /// operand_target_attributes_w probe shape.
    pub fn attributes(path: &str) -> Option<u32> {
        let attrs = unsafe { GetFileAttributesW(wide(path).as_ptr()) };
        if attrs == INVALID_FILE_ATTRIBUTES {
            None
        } else {
            Some(attrs)
        }
    }

    /// native_path.cppm:1141 create_winux_fifo_w: CREATE_NEW + write the
    /// marker, then SYSTEM|ARCHIVE (|READONLY when the mode leaves no
    /// write bit). Returns ERROR_SUCCESS (0) or the Win32 error code.
    pub fn create_fifo(native: &str, readonly: bool) -> u32 {
        let extended = to_extended(native);
        let w = wide(&extended);
        let handle = unsafe {
            CreateFileW(
                w.as_ptr(),
                GENERIC_WRITE,
                0,
                std::ptr::null_mut(),
                CREATE_NEW,
                0,
                std::ptr::null_mut(),
            )
        };
        if handle as isize == -1 || handle.is_null() {
            #[link(name = "kernel32")]
            extern "system" {
                fn GetLastError() -> u32;
            }
            return unsafe { GetLastError() };
        }
        let mut written: u32 = 0;
        let ok = unsafe {
            WriteFile(
                handle,
                FIFO_MARKER.as_ptr(),
                FIFO_MARKER.len() as u32,
                &mut written,
                std::ptr::null_mut(),
            ) != 0
        } && written == FIFO_MARKER.len() as u32;
        unsafe { CloseHandle(handle) };
        if !ok {
            #[link(name = "kernel32")]
            extern "system" {
                fn GetLastError() -> u32;
            }
            let err = unsafe { GetLastError() };
            unsafe { DeleteFileW(w.as_ptr()) };
            return err;
        }
        let mut attrs = FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE;
        if readonly {
            attrs |= FILE_ATTRIBUTE_READONLY;
        }
        unsafe { SetFileAttributesW(w.as_ptr(), attrs) };
        0
    }
}

#[cfg(not(windows))]
pub(crate) mod imp {
    pub fn attributes(_path: &str) -> Option<u32> {
        None
    }
    pub fn create_fifo(_native: &str, _readonly: bool) -> u32 {
        1 // generic failure; non-Windows builds have no fifo markers
    }
}

/// The win32_posix_error_text mapping the 1.x applet applies to a failed
/// marker create (mkfifo.cpp run): ERROR_FILE_EXISTS reads "File exists",
/// invalid names read as missing (win32_posix_error_text's
/// invalid_name_as_missing), everything else its generic text.
pub(crate) fn create_error_text(err: u32) -> &'static str {
    match err {
        80 => "File exists",                // ERROR_FILE_EXISTS
        183 => "File exists",               // ERROR_ALREADY_EXISTS
        2 => "No such file or directory",   // ERROR_FILE_NOT_FOUND (invalid_name_as_missing)
        3 => "No such file or directory",   // ERROR_PATH_NOT_FOUND
        123 => "No such file or directory", // ERROR_INVALID_NAME
        206 => "File name too long",        // ERROR_FILENAME_EXCED_RANGE
        5 => "Permission denied",           // ERROR_ACCESS_DENIED
        _ => "Invalid argument",
    }
}

/// GNU getopt optional_argument pre-scan: pull `--context[=X]` tokens out
/// of argv before the shared parse, returning (context value, remainder).
/// The scan stops at `--` (a post-`--` `--context=x` is a fifo NAME, not
/// the option — probed GNU getopt shape). `-Z` stays in the token stream
/// for argopt so the bundled-invalid form `-Zctx2` keeps its GNU
/// diagnostic ("invalid option -- 'c'", rc 1, nothing created). Shared
/// with mknod, whose --context[=CTX] option has the same getopt shape.
pub fn split_context_tokens(args: &[OsString]) -> (Option<String>, Vec<OsString>) {
    let mut value = None;
    let mut rest = Vec::new();
    let mut no_more_options = false;
    for a in args {
        let text = a.to_string_lossy();
        if !no_more_options {
            if text == "--" {
                no_more_options = true;
                rest.push(a.clone());
                continue;
            }
            if let Some(v) = text.strip_prefix("--context=") {
                value = Some(v.to_string());
                continue;
            }
            if text == "--context" {
                value = Some(String::new());
                continue;
            }
        }
        rest.push(a.clone());
    }
    (value, rest)
}

pub fn mkfifo_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let (context_value, args) = split_context_tokens(args);
    let p = match argopt::parse("mkfifo", &args, OPTS, io, ctx, 1) {
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
                    MKFIFO_HELP_BODY,
                    gnuhelp::footer("mkfifo", "mkfifo invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("mkfifo", "David MacKenzie")
                )?;
                0
            }
        });
    }

    // mkfifo.c:133: the missing-operand check precedes mode compilation.
    if p.positionals.is_empty() {
        let _ = winux_core::usage_error(ctx, io, "mkfifo", &crate::messages::missing_operand(ctx));
        return Ok(1);
    }

    let mut mode_value = 0o666u32;
    let has_mode = p.has("mode");
    if has_mode {
        match evaluate_mode(p.value_of("mode").unwrap_or("")) {
            Ok(v) => mode_value = v,
            Err(msg) => {
                let _ = writeln!(io.stderr, "mkfifo: {msg}");
                return Ok(1);
            }
        }
    }

    // mkfifo.c:118-122: --context=CTX warns on a non-SELinux kernel and
    // the fifo is still created (probed rc 0); a bare -Z / bare --context
    // is silent (probed rc 0).
    if let Some(v) = &context_value {
        if !v.is_empty() {
            let _ = writeln!(
                io.stderr,
                "mkfifo: warning: ignoring --context; it requires an SELinux/SMACK-enabled kernel"
            );
        }
    }

    let readonly = has_mode && (mode_value & 0o222) == 0;
    let mut failed = false;
    for arg in &p.positionals {
        let fifo = arg.to_string_lossy().into_owned();
        let native = resolve(ctx, &fifo).to_string_lossy().into_owned();
        if imp::attributes(&native).is_some() {
            // probed GNU wording: the name is ALWAYS quoted here, even a
            // plain "p" ("mkfifo: cannot create fifo 'p': File exists")
            let _ = writeln!(
                io.stderr,
                "mkfifo: cannot create fifo '{fifo}': File exists"
            );
            failed = true;
            continue;
        }
        let created = imp::create_fifo(&native, readonly);
        if created != 0 {
            let _ = writeln!(
                io.stderr,
                "mkfifo: cannot create fifo '{fifo}': {}",
                create_error_text(created)
            );
            failed = true;
        }
    }
    Ok(if failed { 1 } else { 0 })
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str]) -> (u32, String, String, ScratchCtx) {
        let sc = ScratchCtx::new("mkfifo");
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = mkfifo_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err(), sc)
    }

    #[test]
    fn mode_symbolic_on_0666_base() {
        // probed: -m ug=r -> 446 (o keeps the 0666 base's 6)
        assert_eq!(evaluate_mode("ug=r"), Ok(0o446));
        assert_eq!(evaluate_mode("600"), Ok(0o600));
        assert_eq!(evaluate_mode("777"), Ok(0o777));
        assert_eq!(evaluate_mode("0"), Ok(0));
        // special bits are fatal for mkfifo (probed 1777 / 4755)
        assert_eq!(
            evaluate_mode("1777"),
            Err("mode must specify only file permission bits")
        );
        assert_eq!(
            evaluate_mode("4755"),
            Err("mode must specify only file permission bits")
        );
        assert_eq!(evaluate_mode("a=rwx"), Ok(0o777));
        assert_eq!(evaluate_mode("+x"), Ok(0o777)); // 0666 | x on all classes
        assert_eq!(evaluate_mode("u+x"), Ok(0o766));
        assert_eq!(evaluate_mode("bogus"), Err("invalid mode"));
        assert_eq!(evaluate_mode(""), Err("invalid mode"));
        assert_eq!(evaluate_mode("8"), Err("invalid mode")); // not octal, not symbolic
        assert_eq!(evaluate_mode("u"), Err("invalid mode")); // who without op
        assert_eq!(evaluate_mode("u+"), Ok(0o666)); // empty perms, op present
                                                    // g=u copy
        assert_eq!(evaluate_mode("g=u"), Ok(0o666));
        // u-rw strips user bits
        assert_eq!(evaluate_mode("u-rw"), Ok(0o066));
    }

    #[test]
    fn create_and_marker_shape() {
        let (r, out, err, sc) = run(&["p"]);
        assert_eq!(r, 0);
        assert!(out.is_empty());
        assert!(err.is_empty());
        let bytes = std::fs::read(sc.path("p")).unwrap();
        assert_eq!(&bytes, FIFO_MARKER);
        // SYSTEM attribute set (the 1.x bridge's identifying bit)
        let meta = std::fs::metadata(sc.path("p")).unwrap();
        use std::os::windows::fs::MetadataExt;
        assert_ne!(meta.file_attributes() & FILE_ATTRIBUTE_SYSTEM, 0);
    }

    #[test]
    fn readonly_mode_sets_readonly_attr() {
        let (r, _, _, sc) = run(&["-m", "000", "p"]);
        assert_eq!(r, 0);
        use std::os::windows::fs::MetadataExt;
        let meta = std::fs::metadata(sc.path("p")).unwrap();
        assert_ne!(meta.file_attributes() & FILE_ATTRIBUTE_READONLY, 0);
        let (_, _, _, sc) = run(&["-m", "600", "p6"]);
        let meta = std::fs::metadata(sc.path("p6")).unwrap();
        assert_eq!(meta.file_attributes() & FILE_ATTRIBUTE_READONLY, 0);
    }

    #[test]
    fn existing_names_fail_with_rc1() {
        // same name twice in one invocation: first creates, second fails
        let (r, _, err, _) = run(&["p", "p"]);
        assert_eq!(r, 1);
        assert!(err.contains("cannot create fifo 'p': File exists"));
        // a regular file also collides (probed)
        let sc = ScratchCtx::new("mkfifo-reg");
        std::fs::write(sc.path("reg"), "x").unwrap();
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = ["reg"].iter().map(Into::into).collect();
        let r = mkfifo_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("cannot create fifo 'reg': File exists"));
    }

    #[test]
    fn missing_parent_is_enoent() {
        let (r, _, err, _) = run(&["nodir/x"]);
        assert_eq!(r, 1);
        assert!(err.contains("cannot create fifo 'nodir/x': No such file or directory"));
    }

    #[test]
    fn no_operand_is_usage_rc1() {
        let (r, out, err, _) = run(&[]);
        assert_eq!(r, 1);
        assert!(out.is_empty());
        assert!(err.contains("mkfifo: missing operand"));
        assert!(err.contains("Try 'mkfifo --help' for more information."));
    }

    #[test]
    fn context_warns_but_creates() {
        // probed: --context=CTX -> warn to stderr, fifo still created rc 0;
        // bare -Z, bare --context, and `-Z ctx` (ctx becomes a fifo NAME,
        // optstring "m:Z" never gives the short form a value) are silent.
        let (r, out, err, sc) = run(&["--context=cval", "pz3"]);
        assert_eq!(r, 0);
        assert!(out.is_empty());
        assert!(err.contains("mkfifo: warning: ignoring --context"));
        assert!(sc.path("pz3").exists());
        let (r, _, _, sc) = run(&["-Z", "pz5"]);
        assert_eq!(r, 0);
        assert!(sc.path("pz5").exists());
        let (r, _, _, sc) = run(&["--context", "pz4"]);
        assert_eq!(r, 0);
        assert!(sc.path("pz4").exists());
        // probed `-Z ctx pz`: TWO fifos, no warning
        let (r, _, err, sc) = run(&["-Z", "ctx", "pz"]);
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(sc.path("ctx").exists());
        assert!(sc.path("pz").exists());
    }

    #[test]
    fn bundled_z_rejects_next_char() {
        // probed: `-Zctx2 pz2` -> invalid option -- 'c', rc 1, nothing made
        let (r, out, err, sc) = run(&["-Zctx2", "pz2"]);
        assert_eq!(r, 1);
        assert!(out.is_empty());
        assert!(err.contains("invalid option -- 'c'"));
        assert!(!sc.path("pz2").exists());
    }

    #[test]
    fn context_after_dashdash_is_a_name() {
        // getopt optional_argument pre-scan stops at --: the token is an
        // operand naming the fifo.
        let (r, _, _, sc) = run(&["--", "--context=x"]);
        assert_eq!(r, 0);
        assert!(sc.path("--context=x").exists());
    }

    #[test]
    fn help_version_surface() {
        let (r, out, err, _) = run(&["--help"]);
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.starts_with("Usage: mkfifo [OPTION]... NAME...\n"));
        assert!(out.ends_with("info '(coreutils) mkfifo invocation'\n"));
        let (r, out, _, _) = run(&["--version"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("mkfifo (GNU coreutils) 9.4\n"));
        assert!(out.ends_with("Written by David MacKenzie.\n"));
    }

    #[test]
    fn multiple_names_and_mixed_failure() {
        let (r, _, err, sc) = run(&["ok1", "nodir/bad", "ok2"]);
        assert_eq!(r, 1);
        assert!(sc.path("ok1").exists());
        assert!(sc.path("ok2").exists());
        assert!(err.contains("nodir/bad"));
    }

    #[test]
    fn attached_short_value() {
        // probed: `-m600` (bundled value) works like `-m 600`
        let (r, _, _, sc) = run(&["-m600", "pm"]);
        assert_eq!(r, 0);
        assert!(sc.path("pm").exists());
    }
}
