//! `printenv` — GNU coreutils semantics (probed against coreutils 9.4).
//!
//! GNU printenv.c parses `getopt_long(argc, argv, "+0", longopts)`: the
//! leading '+' ends option processing at the first non-option argument.
//! Only the leading option run sees `-0`/`--null` (NUL instead of newline
//! after each printed value) and help/version; everything after the first
//! NAME is a variable name, even when it looks like an option
//! (`printenv HOME -0` looks up a variable literally named `-0`).
//! With NAMEs: print each found variable's value followed by the
//! separator (empty value -> bare separator); exit 0 only if ALL were
//! found. Without NAMEs: dump the whole environ in `environ[]` order.
//! Usage errors exit 2 (GNU printenv uses EXIT_TROUBLE here).

use std::ffi::OsString;

use winux_core::{usage_error, CmdResult, Ctx, Io};

pub fn printenv_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut null_separator = false;
    let mut names: Vec<String> = Vec::new();
    let mut no_more = false;
    for a in args {
        let s = a.to_string_lossy().into_owned();
        if no_more || !names.is_empty() {
            // getopt("+0") stopped: after "--" or the first NAME every
            // token is a variable name.
            names.push(s);
            continue;
        }
        if s == "--" {
            no_more = true;
            continue;
        }
        if s == "--help" || s == "--version" {
            // Wording-bound: the product's dispatcher prints the branded
            // help/version for an option-position --help/--version before
            // this handler runs; the exit status is what gates.
            return Ok(0);
        }
        if s == "-0" || s == "--null" {
            null_separator = true;
            continue;
        }
        if s.starts_with('-') && s.len() > 1 {
            let msg = if let Some(rest) = s.strip_prefix("--") {
                format!("unrecognized option '--{rest}'")
            } else {
                let c = s.chars().nth(1).unwrap();
                format!("invalid option -- '{c}'")
            };
            let _ = usage_error(io, "printenv", &msg);
            return Ok(2);
        }
        names.push(s);
    }

    let sep: &[u8] = if null_separator { b"\0" } else { b"\n" };
    let out = &mut *io.stdout;
    if names.is_empty() {
        for (_k, v) in ctx.env.entries() {
            out.write_all(v.to_string_lossy().as_bytes())?;
            out.write_all(sep)?;
        }
        return Ok(0);
    }
    let mut all_found = true;
    for name in &names {
        match ctx.env.get(name) {
            Some(v) => {
                out.write_all(v.to_string_lossy().as_bytes())?;
                out.write_all(sep)?;
            }
            None => all_found = false,
        }
    }
    Ok(if all_found { 0 } else { 1 })
}

#[cfg(test)]
mod tests {
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn found_and_missing() {
        let mut io = MemIo::new("");
        let mut sc = ScratchCtx::new("printenv");
        sc.env.set("GFoo", "bar");
        let ctx = sc.ctx();
        let args = ["GFoo".into()];
        let r = super::printenv_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert_eq!(io.out(), "bar\n");

        let mut io = MemIo::new("");
        let args = ["GFoo".into(), "NOPE".into()];
        let r = super::printenv_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert_eq!(io.out(), "bar\n");
    }

    #[test]
    fn empty_value_prints_newline() {
        let mut io = MemIo::new("");
        let mut sc = ScratchCtx::new("printenv2");
        sc.env.set("GEmpty", "");
        let ctx = sc.ctx();
        let args = ["GEmpty".into()];
        let r = super::printenv_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert_eq!(io.out(), "\n");
    }

    #[test]
    fn no_args_dumps_env_in_order() {
        let mut io = MemIo::new("");
        let mut sc = ScratchCtx::new("printenv3");
        sc.env.set("AAA", "1");
        sc.env.set("BBB", "2");
        let ctx = sc.ctx();
        let r = super::printenv_cmd(&[], &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert_eq!(io.out(), "1\n2\n");
    }

    #[test]
    fn unknown_option_is_rc2() {
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("printenv4");
        let ctx = sc.ctx();
        let args = ["--bogus".into()];
        let r = super::printenv_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 2);
    }

    #[test]
    fn null_option_terminates_with_nul() {
        // GNU printenv.c "-0" optstring entry (probed 9.4): the separator
        // after each printed value is NUL.
        let mut io = MemIo::new("");
        let mut sc = ScratchCtx::new("printenv5");
        sc.env.set("GVar", "hello");
        let ctx = sc.ctx();
        let args = ["-0".into(), "GVar".into()];
        let r = super::printenv_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert_eq!(io.out(), "hello\0");

        let mut io = MemIo::new("");
        let args = ["--null".into(), "GVar".into()];
        let r = super::printenv_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert_eq!(io.out(), "hello\0");

        // full dump is NUL-separated too
        let mut io = MemIo::new("");
        let mut sc = ScratchCtx::new("printenv6");
        sc.env.set("A1", "1");
        sc.env.set("B2", "2");
        let ctx = sc.ctx();
        let args = ["-0".into()];
        let r = super::printenv_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert_eq!(io.out(), "1\0".to_string() + "2\0");
    }

    #[test]
    fn tokens_after_first_name_are_names() {
        // getopt("+0") stops at the first non-option argument (probed 9.4):
        // `printenv GVar -0` looks up a variable named "-0" (rc 1), and
        // `printenv GVar --help` is a lookup too, not help.
        let mut io = MemIo::new("");
        let mut sc = ScratchCtx::new("printenv7");
        sc.env.set("GVar", "hello");
        let ctx = sc.ctx();
        let args = ["GVar".into(), "-0".into()];
        let r = super::printenv_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert_eq!(io.out(), "hello\n");

        let mut io = MemIo::new("");
        let args = ["GVar".into(), "--help".into()];
        let r = super::printenv_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert_eq!(io.out(), "hello\n");
    }
}
