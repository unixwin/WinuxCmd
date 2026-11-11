//! `truncate` — GNU coreutils semantics (probed against coreutils 9.4).
//!
//! * `--size SIZE` (or `--reference RFILE`, last of the two wins) sizes
//!   every FILE operand; missing files are created (O_CREAT) unless
//!   `-c/--no-create`, which silently skips them with rc 0.
//! * SIZE grammar (truncate.c:222-258): leading whitespace is skipped
//!   before and after the modifier; one optional modifier BEFORE the
//!   number — `+` grow by, `-` shrink by (clamped at 0, rc 0: `-s -10` on
//!   a 3-byte file gives size 0), `/` round DOWN to a multiple, `%` round
//!   UP to a multiple, `<` at most, `>` at least; then a decimal number,
//!   then an optional unit: single letters K M G T P E Z Y R Q are
//!   1024-based, `KiB`-style and `kB`-style forms are accepted
//!   (1K == 1KiB == 1024, 1kB == 1000). `/0` or `%0` is `division by
//!   zero`, rc 1. Junk, an empty string, a trailing `%` (`+50%`), a
//!   second relative modifier (`<+5`), or overflow is `Invalid number:
//!   'X'` quoting the whitespace-stripped token (`' 5x'` quotes `5x`), rc 1.
//! * A directory operand is `cannot open 'd' for writing: Is a
//!   directory`, rc 1; a missing parent likewise reports the open failure
//!   for writing. All operands are processed; any failure makes rc 1.
//! * Neither --size nor --reference is a usage error rc 1; with no
//!   operands GNU reports `missing file operand` rc 1. `--reference` may
//!   only be combined with a *relative* `--size` (truncate.c:298-303);
//!   with both, the reference's length is the base the modifier applies
//!   to (truncate.c:135-138). `-o/--io-blocks` scales SIZE by the file's
//!   I/O block size (truncate.c:116-125; here the volume geometry of the
//!   file's location, 512 fallback) and is rejected without `--size`
//!   (truncate.c:305-309).
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
        short: Some('o'),
        long: "io-blocks",
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

#[derive(Clone, Copy)]
struct SizeSpec {
    modifier: Modifier,
    value: u64,
}

/// Why a SIZE spec was rejected.
enum SizeError {
    /// GNU `Invalid number: '<token>'` — the token is what xdectoimax
    /// would quote: the spec after leading-whitespace strip, including a
    /// leading +/- sign (` 5x` quotes `5x`, `+ 5` quotes `+ 5`).
    Invalid(String),
    /// GNU `multiple relative modifiers specified` (truncate.c:263) for a
    /// `<>/%` modifier followed by +/-.
    MultipleModifiers,
}

/// Parse a GNU truncate SIZE spec (truncate.c:222-258): leading whitespace
/// is skipped before the `<>/%` modifier and again after it; a +/- sign
/// selects the relative mode and is parsed by strtoimax rules (no
/// whitespace between sign and digits).
fn parse_size(raw: &str) -> Result<SizeSpec, SizeError> {
    let bytes = raw.as_bytes();
    let mut i = 0usize;
    while i < bytes.len() && bytes[i].is_ascii_whitespace() {
        i += 1;
    }
    let mut modifier = Modifier::Absolute;
    if i < bytes.len() && matches!(bytes[i], b'<' | b'>' | b'/' | b'%') {
        modifier = match bytes[i] {
            b'/' => Modifier::RoundDown,
            b'%' => Modifier::RoundUp,
            b'<' => Modifier::AtMost,
            _ => Modifier::AtLeast,
        };
        i += 1;
        while i < bytes.len() && bytes[i].is_ascii_whitespace() {
            i += 1;
        }
    }
    // The +/- sign selects the relative mode; GNU hands the token starting
    // AT the sign to xdectoimax, so diagnostics quote from here.
    let token_start = i;
    if i < bytes.len() && (bytes[i] == b'+' || bytes[i] == b'-') {
        if modifier != Modifier::Absolute {
            return Err(SizeError::MultipleModifiers);
        }
        modifier = if bytes[i] == b'+' {
            Modifier::Grow
        } else {
            Modifier::Shrink
        };
        i += 1;
    }
    let digits_start = i;
    while i < bytes.len() && bytes[i].is_ascii_digit() {
        i += 1;
    }
    let invalid = || SizeError::Invalid(raw[token_start..].to_string());
    if i == digits_start {
        return Err(invalid()); // no digits at all
    }
    let number: u64 = raw[digits_start..i].parse().map_err(|_| invalid())?;
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
        _ => return Err(invalid()),
    };
    let value = number.checked_mul(multiplier).ok_or_else(invalid)?;
    Ok(SizeSpec { modifier, value })
}

/// Why a size request could not be applied.
enum ApplyError {
    /// `/0` or `%0` (GNU: `division by zero`).
    DivZero,
    /// Grow/round-up past off_t (GNU: `overflow extending size of file`).
    Overflow,
}

/// Apply the modifier to the current file size.
fn apply_size(modifier: Modifier, current: u64, value: u64) -> Result<u64, ApplyError> {
    let target = match modifier {
        Modifier::Absolute => value,
        Modifier::Grow => current.checked_add(value).ok_or(ApplyError::Overflow)?,
        Modifier::Shrink => current.saturating_sub(value),
        Modifier::RoundDown => {
            if value == 0 {
                return Err(ApplyError::DivZero);
            }
            current / value * value
        }
        Modifier::RoundUp => {
            if value == 0 {
                return Err(ApplyError::DivZero);
            }
            // GNU: r = fsize % ssize; ssize = r == 0 ? 0 : ssize - r (then
            // ckd_add, so the wrap can also overflow).
            let r = current % value;
            let add = if r == 0 { 0 } else { value - r };
            current.checked_add(add).ok_or(ApplyError::Overflow)?
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
    let io_blocks = p.has("io-blocks");
    let size_raw = p.value_of("size").map(|s| s.to_string());
    let reference = p.value_of("reference").map(|s| s.to_string());
    // GNU parses --size inside getopt (xdectoimax, truncate.c:222-258), so
    // an invalid number or a modifier conflict is diagnosed before any
    // cross-option validation.
    let parsed = match size_raw.as_deref() {
        Some(raw) => match parse_size(raw) {
            Ok(spec) => Some(spec),
            Err(SizeError::Invalid(token)) => {
                let _ = writeln!(io.stderr, "truncate: Invalid number: '{token}'");
                return Ok(1);
            }
            Err(SizeError::MultipleModifiers) => {
                let _ = usage_error(io, "truncate", "multiple relative modifiers specified");
                return Ok(1);
            }
        },
        None => None,
    };
    // Cross-option validation in GNU truncate.c:291-313 order.
    if reference.is_none() && parsed.is_none() {
        let _ = usage_error(
            io,
            "truncate",
            "you must specify either '--size' or '--reference'",
        );
        return Ok(1);
    }
    if let Some(spec) = &parsed {
        if reference.is_some() && spec.modifier == Modifier::Absolute {
            let _ = usage_error(
                io,
                "truncate",
                "you must specify a relative '--size' with '--reference'",
            );
            return Ok(1);
        }
    }
    if io_blocks && parsed.is_none() {
        let _ = usage_error(
            io,
            "truncate",
            "'--io-blocks' was specified but '--size' was not",
        );
        return Ok(1);
    }
    if p.positionals.is_empty() {
        let _ = usage_error(io, "truncate", "missing file operand");
        return Ok(1);
    }
    // The reference is statted once, before any operand is touched
    // (truncate.c:319-350); with a relative --size its length is the base
    // the modifier applies to, without --size it is the target itself.
    let rsize: Option<u64> = match reference.as_deref() {
        Some(r) => match std::fs::metadata(resolve(ctx, r)) {
            Ok(md) => Some(md.len()),
            Err(_) => {
                let _ = writeln!(
                    io.stderr,
                    "truncate: cannot stat '{r}': No such file or directory"
                );
                return Ok(1);
            }
        },
        None => None,
    };

    let mut failed = false;
    for operand in &p.positionals {
        let name = operand.to_string_lossy().into_owned();
        let path = resolve(ctx, &name);
        let existing = std::fs::metadata(&path);
        let exists = existing.is_ok();
        let no_create = p.has("no-create");
        let Some(spec) = parsed else {
            // Pure --reference: every operand is sized to the reference's
            // length.
            if !size_file(ctx, &path, rsize.unwrap_or(0), no_create, exists, io, &name) {
                failed = true;
            }
            continue;
        };
        // Relative --size bases on the reference's length when given
        // (truncate.c:135-138: fsize = rsize when 0 <= rsize), else on the
        // operand's own size.
        let current = existing.as_ref().map(|m| m.len()).unwrap_or(0);
        let base = if reference.is_some() {
            rsize.unwrap_or(0)
        } else {
            current
        };
        // -o scales SIZE by the file's I/O block size before the modifier
        // applies (truncate.c:116-125).
        let value = if io_blocks {
            let block = io_block_size_for(&path);
            match spec.value.checked_mul(block) {
                Some(v) => v,
                None => {
                    let _ = writeln!(
                        io.stderr,
                        "truncate: overflow in {} * {} byte blocks for file '{name}'",
                        spec.value, block
                    );
                    failed = true;
                    continue;
                }
            }
        } else {
            spec.value
        };
        let target = match apply_size(spec.modifier, base, value) {
            Ok(t) => t,
            Err(ApplyError::DivZero) => {
                let _ = writeln!(io.stderr, "truncate: division by zero");
                failed = true;
                continue;
            }
            Err(ApplyError::Overflow) => {
                let _ = writeln!(
                    io.stderr,
                    "truncate: overflow extending size of file '{name}'"
                );
                failed = true;
                continue;
            }
        };
        if !size_file(ctx, &path, target, no_create, existing.is_ok(), io, &name) {
            failed = true;
        }
    }
    Ok(if failed { 1 } else { 0 })
}

/// Set (or create) the file at `path` to `target` bytes. Reports GNU-style
/// open-for-writing errors; returns success.
fn size_file(
    ctx: &Ctx,
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
            // O_CREAT semantics (probed GNU 9.4, WSL + MSYS): a missing
            // `name/` is EISDIR because the trailing slash demands a
            // directory; an existing `file/` is ENOTDIR; a directory
            // operand is EISDIR. Windows-native CreateFile fails those
            // with ERROR_INVALID_NAME/EACCES instead, so the reason comes
            // from the shared GNU recovery helper.
            let reason = crate::fsutil::gnu_open_reason(ctx, name, &e, true);
            let _ = writeln!(
                io.stderr,
                "truncate: cannot open '{name}' for writing: {reason}"
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

const TRUNCATE_MAX_PATH: usize = 260;

#[link(name = "kernel32")]
extern "system" {
    fn GetVolumePathNameW(
        lpsz_file_name: *const u16,
        lpsz_volume_path_buffer: *mut u16,
        cch_buffer_length: u32,
    ) -> i32;
    fn GetDiskFreeSpaceW(
        lp_root_path_name: *const u16,
        lp_sectors_per_cluster: *mut u32,
        lp_bytes_per_sector: *mut u32,
        lp_number_of_free_clusters: *mut u32,
        lp_total_number_of_clusters: *mut u32,
    ) -> i32;
}

fn wide(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}

/// The I/O block size `-o` scales SIZE with: the volume geometry of the
/// location the file lives in (sectors-per-cluster * bytes-per-sector),
/// 512 when the volume cannot be queried. This is the Windows equivalent
/// of GNU's ST_BLKSIZE(sb) of the opened file (truncate.c:116-125), the
/// same query the 1.x body made.
fn io_block_size_for(path: &std::path::Path) -> u64 {
    let dir = path
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or(path);
    let dir_w = wide(&dir.to_string_lossy());
    let mut root = [0u16; TRUNCATE_MAX_PATH];
    let have_root =
        unsafe { GetVolumePathNameW(dir_w.as_ptr(), root.as_mut_ptr(), TRUNCATE_MAX_PATH as u32) };
    if have_root == 0 {
        return 512;
    }
    let (mut spc, mut bps, mut free, mut total) = (0u32, 0u32, 0u32, 0u32);
    let ok = unsafe { GetDiskFreeSpaceW(root.as_ptr(), &mut spc, &mut bps, &mut free, &mut total) };
    if ok == 0 {
        return 512;
    }
    let block = u64::from(spc) * u64::from(bps);
    if block == 0 {
        512
    } else {
        block
    }
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

    #[test]
    fn reference_with_relative_size() {
        let sc = ScratchCtx::new("trunc6");
        // GNU: a relative --size applies to the reference's length
        std::fs::write(sc.path("r"), "01234567").unwrap(); // 8 bytes
        std::fs::write(sc.path("f"), "abc").unwrap();
        let (r, _, _) = run(&sc, &["-r", "r", "-s", "+2", "f"]);
        assert_eq!(r, 0);
        assert_eq!(size_of(&sc), 10);
        std::fs::write(sc.path("f"), "0123456789ABCDEF").unwrap();
        let (r, _, _) = run(&sc, &["--reference=r", "--size=%5", "f"]);
        assert_eq!(r, 0);
        assert_eq!(size_of(&sc), 10); // round 8 up to a multiple of 5
                                      // GNU: an absolute --size with --reference is rejected
        std::fs::write(sc.path("f"), "abc").unwrap();
        let (r, _, err) = run(&sc, &["-r", "r", "-s", "5", "f"]);
        assert_eq!(r, 1);
        assert!(err.contains("you must specify a relative '--size' with '--reference'"));
        assert_eq!(size_of(&sc), 3); // operand untouched
                                     // pure --reference still sizes to the reference length
        let (r, _, _) = run(&sc, &["-r", "r", "f"]);
        assert_eq!(r, 0);
        assert_eq!(size_of(&sc), 8);
    }

    #[test]
    fn io_blocks() {
        let sc = ScratchCtx::new("trunc7");
        // GNU: -o without --size is rejected (truncate.c:305-309)
        std::fs::write(sc.path("f"), "abc").unwrap();
        let (r, _, err) = run(&sc, &["-o", "-r", "r", "f"]);
        assert_eq!(r, 1);
        assert!(err.contains("'--io-blocks' was specified but '--size' was not"));
        // -o -s N sizes to N * the file's I/O block size
        std::fs::write(sc.path("f"), "abc").unwrap();
        let block = super::io_block_size_for(&sc.path("f"));
        let (r, _, _) = run(&sc, &["-o", "-s", "2", "f"]);
        assert_eq!(r, 0);
        assert_eq!(size_of(&sc), block * 2);
        // -o composes with relative modifiers (base is the operand's size)
        std::fs::write(sc.path("f"), "0123456789ABCDEF").unwrap();
        let (r, _, _) = run(&sc, &["-o", "-s", "+1", "f"]);
        assert_eq!(r, 0);
        assert_eq!(size_of(&sc), 16 + block);
    }

    #[test]
    fn whitespace_in_size_issue1360() {
        let sc = ScratchCtx::new("trunc8");
        // GNU truncate.c skips leading whitespace before the number
        std::fs::write(sc.path("f"), "0123456789").unwrap();
        let (r, _, _) = run(&sc, &["-s", " 5", "f"]);
        assert_eq!(r, 0);
        assert_eq!(size_of(&sc), 5);
        // ... and before the +/- sign
        std::fs::write(sc.path("f"), "0123456789").unwrap();
        let (r, _, _) = run(&sc, &["-s", " +5", "f"]);
        assert_eq!(r, 0);
        assert_eq!(size_of(&sc), 15);
        // whitespace between sign and digits stays invalid; the diagnostic
        // quotes the token FROM the sign (xdectoimax's optarg)
        std::fs::write(sc.path("f"), "0123456789").unwrap();
        let (r, _, err) = run(&sc, &["-s", "+ 5", "f"]);
        assert_eq!(r, 1);
        assert!(err.contains("Invalid number: '+ 5'"));
        // junk after whitespace-strip quotes the stripped token
        std::fs::write(sc.path("f"), "x").unwrap();
        let (r, _, err) = run(&sc, &["-s", " 5x", "f"]);
        assert_eq!(r, 1);
        assert!(err.contains("Invalid number: '5x'"));
        // trailing whitespace is junk
        std::fs::write(sc.path("f"), "x").unwrap();
        let (r, _, _) = run(&sc, &["-s", "5 ", "f"]);
        assert_eq!(r, 1);
        // a <>/% modifier followed by +/- is the GNU modifier conflict
        std::fs::write(sc.path("f"), "x").unwrap();
        let (r, _, err) = run(&sc, &["-s", "<+5", "f"]);
        assert_eq!(r, 1);
        assert!(err.contains("multiple relative modifiers specified"));
    }
}
