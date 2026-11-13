//! winux-core: the "library-form command" shape contract for WinuxCmd 2.0
//! (issue unixwin/WinuxCmd#1488, wave1 pilot).
//!
//! Shape rules (see rust2/README.md for the full contract):
//! * A command is a plain function `fn run(args, io, ctx) -> CmdResult`.
//! * All IO is explicit: the caller injects `Read`/`Write` trait objects via
//!   [`Io`]. Nothing in a command touches process-global stdio.
//! * Environment and cwd are explicit: [`Ctx`] carries an [`EnvSource`] and a
//!   cwd the command must treat as its working directory.
//! * No process-global mutable state. Commands own no statics; everything
//!   they need arrives through arguments.
//! * There is no `exit()`. The command returns a status code
//!   (`0` success, `1` GNU-style failure, `2` GNU-style usage/syntax error
//!   for `test`/`[`), and the ABI boundary turns panics into status 125
//!   (see winuxcmd-ffi).
//!
//! GNU C coreutils is the semantic specification; wording of stderr may
//! deviate (recorded by the differential gate), but stdout bytes and exit
//! status must match GNU byte-for-byte on platform-neutral cases.

use std::ffi::OsString;
use std::io::{Read, Write};
use std::path::PathBuf;

pub mod path;
pub mod stream;

/// Explicit IO bundle injected by the caller.
///
/// A command may only write through these handles. Writers are not flushed
/// by the command itself; the *caller* (driver / ABI boundary) flushes, which
/// mirrors GNU's "stdout is the caller's stream" reality and keeps stderr
/// (unbuffered by convention here) independent of stdout buffering.
pub struct Io<'a> {
    pub stdin: &'a mut dyn Read,
    pub stdout: &'a mut dyn Write,
    pub stderr: &'a mut dyn Write,
}

impl<'a> Io<'a> {
    pub fn new(
        stdin: &'a mut dyn Read,
        stdout: &'a mut dyn Write,
        stderr: &'a mut dyn Write,
    ) -> Self {
        Io {
            stdin,
            stdout,
            stderr,
        }
    }
}

/// Explicit environment access. `entries()` returns the environ in the
/// order the caller provides (GNU iterates `environ[]` order, not sorted).
pub trait EnvSource {
    fn get(&self, key: &str) -> Option<OsString>;
    fn entries(&self) -> Vec<(OsString, OsString)>;
}

/// Explicit execution context: the cwd the command must assume plus the
/// environment it may read. Nothing else is inherited.
pub struct Ctx<'e> {
    pub cwd: PathBuf,
    pub env: &'e dyn EnvSource,
}

/// Command return type. `Ok(status)` is the GNU exit status.
/// `Err` is reserved for ABI-level IO failure (broken handle); the boundary
/// maps it to status 125, mirroring a write failure to stdout in GNU.
pub type CmdResult = Result<u32, CmdError>;

#[derive(Debug)]
pub enum CmdError {
    Io(std::io::Error),
}

impl From<std::io::Error> for CmdError {
    fn from(e: std::io::Error) -> Self {
        CmdError::Io(e)
    }
}

/// Status returned at the ABI boundary when a command panics or hits an IO
/// error on its handles. 125 is GNU's "internal/abnormal" convention.
pub const ABI_FAILURE_STATUS: u32 = 125;

/// Write a GNU-style usage diagnostic to stderr:
/// `<prog>: <msg>\nTry '<prog> --help' for more information.\n`
///
/// Wording is recorded by the gate, not gated byte-for-byte (GNU quotes with
/// Unicode quotation marks; we use ASCII quotes on stderr).
pub fn usage_error(io: &mut Io, prog: &str, msg: &str) -> std::io::Result<()> {
    writeln!(io.stderr, "{prog}: {msg}")?;
    writeln!(io.stderr, "Try '{prog} --help' for more information.")
}

/// Quote a word for GNU-style verbose stdout messages (straight quotes,
/// e.g. `mkdir: created directory 'x'`). These go to *stdout* in GNU and are
/// gate-strict, so the quoting must match byte-for-byte.
pub fn q(s: &str) -> String {
    format!("'{s}'")
}

/// Resolve `path` against the explicit `ctx.cwd` (not the process cwd).
pub fn abs_path(ctx: &Ctx, path: &std::path::Path) -> PathBuf {
    if path.is_absolute() {
        path.to_path_buf()
    } else {
        let mut p = ctx.cwd.clone();
        p.push(path);
        p
    }
}

/// Strip the Windows verbatim prefix (`\\?\C:\...`) that
/// `std::fs::canonicalize` produces, so output matches what a user types.
pub fn strip_verbatim(p: PathBuf) -> PathBuf {
    let s = p.as_os_str().to_string_lossy().into_owned();
    if let Some(rest) = s.strip_prefix(r"\\?\UNC\") {
        return PathBuf::from(format!(r"\\{rest}"));
    }
    if let Some(rest) = s.strip_prefix(r"\\?\") {
        return PathBuf::from(rest);
    }
    p
}

/// In-memory [`EnvSource`] for unit tests and the ABI boundary.
pub struct MapEnv {
    pub vars: Vec<(OsString, OsString)>,
}

impl MapEnv {
    pub fn new() -> Self {
        MapEnv { vars: Vec::new() }
    }

    pub fn set(&mut self, key: &str, value: &str) {
        self.vars.push((OsString::from(key), OsString::from(value)));
    }
}

impl Default for MapEnv {
    fn default() -> Self {
        Self::new()
    }
}

impl EnvSource for MapEnv {
    fn get(&self, key: &str) -> Option<OsString> {
        self.vars
            .iter()
            .rev()
            .find(|(k, _)| k.to_string_lossy() == key)
            .map(|(_, v)| v.clone())
    }

    fn entries(&self) -> Vec<(OsString, OsString)> {
        self.vars.clone()
    }
}

/// Win32-block lookup for the process-resolution tier: PATH, PATHEXT,
/// COMSPEC and friends as the loader/search layer sees them. The ABI
/// snapshot mirrors a Win32 environment block (GetEnvironmentStringsW),
/// where the canonical writer casing is the parent's — "Path" from a
/// PowerShell/cmd parent, "PATH" from an MSYS one — and
/// GetEnvironmentVariableW matches case-insensitively, first occurrence
/// wins. An exact-name match there misses the block's only entry and
/// spawns the System32-default invention of the #1567 wiring regression.
/// This is deliberately NOT `EnvSource::get`: environ-semantics commands
/// (printenv `gfoo` vs `GFoo`, gate row pe10-lowercase) must keep GNU's
/// exact POSIX match, so the case-insensitive tier is an explicit,
/// resolution-only escape hatch.
pub fn win32_env_get(env: &dyn EnvSource, key: &str) -> Option<OsString> {
    env.entries()
        .into_iter()
        .find(|(k, _)| k.to_string_lossy().eq_ignore_ascii_case(key))
        .map(|(_, v)| v)
}

/// Process-environment adapter used by the thin CLI driver (the only place
/// process state is allowed to be read; library functions never touch it).
pub struct ProcessEnv;

impl EnvSource for ProcessEnv {
    /// Exact-name lookup. std::env::var_os is case-insensitive on Windows
    /// (the Win32 environment block is), but GNU `printenv gfoo` must NOT
    /// find `GFoo`, so we scan the block and compare names exactly.
    fn get(&self, key: &str) -> Option<OsString> {
        std::env::vars_os()
            .into_iter()
            .find(|(k, _)| k.to_string_lossy() == key)
            .map(|(_, v)| v)
    }

    fn entries(&self) -> Vec<(OsString, OsString)> {
        std::env::vars_os().collect()
    }
}

/// Test support: available unconditionally because downstream crates'
/// `cfg(test)` builds do not enable `cfg(test)` in their dependencies.
/// Runtime code never links these helpers.
pub mod test_support {
    use super::*;
    use std::io::Cursor;

    /// Build a `Vec<OsString>` from string literals for call sites.
    pub fn os(args: &[&str]) -> Vec<OsString> {
        args.iter().map(Into::into).collect()
    }

    /// Disjoint in-memory IO bundle: input via `Cursor`, output/error as
    /// plain `Vec<u8>` buffers. Lets unit tests run commands with zero
    /// filesystem and zero process state.
    pub struct MemIo {
        pub input: Cursor<Vec<u8>>,
        pub output: Vec<u8>,
        pub errout: Vec<u8>,
    }

    impl MemIo {
        pub fn new(input: &str) -> Self {
            MemIo {
                input: Cursor::new(input.as_bytes().to_vec()),
                output: Vec::new(),
                errout: Vec::new(),
            }
        }

        pub fn split(&mut self) -> Io<'_> {
            Io {
                stdin: &mut self.input,
                stdout: &mut self.output,
                stderr: &mut self.errout,
            }
        }

        pub fn out(&self) -> String {
            String::from_utf8_lossy(&self.output).into_owned()
        }

        pub fn err(&self) -> String {
            String::from_utf8_lossy(&self.errout).into_owned()
        }
    }

    /// Context bound to a fresh scratch directory (caller removes it).
    pub struct ScratchCtx {
        pub dir: PathBuf,
        pub env: MapEnv,
    }

    impl ScratchCtx {
        pub fn new(tag: &str) -> Self {
            // Uniqueness = process id + a per-process counter. The clock
            // alone is NOT unique: Windows SystemTime advances in coarse
            // ticks (up to ~15.6 ms granularity), so parallel test threads
            // that build scratch dirs back-to-back can observe the SAME
            // timestamp — two ScratchCtx instances then share one
            // directory, and whichever test finishes first deletes the
            // other's files mid-run (remove_dir_all in Drop). The CI
            // `csplit::tests::prefix_digits_suffix_format` rc-1 flake was
            // exactly that shape: the successor run's ENOENT surfaced as a
            // nonzero command status. pid separates concurrent cargo test
            // processes; the counter separates threads within one.
            use std::sync::atomic::{AtomicU64, Ordering};
            static SCRATCH_SEQ: AtomicU64 = AtomicU64::new(0);
            let dir = std::env::temp_dir().join(format!(
                "winux-cmds-test-{}-{}-{}",
                tag,
                std::process::id(),
                SCRATCH_SEQ.fetch_add(1, Ordering::Relaxed),
            ));
            std::fs::create_dir_all(&dir).unwrap();
            ScratchCtx {
                dir,
                env: MapEnv::new(),
            }
        }

        pub fn ctx(&self) -> Ctx<'_> {
            Ctx {
                cwd: self.dir.clone(),
                env: &self.env,
            }
        }

        pub fn path(&self, rel: &str) -> PathBuf {
            self.dir.join(rel)
        }
    }

    impl Drop for ScratchCtx {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self.dir);
        }
    }

    #[test]
    fn win32_env_get_is_case_insensitive_first_match() {
        // The resolution tier mirrors GetEnvironmentVariableW over the
        // snapshot block: case-insensitive, first occurrence wins (#1567
        // wiring regression: a "Path"-keyed block from a PowerShell parent
        // made the rust2 timeout invent the System32 default for PATH).
        let mut env = MapEnv::new();
        env.set("Path", r"C:\one");
        assert_eq!(
            win32_env_get(&env, "PATH").map(|v| v.to_string_lossy().into_owned()),
            Some(r"C:\one".to_string())
        );
        // first occurrence in block order wins over later variants
        env.set("PATH", r"C:\two");
        assert_eq!(
            win32_env_get(&env, "PaTh").map(|v| v.to_string_lossy().into_owned()),
            Some(r"C:\one".to_string())
        );
        assert_eq!(win32_env_get(&env, "PATHEXT"), None);
        // MapEnv::get stays exact for the environ-semantics commands
        // (printenv `gfoo` vs `GFoo`, gate row pe10-lowercase)
        assert_eq!(env.get("path"), None);
        assert_eq!(
            env.get("PATH").map(|v| v.to_string_lossy().into_owned()),
            Some(r"C:\two".to_string())
        );
    }
}
