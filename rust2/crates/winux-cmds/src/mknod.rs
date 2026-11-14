//! `mknod` — GNU coreutils mknod.c (9.4) semantics, probed byte-for-byte
//! against WSL coreutils 9.4 (rust2/target/probe/w5-mkfifo-mknod.sh; the
//! wave-W4 wire re-probed TYPE/order/context shapes in
//! rust2/target/probe/w4-*.sh).
//!
//! Type `p` creates the #1038 fifo marker (same contract as mkfifo;
//! mkfifo.c/mknod.c share the mode gate at mknod.c:154-162). TYPE is
//! classified by its FIRST character (probed: `pz`/`pp` are fifos,
//! `bz`/`cx`/`uz` special files, `xp` invalid), and the operand-count
//! checks precede TYPE validation (`mknod x1 x` is "missing operand after
//! 'x'", not an invalid type). Types `b`/`c`/`u` are block/character
//! device nodes: Windows has no device node type to create, so this port
//! reports GNU's EPERM wording (`mknod: X: Operation not permitted`,
//! quotef — rc 1; the 1.x C++ applet's documented subset, mknod.cpp
//! run()). Honest boundary: on the WSL gate the GNU oracle runs as root
//! and CREATES such nodes successfully, so device-creation rows are
//! win-mode (Rust vs C++ only) in the gate and the GNU root success is
//! recorded in the probe file.
//!
//! MAJOR/MINOR parse with xstrtoumax base-0 rules (mknod.c:236-245):
//! 0x hex, leading-0 octal, else decimal; '+' and leading whitespace are
//! strtoumax's, an EMPTY operand is `invalid ... device number ''`
//! (probed), not "missing". FIFO rejects major/minor ("Fifos do not have
//! major and minor device numbers.", probed) and special files require
//! them ("Special files require major and minor device numbers.",
//! probed). The exists diagnostic follows quotef (mknod.c:257): plain
//! names unquoted ("mknod: mp: File exists"); create failures too.
//! `--context=CTX` warns and still creates, bare `-Z`/`--context` silent
//! (mknod.c:131-140; `--context=` with an empty present value also warns,
//! probed rc 0 on the fifo path).

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use crate::fsutil::resolve;
use crate::gnuhelp;
use crate::mkfifo::{evaluate_mode, imp, ContextOpt, ModeError};
use winux_core::{tr, CmdResult, Ctx, Io};

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

/// mknod.c:154-162 mode gate, mkfifo's keys do not apply here: mknod owns
/// its own `command.mknod.error.*` text for the same evaluator.
fn mode_error_text(ctx: &Ctx, err: ModeError) -> String {
    match err {
        ModeError::Invalid => tr!(ctx, "command.mknod.error.invalid_mode", "invalid mode"),
        ModeError::NotPermissionBits => tr!(
            ctx,
            "command.mknod.error.mode_not_permission_bits",
            "mode must specify only file permission bits"
        ),
    }
}

/// mknod.c:236-245 xstrtoumax base 0: 0x/0X hex, leading-0 octal, else
/// decimal (a bare "0" is octal-digits → 0 either way). strtoumax also
/// skips leading whitespace, so " 1" is 1; an EMPTY argument is not a
/// "missing" one — GNU rejects it as `invalid major device number ''`
/// (probed), so this returns None and the caller words it as invalid.
fn parse_device_number(arg: &str) -> Option<u64> {
    let arg = arg.trim_start_matches(|c: char| c.is_ascii_whitespace());
    let lower = arg.to_ascii_lowercase();
    let (base, digits): (u32, &str) = if lower.len() > 2 && lower.starts_with("0x") {
        (16, &arg[2..])
    } else if lower.len() > 1 && lower.starts_with('0') {
        (8, &arg[1..])
    } else {
        (10, arg)
    };
    u64::from_str_radix(digits, base).ok()
}

/// The quotef rule (mknod.c:257): plain names print unquoted; quotes
/// appear around names containing space/tab/newline/quote/backslash and
/// around the EMPTY name (probed: `mknod '' p` prints `'': No such file
/// or directory`). Probed plain: "mknod: mp: File exists" unquoted.
fn quotef_mknod(name: &str) -> String {
    let needs = name.is_empty()
        || name
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
            Err(err) => {
                let _ = writeln!(io.stderr, "mknod: {}", mode_error_text(ctx, err));
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
        let _ = winux_core::usage_error(ctx, io, "mknod", &crate::messages::missing_operand(ctx));
        return Ok(1);
    }
    if pos.len() < 2 {
        let _ = winux_core::usage_error(
            ctx,
            io,
            "mknod",
            &crate::messages::missing_after(ctx, &pos[0]),
        );
        return Ok(1);
    }
    let name = pos[0].clone();
    let type_text = pos[1].clone();
    // mknod.c classifies TYPE by its FIRST character only: probed GNU 9.4
    // accepts `pz`/`pp` as fifo and `bz`/`cx`/`uz` as block/char (trailing
    // characters ignored); `xp` is invalid. `p` alone selects the fifo
    // branch, everything else the special-file branch.
    let is_fifo = type_text.starts_with('p');

    if is_fifo {
        // Probed: the fifo branch checks the operand count BEFORE the type
        // spelling (`mknod mf pz 1 3` -> extra operand '1' + fifo note).
        if pos.len() > 2 {
            let msg = format!(
                "{}\n{}",
                crate::messages::extra_operand(ctx, &pos[2]),
                tr!(
                    ctx,
                    "command.mknod.error.fifos_no_device_numbers",
                    "Fifos do not have major and minor device numbers."
                )
            );
            let _ = winux_core::usage_error(ctx, io, "mknod", &msg);
            return Ok(1);
        }
    } else {
        // Operand-count checks precede TYPE validation (probed: `mknod x1
        // x` -> "missing operand after 'x'" + special-files note, not
        // "invalid device type"; `mknod x2 x 1 zz` -> invalid device type
        // before the minor-number parse).
        if pos.len() > 4 {
            let _ = winux_core::usage_error(
                ctx,
                io,
                "mknod",
                &crate::messages::extra_operand(ctx, &pos[4]),
            );
            return Ok(1);
        }
        if pos.len() < 4 {
            let mut msg = crate::messages::missing_after(ctx, &pos[pos.len() - 1]);
            if pos.len() == 2 {
                msg.push('\n');
                msg.push_str(&tr!(
                    ctx,
                    "command.mknod.error.special_files_need_numbers",
                    "Special files require major and minor device numbers."
                ));
            }
            let _ = winux_core::usage_error(ctx, io, "mknod", &msg);
            return Ok(1);
        }
        if !matches!(type_text.chars().next(), Some('b' | 'c' | 'u')) {
            let _ = winux_core::usage_error(
                ctx,
                io,
                "mknod",
                &tr!(
                    ctx,
                    "command.mknod.error.invalid_device_type",
                    "invalid device type '{node_type}'",
                    node_type = type_text
                ),
            );
            return Ok(1);
        }
        if parse_device_number(&pos[2]).is_none() {
            let _ = writeln!(
                io.stderr,
                "mknod: {}",
                tr!(
                    ctx,
                    "command.mknod.error.invalid_major",
                    "invalid major device number '{number}'",
                    number = pos[2]
                )
            );
            return Ok(1);
        }
        if parse_device_number(&pos[3]).is_none() {
            let _ = writeln!(
                io.stderr,
                "mknod: {}",
                tr!(
                    ctx,
                    "command.mknod.error.invalid_minor",
                    "invalid minor device number '{number}'",
                    number = pos[3]
                )
            );
            return Ok(1);
        }
    }

    // mknod.c:131-140: --context=CTX warns and still creates on a
    // non-SELinux kernel; a bare -Z / bare --context is silent. Same
    // optarg distinction as mkfifo (probed GNU 9.4: `--context=` warns).
    if let ContextOpt::Value(_) = &context_value {
        let _ = writeln!(
            io.stderr,
            "mknod: {}",
            tr!(
                ctx,
                "command.mknod.warning.ignoring_context",
                "warning: ignoring --context; it requires an SELinux/SMACK-enabled kernel"
            )
        );
    }

    // An EMPTY name is not "the cwd" (GNU: `mknod '' p` -> ENOENT, probed);
    // resolve() would join "" onto ctx.cwd and report File exists.
    let native = if name.is_empty() {
        String::new()
    } else {
        resolve(ctx, &name).to_string_lossy().into_owned()
    };
    if imp::attributes(&native).is_some() {
        // quotef (mknod.c:257): plain names unquoted
        let _ = writeln!(
            io.stderr,
            "mknod: {}",
            tr!(
                ctx,
                "command.mknod.error.file_exists",
                "{name}: File exists",
                name = quotef_mknod(&name)
            )
        );
        return Ok(1);
    }

    if is_fifo {
        let readonly = has_mode && (mode_value & 0o222) == 0;
        let created = imp::create_fifo(&native, readonly);
        if created != 0 {
            // The create-failure wording follows quotef too (probed:
            // `mknod: nodir/x: No such file or directory` unquoted).
            let _ = writeln!(
                io.stderr,
                "mknod: {}",
                tr!(
                    ctx,
                    "command.mknod.error.cannot_create",
                    "{name}: {reason}",
                    name = quotef_mknod(&name),
                    reason = crate::mkfifo::create_error_text(created)
                )
            );
            return Ok(1);
        }
        return Ok(0);
    }

    // Honest boundary: block/character device nodes have no Windows
    // equivalent (see module head).
    let _ = writeln!(
        io.stderr,
        "mknod: {}",
        tr!(
            ctx,
            "command.mknod.error.device_not_permitted",
            "{name}: Operation not permitted",
            name = quotef_mknod(&name)
        )
    );
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
    fn type_first_char_and_check_order() {
        // probed GNU 9.4: TYPE is classified by its FIRST character, and
        // the operand-count checks precede the TYPE validation.
        let (r, _, err, sc) = run(&["q1", "pz"]);
        assert_eq!(r, 0, "{err}");
        assert_eq!(
            std::fs::read(sc.path("q1")).unwrap(),
            crate::mkfifo::FIFO_MARKER
        );
        let (r, _, err, sc) = run(&["q2", "pp"]);
        assert_eq!(r, 0, "{err}");
        assert!(sc.path("q2").exists());

        // two operands + a bogus type: the count error wins, with the
        // special-files note (probed "missing operand after 'x'").
        let (r, _, err, _) = run(&["x1", "x"]);
        assert_eq!(r, 1);
        assert!(err.contains("missing operand after 'x'"), "{err}");
        assert!(err.contains("Special files require major and minor"));
        assert!(!err.contains("invalid device type"));

        // four operands: invalid TYPE is reported before MAJOR/MINOR.
        let (r, _, err, _) = run(&["x2", "x", "1", "zz"]);
        assert_eq!(r, 1);
        assert!(err.contains("invalid device type 'x'"), "{err}");
        assert!(!err.contains("invalid minor"));

        // fifo branch: count error precedes everything, `pz` still a fifo
        let (r, _, err, _) = run(&["x3", "pz", "1", "3"]);
        assert_eq!(r, 1);
        assert!(err.contains("extra operand '1'"), "{err}");
        assert!(err.contains("Fifos do not have major and minor"));
    }

    #[test]
    fn major_minor_base0_parse() {
        // unit-level: the xstrtoumax base-0 rules
        assert_eq!(parse_device_number("0x1"), Some(1));
        assert_eq!(parse_device_number("01"), Some(1));
        assert_eq!(parse_device_number("12"), Some(12));
        assert_eq!(parse_device_number("+1"), Some(1)); // strtoumax takes '+'
        assert_eq!(parse_device_number(" 1"), Some(1)); // ... and leading space
        assert_eq!(parse_device_number("xyz"), None);
        assert_eq!(parse_device_number("1x"), None);
        assert_eq!(parse_device_number("99999999999999999999"), None);
        // probed GNU 9.4: an empty operand is INVALID, not "missing".
        assert_eq!(parse_device_number(""), None);
    }

    #[test]
    fn empty_device_number_reads_invalid() {
        // probed: `mknod n c '' 3` -> "invalid major device number ''";
        // `mknod n c 1 ''` -> "invalid minor device number ''".
        let (r, _, err, _) = run(&["n", "c", "", "3"]);
        assert_eq!(r, 1);
        assert!(err.contains("invalid major device number ''"), "{err}");
        assert!(!err.contains("missing major"));
        let (r, _, err, _) = run(&["n", "c", "1", ""]);
        assert_eq!(r, 1);
        assert!(err.contains("invalid minor device number ''"), "{err}");
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
        // probed GNU 9.4: --context= (empty present value) warns; bare
        // --context carries no optarg and is silent.
        let (r, _, err, sc) = run(&["--context=", "pe", "p"]);
        assert_eq!(r, 0);
        assert!(err.contains("mknod: warning: ignoring --context"));
        assert!(sc.path("pe").exists());
        let (r, _, err, sc) = run(&["--context", "pb", "p"]);
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(sc.path("pb").exists());
    }

    #[test]
    fn create_failure_follows_quotef() {
        // probed GNU 9.4: `mknod nodir/x p` -> "mknod: nodir/x: No such
        // file or directory" (unquoted); the 1.x body always quoted.
        let (r, _, err, _) = run(&["nodir/x", "p"]);
        assert_eq!(r, 1);
        assert!(
            err.contains("mknod: nodir/x: No such file or directory"),
            "{err}"
        );
        assert!(!err.contains("'nodir/x'"));
        // ... and the EMPTY name is quoted by quotef (probed).
        let (r, _, err, _) = run(&["", "p"]);
        assert_eq!(r, 1);
        assert!(
            err.contains("mknod: '': No such file or directory"),
            "{err}"
        );
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
