//! The per-input scanner: a line-for-line port of grep.c `grep()`,
//! `fillbuf`, `grepbuf`, `prtext`, `prpending`, `prline` and the
//! `print_line_*` helpers (grep 3.11).
//!
//! Input is streamed through a GNU-shaped buffer: reads are page-aligned
//! and sized like fillbuf's (96 KiB initial window, 1.5x growth only when a
//! single line plus its saved context outgrows it), so memory stays flat
//! for any file size and the NUL-detection points of the binary-file rule
//! ("is there a NUL in the current buffer?") fall where GNU's fall. Only
//! complete lines are searched; the trailing partial line (residue) and the
//! `-B` context lines are carried into the next fill.
//!
//! Binary files (no `-a`/`-z`): the first buffer holding a NUL switches the
//! file to quiet, stop-at-first-match mode (`-c` keeps counting), NULs are
//! zapped to line ends, and "grep: NAME: binary file matches" is reported
//! at the end iff a line was selected after the switch. In a UTF-8 locale an
//! OUTPUT line (or `-o` piece) with an encoding error is suppressed and
//! reported the same way (print_line_head), the rest of the file prints
//! normally — both probed against GNU 3.11.

use std::io::Read;

use super::bytes::{count, memchr, memrchr};
use super::pattern::PatternMatcher;
use super::text::Ui;
use super::{BinaryFiles, Colors, Config};

const PAGE: usize = 4096;
const UWORD: usize = 8;
const INITIAL_BUFSIZE: usize = 96 * 1024;
const SEP_SELECTED: u8 = b':';
const SEP_REJECTED: u8 = b'-';

/// Run-wide output state (grep.c statics that outlive one file).
pub struct RunState {
    /// prtext `static bool used`: a group separator may precede output
    pub used: bool,
    /// errseen
    pub errseen: bool,
    /// whether to print file names (out_file after the -r resolution)
    pub out_file: bool,
    pub buf: Buf,
}

impl RunState {
    pub fn new() -> RunState {
        RunState {
            used: false,
            errseen: false,
            out_file: false,
            buf: Buf::new(),
        }
    }
}

/// Early termination of the whole run.
pub enum Stop {
    /// `-q` matched: exit immediately with this status.
    Exit(u32),
    /// stdout write failed (GNU dies with "write error").
    WriteError,
}

/// The fillbuf buffer: data lives in `data[beg..lim]`.
pub struct Buf {
    data: Vec<u8>,
    alloc: usize,
    beg: usize,
    lim: usize,
    offset: u64,
}

impl Buf {
    fn new() -> Buf {
        Buf {
            data: Vec::new(),
            alloc: 0,
            beg: 0,
            lim: 0,
            offset: 0,
        }
    }

    fn reset(&mut self) {
        self.alloc = INITIAL_BUFSIZE + PAGE + UWORD;
        if self.data.len() < self.alloc + 1 {
            self.data.resize(self.alloc + 1, 0);
        }
        self.beg = PAGE;
        self.lim = PAGE;
        self.offset = 0;
    }

    /// grep.c fillbuf(save, st), with the buffer assumed page aligned.
    fn fill(&mut self, save: usize, src: &mut dyn Read, size: Option<u64>) -> std::io::Result<()> {
        let min_after = PAGE + UWORD;
        let readbuf;
        if min_after <= self.alloc.saturating_sub(self.lim) {
            readbuf = self.lim;
        } else {
            let minsize = save + PAGE;
            let incr_min = minsize as i64 - self.alloc as i64 + min_after as i64;
            if incr_min > 0 {
                let mut alloc_max: Option<usize> = None;
                if let Some(sz) = size {
                    let to_be_read = sz as i64 - self.offset as i64;
                    if to_be_read >= 0 {
                        let a = to_be_read as usize + save + min_after;
                        alloc_max = Some(a.max(self.alloc + incr_min as usize));
                    }
                }
                let mut n = self.alloc + self.alloc / 2;
                if let Some(m) = alloc_max {
                    n = n.min(m);
                }
                n = n.max(self.alloc + incr_min as usize);
                self.alloc = n;
            }
            readbuf = (1 + save).div_ceil(PAGE) * PAGE;
            let need = self.alloc.max(readbuf + PAGE) + 1;
            if self.data.len() < need {
                self.data.resize(need, 0);
            }
            if save > 0 {
                let from = self.lim - save;
                self.data.copy_within(from..self.lim, readbuf - save);
            }
        }
        self.beg = readbuf - save;
        let mut readsize = self.alloc.saturating_sub(UWORD + readbuf);
        readsize -= readsize % PAGE;
        let readsize = readsize.max(PAGE);
        if self.data.len() < readbuf + readsize + 1 {
            self.data.resize(readbuf + readsize + 1, 0);
        }
        let n = loop {
            match src.read(&mut self.data[readbuf..readbuf + readsize]) {
                Ok(n) => break n,
                Err(e) if e.kind() == std::io::ErrorKind::Interrupted => continue,
                Err(e) => {
                    self.lim = readbuf;
                    return Err(e);
                }
            }
        };
        self.lim = readbuf + n;
        self.offset += n as u64;
        Ok(())
    }
}

/// Per-file scanner state (grep.c file-level statics).
struct FileState {
    totalcc: u64,
    totalnl: u64,
    lastnl: usize,
    lastout: Option<usize>,
    outleft: i64,
    pending: i64,
    out_quiet: bool,
    done_on_match: bool,
    encoding_error_output: bool,
    offset_width: usize,
    bufbeg: usize,
}

struct Printer<'p, 'a> {
    cfg: &'p Config,
    ui: &'p mut Ui<'a>,
    run_used: &'p mut bool,
    out_file: bool,
    matcher: &'p mut PatternMatcher,
    fs: FileState,
    filename: &'p [u8],
    write_failed: bool,
}

/// Search one input. Returns the number of selected lines (GNU `grep()`).
/// Read errors are diagnosed (suppressible) and end the file.
pub fn grep_input(
    cfg: &Config,
    ui: &mut Ui,
    run: &mut RunState,
    matcher: &mut PatternMatcher,
    src: &mut dyn Read,
    size: Option<u64>,
    filename: &[u8],
) -> Result<i64, Stop> {
    let eol = cfg.eol;
    let mut buf = std::mem::replace(&mut run.buf, Buf::new());
    buf.reset();
    let mut p = Printer {
        cfg,
        ui,
        run_used: &mut run.used,
        out_file: run.out_file,
        matcher,
        fs: FileState {
            totalcc: 0,
            totalnl: 0,
            lastnl: 0,
            lastout: None,
            outleft: cfg.max_count,
            pending: 0,
            out_quiet: cfg.out_quiet,
            done_on_match: cfg.done_on_match,
            encoding_error_output: false,
            offset_width: 0,
            bufbeg: 0,
        },
        filename,
        write_failed: false,
    };
    let mut errseen = false;
    let result = scan(&mut p, &mut buf, src, size, eol, &mut errseen);
    run.buf = buf;
    if errseen {
        run.errseen = true;
    }
    result
}

fn read_error(p: &mut Printer, e: &std::io::Error, errseen: &mut bool) {
    *errseen = true;
    if !p.cfg.suppress_errors {
        let reason = crate::fsutil::reason(e);
        let name = String::from_utf8_lossy(p.filename).into_owned();
        p.ui.error(&format!("{name}: {reason}"));
    }
}

fn scan(
    p: &mut Printer,
    buf: &mut Buf,
    src: &mut dyn Read,
    size: Option<u64>,
    eol: u8,
    errseen: &mut bool,
) -> Result<i64, Stop> {
    let cfg = p.cfg;
    let mut nlines: i64 = 0;
    let mut residue: usize = 0;
    let mut save: usize = 0;
    let mut nlines_first_null: i64 = -1;
    let mut zap = false;

    if let Err(e) = buf.fill(save, src, size) {
        read_error(p, &e, errseen);
        return Ok(0);
    }
    p.matcher.new_buffer();

    if cfg.align_tabs {
        let mut num: u64 = size.unwrap_or(i64::MAX as u64);
        if cfg.out_line && num < i64::MAX as u64 {
            num += 1;
        }
        let mut w = 0usize;
        loop {
            w += 1;
            num /= 10;
            if num == 0 {
                break;
            }
        }
        p.fs.offset_width = w;
    }

    let mut finished = false;
    loop {
        p.fs.bufbeg = buf.beg;
        if nlines_first_null < 0
            && eol != 0
            && cfg.binary_files != BinaryFiles::Text
            && memchr(0, &buf.data[buf.beg..buf.lim]).is_some()
        {
            if cfg.binary_files == BinaryFiles::WithoutMatch {
                return Ok(0);
            }
            if !cfg.count_matches {
                p.fs.done_on_match = true;
                p.fs.out_quiet = true;
            }
            nlines_first_null = nlines;
            zap = true;
        }
        p.fs.lastnl = buf.beg;
        if p.fs.lastout.is_some() {
            p.fs.lastout = Some(buf.beg);
        }
        let newbeg = buf.beg + save;
        if newbeg == buf.lim {
            break; // EOF
        }
        if zap {
            for b in &mut buf.data[newbeg..buf.lim] {
                if *b == 0 {
                    *b = eol;
                }
            }
            p.matcher.new_buffer();
        }
        let lim = match memrchr(eol, &buf.data[newbeg..buf.lim]) {
            Some(k) => newbeg + k + 1,
            None => newbeg - residue,
        };
        let beg = newbeg - residue;
        residue = buf.lim - lim;

        if beg < lim {
            if p.fs.outleft != 0 {
                nlines += grepbuf(p, &buf.data, beg, lim)?;
            }
            if p.fs.pending > 0 {
                prpending(p, &buf.data, lim)?;
            }
            if (p.fs.outleft == 0 && p.fs.pending == 0)
                || (p.fs.done_on_match && nlines_first_null.max(0) < nlines)
            {
                finished = true;
                break;
            }
        }

        // keep the last out_before lines as leading context
        let mut i: i64 = 0;
        let mut bb = lim;
        while i < cfg.out_before && bb > buf.beg && Some(bb) != p.fs.lastout {
            i += 1;
            bb -= 1;
            while bb > buf.beg && buf.data[bb - 1] != eol {
                bb -= 1;
            }
        }
        if Some(bb) != p.fs.lastout {
            p.fs.lastout = None;
        }
        save = residue + lim - bb;
        if cfg.out_byte {
            p.fs.totalcc += (buf.lim - buf.beg - save) as u64;
        }
        if cfg.out_line {
            nlscan(p, &buf.data, bb, eol);
        }
        if let Err(e) = buf.fill(save, src, size) {
            read_error(p, &e, errseen);
            finished = true;
            break;
        }
        p.matcher.new_buffer();
    }
    if !finished && residue > 0 {
        let at = buf.lim;
        if buf.data.len() <= at {
            buf.data.resize(at + 1, 0);
        }
        buf.data[at] = eol;
        buf.lim += 1;
        p.matcher.new_buffer();
        let start = buf.beg + save - residue;
        if p.fs.outleft != 0 {
            nlines += grepbuf(p, &buf.data, start, buf.lim)?;
        }
        if p.fs.pending > 0 {
            prpending(p, &buf.data, buf.lim)?;
        }
    }

    if cfg.binary_files == BinaryFiles::Binary
        && !cfg.out_quiet
        && (p.fs.encoding_error_output || (0 <= nlines_first_null && nlines_first_null < nlines))
    {
        let name = String::from_utf8_lossy(p.filename).into_owned();
        p.ui.error(&format!("{name}: binary file matches"));
    }
    Ok(nlines)
}

fn nlscan(p: &mut Printer, data: &[u8], lim: usize, eol: u8) {
    if lim > p.fs.lastnl {
        p.fs.totalnl += count(eol, &data[p.fs.lastnl..lim]) as u64;
    }
    p.fs.lastnl = lim;
}

fn grepbuf(p: &mut Printer, data: &[u8], beg: usize, lim: usize) -> Result<i64, Stop> {
    let cfg = p.cfg;
    let outleft0 = p.fs.outleft;
    let mut pos = beg;
    while pos < lim {
        let (b, endp) = match p.matcher.find_line(data, pos, lim) {
            Some((ls, le)) => (ls, le + 1),
            None => {
                if !cfg.out_invert {
                    break;
                }
                (lim, lim)
            }
        };
        if !cfg.out_invert && b == lim {
            break;
        }
        if !cfg.out_invert || pos < b {
            let (prbeg, prend) = if cfg.out_invert { (pos, b) } else { (b, endp) };
            prtext(p, data, prbeg, prend)?;
            if p.fs.outleft == 0 || p.fs.done_on_match {
                if cfg.exit_on_match {
                    p.ui.flush_out();
                    return Err(Stop::Exit(0));
                }
                break;
            }
        }
        pos = endp;
    }
    Ok(outleft0 - p.fs.outleft)
}

fn prpending(p: &mut Printer, data: &[u8], lim: usize) -> Result<(), Stop> {
    let eol = p.cfg.eol;
    if p.fs.lastout.is_none() {
        p.fs.lastout = Some(p.fs.bufbeg);
    }
    while p.fs.pending > 0 && p.fs.lastout.expect("set") < lim {
        let lo = p.fs.lastout.expect("set");
        let nl = lo + memchr(eol, &data[lo..]).expect("eol-terminated");
        prline(p, data, lo, nl + 1, SEP_REJECTED)?;
        p.fs.pending -= 1;
    }
    Ok(())
}

fn prtext(p: &mut Printer, data: &[u8], beg: usize, lim: usize) -> Result<(), Stop> {
    let cfg = p.cfg;
    let eol = cfg.eol;
    if !p.fs.out_quiet && p.fs.pending > 0 {
        prpending(p, data, beg)?;
    }
    let mut pos = beg;
    if !p.fs.out_quiet {
        let bp = p.fs.lastout.unwrap_or(p.fs.bufbeg);
        let mut i: i64 = 0;
        while i < cfg.out_before {
            if pos > bp {
                pos -= 1;
                while pos > p.fs.bufbeg && data[pos - 1] != eol {
                    pos -= 1;
                }
            }
            i += 1;
        }
        if (cfg.out_before >= 0 || cfg.out_after >= 0)
            && *p.run_used
            && Some(pos) != p.fs.lastout
        {
            if let Some(sep) = &cfg.group_separator {
                let color = cfg.colors.as_ref().map(|c| c.sep.clone());
                sgr_start_if(p, color.as_deref());
                p.ui.out.extend_from_slice(sep);
                sgr_end_if(p, color.as_deref());
                p.ui.out.push(b'\n');
            }
        }
        while pos < beg {
            let nl = pos + memchr(eol, &data[pos..]).expect("eol");
            prline(p, data, pos, nl + 1, SEP_REJECTED)?;
            pos = nl + 1;
        }
    }
    let n: i64;
    if cfg.out_invert {
        let mut k: i64 = 0;
        while pos < lim && k < p.fs.outleft {
            let nl = pos + memchr(eol, &data[pos..]).expect("eol");
            if !p.fs.out_quiet {
                prline(p, data, pos, nl + 1, SEP_SELECTED)?;
            }
            pos = nl + 1;
            k += 1;
        }
        n = k;
    } else {
        if !p.fs.out_quiet {
            prline(p, data, beg, lim, SEP_SELECTED)?;
        }
        n = 1;
    }
    p.fs.pending = if p.fs.out_quiet {
        0
    } else {
        cfg.out_after.max(0)
    };
    *p.run_used = true;
    p.fs.outleft -= n;
    Ok(())
}

fn sgr_start(p: &mut Printer, s: &str) {
    if !s.is_empty() {
        let ne = p.cfg.colors.as_ref().map_or(false, |c| c.ne);
        p.ui.out.extend_from_slice(b"\x1b[");
        p.ui.out.extend_from_slice(s.as_bytes());
        p.ui.out.push(b'm');
        if !ne {
            p.ui.out.extend_from_slice(b"\x1b[K");
        }
    }
}

fn sgr_end(p: &mut Printer, s: &str) {
    if !s.is_empty() {
        let ne = p.cfg.colors.as_ref().map_or(false, |c| c.ne);
        p.ui.out.extend_from_slice(if ne { b"\x1b[m" as &[u8] } else { b"\x1b[m\x1b[K" });
    }
}

fn sgr_start_if(p: &mut Printer, s: Option<&str>) {
    if let Some(s) = s {
        sgr_start(p, s);
    }
}

fn sgr_end_if(p: &mut Printer, s: Option<&str>) {
    if let Some(s) = s {
        sgr_end(p, s);
    }
}

fn color_of(c: &Option<Colors>, f: impl Fn(&Colors) -> &String) -> Option<String> {
    c.as_ref().map(|c| f(c).clone())
}

fn print_sep(p: &mut Printer, sep: u8) {
    let color = color_of(&p.cfg.colors, |c| &c.sep);
    sgr_start_if(p, color.as_deref());
    p.ui.out.push(sep);
    sgr_end_if(p, color.as_deref());
}

fn print_offset(p: &mut Printer, pos: u64, which: fn(&Colors) -> &String) {
    let color = color_of(&p.cfg.colors, which);
    sgr_start_if(p, color.as_deref());
    let s = format!("{:>width$}", pos, width = p.fs.offset_width);
    p.ui.out.extend_from_slice(s.as_bytes());
    sgr_end_if(p, color.as_deref());
}

/// print_filename (also used for -l/-L/-c lines).
pub fn print_filename(ui: &mut Ui, cfg: &Config, name: &[u8]) {
    match &cfg.colors {
        Some(c) if !c.filename.is_empty() => {
            ui.out.extend_from_slice(b"\x1b[");
            ui.out.extend_from_slice(c.filename.as_bytes());
            ui.out.extend_from_slice(if c.ne { b"m" as &[u8] } else { b"m\x1b[K" });
            ui.out.extend_from_slice(name);
            ui.out.extend_from_slice(if c.ne { b"\x1b[m" as &[u8] } else { b"\x1b[m\x1b[K" });
        }
        _ => ui.out.extend_from_slice(name),
    }
}

/// print_sep for the -c line (outside a file scan).
pub fn print_sep_plain(ui: &mut Ui, cfg: &Config, sep: u8) {
    match &cfg.colors {
        Some(c) if !c.sep.is_empty() => {
            ui.out.extend_from_slice(b"\x1b[");
            ui.out.extend_from_slice(c.sep.as_bytes());
            ui.out.extend_from_slice(if c.ne { b"m" as &[u8] } else { b"m\x1b[K" });
            ui.out.push(sep);
            ui.out.extend_from_slice(if c.ne { b"\x1b[m" as &[u8] } else { b"\x1b[m\x1b[K" });
        }
        _ => ui.out.push(sep),
    }
}

fn has_encoding_errors(cfg: &Config, bytes: &[u8]) -> bool {
    cfg.utf8 && cfg.binary_files != BinaryFiles::Text && std::str::from_utf8(bytes).is_err()
}

/// print_line_head: false when the output was suppressed (encoding error).
fn print_line_head(p: &mut Printer, data: &[u8], beg: usize, len: usize, lim: usize, sep: u8) -> bool {
    let cfg = p.cfg;
    if has_encoding_errors(cfg, &data[beg..beg + len]) {
        p.fs.encoding_error_output = true;
        return false;
    }
    if p.out_file {
        print_filename(p.ui, cfg, p.filename);
        if cfg.filename_mask {
            print_sep(p, sep);
        } else {
            p.ui.out.push(0);
        }
    }
    if cfg.out_line {
        if p.fs.lastnl < lim {
            nlscan(p, data, beg, cfg.eol);
            p.fs.totalnl += 1;
            p.fs.lastnl = lim;
        }
        print_offset(p, p.fs.totalnl, |c| &c.line_num);
        print_sep(p, sep);
    }
    if cfg.out_byte {
        let pos = p.fs.totalcc + (beg - p.fs.bufbeg) as u64;
        print_offset(p, pos, |c| &c.byte_num);
        print_sep(p, sep);
    }
    if cfg.align_tabs && (p.out_file || cfg.out_line || cfg.out_byte) && len != 0 {
        p.ui.out.push(b'\t');
    }
    true
}

/// print_line_middle: Some(cursor) or None when an -o piece was suppressed.
fn print_line_middle(
    p: &mut Printer,
    data: &[u8],
    beg: usize,
    lim: usize,
    line_color: Option<&str>,
    match_color: Option<&str>,
) -> Option<usize> {
    let cfg = p.cfg;
    let line = &data[beg..lim - 1];
    let mut cur = beg;
    let mut mid: Option<usize> = None;
    while cur < lim {
        let Some((s, mut len)) = p.matcher.piece(line, cur - beg) else {
            break;
        };
        let b = beg + s;
        if b == lim {
            break;
        }
        if len == 0 {
            len = 1;
            if mid.is_none() {
                mid = Some(cur);
            }
        } else {
            if cfg.only_matching {
                let sep = if cfg.out_invert {
                    SEP_REJECTED
                } else {
                    SEP_SELECTED
                };
                if !print_line_head(p, data, b, len, lim, sep) {
                    return None;
                }
            } else {
                if let Some(lc) = line_color {
                    sgr_start(p, lc);
                }
                if let Some(m) = mid.take() {
                    cur = m;
                }
                p.ui.out.extend_from_slice(&data[cur..b]);
            }
            sgr_start_if(p, match_color);
            p.ui.out.extend_from_slice(&data[b..b + len]);
            sgr_end_if(p, match_color);
            if cfg.only_matching {
                p.ui.out.push(cfg.eol);
            }
        }
        cur = b + len;
    }
    if cfg.only_matching {
        cur = lim;
    } else if let Some(m) = mid {
        cur = m;
    }
    Some(cur)
}

fn print_line_tail(p: &mut Printer, data: &[u8], beg: usize, lim: usize, line_color: &str) -> usize {
    let eol = p.cfg.eol;
    let mut eol_size = (lim > beg && data[lim - 1] == eol) as usize;
    if lim - eol_size > beg && data[lim - 1 - eol_size] == b'\r' {
        eol_size += 1;
    }
    let tail = lim - eol_size - beg;
    if tail > 0 {
        sgr_start(p, line_color);
        p.ui.out.extend_from_slice(&data[beg..beg + tail]);
        sgr_end(p, line_color);
    }
    beg + tail
}

fn prline(p: &mut Printer, data: &[u8], beg: usize, lim: usize, sep: u8) -> Result<(), Stop> {
    let cfg = p.cfg;
    let mut beg = beg;
    if !cfg.only_matching && !print_line_head(p, data, beg, lim - beg - 1, lim, sep) {
        return Ok(());
    }
    let matching = (sep == SEP_SELECTED) ^ cfg.out_invert;
    let (line_color, match_color) = match &cfg.colors {
        Some(c) => {
            let sl = (sep == SEP_SELECTED) ^ (cfg.out_invert && c.rv);
            (
                Some(if sl {
                    c.selected_line.clone()
                } else {
                    c.context_line.clone()
                }),
                Some(if sep == SEP_SELECTED {
                    c.selected_match.clone()
                } else {
                    c.context_match.clone()
                }),
            )
        }
        None => (None, None),
    };
    let lc_set = line_color.as_deref().map_or(false, |s| !s.is_empty());
    let mc_set = match_color.as_deref().map_or(false, |s| !s.is_empty());
    if (cfg.only_matching && matching) || (cfg.colors.is_some() && (lc_set || mc_set)) {
        if matching && (cfg.only_matching || mc_set) {
            match print_line_middle(p, data, beg, lim, line_color.as_deref(), match_color.as_deref())
            {
                Some(c) => beg = c,
                None => return Ok(()),
            }
        }
        if !cfg.only_matching && lc_set {
            beg = print_line_tail(p, data, beg, lim, line_color.as_deref().expect("set"));
        }
    }
    if !cfg.only_matching && lim > beg {
        p.ui.out.extend_from_slice(&data[beg..lim]);
    }
    if cfg.line_buffered {
        p.ui.flush_out();
    } else {
        p.ui.maybe_flush();
    }
    if p.ui.write_error.is_some() {
        p.write_failed = true;
        return Err(Stop::WriteError);
    }
    p.fs.lastout = Some(lim);
    Ok(())
}
