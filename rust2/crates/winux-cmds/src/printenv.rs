//! `printenv` — GNU coreutils semantics (probed against coreutils 9.4).
//!
//! With NAMEs: print each found variable's value followed by a newline
//! (empty value -> bare newline); exit 0 only if ALL were found.
//! Without: dump the whole environ in `environ[]` order, exit 0.
//! Usage errors exit 2 (GNU printenv uses EXIT_TROUBLE here).

use std::ffi::OsString;

use winux_core::{usage_error, CmdResult, Ctx, Io};

pub fn printenv_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    // printenv takes no options in GNU beyond --help/--version; anything
    // else that looks like an option is a usage error (rc 2).
    let mut names: Vec<String> = Vec::new();
    let mut no_more = false;
    for a in args {
        let s = a.to_string_lossy().into_owned();
        if no_more {
            names.push(s);
            continue;
        }
        if s == "--" {
            no_more = true;
            continue;
        }
        if s == "--help" || s == "--version" {
            return Ok(0);
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

    let out = &mut *io.stdout;
    if names.is_empty() {
        for (_k, v) in ctx.env.entries() {
            out.write_all(v.to_string_lossy().as_bytes())?;
            out.write_all(b"\n")?;
        }
        return Ok(0);
    }
    let mut all_found = true;
    for name in &names {
        match ctx.env.get(name) {
            Some(v) => {
                out.write_all(v.to_string_lossy().as_bytes())?;
                out.write_all(b"\n")?;
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
}
