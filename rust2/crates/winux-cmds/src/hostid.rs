//! `hostid` — GNU coreutils hostid semantics on the Windows value model.
//!
//! GNU hostid prints a 32-bit identifier for the current host in 8
//! lowercase hex digits (hostid.c: get_machine_id formatted "%08lx"). The
//! 1.x C++ applet derives it from the MachineGuid registry value hashed to
//! 32 bits; this library form keeps the same value model — a stable
//! per-machine 32-bit quantity — sourced from the machine identity the
//! Windows environment carries (COMPUTERNAME, FNV-1a 64 hashed, lower 32
//! bits). The printed value therefore differs from the 1.x exe (different
//! hash input) but is equally stable for a given machine; exit status and
//! shape (8 lowercase hex digits) match.
//!
//! GNU rejects any operand: `hostid: extra operand 'X'` plus the try-help
//! line, rc 1.

use std::ffi::OsString;
use std::io::Write;

use crate::argopt;
use winux_core::{usage_error, CmdResult, Ctx, Io};

/// FNV-1a 64 over the machine name; lower 32 bits are the host id.
fn machine_id_hex(ctx: &Ctx) -> String {
    let name = ctx
        .env
        .get("COMPUTERNAME")
        .map(|v| v.to_string_lossy().into_owned())
        .unwrap_or_default();
    let mut hash: u64 = 0xcbf2_9ce4_8422_2325;
    for b in name.as_bytes() {
        hash ^= u64::from(*b);
        hash = hash.wrapping_mul(0x0000_0100_0000_01b3);
    }
    format!("{:08x}", (hash & 0xFFFF_FFFF) as u32)
}

pub fn hostid_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    // getopt: unknown options are usage errors; --help/--version win
    // wherever they appear (the branded text is the dispatcher's job).
    let opts = [
        argopt::OptSpec {
            short: Some('v'),
            long: "version",
            takes_value: false,
        },
        argopt::OptSpec {
            short: Some('V'),
            long: "version",
            takes_value: false,
        },
    ];
    let p = match argopt::parse("hostid", args, &opts, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if p.has("help") || p.has("version") {
        return Ok(0);
    }
    if !p.positionals.is_empty() {
        let _ = usage_error(
            io,
            "hostid",
            &format!("extra operand '{}'", p.positionals[0].to_string_lossy()),
        );
        return Ok(1);
    }
    writeln!(io.stdout, "{}", machine_id_hex(ctx))?;
    Ok(0)
}

#[cfg(test)]
mod tests {
    use std::ffi::OsString;
    use std::sync::atomic::{AtomicU64, Ordering};

    static SEQ: AtomicU64 = AtomicU64::new(0);

    fn scratch(tag: &str) -> winux_core::test_support::ScratchCtx {
        let n = SEQ.fetch_add(1, Ordering::SeqCst);
        winux_core::test_support::ScratchCtx::new(&format!("{}-{}", tag, n))
    }
    use winux_core::test_support::{MemIo, ScratchCtx};
    use winux_core::Ctx;
    use winux_core::MapEnv;

    fn run(args: &[&str]) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let sc = scratch("hostid");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::hostid_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn prints_eight_hex_digits() {
        let (r, out, _) = run(&[]);
        assert_eq!(r, 0);
        assert_eq!(out.len(), 9); // 8 digits + newline
        assert!(out.ends_with('\n'));
        assert!(out[..8]
            .bytes()
            .all(|b| b.is_ascii_hexdigit() && !b.is_ascii_uppercase()));
    }

    #[test]
    fn stable_per_environment() {
        let (_, a, _) = run(&[]);
        let (_, b, _) = run(&[]);
        assert_eq!(a, b);
    }

    #[test]
    fn extra_operand_is_error() {
        let (r, _, err) = run(&["xyz"]);
        assert_eq!(r, 1);
        assert!(err.contains("hostid: extra operand 'xyz'"));
        assert!(err.contains("Try 'hostid --help' for more information."));
    }

    #[test]
    fn machine_value_follows_env() {
        let mut io = MemIo::new("");
        let sc = scratch("hostid-env");
        let mut env = MapEnv::new();
        env.set("COMPUTERNAME", "box-a");
        let ctx = Ctx::new(sc.dir.clone(), &env);
        let r = super::hostid_cmd(&[], &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        let a = io.out();
        let mut io = MemIo::new("");
        let mut env = MapEnv::new();
        env.set("COMPUTERNAME", "box-b");
        let ctx = Ctx::new(sc.dir.clone(), &env);
        let _ = super::hostid_cmd(&[], &mut io.split(), &ctx).unwrap();
        assert_ne!(a, io.out());
        assert_eq!(a.len(), 9);
    }
}
