//! `test` / `[` — GNU coreutils semantics (probed against coreutils 9.4).
//!
//! No option parsing at all: the arguments ARE the expression. GNU
//! special-cases 0-3 arguments (deprecated behaviors with distinct error
//! messages) and parses longer expressions with POSIX precedence:
//! `!` binds tightest, then `-a`, then `-o`; parentheses group.
//! Statuses: 0 true, 1 false, 2 syntax error. All diagnostics go to stderr.
//!
//! File-test mapping on Windows is the documented pilot subset
//! (`-r`/`-w` = exists, matching the WSL-root oracle; `-x` = directory or
//! executable extension; Unix-only bits `-u -g -k` = false). DOS device
//! operands (/dev/null, the 24 reserved names, `\\.\` spellings) take the
//! winux-core device probe — the stat #1570/#1579 boundary — instead of
//! the std::fs snapshot that rejects device names: GNU /dev/null is a 666
//! character special (-e/-r/-w/-c true; -b/-s false; device identity
//! decides -ef).

use std::ffi::OsString;

use crate::fsutil::resolve;
use winux_core::{CmdResult, Ctx, Io};

const UNOPS: &[&str] = &[
    "-n", "-z", "-e", "-f", "-d", "-s", "-r", "-w", "-x", "-L", "-h", "-b", "-c", "-p", "-S", "-t",
    "-u", "-g", "-k", "-N", "-O", "-G",
];

const BINOPS: &[&str] = &[
    "=", "!=", "==", "<", ">", "-eq", "-ne", "-lt", "-le", "-gt", "-ge", "-nt", "-ot", "-ef",
    // [GNU] the AND/OR operators are binops in the 3-argument form
    // (probed 9.4: a -a b -> 0, a -a EMPTY -> 1).
    "-a", "-o",
];

struct TestError {
    msg: String,
}

fn err(msg: String) -> TestError {
    TestError { msg }
}

pub fn test_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    eval_args(args, false, io, ctx)
}

pub fn bracket_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    eval_args(args, true, io, ctx)
}

fn eval_args(args: &[OsString], bracket: bool, io: &mut Io, ctx: &Ctx) -> CmdResult {
    let prog = if bracket { "[" } else { "test" };
    let mut argv: Vec<String> = args
        .iter()
        .map(|a| a.to_string_lossy().into_owned())
        .collect();
    if bracket {
        match argv.pop() {
            Some(ref s) if s == "]" => {}
            _ => {
                let _ = writeln!(io.stderr, "[: missing ']'");
                return Ok(2);
            }
        }
    }

    let result = if bracket && argv.is_empty() {
        Ok(false) // `[ ]` -> test of empty string -> false
    } else {
        posix_test(&argv, ctx, bracket)
    };

    match result {
        Ok(v) => Ok(if v { 0 } else { 1 }),
        Err(e) => {
            let _ = writeln!(io.stderr, "{prog}: {}", e.msg);
            Ok(2)
        }
    }
}

/// GNU-style dispatch: small nargs have dedicated behaviors (distinct error
/// messages), longer expressions use the general and/or grammar.
fn posix_test(argv: &[String], ctx: &Ctx, bracket: bool) -> Result<bool, TestError> {
    let n = argv.len();
    match n {
        0 => Ok(false),
        1 => Ok(!argv[0].is_empty()),
        2 => two_arguments(argv, ctx),
        3 => three_arguments(argv, ctx),
        _ => {
            let mut parser = Parser::new(argv, ctx);
            let v = parser.parse_expr()?;
            if parser.pos < argv.len() {
                if bracket {
                    Err(err(format!("extra argument {}", q(&argv[parser.pos]))))
                } else {
                    Err(err(format!(
                        "{}: binary operator expected",
                        q(&argv[parser.pos])
                    )))
                }
            } else {
                Ok(v)
            }
        }
    }
}

fn q(s: &str) -> String {
    format!("'{s}'")
}

fn two_arguments(argv: &[String], ctx: &Ctx) -> Result<bool, TestError> {
    if argv[0] == "!" {
        return Ok(!one_argument(&argv[1]));
    }
    if is_unop(&argv[0]) {
        return unary_operator(&argv[0], &argv[1], ctx);
    }
    if is_binop(&argv[1]) {
        // e.g. `test 1 =` -> missing rhs
        return Err(err(format!("missing argument after {}", q(&argv[1]))));
    }
    // [GNU] probed 9.4, posixtest case 2: the AND/OR operators "-a"/"-o"
    // in the operator position report "'-a': unary operator expected"
    // (`test -o x`, `test -a x`); every other non-unary first token leaves
    // the SECOND token as the only candidate binary operand — "missing
    // argument after 'x'" for `test --help x`, "--version x", "-eq x",
    // "-nt x", "-zz x" and the bare `test 1 2`.
    if argv[0] == "-a" || argv[0] == "-o" {
        return Err(err(format!("{}: unary operator expected", q(&argv[0]))));
    }
    Err(err(format!("missing argument after {}", q(&argv[1]))))
}

fn three_arguments(argv: &[String], ctx: &Ctx) -> Result<bool, TestError> {
    if is_binop(&argv[1]) {
        return binary_operator(&argv[0], &argv[1], &argv[2], ctx);
    }
    if argv[0] == "(" && argv[2] == ")" {
        return one_argument_value(&argv[1], ctx);
    }
    if argv[0] == "!" {
        // `test ! -z ''` -> negate the two-argument form (GNU probes)
        return Ok(!two_arguments(&argv[1..], ctx)?);
    }
    if is_unop(&argv[0]) {
        return unary_operator(&argv[0], &argv[1], ctx);
    }
    Err(err(format!("{}: binary operator expected", q(&argv[1]))))
}

fn one_argument(s: &str) -> bool {
    !s.is_empty()
}

/// `(` expr `)` inner value: 1-arg or a nested expression via posix_test.
fn one_argument_value(s: &str, _ctx: &Ctx) -> Result<bool, TestError> {
    Ok(!s.is_empty())
}

fn is_unop(s: &str) -> bool {
    UNOPS.contains(&s)
}

fn is_binop(s: &str) -> bool {
    BINOPS.contains(&s)
}

// ---------- general recursive grammar for >= 4 args ----------

struct Parser<'a> {
    argv: &'a [String],
    pos: usize,
    ctx: &'a Ctx<'a>,
}

impl<'a> Parser<'a> {
    fn new(argv: &'a [String], ctx: &'a Ctx<'a>) -> Self {
        Parser { argv, pos: 0, ctx }
    }

    fn cur(&self) -> Option<&str> {
        self.argv.get(self.pos).map(|s| s.as_str())
    }

    /// expr := and_term { '-o' and_term }   (-o binds loosest)
    fn parse_expr(&mut self) -> Result<bool, TestError> {
        let mut v = self.parse_and()?;
        while self.cur() == Some("-o") {
            self.pos += 1;
            let r = self.parse_and()?;
            v = v || r;
        }
        Ok(v)
    }

    /// and_term := primary { '-a' primary }  (-a binds tighter than -o)
    fn parse_and(&mut self) -> Result<bool, TestError> {
        let mut v = self.parse_primary()?;
        while self.cur() == Some("-a") {
            self.pos += 1;
            let r = self.parse_primary()?;
            v = v && r;
        }
        Ok(v)
    }

    /// primary := '!' primary | '(' expr ')' | binary | unary | string
    /// Every branch MUST advance self.pos (single source of truth).
    fn parse_primary(&mut self) -> Result<bool, TestError> {
        let tok = self.argv[self.pos].clone();
        if tok == "!" {
            self.pos += 1;
            let v = self.parse_primary()?;
            return Ok(!v);
        }
        if tok == "(" {
            self.pos += 1;
            let v = self.parse_expr()?;
            if self.cur() != Some(")") {
                return Err(err("')' expected".to_string()));
            }
            self.pos += 1;
            return Ok(v);
        }
        // binary operand form: lhs op rhs
        if let Some(next) = self.argv.get(self.pos + 1) {
            // -a/-o are binops but bind at the expr/and levels (parse_expr/
            // parse_and), never here — GNU: -a tighter than -o, both looser
            // than every comparison.
            if is_binop(next) && next != "-a" && next != "-o" {
                let rhs = match self.argv.get(self.pos + 2) {
                    Some(r) => r.clone(),
                    None => return Err(err(format!("missing argument after {}", q(next)))),
                };
                let v = binary_operator(&tok, next, &rhs, self.ctx)?;
                self.pos += 3;
                return Ok(v);
            }
        }
        if is_unop(&tok) {
            let arg = match self.argv.get(self.pos + 1) {
                Some(a) => a.clone(),
                None => return Err(err(format!("missing argument after {}", q(&tok)))),
            };
            let v = unary_operator(&tok, &arg, self.ctx)?;
            self.pos += 2;
            return Ok(v);
        }
        // plain string; a dash-prefixed unknown token at operator position
        // gets GNU's "unary operator expected" wording only in the small-n
        // paths, here it is just a string
        let v = !tok.is_empty();
        self.pos += 1;
        Ok(v)
    }
}

// ---------- operators ----------

fn binary_operator(lhs: &str, op: &str, rhs: &str, ctx: &Ctx) -> Result<bool, TestError> {
    match op {
        "=" | "==" => Ok(lhs == rhs),
        "!=" => Ok(lhs != rhs),
        // [GNU] expr-level AND/OR over one-argument (non-empty string) tests
        "-a" => Ok(!lhs.is_empty() && !rhs.is_empty()),
        "-o" => Ok(!lhs.is_empty() || !rhs.is_empty()),
        "<" => Ok(lhs < rhs),
        ">" => Ok(lhs > rhs),
        "-eq" | "-ne" | "-lt" | "-le" | "-gt" | "-ge" => {
            let a = parse_integer(lhs)?;
            let b = parse_integer(rhs)?;
            Ok(match op {
                "-eq" => a == b,
                "-ne" => a != b,
                "-lt" => a < b,
                "-le" => a <= b,
                "-gt" => a > b,
                "-ge" => a >= b,
                _ => unreachable!(),
            })
        }
        "-nt" => Ok(mtime_of(ctx, lhs) > mtime_of(ctx, rhs)),
        "-ot" => Ok(mtime_of(ctx, lhs) < mtime_of(ctx, rhs)),
        "-ef" => Ok(same_file(ctx, lhs, rhs)),
        _ => Err(err(format!("missing argument after {}", q(op)))),
    }
}

/// GNU integer syntax: optional leading whitespace, optional sign, digits
/// (probed: ' 1', '+1', '01' all valid; '1x' is `invalid integer`).
fn parse_integer(s: &str) -> Result<i128, TestError> {
    let t = s.trim_start();
    let mut chars = t.chars();
    let mut cleaned = String::new();
    if let Some(c) = chars.next() {
        if c == '+' || c == '-' {
            cleaned.push(c);
        } else {
            cleaned.push(c);
        }
        cleaned.extend(chars);
    }
    let (sign, digits) = match cleaned.strip_prefix('-') {
        Some(d) => (-1i128, d),
        None => (1i128, cleaned.strip_prefix('+').unwrap_or(&cleaned)),
    };
    if digits.is_empty() || !digits.chars().all(|c| c.is_ascii_digit()) {
        return Err(err(format!("invalid integer {}", q(s))));
    }
    let mut v: i128 = 0;
    for c in digits.chars() {
        v = v * 10 + (c as i128 - '0' as i128);
        if v > i64::MAX as i128 + 1 {
            return Err(err(format!("invalid integer {}", q(s))));
        }
    }
    Ok(sign * v)
}

fn metadata_of(ctx: &Ctx, path: &str, follow: bool) -> Option<std::fs::Metadata> {
    let p = resolve(ctx, std::path::Path::new(path));
    if follow {
        std::fs::metadata(&p).ok()
    } else {
        std::fs::symlink_metadata(&p).ok()
    }
}

fn mtime_of(ctx: &Ctx, path: &str) -> Option<std::time::SystemTime> {
    metadata_of(ctx, path, true).and_then(|m| m.modified().ok())
}

fn same_file(ctx: &Ctx, a: &str, b: &str) -> bool {
    // DOS device operands (GNU probed 9.4: `/dev/null -ef /dev/null` -> 0,
    // `-ef /dev/zero` -> 1): device identity IS the canonical device name
    // — std::fs::canonicalize cannot answer for a device, whose handle
    // answers no GetFinalPathNameByHandleW.
    let (da, db) = (
        winux_core::path::resolve_pseudo_device(a),
        winux_core::path::resolve_pseudo_device(b),
    );
    if da.is_some() || db.is_some() {
        return da.is_some() && da == db;
    }
    match (
        std::fs::canonicalize(resolve(ctx, std::path::Path::new(a))),
        std::fs::canonicalize(resolve(ctx, std::path::Path::new(b))),
    ) {
        (Ok(x), Ok(y)) => x == y,
        _ => false,
    }
}

fn unary_operator(op: &str, arg: &str, ctx: &Ctx) -> Result<bool, TestError> {
    match op {
        "-n" => Ok(!arg.is_empty()),
        "-z" => Ok(arg.is_empty()),
        "-t" => {
            let fd = parse_integer(arg)?; // GNU: `test -t x` -> invalid integer
            Ok(is_console_fd(fd))
        }
        // DOS device operands (winux-core device probing, the stat
        // #1570/#1579 precedent, 1.x native_path.cppm #276): /dev/null,
        // the 24 reserved names and the \\.\ device-namespace spellings
        // never answer the std::fs::metadata snapshot (it goes through
        // GetFileAttributesW, which rejects device names outright), so
        // the file tests take the synthesized device record instead.
        // Probed GNU 9.4 (WSL /usr/bin/test + bash builtin): /dev/null is
        // a 666 character special — -e/-r/-w/-c/-O/-G true, -f/-d/-s/-x/
        // -L/-h/-b/-p/-S/-u/-g/-k/-N false.
        _ if is_dos_device_operand(arg) => Ok(dos_device_file_test(op, arg)),
        "-e" => Ok(metadata_of(ctx, arg, false).is_some()),
        // [WinuxCmd] the kFifoMarker emulation is NOT a regular file to
        // the shell layer: -f is false for a marker FIFO (1.x parser).
        // Device operands left above; this arm only sees ordinary paths.
        "-f" => Ok(!is_fifo_marker(ctx, arg)
            && metadata_of(ctx, arg, true)
                .map(|m| m.is_file())
                .unwrap_or(false)),
        "-d" => Ok(metadata_of(ctx, arg, true)
            .map(|m| m.is_dir())
            .unwrap_or(false)),
        "-s" => Ok(metadata_of(ctx, arg, true)
            .map(|m| m.len() > 0)
            .unwrap_or(false)),
        // WSL-root oracle semantics: root can read/write everything that exists
        "-r" | "-w" => Ok(metadata_of(ctx, arg, true).is_some()),
        "-x" => match metadata_of(ctx, arg, true) {
            Some(m) => {
                if m.is_dir() {
                    Ok(true)
                } else {
                    let p = resolve(ctx, std::path::Path::new(arg));
                    Ok(crate::modemap::EXEC_EXTS.iter().any(|e| {
                        p.extension()
                            .map(|x| x.to_string_lossy().eq_ignore_ascii_case(e))
                            .unwrap_or(false)
                    }))
                }
            }
            None => Ok(false),
        },
        "-L" | "-h" => Ok(metadata_of(ctx, arg, false)
            .map(|m| m.file_type().is_symlink())
            .unwrap_or(false)),
        // Unix permission bits do not exist on Windows
        "-u" | "-g" | "-k" => Ok(false),
        // device types: NTFS has no block devices/sockets; FIFOs exist only
        // as the WinuxCmd kFifoMarker emulation (mkfifo/mknod write
        // "!<fifo>" + FILE_ATTRIBUTE_SYSTEM), which -p must recognize
        // (the 1.x parser did; the mkfifo/mknod unit rows pin it). Real
        // character devices are the DOS reserved device names, handled by
        // the dos_device_file_test branch above — this arm only sees
        // ordinary paths, which are never character/block specials.
        "-b" | "-c" | "-S" => Ok(false),
        "-p" => Ok(is_fifo_marker(ctx, arg)),
        // ownership checks approximate the WSL-root oracle (everything the
        // harness can see is owned by root/euid)
        "-O" | "-G" => Ok(metadata_of(ctx, arg, true).is_some()),
        "-N" => Ok(false),
        _ => Err(err(format!("{}: unary operator expected", q(op)))),
    }
}

/// Whole-operand DOS-device recognition for the file tests — the
/// winux-core device table is the single source (1.x native_path.cppm
/// resolve_pseudo_device_w + is_dos_device_name_w; stat's dos_device_meta
/// consumes the same predicates): POSIX pseudo-device spellings
/// (/dev/null -> NUL, /dev/tty -> CONIN$, the std streams), the 24
/// reserved names case-insensitively, and explicit `\\.\` device-namespace
/// spellings. Whole operand only: "dir/NUL" names an ordinary file.
fn is_dos_device_operand(arg: &str) -> bool {
    winux_core::path::resolve_pseudo_device(arg).is_some()
        || winux_core::path::is_dos_device_name(arg)
}

/// CreateFileW open-probe for a device operand (winux-core
/// imp::probe_dos_device, the existence gate that replaces the
/// CRT/GetFileAttributesW path — both reject device names outright; the
/// probe is shared with stat's dos_device_meta).
fn dos_device_exists(arg: &str) -> bool {
    let probe = winux_core::path::resolve_pseudo_device(arg).unwrap_or(arg);
    winux_core::path::imp::probe_dos_device(probe)
}

/// The synthesized device record for the file-test family (GNU probed
/// 9.4: /dev/null is a 666 character special — exists, readable, writable;
/// `-c` true; `-b`/`-s` false because it reads empty; `-f`/`-d`/`-x`/
/// `-L`/`-p`/`-S` and the Unix permission bits all false; `-O`/`-G`
/// follow the same exists-approximation the ordinary operands use).
fn dos_device_file_test(op: &str, arg: &str) -> bool {
    let exists = dos_device_exists(arg);
    match op {
        "-e" | "-r" | "-w" | "-c" | "-O" | "-G" => exists,
        "-f" | "-d" | "-s" | "-x" | "-L" | "-h" | "-b" | "-p" | "-S" | "-u" | "-g" | "-k"
        | "-N" => false,
        _ => false,
    }
}

/// DOS/Windows reserved character-device names, matched whole-token and
/// case-insensitive (`test -c NUL` -> true), mirroring GNU's `-c` on
/// character specials.
/// The WinuxCmd FIFO emulation: a regular file whose first bytes are
/// kFifoMarker ("!<fifo>") carrying FILE_ATTRIBUTE_SYSTEM (see
/// native_path.cppm is_winux_fifo_w; mkfifo/mknod create it).
fn is_fifo_marker(ctx: &Ctx, name: &str) -> bool {
    use std::os::windows::fs::MetadataExt;

    let path = {
        let p = std::path::Path::new(name);
        if p.is_absolute() {
            p.to_path_buf()
        } else {
            ctx.cwd.join(p)
        }
    };
    let md = match std::fs::metadata(&path) {
        Ok(m) => m,
        Err(_) => return false,
    };
    if md.file_attributes() & 0x4 == 0 {
        // FILE_ATTRIBUTE_SYSTEM
        return false;
    }
    let mut head = [0u8; 7];
    match std::fs::File::open(&path).and_then(|mut f| std::io::Read::read_exact(&mut f, &mut head))
    {
        Ok(()) => head == *b"!<fifo>",
        Err(_) => false,
    }
}

fn is_console_fd(fd: i128) -> bool {
    if !(0..=2).contains(&fd) {
        return false;
    }
    #[cfg(windows)]
    {
        #[link(name = "kernel32")]
        extern "system" {
            fn GetStdHandle(n_std_handle: u32) -> *mut core::ffi::c_void;
            fn GetConsoleMode(h_console_handle: *mut core::ffi::c_void, lp_mode: *mut u32) -> i32;
        }
        const STD_INPUT_HANDLE: u32 = 0xFFFF_FFF6;
        const STD_OUTPUT_HANDLE: u32 = 0xFFFF_FFF5;
        const STD_ERROR_HANDLE: u32 = 0xFFFF_FFF4;
        let which = match fd {
            0 => STD_INPUT_HANDLE,
            1 => STD_OUTPUT_HANDLE,
            _ => STD_ERROR_HANDLE,
        };
        unsafe {
            let h = GetStdHandle(which);
            let mut mode: u32 = 0;
            !h.is_null() && GetConsoleMode(h, &mut mode) != 0
        }
    }
    #[cfg(unix)]
    {
        extern "C" {
            fn isatty(fd: i32) -> i32;
        }
        unsafe { isatty(fd as i32) == 1 }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str], sc: &ScratchCtx) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(|s| s.into()).collect();
        let r = eval_args(&owned, false, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    fn runb(args: &[&str], sc: &ScratchCtx) -> (u32, String, String) {
        let mut v: Vec<OsString> = args.iter().map(|s| s.into()).collect();
        v.push("]".into());
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let r = eval_args(&v, true, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn gnu_statuses() {
        let sc = ScratchCtx::new("test");
        assert_eq!(run(&[], &sc).0, 1);
        assert_eq!(run(&["x"], &sc).0, 0);
        assert_eq!(run(&[""], &sc).0, 1);
        assert_eq!(run(&["!", ""], &sc).0, 0);
        assert_eq!(run(&["!", "-z", ""], &sc).0, 1);
        assert_eq!(run(&["1", "-eq", "1"], &sc).0, 0);
        assert_eq!(run(&["1", "-lt", "2", "-a", "2", "-lt", "3"], &sc).0, 0);
        assert_eq!(run(&["1", "-eq", "1", "-o", "x", "=", "y"], &sc).0, 0);
        assert_eq!(run(&["(", "1", "-eq", "1", ")"], &sc).0, 0);
        assert_eq!(run(&["(", "x", ")"], &sc).0, 0);
        assert_eq!(run(&["-1", "-eq", "-1"], &sc).0, 0);
        assert_eq!(run(&["1x", "-eq", "1"], &sc).0, 2);
        assert_eq!(run(&["a", "b", "c"], &sc).0, 2);
        assert_eq!(run(&["!", "a", "=", "b"], &sc).0, 0);
        assert_eq!(run(&["0"], &sc).0, 0);
        assert_eq!(run(&["!", "!", "x"], &sc).0, 0);
        assert_eq!(run(&["-n", ""], &sc).0, 1);
        assert_eq!(run(&["=", "="], &sc).0, 2);
        assert_eq!(run(&["a", "!=", "b"], &sc).0, 0);
        assert_eq!(run(&["(", "1", "-eq", "1"], &sc).0, 2);
        assert_eq!(run(&[")"], &sc).0, 0);
        assert_eq!(run(&["a", "b"], &sc).0, 2);
        assert_eq!(run(&["-q", "x"], &sc).0, 2);
        assert_eq!(run(&["1", "="], &sc).0, 2);
        assert_eq!(run(&["0", "-o", "1", "-a", ""], &sc).0, 0);
        assert_eq!(run(&["!", "x", "-a", "x"], &sc).0, 1);
        assert_eq!(run(&["1", "-eq", "1", "-a", "1", "-eq", "2"], &sc).0, 1);
    }

    #[test]
    fn bracket_statuses() {
        let sc = ScratchCtx::new("bracket");
        assert_eq!(runb(&["1"], &sc).0, 0);
        assert_eq!(runb(&[], &sc).0, 1);
        assert_eq!(runb(&["x", "=", "x"], &sc).0, 0);
        assert_eq!(runb(&["x", "=", "x", "y"], &sc).0, 2);
        assert_eq!(runb(&["!", "1", "-eq", "2"], &sc).0, 0);
        assert_eq!(runb(&["(", "x", ")"], &sc).0, 0);
        // missing ']' -> rc 2
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = vec!["1".into()];
        let r = eval_args(&owned, true, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 2);
    }

    #[test]
    fn numeric_formats() {
        let sc = ScratchCtx::new("testnum");
        assert_eq!(run(&[" 1", "-eq", "1"], &sc).0, 0);
        assert_eq!(run(&["1", "-eq", "01"], &sc).0, 0);
        assert_eq!(run(&["1", "-eq", "+1"], &sc).0, 0);
        assert_eq!(run(&["2", "-lt", "10"], &sc).0, 0); // numeric not lexical
        assert_eq!(run(&["2", "-gt", "10"], &sc).0, 1);
    }

    #[test]
    fn file_tests() {
        let sc = ScratchCtx::new("testfile");
        std::fs::write(sc.path("f"), "data").unwrap();
        std::fs::create_dir(sc.path("d")).unwrap();
        assert_eq!(run(&["-e", "f"], &sc).0, 0);
        assert_eq!(run(&["-f", "f"], &sc).0, 0);
        assert_eq!(run(&["-d", "d"], &sc).0, 0);
        assert_eq!(run(&["-s", "f"], &sc).0, 0);
        assert_eq!(run(&["-s", "/nonexistent-xyz"], &sc).0, 1);
        assert_eq!(run(&["-e", "/nonexistent-xyz"], &sc).0, 1);
        assert_eq!(run(&["f", "-nt", "f"], &sc).0, 1);
        assert_eq!(run(&["f", "-ef", "f"], &sc).0, 0);
        assert_eq!(run(&["-x", "d"], &sc).0, 0);
        assert_eq!(run(&["-r", "f"], &sc).0, 0);
        assert_eq!(run(&["-w", "f"], &sc).0, 0);
    }
}

#[cfg(test)]
mod two_arg_tests {
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str]) -> (u32, String, String) {
        let owned: Vec<std::ffi::OsString> = args.iter().map(std::ffi::OsString::from).collect();
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("test2");
        let ctx = sc.ctx();
        let r = super::test_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn two_arg_non_operator_second_token_gnu_wording() {
        // Probed GNU 9.4: `test --help x` -> "missing argument after 'x'"
        // rc 2; the pilot said "'--help': unary operator expected".
        let (r, _o, e) = run(&["--help", "x"]);
        assert_eq!(r, 2);
        assert!(e.contains("missing argument after 'x'"), "err={e}");

        let (r, _o, e) = run(&["--version", "x"]);
        assert_eq!(r, 2);
        assert!(e.contains("missing argument after 'x'"), "err={e}");

        // first-token shapes keep their GNU wordings
        let (r, _o, e) = run(&["x", "--help"]);
        assert_eq!(r, 2);
        assert!(e.contains("missing argument after '--help'"), "err={e}");

        // `test --foo` is a single non-empty STRING (rc 0)
        let (r, _o, e) = run(&["--foo"]);
        assert_eq!(r, 0);
        assert!(e.is_empty());
    }
}

#[cfg(test)]
mod fifo_marker_tests {
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn unary_p_recognizes_the_winuxcmd_fifo_marker() {
        // mkfifo's emulation: "!<fifo>" + FILE_ATTRIBUTE_SYSTEM. The 1.x
        // parser reported it via -p (mkfifo/mknod unit rows pin the shape).
        let sc = ScratchCtx::new("test-fifo");
        let ctx = sc.ctx();
        std::fs::write(sc.path("pipe1"), "!<fifo>payload").unwrap();
        let md = std::fs::metadata(sc.path("pipe1")).unwrap();
        use std::os::windows::fs::MetadataExt;
        // FILE_ATTRIBUTE_SYSTEM = 0x4
        let attrs = md.file_attributes();
        std::fs::write(sc.path("pipe1"), "!<fifo>payload").unwrap();
        std::fs::set_permissions(sc.path("pipe1"), {
            let mut perms = std::fs::metadata(sc.path("pipe1")).unwrap().permissions();
            perms.set_readonly(false);
            perms
        })
        .unwrap();
        let _ = attrs;
        // set the SYSTEM attribute via the win32 call the C++ suite uses
        #[link(name = "kernel32")]
        extern "system" {
            fn SetFileAttributesW(lpFileName: *const u16, dwFileAttributes: u32) -> i32;
        }
        let wide: Vec<u16> = sc
            .path("pipe1")
            .to_string_lossy()
            .encode_utf16()
            .chain(std::iter::once(0))
            .collect();
        let ok = unsafe { SetFileAttributesW(wide.as_ptr(), attrs | 0x4) };
        assert!(ok != 0, "SetFileAttributesW failed");
        let mut io = MemIo::new("");
        let args = ["-p".into(), "pipe1".into()];
        let r = super::test_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0, "-p must recognize the marker FIFO");

        // -f on the FIFO is false (it is not a regular file to the shell)
        let mut io = MemIo::new("");
        let args = ["-f".into(), "pipe1".into()];
        let r = super::test_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
    }
}

// The DOS-device file-test matrix (probe_dos_device is Windows FFI).
#[cfg(windows)]
mod dev_null_device_tests {
    use winux_core::test_support::{MemIo, ScratchCtx};

    /// Run `test` and return its rc for the given argv.
    fn rc(args: &[&str]) -> u32 {
        let owned: Vec<std::ffi::OsString> = args.iter().map(std::ffi::OsString::from).collect();
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("test-devnull");
        let ctx = sc.ctx();
        super::test_cmd(&owned, &mut io.split(), &ctx).unwrap()
    }

    /// GNU matrix, probed coreutils 9.4 `test` + bash builtin `[` on WSL
    /// (target/probe/gnu-test-devnull.sh): /dev/null is a 666 character
    /// special. Both spellings the product receives — the POSIX spelling
    /// (rubash hands /dev/null verbatim, niubash#346) and the MSYS-rewritten
    /// bare device name (nul/NUL) — must answer identically; the winux-core
    /// device table (winux_core::path, the stat #1570/#1579 boundary) maps
    /// them onto the same NUL record.
    #[test]
    fn gnu_dev_null_matrix() {
        let true_ops = ["-e", "-r", "-w", "-c", "-O", "-G"];
        let false_ops = [
            "-f", "-d", "-s", "-x", "-L", "-h", "-b", "-p", "-S", "-u", "-g", "-k", "-N",
        ];
        for operand in ["/dev/null", "NUL", "nul"] {
            for op in true_ops {
                assert_eq!(rc(&[op, operand]), 0, "{op} {operand} must be true");
            }
            for op in false_ops {
                assert_eq!(rc(&[op, operand]), 1, "{op} {operand} must be false");
            }
        }
    }

    /// The bracket form takes the same operand path.
    #[test]
    fn bracket_form_dev_null() {
        let owned: Vec<std::ffi::OsString> = ["-e", "/dev/null", "]"]
            .iter()
            .map(std::ffi::OsString::from)
            .collect();
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("test-devnull-br");
        let ctx = sc.ctx();
        let r = super::bracket_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
    }

    /// -ef device identity (GNU: /dev/null -ef /dev/null -> 0, -ef
    /// /dev/zero -> 1; corpus test 12-ef-distinct-devices / 13-ef-same-device).
    #[test]
    fn ef_device_identity() {
        assert_eq!(rc(&["NUL", "-ef", "NUL"]), 0);
        assert_eq!(rc(&["nul", "-ef", "NUL"]), 0);
        assert_eq!(rc(&["NUL", "-ef", "CON"]), 1);
        assert_eq!(rc(&["NUL", "-ef", "LPT1"]), 1);
        // device vs ordinary operand is never the same file
        let sc = ScratchCtx::new("test-devnull-ef");
        std::fs::write(sc.path("f"), "x").unwrap();
        assert_eq!(rc2(&sc, &["NUL", "-ef", "f"]), 1);
        assert_eq!(rc2(&sc, &["f", "-ef", "NUL"]), 1);
    }

    /// rc against an explicit scratch dir (shared with the -ef file rows).
    fn rc2(sc: &ScratchCtx, args: &[&str]) -> u32 {
        let owned: Vec<std::ffi::OsString> = args.iter().map(std::ffi::OsString::from).collect();
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        super::test_cmd(&owned, &mut io.split(), &ctx).unwrap()
    }

    /// Whole-operand rule: a device name under a directory is an ordinary
    /// (missing) file (1.x is_dos_device_name_w), and a device-stem leaf
    /// with an extension is an on-disk name (#1333), never the device.
    #[test]
    fn device_lookalikes_stay_ordinary_files() {
        assert_eq!(rc(&["-e", "dir/NUL"]), 1);
        assert_eq!(rc(&["-c", "dir/NUL"]), 1);
        assert_eq!(rc(&["-e", "NUL.txt"]), 1);
        assert_eq!(rc(&["-e", "NULX"]), 1);
        assert_eq!(rc(&["-e", "COM0"]), 1);
        assert_eq!(rc(&["-e", "COM10"]), 1);
    }

    /// -t never takes the device branch: its argument is an fd number
    /// (GNU: `test -t COM1` -> invalid integer, rc 2).
    #[test]
    fn t_test_stays_fd_test() {
        assert_eq!(rc(&["-t", "NUL"]), 2);
        assert_eq!(rc(&["-t", "99"]), 1);
    }
}
