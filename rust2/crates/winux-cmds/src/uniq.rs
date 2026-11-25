//! `uniq` — GNU coreutils uniq.c (9.4) semantics, ported for WinuxCmd 2.0 (#1488).
//!
//! Filters adjacent matching records from INPUT (or stdin) and writes to
//! OUTPUT (or stdout). Explicit `Io` + `Ctx`, no process-global stdio or exit().

use std::ffi::OsString;
use std::io::{Read, Write};

use crate::argopt;
use winux_core::{usage_error, CmdResult, Ctx, Io};

#[derive(Clone, Copy, PartialEq, Eq)]
enum GroupMode {
    None,
    Separate,
    Prepend,
    Append,
    Both,
}

struct Config {
    show_count: bool,
    repeated_only: bool,
    all_repeated: bool,
    unique_only: bool,
    ignore_case: bool,
    group_all: bool,
    output_unique: bool,
    output_first_repeated: bool,
    output_later_repeated: bool,
    group_mode: GroupMode,
    skip_fields: usize,
    skip_chars: usize,
    check_chars: Option<usize>,
    delimiter: u8,
    input: String,
    output: String,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            show_count: false,
            repeated_only: false,
            all_repeated: false,
            unique_only: false,
            ignore_case: false,
            group_all: false,
            output_unique: true,
            output_first_repeated: true,
            output_later_repeated: false,
            group_mode: GroupMode::None,
            skip_fields: 0,
            skip_chars: 0,
            check_chars: None,
            delimiter: b'\n',
            input: "-".to_string(),
            output: "-".to_string(),
        }
    }
}

/// GNU `argmatch` prefix matcher: exact match wins, unambiguous prefix wins,
/// ambiguous or unknown returns `None`.
fn argmatch_prefix<'a>(value: &str, names: &[&'a str]) -> Option<&'a str> {
    let mut hit: Option<&'a str> = None;
    for &name in names {
        if name == value {
            return Some(name);
        }
        if !value.is_empty() && name.starts_with(value) {
            if hit.is_some() {
                return None;
            }
            hit = Some(name);
        }
    }
    hit
}

fn parse_group_mode(method: &str) -> Result<GroupMode, String> {
    if method.is_empty() {
        return Err(
            "ambiguous argument '' for '--group'\nValid arguments are:\n  - 'prepend'\n  - 'append'\n  - 'separate'\n  - 'both'\nTry 'uniq --help' for more information."
                .to_string(),
        );
    }
    let resolved = argmatch_prefix(method, &["separate", "prepend", "append", "both"])
        .unwrap_or(method);
    match resolved {
        "separate" => Ok(GroupMode::Separate),
        "prepend" => Ok(GroupMode::Prepend),
        "append" => Ok(GroupMode::Append),
        "both" => Ok(GroupMode::Both),
        _ => Err(format!(
            "invalid argument '{method}' for '--group'\nValid arguments are:\n  - 'prepend'\n  - 'append'\n  - 'separate'\n  - 'both'\nTry 'uniq --help' for more information."
        )),
    }
}

fn parse_all_repeated_mode(method: &str) -> Result<GroupMode, String> {
    if method.is_empty() {
        return Err(
            "ambiguous argument '' for '--all-repeated'\nValid arguments are:\n  - 'none'\n  - 'prepend'\n  - 'separate'\nTry 'uniq --help' for more information."
                .to_string(),
        );
    }
    let resolved = argmatch_prefix(method, &["none", "prepend", "separate"]).unwrap_or(method);
    match resolved {
        "none" => Ok(GroupMode::None),
        "prepend" => Ok(GroupMode::Prepend),
        "separate" => Ok(GroupMode::Separate),
        _ => Err(format!(
            "invalid argument '{method}' for '--all-repeated'\nValid arguments are:\n  - 'none'\n  - 'prepend'\n  - 'separate'\nTry 'uniq --help' for more information."
        )),
    }
}

/// Parse GNU `size_opt` (unsigned with clamping on overflow; rejects negatives
/// and non-numeric strings with GNU-shaped diagnostics).
fn parse_size_opt(raw: &str, what: &str) -> Result<usize, String> {
    let trimmed = raw.trim_start();
    if trimmed.is_empty() {
        return Err(format!("{raw}: invalid number of {what}"));
    }
    if let Some(rest) = trimmed.strip_prefix('-') {
        if !rest.is_empty() && rest.bytes().all(|b| b.is_ascii_digit()) {
            return Err(format!("{raw}: invalid number of {what}"));
        }
        return Err(format!("{raw}: invalid number of {what}"));
    }
    let digits = trimmed.strip_prefix('+').unwrap_or(trimmed);
    if digits.is_empty() || !digits.bytes().all(|b| b.is_ascii_digit()) {
        return Err(format!("{raw}: invalid number of {what}"));
    }
    match digits.parse::<usize>() {
        Ok(v) => Ok(v),
        Err(_) => Ok(usize::MAX),
    }
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

fn is_field_sep(b: u8) -> bool {
    b == b' ' || b == b'\t' || b == b'\n'
}

fn skip_n_fields(line: &[u8], n: usize) -> &[u8] {
    if n == 0 {
        return line;
    }
    let mut i = 0usize;
    let mut fields = 0usize;
    while i < line.len() && fields < n {
        while i < line.len() && is_field_sep(line[i]) {
            i += 1;
        }
        if i >= line.len() {
            break;
        }
        while i < line.len() && !is_field_sep(line[i]) {
            i += 1;
        }
        fields += 1;
    }
    &line[i..]
}

fn comparison_key(line: &[u8], cfg: &Config) -> Vec<u8> {
    let after_fields = skip_n_fields(line, cfg.skip_fields);
    let start = cfg.skip_chars.min(after_fields.len());
    let mut slice = &after_fields[start..];
    if let Some(limit) = cfg.check_chars {
        slice = &slice[..limit.min(slice.len())];
    }
    if cfg.ignore_case {
        slice.iter().map(|b| b.to_ascii_lowercase()).collect()
    } else {
        slice.to_vec()
    }
}

fn should_emit(count: usize, cfg: &Config) -> bool {
    if cfg.group_all {
        return true;
    }
    if count == 1 {
        cfg.output_unique
    } else {
        cfg.output_first_repeated || cfg.output_later_repeated
    }
}

fn emit_one(
    out: &mut dyn Write,
    line: &[u8],
    count: usize,
    cfg: &Config,
    show_count_override: bool,
) -> std::io::Result<()> {
    if cfg.show_count && !show_count_override {
        write!(out, "{count:7} ")?;
    }
    out.write_all(line)?;
    out.write_all(&[cfg.delimiter])
}

fn emit_group_separator(out: &mut dyn Write, cfg: &Config) -> std::io::Result<()> {
    out.write_all(&[cfg.delimiter])
}

fn split_records<'a>(content: &'a [u8], delimiter: u8) -> Vec<&'a [u8]> {
    let mut out = Vec::new();
    let mut start = 0usize;
    for (i, &b) in content.iter().enumerate() {
        if b == delimiter {
            out.push(&content[start..i]);
            start = i + 1;
        }
    }
    if start < content.len() {
        out.push(&content[start..]);
    }
    out
}

fn run_uniq(cfg: &Config, io: &mut Io, ctx: &Ctx) -> Result<(), (u32, String)> {
    let input_bytes = if cfg.input == "-" {
        let mut buf = Vec::new();
        io.stdin
            .read_to_end(&mut buf)
            .map_err(|e| (1, format!("error reading '-': {}", crate::fsutil::reason(&e))))?;
        buf
    } else {
        crate::fsutil::read_operand_all(ctx, &cfg.input).map_err(|e| {
            let reason = crate::fsutil::gnu_open_reason(ctx, &cfg.input, &e, false);
            (1, format!("{}: {reason}", cfg.input))
        })?
    };

    let mut file_out = if cfg.output != "-" {
        Some(
            crate::fsutil::open_write_operand(ctx, &cfg.output, false).map_err(|e| {
                let reason = crate::fsutil::gnu_open_reason(ctx, &cfg.output, &e, true);
                (1, format!("{}: {reason}", cfg.output))
            })?,
        )
    } else {
        None
    };

    let out: &mut dyn Write = match file_out.as_deref_mut() {
        Some(f) => f,
        None => io.stdout,
    };

    let mut first_emitted_group = false;
    let mut group: Vec<&[u8]> = Vec::new();
    let mut key: Vec<u8> = Vec::new();
    let mut group_count = 0usize;
    let keep_all_records = cfg.group_all || cfg.output_later_repeated;

    let emit_group = |out: &mut dyn Write,
                      records: &[&[u8]],
                      count: usize,
                      first_emitted: &mut bool|
     -> std::io::Result<()> {
        if !should_emit(count, cfg) {
            return Ok(());
        }
        if cfg.group_mode == GroupMode::Prepend
            || cfg.group_mode == GroupMode::Both
            || (*first_emitted
                && (cfg.group_mode == GroupMode::Append || cfg.group_mode == GroupMode::Separate))
        {
            emit_group_separator(out, cfg)?;
        }

        if cfg.group_all {
            for &record in records {
                emit_one(out, record, count, cfg, true)?;
            }
        } else if count == 1 {
            emit_one(out, records[0], count, cfg, false)?;
        } else {
            if cfg.output_first_repeated {
                emit_one(out, records[0], count, cfg, false)?;
            }
            if cfg.output_later_repeated {
                for &record in &records[1..] {
                    emit_one(out, record, count, cfg, true)?;
                }
            }
        }

        *first_emitted = true;
        Ok(())
    };

    for record in split_records(&input_bytes, cfg.delimiter) {
        let line_key = comparison_key(record, cfg);
        if group_count > 0 && line_key != key {
            emit_group(out, &group, group_count, &mut first_emitted_group)
                .map_err(|e| (1, format!("write error: {}", crate::fsutil::reason(&e))))?;
            group.clear();
            group_count = 0;
        }
        if group_count == 0 {
            key = line_key;
        }
        if keep_all_records || group_count == 0 {
            group.push(record);
        }
        group_count += 1;
    }

    if group_count > 0 {
        emit_group(out, &group, group_count, &mut first_emitted_group)
            .map_err(|e| (1, format!("write error: {}", crate::fsutil::reason(&e))))?;
    }

    if first_emitted_group
        && (cfg.group_mode == GroupMode::Append || cfg.group_mode == GroupMode::Both)
    {
        emit_group_separator(out, cfg)
            .map_err(|e| (1, format!("write error: {}", crate::fsutil::reason(&e))))?;
    }

    out.flush()
        .map_err(|e| (1, format!("write error: {}", crate::fsutil::reason(&e))))?;
    Ok(())
}

pub fn uniq_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut cfg = Config::default();
    let mut raw_operands: Vec<String> = Vec::new();
    let mut explicit_skip_fields = false;
    let mut obsolete_digits: Vec<String> = Vec::new();
    let mut group_occ: Option<Option<String>> = None;
    let mut repeated_occ: Option<Option<String>> = None;

    let mut idx = 0usize;
    while idx < args.len() {
        let arg = args[idx].to_string_lossy().into_owned();
        if arg == "--" {
            idx += 1;
            for a in &args[idx..] {
                raw_operands.push(a.to_string_lossy().into_owned());
            }
            break;
        }
        if arg == "-" || !arg.starts_with('-') {
            raw_operands.push(arg);
            idx += 1;
            continue;
        }
        if let Some(long) = arg.strip_prefix("--") {
            let (name, inline) = match long.split_once('=') {
                Some((n, v)) => (n, Some(v.to_string())),
                None => (long, None),
            };
            match name {
                "count" => cfg.show_count = true,
                "repeated" => cfg.repeated_only = true,
                "all-repeated" => {
                    cfg.all_repeated = true;
                    repeated_occ = Some(inline);
                }
                "skip-fields" => {
                    let Some(v) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(
                            ctx,
                            io,
                            "uniq",
                            &argopt::option_requires_argument(ctx, "--skip-fields"),
                        );
                        return Ok(1);
                    };
                    match parse_size_opt(&v, "fields to skip") {
                        Ok(n) => {
                            cfg.skip_fields = n;
                            explicit_skip_fields = true;
                        }
                        Err(msg) => {
                            let _ = writeln!(io.stderr, "uniq: {msg}");
                            return Ok(1);
                        }
                    }
                }
                "ignore-case" => cfg.ignore_case = true,
                "skip-chars" => {
                    let Some(v) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(
                            ctx,
                            io,
                            "uniq",
                            &argopt::option_requires_argument(ctx, "--skip-chars"),
                        );
                        return Ok(1);
                    };
                    match parse_size_opt(&v, "bytes to skip") {
                        Ok(n) => cfg.skip_chars = n,
                        Err(msg) => {
                            let _ = writeln!(io.stderr, "uniq: {msg}");
                            return Ok(1);
                        }
                    }
                }
                "unique" => cfg.unique_only = true,
                "check-chars" => {
                    let Some(v) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(
                            ctx,
                            io,
                            "uniq",
                            &argopt::option_requires_argument(ctx, "--check-chars"),
                        );
                        return Ok(1);
                    };
                    match parse_size_opt(&v, "bytes to compare") {
                        Ok(n) => cfg.check_chars = Some(n),
                        Err(msg) => {
                            let _ = writeln!(io.stderr, "uniq: {msg}");
                            return Ok(1);
                        }
                    }
                }
                "zero-terminated" => cfg.delimiter = b'\0',
                "group" => {
                    group_occ = Some(inline);
                }
                "help" | "version" => return Ok(0),
                _ => {
                    let _ = usage_error(ctx, io, "uniq", &argopt::unrecognized_option(ctx, &arg));
                    return Ok(1);
                }
            }
            idx += 1;
            continue;
        }

        // Short option cluster; check obsolete -NUM when the whole token is -<digits>
        let rest_after_dash = &arg[1..];
        if !rest_after_dash.is_empty() && rest_after_dash.bytes().all(|b| b.is_ascii_digit()) {
            obsolete_digits.push(rest_after_dash.to_string());
            idx += 1;
            continue;
        }

        let chars: Vec<char> = rest_after_dash.chars().collect();
        let mut ci = 0usize;
        let mut consumed_next = false;
        while ci < chars.len() {
            let c = chars[ci];
            match c {
                'c' => {
                    cfg.show_count = true;
                    ci += 1;
                }
                'd' => {
                    cfg.repeated_only = true;
                    ci += 1;
                }
                'D' => {
                    cfg.all_repeated = true;
                    repeated_occ = Some(None);
                    ci += 1;
                }
                'i' => {
                    cfg.ignore_case = true;
                    ci += 1;
                }
                'u' => {
                    cfg.unique_only = true;
                    ci += 1;
                }
                'z' => {
                    cfg.delimiter = b'\0';
                    ci += 1;
                }
                'f' | 's' | 'w' => {
                    let rest: String = chars[ci + 1..].iter().collect();
                    let val = if !rest.is_empty() {
                        ci = chars.len();
                        rest
                    } else if let Some(next_arg) = args.get(idx + 1) {
                        consumed_next = true;
                        ci = chars.len();
                        next_arg.to_string_lossy().into_owned()
                    } else {
                        let flag = c.to_string();
                        let _ = usage_error(
                            ctx,
                            io,
                            "uniq",
                            &argopt::short_option_requires_argument(ctx, &flag),
                        );
                        return Ok(1);
                    };
                    match c {
                        'f' => match parse_size_opt(&val, "fields to skip") {
                            Ok(n) => {
                                cfg.skip_fields = n;
                                explicit_skip_fields = true;
                            }
                            Err(msg) => {
                                let _ = writeln!(io.stderr, "uniq: {msg}");
                                return Ok(1);
                            }
                        },
                        's' => match parse_size_opt(&val, "bytes to skip") {
                            Ok(n) => cfg.skip_chars = n,
                            Err(msg) => {
                                let _ = writeln!(io.stderr, "uniq: {msg}");
                                return Ok(1);
                            }
                        },
                        'w' => match parse_size_opt(&val, "bytes to compare") {
                            Ok(n) => cfg.check_chars = Some(n),
                            Err(msg) => {
                                let _ = writeln!(io.stderr, "uniq: {msg}");
                                return Ok(1);
                            }
                        },
                        _ => unreachable!(),
                    }
                }
                '0'..='9' => {
                    let mut val: usize = 0;
                    while ci < chars.len() && chars[ci].is_ascii_digit() {
                        val = val
                            .saturating_mul(10)
                            .saturating_add((chars[ci] as u8 - b'0') as usize);
                        ci += 1;
                    }
                    cfg.check_chars = Some(val);
                }
                _ => {
                    let _ = usage_error(ctx, io, "uniq", &argopt::invalid_option(ctx, &c.to_string()));
                    return Ok(1);
                }
            }
        }
        idx += if consumed_next { 2 } else { 1 };
    }

    if cfg.repeated_only {
        cfg.output_unique = false;
    }
    if cfg.all_repeated {
        cfg.output_unique = false;
        cfg.output_later_repeated = true;
    }
    if cfg.unique_only {
        cfg.output_first_repeated = false;
    }

    if !explicit_skip_fields && !obsolete_digits.is_empty() {
        let mut acc: usize = 0;
        for tok in &obsolete_digits {
            for b in tok.bytes() {
                if b.is_ascii_digit() {
                    acc = acc.saturating_mul(10).saturating_add((b - b'0') as usize);
                }
            }
        }
        cfg.skip_fields = acc;
    }

    if let Some(opt_val) = group_occ {
        cfg.group_all = true;
        match opt_val {
            None => cfg.group_mode = GroupMode::Separate,
            Some(v) => match parse_group_mode(&v) {
                Ok(m) => cfg.group_mode = m,
                Err(msg) => {
                    let _ = writeln!(io.stderr, "uniq: {msg}");
                    return Ok(1);
                }
            },
        }
    } else if let Some(opt_val) = repeated_occ {
        match opt_val {
            None => cfg.group_mode = GroupMode::None,
            Some(v) => match parse_all_repeated_mode(&v) {
                Ok(m) => cfg.group_mode = m,
                Err(msg) => {
                    let _ = writeln!(io.stderr, "uniq: {msg}");
                    return Ok(1);
                }
            },
        }
    }

    let output_option_used =
        cfg.show_count || cfg.repeated_only || cfg.all_repeated || cfg.unique_only;
    if cfg.group_all && output_option_used {
        let _ = writeln!(
            io.stderr,
            "uniq: --group is mutually exclusive with -c/-d/-D/-u"
        );
        return Ok(1);
    }
    if cfg.show_count && cfg.output_later_repeated {
        let _ = writeln!(
            io.stderr,
            "uniq: printing all duplicated lines and repeat counts is meaningless"
        );
        return Ok(1);
    }

    // [GNU] +NUM obsolete skip-chars operand
    let mut files_only: Vec<String> = Vec::new();
    for op in raw_operands {
        if let Some(rest) = op.strip_prefix('+') {
            if !rest.is_empty() && rest.bytes().all(|b| b.is_ascii_digit()) {
                cfg.skip_chars = rest.parse::<usize>().unwrap_or(usize::MAX);
                continue;
            }
        }
        files_only.push(op);
    }

    // [native-glob] expand wildcard on INPUT operand when enabled
    if !files_only.is_empty() && crate::glob::contains_wildcard(&files_only[0]) {
        if let Some(expanded) = crate::glob::glob_expand_in(ctx, &files_only[0]) {
            let mut merged = expanded;
            merged.extend(files_only.into_iter().skip(1));
            files_only = merged;
        }
    }

    if files_only.len() > 2 {
        let _ = usage_error(
            ctx,
            io,
            "uniq",
            &format!("extra operand '{}'", files_only[2]),
        );
        return Ok(1);
    }
    if !files_only.is_empty() {
        cfg.input = files_only[0].clone();
    }
    if files_only.len() == 2 {
        cfg.output = files_only[1].clone();
    }

    match run_uniq(&cfg, io, ctx) {
        Ok(()) => Ok(0),
        Err((rc, msg)) => {
            let _ = writeln!(io.stderr, "uniq: {msg}");
            Ok(rc)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run_mem(args: &[&str], stdin_str: &str) -> (u32, String, String) {
        let sc = ScratchCtx::new("uniq");
        let mut io = MemIo::new(stdin_str);
        let ctx = sc.ctx();
        let os_args: Vec<OsString> = args.iter().map(OsString::from).collect();
        let rc = uniq_cmd(&os_args, &mut io.split(), &ctx).unwrap();
        (rc, io.out(), io.err())
    }

    #[test]
    fn basic_and_count() {
        let (rc, out, err) = run_mem(&[], "a\na\nb\nc\nc\nc\n");
        assert_eq!(rc, 0);
        assert!(err.is_empty());
        assert_eq!(out, "a\nb\nc\n");

        let (rc, out, _) = run_mem(&["-c"], "a\na\nb\n");
        assert_eq!(rc, 0);
        assert_eq!(out, "      2 a\n      1 b\n");
    }

    #[test]
    fn repeated_and_unique_and_all_repeated() {
        let (rc, out, _) = run_mem(&["-d"], "a\na\nb\nc\nc\n");
        assert_eq!(rc, 0);
        assert_eq!(out, "a\nc\n");

        let (rc, out, _) = run_mem(&["-u"], "a\na\nb\nc\nc\n");
        assert_eq!(rc, 0);
        assert_eq!(out, "b\n");

        let (rc, out, _) = run_mem(&["-D"], "a\na\nb\nc\nc\n");
        assert_eq!(rc, 0);
        assert_eq!(out, "a\na\nc\nc\n");

        let (rc, out, _) = run_mem(&["--all-repeated=separate"], "a\na\nb\nc\nc\n");
        assert_eq!(rc, 0);
        assert_eq!(out, "a\na\n\nc\nc\n");
    }

    #[test]
    fn skip_fields_chars_check_and_case() {
        let (rc, out, _) = run_mem(
            &["-f", "1", "-s", "1", "-w", "2", "-i"],
            "1 aXY\n2 Axy\n3 bXY\n",
        );
        assert_eq!(rc, 0);
        assert_eq!(out, "1 aXY\n3 bXY\n");

        let (rc, out, _) = run_mem(&["-1", "+1"], "x aa\ny ab\n");
        assert_eq!(rc, 0);
        assert_eq!(out, "x aa\ny ab\n");

        let (rc, out, _) = run_mem(&["-D5"], "abcdefX\nabcdefY\n");
        assert_eq!(rc, 0);
        assert_eq!(out, "abcdefX\nabcdefY\n");
    }

    #[test]
    fn group_modes_and_conflicts() {
        let (rc, out, _) = run_mem(&["--group=both"], "a\na\nb\n");
        assert_eq!(rc, 0);
        assert_eq!(out, "\na\na\n\nb\n\n");

        let (rc, _, err) = run_mem(&["--group", "-c"], "a\n");
        assert_eq!(rc, 1);
        assert!(!err.is_empty());

        let (rc, _, err) = run_mem(&["-c", "-D"], "a\n");
        assert_eq!(rc, 1);
        assert!(!err.is_empty());
    }

    #[test]
    fn zero_terminated_and_argmatch_empty() {
        let (rc, out, _) = run_mem(&["-z"], "a\nb\0a\nb\0c\0");
        assert_eq!(rc, 0);
        assert_eq!(out, "a\nb\0c\0");

        let (rc, _, err) = run_mem(&["--group="], "");
        assert_eq!(rc, 1);
        assert!(err.contains("ambiguous argument ''"));
    }
}
