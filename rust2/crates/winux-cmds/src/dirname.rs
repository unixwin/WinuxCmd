//! `dirname` — GNU coreutils semantics (probed against coreutils 9.4).
//!
//! `dirname /` `//` `///` -> `/`; `a` -> `.`; empty -> `.`; trailing slashes
//! are stripped first, then the last component. GNU 9.4 supports multiple
//! operands; `-z` NUL-terminates.

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use winux_core::{tr, usage_error, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[OptSpec {
    short: Some('z'),
    long: "zero",
    takes_value: false,
}];

/// GNU dirname of one path.
pub fn dir_name(path: &str) -> String {
    if path.is_empty() {
        return ".".to_string();
    }
    let s = path.trim_end_matches('/');
    if s.is_empty() {
        return "/".to_string(); // all slashes
    }
    match s.rfind('/') {
        None => ".".to_string(),
        Some(pos) => {
            let p = s[..=pos].trim_end_matches('/');
            if p.is_empty() {
                "/".to_string()
            } else {
                p.to_string()
            }
        }
    }
}

pub fn dirname_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("dirname", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if p.has("help") || p.has("version") {
        return Ok(0);
    }
    let zero = p.has("zero");
    if p.positionals.is_empty() {
        // [GNU] dirname.c: usage (EXIT_FAILURE) with "missing operand".
        let _ = usage_error(
            io,
            "dirname",
            &tr!(
                ctx,
                "command.dirname.error.missing_operand",
                "missing operand"
            ),
        );
        return Ok(1);
    }
    let sep = if zero { '\0' } else { '\n' };
    let out = &mut *io.stdout;
    for name in &p.positionals {
        let d = dir_name(&name.to_string_lossy());
        out.write_all(d.as_bytes())?;
        out.write_all(&[sep as u8])?;
    }
    Ok(0)
}

#[cfg(test)]
mod tests {
    use super::dir_name;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn gnu_cases() {
        assert_eq!(dir_name("/"), "/");
        assert_eq!(dir_name("//"), "/");
        assert_eq!(dir_name("///"), "/");
        assert_eq!(dir_name("a"), ".");
        assert_eq!(dir_name("a/b"), "a");
        assert_eq!(dir_name("a//b"), "a");
        assert_eq!(dir_name("a/b/"), "a");
        assert_eq!(dir_name("a/b//"), "a");
        assert_eq!(dir_name("."), ".");
        assert_eq!(dir_name(".."), ".");
        assert_eq!(dir_name("/a"), "/");
        assert_eq!(dir_name(""), ".");
        assert_eq!(dir_name("a/b/c"), "a/b");
    }

    #[test]
    fn cmd_multi() {
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("dirname");
        let ctx = sc.ctx();
        let args = ["a/b".into(), "c".into()];
        super::dirname_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(io.out(), "a\n.\n");
    }

    #[test]
    fn cmd_missing_operand() {
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("dirname2");
        let ctx = sc.ctx();
        let r = super::dirname_cmd(&[], &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("missing operand"));
    }

    #[test]
    fn catalog_injection_translates_missing_operand() {
        use winux_core::host::MapHost;
        use winux_core::{Ctx, MapEnv};

        let sc = ScratchCtx::new("dirname-i18n");
        let mut host = MapHost::default();
        host.messages.insert(
            "command.dirname.error.missing_operand".to_string(),
            "缺少操作数".to_string(),
        );
        let env = MapEnv::new();
        let ctx = Ctx::with_host(sc.dir.clone(), &env, &host);
        let mut io = MemIo::new("");
        let r = super::dirname_cmd(&[], &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("缺少操作数"), "got {}", io.err());
    }
}
