//! `mktemp` — GNU coreutils mktemp.c (9.4) semantics, probed byte-for-byte
//! against WSL coreutils 9.4 (rust2/target/probe/p-mk.sh + mk2/fin2) and
//! aligned with the audited 1.x body (src/commands/mktemp.cpp, #1358).
//!
//! X-run rules (mktemp.c order, all probed): with --suffix the template
//! must END in X (`with --suffix, template 'T' must end in X`), the
//! suffix must not contain a slash (`invalid suffix 'S', contains
//! directory separator`), and the trailing-X run (>= 3, uppercase only)
//! is checked with the combined template+suffix in the message. Without
//! --suffix the region after the last X is the implicit suffix (a slash
//! there is the same invalid-suffix error) and the run before it must be
//! >= 3 (`too few X's in template 'T'`; lowercase `x` never counts —
//! `mktemp xxx` fails). More than one template operand dies before any
//! validation (`too many templates`).
//!
//! Destination: --tmpdir[=DIR]/-p DIR, else the template's own directory;
//! the default template `tmp.XXXXXXXXXX` implies --tmpdir (TMPDIR env,
//! falling back to TEMP/TMP on Windows — the GetTempPathW analog of
//! mktemp.c's /tmp). A slash in the suffix or an absolute template with
//! --tmpdir dies. Creation replaces the trailing X-run of the final
//! component with 62-alphabet random characters, retries on collision
//! (100 draws), and diagnoses a hard failure as
//! `failed to create file|directory via template 'FULLDEST'` with the
//! X's intact; -q silences exactly that message (too-few-X's still
//! prints, probed). -u picks a free name and prints it without creating.
//!
//! Windows subset: none in the observable rc/stdout contract; created
//! files use default attributes (GNU's u+rw minus umask has no NTFS
//! counterpart).

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use crate::fsutil::resolve;
use crate::gnuhelp;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('d'),
        long: "directory",
        takes_value: false,
    },
    OptSpec {
        short: Some('q'),
        long: "quiet",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "suffix",
        takes_value: true,
    },
    OptSpec {
        short: Some('p'),
        long: "tmpdir",
        takes_value: true,
    },
    OptSpec {
        short: Some('t'),
        long: "",
        takes_value: false,
    },
    OptSpec {
        short: Some('u'),
        long: "dry-run",
        takes_value: false,
    },
];

/// GNU's random-character alphabet (mktemp.c).
const CHARSET: &[u8] = b"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

/// xorshift64* seeded from wall-clock and pid: adequate for temp-name
/// uniqueness, matching mktemp.c's "random enough" contract.
struct Rand(u64);

impl Rand {
    fn new() -> Self {
        let nanos = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_nanos() as u64)
            .unwrap_or(0x2545F4914F6CDD1D);
        let pid = std::process::id() as u64;
        let mut s = nanos ^ (pid << 32) ^ (pid.wrapping_mul(0x9E3779B97F4A7C15));
        if s == 0 {
            s = 0x9E3779B97F4A7C15;
        }
        Rand(s)
    }
    fn next(&mut self) -> u64 {
        let mut x = self.0;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        self.0 = x;
        x.wrapping_mul(0x2545F4914F6CDD1D)
    }
    fn char(&mut self) -> u8 {
        CHARSET[(self.next() % CHARSET.len() as u64) as usize]
    }
}

fn count_trailing_xs(s: &str) -> usize {
    s.bytes().rev().take_while(|&b| b == b'X').count()
}

// `--help` body verbatim from `env mktemp --help` under WSL coreutils
// 9.4, minus the shared gnuhelp::footer tail.
const MKTEMP_HELP_BODY: &str = r#"Usage: mktemp [OPTION]... [TEMPLATE]
Create a temporary file or directory, safely, and print its name.
TEMPLATE must contain at least 3 consecutive 'X's in last component.
If TEMPLATE is not specified, use tmp.XXXXXXXXXX, and --tmpdir is implied.
Files are created u+rw, and directories u+rwx, minus umask restrictions.

  -d, --directory     create a directory, not a file
  -u, --dry-run       do not create anything; merely print a name (unsafe)
  -q, --quiet         suppress diagnostics about file/dir-creation failure
      --suffix=SUFF   append SUFF to TEMPLATE; SUFF must not contain a slash.
                        This option is implied if TEMPLATE does not end in X
  -p DIR, --tmpdir[=DIR]  interpret TEMPLATE relative to DIR; if DIR is not
                        specified, use $TMPDIR if set, else /tmp.  With
                        this option, TEMPLATE must not be an absolute name;
                        unlike with -t, TEMPLATE may contain slashes, but
                        mktemp creates only the final component
  -t                  interpret TEMPLATE as a single file name component,
                        relative to a directory: $TMPDIR, if set; else the
                        directory specified via -p; else /tmp [deprecated]
      --help        display this help and exit
      --version     output version information and exit

"#;

fn looks_absolute(t: &str) -> bool {
    t.starts_with('/') || t.starts_with('\\') || {
        let b = t.as_bytes();
        b.len() >= 2 && b[0].is_ascii_alphabetic() && b[1] == b':'
    }
}

pub fn mktemp_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    // --tmpdir takes an OPTIONAL argument (getopt "::"): bare `--tmpdir`
    // means "use the default directory" and the template stays an
    // operand, while `--tmpdir=DIR` binds the value. Normalize both to
    // the required-argument -p form before parsing.
    let mut norm: Vec<OsString> = Vec::with_capacity(args.len() + 1);
    for a in args {
        let s = a.to_string_lossy().into_owned();
        if s == "--tmpdir" {
            norm.push(OsString::from("-p"));
            norm.push(OsString::from(""));
        } else if let Some(dir) = s.strip_prefix("--tmpdir=") {
            norm.push(OsString::from("-p"));
            norm.push(OsString::from(dir));
        } else {
            norm.push(a.clone());
        }
    }
    let p = match argopt::parse("mktemp", &norm, OPTS, io, ctx, 1) {
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
                    MKTEMP_HELP_BODY,
                    gnuhelp::footer("mktemp", "mktemp invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("mktemp", "Jim Meyering and Eric Blake")
                )?;
                0
            }
        });
    }

    let make_dir = p.has("directory");
    let dry_run = p.has("dry-run");
    let quiet = p.has("quiet");
    let use_t = p.has("t");
    let suffix = p.value_of("suffix").unwrap_or("").to_string();
    let tmpdir_value = p.value_of("tmpdir").map(|v| v.to_string());
    let tmpdir_flag = tmpdir_value.is_some();

    // more than one template operand dies before suffix/template validation
    if p.positionals.len() > 1 {
        let _ = writeln!(io.stderr, "mktemp: too many templates");
        let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "mktemp"));
        return Ok(1);
    }

    let (template, implied_tmpdir) = match p.positionals.first() {
        Some(t) => (t.to_string_lossy().into_owned(), false),
        None => ("tmp.XXXXXXXXXX".to_string(), true),
    };

    // (PathBuf, display string): the display keeps the operand/env value
    // as given so output matches what the user handed in (GNU prints the
    // destination prefix verbatim)
    let default_dir = || -> (std::path::PathBuf, String) {
        for key in ["TMPDIR", "TEMP", "TMP"] {
            let hit = winux_core::win32_env_get(ctx.env, key)
                .map(|v| v.to_string_lossy().into_owned())
                .filter(|s| !s.is_empty());
            if let Some(d) = hit {
                return (std::path::PathBuf::from(&d), d);
            }
        }
        (ctx.cwd.clone(), ctx.cwd.display().to_string())
    };

    // destination-directory mode: --tmpdir/-p, or implied by the default
    // template (mktemp.c: "and --tmpdir is implied")
    let mut dir_display = String::new();
    let dest_dir: Option<std::path::PathBuf> = if tmpdir_flag || implied_tmpdir || use_t {
        let explicit = tmpdir_value.clone().unwrap_or_default();
        let (dir, shown) = if explicit.is_empty() {
            // -t prefers $TMPDIR over a -p DIR (mktemp.c; audited #1358)
            if use_t {
                let env_hit = winux_core::win32_env_get(ctx.env, "TMPDIR")
                    .map(|v| v.to_string_lossy().into_owned())
                    .filter(|s| !s.is_empty());
                match env_hit {
                    Some(d) => (std::path::PathBuf::from(&d), d),
                    None => match tmpdir_value.clone() {
                        Some(v) if !v.is_empty() => {
                            let shown = v.clone();
                            (std::path::PathBuf::from(v), shown)
                        }
                        _ => default_dir(),
                    },
                }
            } else {
                default_dir()
            }
        } else {
            let shown = explicit.clone();
            (std::path::PathBuf::from(explicit), shown)
        };
        dir_display = shown;
        Some(dir)
    } else {
        None
    };

    // X-run validation (mktemp.c order, probed); an EXPLICIT --suffix
    // enters the suffix rules even when its value is empty
    if p.has("suffix") {
        if template.is_empty() || !template.ends_with('X') {
            let _ = writeln!(
                io.stderr,
                "mktemp: with --suffix, template '{template}' must end in X"
            );
            return Ok(1);
        }
        if suffix.contains('/') || suffix.contains('\\') {
            let _ = writeln!(
                io.stderr,
                "mktemp: invalid suffix '{suffix}', contains directory separator"
            );
            return Ok(1);
        }
        if count_trailing_xs(&template) < 3 {
            let _ = writeln!(
                io.stderr,
                "mktemp: too few X's in template '{template}{suffix}'"
            );
            return Ok(1);
        }
    } else {
        let tail = match template.rfind('X') {
            Some(i) => template[i + 1..].to_string(),
            None => String::new(),
        };
        if tail.contains('/') || tail.contains('\\') {
            let _ = writeln!(
                io.stderr,
                "mktemp: invalid suffix '{tail}', contains directory separator"
            );
            return Ok(1);
        }
        let head = match template.rfind('X') {
            Some(i) => &template[..i + 1],
            None => "",
        };
        if count_trailing_xs(head) < 3 {
            let _ = writeln!(io.stderr, "mktemp: too few X's in template '{template}'");
            return Ok(1);
        }
    }

    // template-component checks + base directory resolution
    let mut component = template.clone();
    let base_dir: std::path::PathBuf = match &dest_dir {
        Some(d) => {
            if looks_absolute(&template) {
                let _ = writeln!(
                    io.stderr,
                    "mktemp: invalid template, '{template}'; with --tmpdir, it may not be absolute"
                );
                return Ok(1);
            }
            if use_t && (template.contains('/') || template.contains('\\')) {
                let _ = writeln!(
                    io.stderr,
                    "mktemp: invalid template, '{template}', contains directory separator"
                );
                return Ok(1);
            }
            d.clone()
        }
        None => {
            // no --tmpdir: the template's own directory part is the base
            match template.rfind(['/', '\\']) {
                Some(0) => {
                    component = template[1..].to_string();
                    dir_display = "/".to_string();
                    std::path::PathBuf::from("/")
                }
                Some(i) => {
                    component = template[i + 1..].to_string();
                    dir_display = template[..i].to_string();
                    resolve(ctx, &template[..i])
                }
                None => ctx.cwd.clone(),
            }
        }
    };

    // find the trailing X-run in the final component
    let mut template_str = component.clone();
    let (run_start, run_len) = {
        let run = count_trailing_xs(&component);
        if run >= 3 {
            (component.len() - run, run)
        } else {
            template_str.push_str("XXXXXX");
            (template_str.len() - 6, 6)
        }
    };
    template_str.push_str(&suffix);

    // GNU prints the full destination path, but a bare template in the
    // current directory prints as the plain template (mktemp.c's
    // file_name_concat with an empty dest_dir)
    let bare_cwd = dest_dir.is_none() && !component.contains(['/', '\\']);
    let dest_template_display = if bare_cwd {
        template_str.clone()
    } else {
        format!("{dir_display}/{template_str}")
    };

    let mut rng = Rand::new();
    for _attempt in 0..100 {
        let mut name_bytes = template_str.clone().into_bytes();
        for k in 0..run_len {
            name_bytes[run_start + k] = rng.char();
        }
        let name = String::from_utf8(name_bytes).unwrap_or_else(|_| template_str.clone());
        let candidate = base_dir.join(&name);
        let display = if bare_cwd {
            name.clone()
        } else {
            format!("{dir_display}/{name}")
        };

        if dry_run {
            if std::fs::metadata(&candidate).is_err() {
                writeln!(io.stdout, "{display}")?;
                return Ok(0);
            }
            continue;
        }
        let created = if make_dir {
            std::fs::create_dir(&candidate)
        } else {
            std::fs::File::create_new(&candidate).map(|_| ())
        };
        match created {
            Ok(()) => {
                writeln!(io.stdout, "{display}")?;
                return Ok(0);
            }
            Err(e) if e.kind() == std::io::ErrorKind::AlreadyExists => continue,
            Err(e) => {
                let kind_word = if make_dir { "directory" } else { "file" };
                if !quiet {
                    let reason = crate::fsutil::reason(&e);
                    let _ = writeln!(
                        io.stderr,
                        "mktemp: failed to create {kind_word} via template '{dest_template_display}': {reason}"
                    );
                }
                return Ok(1);
            }
        }
    }
    let kind_word = if make_dir { "directory" } else { "file" };
    if !quiet {
        let _ = writeln!(
            io.stderr,
            "mktemp: failed to create {kind_word} via template '{dest_template_display}': File exists"
        );
    }
    Ok(1)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str]) -> (u32, String, String) {
        let sc = ScratchCtx::new("mktemp");
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = mktemp_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn gnu_help_version_surface() {
        let (r, out, err) = run(&["--help"]);
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.starts_with("Usage: mktemp [OPTION]... [TEMPLATE]\n"));
        assert!(out.contains("  -d, --directory     create a directory"));
        assert!(out.ends_with("info '(coreutils) mktemp invocation'\n"));
        let (r, out, _) = run(&["--version"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("mktemp (GNU coreutils) 9.4\n"));
        assert!(out.ends_with("Written by Jim Meyering and Eric Blake.\n"));
    }

    #[test]
    fn creates_file_in_cwd() {
        let (r, out, err) = run(&["tpl.XXXXXX"]);
        assert_eq!(r, 0, "err={err}");
        assert!(out.starts_with("tpl."));
        assert_eq!(out.trim().len(), "tpl.XXXXXX".len());
        assert!(out.ends_with("\n"));
    }

    #[test]
    fn creates_directory_and_suffix() {
        let sc = ScratchCtx::new("mktemp-d");
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = ["-d", "dir.XXXXXX"].iter().map(Into::into).collect();
        let r = mktemp_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        let name = io.out().trim().to_string();
        assert!(name.starts_with("dir."));
        assert!(sc.path(&name).is_dir());
        // --suffix appends literally
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["--suffix=.txt", "tpXXXXXX"]
            .iter()
            .map(Into::into)
            .collect();
        let r = mktemp_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        let name = io.out().trim().to_string();
        assert!(name.ends_with(".txt"));
        assert!(sc.path(&name).is_file());
    }

    #[test]
    fn x_count_errors() {
        let (r, out, e) = run(&["X"]);
        assert_eq!(r, 1);
        assert_eq!(out, "");
        assert!(e.contains("mktemp: too few X's in template 'X'"));
        assert!(run(&["xx"]).2.contains("too few X's"));
        // lowercase x never counts (probed: `mktemp xxx` fails)
        assert!(run(&["xxx"]).2.contains("too few X's"));
        assert!(run(&["xxx"]).2.contains("'xxx'"));
        // 4 uppercase X's succeed (probed); 3 is the floor
        assert_eq!(run(&["XXXX"]).0, 0);
        assert_eq!(run(&["XXX"]).0, 0);
        assert!(run(&["XX"]).2.contains("too few X's"));
        // trailing-run counting: XaXX has a run of 2
        assert!(run(&["XaXX"]).2.contains("too few X's"));
        // no X at all
        assert!(run(&["abc"]).2.contains("too few X's"));
    }

    #[test]
    fn suffix_errors() {
        assert!(run(&["--suffix=X", "tpXXXXXX"]).0 == 0);
        assert!(run(&["--suffix=/x", "tpXXXXXX"])
            .2
            .contains("invalid suffix '/x'"));
        // template with a non-X tail containing a slash
        assert!(run(&["XXXXXX/a"]).2.contains("invalid suffix '/a'"));
        assert!(run(&["--suffix=", "XXXXXX."]).2.contains("must end in X"));
    }

    #[test]
    fn too_many_templates() {
        let (r, _, e) = run(&["a.XXXXXX", "b.XXXXXX"]);
        assert_eq!(r, 1);
        assert!(e.contains("too many templates"));
        // reported before suffix validation
        let (r, _, e) = run(&["short", "short2", "--suffix=/x"]);
        assert_eq!(r, 1);
        assert!(e.contains("too many templates"));
    }

    #[test]
    fn tmpdir_handling() {
        let sc = ScratchCtx::new("mktemp-p");
        let subdir = sc.path("sub");
        std::fs::create_dir(&subdir).unwrap();
        let sub_str = subdir.display().to_string();
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = ["-p", &sub_str, "mkp.XXXXXX"]
            .iter()
            .map(Into::into)
            .collect();
        let r = mktemp_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert!(io.out().starts_with(&sub_str));
        // absolute template with --tmpdir dies
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["-p", &sub_str, "/abs.XXXXXX"]
            .iter()
            .map(Into::into)
            .collect();
        let r = mktemp_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("may not be absolute"));
        // default template implies tmpdir
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = Vec::new();
        let r = mktemp_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert!(io.out().contains("tmp."));
    }

    #[test]
    fn dry_run_and_missing_dir() {
        // -u prints a name without creating
        let sc = ScratchCtx::new("mktemp-u");
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = ["-u", "u.XXXXXX"].iter().map(Into::into).collect();
        let r = mktemp_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        let name = io.out().trim().to_string();
        assert!(name.starts_with("u."));
        assert!(!sc.path(&name).exists());
        // missing dest dir: failed-to-create, rc 1; -q silences
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["nosuchdir/xXXXXXX"].iter().map(Into::into).collect();
        let r = mktemp_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io
            .err()
            .starts_with("mktemp: failed to create file via template"));
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["-q", "nosuchdir/xXXXXXX"].iter().map(Into::into).collect();
        let r = mktemp_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().is_empty());
        assert_eq!(run(&["--bogus"]).0, 1);
    }
}
