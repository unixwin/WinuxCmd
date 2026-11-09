//! `truncate` — GNU coreutils semantics (probed against coreutils 9.4).
//!
//! * `--size SIZE` (or `--reference RFILE`, last of the two wins) sizes
//!   every FILE operand; missing files are created (O_CREAT) unless
//!   `-c/--no-create`, which silently skips them with rc 0.
//! * SIZE grammar: one optional modifier BEFORE the number — `+` grow by,
//!   `-` shrink by (clamped at 0, rc 0: `-s -10` on a 3-byte file gives
//!   size 0), `/` round DOWN to a multiple, `%` round UP to a multiple,
//!   `<` at most, `>` at least; then an optional sign, then a decimal
//!   number, then an optional unit: single letters K M G T P E Z Y R Q
//!   are 1024-based, `KiB`-style and `kB`-style forms are accepted
//!   (1K == 1KiB == 1024, 1kB == 1000). `/0` or `%0` is `division by
//!   zero`, rc 1. Junk, an empty string, a trailing `%` (`+50%`) or
//!   overflow (`99999999999999999999`) is `Invalid number: 'X'`, rc 1.
//! * A directory operand is `cannot open 'd' for writing: Is a
//!   directory`, rc 1; a missing parent likewise reports the open failure
//!   for writing. All operands are processed; any failure makes rc 1.
//! * Neither --size nor --reference is a usage error rc 1; with no
//!   operands GNU reports `missing file operand` rc 1.
//!
//! Growth fills with NULs (File::set_len), exactly the GNU observable.

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use crate::fsutil::{reason, resolve};
use winux_core::{usage_error, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('s'),
        long: "size",
        takes_value: true,
    },
    OptSpec {
        short: Some('c'),
        long: "no-create",
        takes_value: false,
    },
    OptSpec {
        short: Some('r'),
        long: "reference",
        takes_value: true,
    },
];

/// One parsed size request.
#[derive(Clone, Copy, PartialEq)]
enum Modifier {
    Absolute,
    Grow,
    Shrink,
    RoundDown,
    RoundUp,
    AtMost,
    AtLeast,
}

struct SizeSpec {
    modifier: Modifier,
    value: u64,
}

/// Parse a GNU truncate SIZE spec. None = `Invalid number: '<raw>'`.
fn parse_size(raw: &str) -> Option<SizeSpec> {
    let bytes = raw.as_bytes();
    let mut i = 0usize;
    let mut modifier = Modifier::Absolute;
    if let Some(&c) = bytes.first() {
        let m = match c {
            b'+' => Some(Modifier::Grow),
            b'-' => Some(Modifier::Shrink),
            b'/' => Some(Modifier::RoundDown),
            b'%' => Some(Modifier::RoundUp),
            b'<' => Some(Modifier::AtMost),
            b'>' => Some(Modifier::AtLeast),
            _ => None,
        };
        if let Some(m) = m {
            modifier = m;
            i += 1;
        }
    }
    // optional sign after the modifier
    let mut negative = false;
    if let Some(&c) = bytes.get(i) {
        if c == b'-' {
            negative = true;
            i += 1;
        } else if c == b'+' {
            i += 1;
        }
    }
    let digits_start = i;
    while i < bytes.len() && bytes[i].is_ascii_digit() {
        i += 1;
    }
    if i == digits_start {
        return None; // no digits at all
    }
    let number: u64 = raw[digits_start..i].parse().ok()?;
    let unit = &raw[i..];
    let multiplier: u64 = match unit {
        "" => 1,
        "K" | "k" | "KiB" => 1024,
        "M" | "m" | "MiB" => 1024 * 1024,
        "G" | "g" | "GiB" => 1024 * 1024 * 1024,
        "T" | "TiB" => 1024u64.pow(4),
        "P" | "PiB" => 1024u64.pow(5),
        "E" | "EiB" => 1024u64.pow(6),
        "Z" | "ZiB" => 1024u64.pow(7),
        "Y" | "YiB" => 1024u64.pow(8),
        "kB" => 1000,
        "MB" => 1000 * 1000,
        "GB" => 1000 * 1000 * 1000,
        "TB" => 1000u64.pow(4),
        _ => return None,
    };
    let value = number.checked_mul(multiplier)?;
    if negative {
        // The `-` modifier already handles shrinking; a second sign is
        // junk like `-s --5` (GNU: Invalid number).
        return None;
    }
    Some(SizeSpec { modifier, value })
}

/// Apply the modifier to the current file size.
fn apply_size(modifier: Modifier, current: u64, value: u64) -> Result<u64, ()> {
    let target = match modifier {
        Modifier::Absolute => value,
        Modifier::Grow => current.saturating_add(value),
        Modifier::Shrink => current.saturating_sub(value),
        Modifier::RoundDown => {
            if value == 0 {
                return Err(());
            }
            current / value * value
        }
        Modifier::RoundUp => {
            if value == 0 {
                return Err(());
            }
            current.div_ceil(value) * value
        }
        Modifier::AtMost => current.min(value),
        Modifier::AtLeast => current.max(value),
    };
    Ok(target)
}

pub fn truncate_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("truncate", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if p.has("help") || p.has("version") {
        return Ok(0);
    }
    // Last of --size/--reference wins (GNU keeps the final one).
    let size_raw = p.value_of("size").map(|s| s.to_string());
    let reference = p.value_of("reference").map(|s| s.to_string());
    let spec = if size_raw.is_some() {
        size_raw.map(|raw| (raw, false))
    } else {
        reference.map(|raw| (raw, true))
    };
    let Some((spec_raw, is_reference)) = spec else {
        let _ = usage_error(
            io,
            "truncate",
            "you must specify either '--size' or '--reference'",
        );
        return Ok(1);
    };
    if p.positionals.is_empty() {
        let _ = usage_error(io, "truncate", "missing file operand");
        return Ok(1);
    }
    let no_create = p.has("no-create");

    let mut failed = false;
    for operand in &p.positionals {
        let name = operand.to_string_lossy().into_owned();
        let path = resolve(ctx, &name);
        let existing = std::fs::metadata(&path);
        let exists = existing.is_ok();
        if is_reference {
            // The REFERENCE file is statted (GNU: cannot stat 'R'), then the
            // operand is sized to its length.
            let ref_path = resolve(ctx, spec_raw.clone());
            match std::fs::metadata(&ref_path) {
                Ok(md) => {
                    if !size_file(&path, md.len(), no_create, exists, io, &name) {
                        failed = true;
                    }
                }
                Err(_) => {
                    let _ = writeln!(
                        io.stderr,
                        "truncate: cannot stat '{spec_raw}': No such file or directory"
                    );
                    failed = true;
                }
            }
            continue;
        }
        let Some(parsed) = parse_size(&spec_raw) else {
            let _ = writeln!(io.stderr, "truncate: Invalid number: '{spec_raw}'");
            return Ok(1);
        };
        let current = existing.as_ref().map(|m| m.len()).unwrap_or(0);
        let target = match apply_size(parsed.modifier, current, parsed.value) {
            Ok(t) => t,
            Err(()) => {
                let _ = writeln!(io.stderr, "truncate: division by zero");
                failed = true;
                continue;
            }
        };
        if !size_file(&path, target, no_create, existing.is_ok(), io, &name) {
            failed = true;
        }
    }
    Ok(if failed { 1 } else { 0 })
}

/// Set (or create) the file at `path` to `target` bytes. Reports GNU-style
/// open-for-writing errors; returns success.
fn size_file(
    path: &std::path::Path,
    target: u64,
    no_create: bool,
    exists: bool,
    io: &mut Io,
    name: &str,
) -> bool {
    if no_create && !exists {
        return true; // silently nothing to do
    }
    let file = match std::fs::OpenOptions::new()
        .write(true)
        .create(true)
        .open(path)
    {
        Ok(f) => f,
        Err(e) => {
            let _ = writeln!(
                io.stderr,
                "truncate: cannot open '{name}' for writing: {}",
                reason(&e)
            );
            return false;
        }
    };
    if file.set_len(target).is_err() {
        let _ = writeln!(
            io.stderr,
            "truncate: cannot open '{name}' for writing: Invalid argument"
        );
        return false;
    }
    true
}

#[cfg(test)]
mod tests {
    use std::ffi::OsString;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(sc: &ScratchCtx, args: &[&str]) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::truncate_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    fn setup(sc: &ScratchCtx, content: &[u8]) {
        std::fs::write(sc.path("f"), content).unwrap();
    }

    fn size_of(sc: &ScratchCtx) -> u64 {
        std::fs::metadata(sc.path("f"))
            .map(|m| m.len())
            .unwrap_or(0)
    }

    #[test]
    fn absolute_and_relative() {
        let sc = ScratchCtx::new("trunc");
        setup(&sc, b"0123456789ABCDEF"); // 16 bytes
        let (r, _, _) = run(&sc, &["-s", "4", "f"]);
        assert_eq!(r, 0);
        assert_eq!(size_of(&sc), 4);

        setup(&sc, b"abc");
        let (r, _, _) = run(&sc, &["-s", "+2", "f"]);
        assert_eq!(r, 0);
        assert_eq!(size_of(&sc), 5);

        let (r, _, _) = run(&sc, &["-s", "-2", "f"]);
        assert_eq!(r, 0);
        assert_eq!(size_of(&sc), 3);

        // shrink below zero clamps at 0, rc 0 (GNU probed)
        let (r, _, _) = run(&sc, &["-s", "-10", "f"]);
        assert_eq!(r, 0);
        assert_eq!(size_of(&sc), 0);

        // grow fills NULs
        setup(&sc, b"abc");
        run(&sc, &["-s", "6", "f"]);
        let data = std::fs::read(sc.path("f")).unwrap();
        assert_eq!(data, b"abc\0\0\0");
    }

    #[test]
    fn units_and_rounding() {
        let sc = ScratchCtx::new("trunc2");
        setup(&sc, b"abc");
        run(&sc, &["-s", "1K", "f"]);
        assert_eq!(size_of(&sc), 1024);
        setup(&sc, b"abc");
        run(&sc, &["-s", "1KiB", "f"]);
        assert_eq!(size_of(&sc), 1024);
        setup(&sc, b"abc");
        run(&sc, &["-s", "1kB", "f"]);
        assert_eq!(size_of(&sc), 1000);
        setup(&sc, b"0123456789ABCDEF"); // 16
        run(&sc, &["-s", "%5", "f"]);
        assert_eq!(size_of(&sc), 20); // round up to multiple of 5
        setup(&sc, b"0123456789ABCDEF");
        run(&sc, &["-s", "/5", "f"]);
        assert_eq!(size_of(&sc), 15); // round down
        setup(&sc, b"0123456789ABCDEF");
        run(&sc, &["-s", ">10", "f"]);
        assert_eq!(size_of(&sc), 16); // at least 10
        setup(&sc, b"0123456789ABCDEF");
        run(&sc, &["-s", "<20", "f"]);
        assert_eq!(size_of(&sc), 16); // at most 20
        setup(&sc, b"0123456789ABCDEF");
        let (r, _, _) = run(&sc, &["-s", ">1K", "f"]);
        assert_eq!(r, 0);
        assert_eq!(size_of(&sc), 1024);
    }

    #[test]
    fn create_and_no_create() {
        let sc = ScratchCtx::new("trunc3");
        let (r, _, _) = run(&sc, &["-s", "10", "newf"]);
        assert_eq!(r, 0);
        assert_eq!(std::fs::metadata(sc.path("newf")).unwrap().len(), 10);
        let (r, _, _) = run(&sc, &["-c", "-s", "5", "never"]);
        assert_eq!(r, 0);
        assert!(!sc.path("never").exists());
        let (r, _, _) = run(&sc, &["-c", "-s", "5", "newf"]);
        assert_eq!(r, 0);
        assert_eq!(std::fs::metadata(sc.path("newf")).unwrap().len(), 5);
    }

    #[test]
    fn errors() {
        let sc = ScratchCtx::new("trunc4");
        setup(&sc, b"abc");
        // division by zero
        let (r, _, err) = run(&sc, &["-s", "%0", "f"]);
        assert_eq!(r, 1);
        assert!(err.contains("division by zero"));
        // invalid numbers
        for bad in ["xyz", "", "+50%", "99999999999999999999", "5x"] {
            let (r, _, err) = run(&sc, &["-s", bad, "f"]);
            assert_eq!(r, 1, "bad={bad:?}");
            assert!(err.contains("Invalid number"), "bad={bad:?}");
        }
        // no size
        let (r, _, err) = run(&sc, &["f"]);
        assert_eq!(r, 1);
        assert!(err.contains("you must specify either"));
        // no operands
        let (r, _, err) = run(&sc, &["-s", "1"]);
        assert_eq!(r, 1);
        assert!(err.contains("missing file operand"));
        // missing parent dir
        let (r, _, err) = run(&sc, &["-s", "1", "nodir/f"]);
        assert_eq!(r, 1);
        assert!(err.contains("cannot open 'nodir/f' for writing"));
        // reference missing
        let (r, _, err) = run(&sc, &["--reference=zzz", "f"]);
        assert_eq!(r, 1);
        assert!(err.contains("cannot stat 'zzz'"));
        // reference works
        std::fs::write(sc.path("r"), "01234567").unwrap();
        let (r, _, _) = run(&sc, &["--reference=r", "f"]);
        assert_eq!(r, 0);
        assert_eq!(size_of(&sc), 8);
    }

    #[test]
    fn multiple_files() {
        let sc = ScratchCtx::new("trunc5");
        std::fs::write(sc.path("f"), "abc").unwrap();
        std::fs::write(sc.path("g"), "abcd").unwrap();
        let (r, _, _) = run(&sc, &["-s", "1", "f", "g"]);
        assert_eq!(r, 0);
        assert_eq!(size_of(&sc), 1);
        // a directory operand fails the aggregate rc but still sizes f
        std::fs::write(sc.path("f"), "abcdef").unwrap();
        std::fs::create_dir(sc.path("d")).unwrap();
        let (r, _, _) = run(&sc, &["-s", "1", "d", "f"]);
        assert_eq!(r, 1);
        assert_eq!(size_of(&sc), 1);
    }
}
