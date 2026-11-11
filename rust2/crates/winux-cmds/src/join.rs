//! `join` — GNU coreutils join semantics (src/join.c, coreutils 9.4).
//! Every case below was probed byte-for-byte against WSL coreutils 9.4
//! (target/probes/p_join*.sh).
//!
//! * -1/-2/-j set the join fields as ENCOUNTERED in argv order; assigning
//!   incompatible values twice is fatal (`join -j 3 -1 2` reports
//!   "incompatible join fields 2, 1" — the zero-based fields). Field
//!   numbers go through xstrtol: `01` is 1, out-of-range numbers are
//!   silently clamped (probed: `-1 99999999999999999999` runs), and only
//!   1/2 are valid for -a/-v.
//! * -t takes ONE byte: '' means '\n' (whole-line records) and the
//!   two-character spelling "\0" means NUL; a different second -t is
//!   "incompatible tabs", a longer value "multi-character tab 'X'" (this
//!   also rejects multi-byte UTF-8 tabs — join is byte-oriented).
//! * -o lists split on every ','/' '/'\t'; every item is decoded and
//!   EMPTY items are fatal ("-o '0,'" errors). `auto` builds the default
//!   key + rest-of-both list from the FIRST lines of each file.
//! * Keys: a line whose join field is missing never pairs (not even with
//!   another keyless line — probed), but prints as an unpaired line with
//!   an empty key. -i compares keys case-insensitively.
//! * Order checking: --check-order makes the first "FILE:LINE: is not
//!   sorted: LINE" fatal; the default warns only once a line has gone
//!   unpairable and ends "join: input is not in sorted order" rc 1;
//!   --nocheck-order (last of the pair wins) never checks.
//! * --header joins the first line of each file before the data; -z
//!   switches the record delimiter to NUL; the output separator follows
//!   -t ('\n' for whole-line mode, one space by default).

use std::ffi::OsString;
use std::io::{Read, Write};

use crate::argopt::{self, OptSpec};
use crate::fsutil::resolve;
use winux_core::{usage_error, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    // GNU join defines NO long forms for the letter options; the `long`
    // field carries the short letter as the internal argopt key.
    OptSpec {
        short: Some('1'),
        long: "1",
        takes_value: true,
    },
    OptSpec {
        short: Some('2'),
        long: "2",
        takes_value: true,
    },
    OptSpec {
        short: Some('t'),
        long: "t",
        takes_value: true,
    },
    OptSpec {
        short: Some('e'),
        long: "e",
        takes_value: true,
    },
    OptSpec {
        short: Some('o'),
        long: "o",
        takes_value: true,
    },
    OptSpec {
        short: Some('j'),
        long: "j",
        takes_value: true,
    },
    OptSpec {
        short: Some('a'),
        long: "a",
        takes_value: true,
    },
    OptSpec {
        short: Some('v'),
        long: "v",
        takes_value: true,
    },
    OptSpec {
        short: Some('i'),
        long: "ignore-case",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "check-order",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "nocheck-order",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "header",
        takes_value: false,
    },
    OptSpec {
        short: Some('z'),
        long: "zero-terminated",
        takes_value: false,
    },
];

#[derive(Clone, Copy)]
struct OutputField {
    file: u8,
    field: i64,
}

struct Config {
    field1: i64,
    field2: i64,
    explicit_separator: bool,
    whole_line: bool,
    ignore_case: bool,
    check_order: bool,
    nocheck_order: bool,
    header: bool,
    zero_terminated: bool,
    separator: char,
    empty_field: String,
    output_auto: bool,
    output_fields: Vec<OutputField>,
    print_unpaired1: bool,
    print_unpaired2: bool,
    only_unpaired: bool,
    files: Vec<String>,
    last_raw_arg: String,
}

/// xstrtol-shaped field parse: leading int, clamped when out of range.
fn parse_positive_field(text: &str) -> Result<i64, String> {
    match text.parse::<i64>() {
        Ok(v) => {
            if v <= 0 {
                Err(format!("invalid field number: '{text}'"))
            } else {
                Ok(v)
            }
        }
        Err(e) => {
            // out-of-range integers are silently clamped (xstrtoimax)
            if text
                .chars()
                .enumerate()
                .all(|(i, c)| c.is_ascii_digit() && !(i == 0 && c == '0' && text.len() > 1))
                && *e.kind() == std::num::IntErrorKind::PosOverflow
            {
                Ok(i64::MAX)
            } else {
                Err(format!("invalid field number: '{text}'"))
            }
        }
    }
}

/// Only 1/2 are valid file numbers for -a/-v (through xstrtol, so "01" is 1).
fn parse_file_number(value: &str) -> Result<i64, String> {
    match value.parse::<i64>() {
        Ok(v @ (1 | 2)) => Ok(v),
        _ => Err(format!("invalid field number: '{value}'")),
    }
}

fn split_fields(line: &str, cfg: &Config) -> Vec<String> {
    if cfg.whole_line {
        return vec![line.to_string()];
    }
    if !cfg.explicit_separator {
        // blank-delimited fields collapse runs of blanks (also a '\n' when
        // the NUL delimiter made newline an ordinary byte)
        let mut fields = Vec::new();
        let mut pos = 0usize;
        let bytes = line.as_bytes();
        while pos < bytes.len() {
            while pos < bytes.len()
                && (bytes[pos] == b' '
                    || bytes[pos] == b'\t'
                    || (cfg.zero_terminated && bytes[pos] == b'\n'))
            {
                pos += 1;
            }
            if pos >= bytes.len() {
                break;
            }
            let start = pos;
            while pos < bytes.len()
                && bytes[pos] != b' '
                && bytes[pos] != b'\t'
                && !(cfg.zero_terminated && bytes[pos] == b'\n')
            {
                pos += 1;
            }
            fields.push(line[start..pos].to_string());
        }
        return fields;
    }
    let sep = cfg.separator as u8 as char;
    line.split(sep).map(str::to_string).collect()
}

struct ParsedLine {
    fields: Vec<String>,
    /// None when the join field is missing: such lines never pair.
    key: Option<String>,
}

fn parse_line(line: &str, cfg: &Config, field_num: i64) -> ParsedLine {
    let fields = split_fields(line, cfg);
    let key = if field_num >= 1 && field_num <= fields.len() as i64 {
        let mut k = fields[(field_num - 1) as usize].clone();
        if cfg.ignore_case {
            k = k.to_lowercase();
        }
        Some(k)
    } else {
        None
    };
    ParsedLine { fields, key }
}

/// Decode the -o output format (add_field_list/decode_field_spec): items
/// split on each ','/' '/'\t' and EVERY item is decoded, empty items fatal.
fn parse_output_format(format: &str) -> Result<Vec<OutputField>, String> {
    let mut fields = Vec::new();
    let tokens: Vec<&str> = format
        .split(|c: char| c == ',' || c == ' ' || c == '\t')
        .collect();
    for token in tokens {
        let first = token.chars().next().unwrap_or('\0');
        if token.is_empty() || (first != '0' && first != '1' && first != '2') {
            return Err(format!("invalid file number in field spec: '{token}'"));
        }
        if first == '0' {
            if token.len() != 1 {
                return Err(format!("invalid field specifier: '{token}'"));
            }
            fields.push(OutputField { file: 0, field: 0 });
        } else {
            if token.len() < 2 || token.as_bytes()[1] != b'.' {
                return Err(format!("invalid field specifier: '{token}'"));
            }
            let field = parse_positive_field(&token[2..])?;
            fields.push(OutputField {
                file: first as u8 - b'0',
                field,
            });
        }
    }
    Ok(fields)
}

fn output_separator(cfg: &Config) -> String {
    // with -t '' the record separator '\n' is also the output separator
    if cfg.whole_line {
        "\n".to_string()
    } else if !cfg.explicit_separator {
        " ".to_string()
    } else {
        cfg.separator.to_string()
    }
}

fn field_value(line: Option<&ParsedLine>, field: i64, cfg: &Config) -> String {
    match line {
        Some(l) if field >= 1 && field <= l.fields.len() as i64 => {
            l.fields[(field - 1) as usize].clone()
        }
        _ => cfg.empty_field.clone(),
    }
}

fn join_key_value(line1: Option<&ParsedLine>, line2: Option<&ParsedLine>, cfg: &Config) -> String {
    if let Some(l) = line1 {
        if cfg.field1 >= 1 && cfg.field1 <= l.fields.len() as i64 {
            return l.fields[(cfg.field1 - 1) as usize].clone();
        }
    }
    if let Some(l) = line2 {
        if cfg.field2 >= 1 && cfg.field2 <= l.fields.len() as i64 {
            return l.fields[(cfg.field2 - 1) as usize].clone();
        }
    }
    cfg.empty_field.clone()
}

fn build_auto_output_fields(
    first1: Option<&ParsedLine>,
    first2: Option<&ParsedLine>,
    cfg: &Config,
) -> Vec<OutputField> {
    let mut fields = vec![OutputField { file: 0, field: 0 }];
    if let Some(l) = first1 {
        for i in 1..=l.fields.len() as i64 {
            if i != cfg.field1 {
                fields.push(OutputField { file: 1, field: i });
            }
        }
    }
    if let Some(l) = first2 {
        for i in 1..=l.fields.len() as i64 {
            if i != cfg.field2 {
                fields.push(OutputField { file: 2, field: i });
            }
        }
    }
    fields
}

fn build_output_line(
    line1: Option<&ParsedLine>,
    line2: Option<&ParsedLine>,
    cfg: &Config,
    output_fields: &[OutputField],
) -> String {
    let mut out_fields: Vec<String> = Vec::new();
    if !output_fields.is_empty() {
        for spec in output_fields {
            match spec.file {
                0 => out_fields.push(join_key_value(line1, line2, cfg)),
                1 => out_fields.push(field_value(line1, spec.field, cfg)),
                _ => out_fields.push(field_value(line2, spec.field, cfg)),
            }
        }
    } else {
        out_fields.push(join_key_value(line1, line2, cfg));
        if let Some(l) = line1 {
            for (i, f) in l.fields.iter().enumerate() {
                if i as i64 + 1 != cfg.field1 {
                    out_fields.push(f.clone());
                }
            }
        }
        if let Some(l) = line2 {
            for (i, f) in l.fields.iter().enumerate() {
                if i as i64 + 1 != cfg.field2 {
                    out_fields.push(f.clone());
                }
            }
        }
    }
    let sep = output_separator(cfg);
    out_fields.join(&sep)
}

/// Key comparison; keyless lines never compare equal to anything.
fn key_cmp(a: Option<&String>, b: Option<&String>) -> std::cmp::Ordering {
    match (a, b) {
        (Some(x), Some(y)) => x.cmp(y),
        // keyless lines sort as "less" so the merge walks past them; the
        // equality case (None, None) is prevented by advancing both sides
        _ => {
            if a.is_none() && b.is_none() {
                std::cmp::Ordering::Less
            } else if a.is_none() {
                std::cmp::Ordering::Less
            } else {
                std::cmp::Ordering::Greater
            }
        }
    }
}

/// join.c: in the default check mode a disorder warning is issued only
/// after an unpairable line has been seen; --check-order makes the first
/// disorder fatal. Returns false when processing must stop.
struct OrderState {
    seen_unpairable: bool,
    issued: [bool; 2],
    disorder: bool,
    next_check: [usize; 2],
}

#[allow(clippy::too_many_arguments)]
fn check_line_order(
    state: &mut OrderState,
    which: usize,
    idx: usize,
    start: usize,
    parsed: &[ParsedLine],
    raw_lines: &[String],
    fname: &str,
    cfg: &Config,
    io: &mut Io,
) -> bool {
    while state.next_check[which - 1] <= idx {
        let nc = state.next_check[which - 1];
        if nc > start
            && !state.issued[which - 1]
            && !cfg.nocheck_order
            && (cfg.check_order || state.seen_unpairable)
            && nc > 0
        {
            if let (Some(cur), Some(prev)) = (parsed.get(nc), parsed.get(nc - 1)) {
                if let (Some(ck), Some(pk)) = (&cur.key, &prev.key) {
                    if ck < pk {
                        state.issued[which - 1] = true;
                        let message =
                            format!("join: {fname}:{}: is not sorted: {}", nc + 1, raw_lines[nc]);
                        let _ = writeln!(io.stderr, "{message}");
                        if cfg.check_order {
                            return false;
                        }
                        state.disorder = true;
                    }
                }
            }
        }
        state.next_check[which - 1] += 1;
    }
    true
}

/// [GNU] set_join_field (join.c): a conflicting re-set is fatal; the
/// diagnostic names the fields 0-based ("incompatible join fields 0, 1").
fn apply_field(
    which: u8,
    value: i64,
    cfg: &mut Config,
    field1_seen: &mut bool,
    field2_seen: &mut bool,
) -> Result<(), String> {
    let (slot, seen) = if which == 1 {
        (&mut cfg.field1, field1_seen)
    } else {
        (&mut cfg.field2, field2_seen)
    };
    if *seen && *slot != value {
        return Err(format!(
            "incompatible join fields {}, {}",
            *slot - 1,
            value - 1
        ));
    }
    *seen = true;
    *slot = value;
    Ok(())
}

/// GNU join.c main() tracks what each option left pending through
/// `operand_status`: after `-o LIST` the next operand MIGHT_BE_O_ARG, after
/// an attached `-j1`/`-j2` it MIGHT_BE_J1/J2_ARG, otherwise MUST_BE_OPERAND.
/// add_file_name() then resolves a THIRD file name as the pending option's
/// argument instead of erroring — the obsolescent forms (all probed against
/// WSL coreutils 9.4, target/probes):
///
/// * `join -o 1.2 2.2 f1 f2` — "2.2" joins the -o list, files f1 f2,
///   output `1 X`/`2 Y` rc 0.
/// * `join -o 1.2 2.2 abc f1 f2` — chained absorption, "abc" decodes as a
///   spec and dies `invalid file number in field spec: 'abc'` rc 1.
/// * `join X -o 1.2 f1 f2` — X is a real operand, so the third name
///   resolves f1 (the SECOND slot) as the -o argument: dies
///   `invalid file number in field spec: 'f1'` rc 1.
/// * `join -j1 2 f1 f2` — field 1 of file 1 is 2, rc 0; `-j1` without a
///   separate argument falls back to `-j 1`.
/// * `join f1 f2 f3` — plain extra operand, rc 1 (unchanged).
///
/// argopt::parse keeps values and operands in separate vectors, so this
/// walk replays raw argv for the order-sensitive options only (-o/-1/-2/-j
/// apply at option time, exactly in getopt order); -t/-e/-a/-v and the
/// flags stay on the Parsed result (order-insensitive against this
/// machine). Returns (real_lists, saw_auto, file names, jcount leftovers).
#[allow(clippy::type_complexity)]
fn scan_order_sensitive(
    args: &[OsString],
    io: &mut Io,
    cfg: &mut Config,
    field1_seen: &mut bool,
    field2_seen: &mut bool,
) -> Result<(Vec<String>, bool, Vec<String>, [usize; 2]), ()> {
    #[derive(Clone, Copy, PartialEq)]
    enum OpStatus {
        Operand,
        OArg,
        J1Arg,
        J2Arg,
    }

    // Short options that take a value (join.c optstring "a:e:1:2:j:o:t:v:z"
    // plus flag chars); every other spelling is argopt's to reject before
    // this walk runs.
    fn takes_value(c: char) -> bool {
        matches!(c, 'a' | 'e' | 'j' | 'o' | 't' | 'v' | '1' | '2')
    }

    let mut real_lists: Vec<String> = Vec::new();
    let mut saw_auto = false;
    let mut names: Vec<String> = Vec::new();
    let mut status: Vec<OpStatus> = Vec::new();
    let mut jcount = [0usize; 2];
    let mut no_more_options = false;
    let mut prev = OpStatus::Operand;
    let mut i = 0usize;

    while i < args.len() {
        let tok = args[i].to_string_lossy().into_owned();
        if !no_more_options && tok == "--" {
            // [GNU] operands after "--" are MUST_BE_OPERAND: the post-getopt
            // add_file_name loop resets prev_optc_status.
            no_more_options = true;
            prev = OpStatus::Operand;
            i += 1;
            continue;
        }
        if !no_more_options && tok.starts_with("--") {
            // Long options of join are all flags (argopt validated the
            // token, --help/--version already returned).
            prev = OpStatus::Operand;
            i += 1;
            continue;
        }
        if !no_more_options && tok.len() > 1 && tok.starts_with('-') {
            // getopt cluster: leading flag chars, then an optional
            // value-taking char whose argument is the rest of the token
            // (attached) or the next argv entry.
            let mut chars = tok.chars();
            let _dash = chars.next();
            let mut value_of: Option<char> = None;
            for c in chars.by_ref() {
                if takes_value(c) {
                    value_of = Some(c);
                    break;
                }
            }
            match value_of {
                None => {
                    prev = OpStatus::Operand;
                    i += 1;
                }
                Some(c) => {
                    let rest: String = tok
                        .splitn(2, c)
                        .nth(1)
                        .unwrap_or("")
                        .to_string();
                    let attached = !rest.is_empty();
                    let val = if attached {
                        rest
                    } else {
                        i += 1;
                        args.get(i)
                            .map(|a| a.to_string_lossy().into_owned())
                            .unwrap_or_default()
                    };
                    match c {
                        'o' => {
                            if val == "auto" {
                                saw_auto = true;
                                prev = OpStatus::Operand;
                            } else {
                                // add_field_list at OPTION time.
                                real_lists.push(val);
                                prev = OpStatus::OArg;
                            }
                        }
                        'j' if attached && val == "1" => {
                            jcount[0] += 1;
                            prev = OpStatus::J1Arg;
                        }
                        'j' if attached && val == "2" => {
                            jcount[1] += 1;
                            prev = OpStatus::J2Arg;
                        }
                        'j' => {
                            let field = match parse_positive_field(&val) {
                                Ok(f) => f,
                                Err(e) => {
                                    let _ = writeln!(io.stderr, "join: {e}");
                                    return Err(());
                                }
                            };
                            if let Err(e) = apply_field(1, field, cfg, field1_seen, field2_seen)
                                .and_then(|_| {
                                    apply_field(2, field, cfg, field1_seen, field2_seen)
                                })
                            {
                                let _ = writeln!(io.stderr, "join: {e}");
                                return Err(());
                            }
                            prev = OpStatus::Operand;
                        }
                        '1' | '2' => {
                            let field = match parse_positive_field(&val) {
                                Ok(f) => f,
                                Err(e) => {
                                    let _ = writeln!(io.stderr, "join: {e}");
                                    return Err(());
                                }
                            };
                            let which = if c == '1' { 1 } else { 2 };
                            if let Err(e) =
                                apply_field(which, field, cfg, field1_seen, field2_seen)
                            {
                                let _ = writeln!(io.stderr, "join: {e}");
                                return Err(());
                            }
                            prev = OpStatus::Operand;
                        }
                        _ => {
                            // -a/-e/-t/-v values are consumed here for
                            // classification; their semantics stay on the
                            // Parsed result.
                            prev = OpStatus::Operand;
                        }
                    }
                    i += 1;
                }
            }
            continue;
        }
        // Operand: add_file_name (join.c).
        if names.len() == 2 && prev != OpStatus::Operand {
            let idx = if status[0] == OpStatus::Operand { 1 } else { 0 };
            let arg = names[idx].clone();
            match status[idx] {
                OpStatus::Operand => {
                    let _ = usage_error(io, "join", &format!("extra operand '{arg}'"));
                    return Err(());
                }
                OpStatus::J1Arg | OpStatus::J2Arg => {
                    let field = match parse_positive_field(&arg) {
                        Ok(f) => f,
                        Err(e) => {
                            let _ = writeln!(io.stderr, "join: {e}");
                            return Err(());
                        }
                    };
                    let which = if status[idx] == OpStatus::J1Arg {
                        jcount[0] -= 1;
                        1
                    } else {
                        jcount[1] -= 1;
                        2
                    };
                    if let Err(e) = apply_field(which, field, cfg, field1_seen, field2_seen) {
                        let _ = writeln!(io.stderr, "join: {e}");
                        return Err(());
                    }
                }
                OpStatus::OArg => {
                    // add_field_list at ABSORPTION time (join.c call order).
                    real_lists.push(arg);
                }
            }
            if idx == 0 {
                names[0] = names[1].clone();
                status[0] = status[1];
            }
            names.pop();
            status.pop();
        }
        names.push(tok);
        status.push(prev);
        i += 1;
    }

    // [GNU] a "-j1"/"-j2" whose argument never arrived falls back to
    // "-j 1" / "-j 2" for both fields.
    for (i_, count) in jcount.iter().enumerate() {
        if *count != 0 {
            let field = (i_ + 1) as i64;
            if let Err(e) = apply_field(1, field, cfg, field1_seen, field2_seen)
                .and_then(|_| apply_field(2, field, cfg, field1_seen, field2_seen))
            {
                let _ = writeln!(io.stderr, "join: {e}");
                return Err(());
            }
        }
    }

    Ok((real_lists, saw_auto, names, jcount))
}

pub fn join_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("join", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if p.has("help") || p.has("version") {
        return Ok(0);
    }

    let mut cfg = Config {
        field1: 1,
        field2: 1,
        explicit_separator: false,
        whole_line: false,
        ignore_case: p.has("ignore-case") || p.has("i"),
        check_order: false,
        nocheck_order: false,
        header: p.has("header"),
        zero_terminated: p.has("zero-terminated") || p.has("z"),
        separator: ' ',
        empty_field: String::new(),
        output_auto: false,
        output_fields: Vec::new(),
        print_unpaired1: false,
        print_unpaired2: false,
        only_unpaired: false,
        files: Vec::new(),
        last_raw_arg: args
            .last()
            .map(|a| a.to_string_lossy().into_owned())
            .unwrap_or_default(),
    };

    // [GNU] -1/-2/-j apply as ENCOUNTERED (getopt order); set_join_field
    // reports the already-set value first.
    let mut field1_seen = false;
    let mut field2_seen = false;
    let (real_lists, saw_auto, file_names, _jcount) = match scan_order_sensitive(
        args,
        io,
        &mut cfg,
        &mut field1_seen,
        &mut field2_seen,
    ) {
        Ok(x) => x,
        Err(()) => return Ok(1),
    };

    // -t: one byte; '' means '\n' (whole-line), "\0" spelling means NUL.
    let mut tab_seen = false;
    let mut tab_char = '\0';
    for (_, v) in p.values.iter().filter(|(k, _)| k == "t") {
        let newtab: char = if v.is_empty() {
            '\n'
        } else if v == "\\0" {
            '\0'
        } else {
            // [GNU] the tab is ONE BYTE: a multi-byte UTF-8 character is
            // rejected with the same diagnostic (probed)
            let bytes = v.as_bytes();
            if bytes.len() != 1 {
                let _ = writeln!(io.stderr, "join: multi-character tab '{v}'");
                return Ok(1);
            }
            bytes[0] as char
        };
        if tab_seen && tab_char != newtab {
            let _ = writeln!(io.stderr, "join: incompatible tabs");
            return Ok(1);
        }
        tab_seen = true;
        tab_char = newtab;
    }
    if tab_seen {
        cfg.explicit_separator = true;
        cfg.separator = tab_char;
        cfg.whole_line = tab_char == '\n';
    }

    // --check-order/--nocheck-order: one state, the LAST one in argv wins.
    for f in &p.flags {
        match f.as_str() {
            "check-order" => {
                cfg.check_order = true;
                cfg.nocheck_order = false;
            }
            "nocheck-order" => {
                cfg.check_order = false;
                cfg.nocheck_order = true;
            }
            _ => {}
        }
    }

    // [GNU] a second, DIFFERENT -e value is fatal.
    let mut empty_field_seen = false;
    for (_, v) in p.values.iter().filter(|(k, _)| k == "e") {
        if empty_field_seen && *v != cfg.empty_field {
            let _ = writeln!(
                io.stderr,
                "join: conflicting empty-field replacement strings"
            );
            return Ok(1);
        }
        empty_field_seen = true;
        cfg.empty_field = v.clone();
    }

    // [GNU] every -o appends to the output list; a real list beats auto.
    // The list pieces were collected at option/absorption time by
    // scan_order_sensitive.
    if !real_lists.is_empty() {
        let joined = real_lists.join(",");
        match parse_output_format(&joined) {
            Ok(fields) => cfg.output_fields = fields,
            Err(e) => {
                let _ = writeln!(io.stderr, "join: {e}");
                return Ok(1);
            }
        }
    } else if saw_auto {
        cfg.output_auto = true;
    }

    for (k, v) in p.values.iter().filter(|(k, _)| k == "a" || k == "v") {
        let file_number = match parse_file_number(v) {
            Ok(n) => n,
            Err(e) => {
                let _ = writeln!(io.stderr, "join: {e}");
                return Ok(1);
            }
        };
        if k == "v" {
            cfg.only_unpaired = true;
        }
        if file_number == 1 {
            cfg.print_unpaired1 = true;
        } else {
            cfg.print_unpaired2 = true;
        }
    }

    // File operands come from the add_file_name machine (absorption
    // resolved); GNU reports a missing second operand against g_names[0].
    cfg.files = file_names;
    if cfg.files.is_empty() {
        let _ = usage_error(io, "join", "missing operand");
        return Ok(1);
    }
    if cfg.files.len() < 2 {
        let msg = format!("missing operand after '{}'", cfg.files[0]);
        let _ = usage_error(io, "join", &msg);
        return Ok(1);
    }
    if cfg.files.len() > 2 {
        let msg = format!("extra operand '{}'", cfg.files[2]);
        let _ = usage_error(io, "join", &msg);
        return Ok(1);
    }

    let record_delim = if cfg.zero_terminated { '\0' } else { '\n' };

    // [GNU] "both files cannot be standard input".
    if cfg.files[0] == "-" && cfg.files[1] == "-" {
        let _ = writeln!(io.stderr, "join: both files cannot be standard input");
        return Ok(1);
    }

    let mut read_lines = |name: &str| -> Result<Vec<String>, ()> {
        let mut buf = Vec::new();
        if name == "-" {
            if io.stdin.read_to_end(&mut buf).is_err() {
                return Err(());
            }
        } else {
            match std::fs::read(resolve(ctx, name)) {
                Ok(b) => buf = b,
                Err(_) => {
                    if std::fs::metadata(resolve(ctx, name))
                        .map(|m| m.is_dir())
                        .unwrap_or(false)
                    {
                        // [GNU] fopen on a directory succeeds; the failure
                        // surfaces at READ time and is reported WITHOUT the
                        // file name (probed WSL 9.4: "join: read error: Is
                        // a directory" rc 1 -- the read_error() path, not
                        // the fopen strerror path that carries quotef()).
                        let _ = writeln!(io.stderr, "join: read error: Is a directory");
                    } else {
                        let _ = writeln!(io.stderr, "join: {name}: No such file or directory");
                    }
                    return Err(());
                }
            }
        }
        let text = String::from_utf8_lossy(&buf);
        let mut lines = Vec::new();
        let mut start = 0usize;
        for (i, b) in text.bytes().enumerate() {
            if b == record_delim as u8 {
                lines.push(text[start..i].to_string());
                start = i + 1;
            }
        }
        if start < text.len() {
            lines.push(text[start..].to_string());
        }
        Ok(lines)
    };

    let lines1 = match read_lines(&cfg.files[0]) {
        Ok(l) => l,
        Err(_) => return Ok(1),
    };
    let lines2 = match read_lines(&cfg.files[1]) {
        Ok(l) => l,
        Err(_) => return Ok(1),
    };

    let parsed1: Vec<ParsedLine> = lines1
        .iter()
        .map(|l| parse_line(l, &cfg, cfg.field1))
        .collect();
    let parsed2: Vec<ParsedLine> = lines2
        .iter()
        .map(|l| parse_line(l, &cfg, cfg.field2))
        .collect();

    let data_start1 = usize::from(cfg.header && !parsed1.is_empty());
    let data_start2 = usize::from(cfg.header && !parsed2.is_empty());

    let output_fields: Vec<OutputField> = if cfg.output_auto {
        build_auto_output_fields(parsed1.first(), parsed2.first(), &cfg)
    } else {
        cfg.output_fields.clone()
    };

    // [GNU] the header pair is printed when either file has a first line.
    if cfg.header && (!parsed1.is_empty() || !parsed2.is_empty()) {
        let line = build_output_line(parsed1.first(), parsed2.first(), &cfg, &output_fields);
        let _ = io.stdout.write_all(line.as_bytes());
        let _ = write_record_delim(io, record_delim);
    }

    let mut order = OrderState {
        seen_unpairable: false,
        issued: [false; 2],
        disorder: false,
        next_check: [data_start1, data_start2],
    };
    {
        let cfg_ref = &cfg;
        let mut check = |state: &mut OrderState,
                         which: usize,
                         idx: usize,
                         start: usize,
                         parsed: &[ParsedLine],
                         raw: &[String],
                         fname: &str,
                         io: &mut Io|
         -> bool {
            check_line_order(state, which, idx, start, parsed, raw, fname, cfg_ref, io)
        };

        let n1 = parsed1.len();
        let n2 = parsed2.len();
        let mut i1 = data_start1;
        let mut i2 = data_start2;

        while i1 < n1 && i2 < n2 {
            if !check(
                &mut order,
                1,
                i1,
                data_start1,
                &parsed1,
                &lines1,
                &cfg.files[0],
                io,
            ) {
                return Ok(1);
            }
            if !check(
                &mut order,
                2,
                i2,
                data_start2,
                &parsed2,
                &lines2,
                &cfg.files[1],
                io,
            ) {
                return Ok(1);
            }

            let diff = key_cmp(parsed1[i1].key.as_ref(), parsed2[i2].key.as_ref());
            use std::cmp::Ordering::*;
            if diff == Less {
                // (None, None) never happens here: advance both sides once
                if parsed1[i1].key.is_none() && parsed2[i2].key.is_none() {
                    i1 += 1;
                    i2 += 1;
                    continue;
                }
                if cfg.print_unpaired1 {
                    let line = build_output_line(Some(&parsed1[i1]), None, &cfg, &output_fields);
                    let _ = io.stdout.write_all(line.as_bytes());
                    let _ = write_record_delim(io, record_delim);
                }
                i1 += 1;
                if i1 < n1
                    && !check(
                        &mut order,
                        1,
                        i1,
                        data_start1,
                        &parsed1,
                        &lines1,
                        &cfg.files[0],
                        io,
                    )
                {
                    return Ok(1);
                }
                order.seen_unpairable = true;
                continue;
            }
            if diff == Greater {
                if parsed1[i1].key.is_none() && parsed2[i2].key.is_none() {
                    i1 += 1;
                    i2 += 1;
                    continue;
                }
                if cfg.print_unpaired2 {
                    let line = build_output_line(None, Some(&parsed2[i2]), &cfg, &output_fields);
                    let _ = io.stdout.write_all(line.as_bytes());
                    let _ = write_record_delim(io, record_delim);
                }
                i2 += 1;
                if i2 < n2
                    && !check(
                        &mut order,
                        2,
                        i2,
                        data_start2,
                        &parsed2,
                        &lines2,
                        &cfg.files[1],
                        io,
                    )
                {
                    return Ok(1);
                }
                order.seen_unpairable = true;
                continue;
            }
            // Equal keys: collect the block of equal keys from each file.
            let key = parsed1[i1].key.clone();
            let mut e1 = i1 + 1;
            while e1 < n1 {
                if !check(
                    &mut order,
                    1,
                    e1,
                    data_start1,
                    &parsed1,
                    &lines1,
                    &cfg.files[0],
                    io,
                ) {
                    return Ok(1);
                }
                if parsed1[e1].key != key {
                    break;
                }
                e1 += 1;
            }
            let mut e2 = i2 + 1;
            while e2 < n2 {
                if !check(
                    &mut order,
                    2,
                    e2,
                    data_start2,
                    &parsed2,
                    &lines2,
                    &cfg.files[1],
                    io,
                ) {
                    return Ok(1);
                }
                if parsed2[e2].key != key {
                    break;
                }
                e2 += 1;
            }
            if !cfg.only_unpaired {
                for a in i1..e1 {
                    for b in i2..e2 {
                        let line = build_output_line(
                            Some(&parsed1[a]),
                            Some(&parsed2[b]),
                            &cfg,
                            &output_fields,
                        );
                        let _ = io.stdout.write_all(line.as_bytes());
                        let _ = write_record_delim(io, record_delim);
                    }
                }
            }
            i1 = e1;
            i2 = e2;
        }

        // [GNU] Drain the tail: print unpairable lines when requested and
        // keep checking input order (tail lines do not set seen_unpairable).
        if (cfg.print_unpaired1 || !cfg.nocheck_order) && i1 < n1 {
            if cfg.print_unpaired1 {
                let line = build_output_line(Some(&parsed1[i1]), None, &cfg, &output_fields);
                let _ = io.stdout.write_all(line.as_bytes());
                let _ = write_record_delim(io, record_delim);
            }
            i1 += 1;
            while i1 < n1 {
                if !check(
                    &mut order,
                    1,
                    i1,
                    data_start1,
                    &parsed1,
                    &lines1,
                    &cfg.files[0],
                    io,
                ) {
                    return Ok(1);
                }
                if cfg.print_unpaired1 {
                    let line = build_output_line(Some(&parsed1[i1]), None, &cfg, &output_fields);
                    let _ = io.stdout.write_all(line.as_bytes());
                    let _ = write_record_delim(io, record_delim);
                }
                if order.issued[0] && !cfg.print_unpaired1 {
                    break;
                }
                i1 += 1;
            }
        }
        if (cfg.print_unpaired2 || !cfg.nocheck_order) && i2 < n2 {
            if cfg.print_unpaired2 {
                let line = build_output_line(None, Some(&parsed2[i2]), &cfg, &output_fields);
                let _ = io.stdout.write_all(line.as_bytes());
                let _ = write_record_delim(io, record_delim);
            }
            i2 += 1;
            while i2 < n2 {
                if !check(
                    &mut order,
                    2,
                    i2,
                    data_start2,
                    &parsed2,
                    &lines2,
                    &cfg.files[1],
                    io,
                ) {
                    return Ok(1);
                }
                if cfg.print_unpaired2 {
                    let line = build_output_line(None, Some(&parsed2[i2]), &cfg, &output_fields);
                    let _ = io.stdout.write_all(line.as_bytes());
                    let _ = write_record_delim(io, record_delim);
                }
                if order.issued[1] && !cfg.print_unpaired2 {
                    break;
                }
                i2 += 1;
            }
        }
    }

    if order.disorder {
        // [GNU] "join: input is not in sorted order"
        let _ = writeln!(io.stderr, "join: input is not in sorted order");
        return Ok(1);
    }
    Ok(0)
}

fn write_record_delim(io: &mut Io, delim: char) -> std::io::Result<()> {
    let mut buf = [0u8; 4];
    io.stdout.write_all(delim.encode_utf8(&mut buf).as_bytes())
}

#[cfg(test)]
mod tests {
    use std::ffi::OsString;
    use std::sync::atomic::{AtomicU64, Ordering};

    static SEQ: AtomicU64 = AtomicU64::new(0);

    fn scratch(tag: &str) -> winux_core::test_support::ScratchCtx {
        let n = SEQ.fetch_add(1, Ordering::SeqCst);
        winux_core::test_support::ScratchCtx::new(&format!("{}-{}", tag, n))
    }
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run_files(setup: &[(&str, &str)], args: &[&str], stdin: &str) -> (u32, String, String) {
        let sc = scratch("join");
        for (name, content) in setup {
            std::fs::write(sc.path(name), content).unwrap();
        }
        let mut io = MemIo::new(stdin);
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::join_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    fn run(args: &[&str], stdin: &str) -> (u32, String, String) {
        run_files(&[], args, stdin)
    }

    const F1: &str = "f1";
    const F1C: &str = "a 1\nb 2\n";
    const F2: &str = "f2";
    const F2C: &str = "a X\nb Y\n";

    #[test]
    fn basic_and_output_formats() {
        let (r, out, _) = run_files(&[(F1, F1C), (F2, F2C)], &[F1, F2], "");
        assert_eq!((r, out.as_str()), (0, "a 1 X\nb 2 Y\n"));
        let (r, out, _) = run_files(&[(F1, F1C), (F2, F2C)], &["-o", "1.2 2.2", F1, F2], "");
        assert_eq!((r, out.as_str()), (0, "1 X\n2 Y\n"));
        let (r, out, _) = run_files(&[(F1, F1C), (F2, F2C)], &["-o", "0,1.2", F1, F2], "");
        assert_eq!((r, out.as_str()), (0, "a 1\nb 2\n"));
        let (r, out, _) = run_files(&[(F1, F1C), (F2, F2C)], &["-o", "auto", F1, F2], "");
        assert_eq!((r, out.as_str()), (0, "a 1 X\nb 2 Y\n"));
        let (r, out, _) = run_files(&[(F1, F1C), (F2, F2C)], &["-o", "0 0", F1, F2], "");
        assert_eq!((r, out.as_str()), (0, "a a\nb b\n"));
        let (r, out, _) = run_files(&[(F1, F1C), (F2, F2C)], &["-o", "1.1 2.1", F1, F2], "");
        assert_eq!((r, out.as_str()), (0, "a a\nb b\n"));
        // field beyond the line is empty (or -e)
        let (r, out, _) = run_files(&[(F1, F1C), (F2, F2C)], &["-o", "2.9", F1, F2], "");
        assert_eq!(out, "\n\n");
        let _ = r;
    }

    #[test]
    fn separators() {
        let c1 = "c1";
        let (r, out, _) = run_files(
            &[(c1, "a,1\nb,2\n"), (F2, "a,X\nb,Y\n")],
            &["-t", ",", c1, F2],
            "",
        );
        assert_eq!((r, out.as_str()), (0, "a,1,X\nb,2,Y\n"));
        // -t '' makes whole lines the fields: matching lines join with '\n'
        let (r, out, _) = run_files(
            &[(F1, "a 1\nb 2\n"), (F2, "a 1\nb 2\n")],
            &["-t", "", F1, F2],
            "",
        );
        assert_eq!((r, out.as_str()), (0, "a 1\nb 2\n"));
        // NUL separator via the "\0" spelling: records still end at
        // newline, fields at the NUL byte
        let (r, out, _) = run_files(
            &[(F1, "k 1\0x\n"), (F2, "k 1\0y\n")],
            &["-t", "\\0", F1, F2],
            "",
        );
        assert_eq!((r, out.as_str()), (0, "k 1\0x\0y\n"));
        // multi-character and multi-byte tabs are rejected
        let (r, _, err) = run(&["-t", "ab", F1, F2], "");
        assert_eq!(r, 1);
        assert!(err.contains("multi-character tab 'ab'"));
        let (r, _, err) = run(&["-t", "中", F1, F2], "");
        assert_eq!(r, 1);
        assert!(err.contains("multi-character tab"));
        // a different second -t is fatal
        let (r, _, err) = run(&["-t", ",", "-t", ":", F1, F2], "");
        assert_eq!(r, 1);
        assert!(err.contains("incompatible tabs"));
    }

    #[test]
    fn field_options() {
        let g1 = "g1";
        let (r, out, _) = run_files(
            &[(g1, "a 1\n"), (F2, "1 x\n")],
            &["-1", "2", "-2", "1", g1, F2],
            "",
        );
        assert_eq!((r, out.as_str()), (0, "1 a x\n"));
        let (r, out, _) = run_files(&[(g1, "a 1\n"), (F2, "x a\n")], &["-j", "2", g1, F2], "");
        assert_eq!(out, "");
        // -j then conflicting -1 reports the zero-based pair in GNU order
        let (r, _, err) = run(&["-j", "3", "-1", "2", F1, F2], "");
        assert_eq!(r, 1);
        assert!(err.contains("incompatible join fields 2, 1"));
        // -j 1 -1 1 is compatible
        let (r, out, _) = run_files(&[(F1, F1C), (F2, F2C)], &["-j", "1", "-1", "1", F1, F2], "");
        assert_eq!(r, 0);
        assert_eq!(out, "a 1 X\nb 2 Y\n");
        // xstrtol shapes: 01 is 1, huge clamps to keyless (never pairs)
        let (r, out, _) = run_files(&[(F1, F1C), (F2, F2C)], &["-j", "01", F1, F2], "");
        assert_eq!((r, out.as_str()), (0, "a 1 X\nb 2 Y\n"));
        let (r, out, _) = run_files(
            &[(F1, F1C), (F2, F2C)],
            &["-1", "99999999999999999999", F1, F2],
            "",
        );
        assert_eq!((r, out.as_str()), (0, ""));
        // zero or negative field numbers are errors
        let (r, _, err) = run(&["-1", "0", F1, F2], "");
        assert_eq!(r, 1);
        assert!(err.contains("invalid field number: '0'"));
        let (r, _, err) = run(&["-1", "x", F1, F2], "");
        assert_eq!(r, 1);
        assert!(err.contains("invalid field number: 'x'"));
    }

    #[test]
    fn unpaired_modes() {
        let u1 = "u1";
        let u1c = "a 1\nc 3\n";
        let (r, out, _) = run_files(&[(u1, u1c), (F2, F2C)], &["-a", "1", u1, F2], "");
        assert_eq!((r, out.as_str()), (0, "a 1 X\nc 3\n"));
        let (r, out, _) = run_files(&[(u1, u1c), (F2, F2C)], &["-a", "1", "-a", "2", u1, F2], "");
        assert_eq!((r, out.as_str()), (0, "a 1 X\nb Y\nc 3\n"));
        let (r, out, _) = run_files(&[(u1, u1c), (F2, F2C)], &["-v", "1", u1, F2], "");
        assert_eq!((r, out.as_str()), (0, "c 3\n"));
        let (r, out, _) = run_files(&[(u1, u1c), (F2, F2C)], &["-v", "2", u1, F2], "");
        assert_eq!((r, out.as_str()), (0, "b Y\n"));
        let (r, out, _) = run_files(&[(u1, u1c), (F2, F2C)], &["-v", "1", "-a", "2", u1, F2], "");
        assert_eq!((r, out.as_str()), (0, "b Y\nc 3\n"));
        // -a 3 / -v 0 are invalid file numbers
        let (r, _, err) = run(&["-a", "3", F1, F2], "");
        assert_eq!(r, 1);
        assert!(err.contains("invalid field number: '3'"));
        let (r, _, err) = run(&["-v", "0", F1, F2], "");
        assert_eq!(r, 1);
        assert!(err.contains("invalid field number: '0'"));
    }

    #[test]
    fn empty_field_replacement() {
        let e1 = "e1";
        let (r, out, _) = run_files(
            &[(e1, "a\nc\n"), (F2, F2C)],
            &["-a", "1", "-e", "EMP", "-o", "0 1.2 2.2", e1, F2],
            "",
        );
        assert_eq!((r, out.as_str()), (0, "a EMP X\nc EMP EMP\n"));
        // without -o, missing fields contribute nothing
        let (r, out, _) = run_files(
            &[(e1, "a\nc\n"), (F2, F2C)],
            &["-a", "1", "-e", "EMP", e1, F2],
            "",
        );
        assert_eq!((r, out.as_str()), (0, "a X\nc\n"));
        // -o auto + -a: unpaired lines pad to the auto list
        let (r, out, _) = run_files(
            &[("u2", "a 1\nc 3\n"), (F2, F2C)],
            &["-a", "1", "-e", "EMP", "-o", "auto", "u2", F2],
            "",
        );
        assert_eq!((r, out.as_str()), (0, "a 1 X\nc 3 EMP\n"));
        // conflicting -e values are fatal
        let (r, _, err) = run(&["-e", "a", "-e", "b", "-o", "0", F1, F2], "");
        assert_eq!(r, 1);
        assert!(err.contains("conflicting empty-field replacement strings"));
    }

    #[test]
    fn order_checking() {
        let s1 = "s1";
        let s1c = "b 2\na 1\n";
        // default: warn (stderr) and end "input is not in sorted order" rc 1
        let (r, out, err) = run_files(&[(s1, s1c), (F2, F2C)], &[s1, F2], "");
        assert_eq!(r, 1);
        assert_eq!(out, "b 2 Y\n");
        assert!(err.contains("join: s1:2: is not sorted: a 1"));
        assert!(err.contains("join: input is not in sorted order"));
        // --check-order: fatal immediately, no further output
        let (r, out, err) = run_files(&[(s1, s1c), (F2, F2C)], &["--check-order", s1, F2], "");
        assert_eq!(r, 1);
        assert_eq!(out, "");
        assert!(err.contains("is not sorted: a 1"));
        assert!(!err.contains("input is not in sorted order"));
        // --nocheck-order never checks (last wins even after --check-order)
        let (r, out, err) = run_files(
            &[(s1, s1c), (F2, F2C)],
            &["--check-order", "--nocheck-order", s1, F2],
            "",
        );
        assert_eq!(r, 0);
        assert_eq!(out, "b 2 Y\n");
        assert!(err.is_empty());
        // --nocheck-order + -a1 still prints the unpaired tail
        let (r, out, _) = run_files(
            &[(s1, s1c), (F2, F2C)],
            &["--nocheck-order", "-a", "1", s1, F2],
            "",
        );
        assert_eq!(r, 0);
        assert_eq!(out, "b 2 Y\na 1\n");
        // default + -a1: warning then final message, rc 1
        let (r, out, err) = run_files(&[(s1, s1c), (F2, F2C)], &["-a", "1", s1, F2], "");
        assert_eq!(r, 1);
        assert_eq!(out, "b 2 Y\na 1\n");
        assert!(err.contains("input is not in sorted order"));
        // a disorder discovered in the tail still triggers the fatal path
        let s2 = "s2";
        let (r, out, err) = run_files(
            &[(s2, "a 1\nc 3\nb 2\n"), (F2, F2C)],
            &["--check-order", s2, F2],
            "",
        );
        assert_eq!(r, 1);
        assert_eq!(out, "a 1 X\n");
        assert!(err.contains("join: s2:3: is not sorted: b 2"));
    }

    #[test]
    fn header_and_zero_terminated() {
        let (r, out, _) = run_files(&[(F1, F1C), (F2, F2C)], &["--header", F1, F2], "");
        assert_eq!((r, out.as_str()), (0, "a 1 X\nb 2 Y\n"));
        // one empty side contributes nothing but the header still prints
        let emp = "emp";
        let (r, out, _) = run_files(&[(F1, F1C), (emp, "")], &["--header", F1, emp], "");
        assert_eq!((r, out.as_str()), (0, "a 1\n"));
        let (r, out, _) = run_files(
            &[(F1, "h1 h\0a 1\0"), (F2, "h2 h\0a X\0")],
            &["-z", "--header", F1, F2],
            "",
        );
        assert_eq!((r, out.as_str()), (0, "h1 h h\0a 1 X\0"));
    }

    #[test]
    fn operand_errors_and_stdin() {
        let (r, _, err) = run(&[F1], "");
        assert_eq!(r, 1);
        assert!(err.contains("join: missing operand after 'f1'"));
        assert!(err.contains("Try 'join --help' for more information."));
        let (r, _, err) = run(&[], "");
        assert_eq!(r, 1);
        assert!(err.contains("join: missing operand"));
        let (r, _, err) = run(&[F1, F2, F1], "");
        assert_eq!(r, 1);
        assert!(err.contains("join: extra operand 'f1'"));
        let (r, _, err) = run(&["-", "-"], "a 1\n");
        assert_eq!(r, 1);
        assert!(err.contains("join: both files cannot be standard input"));
        let (r, _, err) = run(&["join-nosuch-file", F2], "");
        assert_eq!(r, 1);
        assert!(err.contains("join: join-nosuch-file: No such file or directory"));
        // dash reads stdin on one side
        let (r, out, _) = run_files(&[(F2, F2C)], &["-", F2], "a 1\nb 2\n");
        assert_eq!((r, out.as_str()), (0, "a 1 X\nb 2 Y\n"));
    }

    #[test]
    fn keyless_lines_and_ignore_case() {
        let b1 = "b1";
        let b2 = "b2";
        // a line whose join field is missing never pairs, even keyless-vs-keyless
        let (r, out, _) = run_files(&[(b1, "a\n"), (b2, "a\n")], &["-1", "3", b1, b2], "");
        assert_eq!((r, out.as_str()), (0, ""));
        // but it prints as an unpaired line with an empty key
        let (r, out, _) = run_files(
            &[(b1, "a\n"), (b2, "a X Y\n")],
            &["-1", "3", "-a", "1", "-a", "2", b1, b2],
            "",
        );
        assert_eq!((r, out.as_str()), (0, " a\na X Y\n"));
        // -i folds key case
        let i1 = "i1";
        let i2 = "i2";
        let (r, out, _) = run_files(&[(i1, "A 1\n"), (i2, "a X\n")], &["-i", i1, i2], "");
        assert_eq!((r, out.as_str()), (0, "A 1 X\n"));
    }

    #[test]
    fn duplicate_keys_cross_product() {
        let d1 = "d1";
        let d2 = "d2";
        let (r, out, _) = run_files(
            &[(d1, "a 1\na 2\n"), (d2, "a X\na Y\n")],
            &["-o", "0 1.2 2.2", d1, d2],
            "",
        );
        assert_eq!((r, out.as_str()), (0, "a 1 X\na 1 Y\na 2 X\na 2 Y\n"));
    }

    #[test]
    fn output_format_errors() {
        for (spec, msg) in [
            ("0.1", "invalid field specifier: '0.1'"),
            ("1x", "invalid field specifier: '1x'"),
            ("", "invalid file number in field spec: ''"),
            ("0,", "invalid file number in field spec: ''"),
            ("1.2.3", "invalid field number: '2.3'"),
            ("1.0", "invalid field number: '0'"),
        ] {
            let (r, _, err) = run(&["-o", spec, F1, F2], "");
            assert_eq!(r, 1, "rc for -o {spec:?}");
            assert!(err.contains(msg), "msg for -o {spec:?}: {err}");
        }
        // space-separated lists are fine
        let (r, out, _) = run_files(&[(F1, F1C), (F2, F2C)], &["-o", "1.1 2.1", F1, F2], "");
        assert_eq!(r, 0);
        assert_eq!(out, "a a\nb b\n");
    }

    // [GNU] join.c operand_status/add_file_name absorption, probed on WSL
    // coreutils 9.4 (wave3b wiring matrix, #1488): a third file name
    // arriving while -o/-j1/-j2 still owe an argument becomes that
    // argument instead of an extra operand.
    #[test]
    fn obsolete_o_argument_absorption() {
        // -o 1.2 2.2 f1 f2: "2.2" joins the list, files f1 f2.
        let (r, out, _) = run_files(&[(F1, F1C), (F2, F2C)], &["-o", "1.2", "2.2", F1, F2], "");
        assert_eq!(r, 0);
        assert_eq!(out, "1 X\n2 Y\n");

        // chained absorption: "abc" decodes as a spec and dies.
        let (r, _, err) = run_files(
            &[(F1, F1C), (F2, F2C)],
            &["-o", "1.2", "2.2", "abc", F1, F2],
            "",
        );
        assert_eq!(r, 1);
        assert!(err.contains("invalid file number in field spec: 'abc'"), "{err}");

        // operand before -o: the THIRD name resolves as the -o argument.
        let (r, _, err) = run_files(&[(F1, F1C), (F2, F2C)], &["X", "-o", "1.2", F1, F2], "");
        assert_eq!(r, 1);
        assert!(err.contains("invalid file number in field spec: 'f1'"), "{err}");

        // repeated -o still works (option-time adds, no absorption).
        let (r, out, _) = run_files(
            &[(F1, F1C), (F2, F2C)],
            &["-o", "1.2", "-o", "2.2", F1, F2],
            "",
        );
        assert_eq!(r, 0);
        assert_eq!(out, "1 X\n2 Y\n");

        // plain extra operand is unchanged.
        let (r, _, err) = run_files(&[(F1, F1C), (F2, F2C)], &[F1, F2, "f3"], "");
        assert_eq!(r, 1);
        assert!(err.contains("extra operand 'f3'"), "{err}");

        // -o auto keeps MUST_BE_OPERAND status: no absorption.
        let (r, _, err) = run_files(
            &[(F1, F1C), (F2, F2C)],
            &["-o", "auto", "x", F1, F2],
            "",
        );
        assert_eq!(r, 1);
        assert!(err.contains("extra operand 'f2'"), "{err}");
    }

    #[test]
    fn obsolete_j1_j2_argument_absorption() {
        // -j1 2: field 1 of file 1 is 2; f1.field2 (1/2) never matches
        // f2.field1 (a/b) -> empty output rc 0.
        let (r, out, _) = run_files(&[(F1, F1C), (F2, F2C)], &["-j1", "2", F1, F2], "");
        assert_eq!(r, 0);
        assert_eq!(out, "");

        // -j2 1: field 2 of file 2 is 1 -> ordinary field-1 join.
        let (r, out, _) = run_files(&[(F1, F1C), (F2, F2C)], &["-j2", "1", F1, F2], "");
        assert_eq!(r, 0);
        assert_eq!(out, "a 1 X\nb 2 Y\n");

        // -j1 without a separate argument falls back to -j 1.
        let (r, out, _) = run_files(&[(F1, F1C), (F2, F2C)], &["-j1", F1, F2], "");
        assert_eq!(r, 0);
        assert_eq!(out, "a 1 X\nb 2 Y\n");
    }
}
