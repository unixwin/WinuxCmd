//! Operands, recursion and file selection: grep.c `grep_command_line_arg`,
//! `grepfile`, `grepdesc`, `grepdirent` plus gnulib `exclude` semantics.
//!
//! * `--include`/`--exclude`/`--exclude-from` patterns apply to
//!   command-line FILES as unanchored fnmatch (whole name, or any suffix
//!   after a `/` — probed: `--exclude=f.c` skips `d/f.c`) and to files met
//!   during recursion as anchored fnmatch of the base name.
//!   `--exclude-dir` does the same for directories (the implicit `.` of a
//!   bare `grep -r` is never excluded, probed). Mixed include/exclude
//!   lists follow gnulib excluded_file_name: the LAST option that matches
//!   decides, and a name no option matches is excluded iff the FIRST
//!   option was an --include (probed both orders).
//! * fnmatch runs with flags 0: `*`/`?` cross `/` and leading dots,
//!   brackets with `!`/`^`, ranges and `[:class:]`, backslash escapes.
//! * `-r` walks physically: symlinks met inside the walk are skipped,
//!   command-line symlinks are followed (FTS_COMFOLLOW). `-R` follows all
//!   symlinks; a directory that is its own ancestor is reported as
//!   "warning: recursive directory loop" (FTS_DC, -s silences it).
//! * Directory entries are visited in the order the OS returns them (fts
//!   without a comparison function; NTFS returns name order, ext4 hash
//!   order — the gate keeps one entry per directory level).
//! * Character devices met in a walk are skipped (READ_COMMAND_LINE_DEVICES
//!   default); `-D skip` also skips command-line devices.

use std::ffi::OsString;
use std::io::Read;
use std::path::{Path, PathBuf};

use super::pattern::PatternMatcher;
use super::scan::{grep_input, print_filename, print_sep_plain, RunState, Stop};
use super::text::Ui;
use super::{Config, Devices, Directories, ListFiles};
use winux_core::Ctx;

/// One include/exclude option list (gnulib struct exclude, segments in
/// option order).
#[derive(Default, Clone, Debug)]
pub struct ExcludeSet {
    segs: Vec<(bool, Vec<String>)>,
}

impl ExcludeSet {
    pub fn add(&mut self, pattern: &str, include: bool) {
        match self.segs.last_mut() {
            Some((inc, pats)) if *inc == include => pats.push(pattern.to_string()),
            _ => self.segs.push((include, vec![pattern.to_string()])),
        }
    }

    /// excluded_file_name: true when `name` must be skipped.
    pub fn excluded(&self, name: &str, anchored: bool) -> bool {
        if self.segs.is_empty() {
            return false;
        }
        for (include, pats) in self.segs.iter().rev() {
            if pats.iter().any(|p| exclude_fnmatch(p, name, anchored)) {
                return !*include;
            }
        }
        self.segs[0].0
    }
}

fn exclude_fnmatch(pattern: &str, name: &str, anchored: bool) -> bool {
    let p: Vec<char> = pattern.chars().collect();
    let n: Vec<char> = name.chars().collect();
    if fnmatch(&p, &n) {
        return true;
    }
    if !anchored {
        for i in 0..n.len() {
            if n[i] == '/' && n.get(i + 1).map_or(true, |&c| c != '/') && fnmatch(&p, &n[i + 1..]) {
                return true;
            }
        }
    }
    false
}

/// POSIX fnmatch with flags 0.
pub fn fnmatch(p: &[char], n: &[char]) -> bool {
    let (mut pi, mut ni) = (0usize, 0usize);
    let mut star: Option<(usize, usize)> = None;
    loop {
        if pi < p.len() {
            match p[pi] {
                '*' => {
                    while pi < p.len() && p[pi] == '*' {
                        pi += 1;
                    }
                    if pi == p.len() {
                        return true;
                    }
                    star = Some((pi, ni));
                    continue;
                }
                '?' if ni < n.len() => {
                    pi += 1;
                    ni += 1;
                    continue;
                }
                '[' if ni < n.len() => {
                    if let Some((hit, next)) = bracket(p, pi, n[ni]) {
                        if hit {
                            pi = next;
                            ni += 1;
                            continue;
                        }
                    } else if n[ni] == '[' {
                        // unterminated bracket: a literal '['
                        pi += 1;
                        ni += 1;
                        continue;
                    }
                }
                '\\' if pi + 1 < p.len() => {
                    if ni < n.len() && n[ni] == p[pi + 1] {
                        pi += 2;
                        ni += 1;
                        continue;
                    }
                }
                c if ni < n.len() && c == n[ni] && c != '?' && c != '[' => {
                    pi += 1;
                    ni += 1;
                    continue;
                }
                _ => {}
            }
        } else if ni == n.len() {
            return true;
        }
        match star {
            Some((sp, sn)) if sn < n.len() => {
                star = Some((sp, sn + 1));
                pi = sp;
                ni = sn + 1;
            }
            _ => return false,
        }
    }
}

/// Bracket at p[pi] == '[' against c: Some((matched, index after ']')),
/// None when unterminated.
fn bracket(p: &[char], pi: usize, c: char) -> Option<(bool, usize)> {
    let mut i = pi + 1;
    let negate = i < p.len() && (p[i] == '!' || p[i] == '^');
    if negate {
        i += 1;
    }
    let mut hit = false;
    let mut first = true;
    loop {
        if i >= p.len() {
            return None;
        }
        let ch = p[i];
        if ch == ']' && !first {
            return Some((hit != negate, i + 1));
        }
        first = false;
        if ch == '[' && i + 1 < p.len() && p[i + 1] == ':' {
            let rest: String = p[i + 2..].iter().collect();
            if let Some(end) = rest.find(":]") {
                let name = &rest[..end];
                if class_match(name, c) {
                    hit = true;
                }
                i += 2 + name.chars().count() + 2;
                continue;
            }
        }
        let mut lo = ch;
        if ch == '\\' && i + 1 < p.len() {
            i += 1;
            lo = p[i];
        }
        if i + 2 < p.len() && p[i + 1] == '-' && p[i + 2] != ']' {
            let mut hi = p[i + 2];
            let mut adv = 3;
            if hi == '\\' && i + 3 < p.len() {
                hi = p[i + 3];
                adv = 4;
            }
            if lo <= c && c <= hi {
                hit = true;
            }
            i += adv;
            continue;
        }
        if lo == c {
            hit = true;
        }
        i += 1;
    }
}

fn class_match(name: &str, c: char) -> bool {
    match name {
        "alpha" => c.is_alphabetic(),
        "digit" => c.is_ascii_digit(),
        "alnum" => c.is_alphanumeric(),
        "upper" => c.is_uppercase(),
        "lower" => c.is_lowercase(),
        "space" => c.is_whitespace(),
        "blank" => c == ' ' || c == '\t',
        "punct" => c.is_ascii_punctuation(),
        "print" => !c.is_control(),
        "graph" => !c.is_control() && !c.is_whitespace(),
        "cntrl" => c.is_control(),
        "xdigit" => c.is_ascii_hexdigit(),
        _ => false,
    }
}

/// Everything the operand loop needs.
pub struct Runner<'r, 'a, 'e> {
    pub cfg: &'r Config,
    pub ui: &'r mut Ui<'a>,
    pub stdin: &'r mut dyn Read,
    pub run: RunState,
    pub matcher: PatternMatcher,
    pub ctx: &'r Ctx<'e>,
    /// grep.c out_file < 0: decided by the first operand's type
    pub out_file_pending: bool,
    pub omit_dot_slash: bool,
    ancestors: Vec<PathBuf>,
}

impl<'r, 'a, 'e> Runner<'r, 'a, 'e> {
    pub fn new(
        cfg: &'r Config,
        ui: &'r mut Ui<'a>,
        stdin: &'r mut dyn Read,
        matcher: PatternMatcher,
        ctx: &'r Ctx<'e>,
        out_file: Option<bool>,
        omit_dot_slash: bool,
    ) -> Self {
        let mut run = RunState::new();
        run.out_file = out_file.unwrap_or(false);
        Runner {
            cfg,
            ui,
            stdin,
            run,
            matcher,
            ctx,
            out_file_pending: out_file.is_none(),
            omit_dot_slash,
            ancestors: Vec::new(),
        }
    }

    fn suppressible(&mut self, name: &str, reason: &str) {
        if !self.cfg.suppress_errors {
            self.ui.error(&format!("{name}: {reason}"));
        }
        self.run.errseen = true;
    }

    /// grep_command_line_arg: Ok(true) when no line was selected.
    pub fn command_line_arg(&mut self, arg: &OsString) -> Result<bool, Stop> {
        let s = arg.to_string_lossy().into_owned();
        if s == "-" {
            let label = self
                .cfg
                .label
                .clone()
                .unwrap_or_else(|| "(standard input)".to_string());
            return self.grep_stdin(&label);
        }
        let native = resolve_operand(self.ctx, &s);
        self.grepfile(&s, &native, true, true)
    }

    fn grep_stdin(&mut self, name: &str) -> Result<bool, Stop> {
        if self.out_file_pending {
            self.out_file_pending = false;
            self.run.out_file = false;
        }
        let count = grep_input(
            self.cfg,
            self.ui,
            &mut self.run,
            &mut self.matcher,
            self.stdin,
            None,
            name.as_bytes(),
        )?;
        self.finish_file(name, count)
    }

    /// grepdesc's tail: -c line, -l/-L names.
    fn finish_file(&mut self, name: &str, count: i64) -> Result<bool, Stop> {
        let cfg = self.cfg;
        if cfg.count_matches {
            if self.run.out_file {
                print_filename(self.ui, cfg, name.as_bytes());
                if cfg.filename_mask {
                    print_sep_plain(self.ui, cfg, b':');
                } else {
                    self.ui.out.push(0);
                }
            }
            self.ui.out.extend_from_slice(format!("{count}\n").as_bytes());
            if cfg.line_buffered {
                self.ui.flush_out();
            }
        }
        let status = count == 0;
        let list = match cfg.list_files {
            ListFiles::None => false,
            ListFiles::Matching => !status,
            ListFiles::NonMatching => status,
        };
        if list {
            print_filename(self.ui, cfg, name.as_bytes());
            self.ui.out.push(if cfg.filename_mask { b'\n' } else { 0 });
            if cfg.line_buffered {
                self.ui.flush_out();
            }
        }
        self.ui.maybe_flush();
        if self.ui.write_error.is_some() {
            return Err(Stop::WriteError);
        }
        Ok(status)
    }

    /// grepfile + grepdesc for a path.
    fn grepfile(
        &mut self,
        display: &str,
        native: &Path,
        follow: bool,
        command_line: bool,
    ) -> Result<bool, Stop> {
        let cfg = self.cfg;
        if is_char_device(display) {
            // DOS devices (NUL, CON, /dev/null...) have no usable metadata
            let skip_devices = cfg.devices == Devices::Skip || !command_line;
            if skip_devices {
                return Ok(true);
            }
            if self.out_file_pending {
                self.out_file_pending = false;
                self.run.out_file = false;
            }
            let mut file = match open_operand(native) {
                Ok(f) => f,
                Err(e) => {
                    let reason = crate::fsutil::gnu_open_reason(self.ctx, display, &e, false);
                    self.suppressible(display, &reason);
                    return Ok(true);
                }
            };
            let name = display.to_string();
            let count = grep_input(
                cfg,
                self.ui,
                &mut self.run,
                &mut self.matcher,
                &mut file,
                None,
                name.as_bytes(),
            )?;
            return self.finish_file(&name, count);
        }
        let meta = if follow {
            std::fs::metadata(native)
        } else {
            std::fs::symlink_metadata(native)
        };
        let meta = match meta {
            Ok(m) => m,
            Err(e) => {
                let reason = crate::fsutil::gnu_open_reason(self.ctx, display, &e, false);
                self.suppressible(display, &reason);
                return Ok(true);
            }
        };
        let is_dir = meta.is_dir();
        let is_device = !is_dir && !meta.is_file();
        let skip_devices = cfg.devices == Devices::Skip || !command_line;
        if skip_devices && is_device {
            return Ok(true);
        }
        if command_line && self.skipped_file(display, true, is_dir) {
            return Ok(true);
        }
        if self.out_file_pending {
            self.out_file_pending = false;
            self.run.out_file = is_dir;
        }
        if is_dir && cfg.directories == Directories::Recurse {
            return self.walk(display, native, command_line);
        }
        if is_dir && cfg.directories == Directories::Skip {
            return Ok(true);
        }
        if is_dir {
            // read(2) on a directory: EISDIR
            self.suppressible(display, "Is a directory");
            return Ok(true);
        }
        let mut file = match open_operand(native) {
            Ok(f) => f,
            Err(e) => {
                let reason = crate::fsutil::gnu_open_reason(self.ctx, display, &e, false);
                self.suppressible(display, &reason);
                return Ok(true);
            }
        };
        let size = if meta.is_file() {
            Some(meta.len())
        } else {
            None
        };
        let name = display.to_string();
        let count = grep_input(
            cfg,
            self.ui,
            &mut self.run,
            &mut self.matcher,
            &mut file,
            size,
            name.as_bytes(),
        )?;
        self.finish_file(&name, count)
    }

    /// skipped_file(name, command_line, is_dir)
    fn skipped_file(&self, name: &str, command_line: bool, is_dir: bool) -> bool {
        let cfg = self.cfg;
        if !is_dir {
            return cfg.excludes.excluded(name, !command_line);
        }
        if cfg.directories == Directories::Skip {
            return true;
        }
        if command_line && self.omit_dot_slash {
            return false;
        }
        let name = if command_line {
            name.trim_end_matches('/')
        } else {
            name
        };
        cfg.exclude_dirs.excluded(name, !command_line)
    }

    fn walk(&mut self, display: &str, native: &Path, command_line: bool) -> Result<bool, Stop> {
        let _ = command_line;
        let real = if self.cfg.deref_recursive {
            std::fs::canonicalize(native).ok()
        } else {
            None
        };
        if let Some(r) = &real {
            if self.ancestors.iter().any(|a| a == r) {
                if !self.cfg.suppress_errors {
                    let shown = self.shown(display);
                    self.ui
                        .error(&format!("{shown}: warning: recursive directory loop"));
                }
                return Ok(true);
            }
            self.ancestors.push(r.clone());
        }
        let r = self.walk_entries(display, native);
        if real.is_some() {
            self.ancestors.pop();
        }
        r
    }

    fn shown(&self, display: &str) -> String {
        if self.omit_dot_slash {
            if let Some(rest) = display.strip_prefix("./") {
                if !rest.is_empty() {
                    return rest.to_string();
                }
            }
        }
        display.to_string()
    }

    fn walk_entries(&mut self, display: &str, native: &Path) -> Result<bool, Stop> {
        let entries = match std::fs::read_dir(native) {
            Ok(e) => e,
            Err(e) => {
                let shown = self.shown(display);
                let reason = crate::fsutil::reason(&e);
                self.suppressible(&shown, reason);
                return Ok(true);
            }
        };
        let mut names: Vec<(OsString, PathBuf)> = Vec::new();
        for entry in entries.flatten() {
            names.push((entry.file_name(), entry.path()));
        }
        let mut status = true;
        for (fname, path) in names {
            let name = fname.to_string_lossy().into_owned();
            let child = if display.ends_with('/') {
                format!("{display}{name}")
            } else {
                format!("{display}/{name}")
            };
            let lmeta = match std::fs::symlink_metadata(&path) {
                Ok(m) => m,
                Err(e) => {
                    let shown = self.shown(&child);
                    self.suppressible(&shown, crate::fsutil::reason(&e));
                    continue;
                }
            };
            let is_link = lmeta.file_type().is_symlink();
            if is_link && !self.cfg.deref_recursive {
                continue; // FTS_SL under FTS_PHYSICAL
            }
            let meta = if is_link {
                match std::fs::metadata(&path) {
                    Ok(m) => m,
                    Err(_) => {
                        // dangling symlink under -R: FTS_SLNONE -> open fails
                        let shown = self.shown(&child);
                        if self.skipped_file(&name, false, false) {
                            continue;
                        }
                        self.suppressible(&shown, "No such file or directory");
                        continue;
                    }
                }
            } else {
                lmeta
            };
            let is_dir = meta.is_dir();
            if self.skipped_file(&name, false, is_dir) {
                continue;
            }
            let shown = self.shown(&child);
            if is_dir {
                status &= self.walk(&child, &path, false)?;
                continue;
            }
            if !meta.is_file() || is_char_device(&name) {
                continue; // devices in a walk: skipped by default
            }
            let mut file = match open_operand(&path) {
                Ok(f) => f,
                Err(e) => {
                    self.suppressible(&shown, crate::fsutil::reason(&e));
                    continue;
                }
            };
            let count = grep_input(
                self.cfg,
                self.ui,
                &mut self.run,
                &mut self.matcher,
                &mut file,
                Some(meta.len()),
                shown.as_bytes(),
            )?;
            status &= self.finish_file(&shown, count)?;
        }
        Ok(status)
    }
}

fn is_char_device(display: &str) -> bool {
    winux_core::path::is_character_device(display)
}

/// Resolve an operand like the 1.x operand boundary: the #1283 carrier
/// colon spelling, drive-relative forms, then fsutil::resolve.
fn resolve_operand(ctx: &Ctx, operand: &str) -> PathBuf {
    let carrier = operand.replace(':', "\u{F03A}");
    let carrier_path = ctx.cwd.join(&carrier);
    if carrier != operand
        && std::fs::metadata(&carrier_path)
            .map(|m| m.is_file())
            .unwrap_or(false)
    {
        return carrier_path;
    }
    let bytes = operand.as_bytes();
    if bytes.len() >= 2
        && bytes[0].is_ascii_alphabetic()
        && bytes[1] == b':'
        && !bytes.get(2).map(|b| *b == b'\\' || *b == b'/').unwrap_or(false)
    {
        let drive = (bytes[0] as char).to_ascii_lowercase();
        let cwd_drive = ctx
            .cwd
            .to_string_lossy()
            .as_bytes()
            .first()
            .map(|b| (*b as char).to_ascii_lowercase());
        let rest = operand[2..].trim_start_matches(['\\', '/']);
        if cwd_drive == Some(drive) {
            return ctx.cwd.join(rest);
        }
        return PathBuf::from(format!("{}:\\{}", drive.to_ascii_uppercase(), rest));
    }
    crate::fsutil::resolve(ctx, operand)
}

/// Open an operand. When the plain Win32 open fails on an absolute path
/// that is not a device, retry the \\?\ literal spelling: Win32 strips
/// trailing dots/spaces ("name." opens "name") and resolves device stems
/// ("NUL.txt" opens NUL); the 1.x operand boundary kept the literal
/// spelling alive (#1332/#1333).
fn open_operand(path: &Path) -> std::io::Result<std::fs::File> {
    let text = path.to_string_lossy();
    let last = text.rsplit(['\\', '/']).next().unwrap_or("");
    let dot_space_tail = last.ends_with('.') || last.ends_with(' ');
    let absolute = path.is_absolute()
        && winux_core::path::resolve_pseudo_device(last).is_none()
        && !text.starts_with(r"\\?\");
    if absolute && dot_space_tail {
        if let Ok(f) = std::fs::File::open(extended_literal(&text)) {
            return Ok(f);
        }
    }
    match std::fs::File::open(path) {
        Ok(f) => Ok(f),
        Err(e) => {
            if absolute && !dot_space_tail {
                if let Ok(f) = std::fs::File::open(extended_literal(&text)) {
                    return Ok(f);
                }
            }
            Err(e)
        }
    }
}

fn extended_literal(native: &str) -> PathBuf {
    if let Some(rest) = native.strip_prefix(r"\\") {
        return PathBuf::from(format!(r"\\?\UNC\{rest}"));
    }
    PathBuf::from(format!(r"\\?\{native}"))
}
