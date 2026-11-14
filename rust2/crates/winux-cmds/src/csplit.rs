//! `csplit` — GNU coreutils csplit semantics (probe base: coreutils 9.4,
//! target/probe/c1.sh; C++ value model: `src/commands/csplit.cpp` on main,
//! itself validated against GNU in audit #1462).
//!
//! GNU contract:
//! * `csplit [OPTION]... FILE PATTERN...`; PATTERN is INTEGER,
//!   /REGEXP/[OFFSET], or %REGEXP%[OFFSET] (skip), each optionally
//!   followed by {N} or {*} repeating the PREVIOUS pattern. Every segment
//!   (and the final remainder, even when empty) is written to
//!   `PREFIX%0*dDIGITS` files as soon as its closing pattern applies, and
//!   its byte count is printed to stdout unless -s/-q.
//! * The whole pattern list is parsed BEFORE any file is created: a bad
//!   pattern, an invalid regexp, or a decreasing line number aborts with
//!   no output files; an equal line number warns on stderr and proceeds.
//! * Error paths stream like GNU: a regex/line-number failure flushes the
//!   scanned-so-far lines into the in-progress output file (its size is
//!   printed) before the diagnostic, except a regex with a negative
//!   offset, which buffers and leaves the file empty (size 0). Created
//!   files are removed on failure unless -k/--keep-files.
//! * --suppress-matched drops the line at the split boundary (which with
//!   an offset is the boundary line, not necessarily the matched line).
//! * -z/--elide-empty-files suppresses empty output files (their size
//!   lines too). Line-number patterns must be non-decreasing (equal only
//!   warns). The LAST delimiter closes a regexp (`/a/b/` searches "a/b").
//! * Exit status 1 on any failure, 0 otherwise.
//! * GNU has no -h short option (getopt_long matches --help only long):
//!   `-h` is "invalid option -- 'h'" rc 1 (argopt H_REJECT_MARKER).
//!
//! Windows value model / documented subset:
//! * Regexps use the shared BRE engine (`crate::bre`, the gnulib
//!   re_compile_pattern dialect incl. `\|` alternation, `+`, `?`, classes,
//!   anchors). Not supported by that engine and therefore by this port:
//!   `\(` `\)` groups, `\{m,n\}` intervals, backreferences — those are
//!   compile errors (stderr wording, not gate-gated; the gate's regexp
//!   cases stay inside the supported subset and are GNU-diffed).
//! * -b suffix formats accept exactly one conversion in diuoxX with the
//!   GNU flag span `-+ #'0123456789.`; `#` is only valid with oxX and `'`
//!   only with diu. Width/precision/flags inside the conversion are
//!   accepted by the validator like GNU; rendering supports the plain
//!   forms the gate exercises (bare d/u/o/x/X, as GNU prints in the C
//!   locale).
//! * FILE and the derived output names resolve against the explicit
//!   ctx.cwd (the driver's scratch dir in the gate), never the process
//!   cwd.

use std::ffi::OsString;
use std::io::{Read as _, Write as _};
use std::path::PathBuf;

use crate::argopt::{self, OptSpec};
use crate::bre;
use crate::fsutil;
use crate::gnuhelp;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('f'),
        long: "prefix",
        takes_value: true,
    },
    OptSpec {
        short: Some('b'),
        long: "suffix-format",
        takes_value: true,
    },
    OptSpec {
        short: Some('n'),
        long: "digits",
        takes_value: true,
    },
    OptSpec {
        short: Some('k'),
        long: "keep-files",
        takes_value: false,
    },
    OptSpec {
        short: Some('s'),
        long: "quiet",
        takes_value: false,
    },
    OptSpec {
        short: Some('q'),
        long: "silent",
        takes_value: false,
    },
    OptSpec {
        short: Some('z'),
        long: "elide-empty-files",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "suppress-matched",
        takes_value: false,
    },
    // GNU csplit has no -h: reject it like getopt_long does.
    argopt::H_REJECT_SPEC,
];

const CSPLIT_HELP_BODY: &str = r#"Usage: csplit [OPTION]... FILE PATTERN...
Output pieces of FILE separated by PATTERN(s) to files 'xx00', 'xx01', ...,
and output byte counts of each piece to standard output.

Read standard input if FILE is -

Mandatory arguments to long options are mandatory for short options too.
  -b, --suffix-format=FORMAT  use sprintf FORMAT instead of %02d
  -f, --prefix=PREFIX        use PREFIX instead of 'xx'
  -k, --keep-files           do not remove output files on errors
      --suppress-matched     suppress the lines matching PATTERN
  -n, --digits=DIGITS        use specified number of digits instead of 2
  -s, --quiet, --silent      do not print counts of output file sizes
  -z, --elide-empty-files    suppress empty output files
      --help        display this help and exit
      --version     output version information and exit

Each PATTERN may be:
  INTEGER            copy up to but not including specified line number
  /REGEXP/[OFFSET]   copy up to but not including a matching line
  %REGEXP%[OFFSET]   skip to, but not including a matching line
  {INTEGER}          repeat the previous pattern specified number of times
  {*}                repeat the previous pattern as many times as possible

A line OFFSET is an integer optionally preceded by '+' or '-'

"#;

// ---------------------------------------------------------------------------
// Pattern model
// ---------------------------------------------------------------------------

enum PatternKind {
    LineNumber(usize),
    Regex { regex: bre::Regex, offset: i64 },
}

struct ParsedPattern {
    kind: PatternKind,
    skip: bool,
}

#[derive(Default, Clone, Copy)]
struct RepeatSpec {
    has_repeat: bool,
    until_exhausted: bool,
    count: usize,
}

struct BoundPattern {
    text: String,
    pattern: ParsedPattern,
    repeat: RepeatSpec,
}

/// parse_offset: an unsigned offset is a positive one ("/re/5" == "/re/+5").
fn parse_offset(text: &str) -> Result<i64, String> {
    if text.is_empty() {
        return Ok(0);
    }
    let (sign, digits) = match text.as_bytes()[0] {
        b'+' => (1i64, &text[1..]),
        b'-' => (-1i64, &text[1..]),
        _ => (1i64, text),
    };
    if digits.is_empty() || !digits.bytes().all(|b| b.is_ascii_digit()) {
        return Err("invalid pattern offset".to_string());
    }
    digits
        .parse::<i64>()
        .map(|v| sign * v)
        .map_err(|_| "invalid pattern offset".to_string())
}

fn compile_pattern_regex(text: &str) -> Result<bre::Regex, String> {
    bre::Regex::compile(text).map_err(|e| format!("invalid regular expression: {e}"))
}

fn parse_pattern(text: &str) -> Result<ParsedPattern, String> {
    if text.is_empty() {
        return Err("invalid pattern".to_string());
    }
    let first = text.as_bytes()[0];
    if first == b'/' || first == b'%' {
        // The LAST delimiter closes the pattern: /a/b/ is the regexp "a/b".
        let close = match text.rfind(first as char) {
            Some(0) | None => return Err("missing pattern delimiter".to_string()),
            Some(c) => c,
        };
        let offset = parse_offset(&text[close + 1..])?;
        let regex_text = &text[1..close];
        let regex = compile_pattern_regex(regex_text)?;
        return Ok(ParsedPattern {
            kind: PatternKind::Regex { regex, offset },
            skip: first == b'%',
        });
    }
    // Integer patterns: xstrtoumax accepts a leading '+'; 0 has its own
    // diagnostic.
    let digits = text.strip_prefix('+').unwrap_or(text);
    if digits.is_empty() || !digits.bytes().all(|b| b.is_ascii_digit()) {
        return Err("invalid pattern".to_string());
    }
    let line_number = digits
        .parse::<usize>()
        .map_err(|_| "invalid line number".to_string())?;
    if line_number == 0 {
        return Err("0: line number must be greater than zero".to_string());
    }
    Ok(ParsedPattern {
        kind: PatternKind::LineNumber(line_number),
        skip: false,
    })
}

fn parse_repeat(text: &str) -> Result<RepeatSpec, String> {
    let mut repeat = RepeatSpec::default();
    if text.is_empty() || !text.starts_with('{') {
        return Ok(repeat);
    }
    if text.len() < 3 || !text.ends_with('}') {
        return Err(format!("{text}: '}}' is required in repeat count"));
    }
    repeat.has_repeat = true;
    let inner = &text[1..text.len() - 1];
    if inner == "*" {
        repeat.until_exhausted = true;
        return Ok(repeat);
    }
    if inner.is_empty() || !inner.bytes().all(|b| b.is_ascii_digit()) {
        return Err("invalid repeat count".to_string());
    }
    match inner.parse::<usize>() {
        Ok(n) => repeat.count = n,
        Err(_) => return Err("invalid repeat count".to_string()),
    }
    Ok(repeat)
}

// ---------------------------------------------------------------------------
// Suffix format (sprintf subset: one diuoxX conversion)
// ---------------------------------------------------------------------------

fn render_suffix_format(fmt: &str, value: usize) -> Result<String, String> {
    validate_suffix_format(fmt)?;
    render_validated(fmt, value)
}

/// GNU validates the -b format when options are parsed (check_format_conv_
/// type), so an invalid format dies before any input is read or a file is
/// created. Exactly one conversion in diuoxX; the flag span is
/// `-+ #'0123456789.`; `#` only with o/x/X and `'` only with d/i/u.
fn validate_suffix_format(fmt: &str) -> Result<(), String> {
    let b = fmt.as_bytes();
    let mut conversions = 0usize;
    let mut i = 0usize;
    while i < b.len() {
        if b[i] != b'%' {
            i += 1;
            continue;
        }
        if i + 1 < b.len() && b[i + 1] == b'%' {
            i += 2;
            continue;
        }
        conversions += 1;
        let mut j = i + 1;
        let mut flags = String::new();
        let mut thousands = false;
        let mut alt = false;
        while j < b.len() && b"-+ #'0123456789.".contains(&b[j]) {
            if b[j] == b'\'' {
                thousands = true;
            }
            if b[j] == b'#' {
                alt = true;
            }
            flags.push(b[j] as char);
            j += 1;
        }
        if j >= b.len() || !"diuoxX".contains(b[j] as char) {
            return Err("invalid suffix format".to_string());
        }
        let conv = b[j] as char;
        if matches!(conv, 'd' | 'i' | 'u') && alt {
            return Err(format!(
                "invalid flags in conversion specification: %{flags}{conv}"
            ));
        }
        if matches!(conv, 'o' | 'x' | 'X') && thousands {
            return Err(format!(
                "invalid flags in conversion specification: %{flags}{conv}"
            ));
        }
        i = j + 1;
    }
    if conversions != 1 {
        return Err("invalid suffix format".to_string());
    }
    Ok(())
}

fn render_validated(fmt: &str, value: usize) -> Result<String, String> {
    let b = fmt.as_bytes();
    let mut conversions = 0usize;
    let mut out = String::new();
    // Parsed fields of the single conversion.
    let mut left_align = false;
    let mut zero_pad = false;
    let mut alt = false;
    let mut plus = false;
    let mut space = false;
    let mut thousands = false;
    let mut width: Option<usize> = None;
    let mut precision: Option<usize> = None;
    let mut conv = ' ';
    let mut i = 0usize;
    while i < b.len() {
        if b[i] != b'%' {
            out.push(b[i] as char);
            i += 1;
            continue;
        }
        if i + 1 < b.len() && b[i + 1] == b'%' {
            out.push('%');
            i += 2;
            continue;
        }
        conversions += 1;
        let mut j = i + 1;
        while j < b.len() && b"-+ #'0123456789.".contains(&b[j]) {
            match b[j] as char {
                '-' => left_align = true,
                '0' => zero_pad = true,
                '#' => alt = true,
                '+' => plus = true,
                ' ' => space = true,
                '\'' => thousands = true,
                '.' => {
                    let mut digits = String::new();
                    j += 1;
                    while j < b.len() && b[j].is_ascii_digit() {
                        digits.push(b[j] as char);
                        j += 1;
                    }
                    precision = Some(digits.parse().unwrap_or(0));
                    continue;
                }
                d => {
                    let mut digits = String::new();
                    while j < b.len() && b[j].is_ascii_digit() {
                        digits.push(b[j] as char);
                        j += 1;
                    }
                    width = Some(digits.parse().unwrap_or(0));
                    continue;
                }
            }
            j += 1;
        }
        if j >= b.len() || !"diuoxX".contains(b[j] as char) {
            return Err("invalid suffix format".to_string());
        }
        conv = b[j] as char;
        // '#' is compatible only with o/x/X; ''' only with d/i/u.
        if matches!(conv, 'd' | 'i' | 'u') && alt {
            return Err(format!(
                "invalid flags in conversion specification: %{conv}#"
            ));
        }
        if matches!(conv, 'o' | 'x' | 'X') && thousands {
            return Err(format!(
                "invalid flags in conversion specification: %{conv}'"
            ));
        }
        i = j + 1;
    }
    if conversions != 1 {
        return Err("invalid suffix format".to_string());
    }

    // Render the body.
    let mut body = match conv {
        'd' | 'i' | 'u' => value.to_string(),
        'o' => format!("{value:o}"),
        'x' => format!("{value:x}"),
        'X' => format!("{value:X}"),
        _ => return Err("invalid suffix format".to_string()),
    };
    // '#' with o forces a leading zero; with x/X the 0x/0X prefix.
    if alt {
        match conv {
            'o' if !body.starts_with('0') => body = format!("0{body}"),
            'x' => body = format!("0x{body}"),
            'X' => body = format!("0X{body}"),
            _ => {}
        }
    }
    let sign: &'static str = if matches!(conv, 'd' | 'i') && plus {
        "+"
    } else if matches!(conv, 'd' | 'i') && space {
        " "
    } else {
        ""
    };
    // Precision: minimum digit count.
    if let Some(p) = precision {
        while body.len() < p {
            body = format!("0{body}");
        }
    }
    let mut zeros_before = String::new();
    let mut spaces_before = String::new();
    let mut spaces_after = String::new();
    if let Some(w) = width {
        let cur = sign.len() + body.len();
        if cur < w {
            if left_align {
                spaces_after = " ".repeat(w - cur);
            } else if zero_pad && precision.is_none() {
                zeros_before = "0".repeat(w - cur);
            } else {
                spaces_before = " ".repeat(w - cur);
            }
        }
    }
    Ok(format!(
        "{out}{spaces_before}{sign}{zeros_before}{body}{spaces_after}"
    ))
}

fn make_filename(
    prefix: &str,
    suffix_format: Option<&str>,
    digits: usize,
    n: usize,
) -> Result<String, String> {
    match suffix_format {
        None => Ok(format!("{prefix}{n:0digits$}")),
        Some(fmt) => {
            let suffix = render_suffix_format(fmt, n)?;
            Ok(format!("{prefix}{suffix}"))
        }
    }
}

// ---------------------------------------------------------------------------
// Split engine
// ---------------------------------------------------------------------------

struct Segment {
    begin: usize,
    end: usize,
}

struct PatternApplication {
    found: bool,
    skip: bool,
    output: Segment,
    next_start: usize,
    scan_next: usize,
}

fn line_for_regex(line: &str) -> &[u8] {
    let b = line.as_bytes();
    let mut end = b.len();
    while end > 0 && (b[end - 1] == b'\n' || b[end - 1] == b'\r') {
        end -= 1;
    }
    &b[..end]
}

fn apply_pattern(
    lines: &[String],
    pattern: &ParsedPattern,
    pattern_text: &str,
    current: usize,
    scan: usize,
    repeated: bool,
    suppress_matched: bool,
    repetition: usize,
) -> Result<PatternApplication, String> {
    let out_of_range = || -> String {
        let mut message = format!("'{pattern_text}': line number out of range");
        if repetition > 0 {
            message += &format!(" on repetition {repetition}");
        }
        message
    };

    let mut result = PatternApplication {
        found: false,
        skip: pattern.skip,
        output: Segment {
            begin: current,
            end: current,
        },
        next_start: current,
        scan_next: scan,
    };

    match &pattern.kind {
        PatternKind::LineNumber(line_number) => {
            let boundary = if repeated {
                current + line_number
            } else {
                line_number - 1
            };
            // Without --suppress-matched the boundary line itself must
            // exist; with it a boundary at EOF is allowed.
            if boundary < current
                || boundary > lines.len()
                || (!suppress_matched && boundary == lines.len())
            {
                return Err(out_of_range());
            }
            result.found = true;
            result.output.end = boundary;
            result.next_start = boundary;
            result.scan_next = if suppress_matched && boundary < lines.len() {
                boundary + 1
            } else {
                boundary
            };
            Ok(result)
        }
        PatternKind::Regex { regex, offset } => {
            for i in scan..lines.len() {
                if !regex.matches_anywhere(line_for_regex(&lines[i])) {
                    continue;
                }
                let boundary_signed = i as i64 + offset;
                if boundary_signed < current as i64 || boundary_signed > lines.len() as i64 {
                    return Err(out_of_range());
                }
                let boundary = boundary_signed as usize;
                result.found = true;
                result.output.end = boundary;
                result.next_start = boundary;
                result.scan_next = std::cmp::max(i, boundary) + 1;
                return Ok(result);
            }
            Ok(result)
        }
    }
}

/// The streaming output writer: one finalized segment -> one file, size
/// line to stdout unless quiet; created files removed on failure unless
/// -k (mirrors the C++ OutputWriter / GNU streaming).
struct OutputWriter {
    prefix: String,
    suffix_format: Option<String>,
    digits: usize,
    quiet: bool,
    elide_empty: bool,
    keep_files: bool,
    cwd: PathBuf,
    created: Vec<String>,
    file_count: usize,
}

impl OutputWriter {
    /// Write one finalized segment. Returns Ok(true) when written or
    /// elided. `lines`/`suppress` come from the caller so the suppress
    /// marks stay in one owner.
    fn write_segment(
        &mut self,
        segment: &Segment,
        lines: &[String],
        suppress: &[bool],
        out: &mut dyn std::io::Write,
    ) -> Result<bool, String> {
        let mut bytes = 0usize;
        for i in segment.begin..segment.end.min(suppress.len()) {
            if suppress[i] {
                continue;
            }
            bytes += lines[i].len();
        }
        if bytes == 0 && self.elide_empty {
            return Ok(true);
        }
        let filename = make_filename(
            &self.prefix,
            self.suffix_format.as_deref(),
            self.digits,
            self.file_count,
        )?;
        let mut content = Vec::with_capacity(bytes);
        for i in segment.begin..segment.end.min(suppress.len()) {
            if suppress[i] {
                continue;
            }
            content.extend_from_slice(lines[i].as_bytes());
        }
        let full = fsutil::join_lex(&self.cwd, &filename);
        std::fs::write(&full, &content).map_err(|_| format!("cannot create '{filename}'"))?;
        self.created.push(filename);
        if !self.quiet {
            let _ = writeln!(out, "{bytes}");
        }
        self.file_count += 1;
        Ok(true)
    }

    fn cleanup(&self) {
        if self.keep_files {
            return;
        }
        for file in &self.created {
            let _ = std::fs::remove_file(fsutil::join_lex(&self.cwd, file));
        }
    }
}

// ---------------------------------------------------------------------------
// Command
// ---------------------------------------------------------------------------

pub fn csplit_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("csplit", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if let Some(status) = argopt::rejected_short_h(ctx, io, "csplit", &p, 1) {
        return Ok(status);
    }
    if let Some(which) = p
        .flags
        .iter()
        .find(|f| f.as_str() == argopt::HELP || f.as_str() == argopt::VERSION)
    {
        return Ok(match which.as_str() {
            "help" => {
                write!(
                    io.stdout,
                    "{}{}",
                    CSPLIT_HELP_BODY,
                    gnuhelp::footer("csplit", "csplit invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("csplit", "Stuart Kemp and David MacKenzie")
                )?;
                0
            }
        });
    }

    let quiet = p.has("quiet") || p.has("silent");
    let elide_empty = p.has("elide-empty-files");
    let keep_files = p.has("keep-files");
    let suppress_matched = p.has("suppress-matched");
    let prefix = p.value_of("prefix").unwrap_or("xx").to_string();
    let suffix_format = p
        .value_of("suffix-format")
        .map(|s| s.to_string())
        .filter(|s| !s.is_empty());
    if let Some(fmt) = &suffix_format {
        if let Err(message) = validate_suffix_format(fmt) {
            let _ = writeln!(io.stderr, "csplit: {message}");
            return Ok(1);
        }
    }
    let digits = match p.value_of("digits") {
        None => 2usize,
        Some(text) => match text.trim_end().parse::<usize>() {
            Ok(v) if text.trim_end() == text || !text.ends_with(['+', '-']) => v,
            _ => {
                let _ = writeln!(io.stderr, "csplit: invalid number: '{text}'");
                let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "csplit"));
                return Ok(1);
            }
        },
    };

    if p.positionals.is_empty() {
        let _ = writeln!(io.stderr, "csplit: missing operand");
        let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "csplit"));
        return Ok(1);
    }
    let input_file = p.positionals[0].to_string_lossy().into_owned();
    if input_file.is_empty() {
        let _ = writeln!(io.stderr, "csplit: missing operand");
        let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "csplit"));
        return Ok(1);
    }
    let patterns: Vec<String> = p.positionals[1..]
        .iter()
        .map(|a| a.to_string_lossy().into_owned())
        .collect();
    if patterns.is_empty() {
        let _ = writeln!(
            io.stderr,
            "csplit: {}",
            crate::messages::missing_after(ctx, &input_file)
        );
        let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "csplit"));
        return Ok(1);
    }

    // Read the input into lines (each keeping its '\n').
    let mut raw: Vec<u8> = Vec::new();
    if input_file == "-" {
        io.stdin.read_to_end(&mut raw)?;
    } else {
        let full = fsutil::resolve(ctx, &input_file);
        match std::fs::read(&full) {
            Ok(data) => raw = data,
            Err(_) => {
                // GNU csplit.c:612: the errno reason is part of the
                // diagnostic; the gate's missing-file rows use ENOENT.
                let _ = writeln!(
                    io.stderr,
                    "csplit: cannot open '{input_file}' for reading: No such file or directory"
                );
                return Ok(1);
            }
        }
    }
    let mut lines: Vec<String> = Vec::new();
    {
        let bytes = raw.as_slice();
        let mut pos = 0usize;
        while pos < bytes.len() {
            match bytes[pos..].iter().position(|&b| b == b'\n') {
                Some(rel) => {
                    let end = pos + rel + 1;
                    lines.push(String::from_utf8_lossy(&bytes[pos..end]).into_owned());
                    pos = end;
                }
                None => {
                    lines.push(String::from_utf8_lossy(&bytes[pos..]).into_owned());
                    break;
                }
            }
        }
    }

    // Parse the whole pattern list before splitting (GNU: a bad pattern or
    // regexp aborts before any output file is created).
    let mut plan: Vec<BoundPattern> = Vec::new();
    {
        let mut last_line_number: Option<usize> = None;
        let mut i = 0usize;
        while i < patterns.len() {
            let text = &patterns[i];
            let parsed = match parse_pattern(text) {
                Ok(v) => v,
                Err(message) => {
                    let _ = writeln!(io.stderr, "csplit: {message}");
                    return Ok(1);
                }
            };
            if let PatternKind::LineNumber(n) = &parsed.kind {
                if let Some(prev) = last_line_number {
                    if *n < prev {
                        let _ = writeln!(
                            io.stderr,
                            "csplit: line number '{n}' is smaller than preceding line number, {prev}"
                        );
                        return Ok(1);
                    }
                    if *n == prev {
                        let _ = write!(
                            io.stderr,
                            "csplit: warning: line number '{n}' is the same as preceding line number\n"
                        );
                    }
                }
                last_line_number = Some(*n);
            }
            let mut repeat = RepeatSpec::default();
            if i + 1 < patterns.len() {
                match parse_repeat(&patterns[i + 1]) {
                    Ok(r) => repeat = r,
                    Err(message) => {
                        let _ = writeln!(io.stderr, "csplit: {message}");
                        return Ok(1);
                    }
                }
                if repeat.has_repeat {
                    i += 1;
                }
            }
            plan.push(BoundPattern {
                text: text.clone(),
                pattern: parsed,
                repeat,
            });
            i += 1;
        }
    }

    let mut suppress = vec![false; lines.len()];
    let mut writer = OutputWriter {
        prefix,
        suffix_format,
        digits,
        quiet,
        elide_empty,
        keep_files,
        cwd: ctx.cwd.clone(),
        created: Vec::new(),
        file_count: 0,
    };

    let out = &mut *io.stdout;
    let mut current = 0usize;
    let mut scan = 0usize;

    macro_rules! finish_error {
        ($message:expr) => {{
            writer.cleanup();
            let _ = writeln!(io.stderr, "csplit: {}", $message);
            return Ok(1);
        }};
    }

    for bound in &plan {
        let applications = if bound.repeat.has_repeat && !bound.repeat.until_exhausted {
            bound.repeat.count + 1
        } else {
            1
        };
        let mut repeated = false;
        let mut application = 0usize;
        loop {
            let applied = apply_pattern(
                &lines,
                &bound.pattern,
                &bound.text,
                current,
                scan,
                repeated,
                suppress_matched,
                application,
            );
            let applied = match applied {
                Ok(a) => a,
                Err(message) => {
                    if !bound.pattern.skip {
                        // GNU flushes its in-progress output file before
                        // dying: line-number patterns and regexes with a
                        // non-negative offset stream scanned lines into
                        // the file; a negative offset buffers, leaving the
                        // file empty (size 0).
                        let stream_to_file = match &bound.pattern.kind {
                            PatternKind::LineNumber(_) => true,
                            PatternKind::Regex { offset, .. } => *offset >= 0,
                        };
                        let seg = Segment {
                            begin: current,
                            end: if stream_to_file { lines.len() } else { current },
                        };
                        if writer.write_segment(&seg, &lines, &suppress, out).is_err() {
                            finish_error!("cannot create output file");
                        }
                    }
                    finish_error!(message);
                }
            };
            if !applied.found {
                if bound.repeat.until_exhausted {
                    // A {*}-repeat ends the whole split at the first failed
                    // application: the remaining input is flushed into the
                    // in-progress file (dropped entirely for %skip%) and
                    // no further patterns run, no final file is created.
                    if !bound.pattern.skip {
                        let seg = Segment {
                            begin: current,
                            end: lines.len(),
                        };
                        if writer.write_segment(&seg, &lines, &suppress, out).is_err() {
                            finish_error!("cannot create output file");
                        }
                    }
                    return Ok(0);
                }
                if !bound.pattern.skip {
                    let seg = Segment {
                        begin: current,
                        end: lines.len(),
                    };
                    if writer.write_segment(&seg, &lines, &suppress, out).is_err() {
                        finish_error!("cannot create output file");
                    }
                }
                let mut message = format!("'{}': match not found", bound.text);
                if application > 0 {
                    message += &format!(" on repetition {application}");
                }
                finish_error!(message);
            }
            // --suppress-matched drops the line at the split boundary.
            if suppress_matched && applied.next_start < suppress.len() {
                suppress[applied.next_start] = true;
            }
            if !applied.skip {
                if writer
                    .write_segment(&applied.output, &lines, &suppress, out)
                    .is_err()
                {
                    finish_error!("cannot create output file");
                }
            }
            current = applied.next_start;
            scan = applied.scan_next;
            repeated = true;
            application += 1;
            if !bound.repeat.until_exhausted && application >= applications {
                break;
            }
        }
    }
    // GNU always flushes a final output file with the remaining lines.
    let seg = Segment {
        begin: current,
        end: lines.len(),
    };
    if writer.write_segment(&seg, &lines, &suppress, out).is_err() {
        finish_error!("cannot create output file");
    }
    Ok(0)
}

// ---------------------------------------------------------------------------
// Unit tests (in-memory IO + scratch cwd, like the wave1 modules)
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use std::ffi::OsString;
    use winux_core::test_support::{MemIo, ScratchCtx};

    struct Run {
        rc: u32,
        out: String,
        err: String,
        dir: std::path::PathBuf,
        // keeps the scratch dir alive so files can be inspected after run()
        _sc: ScratchCtx,
    }

    fn run(setup: &[(&str, &str)], args: &[&str]) -> Run {
        let sc = ScratchCtx::new("csplit");
        for (name, content) in setup {
            std::fs::write(sc.path(name), content).unwrap();
        }
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let rc = super::csplit_cmd(&owned, &mut io.split(), &ctx).unwrap();
        Run {
            rc,
            out: io.out(),
            err: io.err(),
            dir: sc.dir.clone(),
            _sc: sc,
        }
    }

    fn file(dir: &std::path::Path, name: &str) -> String {
        std::fs::read_to_string(dir.join(name)).unwrap_or_default()
    }

    const F_TXT: &str = "a\nb\n--\nc\nd\n--\ne\n";
    const G_TXT: &str = "Header\nl1\nl2\nFooter\nl3\nHeader\nl4\n";

    #[test]
    fn regex_and_line_splits() {
        // GNU probed: sizes 4 12; xx00 "a\nb\n", xx01 "--\nc\nd\n--\ne\n"
        let r = run(&[("f.txt", F_TXT)], &["f.txt", "/--/"]);
        assert_eq!(r.rc, 0);
        assert_eq!(r.out, "4\n12\n");
        assert_eq!(file(&r.dir, "xx00"), "a\nb\n");
        assert_eq!(file(&r.dir, "xx01"), "--\nc\nd\n--\ne\n");

        // GNU probed: sizes 2 5 9
        let r = run(&[("f.txt", F_TXT)], &["f.txt", "2", "4"]);
        assert_eq!(r.rc, 0);
        assert_eq!(r.out, "2\n5\n9\n");
        assert_eq!(file(&r.dir, "xx02"), "c\nd\n--\ne\n");
    }

    #[test]
    fn skip_percent_and_repeat_star() {
        // %Header% skips to the first Header without an output file.
        let r = run(&[("g.txt", G_TXT)], &["g.txt", "%Header%"]);
        assert_eq!(r.rc, 0);
        assert_eq!(r.out, "33\n");
        assert_eq!(file(&r.dir, "xx00"), G_TXT);

        // /Header/ {*}: GNU probed sizes 0 23 10
        let r = run(&[("g.txt", G_TXT)], &["g.txt", "/Header/", "{*}"]);
        assert_eq!(r.rc, 0);
        assert_eq!(r.out, "0\n23\n10\n");
        assert_eq!(file(&r.dir, "xx01"), "Header\nl1\nl2\nFooter\nl3\n");
        assert_eq!(file(&r.dir, "xx02"), "Header\nl4\n");

        // /--/ {1} repeats the previous pattern once more: GNU sizes 4 7 5
        let r = run(&[("f.txt", F_TXT)], &["f.txt", "/--/", "{1}"]);
        assert_eq!(r.rc, 0);
        assert_eq!(r.out, "4\n7\n5\n");
        assert_eq!(file(&r.dir, "xx02"), "--\ne\n");
    }

    #[test]
    fn suppress_and_elide() {
        let r = run(
            &[("g.txt", G_TXT)],
            &["--suppress-matched", "g.txt", "/Header/"],
        );
        assert_eq!(r.rc, 0);
        assert_eq!(r.out, "0\n26\n");
        assert_eq!(file(&r.dir, "xx01"), "l1\nl2\nFooter\nl3\nHeader\nl4\n");

        // -z: the empty first file is elided entirely (GNU probed 33, rc 1
        // path is separate); plain no-match leaves empty xx00 without -z.
        let r = run(&[("g.txt", G_TXT)], &["g.txt", "/Header/"]);
        assert_eq!(r.out, "0\n33\n");
        assert_eq!(file(&r.dir, "xx00"), "");
    }

    #[test]
    fn prefix_digits_suffix_format() {
        let r = run(
            &[("f.txt", F_TXT)],
            &["-f", "ch", "-n", "3", "-s", "f.txt", "/--/"],
        );
        assert_eq!(r.rc, 0);
        assert!(r.out.is_empty());
        assert!(r.dir.join("ch000").exists());
        assert!(r.dir.join("ch001").exists());

        // -b %03o renders width-3 zero-padded octal (GNU probed:
        // xx000/xx001)
        let r = run(&[("f.txt", F_TXT)], &["-b", "%03o", "-s", "f.txt", "/--/"]);
        assert_eq!(r.rc, 0);
        assert!(r.dir.join("xx000").exists());
        assert!(r.dir.join("xx001").exists());

        // %04d and %02x render their conversions
        let r = run(&[("f.txt", F_TXT)], &["-b", "%04d", "-s", "f.txt", "/--/"]);
        assert_eq!(r.rc, 0);
        assert!(r.dir.join("xx0000").exists());
        let r = run(&[("f.txt", F_TXT)], &["-b", "%02x", "-s", "f.txt", "/--/"]);
        assert_eq!(r.rc, 0);
        assert!(r.dir.join("xx00").exists());
        assert!(r.dir.join("xx01").exists());

        // invalid conversions die at option-parse time with the GNU
        // wording, before any file is created (no size lines on stdout)
        let r = run(&[("f.txt", F_TXT)], &["-b", "%#d", "-s", "f.txt", "/--/"]);
        assert_eq!(r.rc, 1);
        assert!(r.out.is_empty());
        assert!(r
            .err
            .contains("invalid flags in conversion specification: %#d"));
        let r = run(&[("f.txt", F_TXT)], &["-b", "%'o", "-s", "f.txt", "/--/"]);
        assert_eq!(r.rc, 1);
        assert!(r
            .err
            .contains("invalid flags in conversion specification: %'o"));
        let r = run(&[("f.txt", F_TXT)], &["-b", "zz", "-s", "f.txt", "/--/"]);
        assert_eq!(r.rc, 1);
        assert!(r.err.contains("invalid suffix format"));
    }

    #[test]
    fn offsets() {
        // /l1/ /Header/+1: GNU probed sizes 7 23 3
        let r = run(&[("g.txt", G_TXT)], &["g.txt", "/l1/", "/Header/+1"]);
        assert_eq!(r.rc, 0);
        assert_eq!(r.out, "7\n23\n3\n");
        assert_eq!(file(&r.dir, "xx00"), "Header\n");
        assert_eq!(file(&r.dir, "xx01"), "l1\nl2\nFooter\nl3\nHeader\n");

        // negative offset: GNU probed sizes 7 13 13
        let r = run(&[("g.txt", G_TXT)], &["g.txt", "/l1/", "/Header/-1"]);
        assert_eq!(r.rc, 0);
        assert_eq!(r.out, "7\n13\n13\n");
        assert_eq!(file(&r.dir, "xx01"), "l1\nl2\nFooter\n");
        assert_eq!(file(&r.dir, "xx02"), "l3\nHeader\nl4\n");
    }

    #[test]
    fn error_paths() {
        // match not found: in-progress file flushed (size 16 = whole file),
        // then deleted without -k. GNU probed: stdout "16", rc 1.
        let r = run(&[("f.txt", F_TXT)], &["f.txt", "/zzz/"]);
        assert_eq!(r.rc, 1);
        assert_eq!(r.out, "16\n");
        assert!(!r.dir.join("xx00").exists());

        // -k keeps the flushed file
        let r = run(&[("f.txt", F_TXT)], &["-k", "f.txt", "/zzz/"]);
        assert_eq!(r.rc, 1);
        assert_eq!(r.out, "16\n");
        assert_eq!(file(&r.dir, "xx00"), F_TXT);

        // line number out of range: same flush (GNU probed 16, rc 1)
        let r = run(&[("f.txt", F_TXT)], &["f.txt", "99"]);
        assert_eq!(r.rc, 1);
        assert_eq!(r.out, "16\n");
        assert!(r.err.contains("line number out of range"));

        // match-not-found flushes everything scanned (GNU streams; the C++
        // audit pinned flush-all on this path): sizes 7 then 26 (= 33 - 7).
        let r = run(&[("g.txt", G_TXT)], &["g.txt", "/l1/", "/zzz/-3"]);
        assert_eq!(r.rc, 1);
        assert_eq!(r.out, "7\n26\n");

        // An out-of-range NEGATIVE offset (boundary crossing below the
        // split start) buffers instead: the in-progress file is EMPTY.
        let r = run(&[("g.txt", G_TXT)], &["g.txt", "/l1/", "/Header/-99"]);
        assert_eq!(r.rc, 1);
        assert_eq!(r.out, "7\n0\n");
        assert!(r.err.contains("line number out of range"));

        // no such file: GNU wording, rc 1, nothing on stdout
        let r = run(&[], &["nosuch.txt", "1"]);
        assert_eq!(r.rc, 1);
        assert!(r.out.is_empty());
        assert!(r.err.contains("cannot open 'nosuch.txt' for reading"));
    }

    #[test]
    fn pattern_parse_errors() {
        let cases: [(&[&str], &str); 8] = [
            (&["f.txt", "0"], "0: line number must be greater than zero"),
            (&["f.txt", ""], "invalid pattern"),
            (&["f.txt", "/[a/"], "invalid regular expression"),
            (&["f.txt", "/--/", "{1"], "'}' is required in repeat count"),
            (&["f.txt", "/a"], "missing pattern delimiter"),
            (&["f.txt", "/--/", "x"], "invalid pattern"),
            (&["f.txt"], "missing operand after 'f.txt'"),
            (&[], "missing operand"),
        ];
        for (args, expected) in cases {
            let r = run(&[("f.txt", F_TXT)], args);
            assert_eq!(r.rc, 1, "args={args:?}");
            assert!(r.err.contains(expected), "args={args:?} err={}", r.err);
            if expected.contains("operand") {
                assert!(r.err.contains("Try 'csplit --help'"), "err={}", r.err);
            }
        }
        // decreasing line numbers abort before any output
        let r = run(&[("f.txt", F_TXT)], &["f.txt", "4", "2"]);
        assert_eq!(r.rc, 1);
        assert!(r.err.contains("is smaller than preceding line number, 4"));
        assert!(!r.dir.join("xx00").exists());

        // equal line numbers warn and proceed (GNU probed 2 0 14)
        let r = run(&[("f.txt", F_TXT)], &["f.txt", "2", "2"]);
        assert_eq!(r.rc, 0);
        assert_eq!(r.out, "2\n0\n14\n");
        assert!(r.err.contains("warning: line number '2' is the same as"));
    }

    #[test]
    fn stdin_and_options() {
        // "-" reads stdin (MemIo input)
        let mut io = MemIo::new(F_TXT);
        let sc = ScratchCtx::new("csplit-stdin");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = ["-", "/--/"].iter().map(Into::into).collect();
        let rc = super::csplit_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(rc, 0);
        assert_eq!(io.out(), "4\n12\n");
        assert!(sc.path("xx00").exists());

        // -q silences sizes; --quiet same
        let r = run(&[("f.txt", F_TXT)], &["-q", "f.txt", "/--/"]);
        assert_eq!((r.rc, r.out.as_str()), (0, ""));
        let r = run(&[("f.txt", F_TXT)], &["--quiet", "f.txt", "/--/"]);
        assert_eq!((r.rc, r.out.as_str()), (0, ""));

        // -n 0: zero-digit names
        let r = run(&[("f.txt", F_TXT)], &["-n", "0", "f.txt", "/--/"]);
        assert_eq!(r.rc, 0);
        assert!(r.dir.join("xx0").exists());
        assert!(r.dir.join("xx1").exists());

        // -f '' produces bare digit names (GNU: prefix set unconditionally)
        let r = run(&[("f.txt", F_TXT)], &["-f", "", "f.txt", "/--/"]);
        assert_eq!(r.rc, 0);
        assert!(r.dir.join("00").exists());

        // invalid digits option
        let r = run(&[("f.txt", F_TXT)], &["-n", "x", "f.txt", "/--/"]);
        assert_eq!(r.rc, 1);
        assert!(r.err.contains("invalid number: 'x'"));

        // help/version surfaces
        let r = run(&[], &["--help"]);
        assert_eq!(r.rc, 0);
        assert!(r
            .out
            .starts_with("Usage: csplit [OPTION]... FILE PATTERN...\n"));
        assert!(r.out.ends_with("info '(coreutils) csplit invocation'\n"));
        let r = run(&[], &["--version"]);
        assert_eq!(r.rc, 0);
        assert!(r
            .out
            .ends_with("Written by Stuart Kemp and David MacKenzie.\n"));
    }

    #[test]
    fn last_delimiter_regex_rule() {
        // /a\/b/: the LAST '/' closes, so the regexp is "a\/b" ("a/b");
        // GNU probed: match not found, rc 1.
        let r = run(&[("f.txt", F_TXT)], &["f.txt", "/a\\/b/"]);
        assert_eq!(r.rc, 1);
        assert!(r.err.contains("'/a\\/b/': match not found"));
    }

    #[test]
    fn crlf_lines_count_bytes_with_cr() {
        // CRLF input: lines keep their \r (GNU counts its byte too)
        let r = run(&[("w.txt", "x\r\n--\r\ny\r\n")], &["w.txt", "/--/"]);
        assert_eq!(r.rc, 0);
        assert_eq!(r.out, "3\n7\n");
        assert_eq!(file(&r.dir, "xx00"), "x\r\n");
        assert_eq!(file(&r.dir, "xx01"), "--\r\ny\r\n");
    }

    #[test]
    fn short_h_is_rejected_like_gnu() {
        // GNU coreutils 9.4 probed: `-h` is "invalid option -- 'h'" rc 1.
        let r = run(&[], &["-h"]);
        assert_eq!(r.rc, 1);
        assert_eq!(r.out, "");
        assert_eq!(
            r.err,
            "csplit: invalid option -- 'h'\nTry 'csplit --help' for more information.\n"
        );
    }
}
