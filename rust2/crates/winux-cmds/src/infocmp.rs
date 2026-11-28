//! `infocmp` and `tput` — ncurses terminfo utilities on the WinuxCmd
//! oracle-verified builtin terminfo table (#1460, #1462, #1488).
//!
//! Pure std-only implementation with explicit `Io`/`Ctx` and zero globals.

use std::ffi::OsString;
use std::io::Write;

use crate::argopt;
use winux_core::{usage_error, CmdResult, Ctx, Io};

#[derive(Clone, Copy, PartialEq, Eq)]
enum Colors {
    None,
    Xterm8,
    Colors256,
}

#[derive(Clone, Copy)]
struct TermCaps {
    bold: Option<&'static str>,
    sgr0: Option<&'static str>,
    clear: Option<&'static str>,
    civis: Option<&'static str>,
    cnorm: Option<&'static str>,
    el: Option<&'static str>,
    ed: Option<&'static str>,
    smcup: Option<&'static str>,
    rmcup: Option<&'static str>,
}

#[derive(Clone, Copy)]
struct TermEntry {
    caps: Option<&'static TermCaps>,
    colors: Colors,
    num_colors: i32,
    num_pairs: i32,
    num_cols: i32,
    num_lines: i32,
}

const XTERM_CAPS: TermCaps = TermCaps {
    bold: Some("\x1b[1m"),
    sgr0: Some("\x1b(B\x1b[m"),
    clear: Some("\x1b[H\x1b[2J\x1b[3J"),
    civis: Some("\x1b[?25l"),
    cnorm: Some("\x1b[?12l\x1b[?25h"),
    el: Some("\x1b[K"),
    ed: Some("\x1b[J"),
    smcup: Some("\x1b[?1049h\x1b[22;0;0t"),
    rmcup: Some("\x1b[?1049l\x1b[23;0;0t"),
};

const SCREEN_CAPS: TermCaps = TermCaps {
    bold: Some("\x1b[1m"),
    sgr0: Some("\x1b[m\x0f"),
    clear: Some("\x1b[H\x1b[J"),
    civis: Some("\x1b[?25l"),
    cnorm: Some("\x1b[34h\x1b[?25h"),
    el: Some("\x1b[K"),
    ed: Some("\x1b[J"),
    smcup: Some("\x1b[?1049h"),
    rmcup: Some("\x1b[?1049l"),
};

const TMUX_CAPS: TermCaps = TermCaps {
    bold: Some("\x1b[1m"),
    sgr0: Some("\x1b[m\x0f"),
    clear: Some("\x1b[H\x1b[J\x1b[3J"),
    civis: Some("\x1b[?25l"),
    cnorm: Some("\x1b[34h\x1b[?25h"),
    el: Some("\x1b[K"),
    ed: Some("\x1b[J"),
    smcup: Some("\x1b[?1049h"),
    rmcup: Some("\x1b[?1049l"),
};

const LINUX_CAPS: TermCaps = TermCaps {
    bold: Some("\x1b[1m"),
    sgr0: Some("\x1b[m\x0f"),
    clear: Some("\x1b[H\x1b[J\x1b[3J"),
    civis: Some("\x1b[?25l\x1b[?1c"),
    cnorm: Some("\x1b[?25h\x1b[?0c"),
    el: Some("\x1b[K"),
    ed: Some("\x1b[J"),
    smcup: None,
    rmcup: None,
};

const VT100_CAPS: TermCaps = TermCaps {
    bold: Some("\x1b[1m"),
    sgr0: Some("\x1b[m\x0f"),
    clear: Some("\x1b[H\x1b[J"),
    civis: None,
    cnorm: None,
    el: Some("\x1b[K"),
    ed: Some("\x1b[J"),
    smcup: None,
    rmcup: None,
};

const XTERM_ENTRY: TermEntry = TermEntry {
    caps: Some(&XTERM_CAPS),
    colors: Colors::Xterm8,
    num_colors: 8,
    num_pairs: 64,
    num_cols: 80,
    num_lines: 24,
};
const XTERM256_ENTRY: TermEntry = TermEntry {
    caps: Some(&XTERM_CAPS),
    colors: Colors::Colors256,
    num_colors: 256,
    num_pairs: 65536,
    num_cols: 80,
    num_lines: 24,
};
const SCREEN_ENTRY: TermEntry = TermEntry {
    caps: Some(&SCREEN_CAPS),
    colors: Colors::Xterm8,
    num_colors: 8,
    num_pairs: 64,
    num_cols: 80,
    num_lines: 24,
};
const SCREEN256_ENTRY: TermEntry = TermEntry {
    caps: Some(&SCREEN_CAPS),
    colors: Colors::Colors256,
    num_colors: 256,
    num_pairs: 65536,
    num_cols: 80,
    num_lines: 24,
};
const TMUX_ENTRY: TermEntry = TermEntry {
    caps: Some(&TMUX_CAPS),
    colors: Colors::Colors256,
    num_colors: 256,
    num_pairs: 65536,
    num_cols: 80,
    num_lines: 24,
};
const LINUX_ENTRY: TermEntry = TermEntry {
    caps: Some(&LINUX_CAPS),
    colors: Colors::Xterm8,
    num_colors: 8,
    num_pairs: 64,
    num_cols: -1,
    num_lines: -1,
};
const VT100_ENTRY: TermEntry = TermEntry {
    caps: Some(&VT100_CAPS),
    colors: Colors::None,
    num_colors: -1,
    num_pairs: -1,
    num_cols: 80,
    num_lines: 24,
};
const DUMB_ENTRY: TermEntry = TermEntry {
    caps: None,
    colors: Colors::None,
    num_colors: -1,
    num_pairs: -1,
    num_cols: 80,
    num_lines: -1,
};

const SETAF_TEMPLATE: &str = "\x1b[%?%p1%{8}%<%t3%p1%d%e%p1%{16}%<%t9%p1%{8}%-%d%e38;5;%p1%d%;m";
const SETAB_TEMPLATE: &str = "\x1b[%?%p1%{8}%<%t4%p1%d%e%p1%{16}%<%t10%p1%{8}%-%d%e48;5;%p1%d%;m";
const CUP_TEMPLATE: &str = "\x1b[%i%p1%d;%p2%dH";

struct NamedTerm {
    name: &'static str,
    description: &'static str,
    entry: &'static TermEntry,
}

const NAMED_TERMS: [NamedTerm; 8] = [
    NamedTerm {
        name: "xterm",
        description: "xterm terminal emulator",
        entry: &XTERM_ENTRY,
    },
    NamedTerm {
        name: "xterm-256color",
        description: "xterm with 256 colors",
        entry: &XTERM256_ENTRY,
    },
    NamedTerm {
        name: "screen",
        description: "VT 100/ANSI X3.64 virtual terminal",
        entry: &SCREEN_ENTRY,
    },
    NamedTerm {
        name: "screen-256color",
        description: "screen with 256 colors",
        entry: &SCREEN256_ENTRY,
    },
    NamedTerm {
        name: "tmux-256color",
        description: "tmux with 256 colors",
        entry: &TMUX_ENTRY,
    },
    NamedTerm {
        name: "linux",
        description: "Linux console",
        entry: &LINUX_ENTRY,
    },
    NamedTerm {
        name: "vt100",
        description: "DEC VT100",
        entry: &VT100_ENTRY,
    },
    NamedTerm {
        name: "dumb",
        description: "dumb terminal",
        entry: &DUMB_ENTRY,
    },
];

fn find_term(term: &str) -> Option<&'static NamedTerm> {
    NAMED_TERMS.iter().find(|n| n.name == term)
}

fn lookup_term(term: &str) -> TermEntry {
    find_term(term).map(|n| *n.entry).unwrap_or(XTERM_ENTRY)
}

fn expand_bytes(raw: &[u8]) -> String {
    let mut out = String::new();
    for &ch in raw {
        match ch {
            0x1b => out.push_str("\\E"),
            b'\n' => out.push_str("\\n"),
            b'\r' => out.push_str("\\r"),
            b'\t' => out.push_str("\\t"),
            0x08 => out.push_str("\\b"),
            0x0c => out.push_str("\\f"),
            0x7f => out.push_str("^?"),
            0..=0x1f => {
                out.push('^');
                out.push((ch + b'@') as char);
            }
            _ => out.push(ch as char),
        }
    }
    out
}

fn render_infocmp(
    name: &str,
    description: &str,
    bools: &[&str],
    nums: &[(&str, i32)],
    strs: &[(&str, &str)],
    one_per_line: bool,
) -> String {
    let mut caps = Vec::new();
    for &b in bools {
        caps.push(b.to_owned());
    }
    for &(n, v) in nums {
        caps.push(format!("{n}#{v}"));
    }
    for &(s, raw) in strs {
        caps.push(format!("{s}={}", expand_bytes(raw.as_bytes())));
    }

    let mut out = format!(
        "#\tReconstructed via infocmp from file: builtin terminfo table\n{name}|{description},\n"
    );
    if one_per_line {
        for (i, cap) in caps.iter().enumerate() {
            let comma = if i + 1 < caps.len() { "," } else { "" };
            out.push_str(&format!("\t{cap}{comma}\n"));
        }
        return out;
    }

    let mut line = String::from("\t");
    for (i, cap) in caps.iter().enumerate() {
        let comma = if i + 1 < caps.len() { "," } else { "" };
        let piece = format!("{cap}{comma}");
        if line.len() > 1 && line.len() + 1 + piece.len() > 79 {
            out.push_str(&line);
            out.push('\n');
            line = String::from("\t");
        }
        if line.len() > 1 {
            line.push(' ');
        }
        line.push_str(&piece);
    }
    out.push_str(&line);
    out.push('\n');
    out
}

pub fn infocmp_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut one_per_line = false;
    let mut positionals: Vec<String> = Vec::new();
    let mut no_more_opts = false;

    for a in args {
        let s = a.to_string_lossy();
        if no_more_opts {
            positionals.push(s.into_owned());
            continue;
        }
        if s == "--" {
            no_more_opts = true;
            continue;
        }
        if s == "-h" || s == "--help" {
            writeln!(
                io.stdout,
                "Usage: infocmp [-1] [TERMNAME]\nPrint the terminfo description of a terminal."
            )?;
            return Ok(0);
        }
        if s == "-V" || s == "--version" {
            writeln!(io.stdout, "ncurses 6.6.20251230")?;
            return Ok(0);
        }
        if s == "-1" {
            one_per_line = true;
            continue;
        }
        if s.starts_with('-') && s.len() > 1 {
            let msg = if let Some(rest) = s.strip_prefix("--") {
                argopt::unrecognized_option(ctx, &format!("--{rest}"))
            } else {
                let c = s.chars().nth(1).unwrap();
                argopt::invalid_option(ctx, &c.to_string())
            };
            let _ = usage_error(ctx, io, "infocmp", &msg);
            return Ok(1);
        }
        positionals.push(s.into_owned());
    }

    if positionals.len() > 1 {
        writeln!(
            io.stderr,
            "infocmp: comparing multiple terminals is not supported on Windows (no terminfo database)"
        )?;
        return Ok(1);
    }

    let term = if let Some(first) = positionals.first() {
        first.clone()
    } else if let Some(env_term) = ctx.env.get("TERM") {
        env_term.to_string_lossy().into_owned()
    } else {
        String::new()
    };

    if term.is_empty() {
        writeln!(
            io.stderr,
            "infocmp: No value for $TERM and no terminal specified"
        )?;
        return Ok(1);
    }

    if term == "windows-ansi" {
        let rendered = render_infocmp(
            "windows-ansi",
            "Windows ANSI Terminal",
            &["am"],
            &[("cols", 80), ("it", 8), ("lines", 24)],
            &[
                ("cr", "\r"),
                ("clear", "\x1b[H\x1b[2J"),
                ("smso", "\x1b[7m"),
                ("smul", "\x1b[4m"),
                ("sgr0", "\x1b[0m"),
            ],
            one_per_line,
        );
        io.stdout.write_all(rendered.as_bytes())?;
        return Ok(0);
    }

    let Some(named) = find_term(&term) else {
        writeln!(io.stderr, "infocmp: couldn't open terminfo file {term}.")?;
        return Ok(1);
    };

    let t = named.entry;
    let mut nums = Vec::new();
    if t.num_cols >= 0 {
        nums.push(("cols", t.num_cols));
    }
    if t.num_lines >= 0 {
        nums.push(("lines", t.num_lines));
    }
    if t.num_colors >= 0 {
        nums.push(("colors", t.num_colors));
    }
    if t.num_pairs >= 0 {
        nums.push(("pairs", t.num_pairs));
    }
    let mut strs = Vec::new();
    if let Some(c) = t.caps {
        if let Some(v) = c.clear {
            strs.push(("clear", v));
        }
        if let Some(v) = c.el {
            strs.push(("el", v));
        }
        if let Some(v) = c.ed {
            strs.push(("ed", v));
        }
        strs.push(("cup", CUP_TEMPLATE));
        if let Some(v) = c.civis {
            strs.push(("civis", v));
        }
        if let Some(v) = c.cnorm {
            strs.push(("cnorm", v));
        }
        if let Some(v) = c.bold {
            strs.push(("bold", v));
        }
        if let Some(v) = c.smcup {
            strs.push(("smcup", v));
        }
        if let Some(v) = c.sgr0 {
            strs.push(("sgr0", v));
        }
        if let Some(v) = c.rmcup {
            strs.push(("rmcup", v));
        }
        if t.colors != Colors::None {
            strs.push(("setab", SETAB_TEMPLATE));
            strs.push(("setaf", SETAF_TEMPLATE));
        }
    }

    let rendered = render_infocmp(named.name, named.description, &[], &nums, &strs, one_per_line);
    io.stdout.write_all(rendered.as_bytes())?;
    Ok(0)
}

#[cfg(windows)]
fn apply_console_size(entry: &mut TermEntry) {
    #[repr(C)]
    struct Coord {
        x: i16,
        y: i16,
    }
    #[repr(C)]
    struct SmallRect {
        left: i16,
        top: i16,
        right: i16,
        bottom: i16,
    }
    #[repr(C)]
    struct ConsoleScreenBufferInfo {
        size: Coord,
        cursor: Coord,
        attrs: u16,
        window: SmallRect,
        max_window: Coord,
    }
    #[link(name = "kernel32")]
    extern "system" {
        fn GetStdHandle(n_std_handle: u32) -> isize;
        fn GetConsoleScreenBufferInfo(
            h_console_output: isize,
            lp_info: *mut ConsoleScreenBufferInfo,
        ) -> i32;
    }
    const STD_OUTPUT_HANDLE: u32 = (-11i32) as u32;
    let h = unsafe { GetStdHandle(STD_OUTPUT_HANDLE) };
    let mut info = ConsoleScreenBufferInfo {
        size: Coord { x: 0, y: 0 },
        cursor: Coord { x: 0, y: 0 },
        attrs: 0,
        window: SmallRect {
            left: 0,
            top: 0,
            right: 0,
            bottom: 0,
        },
        max_window: Coord { x: 0, y: 0 },
    };
    if unsafe { GetConsoleScreenBufferInfo(h, &mut info) } != 0 {
        entry.num_cols = (info.window.right - info.window.left + 1) as i32;
        entry.num_lines = (info.window.bottom - info.window.top + 1) as i32;
    }
}

#[cfg(not(windows))]
fn apply_console_size(_entry: &mut TermEntry) {}

fn parse_operand(arg: &str) -> Option<i64> {
    let bytes = arg.trim_start().as_bytes();
    if bytes.is_empty() {
        return None;
    }
    let mut idx = 0usize;
    let mut sign = 1i64;
    if bytes[idx] == b'+' {
        idx += 1;
    } else if bytes[idx] == b'-' {
        sign = -1;
        idx += 1;
    }
    if idx >= bytes.len() {
        return None;
    }
    let (radix, start) = if bytes[idx] == b'0' && idx + 1 < bytes.len() && (bytes[idx + 1] == b'x' || bytes[idx + 1] == b'X') {
        (16u32, idx + 2)
    } else if bytes[idx] == b'0' {
        (8u32, idx)
    } else {
        (10u32, idx)
    };
    let mut end = start;
    while end < bytes.len() {
        let b = bytes[end];
        let ok = match radix {
            8 => (b'0'..=b'7').contains(&b),
            10 => b.is_ascii_digit(),
            16 => b.is_ascii_hexdigit(),
            _ => false,
        };
        if !ok {
            break;
        }
        end += 1;
    }
    if end == start {
        if radix == 16 && idx < bytes.len() && bytes[idx] == b'0' {
            return Some(0);
        }
        return None;
    }
    let digits = std::str::from_utf8(&bytes[start..end]).ok()?;
    i64::from_str_radix(digits, radix).ok().map(|v| sign * v)
}

fn format_color(cap: &str, colors: Colors, n: i64) -> String {
    let is_fg = cap == "setaf";
    if colors == Colors::Xterm8 || n < 8 {
        format!("\x1b[{}{}m", if is_fg { '3' } else { '4' }, n)
    } else if n <= 15 {
        format!("\x1b[{}m", (if is_fg { 90 } else { 100 }) + n - 8)
    } else {
        format!("\x1b[{}{}m", if is_fg { "38;5;" } else { "48;5;" }, n)
    }
}

pub fn tput_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut term_override: Option<String> = None;
    let mut positionals: Vec<String> = Vec::new();
    let mut no_more_opts = false;
    let mut i = 0usize;

    while i < args.len() {
        let s = args[i].to_string_lossy();
        if no_more_opts {
            positionals.push(s.into_owned());
            i += 1;
            continue;
        }
        if s == "--" {
            no_more_opts = true;
            i += 1;
            continue;
        }
        if s == "-V" || s == "--version" {
            writeln!(io.stdout, "ncurses 6.6.20251230")?;
            return Ok(0);
        }
        if s == "-T" {
            i += 1;
            if i >= args.len() {
                let _ = usage_error(
                    ctx,
                    io,
                    "tput",
                    &argopt::short_option_requires_argument(ctx, "T"),
                );
                return Ok(2);
            }
            term_override = Some(args[i].to_string_lossy().into_owned());
            i += 1;
            continue;
        }
        if let Some(rest) = s.strip_prefix("-T") {
            if !rest.is_empty() {
                term_override = Some(rest.to_owned());
                i += 1;
                continue;
            }
        }
        if s == "-v" || s == "-x" {
            i += 1;
            continue;
        }
        if s == "--help" {
            writeln!(io.stdout, "Usage: tput [options] [command]")?;
            return Ok(0);
        }
        if s.starts_with('-') && s.len() > 1 {
            let msg = if let Some(rest) = s.strip_prefix("--") {
                argopt::unrecognized_option(ctx, &format!("--{rest}"))
            } else {
                let c = s.chars().nth(1).unwrap();
                argopt::invalid_option(ctx, &c.to_string())
            };
            let _ = usage_error(ctx, io, "tput", &msg);
            return Ok(2);
        }
        positionals.push(s.into_owned());
        i += 1;
    }

    if positionals.is_empty() {
        writeln!(io.stderr, "Usage: tput [options] [command]")?;
        return Ok(2);
    }

    let term = term_override.unwrap_or_else(|| {
        ctx.env
            .get("TERM")
            .map(|v| v.to_string_lossy().into_owned())
            .unwrap_or_default()
    });
    if term.is_empty() {
        writeln!(io.stderr, "tput: No value for $TERM and no -T specified")?;
        return Ok(2);
    }

    let mut entry = lookup_term(&term);
    apply_console_size(&mut entry);

    let mut idx = 0usize;
    while idx < positionals.len() {
        let cap = positionals[idx].as_str();
        let num_val = match cap {
            "colors" => Some(entry.num_colors),
            "pairs" => Some(entry.num_pairs),
            "cols" => Some(entry.num_cols),
            "lines" => Some(entry.num_lines),
            _ => None,
        };
        if let Some(v) = num_val {
            writeln!(io.stdout, "{v}")?;
            idx += 1;
            continue;
        }

        let parameterized = matches!(cap, "setaf" | "setab" | "cup");
        let is_no_param = matches!(
            cap,
            "bold" | "sgr0" | "clear" | "el" | "ed" | "civis" | "cnorm" | "smcup" | "rmcup"
        );
        if !parameterized && !is_no_param && cap != "reset" {
            writeln!(io.stderr, "tput: unknown terminfo capability '{cap}'")?;
            return Ok(4);
        }

        let Some(caps) = entry.caps else {
            return Ok(1);
        };

        if is_no_param {
            let slot = match cap {
                "bold" => caps.bold,
                "sgr0" => caps.sgr0,
                "clear" => caps.clear,
                "el" => caps.el,
                "ed" => caps.ed,
                "civis" => caps.civis,
                "cnorm" => caps.cnorm,
                "smcup" => caps.smcup,
                "rmcup" => caps.rmcup,
                _ => None,
            };
            let Some(bytes) = slot else {
                return Ok(1);
            };
            io.stdout.write_all(bytes.as_bytes())?;
            idx += 1;
            continue;
        }

        if cap == "reset" {
            io.stdout.write_all(b"\x1bc")?;
            idx += 1;
            continue;
        }

        let max_operands = if cap == "cup" { 2 } else { 1 };
        let mut operands = [0i64; 2];
        let mut consumed = 1usize;
        while consumed <= max_operands && idx + consumed < positionals.len() {
            let Some(v) = parse_operand(&positionals[idx + consumed]) else {
                break;
            };
            operands[consumed - 1] = v;
            consumed += 1;
        }
        let has_next = idx + 1 < positionals.len();

        if cap == "cup" {
            if consumed == 1 && !has_next {
                io.stdout.write_all(CUP_TEMPLATE.as_bytes())?;
            } else {
                let s = format!("\x1b[{};{}H", operands[0] + 1, operands[1] + 1);
                io.stdout.write_all(s.as_bytes())?;
            }
            idx += consumed;
            continue;
        }

        if consumed == 1 && !has_next {
            let tmpl = if cap == "setaf" {
                SETAF_TEMPLATE
            } else {
                SETAB_TEMPLATE
            };
            io.stdout.write_all(tmpl.as_bytes())?;
            idx += 1;
            continue;
        }
        if entry.colors == Colors::None {
            return Ok(1);
        }
        let colored = format_color(cap, entry.colors, operands[0]);
        io.stdout.write_all(colored.as_bytes())?;
        idx += consumed;
    }

    Ok(0)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn infocmp_dumps_known_and_windows_ansi() {
        let mut sc = ScratchCtx::new("infocmp-test");
        sc.env.set("TERM", "vt100");
        let ctx = sc.ctx();

        let mut io = MemIo::new("");
        let rc = infocmp_cmd(&[], &mut io.split(), &ctx).unwrap();
        assert_eq!(rc, 0);
        assert!(io.out().contains("vt100|DEC VT100,"));

        let mut io_wa = MemIo::new("");
        let rc_wa = infocmp_cmd(&[OsString::from("windows-ansi")], &mut io_wa.split(), &ctx).unwrap();
        assert_eq!(rc_wa, 0);
        assert!(io_wa.out().contains("Windows ANSI Terminal"));

        let mut io_unk = MemIo::new("");
        let rc_unk = infocmp_cmd(&[OsString::from("no-such-term")], &mut io_unk.split(), &ctx).unwrap();
        assert_eq!(rc_unk, 1);
        assert!(io_unk.err().contains("couldn't open terminfo file"));
    }

    #[test]
    fn tput_caps_colors_and_errors() {
        let mut sc = ScratchCtx::new("tput-test");
        sc.env.set("TERM", "xterm-256color");
        let ctx = sc.ctx();

        let mut io = MemIo::new("");
        let rc = tput_cmd(&[OsString::from("clear")], &mut io.split(), &ctx).unwrap();
        assert_eq!(rc, 0);
        assert_eq!(io.out(), "\x1b[H\x1b[2J\x1b[3J");

        let mut io_c = MemIo::new("");
        let rc_c = tput_cmd(
            &[OsString::from("setaf"), OsString::from("9")],
            &mut io_c.split(),
            &ctx,
        )
        .unwrap();
        assert_eq!(rc_c, 0);
        assert_eq!(io_c.out(), "\x1b[91m");

        let mut io_bad = MemIo::new("");
        let rc_bad = tput_cmd(&[OsString::from("boguscap")], &mut io_bad.split(), &ctx).unwrap();
        assert_eq!(rc_bad, 4);
    }
}
