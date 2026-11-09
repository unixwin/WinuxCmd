//! `id` — GNU option grammar with the Windows value model of the 1.x C++
//! applet (`src/commands/id.cpp` on main; gate equivalence for
//! platform-inherent output is the C++ Debug exe).
//!
//! * Identity: OpenProcessToken -> TokenUser (uid), TokenPrimaryGroup
//!   (gid, falling back to uid when unresolvable), TokenGroups
//!   (supplementary, user prepended, deduplicated by SID string).
//!   Numeric ids are SID strings; names come from LookupAccountSidW. The
//!   well-known "None" group counts as unresolvable (#1024) and prints as
//!   the bare id, like GNU's nameless gid.
//! * Default format `uid=<id>(<name>) gid=... groups=a,b,c`; only-id /
//!   only-name resolves via format rules; `-n`/`-r` in default format is
//!   `cannot print only names or real IDs in default format` rc 1;
//!   more than one "only" selector rc 1; `--zero` in default format rc 1;
//!   `-Z/--context` reports the no-SELinux condition rc 1 (all GNU probed
//!   rc values). `-a` is an accepted no-op like GNU's compatibility flag.
//! * A USER operand that is neither the current name nor id is
//!   `'X': no such user` rc 1; a matching one re-prints the identity.
//!
//! Values are Windows-specific: value cases gate `win` (Rust rc vs C++
//! rc), the rc-only error cases gate against GNU.

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use crate::winsys;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    // -a is a compatibility no-op (GNU id.c); the long names of the short
    // -a/-z entries are internal canonical keys never spelled with dashes.
    OptSpec {
        short: Some('a'),
        long: "compat-all",
        takes_value: false,
    },
    OptSpec {
        short: Some('g'),
        long: "group",
        takes_value: false,
    },
    OptSpec {
        short: Some('G'),
        long: "groups",
        takes_value: false,
    },
    OptSpec {
        short: Some('n'),
        long: "name",
        takes_value: false,
    },
    OptSpec {
        short: Some('r'),
        long: "real",
        takes_value: false,
    },
    OptSpec {
        short: Some('u'),
        long: "user",
        takes_value: false,
    },
    OptSpec {
        short: Some('Z'),
        long: "context",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "zero",
        takes_value: false,
    },
    OptSpec {
        short: Some('z'),
        long: "zero-short",
        takes_value: false,
    },
];

/// True when the resolved name is usable (non-empty, not the well-known
/// "None" primary group; case-insensitive like the C++ helper).
fn name_resolved(name: &str) -> bool {
    !name.is_empty() && !name.eq_ignore_ascii_case("None")
}

fn fmt_account(id: &str, name: &str, use_name: bool) -> String {
    if use_name && name_resolved(name) {
        name.to_string()
    } else {
        id.to_string()
    }
}

fn fmt_full(id: &str, name: &str) -> String {
    let mut out = id.to_string();
    if name_resolved(name) {
        out.push('(');
        out.push_str(name);
        out.push(')');
    }
    out
}

pub fn id_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("id", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if p.has("help") || p.has("version") {
        return Ok(0);
    }
    let print_group = p.has("group");
    let print_groups = p.has("groups");
    let print_name = p.has("name");
    let print_user = p.has("user");
    let zero = p.has("zero") || p.has("zero-short");

    let only_count = usize::from(print_user) + usize::from(print_group) + usize::from(print_groups);
    let default_format = only_count == 0 && !p.has("context");

    if only_count > 1 {
        // GNU id.c: cannot print "only" of more than one choice.
        let _ = writeln!(
            io.stderr,
            "id: cannot print \"only\" of more than one choice"
        );
        return Ok(1);
    }
    if default_format && (print_real(&p) || print_name) {
        let _ = writeln!(
            io.stderr,
            "id: cannot print only names or real IDs in default format"
        );
        return Ok(1);
    }
    if default_format && zero {
        let _ = writeln!(
            io.stderr,
            "id: option --zero not permitted in default format"
        );
        return Ok(1);
    }
    if p.has("context") {
        let _ = writeln!(
            io.stderr,
            "id: --context (-Z) works only on an SELinux-enabled kernel"
        );
        return Ok(1);
    }

    let Some((user, primary, groups)) = winsys::imp::process_identity() else {
        let _ = writeln!(io.stderr, "id: cannot get process identity");
        return Ok(1);
    };

    let term = if zero { "\0" } else { "\n" };
    let list_delim = if zero { "\0" } else { " " };

    let mut failed = false;
    for operand in &p.positionals {
        let name = operand.to_string_lossy().into_owned();
        if !user.name.is_empty() && name != user.name && name != user.id {
            let _ = writeln!(io.stderr, "id: '{name}': no such user");
            failed = true;
            continue;
        }
        // Matching operand re-prints this process identity (the Windows
        // token is process-wide; GNU would resolve the named user's ids).
        print_identity(
            &p,
            &user,
            &primary,
            &groups,
            default_format,
            print_user,
            print_group,
            print_groups,
            print_name,
            term,
            list_delim,
            io,
        );
    }
    if failed {
        return Ok(1);
    }
    if p.positionals.is_empty() {
        print_identity(
            &p,
            &user,
            &primary,
            &groups,
            default_format,
            print_user,
            print_group,
            print_groups,
            print_name,
            term,
            list_delim,
            io,
        );
    }
    Ok(0)
}

fn print_real(p: &argopt::Parsed) -> bool {
    p.has("real")
}

#[allow(clippy::too_many_arguments)]
fn print_identity(
    p: &argopt::Parsed,
    user: &winsys::imp::Account,
    primary: &winsys::imp::Account,
    groups: &[winsys::imp::Account],
    default_format: bool,
    print_user: bool,
    print_group: bool,
    print_groups: bool,
    print_name: bool,
    term: &str,
    list_delim: &str,
    io: &mut Io,
) {
    let out = &mut *io.stdout;
    let _ = p;
    if default_format {
        let mut line = format!(
            "uid={} gid={}",
            fmt_full(&user.id, &user.name),
            fmt_full(&primary.id, &primary.name)
        );
        line.push_str(" groups=");
        for (i, g) in groups.iter().enumerate() {
            if i > 0 {
                line.push(',');
            }
            line.push_str(&fmt_full(&g.id, &g.name));
        }
        let _ = write!(out, "{line}{term}");
        return;
    }
    if print_user {
        let _ = write!(
            out,
            "{}{term}",
            fmt_account(&user.id, &user.name, print_name)
        );
        return;
    }
    if print_group {
        let _ = write!(
            out,
            "{}{term}",
            fmt_account(&primary.id, &primary.name, print_name)
        );
        return;
    }
    if print_groups {
        for (i, g) in groups.iter().enumerate() {
            if i > 0 {
                let _ = write!(out, "{list_delim}");
            }
            let _ = write!(out, "{}", fmt_account(&g.id, &g.name, print_name));
        }
        let _ = write!(out, "{term}");
    }
}

#[cfg(test)]
mod tests {
    use std::ffi::OsString;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str]) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("id");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::id_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn default_format_shape() {
        let (r, out, _) = run(&[]);
        assert_eq!(r, 0);
        assert!(out.starts_with("uid="), "got {out}");
        assert!(out.contains(" gid="));
        assert!(out.contains(" groups="));
        // The numeric ids are RID-derived (C++ win32_account_id_from_sid
        // model: digits, remapped 0x30000+RID for NT-domain accounts).
        let uid = out.split("uid=").nth(1).unwrap_or("");
        let uid = uid.split(' ').next().unwrap_or("");
        let digits = uid.split('(').next().unwrap_or("");
        assert!(
            !digits.is_empty() && digits.bytes().all(|b| b.is_ascii_digit()),
            "got {out}"
        );
        // resolved names appear as id(name)
        assert!(out.contains('('));
    }

    #[test]
    fn only_selectors() {
        let (r, out, _) = run(&["-u"]);
        assert_eq!(r, 0);
        let v = out.trim_end();
        assert!(
            !v.is_empty() && v.bytes().all(|b| b.is_ascii_digit()),
            "got {out}"
        );
        let (r, out, _) = run(&["-n", "-u"]);
        assert_eq!(r, 0);
        assert!(!out.trim_end().is_empty());
        assert!(!out.trim_end().starts_with("S-1-")); // name, not SID
        let (r, out, _) = run(&["-g"]);
        assert_eq!(r, 0);
        assert!(out.trim_end().bytes().all(|b| b.is_ascii_digit()));
        let (r, out, _) = run(&["-G"]);
        assert_eq!(r, 0);
        assert!(out.contains(' '));
        let (r, _, _) = run(&["-u", "-r"]);
        assert_eq!(r, 0); // --real is accepted with an only selector
        let (r, _, _) = run(&["-a"]);
        assert_eq!(r, 0); // compatibility flag
    }

    #[test]
    fn gnu_error_conditions() {
        let (r, _, err) = run(&["-u", "-g"]);
        assert_eq!(r, 1);
        assert!(err.contains("cannot print \"only\" of more than one choice"));
        let (r, _, err) = run(&["-n"]);
        assert_eq!(r, 1);
        assert!(err.contains("cannot print only names or real IDs in default format"));
        let (r, _, err) = run(&["--zero"]);
        assert_eq!(r, 1);
        assert!(err.contains("option --zero not permitted in default format"));
        let (r, _, err) = run(&["-Z"]);
        assert_eq!(r, 1);
        assert!(err.contains("SELinux"));
        let (r, _, err) = run(&["--bogus"]);
        assert_eq!(r, 1);
    }

    #[test]
    fn zero_and_operands() {
        let (r, out, _) = run(&["-Gn", "--zero"]);
        assert_eq!(r, 0);
        assert!(out.contains('\0'));
        // unknown user: rc 1 + message, stdout empty
        let (r, out, err) = run(&["nosuchuser123"]);
        assert_eq!(r, 1);
        assert!(out.is_empty());
        assert!(err.contains("nosuchuser123"));
        assert!(err.contains("no such user"));
        // unknown user with -u: same
        let (r, _, err) = run(&["-u", "nosuchuser123"]);
        assert_eq!(r, 1);
        assert!(err.contains("no such user"));
        let (r, _, _) = run(&["--help"]);
        assert_eq!(r, 0);
    }
}
