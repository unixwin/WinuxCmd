//! `hostname` — aligned with the 1.x C++ applet on main
//! (`src/commands/hostname.cpp`, incl. the #1526 `-F/--file` behavior).
//!
//! * No operand: print the system name (GetComputerNameW) rc 0.
//! * One operand SETS the name on GNU; Windows cannot (an administrative,
//!   reboot-scoped API), so it reports
//!   `cannot set name to 'X'` rc 1 like the C++ applet. Two operands are
//!   `extra operand 'X'` rc 1 (GNU hostname.c:91 probed).
//! * The net-tools option set (-b -i -I -s -f -a -A -d -y -n and their
//!   long forms) is REJECTED the way coreutils hostname rejects it
//!   (`unknown option -- s` / `unrecognized option '--short'` + try-help,
//!   rc 1). The WSL `hostname` binary is Debian's and ACCEPTS these, so
//!   those cases gate `win` (Rust rc vs C++ rc) — recorded, not GNU-gated.
//! * `-F/--file FILE` (#1526): read the file (wide paths fine), take the
//!   first line that is neither empty nor a `#` comment, trim ASCII
//!   whitespace, validate with RFC 1035 check_name (alnum first/last,
//!   only alnum/-/. inside, no `..` / `.-` / `-.` pairs), reject invalid
//!   (`the specified hostname is invalid`) and >64 bytes (`name too
//!   long`) with rc 1, a failed open reports the reason rc 1 — and on
//!   success complete with rc 0 WITHOUT changing the host (the documented
//!   Windows [DIFFERS] of the C++ applet). An operand together with -F is
//!   usage-on-stderr + exit 255 (GNU hostname.c main order).
//!
//! Gate classification follows the C++ side; rc-only cases (missing file,
//! invalid name, extra operand) match GNU and gate strictly.

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use crate::fsutil::resolve;
use crate::winsys;
use winux_core::{usage_error, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('F'),
        long: "file",
        takes_value: true,
    },
    // The net-tools set, rejected like coreutils hostname does.
    OptSpec {
        short: Some('b'),
        long: "boot",
        takes_value: false,
    },
    OptSpec {
        short: Some('i'),
        long: "ip-address",
        takes_value: false,
    },
    OptSpec {
        short: Some('I'),
        long: "all-ip-addresses",
        takes_value: false,
    },
    OptSpec {
        short: Some('s'),
        long: "short",
        takes_value: false,
    },
    OptSpec {
        short: Some('f'),
        long: "fqdn",
        takes_value: false,
    },
    OptSpec {
        short: Some('a'),
        long: "alias",
        takes_value: false,
    },
    OptSpec {
        short: Some('A'),
        long: "all-fqdns",
        takes_value: false,
    },
    OptSpec {
        short: Some('d'),
        long: "domain",
        takes_value: false,
    },
    OptSpec {
        short: Some('y'),
        long: "yp",
        takes_value: false,
    },
    OptSpec {
        short: Some('n'),
        long: "node",
        takes_value: false,
    },
];

/// Net-tools short options the C++ applet rejects with the short form.
const REJECTED_SHORT_FORM: &[char] = &['b', 'i', 'I', 's', 'f', 'a', 'A', 'd', 'y', 'n'];

/// [GNU] hostname.c usage(stderr) body (verbatim, Debian wording), exit
/// -1 -> 255 — printed when an operand is combined with -F.
const DEBIAN_USAGE: &str = "Usage: hostname [-b] {hostname|-F file}         set host name (from file)\n       hostname [-a|-A|-d|-f|-i|-I|-s|-y]       display formatted name\n       hostname                                 display host name\n\n       {yp,nis,}domainname {nisdomain|-F file}  set NIS domain name (from file)\n       {yp,nis,}domainname                      display NIS domain name\n\n       dnsdomainname                            display dns domain name\n\n       hostname -V|--version|-h|--help          print info and exit\n\nProgram name:\n       {yp,nis,}domainname=hostname -y\n       dnsdomainname=hostname -d\n\nProgram options:\n    -a, --alias            alias names\n    -A, --all-fqdns        all long host names (FQDNs)\n    -b, --boot             set default hostname if none available\n    -d, --domain           DNS domain name\n    -f, --fqdn, --long     long host name (FQDN)\n    -F, --file             read host name or NIS domain name from given file\n    -i, --ip-address       addresses for the host name\n    -I, --all-ip-addresses all addresses for the host\n    -s, --short            short host name\n    -y, --yp, --nis        NIS/YP domain name\n\nDescription:\n   This command can get or set the host name or the NIS domain name. You can\n   also get the DNS domain or the FQDN (fully qualified domain name).\n   Unless you are using bind or NIS for host lookups you can change the\n   FQDN (Fully Qualified Domain Name) and the DNS domain name (which is\n   part of the FQDN) in the /etc/hosts file.\n";

/// [GNU] hostname.c check_name(): RFC 1035 2.3.1 — alphanumeric first and
/// last, only alnum/'-'/'.' inside, no '.'/'-' adjacency pairs.
fn check_name(name: &str) -> bool {
    let bytes = name.as_bytes();
    let len = bytes.len();
    if len == 0 || !is_host_char(bytes[0]) || !is_host_char(bytes[len - 1]) {
        return false;
    }
    for (i, &c) in bytes.iter().enumerate() {
        if !is_host_char(c) && c != b'-' && c != b'.' {
            return false;
        }
        if c == b'-' && ((i > 0 && bytes[i - 1] == b'.') || (i + 1 < len && bytes[i + 1] == b'.')) {
            return false;
        }
        if c == b'.' && i > 0 && bytes[i - 1] == b'.' {
            return false;
        }
    }
    true
}

fn is_host_char(c: u8) -> bool {
    c.is_ascii_alphanumeric()
}

fn is_name_space(c: u8) -> bool {
    matches!(c, b' ' | b'\t' | b'\n' | 0x0b | 0x0c | b'\r')
}

pub fn hostname_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("hostname", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if p.has("help") || p.has("version") {
        return Ok(0);
    }

    // Reject the net-tools option set like the C++ applet (coreutils
    // hostname wording; rc 1). This mirrors build_config's unsupported[]
    // scan on main. Values consumed by -F/--file are skipped, exactly as
    // the C++ option table consumes them before the scan runs.
    let mut skip_next = false;
    for arg in args.iter() {
        let s = arg.to_string_lossy().into_owned();
        if skip_next {
            skip_next = false;
            continue;
        }
        if s == "--" {
            break;
        }
        if let Some(rest) = s.strip_prefix("--") {
            let (name, inline_val) = match rest.split_once('=') {
                Some((n, v)) => (n, Some(v)),
                None => (rest, None),
            };
            if name == "file" {
                if inline_val.is_none() {
                    skip_next = true;
                }
                continue;
            }
            if OPTS[1..].iter().any(|o| o.long == name) {
                let msg = format!("unrecognized option '--{name}'");
                let _ = usage_error(io, "hostname", &msg);
                return Ok(1);
            }
        } else if s.len() > 1 && s.starts_with('-') {
            let body = &s[1..];
            match body.find('F') {
                Some(fpos) => {
                    // only the chars before the F are scanned for rejection
                    for c in body[..fpos].chars() {
                        if REJECTED_SHORT_FORM.contains(&c) {
                            let msg = format!("unknown option -- {c}");
                            let _ = usage_error(io, "hostname", &msg);
                            return Ok(1);
                        }
                    }
                    // -FFILE has an inline value; bare -F consumes the next arg
                    if body.ends_with('F') {
                        skip_next = true;
                    }
                }
                None => {
                    for c in body.chars() {
                        if REJECTED_SHORT_FORM.contains(&c) {
                            let msg = format!("unknown option -- {c}");
                            let _ = usage_error(io, "hostname", &msg);
                            return Ok(1);
                        }
                    }
                }
            }
        }
    }

    let file = p.value_of("file").map(|f| f.to_string());

    if let Some(fname) = file {
        // [GNU] main(): the operand conflict is decided BEFORE the file is
        // validated, but AFTER the open (hostname.c reads the file, then
        // checks the operand, then validates the name) — the C++ applet
        // models open-first then operand-then-name; keep that order.
        let path = resolve(ctx, &fname);
        let content = match std::fs::read(&path) {
            Ok(c) => c,
            Err(e) => {
                let reason = crate::fsutil::reason(&e);
                let _ = writeln!(io.stderr, "hostname: {reason}");
                return Ok(1);
            }
        };
        // Operand + -F: usage(stderr), exit -1 -> 255.
        if !p.positionals.is_empty() {
            let _ = write!(io.stderr, "{DEBIAN_USAGE}");
            return Ok(255);
        }
        // read_file(): the first line that is neither empty nor '#'.
        let mut raw: &[u8] = &content;
        let mut name: Option<Vec<u8>> = None;
        while let Some(pos) = raw.iter().position(|&b| b == b'\n') {
            let (line, rest) = raw.split_at(pos);
            if !line.is_empty() && line[0] != b'#' {
                name = Some(line.to_vec());
                break;
            }
            raw = &rest[1..];
        }
        if name.is_none() && !raw.is_empty() && raw[0] != b'#' {
            name = Some(raw.to_vec());
        }
        let line = name.unwrap_or_default();
        // set_name(): strip leading/trailing ASCII isspace.
        let begin = line.iter().take_while(|&&b| is_name_space(b)).count();
        let end = line.len() - line.iter().rev().take_while(|&&b| is_name_space(b)).count();
        let line = line[begin..end].to_vec();
        let as_text = String::from_utf8_lossy(&line).into_owned();
        if !check_name(&as_text) {
            let _ = writeln!(io.stderr, "hostname: the specified hostname is invalid");
            return Ok(1);
        }
        // sethostname() rejects len > HOST_NAME_MAX (64) with "name too long".
        if line.len() > 64 {
            let _ = writeln!(io.stderr, "hostname: name too long");
            return Ok(1);
        }
        // [DIFFERS] GNU calls sethostname() (silent, rc 0); Windows cannot,
        // so the C++ applet completes with GNU's success status unchanged.
        return Ok(0);
    }

    if p.positionals.len() > 1 {
        let shown = p.positionals[1].to_string_lossy().into_owned();
        let _ = usage_error(io, "hostname", &format!("extra operand '{shown}'"));
        return Ok(1);
    }
    if p.positionals.len() == 1 {
        let shown = p.positionals[0].to_string_lossy().into_owned();
        let _ = writeln!(io.stderr, "hostname: cannot set name to '{shown}'");
        return Ok(1);
    }
    writeln!(io.stdout, "{}", winsys::imp::computer_name())?;
    Ok(0)
}

#[cfg(test)]
mod tests {
    use std::ffi::OsString;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(sc: &ScratchCtx, args: &[&str]) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::hostname_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn prints_name() {
        let sc = ScratchCtx::new("hn");
        let (r, out, _) = run(&sc, &[]);
        assert_eq!(r, 0);
        assert!(!out.trim_end().is_empty());
        assert_eq!(out.lines().count(), 1);
    }

    #[test]
    fn set_operand_errors() {
        let sc = ScratchCtx::new("hn2");
        let (r, _, err) = run(&sc, &["newname"]);
        assert_eq!(r, 1);
        assert!(err.contains("cannot set name to 'newname'"));
        let (r, _, err) = run(&sc, &["a", "b"]);
        assert_eq!(r, 1);
        assert!(err.contains("extra operand 'b'"));
    }

    #[test]
    fn net_tools_rejected() {
        let sc = ScratchCtx::new("hn3");
        for opt in ["-s", "-f", "-i", "-d", "-y", "-b", "-a", "-A", "-I", "-n"] {
            let (r, _, err) = run(&sc, &[opt]);
            assert_eq!(r, 1, "opt {opt}");
            assert!(!err.is_empty(), "opt {opt}");
        }
        let (r, _, _) = run(&sc, &["--short"]);
        assert_eq!(r, 1);
        let (r, _, _) = run(&sc, &["--bogus"]);
        assert_eq!(r, 1);
        let (r, _, _) = run(&sc, &["--help"]);
        assert_eq!(r, 0);
    }

    #[test]
    fn file_read_validate() {
        let sc = ScratchCtx::new("hn4");
        std::fs::write(sc.path("hf"), "myhost\n").unwrap();
        let (r, _, _) = run(&sc, &["-F", "hf"]);
        assert_eq!(r, 0); // validated, not set (Windows [DIFFERS])
                          // comment lines and blank lines skipped; whitespace trimmed
        std::fs::write(sc.path("hf2"), "# comment\n\n  spaced-name  \n").unwrap();
        let (r, _, _) = run(&sc, &["-F", "hf2"]);
        assert_eq!(r, 0);
        // invalid names
        std::fs::write(sc.path("bad"), "-badname\n").unwrap();
        let (r, _, err) = run(&sc, &["-F", "bad"]);
        assert_eq!(r, 1);
        assert!(err.contains("the specified hostname is invalid"));
        // only comments -> empty name -> invalid
        std::fs::write(sc.path("comments"), "# only\n").unwrap();
        let (r, _, _) = run(&sc, &["-F", "comments"]);
        assert_eq!(r, 1);
        // too long (> 64)
        std::fs::write(sc.path("long"), format!("{}\n", "a".repeat(65))).unwrap();
        let (r, _, err) = run(&sc, &["-F", "long"]);
        assert_eq!(r, 1);
        assert!(err.contains("name too long"));
        // missing file
        let (r, _, err) = run(&sc, &["-F", "nonexistent"]);
        assert_eq!(r, 1);
        assert!(!err.is_empty());
        // operand + -F: usage, exit 255
        let (r, _, _) = run(&sc, &["-F", "hf", "operand"]);
        assert_eq!(r, 255);
        // CRLF line endings: the CR is trimmed by isspace
        std::fs::write(sc.path("crlf"), b"crlfhost\r\n").unwrap();
        let (r, _, _) = run(&sc, &["-F", "crlf"]);
        assert_eq!(r, 0);
    }
}
