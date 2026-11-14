//! `uname` — GNU option grammar with the Windows value model of the 1.x
//! C++ applet (`src/commands/uname.cpp` on main; the gate's equivalence
//! side for platform-inherent output is the C++ Debug exe):
//!
//! * -s/--kernel-name/--sysname: "Windows_NT"
//! * -n/--nodename: GetComputerNameW (fallback "unknown")
//! * -r/--kernel-release/--release: "<major>.<minor>" via RtlGetVersion
//! * -v/--kernel-version: build number
//! * -m/--machine: x86_64 / aarch64 / i386 / unknown
//!   (GetNativeSystemInfo)
//! * -p/--processor: "unknown"; -i/--hardware-platform: "PC";
//!   -o/--operating-system: "Windows_NT"
//!
//! With no option, -s is printed. With -a everything prints in GNU's
//! fixed order (sysname nodename release version machine processor
//! hardware os). uname takes no operands: an operand is
//! `uname: extra operand 'X'` + usage, rc 1 (#1215 parity, GNU probed);
//! unknown options are rc 1. Values differ under WSL, so output cases
//! gate `win` (Rust rc vs C++ rc) while operand/option errors gate on rc.

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use crate::gnuhelp;
use crate::winsys;
use winux_core::{tr, usage_error, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('a'),
        long: "all",
        takes_value: false,
    },
    OptSpec {
        short: Some('s'),
        long: "kernel-name",
        takes_value: false,
    },
    OptSpec {
        short: Some('n'),
        long: "nodename",
        takes_value: false,
    },
    OptSpec {
        short: Some('r'),
        long: "kernel-release",
        takes_value: false,
    },
    OptSpec {
        short: Some('v'),
        long: "kernel-version",
        takes_value: false,
    },
    OptSpec {
        short: Some('m'),
        long: "machine",
        takes_value: false,
    },
    OptSpec {
        short: Some('p'),
        long: "processor",
        takes_value: false,
    },
    OptSpec {
        short: Some('i'),
        long: "hardware-platform",
        takes_value: false,
    },
    OptSpec {
        short: Some('o'),
        long: "operating-system",
        takes_value: false,
    },
    // obsolescent aliases GNU keeps
    OptSpec {
        short: None,
        long: "sysname",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "release",
        takes_value: false,
    },
];

// `--help` body verbatim from `env uname --help` under WSL coreutils 9.4,
// minus the shared gnuhelp::footer tail.
const UNAME_HELP_BODY: &str = r#"Usage: uname [OPTION]...
Print certain system information.  With no OPTION, same as -s.

  -a, --all                print all information, in the following order,
                             except omit -p and -i if unknown:
  -s, --kernel-name        print the kernel name
  -n, --nodename           print the network node hostname
  -r, --kernel-release     print the kernel release
  -v, --kernel-version     print the kernel version
  -m, --machine            print the machine hardware name
  -p, --processor          print the processor type (non-portable)
  -i, --hardware-platform  print the hardware platform (non-portable)
  -o, --operating-system   print the operating system
      --help        display this help and exit
      --version     output version information and exit

"#;

pub fn uname_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("uname", args, OPTS, io, ctx, 1) {
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
                write!(
                    io.stdout,
                    "{}{}",
                    UNAME_HELP_BODY,
                    gnuhelp::footer("uname", "uname invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("uname", "David MacKenzie")
                )?;
                0
            }
        });
    }
    if let Some(first) = p.positionals.first() {
        let shown = first.to_string_lossy().into_owned();
        // [GNU] uname.c: uname takes no operands (#1215 parity).
        let _ = usage_error(
            ctx,
            io,
            "uname",
            &tr!(
                ctx,
                "command.uname.error.extra_operand",
                "extra operand '{operand}'",
                operand = shown
            ),
        );
        return Ok(1);
    }

    let kernel_name = p.has("kernel-name") || p.has("sysname");
    let nodename = p.has("nodename");
    let release = p.has("kernel-release") || p.has("release");
    let version = p.has("kernel-version");
    let machine = p.has("machine");
    let processor = p.has("processor");
    let hardware = p.has("hardware-platform");
    let operating = p.has("operating-system");
    let all = p.has("all");
    // No field option at all behaves like -s (GNU uname default).
    let none_wanted = !all
        && !kernel_name
        && !nodename
        && !release
        && !version
        && !machine
        && !processor
        && !hardware
        && !operating;

    let mut pieces: Vec<String> = Vec::new();
    if all || kernel_name || none_wanted {
        pieces.push("Windows_NT".to_string());
    }
    if all || nodename {
        pieces.push(winsys::imp::computer_name());
    }
    if all || release {
        let (major, minor, _) = winsys::imp::os_version();
        pieces.push(format!("{major}.{minor}"));
    }
    if all || version {
        let (_, _, build) = winsys::imp::os_version();
        pieces.push(format!("{build}"));
    }
    if all || machine {
        pieces.push(winsys::imp::machine_arch().to_string());
    }
    if all || processor {
        pieces.push("unknown".to_string());
    }
    if all || hardware {
        pieces.push("PC".to_string());
    }
    if all || operating {
        pieces.push("Windows_NT".to_string());
    }
    writeln!(io.stdout, "{}", pieces.join(" "))?;
    Ok(0)
}

#[cfg(test)]
mod tests {
    // [GNU] `--help`/`--version` print the probed coreutils text, rc 0
    // (strict rows un17/un18).
    #[test]
    fn gnu_help_version_surface() {
        let (r, out, err) = run(&["--help"]);
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.starts_with("Usage: uname [OPTION]...\n"));
        assert!(out.contains("-i, --hardware-platform  print the hardware platform (non-portable)"));
        assert!(out.ends_with("info '(coreutils) uname invocation'\n"));
        let (r, out, _) = run(&["--version"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("uname (GNU coreutils) 9.4\n"));
        assert!(out.ends_with("Written by David MacKenzie.\n"));
    }

    use std::ffi::OsString;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str]) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("uname");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::uname_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn default_is_kernel_name() {
        let (r, out, _) = run(&[]);
        assert_eq!(r, 0);
        assert_eq!(out, "Windows_NT\n");
        let (r, out, _) = run(&["-s"]);
        assert_eq!(r, 0);
        assert_eq!(out, "Windows_NT\n");
        let (_, out, _) = run(&["--kernel-name"]);
        assert_eq!(out, "Windows_NT\n");
    }

    #[test]
    fn all_prints_every_field_in_gnu_order() {
        let (r, out, _) = run(&["-a"]);
        assert_eq!(r, 0);
        let parts: Vec<&str> = out.trim_end().split(' ').collect();
        assert_eq!(parts[0], "Windows_NT");
        assert_eq!(parts[2], "10.0"); // release = major.minor
        assert_eq!(parts[4], "x86_64"); // machine on this host
        assert_eq!(parts[5], "unknown"); // processor
        assert_eq!(parts[6], "PC"); // hardware platform
        assert_eq!(parts[7], "Windows_NT"); // OS
        assert_eq!(parts.len(), 8);
    }

    #[test]
    fn single_flags_hold_positions() {
        let (_, out, _) = run(&["-r"]);
        assert_eq!(out, "10.0\n");
        let (_, out, _) = run(&["-v"]);
        assert!(out.trim_end().parse::<u32>().is_ok()); // build number
        let (_, out, _) = run(&["-m"]);
        assert_eq!(out.trim_end(), "x86_64");
        let (_, out, _) = run(&["-p"]);
        assert_eq!(out.trim_end(), "unknown");
        let (_, out, _) = run(&["-i"]);
        assert_eq!(out.trim_end(), "PC");
        let (_, out, _) = run(&["-o"]);
        assert_eq!(out.trim_end(), "Windows_NT");
        let (_, out, _) = run(&["-n"]);
        assert!(!out.trim_end().is_empty());
    }

    #[test]
    fn operands_and_options() {
        let (r, _, err) = run(&["extra"]);
        assert_eq!(r, 1);
        assert!(err.contains("extra operand 'extra'"));
        let (r, _, _) = run(&["--bogus"]);
        assert_eq!(r, 1);
        let (r, _, _) = run(&["-Z"]);
        assert_eq!(r, 1);
        let (r, _, _) = run(&["--help"]);
        assert_eq!(r, 0);
        // obsolescent aliases
        let (_, out, _) = run(&["--sysname"]);
        assert_eq!(out, "Windows_NT\n");
        let (_, out, _) = run(&["--release"]);
        assert_eq!(out, "10.0\n");
    }
}
