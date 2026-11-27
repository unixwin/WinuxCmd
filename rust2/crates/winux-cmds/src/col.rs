//! `col` — util-linux col.c semantics, ported for WinuxCmd 2.0 (#1488).
//!
//! Filters reverse (and half-reverse) line feeds from stdin using the
//! half-line `VecDeque<Row>` model, tab compression, and explicit `Io`/`Ctx`.

use std::collections::VecDeque;
use std::ffi::OsString;
use std::io::Write;

use crate::argopt;
use winux_core::{usage_error, CmdResult, Ctx, Io};

#[derive(Clone, Copy)]
struct ColChar {
    column: usize,
    ch: u8,
    width: i32,
    alternate: bool,
}

#[derive(Default)]
struct Row {
    chars: Vec<ColChar>,
}

struct Config {
    no_backspaces: bool,
    fine: bool,
    pass_unknown: bool,
    compress_spaces: bool,
    max_bufd_lines: usize,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            no_backspaces: false,
            fine: false,
            pass_unknown: false,
            compress_spaces: true,
            max_bufd_lines: 64,
        }
    }
}

fn parse_lines_value(raw: &str) -> Option<usize> {
    let s = raw.strip_prefix('+').unwrap_or(raw);
    if s.is_empty() || !s.bytes().all(|b| b.is_ascii_digit()) {
        return None;
    }
    let val = s.parse::<u64>().ok()?;
    if val > 0xFFFF_FFFF {
        return None;
    }
    Some((val as usize).saturating_mul(2))
}

fn take_val(inline: Option<String>, args: &[OsString], idx: &mut usize) -> Option<String> {
    if let Some(v) = inline {
        return Some(v);
    }
    if *idx + 1 < args.len() {
        *idx += 1;
        return Some(args[*idx].to_string_lossy().into_owned());
    }
    None
}

struct Filter<'a> {
    cfg: &'a Config,
    rows: VecDeque<Row>,
    origin: i64,
    node_line: i64,
    total_flushed: i64,
    nblank: i64,
    cur_line: i64,
    max_line: i64,
    cur_col: i64,
    cur_alt: bool,
    last_alt: bool,
    have_last: bool,
    last_width: i32,
    warned: bool,
    out: Vec<u8>,
}

impl<'a> Filter<'a> {
    fn new(cfg: &'a Config) -> Self {
        Self {
            cfg,
            rows: VecDeque::new(),
            origin: 0,
            node_line: 0,
            total_flushed: 0,
            nblank: 0,
            cur_line: 0,
            max_line: 0,
            cur_col: 0,
            cur_alt: false,
            last_alt: false,
            have_last: false,
            last_width: 1,
            warned: false,
            out: Vec::new(),
        }
    }

    fn flush_blanks(&mut self) {
        let mut nb = self.nblank;
        let mut half = false;
        if (nb & 1) != 0 {
            if self.cfg.fine {
                half = true;
            } else {
                nb += 1;
            }
        }
        nb /= 2;
        for _ in 0..nb {
            self.out.push(b'\n');
        }
        if half {
            self.out.push(0x1B);
            self.out.push(b'9');
            if nb == 0 {
                self.out.push(b'\r');
            }
        }
        self.nblank = 0;
    }

    fn emit_char(&mut self, c: ColChar) {
        if c.alternate != self.last_alt {
            self.out.push(if c.alternate { 0x0E } else { 0x0F });
            self.last_alt = c.alternate;
        }
        self.out.push(c.ch);
    }

    fn flush_line(&mut self, row: &mut Row) {
        row.chars.sort_by_key(|c| c.column);
        let mut last_col = 0usize;
        let mut i = 0usize;
        while i < row.chars.len() {
            let mut j = i;
            while j < row.chars.len() && row.chars[j].column == row.chars[i].column {
                j += 1;
            }
            let this_col = row.chars[i].column;
            if last_col < this_col {
                let mut nspace = this_col - last_col;
                if self.cfg.compress_spaces && nspace > 1 {
                    let ntabs = (this_col / 8).saturating_sub(last_col / 8);
                    if ntabs > 0 {
                        nspace = this_col & 7;
                        for _ in 0..ntabs {
                            self.out.push(b'\t');
                        }
                    }
                }
                for _ in 0..nspace {
                    self.out.push(b' ');
                }
                last_col = this_col;
            }
            if self.cfg.no_backspaces {
                let c = row.chars[j - 1];
                self.emit_char(c);
                if c.width > 0 {
                    last_col += c.width as usize;
                }
            } else {
                for k in i..j {
                    let c = row.chars[k];
                    self.emit_char(c);
                    if k + 1 < j && c.width > 0 {
                        for _ in 0..c.width {
                            self.out.push(b'\x08');
                        }
                    }
                }
                let last = row.chars[j - 1];
                if last.width > 0 {
                    last_col += last.width as usize;
                }
            }
            i = j;
        }
    }

    fn flush_rows(&mut self, mut count: i64) {
        while count > 0 {
            count -= 1;
            let Some(mut row) = self.rows.pop_front() else {
                break;
            };
            self.origin += 1;
            self.total_flushed += 1;
            if !row.chars.is_empty() {
                self.flush_blanks();
                self.flush_line(&mut row);
            }
            self.nblank += 1;
        }
    }

    fn ensure_store(&mut self, stderr: &mut dyn Write) {
        let mut t = self.cur_line;
        if !self.cfg.fine && (t & 1) != 0 {
            t += 1;
        }
        if t < self.origin {
            if self.total_flushed == 0 {
                while self.origin > t {
                    self.rows.push_front(Row::default());
                    self.origin -= 1;
                }
            } else {
                if !self.warned {
                    let msg = if self.cur_line < 0 {
                        "col: warning: can't back up past first line."
                    } else {
                        "col: warning: can't back up -- line already flushed."
                    };
                    let _ = writeln!(stderr, "{msg}");
                    self.warned = true;
                }
                t = self.node_line;
            }
        }
        while t >= self.origin + (self.rows.len() as i64) {
            self.rows.push_back(Row::default());
        }
        self.node_line = t;
        let nmove = self.node_line - self.origin;
        if nmove > 0 && (self.cfg.max_bufd_lines as i64) + 32 <= nmove {
            self.flush_rows(nmove - (self.cfg.max_bufd_lines as i64));
        }
    }

    fn store(&mut self, ch: u8, width: i32, stderr: &mut dyn Write) {
        self.ensure_store(stderr);
        let idx = (self.node_line - self.origin) as usize;
        let col = if self.cur_col > 0 {
            self.cur_col as usize
        } else {
            0
        };
        self.rows[idx].chars.push(ColChar {
            column: col,
            ch,
            width,
            alternate: self.cur_alt,
        });
        self.have_last = true;
        self.last_width = width;
        if width > 0 {
            self.cur_col += width as i64;
        }
    }

    fn forward(&mut self, delta: i64) {
        self.cur_line += delta;
        if self.cur_line > 0 && self.max_line < self.cur_line {
            self.max_line = self.cur_line;
        }
    }

    fn run(&mut self, input: &[u8], stderr: &mut dyn Write) {
        self.rows.push_back(Row::default());
        let mut i = 0usize;
        while i < input.len() {
            let ch = input[i];
            match ch {
                0x08 => {
                    if self.cur_col != 0 {
                        let step = if self.have_last {
                            self.last_width as i64
                        } else {
                            1
                        };
                        self.cur_col -= step;
                    }
                }
                b'\r' => self.cur_col = 0,
                0x1B => {
                    if i + 1 >= input.len() {
                        break;
                    }
                    i += 1;
                    match input[i] {
                        0x07 => self.cur_line -= 2,
                        0x08 => self.cur_line -= 1,
                        b'\t' => self.forward(1),
                        _ => {}
                    }
                }
                b'\n' => {
                    self.forward(2);
                    self.cur_col = 0;
                }
                b' ' => self.cur_col += 1,
                0x0E => self.cur_alt = true,
                0x0F => self.cur_alt = false,
                b'\t' => {
                    self.cur_col |= 7;
                    self.cur_col += 1;
                }
                0x0B => self.cur_line -= 2,
                0x0C => {}
                _ => {
                    if ch >= 0x21 && ch != 0x7F {
                        self.store(ch, 1, stderr);
                    } else if self.cfg.pass_unknown {
                        self.store(ch, -1, stderr);
                    }
                }
            }
            i += 1;
        }

        if self.max_line == 0 && self.cur_col == 0 {
            return;
        }
        let last_line = if self.rows.is_empty() {
            self.node_line
        } else {
            self.origin + (self.rows.len() as i64) - 1
        };
        let n = self.rows.len() as i64;
        self.flush_rows(n);
        if self.last_alt {
            self.out.push(0x0F);
        }
        self.nblank = self.max_line - last_line;
        if (self.max_line & 1) != 0 {
            self.nblank += 1;
        } else if self.nblank == 0 {
            self.nblank = 2;
        }
        self.flush_blanks();
    }
}

pub fn col_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut cfg = Config::default();
    let mut seen_tabs = false;
    let mut seen_spaces = false;
    let mut positionals: Vec<String> = Vec::new();
    let mut idx = 0usize;

    while idx < args.len() {
        let arg = args[idx].to_string_lossy().into_owned();
        if arg == "--" {
            idx += 1;
            for a in &args[idx..] {
                positionals.push(a.to_string_lossy().into_owned());
            }
            break;
        }
        if arg == "-" || !arg.starts_with('-') {
            positionals.push(arg);
            idx += 1;
            continue;
        }
        if let Some(long) = arg.strip_prefix("--") {
            let (name, inline) = match long.split_once('=') {
                Some((n, v)) => (n, Some(v.to_string())),
                None => (long, None),
            };
            match name {
                "no-backspaces" => cfg.no_backspaces = true,
                "fine" => cfg.fine = true,
                "pass" => cfg.pass_unknown = true,
                "tabs" => {
                    if seen_spaces {
                        let _ = writeln!(io.stderr, "col: mutually exclusive arguments: --tabs --spaces");
                        return Ok(1);
                    }
                    seen_tabs = true;
                    cfg.compress_spaces = true;
                }
                "spaces" => {
                    if seen_tabs {
                        let _ = writeln!(io.stderr, "col: mutually exclusive arguments: --tabs --spaces");
                        return Ok(1);
                    }
                    seen_spaces = true;
                    cfg.compress_spaces = false;
                }
                "lines" => {
                    let Some(v) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(ctx, io, "col", &argopt::option_requires_argument(ctx, "--lines"));
                        return Ok(1);
                    };
                    match parse_lines_value(&v) {
                        Some(n) => cfg.max_bufd_lines = n,
                        None => {
                            let _ = writeln!(io.stderr, "col: bad -l argument: '{v}'");
                            return Ok(1);
                        }
                    }
                }
                "help" | "version" => return Ok(0),
                _ => {
                    let _ = usage_error(ctx, io, "col", &argopt::unrecognized_option(ctx, &arg));
                    return Ok(1);
                }
            }
            idx += 1;
            continue;
        }

        let chars: Vec<char> = arg.chars().skip(1).collect();
        let mut ci = 0usize;
        let mut consumed_next = false;
        while ci < chars.len() {
            let c = chars[ci];
            match c {
                'b' => {
                    cfg.no_backspaces = true;
                    ci += 1;
                }
                'f' => {
                    cfg.fine = true;
                    ci += 1;
                }
                'p' => {
                    cfg.pass_unknown = true;
                    ci += 1;
                }
                'h' => {
                    if seen_spaces {
                        let _ = writeln!(io.stderr, "col: mutually exclusive arguments: --tabs --spaces");
                        return Ok(1);
                    }
                    seen_tabs = true;
                    cfg.compress_spaces = true;
                    ci += 1;
                }
                'x' => {
                    if seen_tabs {
                        let _ = writeln!(io.stderr, "col: mutually exclusive arguments: --tabs --spaces");
                        return Ok(1);
                    }
                    seen_spaces = true;
                    cfg.compress_spaces = false;
                    ci += 1;
                }
                'H' | 'V' => return Ok(0),
                'l' => {
                    let rest: String = chars[ci + 1..].iter().collect();
                    let val = if !rest.is_empty() {
                        ci = chars.len();
                        rest
                    } else if let Some(next_arg) = args.get(idx + 1) {
                        consumed_next = true;
                        ci = chars.len();
                        next_arg.to_string_lossy().into_owned()
                    } else {
                        let _ = usage_error(ctx, io, "col", &argopt::short_option_requires_argument(ctx, "l"));
                        return Ok(1);
                    };
                    match parse_lines_value(&val) {
                        Some(n) => cfg.max_bufd_lines = n,
                        None => {
                            let _ = writeln!(io.stderr, "col: bad -l argument: '{val}'");
                            return Ok(1);
                        }
                    }
                }
                _ => {
                    let _ = usage_error(ctx, io, "col", &argopt::invalid_option(ctx, &c.to_string()));
                    return Ok(1);
                }
            }
        }
        idx += if consumed_next { 2 } else { 1 };
    }

    if !positionals.is_empty() {
        let _ = writeln!(io.stderr, "col: bad usage");
        let _ = writeln!(io.stderr, "Try 'col --help' for more information.");
        return Ok(1);
    }

    let mut input = Vec::new();
    if let Err(e) = io.stdin.read_to_end(&mut input) {
        let _ = writeln!(io.stderr, "col: read error: {}", crate::fsutil::reason(&e));
        return Ok(1);
    }

    let mut filter = Filter::new(&cfg);
    filter.run(&input, &mut *io.stderr);
    if !filter.out.is_empty() {
        if let Err(e) = io.stdout.write_all(&filter.out) {
            let _ = writeln!(io.stderr, "col: write error: {}", crate::fsutil::reason(&e));
            return Ok(1);
        }
    }
    let _ = io.stdout.flush();
    Ok(0)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run_mem(args: &[&str], stdin_str: &str) -> (u32, String, String) {
        let sc = ScratchCtx::new("col");
        let mut io = MemIo::new(stdin_str);
        let ctx = sc.ctx();
        let os_args: Vec<OsString> = args.iter().map(OsString::from).collect();
        let rc = col_cmd(&os_args, &mut io.split(), &ctx).unwrap();
        (rc, io.out(), io.err())
    }

    #[test]
    fn tabs_spaces_and_backspaces() {
        let (rc, out, err) = run_mem(&[], "a       b\n");
        assert_eq!(rc, 0);
        assert!(err.is_empty());
        assert_eq!(out, "a\tb\n");

        let (rc, out, _) = run_mem(&["--spaces"], "a       b\n");
        assert_eq!(rc, 0);
        assert_eq!(out, "a       b\n");

        let (rc, out, _) = run_mem(&["-b"], "abc\x08d\n");
        assert_eq!(rc, 0);
        assert_eq!(out, "abd\n");
    }

    #[test]
    fn validation_and_conflicts() {
        let (rc, _, err) = run_mem(&["--lines", "bogus"], "hello\n");
        assert_eq!(rc, 1);
        assert!(err.contains("bad -l argument"));

        let (rc, _, _) = run_mem(&["-h", "-x"], "hello\n");
        assert_eq!(rc, 1);
    }
}
