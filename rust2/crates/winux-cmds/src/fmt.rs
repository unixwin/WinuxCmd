//! `fmt` — GNU coreutils fmt.c (9.4) semantics, ported function-by-function
//! from the source and probed byte-for-byte against WSL coreutils 9.4.
//!
//! Paragraph refilling with the Knuth-ish cost DP of fmt.c: words carry
//! paren/period/punct/final attributes (emacs sentence rules: a word
//! ending in [.?!] followed by end-of-line or >=2 spaces ends a sentence);
//! fmt_paragraph() computes an optimal break chain per paragraph suffix
//! with fmt.c's exact cost constants (SHORT_COST = (10n)^2, RAGGED half
//! of that, LINE_COST 70^2, sentence bonus 50^2, NOBREAK 600^2, widow/
//! orphan 200^2/(n+2) and 150^2/(n+2), paren/punct bonus 40^2,
//! LINE_CREDIT 3^2, goal = width*187/200). Breaks are chosen to minimize
//! total cost — NOT a greedy fill — so output matches GNU exactly, not a
//! textbook justification.
//!
//! Modes: -s (each input line is its own paragraph), -c (crown margin:
//! continuation lines keep the indentation of the first line), -t
//! (tagged: first line indent differs from the rest), -u (uniform
//! spacing: 1 space between words, 2 after sentence ends), -p PREFIX,
//! -w WIDTH (0..=2500), -g GOAL. Old syntax: a first argument like
//! `-20` sets the width; digits as a later option give GNU's
//! "invalid option -- N; -WIDTH is recognized only when it is the first
//! option" error, rc 1.
//!
//! Tabs: once a tab is seen anywhere in a file's input, leading
//! whitespace on output lines is re-emitted in tab runs (put_space()).
//! Blank and non-paragraph lines are copied verbatim (copy_rest()).
//!
//! Known divergence (documented): GNU flushes a partial paragraph when
//! its 5000-char / 1000-word buffers fill (fmt.c flush_paragraph); this
//! port buffers the whole paragraph, so only pathological >5000-char
//! paragraphs may break differently. Files are processed sequentially;
//! a missing file reports `cannot open 'X' for reading` and rc 1.

use std::ffi::OsString;
use std::io::Write;

use crate::argopt;
use winux_core::{usage_error, CmdResult, Ctx, Io};

const WIDTH_DEFAULT: usize = 75;
const LEEWAY: usize = 7;
const DEF_INDENT: usize = 3;
const TABWIDTH: usize = 8;
const MAX_WIDTH_LIMIT: usize = 2500; // MAXCHARS/2

const EOF: i32 = -1;

// fmt.c cost constants (all expressed in tenths, squared).
const LINE_COST: i64 = 70 * 70;
const SENTENCE_BONUS: i64 = 50 * 50;
const NOBREAK_COST: i64 = 600 * 600;
const PUNCT_BONUS: i64 = 40 * 40;
const PAREN_BONUS: i64 = 40 * 40;
/// (only consumed by flush_paragraph, which the unbounded-buffer port
/// does not hit)
#[allow(dead_code)]
const LINE_CREDIT: i64 = 3 * 3;
const MAXCOST: i64 = i64::MAX;

fn sqr(n: i64) -> i64 {
    n * n
}
fn short_cost(n: i64) -> i64 {
    sqr(n * 10)
}
fn ragged_cost(n: i64) -> i64 {
    short_cost(n) / 2
}
fn widow_cost(n: usize) -> i64 {
    sqr(200) / (n as i64 + 2)
}
fn orphan_cost(n: usize) -> i64 {
    sqr(150) / (n as i64 + 2)
}

fn isopen(c: u8) -> bool {
    matches!(c, b'(' | b'[' | b'\'' | b'"')
}
fn isclose(c: u8) -> bool {
    matches!(c, b')' | b']' | b'\'' | b'"')
}
fn isperiod(c: u8) -> bool {
    matches!(c, b'.' | b'?' | b'!')
}
fn ispunct(c: u8) -> bool {
    c.is_ascii_punctuation()
}
fn isspace(c: u8) -> bool {
    matches!(c, b' ' | b'\t' | b'\n' | b'\r' | 0x0b | 0x0c)
}

#[derive(Default, Clone)]
struct Word {
    text: usize, // offset into parabuf
    length: usize,
    space: usize,
    paren: bool,
    period: bool,
    punct: bool,
    final_: bool,
    line_length: usize,
    best_cost: i64,
    next_break: usize,
}

struct Fmt<'a> {
    data: &'a [u8],
    pos: usize,
    crown: bool,
    tagged: bool,
    split: bool,
    uniform: bool,
    max_width: usize,
    goal_width: usize,
    prefix: Vec<u8>,
    prefix_lead_space: usize,
    prefix_length: usize,
    prefix_full_length: usize,
    tabs: bool,
    other_indent: usize,
    next_char: i32,
    in_column: usize,
    next_prefix_indent: usize,
    prefix_indent: usize,
    first_indent: usize,
    last_line_length: usize,
    out_column: usize,
    parabuf: Vec<u8>,
    words: Vec<Word>,
}

impl<'a> Fmt<'a> {
    fn getc(&mut self) -> i32 {
        if self.pos >= self.data.len() {
            EOF
        } else {
            let c = self.data[self.pos] as i32;
            self.pos += 1;
            c
        }
    }

    /// get_space(): consume blanks, keeping in_column up to date.
    fn get_space(&mut self, mut c: i32) -> i32 {
        const SP: i32 = b' ' as i32;
        const TAB: i32 = b'\t' as i32;
        loop {
            match c {
                SP => self.in_column += 1,
                TAB => {
                    self.tabs = true;
                    self.in_column = (self.in_column / TABWIDTH + 1) * TABWIDTH;
                }
                _ => return c,
            }
            c = self.getc();
        }
    }

    /// get_prefix(): read the prefix (if any), return the first
    /// non-matching or first non-blank character.
    fn get_prefix(&mut self) -> i32 {
        self.in_column = 0;
        let first = self.getc();
        let mut c = self.get_space(first);
        if self.prefix_length == 0 {
            self.next_prefix_indent = if self.prefix_lead_space < self.in_column {
                self.prefix_lead_space
            } else {
                self.in_column
            };
        } else {
            self.next_prefix_indent = self.in_column;
            let prefix = self.prefix.clone();
            for pc in prefix {
                if c != pc as i32 {
                    return c;
                }
                self.in_column += 1;
                c = self.getc();
            }
            c = self.get_space(c);
        }
        c
    }

    /// same_para(): could a line starting with c belong to the paragraph?
    fn same_para(&self, c: i32) -> bool {
        self.next_prefix_indent == self.prefix_indent
            && self.in_column >= self.next_prefix_indent + self.prefix_full_length
            && c != b'\n' as i32
            && c != EOF
    }

    /// set_other_indent(): pick the secondary indent for continuation lines.
    fn set_other_indent(&mut self, same_paragraph: bool) {
        if self.split {
            self.other_indent = self.first_indent;
        } else if self.crown {
            self.other_indent = if same_paragraph {
                self.in_column
            } else {
                self.first_indent
            };
        } else if self.tagged {
            if same_paragraph && self.in_column != self.first_indent {
                self.other_indent = self.in_column;
            } else if self.other_indent == self.first_indent {
                self.other_indent = if self.first_indent == 0 {
                    DEF_INDENT
                } else {
                    0
                };
            }
        } else {
            self.other_indent = self.first_indent;
        }
    }

    /// put_space(): emit n spaces worth of movement, as tabs when the
    /// input contained tabs.
    fn put_space(&mut self, out: &mut dyn Write, space: usize) -> std::io::Result<()> {
        let space_target = self.out_column + space;
        if self.tabs {
            let tab_target = space_target / TABWIDTH * TABWIDTH;
            if self.out_column + 1 < tab_target {
                while self.out_column < tab_target {
                    out.write_all(b"\t")?;
                    self.out_column = (self.out_column / TABWIDTH + 1) * TABWIDTH;
                }
            }
        }
        while self.out_column < space_target {
            out.write_all(b" ")?;
            self.out_column += 1;
        }
        Ok(())
    }

    /// put_word(): copy the word bytes.
    fn put_word(&mut self, w: usize, out: &mut dyn Write) -> std::io::Result<()> {
        let word = &self.words[w];
        let text = &self.parabuf[word.text..word.text + word.length];
        out.write_all(text)?;
        self.out_column += word.length;
        Ok(())
    }

    /// put_line(): one output line starting at word w, indented.
    fn put_line(&mut self, w: usize, indent: usize, out: &mut dyn Write) -> std::io::Result<()> {
        self.out_column = 0;
        let prefix_indent = self.prefix_indent;
        self.put_space(out, prefix_indent)?;
        out.write_all(&self.prefix)?;
        self.out_column += self.prefix_length;
        self.put_space(out, indent.saturating_sub(self.out_column))?;
        let endline = self.words[w].next_break - 1;
        let mut cur = w;
        while cur != endline {
            self.put_word(cur, out)?;
            let sp = self.words[cur].space;
            self.put_space(out, sp)?;
            cur += 1;
        }
        self.put_word(cur, out)?;
        self.last_line_length = self.out_column;
        out.write_all(b"\n")?;
        Ok(())
    }

    /// put_paragraph(): emit the break chain up to (not including) finish.
    fn put_paragraph(&mut self, finish: usize, out: &mut dyn Write) -> std::io::Result<()> {
        let fi = self.first_indent;
        self.put_line(0, fi, out)?;
        let mut w = self.words[0].next_break;
        while w != finish {
            let oi = self.other_indent;
            self.put_line(w, oi, out)?;
            w = self.words[w].next_break;
        }
        Ok(())
    }

    /// check_punctuation(): set the punctuation attributes of a word.
    fn check_punctuation(&mut self, w: usize) {
        let start = self.words[w].text;
        let length = self.words[w].length;
        let mut finish = start + length - 1;
        let fin = self.parabuf[start + length - 1];
        self.words[w].paren = isopen(self.parabuf[start]);
        self.words[w].punct = ispunct(fin);
        while finish > start && isclose(self.parabuf[finish]) {
            finish -= 1;
        }
        self.words[w].period = isperiod(self.parabuf[finish]);
    }

    /// get_line(): split one input line into words.
    fn get_line(&mut self, mut c: i32) -> i32 {
        loop {
            // scan one word
            let text = self.parabuf.len();
            loop {
                self.parabuf.push(c as u8);
                c = self.getc();
                if c == EOF || isspace(c as u8) {
                    break;
                }
            }
            let length = self.parabuf.len() - text;
            self.in_column += length;
            let start = self.in_column;
            let mut word = Word {
                text,
                length,
                ..Default::default()
            };
            self.words.push(word);
            let idx = self.words.len() - 1;
            self.check_punctuation(idx);
            // scan inter-word space
            c = self.get_space(c);
            let space = self.in_column - start;
            let period = self.words[idx].period;
            let final_ = c == EOF || (period && (c == b'\n' as i32 || space > 1));
            let space = if c == b'\n' as i32 || c == EOF || self.uniform {
                if final_ {
                    2
                } else {
                    1
                }
            } else {
                space
            };
            word = std::mem::take(&mut self.words[idx]);
            word.space = space;
            word.final_ = final_;
            self.words[idx] = word;
            if c == b'\n' as i32 || c == EOF {
                break;
            }
        }
        self.get_prefix()
    }

    /// copy_rest(): pass through a line that is not part of a paragraph.
    fn copy_rest(&mut self, mut c: i32, out: &mut dyn Write) -> std::io::Result<i32> {
        self.out_column = 0;
        if self.in_column > self.next_prefix_indent || (c != b'\n' as i32 && c != EOF) {
            let npi = self.next_prefix_indent;
            self.put_space(out, npi)?;
            let mut s = 0usize;
            while s < self.prefix.len() && self.out_column != self.in_column {
                out.write_all(&[self.prefix[s]])?;
                self.out_column += 1;
                s += 1;
            }
            if c != EOF && c != b'\n' as i32 {
                let n = self.in_column - self.out_column;
                self.put_space(out, n)?;
            }
            if c == EOF && self.in_column >= self.next_prefix_indent + self.prefix_length {
                out.write_all(b"\n")?;
            }
        }
        while c != b'\n' as i32 && c != EOF {
            out.write_all(&[c as u8])?;
            c = self.getc();
        }
        Ok(c)
    }

    /// get_paragraph(): read one paragraph; false at end of input.
    fn get_paragraph(&mut self, out: &mut dyn Write) -> std::io::Result<bool> {
        self.last_line_length = 0;
        let mut c = self.next_char;
        // copy blank lines and non-prefix lines
        while c == b'\n' as i32
            || c == EOF
            || self.next_prefix_indent < self.prefix_lead_space
            || self.in_column < self.next_prefix_indent + self.prefix_full_length
        {
            c = self.copy_rest(c, out)?;
            if c == EOF {
                self.next_char = EOF;
                return Ok(false);
            }
            out.write_all(b"\n")?;
            c = self.get_prefix();
        }
        // first line of a paragraph
        self.prefix_indent = self.next_prefix_indent;
        self.first_indent = self.in_column;
        self.parabuf.clear();
        self.words.clear();
        c = self.get_line(c);
        let same = self.same_para(c);
        self.set_other_indent(same);
        if self.split {
            // one line per paragraph
        } else if self.crown {
            if self.same_para(c) {
                loop {
                    c = self.get_line(c);
                    let s = self.same_para(c);
                    if !(s && self.in_column == self.other_indent) {
                        break;
                    }
                }
            }
        } else if self.tagged {
            if self.same_para(c) && self.in_column != self.first_indent {
                loop {
                    c = self.get_line(c);
                    let s = self.same_para(c);
                    if !(s && self.in_column == self.other_indent) {
                        break;
                    }
                }
            }
        } else {
            loop {
                let s = self.same_para(c);
                if !(s && self.in_column == self.other_indent) {
                    break;
                }
                c = self.get_line(c);
            }
        }
        // last word always ends the paragraph like a sentence end
        let last = self.words.len() - 1;
        self.words[last].period = true;
        self.words[last].final_ = true;
        // the sentinel slot past the last word (fmt.c's word_limit)
        self.words.push(Word {
            text: self.parabuf.len(),
            length: 0,
            next_break: usize::MAX,
            best_cost: 0,
            ..Default::default()
        });
        self.next_char = c;
        Ok(true)
    }

    /// base_cost(): the constant component of breaking before `this`.
    fn base_cost(&self, this: usize) -> i64 {
        let mut cost = LINE_COST;
        if this > 0 {
            let prev = &self.words[this - 1];
            if prev.period {
                if prev.final_ {
                    cost -= SENTENCE_BONUS;
                } else {
                    cost += NOBREAK_COST;
                }
            } else if prev.punct {
                cost -= PUNCT_BONUS;
            } else if this > 1 && self.words[this - 2].final_ {
                cost += widow_cost(self.words[this - 1].length);
            }
        }
        let w = &self.words[this];
        if w.paren {
            cost -= PAREN_BONUS;
        } else if w.final_ {
            cost += orphan_cost(w.length);
        }
        cost
    }

    /// line_cost(): the length-dependent cost of breaking before `next`.
    fn line_cost(&self, next: usize, len: usize, sentinel: usize) -> i64 {
        if next == sentinel {
            return 0;
        }
        let n = self.goal_width as i64 - len as i64;
        let mut cost = short_cost(n);
        if self.words[next].next_break != sentinel {
            let n2 = len as i64 - self.words[next].line_length as i64;
            cost += ragged_cost(n2);
        }
        cost
    }

    /// fmt_paragraph(): the DP over word suffixes (fmt.c lines 858-909).
    /// words[N] is the sentinel slot (fmt.c's word_limit): best_cost 0,
    /// length temporarily max_width so line-length sums stay sane.
    fn fmt_paragraph(&mut self) {
        let sentinel = self.words.len() - 1;
        let max_width = self.max_width;
        self.words[sentinel].best_cost = 0;
        let saved_length = self.words[sentinel].length;
        self.words[sentinel].length = max_width;
        for start in (0..sentinel).rev() {
            let mut best = MAXCOST;
            let mut len = if start == 0 {
                self.first_indent
            } else {
                self.other_indent
            } as i64;
            len += self.words[start].length as i64;
            let mut w = start;
            loop {
                w += 1;
                // consider breaking before w
                let mut wcost = self.line_cost(w, len as usize, sentinel) + self.words[w].best_cost;
                if start == 0 && self.last_line_length > 0 {
                    wcost += ragged_cost(len - self.last_line_length as i64);
                }
                if wcost < best {
                    best = wcost;
                    self.words[start].next_break = w;
                    self.words[start].line_length = len as usize;
                }
                if w == sentinel {
                    break;
                }
                len += (self.words[w - 1].space + self.words[w].length) as i64;
                if len >= max_width as i64 {
                    break;
                }
            }
            self.words[start].best_cost = best.saturating_add(self.base_cost(start));
        }
        self.words[sentinel].length = saved_length;
    }

    /// fmt(): format one whole input buffer.
    fn run(&mut self, out: &mut dyn Write) -> std::io::Result<()> {
        self.tabs = false;
        self.other_indent = 0;
        self.next_char = self.get_prefix();
        while self.get_paragraph(out)? {
            self.fmt_paragraph();
            let finish = self.words.len() - 1; // the sentinel slot
            self.put_paragraph(finish, out)?;
        }
        Ok(())
    }
}

/// xdectoumax(text, 0, MAXCHARS/2, "invalid width", 0): non-numbers give
/// `invalid width: 'x'` with no suffix; numbers above the limit give
/// `invalid width: 'N': Numerical result out of range`. 0 is allowed.
fn parse_width_arg(text: &str) -> Result<usize, String> {
    if text.is_empty() || !text.bytes().all(|b| b.is_ascii_digit()) {
        return Err(format!("invalid width: '{text}'"));
    }
    match text.parse::<u128>() {
        Ok(v) if v <= MAX_WIDTH_LIMIT as u128 => Ok(v as usize),
        _ => Err(format!(
            "invalid width: '{text}': Numerical result out of range"
        )),
    }
}

pub fn fmt_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let strs: Vec<String> = args
        .iter()
        .map(|a| a.to_string_lossy().into_owned())
        .collect();
    let mut crown = false;
    let mut tagged = false;
    let mut split = false;
    let mut uniform = false;
    let mut max_width = WIDTH_DEFAULT;
    let mut max_width_given = false;
    let mut goal_width: Option<usize> = None;
    let mut prefix = String::new();
    let mut operands: Vec<String> = Vec::new();
    // Old option syntax: the FIRST argument may be "-<digits>".
    let mut rest = &strs[..];
    if let Some(first) = strs.first() {
        if first.starts_with('-') && first.len() > 1 && first.as_bytes()[1].is_ascii_digit() {
            match parse_width_arg(&first[1..]) {
                Ok(w) => {
                    max_width = w;
                    max_width_given = true;
                }
                Err(msg) => {
                    let _ = writeln!(io.stderr, "fmt: {msg}");
                    return Ok(1);
                }
            }
            rest = &strs[1..];
        }
    }
    let mut i = 0usize;
    while i < rest.len() {
        let arg = &rest[i];
        if arg == "--" {
            i += 1;
            for a in &rest[i..] {
                operands.push(a.clone());
            }
            break;
        }
        if arg == "-" || !arg.starts_with('-') {
            operands.push(arg.clone());
            i += 1;
            continue;
        }
        if let Some(long) = arg.strip_prefix("--") {
            let (name, inline) = match long.split_once('=') {
                Some((n, v)) => (n.to_string(), Some(v.to_string())),
                None => (long.to_string(), None),
            };
            match name.as_str() {
                "crown-margin" => crown = true,
                "split-only" => split = true,
                "tagged-paragraph" => tagged = true,
                "uniform-spacing" => uniform = true,
                "width" => {
                    let v = match inline {
                        Some(v) => v,
                        None => {
                            i += 1;
                            match rest.get(i) {
                                Some(v) => v.clone(),
                                None => {
                                    let _ = usage_error(
                                        ctx,
                                        io,
                                        "fmt",
                                        &argopt::option_requires_argument(ctx, "--width"),
                                    );
                                    return Ok(1);
                                }
                            }
                        }
                    };
                    match parse_width_arg(&v) {
                        Ok(w) => {
                            max_width = w;
                            max_width_given = true;
                        }
                        Err(msg) => {
                            let _ = writeln!(io.stderr, "fmt: {msg}");
                            return Ok(1);
                        }
                    }
                }
                "goal" => {
                    let v = match inline {
                        Some(v) => v,
                        None => {
                            i += 1;
                            match rest.get(i) {
                                Some(v) => v.clone(),
                                None => {
                                    let _ = usage_error(
                                        ctx,
                                        io,
                                        "fmt",
                                        &argopt::option_requires_argument(ctx, "--goal"),
                                    );
                                    return Ok(1);
                                }
                            }
                        }
                    };
                    match parse_width_arg(&v) {
                        Ok(g) => goal_width = Some(g),
                        Err(msg) => {
                            let _ = writeln!(io.stderr, "fmt: {msg}");
                            return Ok(1);
                        }
                    }
                }
                "prefix" => {
                    prefix = match inline {
                        Some(v) => v,
                        None => {
                            i += 1;
                            match rest.get(i) {
                                Some(v) => v.clone(),
                                None => {
                                    let _ = usage_error(
                                        ctx,
                                        io,
                                        "fmt",
                                        &argopt::option_requires_argument(ctx, "--prefix"),
                                    );
                                    return Ok(1);
                                }
                            }
                        }
                    };
                }
                "help" | "version" => return Ok(0),
                _ => {
                    let _ = usage_error(ctx, io, "fmt", &argopt::unrecognized_option(ctx, arg));
                    return Ok(1);
                }
            }
            i += 1;
            continue;
        }
        // short cluster "0123456789cstuw:p:g:"
        let chars: Vec<char> = arg.chars().skip(1).collect();
        let mut ci = 0usize;
        let mut consumed_next = false;
        while ci < chars.len() {
            let c = chars[ci];
            match c {
                'c' => crown = true,
                's' => split = true,
                't' => tagged = true,
                'u' => uniform = true,
                'w' | 'g' | 'p' => {
                    let rest_s: String = chars[ci + 1..].iter().collect();
                    let v = if !rest_s.is_empty() {
                        ci = chars.len();
                        rest_s
                    } else {
                        match rest.get(i + 1) {
                            Some(v) => {
                                consumed_next = true;
                                ci = chars.len();
                                v.clone()
                            }
                            None => {
                                let _ = usage_error(
                                    ctx,
                                    io,
                                    "fmt",
                                    &argopt::short_option_requires_argument(ctx, &c.to_string()),
                                );
                                return Ok(1);
                            }
                        }
                    };
                    match c {
                        'w' => match parse_width_arg(&v) {
                            Ok(w) => {
                                max_width = w;
                                max_width_given = true;
                            }
                            Err(msg) => {
                                let _ = writeln!(io.stderr, "fmt: {msg}");
                                return Ok(1);
                            }
                        },
                        'g' => match parse_width_arg(&v) {
                            Ok(g) => goal_width = Some(g),
                            Err(msg) => {
                                let _ = writeln!(io.stderr, "fmt: {msg}");
                                return Ok(1);
                            }
                        },
                        _ => prefix = v,
                    }
                }
                d if d.is_ascii_digit() => {
                    let _ = usage_error(
                        ctx,
                        io,
                        "fmt",
                        &format!(
                            "invalid option -- {d}; -WIDTH is recognized only when it is the first\noption; use -w N instead"
                        ),
                    );
                    return Ok(1);
                }
                d => {
                    let _ =
                        usage_error(ctx, io, "fmt", &argopt::invalid_option(ctx, &d.to_string()));
                    return Ok(1);
                }
            }
            ci += 1;
        }
        i += if consumed_next { 2 } else { 1 };
    }
    // goal resolution (fmt.c main lines 393-412)
    let goal = match goal_width {
        Some(g) => {
            if g > max_width {
                let _ = writeln!(io.stderr, "fmt: invalid width: '{g}'");
                return Ok(1);
            }
            if !max_width_given {
                // no -w / -WIDTH given: max_width derives from the goal
                max_width = g + 10;
            }
            g
        }
        None => max_width * (2 * (100 - LEEWAY) + 1) / 200,
    };
    let mut ok = true;
    let prefix_bytes = {
        // set_prefix(): leading spaces counted, trailing spaces cut from
        // prefix_length but KEPT in prefix_full_length
        let trimmed_lead = prefix.trim_start_matches(' ');
        let lead = prefix.len() - trimmed_lead.len();
        let body = trimmed_lead.trim_end_matches(' ');
        let full = trimmed_lead.len();
        let v = body.as_bytes().to_vec();
        (v, lead, full)
    };
    if operands.is_empty() {
        let mut data = Vec::new();
        if std::io::Read::read_to_end(&mut io.stdin, &mut data).is_err() {
            let _ = writeln!(io.stderr, "fmt: read error");
            ok = false;
        }
        let mut f = make_fmt(
            &data,
            crown,
            tagged,
            split,
            uniform,
            max_width,
            goal,
            &prefix_bytes.0,
            prefix_bytes.1,
            prefix_bytes.2,
        );
        if f.run(io.stdout).is_err() {
            ok = false;
        }
    } else {
        for op in &operands {
            if op == "-" {
                let mut data = Vec::new();
                if std::io::Read::read_to_end(&mut io.stdin, &mut data).is_err() {
                    let _ = writeln!(io.stderr, "fmt: read error");
                    ok = false;
                    continue;
                }
                let mut f = make_fmt(
                    &data,
                    crown,
                    tagged,
                    split,
                    uniform,
                    max_width,
                    goal,
                    &prefix_bytes.0,
                    prefix_bytes.1,
                    prefix_bytes.2,
                );
                if f.run(io.stdout).is_err() {
                    ok = false;
                }
                continue;
            }
            // the one operand boundary (drive dialect, carrier colons,
            // shell tree — the 1.x native_path layer)
            let path = std::path::PathBuf::from(crate::fs_win::api_path(ctx, op));
            match std::fs::read(&path) {
                Ok(data) => {
                    let mut f = make_fmt(
                        &data,
                        crown,
                        tagged,
                        split,
                        uniform,
                        max_width,
                        goal,
                        &prefix_bytes.0,
                        prefix_bytes.1,
                        prefix_bytes.2,
                    );
                    if f.run(io.stdout).is_err() {
                        let _ = writeln!(io.stderr, "fmt: read error: {op}");
                        ok = false;
                    }
                }
                Err(e) => {
                    // [GNU] a directory operand: POSIX opens it fine and the
                    // READ fails, so fmt reports its bare read-error line
                    // (probed coreutils 9.4 2026-11-11: `fmt indir` ->
                    // "fmt: read error" rc 1, no operand name, no errno
                    // text); Windows fails the OPEN with
                    // ERROR_ACCESS_DENIED, so the directory attribute
                    // routes to the GNU shape here. A missing file keeps
                    // the probed cannot-open shape.
                    let reason = if e.kind() == std::io::ErrorKind::PermissionDenied
                        && crate::fs_win::file_meta(ctx, op, true)
                            .map(|m| m.is_dir)
                            .unwrap_or(false)
                    {
                        let _ = writeln!(io.stderr, "fmt: read error");
                        ok = false;
                        continue;
                    } else {
                        match e.kind() {
                            std::io::ErrorKind::NotFound => "No such file or directory",
                            std::io::ErrorKind::PermissionDenied => "Permission denied",
                            _ => "Invalid argument",
                        }
                    };
                    let _ = writeln!(io.stderr, "fmt: cannot open '{op}' for reading: {reason}");
                    ok = false;
                }
            }
        }
    }
    Ok(if ok { 0 } else { 1 })
}

#[allow(clippy::too_many_arguments)]
fn make_fmt<'a>(
    data: &'a [u8],
    crown: bool,
    tagged: bool,
    split: bool,
    uniform: bool,
    max_width: usize,
    goal_width: usize,
    prefix: &[u8],
    prefix_lead_space: usize,
    prefix_full_length: usize,
) -> Fmt<'a> {
    Fmt {
        data,
        pos: 0,
        crown,
        tagged,
        split,
        uniform,
        max_width,
        goal_width,
        prefix: prefix.to_vec(),
        prefix_lead_space,
        prefix_length: prefix.len(),
        prefix_full_length,
        tabs: false,
        other_indent: 0,
        next_char: EOF,
        in_column: 0,
        next_prefix_indent: 0,
        prefix_indent: 0,
        first_indent: 0,
        last_line_length: 0,
        out_column: 0,
        parabuf: Vec::new(),
        words: Vec::new(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str], stdin: &str) -> (u32, String, String) {
        let sc = ScratchCtx::new("fmt");
        let mut io = MemIo::new(stdin);
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = fmt_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn basic_refill() {
        // all GNU probed outputs
        assert_eq!(
            run(&[], "The quick brown fox jumps over the lazy dog.\n").1,
            "The quick brown fox jumps over the lazy dog.\n"
        );
        assert_eq!(
            run(&["-w", "20"], "aaa bbb ccc ddd eee fff ggg\n").1,
            "aaa bbb ccc ddd eee\nfff ggg\n"
        );
        assert_eq!(
            run(&["-w", "10"], "aaa bbb ccc ddd eee fff ggg hhh iii jjj kkk lll mmm nnn ooo ppp qqq rrr sss ttt uuu\n").1,
            "aaa\nbbb ccc\nddd eee\nfff ggg\nhhh iii\njjj kkk\nlll mmm\nnnn ooo\nppp qqq\nrrr sss\nttt uuu\n"
        );
        assert_eq!(
            run(&["-w", "15"], "word\n\nanother\n").1,
            "word\n\nanother\n"
        );
        assert_eq!(run(&["-w", "0"], "abc\n").1, "abc\n");
        assert_eq!(
            run(&[], "para one words here\ncontinuing the paragraph with more words to fill it up nicely\n\npara two after blank\n").1,
            "para one words here continuing the paragraph with more words to fill it\nup nicely\n\npara two after blank\n"
        );
        assert_eq!(
            run(&[], "  indented paragraph first line\n  more indent same two spaces bbb ccc ddd eee fff ggg hhh iii jjj kkk lll mmm\n").1,
            "  indented paragraph first line more indent same two spaces bbb ccc ddd\n  eee fff ggg hhh iii jjj kkk lll mmm\n"
        );
    }

    #[test]
    fn split_only() {
        assert_eq!(
            run(
                &["-s", "-w", "20"],
                "aaa bbb ccc ddd eee fff ggg hhh iii jjj kkk lll mmm nnn ooo ppp\n"
            )
            .1,
            "aaa bbb ccc ddd\neee fff ggg hhh iii\njjj kkk lll mmm nnn\nooo ppp\n"
        );
        // short lines stay short under -s
        assert_eq!(
            run(&["-s"], "short line\nalso short\n").1,
            "short line\nalso short\n"
        );
    }

    #[test]
    fn uniform_spacing() {
        assert_eq!(
            run(&["-u", "-w", "20"], "aaa   bbb     ccc ddd\n").1,
            "aaa bbb ccc ddd\n"
        );
    }

    #[test]
    fn crown_and_tagged() {
        assert_eq!(
            run(&["-c"], "aaa bbb ccc ddd eee fff ggg hhh iii jjj\n  second line indent two aaa bbb ccc ddd\n").1,
            "aaa bbb ccc ddd eee fff ggg hhh iii jjj second line indent two aaa bbb\n  ccc ddd\n"
        );
        assert_eq!(
            run(
                &["-t"],
                "aaa bbb ccc ddd eee fff ggg hhh\n    tagged indent four bbb ccc ddd eee\n"
            )
            .1,
            "aaa bbb ccc ddd eee fff ggg hhh tagged indent four bbb ccc ddd eee\n"
        );
    }

    #[test]
    fn sentences_and_costs() {
        assert_eq!(
            run(&["-w", "30"], "one. two. three. four. five. six. seven. eight. nine. ten. eleven. twelve. thirteen.\n").1,
            "one. two. three. four. five.\nsix. seven. eight. nine. ten.\neleven. twelve. thirteen.\n"
        );
        assert_eq!(
            run(&["-w", "30"], "first sentence ends here.  Second sentence follows immediately with more text to wrap around.\n").1,
            "first sentence ends here.\nSecond sentence follows\nimmediately with more text to\nwrap around.\n"
        );
        assert_eq!(
            run(
                &["-w", "25"],
                "aaa bbb.  ccc ddd.  eee fff.  ggg hhh iii jjj kkk lll mmm nnn ooo.\n"
            )
            .1,
            "aaa bbb.  ccc ddd.\neee fff.  ggg hhh iii\njjj kkk lll mmm nnn ooo.\n"
        );
    }

    #[test]
    fn goal_and_paren() {
        assert_eq!(
            run(
                &["-g", "5", "-w", "20"],
                "aaa bbb ccc ddd eee fff ggg hhh iii jjj kkk lll mmm nnn ooo ppp\n"
            )
            .1,
            "aaa bbb\nccc ddd\neee fff\nggg hhh\niii jjj kkk\nlll mmm nnn ooo ppp\n"
        );
        assert_eq!(
            run(
                &["-w", "20"],
                "a (parenthesized thing) b c d e f g h i j k l m n o p q r s t u v w x\n"
            )
            .1,
            "a (parenthesized\nthing) b c d e f g\nh i j k l m n o p\nq r s t u v w x\n"
        );
        assert_eq!(
            run(
                &["-w", "12"],
                "aaaaaaaaaa bb cc dd ee ff gg hh ii jj kk ll mm nn oo pp\n"
            )
            .1,
            "aaaaaaaaaa\nbb cc dd ee\nff gg hh ii\njj kk ll mm\nnn oo pp\n"
        );
    }

    #[test]
    fn old_width_syntax() {
        assert_eq!(
            run(&["-20"], "aaa bbb ccc ddd eee fff ggg\n").1,
            run(&["-w", "20"], "aaa bbb ccc ddd eee fff ggg\n").1
        );
        // digits as a later option are an error
        let (r, _, e) = run(&["-w", "20", "-3"], "x\n");
        assert_eq!(r, 1);
        assert!(e.contains("-WIDTH is recognized only when it is the first"));
    }

    #[test]
    fn width_errors() {
        let (r, _, e) = run(&["-w", "x"], "abc\n");
        assert_eq!(r, 1);
        assert!(e.contains("invalid width: 'x'"));
        let (r, _, _) = run(&["-w", "2501"], "abc\n");
        assert_eq!(r, 1);
        // goal above width
        let (r, _, _) = run(&["-w", "10", "-g", "20"], "abc\n");
        assert_eq!(r, 1);
        assert_eq!(run(&["--bogus"], "x\n").0, 1);
        assert_eq!(run(&["--help"], "").0, 0);
    }

    #[test]
    fn files() {
        let sc = ScratchCtx::new("fmt-files");
        std::fs::write(sc.path("f1"), "one two three\nfour five six\n").unwrap();
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = ["-w", "10", "f1"].iter().map(Into::into).collect();
        let r = fmt_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert!(!io.out().is_empty());
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["missing"].iter().map(Into::into).collect();
        let r = fmt_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("cannot open 'missing' for reading"));
    }
}
