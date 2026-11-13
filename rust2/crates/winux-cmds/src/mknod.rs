//! `mknod` — GNU coreutils mknod.c (9.4) semantics, probed byte-for-byte
//! against WSL coreutils 9.4 (rust2/target/probe/w5-mkfifo-mknod.sh).
//!
//! Type `p` creates the #1038 fifo marker (same contract as mkfifo;
//! mkfifo.c/mknod.c share the mode gate at mknod.c:154-162). Types
//! `b`/`c`/`u` are block/character device nodes: Windows has no device
//! node type to create, so this port reports GNU's EPERM wording
//! (`mknod: 'X': Operation not permitted`, rc 1 — the 1.x C++ applet's
//! documented subset; mknod.cpp run()). Honest boundary: on the WSL gate
//! the GNU oracle runs as root and CREATES such nodes successfully, so
//! device-creation rows are win-mode (Rust vs C++ only) in the gate and
//! the GNU root success is recorded in the probe file.
//!
//! MAJOR/MINOR parse with xstrtoumax base-0 rules (mknod.c:236-245):
//! 0x hex, leading-0 octal, else decimal. FIFO rejects major/minor
//! ("Fifos do not have major and minor device numbers.", probed) and
//! special files require them ("Special files require major and minor
//! device numbers.", probed). The exists diagnostic follows quotef
//! (mknod.c:257): plain names unquoted ("mknod: mp: File exists").
//! `--context=CTX` warns and still creates; bare `-Z` silent (mknod.c:
//! 131-140, probed rc 0 on the fifo path).

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use crate::fsutil::resolve;
use crate::gnuhelp;
use crate::mkfifo::{evaluate_mode, imp};
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('m'),
        long: "mode",
        takes_value: true,
    },
    // mknod.c: --context[=CTX] optional_argument, -Z bare (same getopt
    // shape mkfifo.c uses; see mkfifo.rs split_context_tokens).
    OptSpec {
        short: Some('Z'),
        long: "context",
        takes_value: false,
    },
];

// `--help` body verbatim from `env mknod --help` under WSL coreutils 9.4
// (Ubuntu 24.04 carries an extra NOTE paragraph after the TYPE list —
// reproduced verbatim because the gate's strict --help row compares it).
const MKNOD_HELP_BODY: &str = r#"Usage: mknod [OPTION]... NAME TYPE [MAJOR MINOR]
Create the special file NAME of the given TYPE.

Mandatory arguments to long options are mandatory for short options too.
  -m, --mode=MODE    set file permission bits to MODE, not a=rw - umask
  -Z                   set the SELinux security context to default type
      --context[=CTX]  like -Z, or if CTX is specified then set the SELinux
                         or SMACK security context to CTX
      --help        display this help and exit
      --version     output version information and exit

Both MAJOR and MINOR must be specified when TYPE is b, c, or u, and they
must be omitted when TYPE is p.  If MAJOR or MINOR begins with 0x or 0X,
it is interpreted as hexadecimal; otherwise, if it begins with 0, as octal;
otherwise, as decimal.  TYPE may be:

  b      create a block (buffered) special file
  c, u   create a character (unbuffered) special file
  p      create a FIFO

NOTE: your shell may have its own version of mknod, which usually supersedes
the version described here.  Please refer to your shell's documentation
for details about the options it supports.

"#;

#[derive(Clone, Copy, PartialEq)]
enum NodeType {
    Block,
    Character,
    Fifo,
}

/// mknod.c:236-245 xstrtoumax base 0: 0x/0X hex, leading-0 octal, else
/// decimal (a bare "0" is octal-digits → 0 either way).
fn parse_uint_arg(arg: &str, label: &str) -> Result<u64, String> {
    if arg.is_empty() {
        return Err(format!("missing {label}"));
    }
    let lower = arg.to_ascii_lowercase();
    let (base, digits): (u32, &str) = if lower.len() > 2 && lower.starts_with("0x") {
        (16, &arg[2..])
    } else if lower.len() > 1 && lower.starts_with('0') {
        (8, &arg[1..])
    } else {
        (10, arg)
    };
    u64::from_str_radix(digits, base).map_err(|_| format!("invalid {label} '{arg}'"))
}

/// The quotef rule (mknod.c:257): plain names print unquoted; quotes
/// appear around names containing space/tab/newline/quote/backslash
/// (probed: "mknod: mp: File exists" unquoted).
fn quotef_mknod(name: &str) -> String {
    let needs = name
        .bytes()
        .any(|b| matches!(b, b' ' | b'\t' | b'\n' | b'\'' | b'"' | b'\\'));
    if needs {
        format!("'{name}'")
    } else {
        name.to_string()
    }
}

pub fn mknod_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let (context_value, args) = crate::mkfifo::split_context_tokens(args);
    let p = match argopt::parse("mknod", &args, OPTS, io, ctx, 1) {
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
                    MKNOD_HELP_BODY,
                    gnuhelp::footer("mknod", "mknod invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("mknod", "David MacKenzie")
                )?;
                0
            }
        });
    }

    // mknod.c:154-162 mode gate runs before the operand-shape errors
    // (probed order in the C++ build_config; GNU compiles the mode at
    // case 'm' time, i.e. BEFORE the positional walk).
    let mut mode_value = 0o666u32;
    let has_mode = p.has("mode");
    if has_mode {
        match evaluate_mode(p.value_of("mode").unwrap_or("")) {
            Ok(v) => mode_value = v,
            Err(msg) => {
                let _ = writeln!(io.stderr, "mknod: {msg}");
                return Ok(1);
            }
        }
    }

    let pos: Vec<String> = p
        .positionals
        .iter()
        .map(|a| a.to_string_lossy().into_owned())
        .collect();

    // Operand-shape errors, probed:
    //   `mknod` -> missing operand (+try, rc 1)
    //   `mknod onlyname` -> missing operand after 'onlyname' (+try)
    //   `mknod n2 c` -> missing operand after 'c' + special-files note (+try)
    //   `mknod mx x 1 3` -> invalid device type 'x' (+try)
    //   `mknod mf p 1 3` -> extra operand '1' + fifo note (+try)
    //   `mknod me c 1 3 extra` -> extra operand 'extra' (+try)
    if pos.is_empty() {
        let _ = winux_core::usage_error(io, "mknod", "missing operand");
        return Ok(1);
    }
    if pos.len() < 2 {
        let _ =
            winux_core::usage_error(io, "mknod", &format!("missing operand after '{}'", pos[0]));
        return Ok(1);
    }
    let name = pos[0].clone();
    let node_type = match pos[1].as_str() {
        "b" => NodeType::Block,
        "c" | "u" => NodeType::Character,
        "p" => NodeType::Fifo,
        other => {
            let _ = winux_core::usage_error(io, "mknod", &format!("invalid device type '{other}'"));
            return Ok(1);
        }
    };

    if node_type == NodeType::Fifo {
        if pos.len() > 2 {
            let _ = winux_core::usage_error(
                io,
                "mknod",
                &format!(
                    "extra operand '{}'\nFifos do not have major and minor device numbers.",
                    pos[2]
                ),
            );
            return Ok(1);
        }
    } else {
        if pos.len() > 4 {
            let _ = winux_core::usage_error(io, "mknod", &format!("extra operand '{}'", pos[4]));
            return Ok(1);
        }
        if pos.len() < 4 {
            let mut msg = format!("missing operand after '{}'", pos[pos.len() - 1]);
            if pos.len() == 2 {
                msg.push_str("\nSpecial files require major and minor device numbers.");
            }
            let _ = winux_core::usage_error(io, "mknod", &msg);
            return Ok(1);
        }
        if let Err(msg) = parse_uint_arg(&pos[2], "major device number") {
            let _ = writeln!(io.stderr, "mknod: {msg}");
            return Ok(1);
        }
        if let Err(msg) = parse_uint_arg(&pos[3], "minor device number") {
            let _ = writeln!(io.stderr, "mknod: {msg}");
            return Ok(1);
        }
    }

    // mknod.c:131-140: --context=CTX warns and still creates on a
    // non-SELinux kernel; a bare -Z is silent.
    if let Some(v) = &context_value {
        if !v.is_empty() {
            let _ = writeln!(
                io.stderr,
                "mknod: warning: ignoring --context; it requires an SELinux/SMACK-enabled kernel"
            );
        }
    }

    let native = resolve(ctx, &name).to_string_lossy().into_owned();
    if imp::attributes(&native).is_some() {
        // quotef (mknod.c:257): plain names unquoted
        let _ = writeln!(io.stderr, "mknod: {}: File exists", quotef_mknod(&name));
        return Ok(1);
    }

    if node_type == NodeType::Fifo {
        let readonly = has_mode && (mode_value & 0o222) == 0;
        let created = imp::create_fifo(&native, readonly);
        if created != 0 {
            let _ = writeln!(
                io.stderr,
                "mknod: '{}': {}",
                name,
                crate::mkfifo::create_error_text(created)
            );
            return Ok(1);
        }
        return Ok(0);
    }

    // Honest boundary: block/character device nodes have no Windows
    // equivalent (see module head).
    let _ = writeln!(io.stderr, "mknod: '{}': Operation not permitted", name);
    Ok(1)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str]) -> (u32, String, String, ScratchCtx) {
        let sc = ScratchCtx::new("mknod");
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = mknod_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err(), sc)
    }

    #[test]
    fn fifo_via_marker() {
        let (r, out, err, sc) = run(&["mp", "p"]);
        assert_eq!(r, 0);
        assert!(out.is_empty());
        assert!(err.is_empty());
        assert_eq!(
            std::fs::read(sc.path("mp")).unwrap(),
            crate::mkfifo::FIFO_MARKER
        );
    }

    #[test]
    fn mode_applies_to_fifo() {
        // probed: mknod -m 600 m6 p -> rc 0 (readonly attr, per mkfifo rule)
        let (r, _, _, sc) = run(&["-m", "600", "m6", "p"]);
        assert_eq!(r, 0);
        assert!(sc.path("m6").exists());
    }

    #[test]
    fn device_types_report_eperm() {
        // probed C++ subset wording (GNU-as-root would create: win-mode
        // rows in the gate; see module head)
        for args in [
            vec!["mc", "c", "1", "3"],
            vec!["mb", "b", "7", "0"],
            vec!["mu", "u", "1", "5"],
        ] {
            let (r, out, err, sc) = run(&args);
            assert_eq!(r, 1, "{args:?}");
            assert!(out.is_empty());
            assert!(err.contains("Operation not permitted"));
            assert!(!sc.path(args[0]).exists());
        }
    }

    #[test]
    fn operand_shape_errors() {
        // probed wordings
        let (r, _, err, _) = run(&[]);
        assert_eq!(r, 1);
        assert!(err.contains("mknod: missing operand"));
        assert!(err.contains("Try 'mknod --help' for more information."));

        let (r, _, err, _) = run(&["onlyname"]);
        assert_eq!(r, 1);
        assert!(err.contains("missing operand after 'onlyname'"));

        let (r, _, err, _) = run(&["n2", "c"]);
        assert_eq!(r, 1);
        assert!(err.contains("missing operand after 'c'"));
        assert!(err.contains("Special files require major and minor device numbers."));

        let (r, _, err, _) = run(&["mx", "x", "1", "3"]);
        assert_eq!(r, 1);
        assert!(err.contains("invalid device type 'x'"));

        let (r, _, err, _) = run(&["mf", "p", "1", "3"]);
        assert_eq!(r, 1);
        assert!(err.contains("extra operand '1'"));
        assert!(err.contains("Fifos do not have major and minor device numbers."));

        let (r, _, err, _) = run(&["me", "c", "1", "3", "extra"]);
        assert_eq!(r, 1);
        assert!(err.contains("extra operand 'extra'"));
    }

    #[test]
    fn major_minor_base0_parse() {
        // unit-level: the xstrtoumax base-0 rules
        assert_eq!(parse_uint_arg("0x1", "major device number"), Ok(1));
        assert_eq!(parse_uint_arg("01", "major device number"), Ok(1));
        assert_eq!(parse_uint_arg("12", "major device number"), Ok(12));
        assert!(parse_uint_arg("xyz", "major device number")
            .unwrap_err()
            .contains("invalid major device number 'xyz'"));
        assert_eq!(
            parse_uint_arg("", "minor device number"),
            Err("missing minor device number".to_string())
        );
    }

    #[test]
    fn exists_wording_follows_quotef() {
        // probed: plain names UNQUOTED ("mknod: mp: File exists"); the
        // second invocation in the SAME scratch collides with the first.
        let sc = ScratchCtx::new("mknod-exists");
        let ctx = sc.ctx();
        let mk = |args: &[&str], io: &mut MemIo| {
            let owned: Vec<OsString> = args.iter().map(Into::into).collect();
            mknod_cmd(&owned, &mut io.split(), &ctx).unwrap()
        };
        let mut io = MemIo::new("");
        assert_eq!(mk(&["mp", "p"], &mut io), 0);
        let mut io = MemIo::new("");
        assert_eq!(mk(&["mp", "p"], &mut io), 1);
        assert!(io.err().contains("mknod: mp: File exists"));
        assert!(!io.err().contains("'mp'"));
    }

    #[test]
    fn context_warn_on_fifo() {
        let (r, _, err, sc) = run(&["--context=c", "pf", "p"]);
        assert_eq!(r, 0);
        assert!(err.contains("mknod: warning: ignoring --context"));
        assert!(sc.path("pf").exists());
    }

    #[test]
    fn help_version_surface() {
        let (r, out, err, _) = run(&["--help"]);
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.starts_with("Usage: mknod [OPTION]... NAME TYPE [MAJOR MINOR]\n"));
        assert!(out.ends_with("info '(coreutils) mknod invocation'\n"));
        let (r, out, _, _) = run(&["--version"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("mknod (GNU coreutils) 9.4\n"));
    }

    #[test]
    fn missing_parent_enoent() {
        let (r, _, err, _) = run(&["nodir/x", "p"]);
        assert_eq!(r, 1);
        assert!(err.contains("No such file or directory"));
    }
}
