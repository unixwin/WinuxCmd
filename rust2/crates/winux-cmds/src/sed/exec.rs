//! sed execution engine — a structural port of GNU sed 4.9 `sed/execute.c`
//! (read_pattern_space / execute_program / do_subst / match_address_p) and
//! the `--debug` printers of `sed/debug.c`.
//!
//! Stream discipline: input is read one record at a time through a
//! `BufRead` (64 KiB; 1 byte under `-u`, GNU's unbuffered reads), `$`
//! is detected by peeking the buffer and then the following files exactly
//! like GNU `test_eof` / `last_file_with_data_p`, and output goes through
//! a buffered writer flushed after every record under `-u`.

use std::fs::File;
use std::io::{BufRead, BufReader, BufWriter, Read, Seek, Write};

use winux_core::Ctx;
use winux_regex::Match;

use crate::fsutil;

use super::compile::{
    Addr, AddrType, Cmd, CmdX, InKind, OutKind, Posix, Program, RangeState, ReplPart, Subst, Trans,
    Translate, REPL_ASIS, REPL_LOWERCASE, REPL_LOWERCASE_FIRST, REPL_MODIFIERS, REPL_UPPERCASE,
    REPL_UPPERCASE_FIRST,
};
use super::Fatal;

pub(crate) struct Settings {
    pub no_default_output: bool,
    pub separate: bool,
    /// GNU `in_place_extension` (`*` = no backup).
    pub in_place: Option<String>,
    pub posix: Posix,
    pub delim: u8,
    pub unbuffered: bool,
    pub debug: bool,
    pub line_len: i64,
    pub follow_symlinks: bool,
    pub utf8: bool,
    /// Runtime "no previous regular expression" diagnostic (GNU bad_prog
    /// with the post-compile error_info).
    pub no_regex: Fatal,
}

enum Src {
    Stdin,
    File(BufReader<File>),
}

enum AppendItem {
    CmdText(usize),
    Text(Vec<u8>),
    File(Vec<u8>),
}

#[derive(Clone, Copy)]
enum Target {
    Main,
    W(usize),
}

struct InPlace {
    target: std::path::PathBuf,
    temp: std::path::PathBuf,
    out: BufWriter<File>,
    attrs: Option<u32>,
}

pub(crate) struct Exec<'a> {
    ctx: &'a Ctx<'a>,
    prog: Program,
    s: Settings,
    stdout: BufWriter<&'a mut dyn Write>,
    stderr: &'a mut dyn Write,
    stdin: BufReader<&'a mut dyn Read>,
    files: Vec<String>,
    next_file: usize,
    cur: Option<Src>,
    in_file_name: String,
    line_number: u64,
    reset_at_next_file: bool,
    bad_count: u64,
    inplace: Option<InPlace>,
    main_missing_newline: bool,
    line: Vec<u8>,
    chomped: bool,
    hold: Vec<u8>,
    hold_chomped: bool,
    accum: Vec<u8>,
    /// GNU `s_accum.chomped`: travels with the scratch line through the
    /// s / e exchanges (observable after `e`, see do_subst).
    accum_chomped: bool,
    append: Vec<AppendItem>,
    replaced: bool,
    last_regex: Option<usize>,
    block_level: usize,
}

fn io_fatal(what: &str, e: &std::io::Error) -> Fatal {
    Fatal {
        msg: format!("sed: couldn't write items to {what}: {}", fsutil::reason(e)),
        status: 4,
    }
}

/// debug.c debug_print_char (signed-char octal quirk included).
fn debug_char(out: &mut Vec<u8>, c: u8) {
    if (0x20..=0x7e).contains(&c) && c != b'\\' {
        out.push(c);
        return;
    }
    out.push(b'\\');
    match c {
        0x07 => out.push(b'a'),
        0x0c => out.push(b'f'),
        b'\r' => out.push(b'r'),
        b'\t' => out.push(b't'),
        0x0b => out.push(b'v'),
        b'\n' => out.push(b'n'),
        b'\\' => out.push(b'\\'),
        _ => {
            let v = (c as i8) as i32 as u32;
            out.extend_from_slice(format!("o{v:03o}").as_bytes());
        }
    }
}

fn case_char(ch: &[u8], upper: bool, utf8: bool) -> Vec<u8> {
    if ch.len() == 1 {
        let b = ch[0];
        return vec![if upper {
            b.to_ascii_uppercase()
        } else {
            b.to_ascii_lowercase()
        }];
    }
    if utf8 {
        if let Ok(s) = std::str::from_utf8(ch) {
            if let Some(c) = s.chars().next() {
                let mut it: Vec<char> = if upper {
                    c.to_uppercase().collect()
                } else {
                    c.to_lowercase().collect()
                };
                // towupper is a 1:1 map: multi-char expansions stay as-is.
                if it.len() == 1 {
                    let mut buf = [0u8; 4];
                    return it.remove(0).encode_utf8(&mut buf).as_bytes().to_vec();
                }
            }
        }
    }
    ch.to_vec()
}

/// execute.c str_append_modified.
fn append_modified(out: &mut Vec<u8>, s: &[u8], mut ty: u8, utf8: bool) {
    if ty == REPL_ASIS {
        out.extend_from_slice(s);
        return;
    }
    let mut i = 0;
    while i < s.len() {
        let w = if utf8 {
            let w = super::compile::utf8_len(s, i);
            if w == 1 && s[i] >= 0x80 {
                // invalid sequence: single byte, first-char modifiers drop
                ty &= !(REPL_LOWERCASE_FIRST | REPL_UPPERCASE_FIRST);
                if ty == REPL_ASIS {
                    out.extend_from_slice(&s[i..]);
                    return;
                }
                out.push(s[i]);
                i += 1;
                continue;
            }
            w
        } else {
            1
        };
        let ch = &s[i..i + w];
        i += w;
        if ty & (REPL_UPPERCASE_FIRST | REPL_LOWERCASE_FIRST) != 0 {
            let up = ty & REPL_UPPERCASE_FIRST != 0;
            out.extend_from_slice(&case_char(ch, up, utf8));
            ty &= !(REPL_LOWERCASE_FIRST | REPL_UPPERCASE_FIRST);
            if ty == REPL_ASIS {
                out.extend_from_slice(&s[i..]);
                return;
            }
        } else {
            let up = ty & REPL_UPPERCASE != 0;
            out.extend_from_slice(&case_char(ch, up, utf8));
        }
    }
}

/// execute.c append_replacement.
fn append_replacement(out: &mut Vec<u8>, parts: &[ReplPart], m: &Match, line: &[u8], utf8: bool) {
    let mut repl_mod = 0u8;
    for p in parts {
        let i = p.subst_id;
        let mut curr = if p.repl_type & REPL_MODIFIERS != 0 {
            p.repl_type
        } else {
            p.repl_type | repl_mod
        };
        repl_mod = 0;
        if !p.prefix.is_empty() {
            append_modified(out, &p.prefix, curr, utf8);
            curr &= !REPL_MODIFIERS;
        }
        if i >= 0 && (i as usize) < m.captures.len() {
            let (b, e) = m.group(i as usize).unwrap_or((0, 0));
            if b == e && p.repl_type & REPL_MODIFIERS != 0 {
                repl_mod = curr & REPL_MODIFIERS;
            } else if b != e {
                append_modified(out, &line[b..e], curr, utf8);
            }
        }
    }
}

#[cfg(windows)]
mod attrs {
    #[link(name = "kernel32")]
    extern "system" {
        fn SetFileAttributesW(name: *const u16, attrs: u32) -> i32;
    }
    pub fn set(path: &std::path::Path, attrs: u32) {
        use std::os::windows::ffi::OsStrExt;
        let w: Vec<u16> = path.as_os_str().encode_wide().chain(Some(0)).collect();
        unsafe {
            SetFileAttributesW(w.as_ptr(), attrs);
        }
    }
    pub fn get(path: &std::path::Path) -> Option<u32> {
        use std::os::windows::fs::MetadataExt;
        std::fs::metadata(path).ok().map(|m| m.file_attributes())
    }
}

#[cfg(not(windows))]
mod attrs {
    pub fn set(_path: &std::path::Path, _attrs: u32) {}
    pub fn get(_path: &std::path::Path) -> Option<u32> {
        None
    }
}

const FILE_ATTRIBUTE_READONLY: u32 = 0x1;

impl<'a> Exec<'a> {
    #[allow(clippy::too_many_arguments)]
    pub(crate) fn new(
        ctx: &'a Ctx<'a>,
        prog: Program,
        s: Settings,
        files: Vec<String>,
        stdin: &'a mut dyn Read,
        stdout: &'a mut dyn Write,
        stderr: &'a mut dyn Write,
    ) -> Exec<'a> {
        let cap = if s.unbuffered { 1 } else { 64 * 1024 };
        Exec {
            ctx,
            prog,
            stdout: BufWriter::with_capacity(64 * 1024, stdout),
            stderr,
            stdin: BufReader::with_capacity(cap, stdin),
            files,
            next_file: 0,
            cur: None,
            in_file_name: String::new(),
            line_number: 0,
            reset_at_next_file: true,
            bad_count: 0,
            inplace: None,
            main_missing_newline: false,
            line: Vec::new(),
            chomped: true,
            hold: Vec::new(),
            hold_chomped: true,
            accum: Vec::new(),
            accum_chomped: true,
            append: Vec::new(),
            replaced: false,
            last_regex: None,
            block_level: 0,
            s,
        }
    }

    // ---- program-level driver ---------------------------------------------------

    /// process_files. Always flushes every stream before returning.
    pub(crate) fn run(&mut self) -> Result<u32, Fatal> {
        let r = self.run_inner();
        let fin = self.finish();
        match r {
            Ok(st) => {
                fin?;
                Ok(st)
            }
            Err(e) => {
                if let Some(ip) = self.inplace.take() {
                    drop(ip.out);
                    let _ = std::fs::remove_file(&ip.temp);
                }
                Err(e)
            }
        }
    }

    fn run_inner(&mut self) -> Result<u32, Fatal> {
        let mut status: i64 = 0;
        while self.read_pattern_space(false)? {
            if self.s.debug {
                self.debug_input()?;
                self.debug_line(false)?;
            }
            let st = self.execute_program()?;
            if st != -1 {
                status = st;
                break;
            }
        }
        self.closedown()?;
        if self.bad_count > 0 {
            status = 2;
        }
        Ok(status as u32)
    }

    fn finish(&mut self) -> Result<(), Fatal> {
        let mut first_err = None;
        for f in self.prog.wfiles.iter_mut() {
            if let OutKind::File(Some(w)) = &mut f.kind {
                if let Err(e) = w.flush() {
                    first_err.get_or_insert(io_fatal(&String::from_utf8_lossy(&f.name), &e));
                }
            }
        }
        if let Err(e) = self.stdout.flush() {
            first_err.get_or_insert(io_fatal("stdout", &e));
        }
        match first_err {
            Some(e) => Err(e),
            None => Ok(()),
        }
    }

    // ---- output -------------------------------------------------------------------

    fn write_target(&mut self, t: Target, data: &[u8]) -> Result<(), Fatal> {
        match t {
            Target::Main => {
                if let Some(ip) = self.inplace.as_mut() {
                    ip.out
                        .write_all(data)
                        .map_err(|e| io_fatal(&ip.target.to_string_lossy(), &e))
                } else {
                    self.stdout
                        .write_all(data)
                        .map_err(|e| io_fatal("stdout", &e))
                }
            }
            Target::W(i) => match &mut self.prog.wfiles[i].kind {
                OutKind::Stdout => self
                    .stdout
                    .write_all(data)
                    .map_err(|e| io_fatal("stdout", &e)),
                OutKind::Stderr => {
                    let _ = self.stdout.flush();
                    self.stderr
                        .write_all(data)
                        .map_err(|e| io_fatal("stderr", &e))
                }
                OutKind::File(Some(w)) => w.write_all(data).map_err(|e| io_fatal("file", &e)),
                OutKind::File(None) => Ok(()),
            },
        }
    }

    fn flush_target(&mut self, t: Target) -> Result<(), Fatal> {
        if !self.s.unbuffered {
            return Ok(());
        }
        let r = match t {
            Target::Main => match self.inplace.as_mut() {
                Some(ip) => ip.out.flush(),
                None => self.stdout.flush(),
            },
            Target::W(i) => match &mut self.prog.wfiles[i].kind {
                OutKind::Stdout => self.stdout.flush(),
                OutKind::Stderr => self.stderr.flush(),
                OutKind::File(Some(w)) => w.flush(),
                OutKind::File(None) => Ok(()),
            },
        };
        r.map_err(|e| io_fatal("stdout", &e))
    }

    fn missing_flag(&mut self, t: Target) -> &mut bool {
        match t {
            Target::Main => &mut self.main_missing_newline,
            Target::W(i) => &mut self.prog.wfiles[i].missing_newline,
        }
    }

    fn output_missing_newline(&mut self, t: Target) -> Result<(), Fatal> {
        if *self.missing_flag(t) {
            let d = [self.s.delim];
            self.write_target(t, &d)?;
            *self.missing_flag(t) = false;
        }
        Ok(())
    }

    fn output_line(&mut self, text: &[u8], nl: bool, t: Target) -> Result<(), Fatal> {
        self.output_missing_newline(t)?;
        if !text.is_empty() {
            self.write_target(t, text)?;
        }
        if nl {
            let d = [self.s.delim];
            self.write_target(t, &d)?;
        } else {
            *self.missing_flag(t) = true;
        }
        self.flush_target(t)
    }

    /// Print the pattern space (`p`, autoprint, `n`, `N` at EOF ...).
    fn output_pattern(&mut self, t: Target) -> Result<(), Fatal> {
        let line = std::mem::take(&mut self.line);
        let r = self.output_line(&line, self.chomped, t);
        self.line = line;
        r
    }

    fn dump_append_queue(&mut self) -> Result<(), Fatal> {
        self.output_missing_newline(Target::Main)?;
        let items = std::mem::take(&mut self.append);
        for item in &items {
            match item {
                AppendItem::CmdText(idx) => {
                    let text = match &self.prog.cmds[*idx].x {
                        CmdX::Text(Some(t)) => t.clone(),
                        _ => Vec::new(),
                    };
                    self.write_target(Target::Main, &text)?;
                }
                AppendItem::Text(t) => self.write_target(Target::Main, t)?,
                AppendItem::File(name) => self.print_file(name)?,
            }
        }
        self.flush_target(Target::Main)
    }

    /// print_file: a missing / unreadable file is silently empty (POSIX).
    fn print_file(&mut self, name: &[u8]) -> Result<(), Fatal> {
        let mut data = Vec::new();
        if name == b"/dev/stdin" {
            let _ = self.stdin.read_to_end(&mut data);
        } else {
            let sname = String::from_utf8_lossy(name).into_owned();
            let path = fsutil::resolve(self.ctx, &sname);
            if std::fs::metadata(&path)
                .map(|m| m.is_dir())
                .unwrap_or(false)
            {
                return Ok(());
            }
            if let Ok(mut f) = File::open(&path) {
                let _ = f.read_to_end(&mut data);
            }
        }
        if !data.is_empty() {
            self.write_target(Target::Main, &data)?;
        }
        Ok(())
    }

    // ---- input --------------------------------------------------------------------

    fn read_record(&mut self) -> Result<bool, Fatal> {
        let delim = self.s.delim;
        let r = match self.cur.as_mut() {
            None => return Ok(false),
            Some(Src::Stdin) => self.stdin.read_until(delim, &mut self.line),
            Some(Src::File(f)) => f.read_until(delim, &mut self.line),
        };
        match r {
            Ok(0) => Ok(false),
            Ok(_) => {
                if self.line.last() == Some(&delim) {
                    self.line.pop();
                } else {
                    self.chomped = false;
                }
                Ok(true)
            }
            Err(e) => Err(Fatal {
                msg: format!(
                    "sed: read error on {}: {}",
                    self.read_name(),
                    fsutil::reason(&e)
                ),
                status: 4,
            }),
        }
    }

    fn read_name(&self) -> String {
        match self.cur {
            Some(Src::Stdin) => "stdin".to_string(),
            _ => self.in_file_name.clone(),
        }
    }

    /// Does the current input have more data (getc/ungetc peek)?
    fn peek_has_data(&mut self) -> bool {
        match self.cur.as_mut() {
            None => false,
            Some(Src::Stdin) => self
                .stdin
                .fill_buf()
                .map(|b| !b.is_empty())
                .unwrap_or(false),
            Some(Src::File(f)) => f.fill_buf().map(|b| !b.is_empty()).unwrap_or(false),
        }
    }

    fn read_pattern_space(&mut self, append: bool) -> Result<bool, Fatal> {
        if !self.append.is_empty() {
            self.dump_append_queue()?;
        }
        self.replaced = false;
        if !append {
            self.line.clear();
        }
        self.chomped = true;
        loop {
            if self.read_record()? {
                break;
            }
            self.closedown()?;
            if self.next_file >= self.files.len() {
                return Ok(false);
            }
            if self.reset_at_next_file {
                self.line_number = 0;
                self.hold.clear();
                self.reset_addresses();
                self.rewind_read_files();
                if self.s.in_place.is_some() {
                    self.main_missing_newline = false;
                }
                self.reset_at_next_file = self.s.separate;
            }
            let name = self.files[self.next_file].clone();
            self.next_file += 1;
            self.open_next_file(&name)?;
        }
        self.line_number += 1;
        Ok(true)
    }

    fn reset_addresses(&mut self) {
        for c in self.prog.cmds.iter_mut() {
            c.range = match c.a1 {
                Some(Addr {
                    kind: AddrType::Num,
                    number: 0,
                    ..
                }) => RangeState::Active,
                _ => RangeState::Inactive,
            };
        }
    }

    fn rewind_read_files(&mut self) {
        for f in self.prog.rfiles.iter_mut() {
            if let InKind::File(Some(r)) = &mut f.kind {
                let _ = r.rewind();
                f.eof = false;
            }
        }
    }

    /// follow_symlink: resolve a symlink chain textually (GNU uses the
    /// final target's name for editing and for `F`).
    ///
    /// [GNU sed 4.9 utils.c follow_symlinks] readlink on a chain element
    /// is benign only when it answers EINVAL ("not a symlink"). Every
    /// other failure — above all ENOENT on a missing operand — is a
    /// `couldn't readlink` panic with status 4 (sd599 oracle probe:
    /// `sed --follow-symlinks -e p nosuch`).
    fn follow_symlink(&self, name: &str) -> Result<String, Fatal> {
        let mut cur = name.to_string();
        for _ in 0..40 {
            let p = fsutil::resolve(self.ctx, &cur);
            let is_link = match std::fs::symlink_metadata(&p) {
                Ok(m) => m.file_type().is_symlink(),
                Err(e) => {
                    return Err(Fatal {
                        msg: format!("sed: couldn't readlink {cur}: {}", fsutil::reason(&e)),
                        status: 4,
                    });
                }
            };
            if !is_link {
                break;
            }
            let target = match std::fs::read_link(&p) {
                Ok(t) => t.to_string_lossy().into_owned(),
                Err(e) => {
                    return Err(Fatal {
                        msg: format!("sed: couldn't readlink {cur}: {}", fsutil::reason(&e)),
                        status: 4,
                    });
                }
            };
            let target = target
                .strip_prefix(r"\\?\")
                .map(str::to_string)
                .unwrap_or(target);
            let absolute = std::path::Path::new(&target).is_absolute() || target.starts_with('/');
            cur = if absolute {
                target
            } else {
                match cur.rfind(['/', '\\']) {
                    Some(i) => format!("{}/{}", &cur[..i], target),
                    None => target,
                }
            };
        }
        Ok(cur)
    }

    fn open_next_file(&mut self, name: &str) -> Result<(), Fatal> {
        self.in_file_name = name.to_string();
        if name == "-" && self.s.in_place.is_none() {
            self.cur = Some(Src::Stdin);
            return Ok(());
        }
        let mut name = name.to_string();
        if self.s.follow_symlinks {
            name = self.follow_symlink(&name)?;
            self.in_file_name = name.clone();
        }
        let path = fsutil::resolve(self.ctx, &name);
        let meta = std::fs::metadata(&path);
        if let Ok(m) = &meta {
            if m.is_dir() {
                if self.s.in_place.is_some() {
                    return Err(Fatal {
                        msg: format!("sed: couldn't edit {name}: not a regular file"),
                        status: 4,
                    });
                }
                return Err(Fatal {
                    msg: format!("sed: read error on {name}: Is a directory"),
                    status: 4,
                });
            }
        }
        let f = match File::open(&path) {
            Ok(f) => f,
            Err(e) => {
                let reason = fsutil::gnu_open_reason(self.ctx, &name, &e, false);
                let _ = self.stdout.flush();
                let _ = writeln!(self.stderr, "sed: can't read {name}: {reason}");
                self.bad_count += 1;
                self.cur = None;
                return Ok(());
            }
        };
        let cap = if self.s.unbuffered { 1 } else { 64 * 1024 };
        self.cur = Some(Src::File(BufReader::with_capacity(cap, f)));
        if self.s.in_place.is_some() {
            let regular = meta.map(|m| m.is_file()).unwrap_or(false)
                && winux_core::path::resolve_pseudo_device(&name).is_none();
            if !regular {
                return Err(Fatal {
                    msg: format!("sed: couldn't edit {name}: not a regular file"),
                    status: 4,
                });
            }
            let dir = match path.parent() {
                Some(d) if !d.as_os_str().is_empty() => d.to_path_buf(),
                _ => self.ctx.cwd.clone(),
            };
            let (temp, out) = create_temp(&dir).map_err(|e| Fatal {
                msg: format!(
                    "sed: couldn't open temporary file {}/sedXXXXXX: {}",
                    dir.display(),
                    fsutil::reason(&e)
                ),
                status: 4,
            })?;
            self.inplace = Some(InPlace {
                attrs: attrs::get(&path),
                target: path,
                temp,
                out: BufWriter::with_capacity(64 * 1024, out),
            });
            self.main_missing_newline = false;
        }
        Ok(())
    }

    fn closedown(&mut self) -> Result<(), Fatal> {
        if self.cur.is_none() {
            return Ok(());
        }
        self.cur = None;
        if let Some(mut ip) = self.inplace.take() {
            if let Err(e) = ip.out.flush() {
                let _ = std::fs::remove_file(&ip.temp);
                return Err(io_fatal(&ip.temp.to_string_lossy(), &e));
            }
            drop(ip.out);
            let ext = self.s.in_place.clone().unwrap_or_default();
            if ext != "*" {
                let backup = backup_name(&ext, &self.in_file_name);
                let bpath = fsutil::resolve(self.ctx, &backup);
                if let Err(e) = std::fs::rename(&ip.target, &bpath) {
                    let _ = std::fs::remove_file(&ip.temp);
                    return Err(Fatal {
                        msg: format!(
                            "sed: cannot rename {}: {}",
                            self.in_file_name,
                            fsutil::reason(&e)
                        ),
                        status: 4,
                    });
                }
            } else if let Some(a) = ip.attrs {
                // a read-only target refuses MoveFileEx replacement
                if a & FILE_ATTRIBUTE_READONLY != 0 {
                    attrs::set(&ip.target, a & !FILE_ATTRIBUTE_READONLY);
                }
            }
            if let Err(e) = std::fs::rename(&ip.temp, &ip.target) {
                let _ = std::fs::remove_file(&ip.temp);
                return Err(Fatal {
                    msg: format!(
                        "sed: cannot rename {}: {}",
                        ip.temp.display(),
                        fsutil::reason(&e)
                    ),
                    status: 4,
                });
            }
            if let Some(a) = ip.attrs {
                attrs::set(&ip.target, a);
            }
        }
        Ok(())
    }

    /// test_eof: is the current line the last one (`$`)?
    fn test_eof(&mut self) -> Result<bool, Fatal> {
        if self.cur.is_none() || !self.peek_has_data() {
            return Ok(self.s.separate || self.last_file_with_data_p()?);
        }
        Ok(false)
    }

    fn last_file_with_data_p(&mut self) -> Result<bool, Fatal> {
        loop {
            self.closedown()?;
            if self.next_file >= self.files.len() {
                return Ok(true);
            }
            let name = self.files[self.next_file].clone();
            self.next_file += 1;
            self.open_next_file(&name)?;
            if self.cur.is_some() && self.peek_has_data() {
                return Ok(false);
            }
        }
    }

    // ---- regex / addresses ------------------------------------------------------------

    fn resolve_regex(&mut self, idx: Option<usize>) -> Result<usize, Fatal> {
        match idx {
            Some(i) => {
                self.last_regex = Some(i);
                Ok(i)
            }
            None => self.last_regex.ok_or_else(|| self.s.no_regex.clone()),
        }
    }

    fn match_an_address_p(&mut self, addr: Addr) -> Result<bool, Fatal> {
        Ok(match addr.kind {
            AddrType::Null => true,
            AddrType::Regex(idx) => {
                let i = self.resolve_regex(idx)?;
                let r = &mut self.prog.regexes[i];
                r.re.is_match_with(&mut r.cache, &self.line)
            }
            AddrType::NumMod => {
                self.line_number >= addr.number
                    && (self.line_number - addr.number).is_multiple_of(addr.step)
            }
            AddrType::Step | AddrType::StepMod => addr.number <= self.line_number,
            AddrType::Last => self.test_eof()?,
            AddrType::Num => addr.number == self.line_number,
        })
    }

    fn match_address_p(&mut self, pc: usize) -> Result<bool, Fatal> {
        let (a1, a2, range) = {
            let c = &self.prog.cmds[pc];
            (c.a1, c.a2, c.range)
        };
        let a1 = match a1 {
            None => return Ok(true),
            Some(a) => a,
        };
        if range != RangeState::Active {
            let a2 = match a2 {
                None => return self.match_an_address_p(a1),
                Some(a) => a,
            };
            if a1.kind == AddrType::Num {
                if range == RangeState::Closed || self.line_number < a1.number {
                    return Ok(false);
                }
            } else if !self.match_an_address_p(a1)? {
                return Ok(false);
            }
            self.prog.cmds[pc].range = RangeState::Active;
            match a2.kind {
                AddrType::Regex(_) => return Ok(true),
                AddrType::Num => {
                    if self.line_number >= a2.number {
                        self.prog.cmds[pc].range = RangeState::Closed;
                    }
                    return Ok(self.line_number <= a2.number || self.match_an_address_p(a1)?);
                }
                AddrType::Step => {
                    let n = self.line_number + a2.step;
                    if let Some(a) = self.prog.cmds[pc].a2.as_mut() {
                        a.number = n;
                    }
                    return Ok(true);
                }
                AddrType::StepMod => {
                    let n = self.line_number + a2.step - (self.line_number % a2.step);
                    if let Some(a) = self.prog.cmds[pc].a2.as_mut() {
                        a.number = n;
                    }
                    return Ok(true);
                }
                _ => {}
            }
        }
        let a2 = self.prog.cmds[pc].a2.expect("range implies a2");
        if a2.kind == AddrType::Num {
            if self.line_number >= a2.number {
                self.prog.cmds[pc].range = RangeState::Closed;
            }
            return Ok(self.line_number <= a2.number);
        }
        if self.match_an_address_p(a2)? {
            self.prog.cmds[pc].range = RangeState::Closed;
        }
        Ok(true)
    }

    // ---- commands -----------------------------------------------------------------------

    fn execute_program(&mut self) -> Result<i64, Fatal> {
        let mut pc = 0usize;
        let end = self.prog.cmds.len();
        while pc < end {
            if self.s.debug {
                let mut b = b"COMMAND: ".to_vec();
                self.debug_command(&mut b, pc);
                self.dbg(&b)?;
            }
            let matched = self.match_address_p(pc)?;
            let cmd_bang = self.prog.cmds[pc].bang;
            if matched != cmd_bang {
                let ch = self.prog.cmds[pc].ch;
                match ch {
                    b'a' => self.append.push(AppendItem::CmdText(pc)),
                    b'{' | b'b' => {
                        pc = self.jump_of(pc);
                        continue;
                    }
                    b'}' | b'#' | b':' => {}
                    b'c' | b'd' => {
                        if ch == b'c' && self.prog.cmds[pc].range != RangeState::Active {
                            if let CmdX::Text(Some(t)) = &self.prog.cmds[pc].x {
                                let t = t[..t.len() - 1].to_vec();
                                self.output_line(&t, true, Target::Main)?;
                            }
                        }
                        if self.s.debug {
                            self.dbg(b"END-OF-CYCLE:\n")?;
                        }
                        return Ok(-1);
                    }
                    b'D' => {
                        let d = self.s.delim;
                        match self.line.iter().position(|&b| b == d) {
                            None => return Ok(-1),
                            Some(p) => {
                                self.line.drain(..=p);
                                pc = 0;
                                if self.s.debug {
                                    self.debug_line(false)?;
                                }
                                continue;
                            }
                        }
                    }
                    b'e' => self.cmd_e(pc)?,
                    b'g' => {
                        self.line.clear();
                        self.line.extend_from_slice(&self.hold);
                        self.chomped = self.hold_chomped;
                        if self.s.debug {
                            self.debug_line(true)?;
                        }
                    }
                    b'G' => {
                        self.line.push(self.s.delim);
                        self.line.extend_from_slice(&self.hold);
                        self.chomped = self.hold_chomped;
                        if self.s.debug {
                            self.debug_line(false)?;
                        }
                    }
                    b'h' => {
                        self.hold.clear();
                        self.hold.extend_from_slice(&self.line);
                        self.hold_chomped = self.chomped;
                        if self.s.debug {
                            self.debug_line(true)?;
                        }
                    }
                    b'H' => {
                        self.hold.push(self.s.delim);
                        self.hold.extend_from_slice(&self.line);
                        self.hold_chomped = self.chomped;
                        if self.s.debug {
                            self.debug_line(true)?;
                        }
                    }
                    b'i' => {
                        if let CmdX::Text(Some(t)) = &self.prog.cmds[pc].x {
                            let t = t[..t.len() - 1].to_vec();
                            self.output_line(&t, true, Target::Main)?;
                        }
                    }
                    b'l' => {
                        let n = match self.prog.cmds[pc].x {
                            CmdX::Int(n) => n,
                            _ => -1,
                        };
                        let len = if n == -1 { self.s.line_len } else { n };
                        self.do_list(len as i32 as i64)?;
                    }
                    b'n' => {
                        if !self.s.no_default_output {
                            self.output_pattern(Target::Main)?;
                        }
                        if self.test_eof()? || !self.read_pattern_space(false)? {
                            if self.s.debug {
                                self.dbg(b"END-OF-CYCLE:\n")?;
                            }
                            return Ok(-1);
                        }
                        if self.s.debug {
                            self.debug_line(false)?;
                        }
                    }
                    b'N' => {
                        self.line.push(self.s.delim);
                        if self.test_eof()? || !self.read_pattern_space(true)? {
                            if self.s.debug {
                                self.dbg(b"END-OF-CYCLE:\n")?;
                            }
                            self.line.pop();
                            if self.s.posix == Posix::Extended && !self.s.no_default_output {
                                self.output_pattern(Target::Main)?;
                            }
                            return Ok(-1);
                        }
                        if self.s.debug {
                            self.debug_line(false)?;
                        }
                    }
                    b'p' => self.output_pattern(Target::Main)?,
                    b'P' => {
                        let d = self.s.delim;
                        let line = std::mem::take(&mut self.line);
                        let r = match line.iter().position(|&b| b == d) {
                            Some(p) => self.output_line(&line[..p], true, Target::Main),
                            None => self.output_line(&line, self.chomped, Target::Main),
                        };
                        self.line = line;
                        r?;
                    }
                    b'q' | b'Q' => {
                        if ch == b'q' {
                            if !self.s.no_default_output {
                                self.output_pattern(Target::Main)?;
                            }
                            self.dump_append_queue()?;
                        }
                        let n = match self.prog.cmds[pc].x {
                            CmdX::Int(n) => n,
                            _ => -1,
                        };
                        return Ok(if n == -1 { 0 } else { n });
                    }
                    b'r' => {
                        if let CmdX::Read { fname, append } = &self.prog.cmds[pc].x {
                            let (fname, append) = (fname.clone(), *append);
                            if append {
                                self.append.push(AppendItem::File(fname));
                            } else {
                                self.print_file(&fname)?;
                            }
                        }
                    }
                    b'R' => {
                        if let CmdX::RFile(i) = self.prog.cmds[pc].x {
                            self.cmd_big_r(i)?;
                        }
                    }
                    b's' => {
                        let x = std::mem::replace(&mut self.prog.cmds[pc].x, CmdX::None);
                        let r = if let CmdX::Subst(sub) = &x {
                            self.do_subst(sub)
                        } else {
                            Ok(())
                        };
                        self.prog.cmds[pc].x = x;
                        r?;
                        if self.s.debug {
                            self.debug_line(false)?;
                        }
                    }
                    b't' => {
                        if self.replaced {
                            self.replaced = false;
                            pc = self.jump_of(pc);
                            continue;
                        }
                    }
                    b'T' => {
                        if !self.replaced {
                            pc = self.jump_of(pc);
                            continue;
                        } else {
                            self.replaced = false;
                        }
                    }
                    b'w' => {
                        if let CmdX::WFile(i) = self.prog.cmds[pc].x {
                            if !matches!(self.prog.wfiles[i].kind, OutKind::File(None)) {
                                self.output_pattern(Target::W(i))?;
                            }
                        }
                    }
                    b'W' => {
                        if let CmdX::WFile(i) = self.prog.cmds[pc].x {
                            let d = self.s.delim;
                            let line = std::mem::take(&mut self.line);
                            let r = match line.iter().position(|&b| b == d) {
                                Some(p) => self.output_line(&line[..p], true, Target::W(i)),
                                None => self.output_line(&line, self.chomped, Target::W(i)),
                            };
                            self.line = line;
                            r?;
                        }
                    }
                    b'x' => {
                        std::mem::swap(&mut self.line, &mut self.hold);
                        std::mem::swap(&mut self.chomped, &mut self.hold_chomped);
                        if self.s.debug {
                            self.debug_line(false)?;
                            self.debug_line(true)?;
                        }
                    }
                    b'y' => {
                        if let CmdX::Translate(t) = &self.prog.cmds[pc].x {
                            translate(&mut self.line, &mut self.accum, t);
                        }
                        if self.s.debug {
                            self.debug_line(false)?;
                        }
                    }
                    b'z' => {
                        self.line.clear();
                        if self.s.debug {
                            self.debug_line(false)?;
                        }
                    }
                    b'=' => {
                        self.output_missing_newline(Target::Main)?;
                        let mut s = self.line_number.to_string().into_bytes();
                        s.push(self.s.delim);
                        self.write_target(Target::Main, &s)?;
                        self.flush_target(Target::Main)?;
                    }
                    b'F' => {
                        self.output_missing_newline(Target::Main)?;
                        let mut s = self.in_file_name.clone().into_bytes();
                        s.push(self.s.delim);
                        self.write_target(Target::Main, &s)?;
                        self.flush_target(Target::Main)?;
                    }
                    other => {
                        return Err(Fatal {
                            msg: format!("sed: INTERNAL ERROR: Bad cmd {}", other as char),
                            status: 4,
                        })
                    }
                }
            }
            pc += 1;
        }
        if self.s.debug {
            self.dbg(b"END-OF-CYCLE:\n")?;
        }
        if !self.s.no_default_output {
            self.output_pattern(Target::Main)?;
        }
        Ok(-1)
    }

    fn jump_of(&self, pc: usize) -> usize {
        match self.prog.cmds[pc].x {
            CmdX::Jump(t) => t,
            _ => pc + 1,
        }
    }

    fn cmd_big_r(&mut self, i: usize) -> Result<(), Fatal> {
        let delim = self.s.delim;
        let mut text = Vec::new();
        let f = &mut self.prog.rfiles[i];
        if f.eof {
            return Ok(());
        }
        let n = match &mut f.kind {
            InKind::Stdin => self.stdin.read_until(delim, &mut text).unwrap_or(0),
            InKind::File(Some(r)) => r.read_until(delim, &mut text).unwrap_or(0),
            InKind::File(None) => 0,
        };
        if n == 0 {
            f.eof = true;
        } else {
            self.append.push(AppendItem::Text(text));
        }
        Ok(())
    }

    fn cmd_e(&mut self, pc: usize) -> Result<(), Fatal> {
        let text = match &self.prog.cmds[pc].x {
            CmdX::Text(Some(t)) if !t.is_empty() => Some(t[..t.len() - 1].to_vec()),
            _ => None,
        };
        match text {
            None => {
                let cmd = self.line.clone();
                let mut out = run_shell(self.ctx, &cmd, &mut *self.stderr)?;
                if out.last() == Some(&self.s.delim) {
                    out.pop();
                }
                self.exchange_in(out);
            }
            Some(cmd) => {
                self.output_missing_newline(Target::Main)?;
                let out = run_shell(self.ctx, &cmd, &mut *self.stderr)?;
                self.write_target(Target::Main, &out)?;
                self.flush_target(Target::Main)?;
            }
        }
        Ok(())
    }

    /// line_exchange(&line, &s_accum) after filling s_accum with `out`.
    fn exchange_in(&mut self, out: Vec<u8>) {
        let old = std::mem::replace(&mut self.line, out);
        self.accum = old;
        std::mem::swap(&mut self.chomped, &mut self.accum_chomped);
    }

    fn do_list(&mut self, line_len: i64) -> Result<(), Fatal> {
        self.output_missing_newline(Target::Main)?;
        let delim = self.s.delim;
        let mut out: Vec<u8> = Vec::with_capacity(self.line.len() * 2 + 4);
        let mut width: i64 = 0;
        let mut obuf: Vec<u8> = Vec::with_capacity(8);
        for &c in &self.line {
            obuf.clear();
            if (0x20..=0x7e).contains(&c) {
                obuf.push(c);
                if c == b'\\' {
                    obuf.push(b'\\');
                }
            } else {
                obuf.push(b'\\');
                match c {
                    0x07 => obuf.push(b'a'),
                    0x08 => obuf.push(b'b'),
                    0x0c => obuf.push(b'f'),
                    b'\n' => obuf.push(b'n'),
                    b'\r' => obuf.push(b'r'),
                    b'\t' => obuf.push(b't'),
                    0x0b => obuf.push(b'v'),
                    _ => obuf.extend_from_slice(format!("{c:03o}").as_bytes()),
                }
            }
            let olen = obuf.len() as i64;
            if width + olen >= line_len && line_len > 0 {
                out.push(b'\\');
                out.push(delim);
                width = 0;
            }
            out.extend_from_slice(&obuf);
            width += olen;
        }
        out.push(b'$');
        out.push(delim);
        self.write_target(Target::Main, &out)?;
        self.flush_target(Target::Main)
    }

    fn do_subst(&mut self, sub: &Subst) -> Result<(), Fatal> {
        let ri = self.resolve_regex(sub.regex)?;
        let utf8 = self.s.utf8;
        let first = {
            let r = &mut self.prog.regexes[ri];
            r.re.find_at_bytes_with(&mut r.cache, &self.line, 0)
        };
        let mut m = match first {
            Some(m) => m,
            None => return Ok(()),
        };
        if self.s.debug {
            let mut b = b"MATCHED REGEX REGISTERS\n".to_vec();
            for i in 0..m.captures.len() {
                let Some((s, e)) = m.group(i) else { break };
                b.extend_from_slice(format!("  regex[{i}] = {s}-{e} '").as_bytes());
                b.extend_from_slice(&self.line[s..e]);
                b.extend_from_slice(b"'\n");
            }
            self.dbg(&b)?;
        }
        // GNU fast path for an empty replacement (s/^x// and s/x$//): the
        // line is trimmed in place and s_accum stays untouched — which is
        // observable through its `chomped` flag after an `e` exchange.
        if sub.replacement.is_empty() && sub.numb <= 1 {
            if m.begin == 0 && !sub.global {
                self.replaced = true;
                self.line.drain(..m.end);
                return self.post_subst(sub);
            } else if m.end == self.line.len() {
                self.replaced = true;
                self.line.truncate(m.begin);
                return self.post_subst(sub);
            }
        }
        let mut accum = std::mem::take(&mut self.accum);
        accum.clear();
        let mut start = 0usize;
        let mut last_end = 0usize;
        let mut count: u64 = 0;
        let line_len = self.line.len();
        loop {
            let offset = m.begin;
            let mut matched = m.end - m.begin;
            if start < offset {
                accum.extend_from_slice(&self.line[start..offset]);
                start = offset;
            }
            let mut again = true;
            if (matched > 0 || count == 0 || offset > last_end) && {
                count += 1;
                count >= sub.numb
            } {
                self.replaced = true;
                append_replacement(&mut accum, &sub.replacement, &m, &self.line, utf8);
                again = sub.global;
            } else {
                if matched == 0 {
                    if start < line_len {
                        matched = 1;
                    } else {
                        break;
                    }
                }
                accum.extend_from_slice(&self.line[offset..offset + matched]);
            }
            start = offset + matched;
            last_end = m.end;
            if !(again && start <= line_len) {
                break;
            }
            let found = {
                let r = &mut self.prog.regexes[ri];
                r.re.find_at_bytes_with(&mut r.cache, &self.line, start)
            };
            match found {
                Some(mm) => m = mm,
                None => break,
            }
        }
        if start < line_len {
            accum.extend_from_slice(&self.line[start..]);
        }
        std::mem::swap(&mut self.line, &mut accum);
        self.accum = accum;
        self.accum_chomped = self.chomped;
        if count < sub.numb {
            return Ok(());
        }
        self.post_subst(sub)
    }

    fn post_subst(&mut self, sub: &Subst) -> Result<(), Fatal> {
        if sub.print & 1 != 0 {
            self.output_pattern(Target::Main)?;
        }
        if sub.eval {
            let cmd = self.line.clone();
            let mut out = run_shell(self.ctx, &cmd, &mut *self.stderr)?;
            if out.last() == Some(&self.s.delim) {
                out.pop();
            }
            self.exchange_in(out);
        }
        if sub.print & 2 != 0 {
            self.output_pattern(Target::Main)?;
        }
        if let Some(w) = sub.outf {
            self.output_pattern(Target::W(w))?;
        }
        Ok(())
    }

    // ---- --debug -------------------------------------------------------------------------

    fn dbg(&mut self, b: &[u8]) -> Result<(), Fatal> {
        self.stdout.write_all(b).map_err(|e| io_fatal("stdout", &e))
    }

    fn debug_input(&mut self) -> Result<(), Fatal> {
        let name = match self.cur {
            Some(Src::Stdin) => "STDIN".to_string(),
            _ => self.in_file_name.clone(),
        };
        let s = format!("INPUT:   '{}' line {}\n", name, self.line_number);
        self.dbg(s.as_bytes())
    }

    fn debug_line(&mut self, hold: bool) -> Result<(), Fatal> {
        let mut b: Vec<u8> = if hold {
            b"HOLD:    ".to_vec()
        } else {
            b"PATTERN: ".to_vec()
        };
        let src = if hold { &self.hold } else { &self.line };
        for &c in src.iter() {
            debug_char(&mut b, c);
        }
        b.push(b'\n');
        self.dbg(&b)
    }

    fn debug_command(&mut self, out: &mut Vec<u8>, pc: usize) {
        let lvl = &mut self.block_level;
        debug_print_command(&self.prog, pc, lvl, out);
    }
}

/// debug.c debug_print_command (shared with the SED PROGRAM dump).
pub(crate) fn debug_print_command(prog: &Program, pc: usize, level: &mut usize, out: &mut Vec<u8>) {
    let sc = &prog.cmds[pc];
    if sc.ch == b'}' {
        *level = level.saturating_sub(1);
    }
    for _ in 0..*level {
        out.extend_from_slice(b"  ");
    }
    if let Some(a) = &sc.a1 {
        debug_addr(prog, a, out);
    }
    if sc.a2.is_some() {
        out.push(b',');
    }
    if let Some(a) = &sc.a2 {
        debug_addr(prog, a, out);
    }
    let mut bang = sc.bang;
    if sc.ch == b'{' {
        bang = !bang;
    }
    if bang {
        out.push(b'!');
    }
    if sc.a1.is_some() || sc.a2.is_some() {
        out.push(b' ');
    }
    debug_function(prog, sc, out);
    out.push(b'\n');
    if sc.ch == b'{' {
        *level += 1;
    }
}

fn debug_regex(prog: &Program, idx: Option<usize>, out: &mut Vec<u8>) {
    match idx {
        None => out.extend_from_slice(b"//"),
        Some(i) => {
            out.push(b'/');
            for &c in &prog.regexes[i].pattern {
                if c == b'/' {
                    out.extend_from_slice(b"\\/");
                } else {
                    debug_char(out, c);
                }
            }
            out.push(b'/');
        }
    }
}

fn debug_regex_flags(prog: &Program, idx: Option<usize>, addr: bool, out: &mut Vec<u8>) {
    if let Some(i) = idx {
        let r = &prog.regexes[i];
        if r.icase {
            out.push(if addr { b'I' } else { b'i' });
        }
        if r.mline {
            out.push(if addr { b'M' } else { b'm' });
        }
    }
}

fn debug_addr(prog: &Program, a: &Addr, out: &mut Vec<u8>) {
    match a.kind {
        AddrType::Null => out.extend_from_slice(b"[ADDR-NULL]"),
        AddrType::Regex(i) => {
            debug_regex(prog, i, out);
            debug_regex_flags(prog, i, true, out);
        }
        AddrType::Num => out.extend_from_slice(a.number.to_string().as_bytes()),
        AddrType::NumMod => out.extend_from_slice(format!("{}~{}", a.number, a.step).as_bytes()),
        AddrType::Step => out.extend_from_slice(format!("+{}", a.step).as_bytes()),
        AddrType::StepMod => out.extend_from_slice(format!("~{}", a.step).as_bytes()),
        AddrType::Last => out.push(b'$'),
    }
}

fn debug_function(prog: &Program, sc: &Cmd, out: &mut Vec<u8>) {
    out.push(sc.ch);
    match sc.ch {
        b':' => {
            if let CmdX::Label(l) = &sc.x {
                out.extend_from_slice(l);
            }
        }
        b'a' | b'c' | b'i' => {
            out.push(b'\\');
            if let CmdX::Text(Some(t)) = &sc.x {
                out.extend_from_slice(t);
            }
        }
        b'b' | b't' | b'T' => {
            if let CmdX::Jump(t) = sc.x {
                if t < prog.cmds.len() {
                    if let CmdX::Label(l) = &prog.cmds[t].x {
                        out.push(b' ');
                        out.extend_from_slice(l);
                    }
                }
            }
        }
        b'e' => {
            out.push(b' ');
            if let CmdX::Text(Some(t)) = &sc.x {
                out.extend_from_slice(t);
            }
        }
        b'l' | b'L' | b'q' | b'Q' => {
            if let CmdX::Int(n) = sc.x {
                if n != -1 {
                    out.extend_from_slice(format!(" {n}").as_bytes());
                }
            }
        }
        b'r' => {
            if let CmdX::Read { fname, .. } = &sc.x {
                out.push(b' ');
                out.extend_from_slice(fname);
            }
        }
        b'R' => {
            if let CmdX::RFile(i) = sc.x {
                out.push(b' ');
                out.extend_from_slice(&prog.rfiles[i].name);
            }
        }
        b's' => {
            if let CmdX::Subst(s) = &sc.x {
                debug_subst(prog, s, out);
            }
        }
        b'w' | b'W' => {
            if let CmdX::WFile(i) = sc.x {
                out.extend_from_slice(&prog.wfiles[i].name);
            }
        }
        b'y' => {
            if let CmdX::Translate(t) = &sc.x {
                out.push(b'/');
                for c in &t.src {
                    out.extend_from_slice(c);
                }
                out.push(b'/');
                for c in &t.dst {
                    out.extend_from_slice(c);
                }
                out.push(b'/');
            }
        }
        _ => {}
    }
}

fn debug_subst(prog: &Program, s: &Subst, out: &mut Vec<u8>) {
    debug_regex(prog, s.regex, out);
    let mut last = REPL_ASIS;
    for p in &s.replacement {
        if p.repl_type != last {
            out.push(b'\\');
            let t = p.repl_type;
            if t == 0 {
                out.push(b'E');
            } else if t == REPL_UPPERCASE {
                out.push(b'U');
            } else if t == REPL_LOWERCASE {
                out.push(b'L');
            } else if t & REPL_MODIFIERS == REPL_UPPERCASE_FIRST {
                out.push(b'u');
            } else if t & REPL_MODIFIERS == REPL_LOWERCASE_FIRST {
                out.push(b'l');
            }
            last = t;
        }
        out.extend_from_slice(&p.prefix);
        if p.subst_id != -1 {
            if p.subst_id == 0 {
                out.push(b'&');
            } else {
                out.extend_from_slice(format!("\\{}", p.subst_id).as_bytes());
            }
        }
    }
    out.push(b'/');
    debug_regex_flags(prog, s.regex, false, out);
    if s.global {
        out.push(b'g');
    }
    if s.eval {
        out.push(b'e');
    }
    if s.print != 0 {
        out.push(b'p');
    }
    if s.numb != 0 {
        out.extend_from_slice(s.numb.to_string().as_bytes());
    }
    if let Some(w) = s.outf {
        out.push(b'w');
        out.extend_from_slice(&prog.wfiles[w].name);
    }
}

/// `y`: the 256-byte table or GNU translate_mb over characters.
fn translate(line: &mut Vec<u8>, scratch: &mut Vec<u8>, t: &Translate) {
    match &t.trans {
        Trans::Table(tab) => {
            for b in line.iter_mut() {
                *b = tab[*b as usize];
            }
        }
        Trans::Pairs(pairs) => {
            scratch.clear();
            let mut i = 0;
            while i < line.len() {
                let w = super::compile::utf8_len(line, i);
                let ch = &line[i..i + w];
                match pairs.iter().find(|(s, _)| s.as_slice() == ch) {
                    Some((_, d)) => scratch.extend_from_slice(d),
                    None => scratch.extend_from_slice(ch),
                }
                i += w;
            }
            std::mem::swap(line, scratch);
        }
    }
}

fn backup_name(ext: &str, name: &str) -> String {
    if ext.contains('*') {
        ext.replace('*', name)
    } else {
        format!("{name}{ext}")
    }
}

fn create_temp(dir: &std::path::Path) -> std::io::Result<(std::path::PathBuf, File)> {
    use std::time::{SystemTime, UNIX_EPOCH};
    const ALNUM: &[u8] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    let mut seed = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_nanos() as u64)
        .unwrap_or(0)
        ^ (std::process::id() as u64).rotate_left(32)
        ^ (dir as *const _ as *const u8 as u64);
    let mut last_err = None;
    for _ in 0..100 {
        let mut name = String::from("sed");
        for _ in 0..6 {
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            name.push(ALNUM[(seed % ALNUM.len() as u64) as usize] as char);
        }
        let p = dir.join(&name);
        match std::fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&p)
        {
            Ok(f) => return Ok((p, f)),
            Err(e) if e.kind() == std::io::ErrorKind::AlreadyExists => last_err = Some(e),
            Err(e) => return Err(e),
        }
    }
    Err(last_err.unwrap_or_else(|| std::io::Error::other("temp")))
}

/// popen("cmd", "r") analog: `sh -c` when a POSIX shell is reachable on
/// PATH (the product ships one), else `cmd.exe /d /c`.
fn run_shell(ctx: &Ctx, cmd: &[u8], stderr: &mut dyn Write) -> Result<Vec<u8>, Fatal> {
    use crate::childproc::{build_child_command, resolve_child, ChildResolve};
    use std::process::Stdio;
    let text = String::from_utf8_lossy(cmd).into_owned();
    let mut command = match resolve_child("sh", ctx) {
        ChildResolve::Found(launch) => {
            build_child_command(&launch, &["-c".to_string(), text.clone()], ctx)
        }
        _ => {
            let mut c = std::process::Command::new("cmd.exe");
            c.arg("/d").arg("/c").arg(&text);
            c.current_dir(&ctx.cwd);
            c
        }
    };
    command
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());
    match command.output() {
        Ok(o) => {
            let _ = stderr.write_all(&o.stderr);
            Ok(o.stdout)
        }
        Err(_) => Err(Fatal {
            msg: "sed: error in subprocess".to_string(),
            status: 4,
        }),
    }
}
