//! `wc` — GNU coreutils wc.c (9.4) semantics, ported for WinuxCmd 2.0 (#1488).
//!
//! Prints newline, word, character, byte, and max-display-width counts for
//! each FILE (or stdin), with `--files0-from`, `--total=WHEN`, `--debug`,
//! GNU UTF-8 / control / wide-char rules, and explicit `Io`/`Ctx`.

use std::ffi::OsString;
use std::io::{Read, Write};

use crate::argopt;
use winux_core::{usage_error, CmdResult, Ctx, Io};

#[derive(Clone, Copy)]
struct CountRequest {
    lines: bool,
    words: bool,
    chars: bool,
    bytes: bool,
    max_line_length: bool,
}

impl CountRequest {
    fn only_bytes(self) -> bool {
        self.bytes && !self.lines && !self.words && !self.chars && !self.max_line_length
    }

    fn lines_and_optional_bytes_only(self) -> bool {
        self.lines && !self.words && !self.chars && !self.max_line_length
    }

    fn selected_count(self) -> usize {
        usize::from(self.lines)
            + usize::from(self.words)
            + usize::from(self.chars)
            + usize::from(self.bytes)
            + usize::from(self.max_line_length)
    }
}

#[derive(Clone)]
struct CountResult {
    lines: u64,
    words: u64,
    chars: u64,
    bytes: u64,
    max_line_length: u64,
    filename: String,
    display_filename: bool,
}

impl CountResult {
    fn empty(filename: String, display_filename: bool) -> Self {
        Self {
            lines: 0,
            words: 0,
            chars: 0,
            bytes: 0,
            max_line_length: 0,
            filename,
            display_filename,
        }
    }
}

fn quote_name_for_output(name: &str) -> String {
    if !name.contains('\n') {
        return name.to_string();
    }
    let mut out = String::new();
    let mut start = 0usize;
    loop {
        match name[start..].find('\n') {
            Some(rel) => {
                let part = &name[start..start + rel];
                if !part.is_empty() {
                    out.push('\'');
                    for ch in part.chars() {
                        if ch == '\'' {
                            out.push_str("'\\''");
                        } else {
                            out.push(ch);
                        }
                    }
                    out.push('\'');
                }
                out.push_str("$'\\n'");
                start += rel + 1;
            }
            None => {
                let part = &name[start..];
                if !part.is_empty() {
                    out.push('\'');
                    for ch in part.chars() {
                        if ch == '\'' {
                            out.push_str("'\\''");
                        } else {
                            out.push(ch);
                        }
                    }
                    out.push('\'');
                }
                break;
            }
        }
    }
    out
}

fn decode_utf8_char(data: &[u8], pos: usize) -> (u32, usize, bool) {
    let first = data[pos];
    if first < 0x80 {
        return (first as u32, 1, true);
    }
    let cont = |idx: usize| idx < data.len() && (data[idx] & 0xC0) == 0x80;
    if (first & 0xE0) == 0xC0 && cont(pos + 1) {
        let cp = (((first & 0x1F) as u32) << 6) | ((data[pos + 1] & 0x3F) as u32);
        if cp >= 0x80 {
            return (cp, 2, true);
        }
    }
    if (first & 0xF0) == 0xE0 && cont(pos + 1) && cont(pos + 2) {
        let cp = (((first & 0x0F) as u32) << 12)
            | (((data[pos + 1] & 0x3F) as u32) << 6)
            | ((data[pos + 2] & 0x3F) as u32);
        if cp >= 0x800 && !(0xD800..=0xDFFF).contains(&cp) {
            return (cp, 3, true);
        }
    }
    if (first & 0xF8) == 0xF0 && cont(pos + 1) && cont(pos + 2) && cont(pos + 3) {
        let cp = (((first & 0x07) as u32) << 18)
            | (((data[pos + 1] & 0x3F) as u32) << 12)
            | (((data[pos + 2] & 0x3F) as u32) << 6)
            | ((data[pos + 3] & 0x3F) as u32);
        if (0x10000..=0x10FFFF).contains(&cp) {
            return (cp, 4, true);
        }
    }
    (first as u32, 1, false)
}

fn utf8_expected_bytes(first: u8) -> usize {
    if first < 0x80 {
        1
    } else if (first & 0xE0) == 0xC0 {
        2
    } else if (first & 0xF0) == 0xE0 {
        3
    } else if (first & 0xF8) == 0xF0 {
        4
    } else {
        1
    }
}

fn is_word_separator(cp: u32, posixly_correct: bool) -> bool {
    if cp == 0x1680
        || (0x2000..=0x200A).contains(&cp)
        || cp == 0x2028
        || cp == 0x2029
        || cp == 0x205F
        || cp == 0x3000
    {
        return true;
    }
    if cp == 0x00A0 || cp == 0x2007 || cp == 0x202F || cp == 0x2060 {
        return !posixly_correct;
    }
    false
}

fn is_nonprintable(cp: u32) -> bool {
    cp < 0x20 || cp == 0x7F || (0x80..=0x9F).contains(&cp)
}

fn is_wide_codepoint(cp: u32) -> bool {
    (0x1100..=0x115F).contains(&cp)
        || (0x2329..=0x232A).contains(&cp)
        || (0x2E80..=0xA4CF).contains(&cp)
        || (0xAC00..=0xD7A3).contains(&cp)
        || (0xF900..=0xFAFF).contains(&cp)
        || (0xFE10..=0xFE19).contains(&cp)
        || (0xFE30..=0xFE6F).contains(&cp)
        || (0xFF00..=0xFF60).contains(&cp)
        || (0xFFE0..=0xFFE6).contains(&cp)
        || (0x1F300..=0x1FAFF).contains(&cp)
        || (0x20000..=0x3FFFD).contains(&cp)
}

fn display_width(cp: u32) -> u64 {
    if is_nonprintable(cp) {
        return 0;
    }
    if (0x0300..=0x036F).contains(&cp)
        || (0x0483..=0x0489).contains(&cp)
        || (0x1AB0..=0x1AFF).contains(&cp)
        || (0x1DC0..=0x1DFF).contains(&cp)
        || (0x20D0..=0x20FF).contains(&cp)
        || (0xFE00..=0xFE0F).contains(&cp)
        || (0xFE20..=0xFE2F).contains(&cp)
    {
        return 0;
    }
    if is_wide_codepoint(cp) {
        2
    } else {
        1
    }
}

fn count_reader(
    reader: &mut dyn Read,
    filename: String,
    display_filename: bool,
    request: CountRequest,
    posixly_correct: bool,
) -> std::io::Result<CountResult> {
    let mut res = CountResult::empty(filename, display_filename);
    let mut buf = [0u8; 64 * 1024];

    if request.only_bytes() {
        loop {
            let n = reader.read(&mut buf)?;
            if n == 0 {
                break;
            }
            res.bytes += n as u64;
        }
        return Ok(res);
    }

    if request.lines_and_optional_bytes_only() {
        loop {
            let n = reader.read(&mut buf)?;
            if n == 0 {
                break;
            }
            res.bytes += n as u64;
            res.lines += buf[..n].iter().filter(|&&b| b == b'\n').count() as u64;
        }
        return Ok(res);
    }

    let mut current_line_len = 0u64;
    let mut in_word = false;
    let mut carry: Vec<u8> = Vec::new();

    fn finish_line(request: CountRequest, cur: &mut u64, max_len: &mut u64) {
        if request.max_line_length {
            if *cur > *max_len {
                *max_len = *cur;
            }
            *cur = 0;
        }
    }

    fn consume_cp(
        cp: u32,
        request: CountRequest,
        posixly_correct: bool,
        res: &mut CountResult,
        cur_len: &mut u64,
        in_word: &mut bool,
    ) {
        if request.chars {
            res.chars += 1;
        }
        match cp {
            0x0A => {
                if request.lines {
                    res.lines += 1;
                }
                finish_line(request, cur_len, &mut res.max_line_length);
                if request.words {
                    *in_word = false;
                }
            }
            0x0D | 0x0C => {
                finish_line(request, cur_len, &mut res.max_line_length);
                if request.words {
                    *in_word = false;
                }
            }
            0x09 => {
                if request.max_line_length {
                    *cur_len += 8 - (*cur_len % 8);
                }
                if request.words {
                    *in_word = false;
                }
            }
            0x20 => {
                if request.max_line_length {
                    *cur_len += 1;
                }
                if request.words {
                    *in_word = false;
                }
            }
            0x0B => {
                if request.words {
                    *in_word = false;
                }
            }
            _ => {
                if is_word_separator(cp, posixly_correct) {
                    if request.max_line_length {
                        *cur_len += display_width(cp);
                    }
                    if request.words {
                        *in_word = false;
                    }
                } else if !is_nonprintable(cp) {
                    if request.max_line_length {
                        *cur_len += display_width(cp);
                    }
                    if request.words && !*in_word {
                        res.words += 1;
                        *in_word = true;
                    }
                }
            }
        }
    }

    fn process_chunk(
        data: &[u8],
        final_chunk: bool,
        request: CountRequest,
        posixly_correct: bool,
        res: &mut CountResult,
        current_line_len: &mut u64,
        in_word: &mut bool,
        carry: &mut Vec<u8>,
    ) {
        let mut i = 0usize;
        while i < data.len() {
            let mut j = i;
            while j < data.len() && data[j] < 0x80 {
                let c = data[j];
                if (0x21..=0x7E).contains(&c) {
                    let mut k = j + 1;
                    while k < data.len() && (0x21..=0x7E).contains(&data[k]) {
                        k += 1;
                    }
                    let run = (k - j) as u64;
                    if request.chars {
                        res.chars += run;
                    }
                    if request.max_line_length {
                        *current_line_len += run;
                    }
                    if request.words && !*in_word {
                        res.words += 1;
                        *in_word = true;
                    }
                    j = k;
                    continue;
                }
                if request.chars {
                    res.chars += 1;
                }
                match c {
                    0x0A => {
                        if request.lines {
                            res.lines += 1;
                        }
                        finish_line(request, current_line_len, &mut res.max_line_length);
                        if request.words {
                            *in_word = false;
                        }
                    }
                    0x0D | 0x0C => {
                        finish_line(request, current_line_len, &mut res.max_line_length);
                        if request.words {
                            *in_word = false;
                        }
                    }
                    0x09 => {
                        if request.max_line_length {
                            *current_line_len += 8 - (*current_line_len % 8);
                        }
                        if request.words {
                            *in_word = false;
                        }
                    }
                    0x20 => {
                        if request.max_line_length {
                            *current_line_len += 1;
                        }
                        if request.words {
                            *in_word = false;
                        }
                    }
                    0x0B => {
                        if request.words {
                            *in_word = false;
                        }
                    }
                    _ => {}
                }
                j += 1;
            }
            i = j;
            if i >= data.len() {
                break;
            }
            let expected = utf8_expected_bytes(data[i]);
            if !final_chunk && expected > data.len() - i {
                carry.extend_from_slice(&data[i..]);
                return;
            }
            let (cp, step, valid) = decode_utf8_char(data, i);
            if valid {
                consume_cp(
                    cp,
                    request,
                    posixly_correct,
                    res,
                    current_line_len,
                    in_word,
                );
            }
            i += step;
        }
    }

    loop {
        let n = reader.read(&mut buf)?;
        if n == 0 {
            break;
        }
        res.bytes += n as u64;
        if carry.is_empty() {
            process_chunk(
                &buf[..n],
                false,
                request,
                posixly_correct,
                &mut res,
                &mut current_line_len,
                &mut in_word,
                &mut carry,
            );
        } else {
            let mut combined = std::mem::take(&mut carry);
            combined.extend_from_slice(&buf[..n]);
            process_chunk(
                &combined,
                false,
                request,
                posixly_correct,
                &mut res,
                &mut current_line_len,
                &mut in_word,
                &mut carry,
            );
        }
    }
    if !carry.is_empty() {
        let tail = std::mem::take(&mut carry);
        process_chunk(
            &tail,
            true,
            request,
            posixly_correct,
            &mut res,
            &mut current_line_len,
            &mut in_word,
            &mut carry,
        );
    }
    finish_line(request, &mut current_line_len, &mut res.max_line_length);
    Ok(res)
}

fn decimal_digits(mut v: u64) -> usize {
    let mut digits = 1usize;
    while v >= 10 {
        v /= 10;
        digits += 1;
    }
    digits
}

fn compute_number_width(
    ctx: &Ctx,
    requested_input_count: usize,
    results: &[CountResult],
    selected_count: usize,
    total_only: bool,
) -> usize {
    if total_only
        || requested_input_count == 0
        || (requested_input_count == 1 && selected_count == 1)
        || results.is_empty()
    {
        return 1;
    }
    let mut regular_total: u64 = 0;
    let mut saw_non_regular = false;
    for r in results {
        if !r.display_filename || r.filename == "-" {
            saw_non_regular = true;
            continue;
        }
        let resolved = crate::fsutil::resolve(ctx, &r.filename);
        match std::fs::metadata(&resolved) {
            Ok(meta) if meta.is_file() => {
                regular_total = regular_total.saturating_add(meta.len());
            }
            _ => {
                saw_non_regular = true;
            }
        }
    }
    let w = decimal_digits(regular_total);
    if saw_non_regular {
        w.max(7)
    } else {
        w
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

fn resolve_total_mode(val: &str) -> Result<&'static str, String> {
    let candidates = ["auto", "always", "only", "never"];
    let mut matches: Vec<&'static str> = Vec::new();
    for &c in &candidates {
        if c == val {
            return Ok(c);
        }
        if c.starts_with(val) {
            matches.push(c);
        }
    }
    if matches.len() == 1 {
        return Ok(matches[0]);
    }
    let kind = if matches.len() > 1 {
        "ambiguous"
    } else {
        "invalid"
    };
    Err(format!(
        "{kind} argument '{val}' for '--total'\nValid arguments are:\n  - 'auto'\n  - 'always'\n  - 'only'\n  - 'never'"
    ))
}

pub fn wc_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut print_lines = false;
    let mut print_words = false;
    let mut print_chars = false;
    let mut print_bytes = false;
    let mut print_max_len = false;
    let mut debug = false;
    let mut files0_from: Option<String> = None;
    let mut total_arg: Option<String> = None;
    let mut raw_operands: Vec<String> = Vec::new();

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
                "bytes" => print_bytes = true,
                "chars" => print_chars = true,
                "lines" => print_lines = true,
                "words" => print_words = true,
                "max-line-length" => print_max_len = true,
                "debug" => debug = true,
                "files0-from" => {
                    let Some(v) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(ctx, io, "wc", &argopt::option_requires_argument(ctx, "--files0-from"));
                        return Ok(1);
                    };
                    files0_from = Some(v);
                }
                "total" => {
                    let Some(v) = take_val(inline, args, &mut idx) else {
                        let _ = usage_error(ctx, io, "wc", &argopt::option_requires_argument(ctx, "--total"));
                        return Ok(1);
                    };
                    total_arg = Some(v);
                }
                "help" | "version" => return Ok(0),
                _ => {
                    let _ = usage_error(ctx, io, "wc", &argopt::unrecognized_option(ctx, &arg));
                    return Ok(1);
                }
            }
            idx += 1;
            continue;
        }
        for c in arg[1..].chars() {
            match c {
                'c' => print_bytes = true,
                'm' => print_chars = true,
                'l' => print_lines = true,
                'w' => print_words = true,
                'L' => print_max_len = true,
                _ => {
                    let _ = usage_error(ctx, io, "wc", &argopt::invalid_option(ctx, &c.to_string()));
                    return Ok(1);
                }
            }
        }
        idx += 1;
    }

    if !print_lines && !print_words && !print_chars && !print_bytes && !print_max_len {
        print_lines = true;
        print_words = true;
        print_bytes = true;
    }

    let request = CountRequest {
        lines: print_lines,
        words: print_words,
        chars: print_chars,
        bytes: print_bytes,
        max_line_length: print_max_len,
    };

    let total_when = match total_arg.as_deref() {
        Some(v) => match resolve_total_mode(v) {
            Ok(m) => m,
            Err(msg) => {
                let _ = writeln!(io.stderr, "wc: {msg}");
                return Ok(1);
            }
        },
        None => "auto",
    };

    let mut any_error = false;
    let mut paths: Vec<String> = Vec::new();
    let mut requested_input_count = 0usize;
    let has_files0 = files0_from.is_some();

    if let Some(ref f0) = files0_from {
        if !raw_operands.is_empty() {
            let _ = writeln!(
                io.stderr,
                "wc: extra operand '{}'\nfile operands cannot be combined with --files0-from",
                raw_operands[0]
            );
            return Ok(1);
        }
        let from_stdin = f0 == "-";
        let list_bytes = if from_stdin {
            let mut buf = Vec::new();
            if let Err(e) = io.stdin.read_to_end(&mut buf) {
                let _ = writeln!(io.stderr, "wc: 'standard input': {}", crate::fsutil::reason(&e));
                return Ok(1);
            }
            buf
        } else {
            match crate::fsutil::read_operand_all(ctx, f0) {
                Ok(b) => b,
                Err(e) => {
                    let reason = crate::fsutil::gnu_open_reason(ctx, f0, &e, false);
                    let _ = writeln!(io.stderr, "wc: cannot open '{f0}' for reading: {reason}");
                    return Ok(1);
                }
            }
        };
        let mut rec_no = 0usize;
        let mut start = 0usize;
        for (i, &b) in list_bytes.iter().enumerate() {
            if b == 0 {
                rec_no += 1;
                let rec = String::from_utf8_lossy(&list_bytes[start..i]).into_owned();
                if rec.is_empty() {
                    let _ = writeln!(io.stderr, "wc: {f0}:{rec_no}: invalid zero-length file name");
                    any_error = true;
                } else if from_stdin && rec == "-" {
                    let _ = writeln!(
                        io.stderr,
                        "wc: when reading file names from stdin, no file name of '-' allowed"
                    );
                    any_error = true;
                } else {
                    paths.push(rec);
                }
                start = i + 1;
            }
        }
        if start < list_bytes.len() {
            rec_no += 1;
            let rec = String::from_utf8_lossy(&list_bytes[start..]).into_owned();
            if from_stdin && rec == "-" {
                let _ = writeln!(
                    io.stderr,
                    "wc: when reading file names from stdin, no file name of '-' allowed"
                );
                any_error = true;
            } else {
                paths.push(rec);
            }
        }
        requested_input_count = rec_no;
    } else {
        for op in raw_operands {
            if crate::glob::contains_wildcard(&op) {
                if let Some(expanded) = crate::glob::glob_expand_in(ctx, &op) {
                    paths.extend(expanded);
                    continue;
                }
            }
            paths.push(op);
        }
        requested_input_count = paths.len();
    }

    let posixly_correct = ctx.env.get("POSIXLY_CORRECT").is_some();
    let mut results: Vec<CountResult> = Vec::new();

    if paths.is_empty() && !has_files0 {
        match count_reader(&mut io.stdin, "-".to_string(), false, request, posixly_correct) {
            Ok(r) => results.push(r),
            Err(e) => {
                let _ = writeln!(io.stderr, "wc: 'standard input': {}", crate::fsutil::reason(&e));
                any_error = true;
            }
        }
        requested_input_count = 1;
    } else {
        for path in &paths {
            if path == "-" {
                match count_reader(&mut io.stdin, "-".to_string(), true, request, posixly_correct) {
                    Ok(r) => results.push(r),
                    Err(e) => {
                        let _ = writeln!(io.stderr, "wc: -: {}", crate::fsutil::reason(&e));
                        any_error = true;
                    }
                }
                continue;
            }
            match crate::fsutil::open_read_operand(ctx, path) {
                Ok(mut f) => {
                    match count_reader(&mut f, path.clone(), true, request, posixly_correct) {
                        Ok(r) => results.push(r),
                        Err(e) => {
                            let _ = writeln!(
                                io.stderr,
                                "wc: {}: {}",
                                quote_name_for_output(path),
                                crate::fsutil::reason(&e)
                            );
                            any_error = true;
                        }
                    }
                }
                Err(e) => {
                    let is_dir = crate::fsutil::failed_open_is_directory(ctx, path);
                    let reason = crate::fsutil::gnu_open_reason(ctx, path, &e, false);
                    let _ = writeln!(
                        io.stderr,
                        "wc: {}: {reason}",
                        quote_name_for_output(path)
                    );
                    if is_dir {
                        results.push(CountResult::empty(path.clone(), true));
                    }
                    any_error = true;
                }
            }
        }
    }

    if debug {
        let impl_name = if request.only_bytes() {
            "byte-count fast path"
        } else if request.lines_and_optional_bytes_only() {
            "line-count byte scan"
        } else {
            "multicolumn utf-8 scanner"
        };
        let _ = writeln!(io.stderr, "wc: debug: line count implementation: {impl_name}");
    }

    let print_total = match total_when {
        "always" | "only" => true,
        "auto" => requested_input_count > 1,
        _ => false,
    };

    let mut total = CountResult::empty("total".to_string(), true);
    for r in &results {
        total.lines += r.lines;
        total.words += r.words;
        total.chars += r.chars;
        total.bytes += r.bytes;
        total.max_line_length = total.max_line_length.max(r.max_line_length);
    }

    let width = compute_number_width(
        ctx,
        requested_input_count,
        &results,
        request.selected_count(),
        total_when == "only",
    );

    fn emit_row(
        out: &mut dyn Write,
        r: &CountResult,
        request: CountRequest,
        width: usize,
        show_name: bool,
    ) -> std::io::Result<()> {
        let mut first = true;
        for (enabled, val) in [
            (request.lines, r.lines),
            (request.words, r.words),
            (request.chars, r.chars),
            (request.bytes, r.bytes),
            (request.max_line_length, r.max_line_length),
        ] {
            if enabled {
                if !first {
                    out.write_all(b" ")?;
                }
                write!(out, "{val:>width$}")?;
                first = false;
            }
        }
        if show_name {
            if !first {
                out.write_all(b" ")?;
            }
            out.write_all(quote_name_for_output(&r.filename).as_bytes())?;
        }
        out.write_all(b"\n")
    }

    if total_when == "only" {
        let _ = emit_row(&mut *io.stdout, &total, request, width, false);
    } else {
        for r in &results {
            let _ = emit_row(&mut *io.stdout, r, request, width, r.display_filename);
        }
        if print_total {
            let _ = emit_row(&mut *io.stdout, &total, request, width, true);
        }
    }

    let _ = io.stdout.flush();
    Ok(if any_error { 1 } else { 0 })
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run_mem(args: &[&str], stdin_str: &str) -> (u32, String, String) {
        let sc = ScratchCtx::new("wc");
        let mut io = MemIo::new(stdin_str);
        let ctx = sc.ctx();
        let os_args: Vec<OsString> = args.iter().map(OsString::from).collect();
        let rc = wc_cmd(&os_args, &mut io.split(), &ctx).unwrap();
        (rc, io.out(), io.err())
    }

    #[test]
    fn basic_stdin_and_flags() {
        let (rc, out, err) = run_mem(&[], "hello world\nsecond line\n");
        assert_eq!(rc, 0);
        assert!(err.is_empty());
        assert_eq!(out, "      2       4      24\n");

        let (rc, out, _) = run_mem(&["-l"], "a\nb\nc\n");
        assert_eq!(rc, 0);
        assert_eq!(out, "3\n");
    }

    #[test]
    fn utf8_chars_and_display_width() {
        let (rc, out, _) = run_mem(&["-m", "-L"], "a\t中\n");
        assert_eq!(rc, 0);
        assert_eq!(out, "      4      10\n");
    }

    #[test]
    fn total_argmatch_and_errors() {
        let (rc, out, _) = run_mem(&["--total=al"], "a b\n");
        assert_eq!(rc, 0);
        assert_eq!(
            out,
            "      1       2       4\n      1       2       4 total\n"
        );

        let (rc, _, err) = run_mem(&["--total=a"], "a\n");
        assert_eq!(rc, 1);
        assert!(err.contains("ambiguous argument 'a'"));
    }
}
