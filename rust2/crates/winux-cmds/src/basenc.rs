//! `base64` / `base32` — GNU coreutils basenc semantics (probed against
//! coreutils 9.4 under WSL; see crates/winux-cmds/src/b64core.rs for the
//! codec and its probe vectors).
//!
//! * `-d/--decode` decodes stdin or the single FILE operand (`-` = stdin);
//!   a second operand is `extra operand 'X'`, rc 1; a missing file reports
//!   `X: No such file or directory`, rc 1.
//! * `-w N/--wrap=N` sets the encode line width (default 76; 0 = no
//!   wrapping and no trailing newline; overflow -> 0; junk/negative ->
//!   `invalid wrap size: 'X'`, rc 1).
//! * `-i/--ignore-garbage` skips non-alphabet bytes while decoding, but
//!   never skips `=` (all probed GNU quirks live in b64core::decode).
//! * Decode errors print the bytes decoded before the error (GNU keeps the
//!   prefix) and exit 1; the diagnostic itself is stderr and not gated.

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use crate::b64core;
use crate::fsutil::resolve;
use crate::gnuhelp;
use winux_core::{usage_error, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('d'),
        long: "decode",
        takes_value: false,
    },
    OptSpec {
        short: Some('i'),
        long: "ignore-garbage",
        takes_value: false,
    },
    OptSpec {
        short: Some('w'),
        long: "wrap",
        takes_value: true,
    },
];

// `--help` body verbatim from `env base64 --help` under WSL coreutils 9.4,
// minus the shared gnuhelp::footer tail.
const BASE64_HELP_BODY: &str = r#"Usage: base64 [OPTION]... [FILE]
Base64 encode or decode FILE, or standard input, to standard output.

With no FILE, or when FILE is -, read standard input.

Mandatory arguments to long options are mandatory for short options too.
  -d, --decode          decode data
  -i, --ignore-garbage  when decoding, ignore non-alphabet characters
  -w, --wrap=COLS       wrap encoded lines after COLS character (default 76).
                          Use 0 to disable line wrapping
      --help        display this help and exit
      --version     output version information and exit

The data are encoded as described for the base64 alphabet in RFC 4648.
When decoding, the input may contain newlines in addition to the bytes of
the formal base64 alphabet.  Use --ignore-garbage to attempt to recover
from any other non-alphabet bytes in the encoded stream.

"#;

// `--help` body verbatim from `env base32 --help` under WSL coreutils 9.4,
// minus the shared gnuhelp::footer tail.
const BASE32_HELP_BODY: &str = r#"Usage: base32 [OPTION]... [FILE]
Base32 encode or decode FILE, or standard input, to standard output.

With no FILE, or when FILE is -, read standard input.

Mandatory arguments to long options are mandatory for short options too.
  -d, --decode          decode data
  -i, --ignore-garbage  when decoding, ignore non-alphabet characters
  -w, --wrap=COLS       wrap encoded lines after COLS character (default 76).
                          Use 0 to disable line wrapping
      --help        display this help and exit
      --version     output version information and exit

The data are encoded as described for the base32 alphabet in RFC 4648.
When decoding, the input may contain newlines in addition to the bytes of
the formal base32 alphabet.  Use --ignore-garbage to attempt to recover
from any other non-alphabet bytes in the encoded stream.

"#;

fn run(name: &str, codec: &b64core::Codec, args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse(name, args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    // GNU exits at the first --help/--version getopt_long encounters; the
    // ordered find reproduces that for `--version --help` (version wins).
    if let Some(which) = p
        .flags
        .iter()
        .find(|f| f.as_str() == argopt::HELP || f.as_str() == argopt::VERSION)
    {
        return Ok(match which.as_str() {
            "help" => {
                let body = if name == "base64" {
                    BASE64_HELP_BODY
                } else {
                    BASE32_HELP_BODY
                };
                write!(
                    io.stdout,
                    "{}{}",
                    body,
                    gnuhelp::footer(name, &format!("{name} invocation"))
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version(name, "Simon Josefsson")
                )?;
                0
            }
        });
    }
    if p.positionals.len() > 1 {
        let shown = p.positionals[1].to_string_lossy().into_owned();
        let _ = usage_error(io, name, &format!("extra operand '{}'", shown));
        return Ok(1);
    }

    // Read the input bytes: the single operand (or stdin when absent or "-").
    let mut input = Vec::new();
    let operand: Option<&OsString> = p.positionals.first();
    match operand {
        None => {
            std::io::copy(&mut io.stdin, &mut input)?;
        }
        Some(op) => {
            let shown = op.to_string_lossy().into_owned();
            if shown == "-" {
                std::io::copy(&mut io.stdin, &mut input)?;
            } else {
                match std::fs::File::open(resolve(ctx, &shown)) {
                    Ok(mut f) => {
                        std::io::copy(&mut f, &mut input)?;
                    }
                    Err(e) => {
                        let reason = crate::fsutil::reason(&e);
                        let _ = writeln!(io.stderr, "{name}: {shown}: {reason}");
                        return Ok(1);
                    }
                }
            }
        }
    }

    let out = &mut *io.stdout;
    if p.has("decode") {
        let ignore = p.has("ignore-garbage");
        let bytes = b64core::decode(codec, &input, ignore);
        match bytes {
            Ok(b) => {
                out.write_all(&b)?;
                Ok(0)
            }
            Err(b) => {
                out.write_all(&b)?;
                let _ = writeln!(io.stderr, "{name}: invalid input");
                Ok(1)
            }
        }
    } else {
        let wrap = match p.value_of("wrap") {
            Some(raw) => match b64core::parse_wrap(raw) {
                Ok(w) => w,
                Err(()) => {
                    let msg = format!("invalid wrap size: '{}'", raw);
                    let _ = usage_error(io, name, &msg);
                    return Ok(1);
                }
            },
            None => 76,
        };
        let enc = b64core::encode(codec, &input, wrap);
        out.write_all(&enc)?;
        Ok(0)
    }
}

pub fn base64_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    run("base64", &b64core::base64_codec(), args, io, ctx)
}

pub fn base32_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    run("base32", &b64core::base32_codec(), args, io, ctx)
}

#[cfg(test)]
mod tests {
    // [GNU] `--help`/`--version` print the probed coreutils text, rc 0
    // (strict rows b64a28/b64a29, b32a23/b32a24).
    #[test]
    fn gnu_help_version_surface() {
        for prog in ["base64", "base32"] {
            let mut io = MemIo::new("");
            let sc = ScratchCtx::new("basenc-hv");
            let ctx = sc.ctx();
            let args: Vec<OsString> = vec!["--help".into()];
            let r = if prog == "base64" {
                super::base64_cmd(&args, &mut io.split(), &ctx).unwrap()
            } else {
                super::base32_cmd(&args, &mut io.split(), &ctx).unwrap()
            };
            assert_eq!(r, 0);
            assert!(io
                .out()
                .starts_with(&format!("Usage: {prog} [OPTION]... [FILE]\n")));
            assert!(io.out().contains("RFC 4648"));
            assert!(io
                .out()
                .ends_with(&format!("info '(coreutils) {prog} invocation'\n")));
            let mut io = MemIo::new("");
            let args: Vec<OsString> = vec!["--version".into()];
            let r = if prog == "base64" {
                super::base64_cmd(&args, &mut io.split(), &ctx).unwrap()
            } else {
                super::base32_cmd(&args, &mut io.split(), &ctx).unwrap()
            };
            assert_eq!(r, 0);
            assert!(io
                .out()
                .starts_with(&format!("{prog} (GNU coreutils) 9.4\n")));
            assert!(io.out().ends_with("Written by Simon Josefsson.\n"));
        }
    }

    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run64(args: &[&str], stdin: &str) -> (u32, String, String) {
        let mut io = MemIo::new(stdin);
        let sc = ScratchCtx::new("basenc");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::base64_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    use std::ffi::OsString;

    #[test]
    fn encode_default_wrap_and_rc() {
        let (r, out, _) = run64(&[], "foobar");
        assert_eq!(r, 0);
        assert_eq!(out, "Zm9vYmFy\n");
        let (r, out, _) = run64(&["-w", "2"], "foobar");
        assert_eq!(r, 0);
        assert_eq!(out, "Zm\n9v\nYm\nFy\n");
        let (r, out, _) = run64(&["-w", "0"], "foobar");
        assert_eq!(r, 0);
        assert_eq!(out, "Zm9vYmFy");
    }

    #[test]
    fn decode_ok_and_partial_prefix() {
        let (r, out, _) = run64(&["-d"], "Zm9vYmFy");
        assert_eq!(r, 0);
        assert_eq!(out, "foobar");
        // GNU keeps the decoded prefix before "invalid input" and exits 1.
        let (r, out, err) = run64(&["-d"], "Zm9vYmF");
        assert_eq!(r, 1);
        assert_eq!(out, "fooba");
        assert!(err.contains("invalid input"));
        // ignore-garbage
        let (r, out, _) = run64(&["-d", "-i"], "Z***g==");
        assert_eq!(r, 0);
        assert_eq!(out, "f");
    }

    #[test]
    fn wrap_size_errors() {
        let (r, _, err) = run64(&["-w", "bad"], "foobar");
        assert_eq!(r, 1);
        assert!(err.contains("invalid wrap size"));
        let (r, _, _) = run64(&["-w", "-1"], "foobar");
        assert_eq!(r, 1);
        // overflow -> wrap disabled, rc 0, no trailing newline
        let (r, out, _) = run64(&["-w", "99999999999999999999"], "foobar");
        assert_eq!(r, 0);
        assert_eq!(out, "Zm9vYmFy");
    }

    #[test]
    fn operand_rules() {
        let (r, _, _) = run64(&["a", "b"], "");
        assert_eq!(r, 1); // extra operand
        let (r, _, err) = run64(&["nosuchfile"], "");
        assert_eq!(r, 1);
        assert!(err.contains("No such file or directory"));
        let (r, out, _) = run64(&["-d"], "Zg==");
        assert_eq!(r, 0);
        assert_eq!(out, "f");
    }

    #[test]
    fn base32_command() {
        let mut io = MemIo::new("foobar");
        let sc = ScratchCtx::new("basenc32");
        let ctx = sc.ctx();
        let r = super::base32_cmd(&[], &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert_eq!(io.out(), "MZXW6YTBOI======\n");

        let mut io = MemIo::new("MZXW6YTB");
        let ctx = sc.ctx();
        let r = super::base32_cmd(&["-d".into()], &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert_eq!(io.out(), "fooba"); // canonical 8-char form, no pads
    }
}
