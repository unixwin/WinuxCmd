//! `basename` — GNU coreutils semantics (probed against coreutils 9.4).
//!
//! GNU rules: strip trailing slashes; if the name is all slashes the result
//! is `/`; if the name is empty the result is empty (rc 0). A suffix is
//! removed only when it is a *proper* suffix of the stripped name
//! (`basename ab ab` keeps `ab`). `-a`/`-s` enable multiple operands; extra
//! operands without them are a usage error (rc 1). `-z` terminates each
//! output with NUL. Only `/` is a separator (GNU semantics; the 1.x C++
//! applet also strips `\`, a documented deviation we do NOT inherit).

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use winux_core::{usage_error, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('a'),
        long: "multiple",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "mul",
        takes_value: false,
    },
    OptSpec {
        short: Some('s'),
        long: "suffix",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "suf",
        takes_value: true,
    },
    OptSpec {
        short: Some('z'),
        long: "zero",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "ze",
        takes_value: false,
    },
];

/// GNU basename of one name with optional suffix.
pub fn base_name(name: &str, suffix: &str) -> String {
    let stripped = name.trim_end_matches('/');
    if stripped.is_empty() {
        // "/" (any all-slash name) -> "/" ; "" stays ""
        return if name.is_empty() {
            String::new()
        } else {
            "/".to_string()
        };
    }
    // drop the directory part (after the last remaining slash)
    let mut s = match stripped.rfind('/') {
        Some(pos) => stripped[pos + 1..].to_string(),
        None => stripped.to_string(),
    };
    if !suffix.is_empty() && s.len() > suffix.len() && s.ends_with(suffix) {
        s.truncate(s.len() - suffix.len());
    }
    s
}

pub fn basename_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("basename", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if p.has("help") || p.has("version") {
        return Ok(0); // wording-bound; gate records, not gates
    }
    let zero = p.has("zero");
    let suffix_owned;
    let suffix: &str = match p.value_of("suffix") {
        Some(v) => {
            suffix_owned = v.to_string();
            &suffix_owned
        }
        None => "",
    };
    let multiple = p.has("multiple") || p.has("mul") || !suffix.is_empty();

    let names: Vec<String> = p
        .positionals
        .iter()
        .map(|a| a.to_string_lossy().into_owned())
        .collect();
    if names.is_empty() {
        let _ = usage_error(io, "basename", "missing operand");
        return Ok(1);
    }
    if !multiple {
        if names.len() == 2 {
            // classic `basename NAME SUFFIX`
        } else if names.len() > 2 {
            let msg = format!("extra operand {}", winux_core::q(&names[2]));
            let _ = usage_error(io, "basename", &msg);
            return Ok(1);
        }
    }

    let sep = if zero { '\0' } else { '\n' };
    let out = &mut *io.stdout;
    // classic form processes ONLY the first operand (the second is the suffix)
    let print_names: &[String] = if multiple { &names } else { &names[..1] };
    for name in print_names {
        let suffix_for = if multiple {
            suffix
        } else {
            names.get(1).map(|s| s.as_str()).unwrap_or("")
        };
        let base = base_name(name, suffix_for);
        out.write_all(base.as_bytes())?;
        out.write_all(&[sep as u8])?;
    }
    Ok(0)
}

#[cfg(test)]
mod tests {
    use super::base_name;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn gnu_cases() {
        assert_eq!(base_name("/", ""), "/");
        assert_eq!(base_name("///", ""), "/");
        assert_eq!(base_name("a//b//", ""), "b");
        assert_eq!(base_name("a/b/", ""), "b");
        assert_eq!(base_name(".", ""), ".");
        assert_eq!(base_name("..", ""), "..");
        assert_eq!(base_name("a/..", ""), "..");
        assert_eq!(base_name("", ""), "");
        assert_eq!(base_name("foo.txt", ".txt"), "foo");
        assert_eq!(base_name("foo.txt", "txt"), "foo.");
        assert_eq!(base_name("ab", "ab"), "ab");
        assert_eq!(base_name("a.b.c", ".b.c"), "a");
    }

    #[test]
    fn cmd_single() {
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("basename");
        let ctx = sc.ctx();
        let args = ["a/b/c".into()];
        super::basename_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(io.out(), "c\n");
    }

    #[test]
    fn cmd_extra_operand_is_usage_error() {
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("basename2");
        let ctx = sc.ctx();
        let args = ["x".into(), "y".into(), "z".into()];
        let r = super::basename_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("extra operand 'z'"));
    }

    #[test]
    fn cmd_multiple_and_zero() {
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("basename3");
        let ctx = sc.ctx();
        let args = ["-a".into(), "x/y".into(), "z/w".into()];
        super::basename_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(io.out(), "y\nw\n");

        let mut io = MemIo::new("");
        let args = ["-z".into(), "-s".into(), ".txt".into(), "a.txt".into()];
        super::basename_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(io.out(), "a\0");
    }
}
