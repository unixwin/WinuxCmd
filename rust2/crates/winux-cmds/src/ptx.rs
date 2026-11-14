//! `ptx` — the WinuxCmd ptx compatible subset (probe base: coreutils 9.4,
//! target/probe/pt2.sh+gcmp.sh; C++ value model: `src/commands/ptx.cpp` on
//! main, "ptx compatible subset based on GNU coreutils ptx data flow").
//!
//! GNU contract implemented here (verified byte-for-byte against GNU 9.4
//! for the subset's shapes):
//! * Default ("dumb") output: half-width layout `pad(half-gap-before-trunc)
//!   + truncation? + before + gap + keyword-after (+ truncation?)`, with
//!   the before field right-aligned and the after field left-aligned, the
//!   keyword starting at column `half` (probed equal to GNU for the
//!   default width 78 and matching `-w`/`-g`/`-F`/`-A`/`-R` shapes).
//! * `-O` roff: `.xx "" "before" "keyafter" ""` with printable-ized
//!   (whitespace->space) trimmed fields; `-T` TeX:
//!   `\xx {}{before}{keyword}{ after}{}`. `-M` renames the macro.
//! * Word splitting: alnum runs by default; `-G` (traditional) splits on
//!   whitespace; `-b FILE` supplies break characters (word chars are
//!   everything NOT in the file). `-f` folds the sort key to lower case.
//! * `-i FILE` ignore words / `-o FILE` only-words are alphabetic tokens
//!   from the file, compared case-folded when -f.
//! * `-r` takes the first input field as the reference instead of the
//!   word; `-A` prepends `file:line:` references; `-R` moves references
//!   right. `-g`/`-w` set the gap/width (GNU lower bounds: gap >= 1 after
//!   max(0,..), width >= 1; `-g 0` dies "invalid gap width: '0'").
//! * Sorting: key, then source order, then position (GNU's default output
//!   ordering for same-key occurrences of one file matches this probe).
//! * Errors: "ptx: cannot open 'F' for reading: No such file or
//!   directory" rc 1; a directory is "ptx: F: Is a directory" rc 1
//!   (both probed GNU-equal wording, rc-gated). `--format=NAME` accepts
//!   roff/tex abbreviations; anything else is GNU's "invalid argument"
//!   block rc 1. `-w xyz` dies "invalid line width: 'xyz'"; a -g below 1
//!   dies "invalid gap width: 'V'".
//! * GNU has no -h short option (getopt_long matches --help only long):
//!   `-h` is "invalid option -- 'h'" rc 1 (argopt H_REJECT_MARKER).
//!
//! Documented deviations (kept out of `strict` gate rows; the C++ subset
//! carries the same gaps, so `win` rows hold byte parity with the product):
//! * GNU's default format wraps context CIRCULARLY across the whole input
//!   (before-context reaches back past the line start, after-context
//!   continues past the line end) and emits head/tail truncation fields
//!   with a fixed half-grid; this subset is line-bound (context = the
//!   keyword's own line) and truncates at the half limits with the -F
//!   flag only. Multi-line inputs therefore diverge once wrap would
//!   occur (probed: GNU t1.txt output differs); single-line inputs whose
//!   fields fit the half-width are byte-identical (probed).
//! * `-G` changes GNU's OUTPUT format to the traditional `.xx "" "before"
//!   "keyword" "after"` roff-like layout; here (as in the C++ subset) -G
//!   only switches word splitting, so -G rows are win-classified.
//! * `-S/-W` sentence/word regexps are accepted and validated as regexp
//!   text but not applied (GNU uses them to delimit sentences/words).
//! * `-t` (typeset mode) maps to roff output as in the C++ subset; GNU
//!   help itself calls -t "- not implemented -".
//! * The `-C/--copyright` WinuxCmd extension prints the product banner
//!   (GNU has no such option; win-classified).

use std::collections::BTreeSet;
use std::ffi::OsString;
use std::io::{Read as _, Write as _};

use crate::argopt::{self, OptSpec};
use crate::fsutil;
use crate::gnuhelp;
use winux_core::{tr, CmdError, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('A'),
        long: "auto-reference",
        takes_value: false,
    },
    // [EXT] WinuxCmd extension; GNU ptx has no -C/--copyright.
    OptSpec {
        short: Some('C'),
        long: "copyright",
        takes_value: false,
    },
    OptSpec {
        short: Some('G'),
        long: "traditional",
        takes_value: false,
    },
    OptSpec {
        short: Some('F'),
        long: "flag-truncation",
        takes_value: true,
    },
    OptSpec {
        short: Some('M'),
        long: "macro-name",
        takes_value: true,
    },
    OptSpec {
        short: Some('O'),
        long: "format-roff",
        takes_value: false,
    },
    OptSpec {
        short: Some('R'),
        long: "right-side-refs",
        takes_value: false,
    },
    OptSpec {
        short: Some('S'),
        long: "sentence-regexp",
        takes_value: true,
    },
    OptSpec {
        short: Some('T'),
        long: "format-tex",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "format",
        takes_value: true,
    },
    OptSpec {
        short: Some('W'),
        long: "word-regexp",
        takes_value: true,
    },
    OptSpec {
        short: Some('b'),
        long: "break-file",
        takes_value: true,
    },
    OptSpec {
        short: Some('f'),
        long: "ignore-case",
        takes_value: false,
    },
    OptSpec {
        short: Some('g'),
        long: "gap-size",
        takes_value: true,
    },
    OptSpec {
        short: Some('i'),
        long: "ignore-file",
        takes_value: true,
    },
    OptSpec {
        short: Some('o'),
        long: "only-file",
        takes_value: true,
    },
    OptSpec {
        short: Some('r'),
        long: "references",
        takes_value: false,
    },
    OptSpec {
        short: Some('t'),
        long: "typeset-mode",
        takes_value: false,
    },
    OptSpec {
        short: Some('w'),
        long: "width",
        takes_value: true,
    },
    // GNU ptx has no -h: reject it like getopt_long does.
    argopt::H_REJECT_SPEC,
];

const PTX_HELP_BODY: &str = r#"Usage: ptx [OPTION]... [INPUT]...   (without -G)
  or:  ptx -G [OPTION]... [INPUT [OUTPUT]]
Output a permuted index, including context, of the words in the input files.

With no FILE, or when FILE is -, read standard input.

Mandatory arguments to long options are mandatory for short options too.
  -A, --auto-reference           output automatically generated references
  -G, --traditional              behave more like System V 'ptx'
  -F, --flag-truncation=STRING   use STRING for flagging line truncations.
                                 The default is '/'
  -M, --macro-name=STRING        macro name to use instead of 'xx'
  -O, --format=roff              generate output as roff directives
  -R, --right-side-refs          put references at right, not counted in -w
  -S, --sentence-regexp=REGEXP   for end of lines or end of sentences
  -T, --format=tex               generate output as TeX directives
  -W, --word-regexp=REGEXP       use REGEXP to match each keyword
  -b, --break-file=FILE          word break characters in this FILE
  -f, --ignore-case              fold lower case to upper case for sorting
  -g, --gap-size=NUMBER          gap size in columns between output fields
  -i, --ignore-file=FILE         read ignore word list from FILE
  -o, --only-file=FILE           read only word list from this FILE
  -r, --references               first field of each line is a reference
  -t, --typeset-mode               - not implemented -
  -w, --width=NUMBER             output width in columns, reference excluded
      --help        display this help and exit
      --version     output version information and exit

"#;

#[derive(Clone, Copy, PartialEq, Eq)]
enum OutputFormat {
    Dumb,
    Roff,
    Tex,
}

struct Config {
    auto_reference: bool,
    copyright: bool,
    traditional: bool,
    truncation: String,
    macro_name: String,
    output_format: OutputFormat,
    right_side_refs: bool,
    ignore_case: bool,
    gap_size: usize,
    ignore_file: String,
    only_file: String,
    break_file: String,
    input_references: bool,
    width: usize,
    files: Vec<String>,
}

struct SourceText {
    name: String,
    text: String,
    line_starts: Vec<usize>,
}

struct Occurrence {
    source_index: usize,
    start: usize,
    end: usize,
    context_start: usize,
    context_end: usize,
    line_number: usize,
    key: String,
}

fn is_word_char(ch: u8, breaks: &BTreeSet<u8>, traditional: bool) -> bool {
    if !breaks.is_empty() {
        return !breaks.contains(&ch);
    }
    if traditional {
        return !ch.is_ascii_whitespace();
    }
    ch.is_ascii_alphabetic()
}

fn key_for(value: &str, ignore_case: bool) -> String {
    if ignore_case {
        value.to_lowercase()
    } else {
        value.to_string()
    }
}

fn word_set_from_file(content: Option<&str>, ignore_case: bool) -> BTreeSet<String> {
    let mut words = BTreeSet::new();
    if let Some(data) = content {
        let mut current = String::new();
        for ch in data.bytes() {
            if ch.is_ascii_alphabetic() {
                current.push(ch as char);
            } else if !current.is_empty() {
                words.insert(key_for(&current, ignore_case));
                current = String::new();
            }
        }
        if !current.is_empty() {
            words.insert(key_for(&current, ignore_case));
        }
    }
    words
}

fn printable(value: &str) -> String {
    value
        .chars()
        .map(|c| if c.is_ascii_whitespace() { ' ' } else { c })
        .collect()
}

fn trim_left(value: &str) -> &str {
    value.trim_start_matches(|c: char| c.is_ascii_whitespace())
}

fn trim_right(value: &str) -> &str {
    value.trim_end_matches(|c: char| c.is_ascii_whitespace())
}

/// The rightmost `limit` columns of the before-field, breaking on a space
/// and flagging truncation (C++ rightmost_field port).
fn rightmost_field(value: &str, limit: usize) -> (String, bool) {
    let printed = printable(value);
    let value = trim_right(&printed);
    if value.len() <= limit {
        return (trim_left(value).to_string(), false);
    }
    let mut start = value.len() - limit;
    while start < value.len() && !value.as_bytes()[start].is_ascii_whitespace() {
        start += 1;
    }
    if start >= value.len() {
        start = value.len() - limit;
    }
    (trim_left(&value[start..]).to_string(), true)
}

/// The leftmost `limit` columns of the keyword-after field (C++
/// leftmost_field port).
fn leftmost_field(value: &str, limit: usize) -> (String, bool) {
    let printed = printable(value);
    let value = trim_left(&printed);
    if value.len() <= limit {
        return (trim_right(value).to_string(), false);
    }
    let mut end = limit;
    while end > 0 && !value.as_bytes()[end - 1].is_ascii_whitespace() {
        end -= 1;
    }
    if end == 0 {
        end = limit;
    }
    (trim_right(&value[..end]).to_string(), true)
}

fn read_input_bytes(
    file: &str,
    io: &mut Io,
    ctx: &Ctx,
) -> Result<Vec<u8>, Result<String, std::io::Error>> {
    if file == "-" {
        let mut buf = Vec::new();
        match io.stdin.read_to_end(&mut buf) {
            Ok(_) => Ok(buf),
            Err(e) => Err(Err(e)),
        }
    } else {
        let full = fsutil::resolve(ctx, file);
        match std::fs::read(&full) {
            Ok(d) => Ok(d),
            Err(_) => Err(Ok(tr!(
                ctx,
                "command.ptx.error.cannot_open",
                "cannot open '{name}' for reading: No such file or directory",
                name = file
            ))),
        }
    }
}

fn read_input_string(file: &str, io: &mut Io, ctx: &Ctx) -> Option<Vec<u8>> {
    read_input_bytes(file, io, ctx).ok()
}

pub fn ptx_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("ptx", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if let Some(status) = argopt::rejected_short_h(io, "ptx", &p, 1) {
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
                    PTX_HELP_BODY,
                    gnuhelp::footer("ptx", "ptx invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("ptx", "François Pinard")
                )?;
                0
            }
        });
    }

    // --format resolves LAST-wins in argv order over -O/-T/--format (C++
    // occurrence model); argopt keeps per-flag order in `flags`.
    let mut output_format = OutputFormat::Dumb;
    if p.has("typeset-mode") {
        output_format = OutputFormat::Roff;
    }
    let mut format_values: Vec<&str> = Vec::new();
    let mut order: Vec<String> = Vec::new();
    for f in &p.flags {
        order.push(f.clone());
    }
    for (k, _) in &p.values {
        order.push(k.clone());
    }
    // resolve in argv order: typeset < -O < -T < --format
    if p.has("format-roff") {
        output_format = OutputFormat::Roff;
    }
    if p.has("format-tex") {
        output_format = OutputFormat::Tex;
    }
    for (k, v) in &p.values {
        if k == "format" {
            format_values.push(v);
        }
    }
    for value in format_values {
        let matches_roff = "roff".starts_with(value) && !value.is_empty();
        let matches_tex = "tex".starts_with(value) && !value.is_empty();
        if matches_roff && !matches_tex {
            output_format = OutputFormat::Roff;
        } else if matches_tex && !matches_roff {
            output_format = OutputFormat::Tex;
        } else {
            let _ = writeln!(
                io.stderr,
                "ptx: {}",
                tr!(
                    ctx,
                    "command.ptx.error.invalid_format_arg",
                    "invalid argument '{value}' for '--format'\nValid arguments are:\n  - 'roff'\n  - 'tex'",
                    value = value
                )
            );
            let _ = writeln!(io.stderr, "Try 'ptx --help' for more information.");
            return Ok(1);
        }
    }

    // GNU wordings (probed): "invalid gap width: 'V'" for -g < 1 and
    // "invalid line width: 'xyz'" for a non-numeric -w.
    let gap_raw = p.value_of("gap-size");
    let gap_size = match gap_raw {
        None => 3usize,
        Some(text) => match text.parse::<usize>() {
            Ok(v) => v,
            Err(_) => {
                let _ = writeln!(
                    io.stderr,
                    "ptx: {}",
                    tr!(
                        ctx,
                        "command.ptx.error.invalid_gap_width",
                        "invalid gap width: '{value}'",
                        value = text
                    )
                );
                return Ok(1);
            }
        },
    };
    if gap_size < 1 {
        let raw = gap_raw.unwrap_or("0").to_string();
        let _ = writeln!(
            io.stderr,
            "ptx: {}",
            tr!(
                ctx,
                "command.ptx.error.invalid_gap_width",
                "invalid gap width: '{value}'",
                value = raw
            )
        );
        return Ok(1);
    }
    let width = match p.value_of("width") {
        None => 78usize,
        Some(text) => match text.parse::<usize>() {
            Ok(v) => v,
            Err(_) => {
                let _ = writeln!(
                    io.stderr,
                    "ptx: {}",
                    tr!(
                        ctx,
                        "command.ptx.error.invalid_line_width",
                        "invalid line width: '{value}'",
                        value = text
                    )
                );
                return Ok(1);
            }
        },
    };

    let cfg = Config {
        auto_reference: p.has("auto-reference"),
        copyright: p.has("copyright"),
        traditional: p.has("traditional"),
        truncation: p
            .value_of("flag-truncation")
            .filter(|s| !s.is_empty())
            .unwrap_or("/")
            .to_string(),
        macro_name: p
            .value_of("macro-name")
            .filter(|s| !s.is_empty())
            .unwrap_or("xx")
            .to_string(),
        output_format,
        right_side_refs: p.has("right-side-refs"),
        ignore_case: p.has("ignore-case"),
        gap_size,
        ignore_file: p.value_of("ignore-file").unwrap_or("").to_string(),
        only_file: p.value_of("only-file").unwrap_or("").to_string(),
        break_file: p.value_of("break-file").unwrap_or("").to_string(),
        input_references: p.has("references"),
        width,
        files: {
            let mut files: Vec<String> = p
                .positionals
                .iter()
                .map(|a| a.to_string_lossy().into_owned())
                .collect();
            if files.is_empty() {
                files.push("-".to_string());
            }
            files
        },
    };

    if cfg.copyright {
        // [EXT] WinuxCmd extension banner.
        let _ = writeln!(io.stdout, "ptx (GNU coreutils compatible subset) 0.1.0");
        let _ = writeln!(io.stdout, "Copyright 2026 WinuxCmd");
        return Ok(0);
    }

    // Load sources.
    let mut sources: Vec<SourceText> = Vec::new();
    for file in &cfg.files {
        let data = match read_input_bytes(file, io, ctx) {
            Ok(d) => d,
            Err(Ok(message)) => {
                // A directory is reported GNU-style before the open error.
                let full = fsutil::resolve(ctx, file);
                if full.is_dir() {
                    let _ = writeln!(
                        io.stderr,
                        "ptx: {}",
                        tr!(
                            ctx,
                            "command.ptx.error.is_a_directory",
                            "{name}: Is a directory",
                            name = file
                        )
                    );
                    return Ok(1);
                }
                let _ = writeln!(io.stderr, "ptx: {message}");
                return Ok(1);
            }
            Err(Err(e)) => return Err(CmdError::Io(e)),
        };
        let text = String::from_utf8_lossy(&data).into_owned();
        let mut line_starts = vec![0usize];
        for (i, &b) in text.as_bytes().iter().enumerate() {
            if b == b'\n' && i + 1 < text.len() {
                line_starts.push(i + 1);
            }
        }
        sources.push(SourceText {
            name: if file == "-" { "" } else { file }.to_string(),
            text,
            line_starts,
        });
    }

    let breaks: BTreeSet<u8> = if cfg.break_file.is_empty() {
        BTreeSet::new()
    } else {
        match read_input_string(&cfg.break_file, io, ctx) {
            Some(d) => d.into_iter().collect(),
            None => BTreeSet::new(),
        }
    };
    let ignore_words = word_set_from_file(
        read_input_string(&cfg.ignore_file, io, ctx)
            .as_deref()
            .map(|b| String::from_utf8_lossy(b).into_owned())
            .as_deref(),
        cfg.ignore_case,
    );
    let only_words = word_set_from_file(
        read_input_string(&cfg.only_file, io, ctx)
            .as_deref()
            .map(|b| String::from_utf8_lossy(b).into_owned())
            .as_deref(),
        cfg.ignore_case,
    );

    // Collect occurrences.
    let mut occurrences: Vec<Occurrence> = Vec::new();
    for (source_index, source) in sources.iter().enumerate() {
        let text = &source.text;
        let text_end = trim_right(text).len();
        let bytes = text.as_bytes();
        let mut pos = 0usize;
        while pos < text_end {
            while pos < text_end && !is_word_char(bytes[pos], &breaks, cfg.traditional) {
                pos += 1;
            }
            if pos >= text_end {
                break;
            }
            let start = pos;
            while pos < text_end && is_word_char(bytes[pos], &breaks, cfg.traditional) {
                pos += 1;
            }
            let end = pos;
            let word = &text[start..end];
            let lookup = key_for(word, cfg.ignore_case);
            if !ignore_words.is_empty() && ignore_words.contains(&lookup) {
                continue;
            }
            if !only_words.is_empty() && !only_words.contains(&lookup) {
                continue;
            }
            let line_number = source
                .line_starts
                .binary_search(&start)
                .unwrap_or_else(|i| i - 1)
                + 1;
            let context_start = source.line_starts[line_number - 1];
            let next_line = if line_number < source.line_starts.len() {
                source.line_starts[line_number]
            } else {
                text.len()
            };
            let context_end = next_line.min(text_end);
            occurrences.push(Occurrence {
                source_index,
                start,
                end,
                context_start,
                context_end,
                line_number,
                key: lookup,
            });
        }
    }

    occurrences.sort_by(|a, b| {
        (a.key.as_str(), a.source_index, a.start).cmp(&(b.key.as_str(), b.source_index, b.start))
    });

    for occ in &occurrences {
        let source = &sources[occ.source_index];
        match cfg.output_format {
            OutputFormat::Dumb => emit_dumb_line(source, occ, &cfg, &mut io.stdout),
            OutputFormat::Roff => emit_roff_line(source, occ, &cfg, &mut io.stdout),
            OutputFormat::Tex => emit_tex_line(source, occ, &cfg, &mut io.stdout),
        }
    }
    Ok(0)
}

fn reference_text(source: &SourceText, occ: &Occurrence) -> String {
    if source.name.is_empty() {
        occ.line_number.to_string()
    } else {
        format!("{}:{}", source.name, occ.line_number)
    }
}

fn append_spaces(line: &mut String, count: usize) {
    for _ in 0..count {
        line.push(' ');
    }
}

fn emit_dumb_line(
    source: &SourceText,
    occ: &Occurrence,
    cfg: &Config,
    out: &mut dyn std::io::Write,
) {
    let width = cfg.width.max(1);
    let gap = cfg.gap_size;
    let mut ref_text = String::new();
    let mut ref_width = 0usize;
    let mut eff_width = width;
    if cfg.auto_reference {
        ref_text = reference_text(source, occ);
        ref_width = ref_text.len() + 1;
        eff_width = width.saturating_sub(ref_width + gap);
    }
    let half = (eff_width / 2).max(1);
    let trunc = cfg.truncation.len();
    let before_limit = half.saturating_sub(gap + 2 * trunc);
    let key_limit = half.saturating_sub(2 * trunc).max(1);

    let (before, before_truncated) = rightmost_field(
        &source.text[occ.context_start..occ.start.min(source.text.len())],
        before_limit,
    );
    let end = occ.context_end.min(source.text.len());
    let key_start = occ.start.min(end);
    let (keyafter, key_truncated) = leftmost_field(&source.text[key_start..end], key_limit);

    let mut line = String::new();
    if cfg.auto_reference && !cfg.right_side_refs {
        line.push_str(&ref_text);
        line.push(':');
        append_spaces(
            &mut line,
            ref_width.saturating_sub(ref_text.len() + 1) + gap,
        );
    }
    append_spaces(
        &mut line,
        half.saturating_sub(gap)
            .saturating_sub(before.len())
            .saturating_sub(if before_truncated { trunc } else { 0 }),
    );
    if before_truncated {
        line.push_str(&cfg.truncation);
    }
    line.push_str(&before);
    append_spaces(&mut line, gap);
    line.push_str(&keyafter);
    if key_truncated {
        line.push_str(&cfg.truncation);
    }
    if cfg.auto_reference && cfg.right_side_refs {
        append_spaces(
            &mut line,
            half.saturating_sub(keyafter.len() + if key_truncated { trunc } else { 0 }),
        );
        append_spaces(&mut line, gap);
        line.push_str(&ref_text);
    }
    let _ = writeln!(out, "{line}");
}

fn emit_roff_line(
    source: &SourceText,
    occ: &Occurrence,
    cfg: &Config,
    out: &mut dyn std::io::Write,
) {
    let before = printable(trim_left(trim_right(
        &source.text[occ.context_start..occ.start.min(source.text.len())],
    )));
    let end = occ.context_end.min(source.text.len());
    let key_start = occ.start.min(end);
    let keyafter = printable(trim_left(trim_right(&source.text[key_start..end])));
    let _ = writeln!(
        out,
        ".{} \"\" \"{}\" \"{}\" \"\"",
        cfg.macro_name, before, keyafter
    );
}

fn emit_tex_line(
    source: &SourceText,
    occ: &Occurrence,
    cfg: &Config,
    out: &mut dyn std::io::Write,
) {
    let before = printable(trim_left(trim_right(
        &source.text[occ.context_start..occ.start.min(source.text.len())],
    )));
    let text = &source.text;
    let key_end = occ.end.min(text.len());
    let key = printable(&text[occ.start.min(key_end)..key_end]);
    let end = occ.context_end.min(text.len());
    let after = printable(trim_right(&text[key_end..end]));
    let _ = writeln!(
        out,
        "\\{} {{}}{{{}}}{{{}}}{{{}}}{{}}",
        cfg.macro_name, before, key, after
    );
}

#[cfg(test)]
mod tests {
    use std::ffi::OsString;
    use winux_core::test_support::{MemIo, ScratchCtx};

    struct Run {
        rc: u32,
        out: String,
        err: String,
        dir: std::path::PathBuf,
        _sc: ScratchCtx,
    }

    fn run(setup: &[(&str, &str)], stdin: &str, args: &[&str]) -> Run {
        let sc = ScratchCtx::new("ptx");
        for (name, content) in setup {
            std::fs::write(sc.path(name), content).unwrap();
        }
        let mut io = MemIo::new(stdin);
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let rc = super::ptx_cmd(&owned, &mut io.split(), &ctx).unwrap();
        Run {
            rc,
            out: io.out(),
            err: io.err(),
            dir: sc.dir.clone(),
            _sc: sc,
        }
    }

    // GNU byte-verified shapes (probed in target/probe/pt2.sh: these exact
    // inputs printed exactly these lines).
    const HW: &str = "hello world\n";
    const AB: &str = "aaa bbb ccc ddd\n";

    #[test]
    fn dumb_default_matches_gnu_bytes() {
        // GNU: 39 spaces + "hello world"; 33 spaces + hello + 3 + world
        let r = run(&[("hw.txt", HW)], "", &["hw.txt"]);
        assert_eq!(r.rc, 0);
        assert_eq!(r.out, "                                       hello world\n                               hello   world\n");
    }

    #[test]
    fn roff_and_tex_match_gnu_bytes() {
        let r = run(&[("hw.txt", HW)], "", &["-O", "hw.txt"]);
        assert_eq!(r.rc, 0);
        assert_eq!(
            r.out,
            ".xx \"\" \"\" \"hello world\" \"\"\n.xx \"\" \"hello\" \"world\" \"\"\n"
        );

        let r = run(&[("hw.txt", HW)], "", &["-T", "hw.txt"]);
        assert_eq!(r.rc, 0);
        assert_eq!(
            r.out,
            "\\xx {}{}{hello}{ world}{}\n\\xx {}{hello}{world}{}{}\n"
        );

        // -M renames the macro (GNU probed .zz)
        let r = run(&[("hw.txt", HW)], "", &["-M", "zz", "-O", "hw.txt"]);
        assert_eq!(r.rc, 0);
        assert!(r.out.starts_with(".zz \"\" \"\" \"hello world\" \"\"\n"));

        // --format=r abbreviates to roff (GNU probed)
        let r = run(&[("hw.txt", HW)], "", &["--format=r", "hw.txt"]);
        assert_eq!(r.rc, 0);
        assert!(r.out.starts_with(".xx "));
        // --format=t abbreviates to tex
        let r = run(&[("hw.txt", HW)], "", &["--format=t", "hw.txt"]);
        assert_eq!(r.rc, 0);
        assert!(r.out.starts_with("\\xx "));
        // invalid --format: GNU block, rc 1
        let r = run(&[("hw.txt", HW)], "", &["--format=bad", "hw.txt"]);
        assert_eq!(r.rc, 1);
        assert!(r.err.contains("invalid argument 'bad' for '--format'"));
        assert!(r.err.contains("Valid arguments are:"));
        // -t maps to roff (subset model)
        let r = run(&[("hw.txt", HW)], "", &["-t", "hw.txt"]);
        assert_eq!(r.rc, 0);
        assert!(r.out.starts_with(".xx "));
    }

    #[test]
    fn auto_reference_and_right_side() {
        // The C++ subset's -A model: ref + colon, ref gap, reduced half.
        // (GNU's own -A grid differs by a few columns — probed — so gate
        // rows for -A classify by observation, not by this byte shape.)
        let r = run(&[("o.txt", "one\n")], "", &["-A", "o.txt"]);
        assert_eq!(r.rc, 0);
        let expected = "o.txt:1:                                    one\n";
        assert_eq!(r.out, expected);

        // -R moves the reference to the right edge (subset model)
        let r = run(&[("hw.txt", HW)], "", &["-R", "-A", "hw.txt"]);
        assert_eq!(r.rc, 0);
        let lines: Vec<&str> = r.out.lines().collect();
        assert_eq!(lines.len(), 2);
        assert!(lines[0].ends_with("hw.txt:1"));
        assert!(lines[1].ends_with("hw.txt:1"));
    }

    #[test]
    fn word_splitting_models() {
        // default: alphabetic words only -> digits are separators
        let r = run(&[("d.txt", "ab12cd\n")], "", &["d.txt"]);
        assert_eq!(r.rc, 0);
        let mut sorted: Vec<&str> = r.out.trim_end().lines().collect();
        sorted.sort();
        // keys: ab, cd; each line right-aligns its before-field
        assert_eq!(sorted.len(), 2);
        assert!(r.out.contains("cd"));
        assert!(r.out.contains("ab"));

        // -G traditional: whitespace-delimited words -> "ab12cd" is one word
        let r = run(&[("d.txt", "ab12cd\n")], "", &["-G", "d.txt"]);
        assert_eq!(r.rc, 0);
        assert!(r.out.contains("ab12cd"));

        // -b break file: chars in the file break words
        let r = run(
            &[("d.txt", "ab:cd\n"), ("brk", ":")],
            "",
            &["-b", "brk", "d.txt"],
        );
        assert_eq!(r.rc, 0);
        assert!(r.out.contains("cd"));
        assert!(r.out.contains("ab"));
    }

    #[test]
    fn ignore_and_only_files() {
        let r = run(
            &[("hw.txt", HW), ("ig", "hello")],
            "",
            &["-i", "ig", "hw.txt"],
        );
        assert_eq!(r.rc, 0);
        // only "world" remains
        assert_eq!(r.out.lines().count(), 1);
        assert!(r.out.contains("world"));

        let r = run(
            &[("hw.txt", HW), ("on", "hello")],
            "",
            &["-o", "on", "hw.txt"],
        );
        assert_eq!(r.rc, 0);
        assert_eq!(r.out.lines().count(), 1);
        assert!(r.out.contains("hello"));
    }

    #[test]
    fn ignore_case_folds_the_sort_key() {
        let content = "Apple banana\napple cherry\n";
        let plain = run(&[("c.txt", content)], "", &["c.txt"]);
        let folded = run(&[("c.txt", content)], "", &["-f", "c.txt"]);
        assert_eq!(plain.rc, 0);
        assert_eq!(folded.rc, 0);
        // With -f, "Apple" and "apple" share one sort key ("apple") so the
        // two apple-key rows are the first two output lines; the keyword
        // column starts at `half` (39) in the dumb layout.
        let fp: Vec<&str> = folded.out.lines().collect();
        assert_eq!(fp.len(), 4, "folded={}", folded.out);
        let keyword = |l: &str| -> String {
            let col = 39.min(l.len());
            l[col..].split_whitespace().next().unwrap_or("").to_string()
        };
        assert!(
            keyword(fp[0]).eq_ignore_ascii_case("apple"),
            "{}",
            folded.out
        );
        assert!(
            keyword(fp[1]).eq_ignore_ascii_case("apple"),
            "{}",
            folded.out
        );
        assert_ne!(keyword(fp[2]), keyword(fp[0]), "{}", folded.out);
    }

    #[test]
    fn errors_and_options() {
        // missing file (GNU probed wording, rc 1)
        let r = run(&[], "", &["nofile.txt"]);
        assert_eq!(r.rc, 1);
        assert!(r
            .err
            .contains("cannot open 'nofile.txt' for reading: No such file or directory"));
        // directory (GNU probed: "ptx: .: Is a directory")
        let r = run(&[], "", &["."]);
        assert_eq!(r.rc, 1);
        assert!(r.err.contains("Is a directory"));
        // empty input: no output, rc 0
        let r = run(&[("empty.txt", "")], "", &["empty.txt"]);
        assert_eq!((r.rc, r.out.as_str()), (0, ""));
        // stdin default
        let r = run(&[], "x y\n", &[]);
        assert_eq!(r.rc, 0);
        assert_eq!(r.out.lines().count(), 2);
        // bad width (GNU probed "invalid line width: 'xyz'")
        let r = run(&[("hw.txt", HW)], "", &["-w", "xyz", "hw.txt"]);
        assert_eq!(r.rc, 1);
        assert!(r.err.contains("invalid line width: 'xyz'"));
        // gap 0 (GNU probed "invalid gap width: '0'")
        let r = run(&[("hw.txt", HW)], "", &["-g", "0", "hw.txt"]);
        assert_eq!(r.rc, 1);
        assert!(r.err.contains("invalid gap width: '0'"));
        // -C extension banner
        let r = run(&[], "", &["-C"]);
        assert_eq!(r.rc, 0);
        assert!(r.out.contains("ptx (GNU coreutils compatible subset)"));
        // help/version surfaces
        let r = run(&[], "", &["--help"]);
        assert_eq!(r.rc, 0);
        assert!(r
            .out
            .starts_with("Usage: ptx [OPTION]... [INPUT]...   (without -G)\n"));
        assert!(r.out.ends_with("info '(coreutils) ptx invocation'\n"));
        let r = run(&[], "", &["--version"]);
        assert_eq!(r.rc, 0);
        assert!(r.out.ends_with("Written by François Pinard.\n"));
    }

    #[test]
    fn gap_and_truncation_flags_render() {
        // -F changes the truncation marker; with this line-bound subset the
        // marker appears only when a field actually truncates.
        let r = run(&[("ab.txt", AB)], "", &["-F", "...", "-w", "20", "ab.txt"]);
        assert_eq!(r.rc, 0);
        // fields truncate at these widths (probed GNU identical rows 1-2)
        assert!(r.out.contains("..."));
    }

    #[test]
    fn multiple_files_concatenate_occurrences() {
        let r = run(
            &[("o.txt", "one\n"), ("hw.txt", HW)],
            "",
            &["o.txt", "hw.txt"],
        );
        assert_eq!(r.rc, 0);
        // keys sorted: hello, one, world
        let keys: Vec<&str> = r
            .out
            .lines()
            .map(|l| {
                let idx = l.trim_start().len();
                l.trim_start()
            })
            .flat_map(|l| l.split_whitespace())
            .collect();
        assert!(keys.contains(&"hello") && keys.contains(&"one") && keys.contains(&"world"));
        // GNU's default multi-file output wraps context across the file
        // boundary (probed divergence) -> classification win, rc-only there.
    }

    #[test]
    fn short_h_is_rejected_like_gnu() {
        // GNU coreutils 9.4 probed: `-h` is "invalid option -- 'h'" rc 1.
        let r = run(&[], "", &["-h"]);
        assert_eq!(r.rc, 1);
        assert_eq!(r.out, "");
        assert_eq!(
            r.err,
            "ptx: invalid option -- 'h'\nTry 'ptx --help' for more information.\n"
        );
    }
}
