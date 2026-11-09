//! `unlink` — GNU coreutils semantics (probed against coreutils 9.4).
//!
//! Removes exactly one file operand. A directory is refused
//! (`cannot unlink 'd': Is a directory` on GNU; the Windows removal of a
//! directory also fails, rc 1 both sides — stderr wording not gated).
//! Zero operands, `--` with nothing after it, or two operands are usage
//! errors rc 1 (`missing operand` / `extra operand 'b'`); unknown options
//! rc 1. A missing file is `cannot unlink 'X': No such file or directory`,
//! rc 1. stdout is always empty, so GNU-side cases gate strictly on rc.

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use crate::fsutil::{reason, resolve};
use winux_core::{usage_error, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[];

pub fn unlink_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("unlink", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if p.has("help") || p.has("version") {
        return Ok(0);
    }
    if p.positionals.is_empty() {
        let _ = usage_error(io, "unlink", "missing operand");
        return Ok(1);
    }
    if p.positionals.len() > 1 {
        let shown = p.positionals[1].to_string_lossy().into_owned();
        let _ = usage_error(io, "unlink", &format!("extra operand '{shown}'"));
        return Ok(1);
    }
    let name = p.positionals[0].to_string_lossy().into_owned();
    match std::fs::remove_file(resolve(ctx, &name)) {
        Ok(()) => Ok(0),
        Err(e) => {
            let _ = writeln!(io.stderr, "unlink: cannot unlink '{name}': {}", reason(&e));
            Ok(1)
        }
    }
}

#[cfg(test)]
mod tests {
    use std::ffi::OsString;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(sc: &ScratchCtx, args: &[&str]) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::unlink_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn removes_a_file() {
        let sc = ScratchCtx::new("unlink");
        std::fs::write(sc.path("f"), "data").unwrap();
        let (r, out, _) = run(&sc, &["f"]);
        assert_eq!(r, 0);
        assert!(out.is_empty());
        assert!(!sc.path("f").exists());
    }

    #[test]
    fn missing_operand_forms() {
        let sc = ScratchCtx::new("unlink2");
        let (r, _, err) = run(&sc, &[]);
        assert_eq!(r, 1);
        assert!(err.contains("missing operand"));
        // `--` alone is still zero operands (GNU probed rc 1)
        let (r, _, _) = run(&sc, &["--"]);
        assert_eq!(r, 1);
        // two operands -> extra operand
        std::fs::write(sc.path("g"), "x").unwrap();
        let (r, _, err) = run(&sc, &["g", "second"]);
        assert_eq!(r, 1);
        assert!(err.contains("extra operand 'second'"));
    }

    #[test]
    fn missing_file_and_directory() {
        let sc = ScratchCtx::new("unlink3");
        let (r, _, err) = run(&sc, &["missing"]);
        assert_eq!(r, 1);
        assert!(err.contains("cannot unlink 'missing'"));
        assert!(err.contains("No such file or directory"));
        std::fs::create_dir(sc.path("d")).unwrap();
        let (r, _, _) = run(&sc, &["d"]);
        assert_eq!(r, 1);
    }

    #[test]
    fn dashdash_and_options() {
        let sc = ScratchCtx::new("unlink4");
        std::fs::write(sc.path("f"), "x").unwrap();
        let (r, _, _) = run(&sc, &["--", "f"]);
        assert_eq!(r, 0);
        let (r, _, _) = run(&sc, &["--bogus"]);
        assert_eq!(r, 1);
        let (r, _, _) = run(&sc, &["--help"]);
        assert_eq!(r, 0);
    }
}
