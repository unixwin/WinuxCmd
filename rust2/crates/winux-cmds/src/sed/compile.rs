//! sed script compiler — a structural port of GNU sed 4.9 `sed/compile.c`.
//!
//! The reader keeps GNU's exact character accounting (`inchar` /
//! `savchar` / `in_nonblank`), because every compile diagnostic carries
//! the consumed-character count: `sed: -e expression #N, char M: <msg>`
//! (`file F line L: <msg>` for `-f`). Each function below mirrors its
//! compile.c namesake; the comments name the GNU behavior that matters.

use std::fs::File;
use std::io::{BufReader, BufWriter};

use winux_core::Ctx;
use winux_regex::{CompileOptions, Regex, Syntax};

use crate::fsutil;

use super::Fatal;

pub(crate) const EOF: i32 = -1;

/// GNU `posixicity`: POSIXLY_EXTENDED (default), POSIXLY_CORRECT (env),
/// POSIXLY_BASIC (`--posix`).
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub(crate) enum Posix {
    Extended,
    Correct,
    Basic,
}

// compile.c `errors[]`, verbatim.
const BAD_BANG: &str = "multiple `!'s";
const BAD_COMMA: &str = "unexpected `,'";
const BAD_STEP: &str = "invalid usage of +N or ~N as first address";
const EXCESS_OPEN_BRACE: &str = "unmatched `{'";
const EXCESS_CLOSE_BRACE: &str = "unexpected `}'";
const EXCESS_JUNK: &str = "extra characters after command";
const EXPECTED_SLASH: &str = "expected \\ after `a', `c' or `i'";
const NO_CLOSE_BRACE_ADDR: &str = "`}' doesn't want any addresses";
const NO_COLON_ADDR: &str = ": doesn't want any addresses";
const NO_SHARP_ADDR: &str = "comments don't accept any addresses";
const NO_COMMAND: &str = "missing command";
const ONE_ADDR: &str = "command only uses one address";
const UNTERM_ADDR_RE: &str = "unterminated address regex";
const UNTERM_S_CMD: &str = "unterminated `s' command";
const UNTERM_Y_CMD: &str = "unterminated `y' command";
const UNKNOWN_S_OPT: &str = "unknown option to `s'";
const EXCESS_P_OPT: &str = "multiple `p' options to `s' command";
const EXCESS_G_OPT: &str = "multiple `g' options to `s' command";
const EXCESS_N_OPT: &str = "multiple number options to `s' command";
const ZERO_N_OPT: &str = "number option to `s' command may not be zero";
const Y_CMD_LEN: &str = "strings for `y' command are different lengths";
const BAD_DELIM: &str = "delimiter character is not a single-byte character";
const ANCIENT_VERSION: &str = "expected newer version of sed";
const INVALID_LINE_0: &str = "invalid usage of line address 0";
const INCOMPLETE_CMD: &str = "incomplete command";
const COLON_LACKS_LABEL: &str = "\":\" lacks a label";
const RECURSIVE_ESCAPE_C: &str = "recursive escaping after \\c not allowed";
const DISALLOWED_CMD: &str = "e/r/w commands disabled in sandbox mode";
const MISSING_FILENAME: &str = "missing filename in r/R/w/W commands";
// regexp.c
const NO_REGEX: &str = "no previous regular expression";
const BAD_MODIF: &str = "cannot specify modifiers on empty regexp";

/// The sed version `v` compares against (strverscmp).
pub(crate) const PACKAGE_VERSION: &str = "4.9";

// replacement_types (sed.h)
pub(crate) const REPL_ASIS: u8 = 0;
pub(crate) const REPL_UPPERCASE: u8 = 1;
pub(crate) const REPL_LOWERCASE: u8 = 2;
pub(crate) const REPL_UPPERCASE_FIRST: u8 = 4;
pub(crate) const REPL_LOWERCASE_FIRST: u8 = 8;
pub(crate) const REPL_MODIFIERS: u8 = REPL_UPPERCASE_FIRST | REPL_LOWERCASE_FIRST;

#[derive(Clone, Copy, PartialEq, Eq)]
enum TextKind {
    Buffer,
    Replacement,
    Regex,
}

/// A compiled address regex / `s` regex.
pub(crate) struct SedRegex {
    pub re: Regex,
    /// Normalized pattern bytes (what `--debug` prints).
    pub pattern: Vec<u8>,
    pub icase: bool,
    pub mline: bool,
    /// NFA scratch reused across lines (winux-regex MatchCache).
    pub cache: winux_regex::MatchCache,
    /// Line DFA used ONLY as a line filter for address selection; captures
    /// and match spans always come from the interpreter
    /// (`Regex::find_at_bytes_with`). `None` when the pattern is outside the
    /// DFA's contract (back-references, `M` anchors, oversized automaton) —
    /// the address path then keeps the interpreter. See
    /// `winux_regex::dfa` for the equivalence contract.
    pub dfa: Option<winux_regex::dfa::LineDfa>,
    /// End-of-record byte the DFA scanning model assumes (`\n`, `\0` with
    /// `-z`): a pattern space that contains it (`N`, `G`-joined cycles) is
    /// routed to the interpreter.
    pub dfa_eol: u8,
}

impl SedRegex {
    /// Does the address regex match `line`? The DFA answers the boolean when
    /// it is exact on this input; every capture / span query elsewhere still
    /// goes through `re`.
    pub fn matches_line(&mut self, line: &[u8]) -> bool {
        if let Some(d) = self.dfa.as_mut() {
            use winux_regex::dfa::InputRequirement;
            let input_ok = match d.input_requirement() {
                InputRequirement::Any => true,
                InputRequirement::ValidUtf8 => line.is_ascii() || std::str::from_utf8(line).is_ok(),
                InputRequirement::Ascii => line.is_ascii(),
            };
            if input_ok && !line.contains(&self.dfa_eol) {
                return d.is_match_line(line);
            }
        }
        self.re.is_match_with(&mut self.cache, line)
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub(crate) enum AddrType {
    Null,
    /// Index into `Program::regexes`; `None` is `//` (the last regex).
    Regex(Option<usize>),
    Num,
    NumMod,
    Step,
    StepMod,
    Last,
}

#[derive(Clone, Copy, Debug)]
pub(crate) struct Addr {
    pub kind: AddrType,
    pub number: u64,
    pub step: u64,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub(crate) enum RangeState {
    Inactive,
    Active,
    Closed,
}

pub(crate) struct ReplPart {
    pub prefix: Vec<u8>,
    pub subst_id: i32,
    pub repl_type: u8,
}

pub(crate) struct Subst {
    pub regex: Option<usize>,
    pub replacement: Vec<ReplPart>,
    pub max_id: usize,
    pub global: bool,
    /// bit 1: print before `e`, bit 2: print after (GNU `print`).
    pub print: u8,
    pub eval: bool,
    pub numb: u64,
    pub outf: Option<usize>,
}

pub(crate) enum Trans {
    /// Every source and target is one byte: a 256-entry map.
    Table(Box<[u8; 256]>),
    /// Multibyte character pairs (GNU translate_mb), first match wins.
    Pairs(Vec<(Vec<u8>, Vec<u8>)>),
}

pub(crate) struct Translate {
    pub trans: Trans,
    /// Source / destination character lists for `--debug`.
    pub src: Vec<Vec<u8>>,
    pub dst: Vec<Vec<u8>>,
}

pub(crate) enum CmdX {
    None,
    Jump(usize),
    Label(Vec<u8>),
    /// a/i/c/e text: `None` is GNU's NULL text (prints nothing).
    Text(Option<Vec<u8>>),
    Int(i64),
    Read {
        fname: Vec<u8>,
        append: bool,
    },
    RFile(usize),
    WFile(usize),
    Subst(Box<Subst>),
    Translate(Box<Translate>),
}

pub(crate) struct Cmd {
    pub a1: Option<Addr>,
    pub a2: Option<Addr>,
    pub range: RangeState,
    pub bang: bool,
    pub ch: u8,
    pub x: CmdX,
}

/// An output file of `w`/`W`/`s///w` (shared by name, like GNU).
pub(crate) enum OutKind {
    Stdout,
    Stderr,
    File(Option<BufWriter<File>>),
}

pub(crate) struct OutFile {
    pub name: Vec<u8>,
    pub kind: OutKind,
    pub missing_newline: bool,
}

pub(crate) enum InKind {
    Stdin,
    File(Option<BufReader<File>>),
}

/// An `R` input file (shared by name).
pub(crate) struct InFile {
    pub name: Vec<u8>,
    pub kind: InKind,
    pub eof: bool,
}

#[derive(Default)]
pub(crate) struct Program {
    pub cmds: Vec<Cmd>,
    pub regexes: Vec<SedRegex>,
    pub wfiles: Vec<OutFile>,
    pub rfiles: Vec<InFile>,
}

#[derive(Clone)]
struct ErrInfo {
    name: Option<String>,
    line: u64,
    expr_count: u64,
}

/// Compile-wide state: GNU's file-scope statics in compile.c / sed.c.
pub(crate) struct Compiler<'c> {
    ctx: &'c Ctx<'c>,
    // --- prog_info ---
    src: Vec<u8>,
    cur: usize,
    is_file: bool,
    // --- error_info ---
    name: Option<String>,
    line: u64,
    expr_count: u64,
    string_expr_count: u64,
    // --- settings consumed while compiling ---
    pub posix: Posix,
    pub extended: bool,
    pub sandbox: bool,
    pub debug: bool,
    pub delim: u8,
    pub no_default_output: bool,
    pub utf8: bool,
    first_script: bool,
    // --- cross-chunk state ---
    pending_text: Option<Vec<u8>>,
    old_text_idx: Option<usize>,
    blocks: Vec<(usize, ErrInfo)>,
    labels: Vec<(Vec<u8>, usize)>,
    jumps: Vec<(usize, Vec<u8>)>,
    pub prog: Program,
}

fn is_space(ch: i32) -> bool {
    matches!(ch, 0x20 | 0x09 | 0x0a | 0x0b | 0x0c | 0x0d)
}

fn is_blank(ch: i32) -> bool {
    ch == 0x20 || ch == 0x09
}

fn is_digit(ch: i32) -> bool {
    (0x30..=0x39).contains(&ch)
}

/// Incremental UTF-8 IS_MB_CHAR (sed/utils.c is_mb_char): every byte of a
/// multibyte character counts as "mb" so it is never taken as a delimiter,
/// backslash or bracket.
#[derive(Default)]
pub(crate) struct MbState {
    remaining: u8,
}

impl MbState {
    pub(crate) fn is_mb(&mut self, utf8: bool, ch: i32) -> bool {
        if !utf8 || ch < 0 {
            return false;
        }
        let b = ch as u8;
        if self.remaining > 0 && (0x80..0xC0).contains(&b) {
            self.remaining -= 1;
            return true;
        }
        self.remaining = 0;
        let need = match b {
            0xC2..=0xDF => 1,
            0xE0..=0xEF => 2,
            0xF0..=0xF4 => 3,
            _ => 0,
        };
        if need > 0 {
            self.remaining = need;
            true
        } else {
            false
        }
    }
}

/// Width of a well-formed UTF-8 sequence at `p` (1 for ASCII / invalid).
pub(crate) fn utf8_len(buf: &[u8], p: usize) -> usize {
    let b = buf[p];
    let need = match b {
        0xC2..=0xDF => 2,
        0xE0..=0xEF => 3,
        0xF0..=0xF4 => 4,
        _ => return 1,
    };
    if p + need > buf.len() {
        return 1;
    }
    if buf[p + 1..p + need]
        .iter()
        .all(|c| (0x80..0xC0).contains(c))
    {
        need
    } else {
        1
    }
}

/// strverscmp for the `v` command's dotted versions.
fn version_greater(a: &[u8], b: &[u8]) -> bool {
    fn parts(s: &[u8]) -> Vec<Result<u64, Vec<u8>>> {
        let mut out = Vec::new();
        let mut i = 0;
        while i < s.len() {
            let start = i;
            if s[i].is_ascii_digit() {
                while i < s.len() && s[i].is_ascii_digit() {
                    i += 1;
                }
                let n = std::str::from_utf8(&s[start..i])
                    .ok()
                    .and_then(|t| t.parse::<u64>().ok())
                    .unwrap_or(u64::MAX);
                out.push(Ok(n));
            } else {
                while i < s.len() && !s[i].is_ascii_digit() {
                    i += 1;
                }
                out.push(Err(s[start..i].to_vec()));
            }
        }
        out
    }
    let (pa, pb) = (parts(a), parts(b));
    for (x, y) in pa.iter().zip(pb.iter()) {
        let ord = match (x, y) {
            (Ok(m), Ok(n)) => m.cmp(n),
            (Err(m), Err(n)) => m.cmp(n),
            (Ok(_), Err(n)) => {
                // digits sort after non-digit text except '.'
                if n.first() == Some(&b'.') {
                    std::cmp::Ordering::Greater
                } else {
                    std::cmp::Ordering::Less
                }
            }
            (Err(m), Ok(_)) => {
                if m.first() == Some(&b'.') {
                    std::cmp::Ordering::Less
                } else {
                    std::cmp::Ordering::Greater
                }
            }
        };
        if ord != std::cmp::Ordering::Equal {
            return ord == std::cmp::Ordering::Greater;
        }
    }
    pa.len() > pb.len()
}

/// glibc regerror text for a winux-regex compile error.
pub(crate) fn glibc_regerror(e: &winux_regex::Error) -> String {
    let m = e.message();
    match m {
        "unmatched [ or [^" => "Unmatched [, [^, [:, [., or [=".to_string(),
        "regular expression too complex" => "Regular expression too big".to_string(),
        "work limit exceeded (conservative no-match)" => "Memory exhausted".to_string(),
        _ => {
            let mut s = m.to_string();
            if let Some(f) = s.get_mut(0..1) {
                f.make_ascii_uppercase();
            }
            s
        }
    }
}

/// gnulib dfa.c parse_bracket_exp colon warning: a bracket whose content
/// is `:...:` (no inner class) — "character class syntax is [[:space:]],
/// not [:space:]". sed's dfawarn turns it into a panic (rc 4) unless
/// POSIXLY_CORRECT is set. `pat` is the normalized pattern.
fn confusing_bracket(pat: &[u8]) -> bool {
    let mut i = 0;
    while i < pat.len() {
        match pat[i] {
            b'\\' => i += 2,
            b'[' => {
                i += 1;
                if i < pat.len() && pat[i] == b'^' {
                    i += 1;
                }
                let first = i;
                let mut state: u32 = if i < pat.len() && pat[i] == b':' {
                    1
                } else {
                    0
                };
                let mut k = i;
                let mut closed = false;
                while k < pat.len() {
                    let c = pat[k];
                    if c == b']' && k > first {
                        closed = true;
                        break;
                    }
                    state &= !2;
                    if c == b'[' && k + 1 < pat.len() && matches!(pat[k + 1], b':' | b'.' | b'=') {
                        let d = pat[k + 1];
                        let mut j = k + 2;
                        while j + 1 < pat.len() && !(pat[j] == d && pat[j + 1] == b']') {
                            j += 1;
                        }
                        state |= 8;
                        k = j + 2;
                        continue;
                    }
                    state |= if c == b':' { 2 } else { 4 };
                    k += 1;
                }
                if closed && state == 7 {
                    return true;
                }
                i = k + 1;
            }
            _ => i += 1,
        }
    }
    false
}

impl<'c> Compiler<'c> {
    pub(crate) fn new(ctx: &'c Ctx<'c>, posix: Posix, utf8: bool) -> Compiler<'c> {
        Compiler {
            ctx,
            src: Vec::new(),
            cur: 0,
            is_file: false,
            name: None,
            line: 0,
            expr_count: 0,
            string_expr_count: 0,
            posix,
            extended: false,
            sandbox: false,
            debug: false,
            delim: b'\n',
            no_default_output: false,
            utf8,
            first_script: true,
            pending_text: None,
            old_text_idx: None,
            blocks: Vec::new(),
            labels: Vec::new(),
            jumps: Vec::new(),
            prog: Program::default(),
        }
    }

    // ---- diagnostics -----------------------------------------------------

    fn bad_prog(&self, why: &str) -> Fatal {
        let msg = match &self.name {
            Some(n) => format!("sed: file {} line {}: {}", n, self.line, why),
            None => format!(
                "sed: -e expression #{}, char {}: {}",
                self.expr_count, self.cur, why
            ),
        };
        Fatal { msg, status: 1 }
    }

    /// Runtime bad_prog (match_regex "no previous regular expression"):
    /// the reader is gone, so GNU prints char 0 / the last file line.
    pub(crate) fn runtime_bad_prog(&self, why: &str) -> Fatal {
        let msg = match &self.name {
            Some(n) => format!("sed: file {} line {}: {}", n, self.line, why),
            None => format!("sed: -e expression #{}, char 0: {}", self.expr_count, why),
        };
        Fatal { msg, status: 1 }
    }

    pub(crate) fn no_regex_error(&self) -> Fatal {
        self.runtime_bad_prog(NO_REGEX)
    }

    fn bad_command(&self, ch: i32) -> Fatal {
        let c = if ch < 0 { 0xFF } else { ch as u8 };
        let s = String::from_utf8_lossy(&[c]).into_owned();
        self.bad_prog(&format!("unknown command: `{s}'"))
    }

    // ---- reader (inchar / savchar / in_nonblank) -------------------------

    fn inchar(&mut self) -> i32 {
        let ch = if self.cur < self.src.len() {
            let c = self.src[self.cur] as i32;
            self.cur += 1;
            c
        } else {
            EOF
        };
        if ch == b'\n' as i32 {
            self.line += 1;
        }
        ch
    }

    fn savchar(&mut self, ch: i32) {
        if ch == EOF {
            return;
        }
        if ch == b'\n' as i32 && self.line > 0 {
            self.line -= 1;
        }
        if self.cur > 0 {
            self.cur -= 1;
        }
    }

    fn in_nonblank(&mut self) -> i32 {
        loop {
            let ch = self.inchar();
            if !is_blank(ch) {
                return ch;
            }
        }
    }

    fn read_end_of_cmd(&mut self) -> Result<(), Fatal> {
        let ch = self.in_nonblank();
        if ch == b'}' as i32 || ch == b'#' as i32 {
            self.savchar(ch);
        } else if ch != EOF && ch != b'\n' as i32 && ch != b';' as i32 {
            return Err(self.bad_prog(EXCESS_JUNK));
        }
        Ok(())
    }

    fn in_integer(&mut self, mut ch: i32) -> u64 {
        let mut num: u64 = 0;
        while is_digit(ch) {
            num = num.wrapping_mul(10).wrapping_add((ch - 0x30) as u64);
            ch = self.inchar();
        }
        self.savchar(ch);
        num
    }

    fn add_then_next(&mut self, b: &mut Vec<u8>, ch: i32) -> i32 {
        b.push(ch as u8);
        self.inchar()
    }

    // ---- entry points ----------------------------------------------------

    /// compile_string: one `-e` chunk (or the script operand).
    pub(crate) fn compile_string(&mut self, s: &[u8]) -> Result<(), Fatal> {
        self.src = s.to_vec();
        self.cur = 0;
        self.is_file = false;
        self.line = 0;
        self.name = None;
        self.string_expr_count += 1;
        self.expr_count = self.string_expr_count;
        let r = self.compile_program();
        self.src = Vec::new();
        self.cur = 0;
        self.first_script = false;
        r
    }

    /// compile_file: one `-f` script (`-` reads standard input).
    pub(crate) fn compile_file(
        &mut self,
        name: &str,
        stdin: &mut dyn std::io::Read,
    ) -> Result<(), Fatal> {
        let data = if name == "-" {
            let mut v = Vec::new();
            let _ = stdin.read_to_end(&mut v);
            v
        } else {
            match std::fs::read(fsutil::resolve(self.ctx, name)) {
                Ok(v) => v,
                Err(e) => {
                    let reason = fsutil::gnu_open_reason(self.ctx, name, &e, false);
                    return Err(Fatal {
                        msg: format!("sed: couldn't open file {name}: {reason}"),
                        status: 4,
                    });
                }
            }
        };
        self.src = data;
        self.cur = 0;
        self.is_file = true;
        self.line = 1;
        self.name = Some(name.to_string());
        self.expr_count = 0;
        let r = self.compile_program();
        self.src = Vec::new();
        self.cur = 0;
        self.first_script = false;
        r
    }

    /// check_final_program: unmatched `{`, a trailing a/i/c text, labels.
    pub(crate) fn check_final_program(&mut self) -> Result<(), Fatal> {
        if let Some((_, info)) = self.blocks.last().cloned() {
            self.name = info.name;
            self.line = info.line;
            self.expr_count = info.expr_count;
            self.cur = 0;
            return Err(self.bad_prog(EXCESS_OPEN_BRACE));
        }
        if let Some(text) = self.pending_text.take() {
            if let Some(idx) = self.old_text_idx {
                if !text.is_empty() {
                    self.prog.cmds[idx].x = CmdX::Text(Some(text));
                }
            }
        }
        let jumps = std::mem::take(&mut self.jumps);
        for (idx, name) in jumps.iter().rev() {
            let target = self
                .labels
                .iter()
                .rev()
                .find(|(l, _)| l == name)
                .map(|(_, i)| *i);
            match target {
                Some(t) => self.prog.cmds[*idx].x = CmdX::Jump(t),
                None => {
                    if !name.is_empty() {
                        return Err(Fatal {
                            msg: format!(
                                "sed: can't find label for jump to `{}'",
                                String::from_utf8_lossy(name)
                            ),
                            status: 4,
                        });
                    }
                    let end = self.prog.cmds.len();
                    self.prog.cmds[*idx].x = CmdX::Jump(end);
                }
            }
        }
        Ok(())
    }

    // ---- text helpers ------------------------------------------------------

    /// convert_number: `\dNNN` / `\oNNN` / `\xHH` (`p` is at the letter).
    fn convert_number(buf: &[u8], p: usize, base: u32) -> (u8, usize) {
        let mut n: u32 = 0;
        let mut max: u32 = 1;
        let mut q = p + 1;
        while q < buf.len() && max <= 255 {
            let d = match (buf[q] as char).to_digit(16) {
                Some(d) if d < base => d,
                _ => break,
            };
            n = n.wrapping_mul(base).wrapping_add(d);
            q += 1;
            max *= base;
        }
        if q == p + 1 {
            (buf[p], q)
        } else {
            (n as u8, q)
        }
    }

    /// normalize_text: the `\X` escape pass shared by text, replacement
    /// and regex buffers.
    fn normalize_text(&self, buf: &[u8], kind: TextKind) -> Result<Vec<u8>, Fatal> {
        let mut q: Vec<u8> = Vec::with_capacity(buf.len());
        let mut p = 0usize;
        let mut bracket_state: i32 = 0;
        let len = buf.len();
        while p < len {
            if self.utf8 {
                let w = utf8_len(buf, p);
                if w > 1 {
                    q.extend_from_slice(&buf[p..p + w]);
                    p += w;
                    continue;
                }
            }
            if buf[p] == b'\\' && p + 1 < len && bracket_state == 0 {
                p += 1;
                match buf[p] {
                    b'a' => {
                        q.push(0x07);
                        p += 1;
                        continue;
                    }
                    b'f' => {
                        q.push(0x0c);
                        p += 1;
                        continue;
                    }
                    b'\n' | b'n' => {
                        q.push(b'\n');
                        p += 1;
                        continue;
                    }
                    b'r' => {
                        q.push(b'\r');
                        p += 1;
                        continue;
                    }
                    b't' => {
                        q.push(b'\t');
                        p += 1;
                        continue;
                    }
                    b'v' => {
                        q.push(0x0b);
                        p += 1;
                        continue;
                    }
                    c @ (b'd' | b'x' | b'o') => {
                        let base = match c {
                            b'd' => 10,
                            b'x' => 16,
                            _ => 8,
                        };
                        let (ch, np) = Self::convert_number(buf, p, base);
                        if kind == TextKind::Replacement && (ch == b'&' || ch == b'\\') {
                            q.push(b'\\');
                        }
                        q.push(ch);
                        p = np;
                        continue;
                    }
                    b'c' => {
                        p += 1;
                        if p < len {
                            q.push(buf[p].to_ascii_uppercase() ^ 0x40);
                            if buf[p] == b'\\' {
                                p += 1;
                                if p >= len || buf[p] != b'\\' {
                                    return Err(self.bad_prog(RECURSIVE_ESCAPE_C));
                                }
                            }
                            p += 1;
                            continue;
                        } else {
                            if kind != TextKind::Buffer {
                                q.push(b'\\');
                            }
                            continue;
                        }
                    }
                    _ => {
                        if kind != TextKind::Buffer {
                            q.push(b'\\');
                        }
                    }
                }
            } else if kind == TextKind::Regex && self.posix != Posix::Extended {
                match buf[p] {
                    b'[' => {
                        if bracket_state == 0 {
                            bracket_state = -1;
                        }
                    }
                    c @ (b':' | b'.' | b'=') => {
                        if bracket_state == -1 && p > 0 && buf[p - 1] == b'[' {
                            bracket_state = c as i32;
                        }
                    }
                    b']' => {
                        if bracket_state == 0 {
                        } else if bracket_state == -1 {
                            bracket_state = 0;
                        } else if p >= 2
                            && buf[p - 2] as i32 != bracket_state
                            && buf[p - 1] as i32 == bracket_state
                        {
                            bracket_state = -1;
                        }
                    }
                    _ => {}
                }
            }
            q.push(buf[p]);
            p += 1;
        }
        Ok(q)
    }

    // ---- slash-delimited parts ----------------------------------------------

    fn snarf_char_class(&mut self, b: &mut Vec<u8>, mb: &mut MbState) -> i32 {
        let mut state = 0i32;
        let mut delim = 0i32;
        let mut ch = self.inchar();
        if ch == b'^' as i32 {
            ch = self.add_then_next(b, ch);
        }
        if ch == b']' as i32 {
            ch = self.add_then_next(b, ch);
        }
        loop {
            let mb_char = mb.is_mb(self.utf8, ch);
            let mut clear = true;
            if ch == EOF || ch == b'\n' as i32 {
                return ch;
            } else if ch == b'.' as i32 || ch == b':' as i32 || ch == b'=' as i32 {
                if mb_char {
                    clear = false;
                } else if state == 1 {
                    delim = ch;
                    state = 2;
                    clear = false;
                } else if state == 2 && ch == delim {
                    state = 3;
                    clear = false;
                }
            } else if ch == b'[' as i32 {
                if !mb_char && state == 0 {
                    state = 1;
                }
                clear = false;
            } else if ch == b']' as i32 {
                if mb_char {
                    clear = false;
                } else if state == 0 || state == 1 {
                    return ch;
                } else if state == 3 {
                    state = 0;
                }
            }
            if clear {
                state &= !1;
            }
            ch = self.add_then_next(b, ch);
        }
    }

    fn match_slash(&mut self, slash: i32, regex: bool) -> Result<Option<Vec<u8>>, Fatal> {
        let mut mb = MbState::default();
        if mb.is_mb(self.utf8, slash) {
            return Err(self.bad_prog(BAD_DELIM));
        }
        let mut mb = MbState::default();
        let mut b = Vec::new();
        let mut ch;
        loop {
            ch = self.inchar();
            if ch == EOF || ch == b'\n' as i32 {
                break;
            }
            let mb_char = mb.is_mb(self.utf8, ch);
            if !mb_char {
                if ch == slash {
                    return Ok(Some(b));
                } else if ch == b'\\' as i32 {
                    ch = self.inchar();
                    if ch == EOF {
                        break;
                    } else if ch != b'\n' as i32 && (ch != slash || (!regex && ch == b'&' as i32)) {
                        b.push(b'\\');
                    }
                } else if ch == b'[' as i32 && regex {
                    b.push(ch as u8);
                    ch = self.snarf_char_class(&mut b, &mut mb);
                    if ch != b']' as i32 {
                        break;
                    }
                }
            }
            b.push(ch as u8);
        }
        if ch == b'\n' as i32 {
            self.savchar(ch);
        }
        Ok(None)
    }

    fn read_label(&mut self) -> Vec<u8> {
        let mut b = Vec::new();
        let mut ch = self.in_nonblank();
        while ch != EOF
            && ch != b'\n' as i32
            && !is_blank(ch)
            && ch != b';' as i32
            && ch != b'}' as i32
            && ch != b'#' as i32
        {
            ch = self.add_then_next(&mut b, ch);
        }
        self.savchar(ch);
        b
    }

    fn read_filename(&mut self) -> Result<Vec<u8>, Fatal> {
        if self.sandbox {
            return Err(self.bad_prog(DISALLOWED_CMD));
        }
        let mut b = Vec::new();
        let mut ch = self.in_nonblank();
        while ch != EOF && ch != b'\n' as i32 {
            ch = self.add_then_next(&mut b, ch);
        }
        Ok(b)
    }

    /// get_openfile for `w` / `W` / `s///w` (opened and truncated at
    /// compile time, fail = panic rc 4).
    fn get_write_file(&mut self) -> Result<usize, Fatal> {
        let name = self.read_filename()?;
        if name.is_empty() {
            return Err(self.bad_prog(MISSING_FILENAME));
        }
        if let Some(i) = self.prog.wfiles.iter().position(|f| f.name == name) {
            return Ok(i);
        }
        let special = match name.as_slice() {
            b"/dev/stdout" => Some(OutKind::Stdout),
            b"/dev/stderr" => Some(OutKind::Stderr),
            _ => None,
        };
        // GNU only treats the std* names specially under POSIXLY_EXTENDED;
        // on Linux the other modes still reach the same streams through
        // the /dev files, so the special routing is kept for every mode.
        let kind = match special {
            Some(k) => k,
            None => {
                let sname = String::from_utf8_lossy(&name).into_owned();
                match File::create(fsutil::resolve(self.ctx, &sname)) {
                    Ok(f) => OutKind::File(Some(BufWriter::new(f))),
                    Err(e) => {
                        let reason = fsutil::gnu_open_reason(self.ctx, &sname, &e, true);
                        return Err(Fatal {
                            msg: format!("sed: couldn't open file {sname}: {reason}"),
                            status: 4,
                        });
                    }
                }
            }
        };
        self.prog.wfiles.push(OutFile {
            name,
            kind,
            missing_newline: false,
        });
        Ok(self.prog.wfiles.len() - 1)
    }

    /// get_openfile for `R` (fail = false: a missing file reads as empty).
    fn get_read_file(&mut self) -> Result<usize, Fatal> {
        let name = self.read_filename()?;
        if name.is_empty() {
            return Err(self.bad_prog(MISSING_FILENAME));
        }
        if let Some(i) = self.prog.rfiles.iter().position(|f| f.name == name) {
            return Ok(i);
        }
        let kind = if name == b"/dev/stdin" {
            InKind::Stdin
        } else {
            let sname = String::from_utf8_lossy(&name).into_owned();
            InKind::File(
                File::open(fsutil::resolve(self.ctx, &sname))
                    .ok()
                    .filter(|f| f.metadata().map(|m| !m.is_dir()).unwrap_or(true))
                    .map(BufReader::new),
            )
        };
        self.prog.rfiles.push(InFile {
            name,
            kind,
            eof: false,
        });
        Ok(self.prog.rfiles.len() - 1)
    }

    // ---- s command pieces --------------------------------------------------

    fn mark_subst_opts(&mut self, cmd: &mut Subst) -> Result<(bool, bool), Fatal> {
        let (mut icase, mut mline) = (false, false);
        loop {
            let ch = self.in_nonblank();
            match ch {
                c if c == b'i' as i32 || c == b'I' as i32 => {
                    if self.posix == Posix::Basic {
                        return Err(self.bad_prog(UNKNOWN_S_OPT));
                    }
                    icase = true;
                }
                c if c == b'm' as i32 || c == b'M' as i32 => {
                    if self.posix == Posix::Basic {
                        return Err(self.bad_prog(UNKNOWN_S_OPT));
                    }
                    mline = true;
                }
                c if c == b'e' as i32 => {
                    if self.posix == Posix::Basic {
                        return Err(self.bad_prog(UNKNOWN_S_OPT));
                    }
                    cmd.eval = true;
                }
                c if c == b'p' as i32 => {
                    if cmd.print != 0 {
                        return Err(self.bad_prog(EXCESS_P_OPT));
                    }
                    cmd.print |= 1 << (cmd.eval as u8);
                }
                c if c == b'g' as i32 => {
                    if cmd.global {
                        return Err(self.bad_prog(EXCESS_G_OPT));
                    }
                    cmd.global = true;
                }
                c if c == b'w' as i32 => {
                    cmd.outf = Some(self.get_write_file()?);
                    return Ok((icase, mline));
                }
                c if is_digit(c) => {
                    if cmd.numb != 0 {
                        return Err(self.bad_prog(EXCESS_N_OPT));
                    }
                    cmd.numb = self.in_integer(c);
                    if cmd.numb == 0 {
                        return Err(self.bad_prog(ZERO_N_OPT));
                    }
                }
                c if c == b'}' as i32 || c == b'#' as i32 => {
                    self.savchar(c);
                    return Ok((icase, mline));
                }
                c if c == EOF || c == b'\n' as i32 || c == b';' as i32 => {
                    return Ok((icase, mline));
                }
                c if c == b'\r' as i32 => {
                    if self.inchar() == b'\n' as i32 {
                        return Ok((icase, mline));
                    }
                    return Err(self.bad_prog(UNKNOWN_S_OPT));
                }
                _ => return Err(self.bad_prog(UNKNOWN_S_OPT)),
            }
        }
    }

    fn setup_replacement(&self, text: &[u8], sub: &mut Subst) -> Result<(), Fatal> {
        let mut base_buf = self.normalize_text(text, TextKind::Replacement)?;
        sub.max_id = 0;
        let mut parts: Vec<ReplPart> = Vec::new();
        let mut repl_type = REPL_ASIS;
        let mut save_type = REPL_ASIS;
        let end = base_buf.len();
        let mut base = 0usize;
        let mut p = 0usize;
        while p < end {
            if base_buf[p] == b'\\' {
                parts.push(ReplPart {
                    prefix: base_buf[base..p].to_vec(),
                    subst_id: -1,
                    repl_type,
                });
                repl_type = save_type;
                p += 1;
                let tail = parts.last_mut().expect("just pushed");
                if p == end {
                    tail.prefix.push(b'\\');
                } else if self.posix == Posix::Basic && !base_buf[p].is_ascii_digit() {
                    tail.prefix.push(base_buf[p]);
                } else {
                    match base_buf[p] {
                        d @ b'0'..=b'9' => {
                            tail.subst_id = (d - b'0') as i32;
                            if sub.max_id < tail.subst_id as usize {
                                sub.max_id = tail.subst_id as usize;
                            }
                        }
                        b'L' => {
                            repl_type = REPL_LOWERCASE;
                            save_type = REPL_LOWERCASE;
                        }
                        b'U' => {
                            repl_type = REPL_UPPERCASE;
                            save_type = REPL_UPPERCASE;
                        }
                        b'E' => {
                            repl_type = REPL_ASIS;
                            save_type = REPL_ASIS;
                        }
                        b'l' => {
                            save_type = repl_type;
                            repl_type |= REPL_LOWERCASE_FIRST;
                        }
                        b'u' => {
                            save_type = repl_type;
                            repl_type |= REPL_UPPERCASE_FIRST;
                        }
                        c => {
                            tail.prefix.push(c);
                        }
                    }
                }
                base = p + 1;
            } else if base_buf[p] == b'&' {
                parts.push(ReplPart {
                    prefix: base_buf[base..p].to_vec(),
                    subst_id: 0,
                    repl_type,
                });
                repl_type = save_type;
                base = p + 1;
            }
            p += 1;
        }
        if base < end {
            parts.push(ReplPart {
                prefix: base_buf[base..end].to_vec(),
                subst_id: -1,
                repl_type,
            });
        }
        base_buf.clear();
        sub.replacement = parts;
        Ok(())
    }

    /// compile_regex (+ compile_regex_1's checks). `None` = `//`.
    fn compile_regex(
        &mut self,
        b: &[u8],
        icase: bool,
        mline: bool,
        needed_sub: usize,
        line_filter: bool,
    ) -> Result<Option<usize>, Fatal> {
        if b.is_empty() {
            if icase || mline {
                return Err(self.bad_prog(BAD_MODIF));
            }
            return Ok(None);
        }
        let pattern = self.normalize_text(b, TextKind::Regex)?;
        let syntax = if self.extended {
            Syntax::Extended
        } else {
            Syntax::Basic
        };
        let mut opts = CompileOptions::sed(icase, mline);
        opts.line_delimiter = self.delim;
        opts.strict_utf8_dot = self.utf8;
        // regexp.c compile_regex_1: POSIXLY_CORRECT / --posix add
        // RE_UNMATCHED_RIGHT_PAREN_ORD; --posix adds RE_NO_GNU_OPS (+
        // RE_LIMITED_OPS for BRE).
        opts.unmatched_right_paren_ord = self.posix != Posix::Extended;
        opts.no_gnu_ops = self.posix == Posix::Basic;
        let re = match Regex::compile_with(syntax, &pattern, opts) {
            Ok(r) => r,
            Err(e) => return Err(self.bad_prog(&glibc_regerror(&e))),
        };
        if needed_sub > 0 && re.capture_count() < needed_sub - 1 && self.posix == Posix::Extended {
            return Err(self.bad_prog(&format!(
                "invalid reference \\{} on `s' command's RHS",
                needed_sub - 1
            )));
        }
        if confusing_bracket(&pattern) && self.ctx.env.get("POSIXLY_CORRECT").is_none() {
            return Err(Fatal {
                msg: "sed: character class syntax is [[:space:]], not [:space:]".to_string(),
                status: 4,
            });
        }
        // The line DFA is an address-selection filter only: substitution
        // regexes need spans and captures, so they never build one. Patterns
        // without the Thompson NFA (back-references, oversized intervals) are
        // decided by the budgeted backtracker, whose exhaustion is a
        // conservative no-match — the DFA is exact, so those keep the
        // interpreter to preserve verdicts.
        let dfa = if line_filter && re.has_nfa() {
            winux_regex::dfa::LineDfa::new(&[&re], winux_regex::dfa::LineMode::Contains, self.delim)
        } else {
            None
        };
        self.prog.regexes.push(SedRegex {
            dfa,
            dfa_eol: self.delim,
            re,
            pattern,
            icase,
            mline,
            cache: winux_regex::MatchCache::new(),
        });
        Ok(Some(self.prog.regexes.len() - 1))
    }

    // ---- addresses -----------------------------------------------------------

    fn compile_address(&mut self, mut ch: i32) -> Result<Option<Addr>, Fatal> {
        let mut addr = Addr {
            kind: AddrType::Null,
            number: u64::MAX,
            step: 0,
        };
        if ch == b'/' as i32 || ch == b'\\' as i32 {
            if ch == b'\\' as i32 {
                ch = self.inchar();
            }
            let b = match self.match_slash(ch, true)? {
                Some(b) => b,
                None => return Err(self.bad_prog(UNTERM_ADDR_RE)),
            };
            let (mut icase, mut mline) = (false, false);
            loop {
                let c = self.in_nonblank();
                if self.posix != Posix::Basic && c == b'I' as i32 {
                    icase = true;
                } else if self.posix != Posix::Basic && c == b'M' as i32 {
                    mline = true;
                } else {
                    self.savchar(c);
                    let idx = self.compile_regex(&b, icase, mline, 0, true)?;
                    addr.kind = AddrType::Regex(idx);
                    return Ok(Some(addr));
                }
            }
        } else if is_digit(ch) {
            addr.number = self.in_integer(ch);
            addr.kind = AddrType::Num;
            let c = self.in_nonblank();
            if c != b'~' as i32 || self.posix == Posix::Basic {
                self.savchar(c);
            } else {
                let c2 = self.in_nonblank();
                let step = self.in_integer(c2);
                if step > 0 {
                    addr.step = step;
                    addr.kind = AddrType::NumMod;
                }
            }
        } else if (ch == b'+' as i32 || ch == b'~' as i32) && self.posix != Posix::Basic {
            let c2 = self.in_nonblank();
            addr.step = self.in_integer(c2);
            if addr.step == 0 {
                // ADDR_IS_NULL: the range stops on the next line.
            } else if ch == b'+' as i32 {
                addr.kind = AddrType::Step;
            } else {
                addr.kind = AddrType::StepMod;
            }
        } else if ch == b'$' as i32 {
            addr.kind = AddrType::Last;
        } else {
            return Ok(None);
        }
        Ok(Some(addr))
    }

    // ---- read_text -------------------------------------------------------------

    /// read_text: `fresh` starts a new text (GNU `buf != NULL`). Returns
    /// the finished text, or `None` while it stays pending (continued by
    /// the next chunk).
    fn read_text(&mut self, fresh: bool, leadin: i32) -> Result<Option<Vec<u8>>, Fatal> {
        if fresh {
            self.pending_text = Some(Vec::new());
        }
        if leadin == EOF {
            return Ok(None);
        }
        let mut pending = self.pending_text.take().unwrap_or_default();
        if leadin != b'\n' as i32 {
            pending.push(leadin as u8);
        }
        let mut ch = self.inchar();
        while ch != EOF && ch != b'\n' as i32 {
            if ch == b'\\' as i32 {
                ch = self.inchar();
                if ch != EOF {
                    pending.push(b'\\');
                }
            }
            if ch == EOF {
                pending.push(b'\n');
                self.pending_text = Some(pending);
                return Ok(None);
            }
            ch = self.add_then_next(&mut pending, ch);
        }
        pending.push(b'\n');
        let text = self.normalize_text(&pending, TextKind::Buffer)?;
        Ok(Some(text))
    }

    // ---- compile_program -------------------------------------------------------

    fn compile_program(&mut self) -> Result<(), Fatal> {
        if self.pending_text.is_some() {
            if let Some(text) = self.read_text(false, b'\n' as i32)? {
                if let Some(idx) = self.old_text_idx {
                    self.prog.cmds[idx].x = CmdX::Text(Some(text));
                }
            }
        }
        loop {
            let mut ch;
            loop {
                ch = self.inchar();
                if !(ch == b';' as i32 || is_space(ch)) {
                    break;
                }
            }
            if ch == EOF {
                break;
            }
            let idx = self.prog.cmds.len();
            let mut cmd = Cmd {
                a1: None,
                a2: None,
                range: RangeState::Inactive,
                bang: false,
                ch: 0,
                x: CmdX::None,
            };
            if let Some(a) = self.compile_address(ch)? {
                if matches!(a.kind, AddrType::Step | AddrType::StepMod) {
                    return Err(self.bad_prog(BAD_STEP));
                }
                cmd.a1 = Some(a);
                ch = self.in_nonblank();
                if ch == b',' as i32 {
                    let c2 = self.in_nonblank();
                    match self.compile_address(c2)? {
                        Some(a2) => cmd.a2 = Some(a2),
                        None => return Err(self.bad_prog(BAD_COMMA)),
                    }
                    ch = self.in_nonblank();
                }
                let a1 = cmd.a1.as_ref().expect("set above");
                if a1.kind == AddrType::Num
                    && a1.number == 0
                    && ((cmd.a2.is_none() && ch != b'r' as i32)
                        || cmd
                            .a2
                            .as_ref()
                            .map(|a2| !matches!(a2.kind, AddrType::Regex(_)))
                            .unwrap_or(false)
                        || self.posix == Posix::Basic)
                {
                    return Err(self.bad_prog(INVALID_LINE_0));
                }
            }
            if ch == b'!' as i32 {
                cmd.bang = true;
                ch = self.in_nonblank();
                if ch == b'!' as i32 {
                    return Err(self.bad_prog(BAD_BANG));
                }
            }
            if self.posix == Posix::Basic {
                let c = if ch >= 0 { ch as u8 } else { 0 };
                if matches!(
                    c,
                    b'e' | b'F' | b'v' | b'z' | b'L' | b'Q' | b'T' | b'R' | b'W'
                ) {
                    return Err(self.bad_command(ch));
                }
                if matches!(c, b'a' | b'i' | b'l' | b'=' | b'r') && cmd.a2.is_some() {
                    return Err(self.bad_prog(ONE_ADDR));
                }
            }
            if ch == EOF {
                return Err(self.bad_prog(NO_COMMAND));
            }
            cmd.ch = ch as u8;
            match ch as u8 {
                b'#' => {
                    if cmd.a1.is_some() {
                        return Err(self.bad_prog(NO_SHARP_ADDR));
                    }
                    let mut c = self.inchar();
                    if c == b'n' as i32 && self.first_script && self.line < 2 && self.cur == 2 {
                        self.no_default_output = true;
                    }
                    while c != EOF && c != b'\n' as i32 {
                        c = self.inchar();
                    }
                    continue;
                }
                b'v' => {
                    let version = self.read_label();
                    let compared: &[u8] = if version.is_empty() { b"4.0" } else { &version };
                    if version_greater(compared, PACKAGE_VERSION.as_bytes()) {
                        return Err(self.bad_prog(ANCIENT_VERSION));
                    }
                    self.posix = Posix::Extended;
                    continue;
                }
                b'{' => {
                    self.blocks.push((
                        idx,
                        ErrInfo {
                            name: self.name.clone(),
                            line: self.line,
                            expr_count: self.expr_count,
                        },
                    ));
                    cmd.bang = !cmd.bang;
                }
                b'}' => {
                    if self.blocks.is_empty() {
                        return Err(self.bad_prog(EXCESS_CLOSE_BRACE));
                    }
                    if cmd.a1.is_some() {
                        return Err(self.bad_prog(NO_CLOSE_BRACE_ADDR));
                    }
                    self.read_end_of_cmd()?;
                    let (open, _) = self.blocks.pop().expect("checked");
                    self.prog.cmds[open].x = CmdX::Jump(idx);
                }
                b'e' => {
                    if self.sandbox {
                        return Err(self.bad_prog(DISALLOWED_CMD));
                    }
                    let c = self.in_nonblank();
                    if c == EOF || c == b'\n' as i32 {
                        cmd.x = CmdX::Text(None);
                    } else {
                        self.read_text_cmd(&mut cmd, idx, c)?;
                    }
                }
                b'a' | b'i' | b'c' => {
                    let c = self.in_nonblank();
                    self.read_text_cmd(&mut cmd, idx, c)?;
                }
                b':' => {
                    if cmd.a1.is_some() {
                        return Err(self.bad_prog(NO_COLON_ADDR));
                    }
                    let label = self.read_label();
                    if label.is_empty() {
                        return Err(self.bad_prog(COLON_LACKS_LABEL));
                    }
                    self.labels.push((label.clone(), idx));
                    cmd.x = CmdX::Label(label);
                }
                b'T' | b'b' | b't' => {
                    let label = self.read_label();
                    self.jumps.push((idx, label));
                    cmd.x = CmdX::Jump(usize::MAX);
                }
                b'Q' | b'q' | b'L' | b'l' => {
                    if matches!(ch as u8, b'Q' | b'q') && cmd.a2.is_some() {
                        return Err(self.bad_prog(ONE_ADDR));
                    }
                    let c = self.in_nonblank();
                    if is_digit(c) && self.posix != Posix::Basic {
                        cmd.x = CmdX::Int(self.in_integer(c) as i64);
                    } else {
                        cmd.x = CmdX::Int(-1);
                        self.savchar(c);
                    }
                    self.read_end_of_cmd()?;
                }
                b'=' | b'd' | b'D' | b'F' | b'g' | b'G' | b'h' | b'H' | b'n' | b'N' | b'p'
                | b'P' | b'z' | b'x' => {
                    self.read_end_of_cmd()?;
                }
                b'r' => {
                    let b = self.read_filename()?;
                    if b.is_empty() {
                        return Err(self.bad_prog(MISSING_FILENAME));
                    }
                    let mut append = true;
                    if let Some(a1) = cmd.a1.as_mut() {
                        if a1.kind == AddrType::Num && a1.number == 0 && cmd.a2.is_none() {
                            a1.number = 1;
                            append = false;
                        }
                    }
                    cmd.x = CmdX::Read { fname: b, append };
                }
                b'R' => {
                    cmd.x = CmdX::RFile(self.get_read_file()?);
                }
                b'W' | b'w' => {
                    cmd.x = CmdX::WFile(self.get_write_file()?);
                }
                b's' => {
                    let slash = self.inchar();
                    let b = match self.match_slash(slash, true)? {
                        Some(b) => b,
                        None => return Err(self.bad_prog(UNTERM_S_CMD)),
                    };
                    let b2 = match self.match_slash(slash, false)? {
                        Some(b) => b,
                        None => return Err(self.bad_prog(UNTERM_S_CMD)),
                    };
                    let mut sub = Box::new(Subst {
                        regex: None,
                        replacement: Vec::new(),
                        max_id: 0,
                        global: false,
                        print: 0,
                        eval: false,
                        numb: 0,
                        outf: None,
                    });
                    self.setup_replacement(&b2, &mut sub)?;
                    let (icase, mline) = self.mark_subst_opts(&mut sub)?;
                    sub.regex = self.compile_regex(&b, icase, mline, sub.max_id + 1, false)?;
                    if sub.eval && self.sandbox {
                        return Err(self.bad_prog(DISALLOWED_CMD));
                    }
                    cmd.x = CmdX::Subst(sub);
                }
                b'y' => {
                    let slash = self.inchar();
                    let b = match self.match_slash(slash, false)? {
                        Some(b) => b,
                        None => return Err(self.bad_prog(UNTERM_Y_CMD)),
                    };
                    let src = self.normalize_text(&b, TextKind::Buffer)?;
                    let b2 = match self.match_slash(slash, false)? {
                        Some(b) => b,
                        None => return Err(self.bad_prog(UNTERM_Y_CMD)),
                    };
                    let dst = self.normalize_text(&b2, TextKind::Buffer)?;
                    let t = self.build_translate(&src, &dst)?;
                    cmd.x = CmdX::Translate(Box::new(t));
                    self.read_end_of_cmd()?;
                }
                _ => return Err(self.bad_command(ch)),
            }
            self.prog.cmds.push(cmd);
        }
        if self.posix == Posix::Basic && self.pending_text.is_some() {
            return Err(self.bad_prog(INCOMPLETE_CMD));
        }
        Ok(())
    }

    /// The a/i/c/e text reader after the command letter (GNU
    /// `read_text_to_slash`), `c` = first non-blank after the letter.
    fn read_text_cmd(&mut self, cmd: &mut Cmd, idx: usize, mut c: i32) -> Result<(), Fatal> {
        if c == EOF {
            return Err(self.bad_prog(EXPECTED_SLASH));
        }
        if c == b'\\' as i32 {
            c = self.inchar();
        } else {
            if self.posix == Posix::Basic {
                return Err(self.bad_prog(EXPECTED_SLASH));
            }
            self.savchar(c);
            c = b'\n' as i32;
        }
        self.old_text_idx = Some(idx);
        let text = self.read_text(true, c)?;
        cmd.x = CmdX::Text(text);
        Ok(())
    }

    fn split_chars(&self, s: &[u8]) -> Vec<Vec<u8>> {
        let mut out = Vec::new();
        let mut i = 0;
        while i < s.len() {
            let w = if self.utf8 { utf8_len(s, i) } else { 1 };
            out.push(s[i..i + w].to_vec());
            i += w;
        }
        out
    }

    fn build_translate(&self, src: &[u8], dst: &[u8]) -> Result<Translate, Fatal> {
        let s = self.split_chars(src);
        let d = self.split_chars(dst);
        if s.len() != d.len() {
            return Err(self.bad_prog(Y_CMD_LEN));
        }
        let single = s.iter().chain(d.iter()).all(|c| c.len() == 1);
        let trans = if single {
            let mut t = Box::new([0u8; 256]);
            for (i, v) in t.iter_mut().enumerate() {
                *v = i as u8;
            }
            // first pair wins (translate_mb scans pairs in order)
            for (a, b) in s.iter().zip(d.iter()).rev() {
                t[a[0] as usize] = b[0];
            }
            Trans::Table(t)
        } else {
            Trans::Pairs(s.iter().cloned().zip(d.iter().cloned()).collect())
        };
        Ok(Translate {
            trans,
            src: s,
            dst: d,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn colon_warning_shapes() {
        assert!(confusing_bracket(b"[:alpha:]"));
        assert!(confusing_bracket(b"x[^:a:]"));
        assert!(!confusing_bracket(b"[[:alpha:]]"));
        assert!(!confusing_bracket(b"[::]"));
        assert!(!confusing_bracket(b"[:a]"));
        assert!(!confusing_bracket(b"\\[:a:]"));
    }

    #[test]
    fn strverscmp_like() {
        assert!(version_greater(b"9.0", b"4.9"));
        assert!(version_greater(b"4.10", b"4.9"));
        assert!(!version_greater(b"4.2", b"4.9"));
        assert!(!version_greater(b"4.9", b"4.9"));
        assert!(!version_greater(b"4.0", b"4.9"));
    }
}
