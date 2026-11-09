//! `chown` — GNU coreutils semantics, Windows no-op ownership subset.
//!
//! Spec grammar matches GNU (probed against coreutils 9.4): `[OWNER][:[GROUP]]`
//! where the empty string means "no change" (rc 0); an invalid spec aborts
//! before touching files (`invalid user: 'X'` / `invalid group: ':X'`,
//! status 1); missing files report `cannot access 'X': ...` and continue;
//! missing operand after the spec is a usage error.
//!
//! Windows pilot subset (documented deviation): NTFS ownership cannot be
//! changed from this unprivileged context, so numeric and resolvable specs
//! are validated syntactically and applied as no-ops with status 0. Named
//! owners/groups are NOT resolved against a user database (GNU would fail
//! with `invalid user`); those cases are gate-classified platform-bound.

use std::ffi::OsString;
use std::path::Path;

use crate::argopt::{self, OptSpec};
use crate::fsutil::resolve;
use winux_core::{usage_error, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('R'),
        long: "recursive",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "rec",
        takes_value: false,
    },
    OptSpec {
        short: Some('v'),
        long: "verbose",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "verb",
        takes_value: false,
    },
    OptSpec {
        short: Some('c'),
        long: "changes",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "cha",
        takes_value: false,
    },
    OptSpec {
        short: Some('f'),
        long: "quiet",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "sil",
        takes_value: false,
    },
    OptSpec {
        short: Some('H'),
        long: "dereference-args",
        takes_value: false,
    },
    OptSpec {
        short: Some('L'),
        long: "dereference",
        takes_value: false,
    },
    OptSpec {
        short: Some('P'),
        long: "no-dereference",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "reference",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "from",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "preserve-root",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "no-preserve-root",
        takes_value: false,
    },
];

pub fn chown_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("chown", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if p.has("help") || p.has("version") {
        return Ok(0);
    }
    if p.positionals.is_empty() {
        let _ = usage_error(io, "chown", "missing operand");
        return Ok(1);
    }
    let spec = p.positionals[0].to_string_lossy().into_owned();
    if p.positionals.len() == 1 {
        let msg = format!("missing operand after {}", winux_core::q(&spec));
        let _ = usage_error(io, "chown", &msg);
        return Ok(1);
    }
    // GNU validation order: user first, then group. Named accounts are
    // resolved against the Windows account database (LookupAccountNameW,
    // same API family the 1.x C++ applet uses); unresolvable names are
    // reported exactly like GNU (`invalid user` / `invalid group`).
    let (owner, group) = match split_spec(&spec) {
        Some(pg) => pg,
        None => {
            let msg = format!("invalid user {}", winux_core::q(&spec));
            let _ = usage_error(io, "chown", &msg);
            return Ok(1);
        }
    };
    // GNU rejects a trailing colon with an empty group (`0:`) as invalid spec
    if group.as_deref() == Some("") {
        let msg = format!("invalid spec {}", winux_core::q(&spec));
        let _ = usage_error(io, "chown", &msg);
        return Ok(1);
    }
    if !owner.is_empty() && !is_numeric(&owner) && !account_exists(&owner) {
        let msg = format!("invalid user {}", winux_core::q(&spec));
        let _ = usage_error(io, "chown", &msg);
        return Ok(1);
    }
    if let Some(g) = &group {
        if !g.is_empty() && !is_numeric(g) && !account_exists(g) {
            let msg = format!("invalid group {}", winux_core::q(&format!(":{g}")));
            let _ = usage_error(io, "chown", &msg);
            return Ok(1);
        }
    }

    let recursive = p.has("recursive");
    let verbose = p.has("verbose") || p.has("changes");
    let _ = verbose;
    let _ = recursive;

    let mut failed = false;
    for operand in &p.positionals[1..] {
        let name = operand.to_string_lossy().into_owned();
        let path = resolve(ctx, Path::new(&name));
        if !path.exists() {
            let _ = writeln!(
                io.stderr,
                "chown: cannot access {}: {}",
                winux_core::q(&name),
                "No such file or directory"
            );
            failed = true;
            continue;
        }
        // Windows no-op: ownership change is not representable here.
    }
    Ok(if failed { 1 } else { 0 })
}

/// Resolve an account name against the local Windows account database.
/// Well-known names (`SYSTEM`, `Administrators`, ...) resolve via
/// LookupAccountNameW exactly like the 1.x C++ applet's chown.
#[cfg(windows)]
fn account_exists(name: &str) -> bool {
    #[link(name = "advapi32")]
    extern "system" {
        fn LookupAccountNameW(
            lp_system_name: *const u16,
            lp_account_name: *const u16,
            sid: *mut u8,
            cb_sid: *mut u32,
            referenced_domain_name: *mut u16,
            cch_referenced_domain_name: *mut u32,
            pe_use: *mut i32,
        ) -> i32;
    }
    let wide: Vec<u16> = name.encode_utf16().chain(std::iter::once(0)).collect();
    let mut sid = [0u8; 68];
    let mut sid_len = sid.len() as u32;
    let mut domain = [0u16; 256];
    let mut domain_len = domain.len() as u32;
    let mut use_type: i32 = 0;
    unsafe {
        LookupAccountNameW(
            std::ptr::null(),
            wide.as_ptr(),
            sid.as_mut_ptr(),
            &mut sid_len,
            domain.as_mut_ptr(),
            &mut domain_len,
            &mut use_type,
        ) != 0
    }
}

#[cfg(not(windows))]
fn account_exists(_name: &str) -> bool {
    false
}

/// Split `owner:group`; the whole spec empty means no-change; `:group` is
/// group-only. A spec with more than one colon is invalid (GNU reports it
/// as `invalid user`).
fn split_spec(spec: &str) -> Option<(String, Option<String>)> {
    if spec.is_empty() {
        return Some((String::new(), None));
    }
    match spec.find(':') {
        None => Some((spec.to_string(), None)),
        Some(pos) => {
            let rest = &spec[pos + 1..];
            if rest.contains(':') {
                return None;
            }
            Some((spec[..pos].to_string(), Some(rest.to_string())))
        }
    }
}

fn is_numeric(s: &str) -> bool {
    !s.is_empty() && s.chars().all(|c| c.is_ascii_digit())
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str], sc: &ScratchCtx) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(|s| s.into()).collect();
        let r = chown_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn numeric_and_empty_are_noop_success() {
        let sc = ScratchCtx::new("chown");
        std::fs::write(sc.path("f"), "x").unwrap();
        assert_eq!(run(&["1234:5678", "f"], &sc).0, 0);
        assert_eq!(run(&["", "f"], &sc).0, 0);
        assert_eq!(run(&["0", "f"], &sc).0, 0);
    }

    #[test]
    fn missing_file_and_operand() {
        let sc = ScratchCtx::new("chown2");
        assert_eq!(run(&["Administrators", "missing"], &sc).0, 1);
        assert!(run(&["Administrators", "missing"], &sc)
            .2
            .contains("cannot access"));
        assert_eq!(run(&["Administrators"], &sc).0, 1);
        assert!(run(&["Administrators"], &sc)
            .2
            .contains("missing operand after"));
    }

    #[test]
    fn invalid_group_spec() {
        let sc = ScratchCtx::new("chown3");
        std::fs::write(sc.path("f"), "x").unwrap();
        let (r, _, e) = run(&[":nosuchgroup", "f"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("invalid group"));
    }
}
