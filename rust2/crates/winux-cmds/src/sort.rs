//! `sort` — GNU coreutils semantics, following src/sort.c (coreutils 9.4,
//! vendored spec copy at rust2/target/sort-spec/sort.c) plus the gnulib
//! helpers it delegates to: strnumcmp-in.h (-n), filevercmp.c (-V). Every
//! behavior cited below was probed byte-for-byte against WSL coreutils 9.4
//! (rust2/target/sort-probe/p1..p5.sh) and is covered by the unit tests and
//! the gate cases (gate/cases/sort.tsv).
//!
//! Pipeline (LC_ALL=C shape; no locale tables are consulted):
//! * Inputs are concatenated in argument order ("-"/no operand = stdin) and
//!   split in memory (the v1 whole-buffer ABI; the in-memory sort replaces
//!   GNU's temp-file spill without observable output difference). A file
//!   whose last line lacks the eol gets one supplied at its EOF, exactly
//!   like sort.c fillbuf — output therefore always ends with the eol
//!   (probed p5: "x" -> "x\n"; two files concatenate with the fixup).
//! * One key list per run. Keys come from -k F[.C][OPTS][,F[.C][OPTS]]
//!   (parse_keyspec port) or the obsolescent +POS1 [-POS2] operands
//!   (traditional usage is the default, sort.c main case 1). Global
//!   ordering options (bdfghinMrRV and --sort=WORD) accumulate into gkey
//!   and are inherited by keys that carry NO ordering flags themselves and
//!   are not -r (sort.c inheritance loop); with no keys at all the gkey
//!   becomes the single whole-line key. Global -r additionally reverses
//!   only the last-resort comparison (reverse = gkey.reverse).
//! * keycompare: per key the span is computed with the literal begfield /
//!   limfield port (default separator = blank-to-blank transition, the
//!   leading separator included in the field; -t SEP = byte separator, the
//!   separator itself excluded; sword/schar/eword/echar zero-origin with
//!   the sort.c SIZE_MAX sentinels). Comparison dispatch per key:
//!   numeric -> general_numeric -> human_numeric -> month -> random ->
//!   version -> (dictionary/nonprinting ignore) -> fold translate ->
//!   plain byte compare with original-length tiebreak. numeric /
//!   general_numeric / human_numeric / month keys with a second ordering
//!   flag are rejected up front by check_ordering_compatibility (probed
//!   p4: "-nd" -> options '-dn' are incompatible, rc 2; "-k1n -k2M" is
//!   fine — the check is per key), but version/random may pair with
//!   ignore letters, which then filter the key before hashing/comparing,
//!   exactly like the sort.c copy-and-dispatch branch.
//! * -n is the gnulib strnumcmp byte-wise algorithm (no conversion):
//!   only '-' is a sign ("+5" compares as a non-number), leading blanks
//!   are skipped, zero runs are skipped on both sides, digit-run length
//!   decides magnitude, fraccompare handles decimal fractions.
//!   -g parses an strtold-shaped prefix (decimal, hex float, inf, nan
//!   with payload); conversion errors sort first, NaNs sort before
//!   numbers (-nan < nan), mirroring general_numcompare. -g uses f64,
//!   not x87 long double (documented deviation; same ordering for
//!   ordinary magnitudes). -h = find_unit_order (suffix order
//!   K<M<G<T<P<E<Z<Y<R<Q) then strnumcmp on ties (probed p2: "2000"
//!   sorts before "1K"; suffix order dominates the byte value).
//!   -M matches the 3-letter English month names case-insensitively as
//!   a prefix (JANX -> JAN); unknown keys are all 0 and then fall to the
//!   last-resort comparison. -R hashes the (filtered) key with FNV-1a-64
//!   and compares hashes byte-wise; GNU shuffles per run with an MD5 of
//!   a random seed, so the OUTPUT ORDER differs from GNU by design while
//!   identical keys stay grouped — documented deviation (gate win rows).
//!   -V is the filevercmp.c filenvercmp port (Debian verrevcmp, dot-file
//!   specials, suffix cut + second pass).
//! * compare(): keys first; if equal (or no keys) the whole lines
//!   (without the eol) compare byte-wise unless -s or -u, and that
//!   last resort is reversed by global -r only. The whole run is a
//!   stable sort (GNU's chunk sort + merge is stable too), so with -s
//!   equal keys keep input order and with -u the first line of every
//!   equal-key run survives (probed p2: "-u -f" keeps the first folded
//!   duplicate; last resort decides which line leads a -u run without
//!   -s, e.g. "sort -u -k1,1" on "a x/a y" keeps "a y").
//! * -m merges pre-sorted inputs, stable across files (earlier file
//!   wins ties), with the same -u first-of-run rule (mergefps port).
//! * -c/-C check a single input: disorder (or duplicate keys with -u)
//!   exits 1 (SORT_OUT_OF_ORDER); -c prints `sort: FILE:LINE: disorder:
//!   LINE` for the first offender with the eol always appended. -s/-r
//!   and keys participate exactly as in compare() (probed p5). -c runs
//!   before check_inputs/-m: `-c -m` checks, `-c missing` reports
//!   "open failed" (probed p5), a directory reports "read failed".
//! * -o writes after all reads (inputs read first, so -o FILE FILE is
//!   safe like GNU's temp-copy path); -o must not repeat with different
//!   names; -c/-C reject a second operand and -o. Diagnostics use GNU
//!   wording: "cannot read: X: ...", "open failed: X: ...", "read
//!   failed: X: ...", "write failed" (rc 2 for the SORT_FAILURE paths).
//! * Accepted-and-ignored (documented, no observable effect of the
//!   in-memory pipeline): -S/--buffer-size (grammar validated), -T
//!   --temporary-directory, --parallel (0 rejected like GNU),
//!   --batch-size, --compress-program, --random-source (opened when a
//!   random key is in effect, like GNU, but the seed bytes do not
//!   influence our fixed FNV hash), -y (Solaris quirk), --debug
//!   (incompatible with -c/-o like GNU; no key annotation output).
//!   Not implemented: --files0-from (the long option is intentionally
//!   absent from the table, like the 1.x applet's --files0-from gap
//!   class), POSIX2_VERSION / POSIXLY_CORRECT parsing variants.
//!   -g f64 vs long double precision and the -R hash scheme are the
//!   only ordering-visible deviations.

use std::ffi::OsString;
use std::io::{Read, Write};

use crate::argopt;
use crate::fsutil;
use crate::gnuhelp;
use winux_core::{CmdResult, Ctx, Io};

// ---------------------------------------------------------------------
// ordering option model (sort.c struct keyfield)
// ---------------------------------------------------------------------

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Ignore {
    None,
    /// -i: ignore non-printing characters (!isprint, C locale)
    Nonprinting,
    /// -d: keep only blanks and alphanumerics; beats -i regardless of order
    Nondictionary,
}

#[derive(Clone, Debug)]
struct KeyField {
    /// zero-origin field of key start; usize::MAX = line start (sort.c sentinel)
    sword: usize,
    /// zero-origin char offset within the start field
    schar: usize,
    /// zero-origin field of key end; usize::MAX = end of line
    eword: usize,
    /// zero-origin end char offset (past-the-end style; 0 = whole end field)
    echar: usize,
    skipsblanks: bool,
    skipeblanks: bool,
    numeric: bool,
    general_numeric: bool,
    human_numeric: bool,
    month: bool,
    random: bool,
    version: bool,
    ignore: Ignore,
    translate: bool,
    reverse: bool,
}

impl KeyField {
    /// sort.c key_init: zeroed, eword = SIZE_MAX.
    fn new() -> Self {
        KeyField {
            sword: 0,
            schar: 0,
            eword: usize::MAX,
            echar: 0,
            skipsblanks: false,
            skipeblanks: false,
            numeric: false,
            general_numeric: false,
            human_numeric: false,
            month: false,
            random: false,
            version: false,
            ignore: Ignore::None,
            translate: false,
            reverse: false,
        }
    }

    /// sort.c default_key_compare: a key with no ordering flag at all
    /// (reverse deliberately excluded).
    fn default_compare(&self) -> bool {
        self.ignore == Ignore::None
            && !self.translate
            && !self.skipsblanks
            && !self.skipeblanks
            && !self.numeric
            && !self.general_numeric
            && !self.human_numeric
            && !self.month
            && !self.version
            && !self.random
    }
}

/// sort.c set_ordering: apply one ordering letter; blank_start/blank_end
/// select which halves bl_start/bl_end/bl_both map to.
fn apply_ordering(key: &mut KeyField, c: char, blank_start: bool, blank_end: bool) -> bool {
    match c {
        'b' => {
            if blank_start {
                key.skipsblanks = true;
            }
            if blank_end {
                key.skipeblanks = true;
            }
        }
        'd' => key.ignore = Ignore::Nondictionary,
        'f' => key.translate = true,
        'g' => key.general_numeric = true,
        'h' => key.human_numeric = true,
        // -d implies -i but -i never overrides -d (order-independent)
        'i' => {
            if key.ignore == Ignore::None {
                key.ignore = Ignore::Nonprinting;
            }
        }
        'M' => key.month = true,
        'n' => key.numeric = true,
        'R' => key.random = true,
        'r' => key.reverse = true,
        'V' => key.version = true,
        _ => return false,
    }
    true
}

// ---------------------------------------------------------------------
// run configuration
// ---------------------------------------------------------------------

#[derive(Debug)]
struct Cfg {
    keys: Vec<KeyField>,
    /// gkey.reverse: reverses only the last-resort comparison
    reverse: bool,
    unique: bool,
    stable: bool,
    mergeonly: bool,
    debug: bool,
    /// 'c' diagnose-first / 'C' quiet
    checkonly: Option<char>,
    /// byte separator; None = blank transition (tab == TAB_DEFAULT)
    tab: Option<u8>,
    /// '\n' or 0 (-z)
    eol: u8,
    outfile: Option<String>,
    random_source: Option<String>,
    compress_program: Option<String>,
    files: Vec<String>,
}

impl Cfg {
    fn new() -> Self {
        Cfg {
            keys: Vec::new(),
            reverse: false,
            unique: false,
            stable: false,
            mergeonly: false,
            debug: false,
            checkonly: None,
            tab: None,
            eol: b'\n',
            outfile: None,
            random_source: None,
            compress_program: None,
            files: Vec::new(),
        }
    }
}

// ---------------------------------------------------------------------
// option tables
// ---------------------------------------------------------------------

/// Long options in registration order (the ambiguity-list order); values
/// mark required-argument options. --check is optional-argument and
/// handled specially (bare --check never consumes the next argv element).
/// --files0-from is intentionally absent: not implemented here, so it
/// reports GNU's unrecognized-option usage error rather than pretending.
const LONG_OPTS: &[(&str, bool)] = &[
    ("ignore-leading-blanks", false),
    ("check", false),
    ("compress-program", true),
    ("debug", false),
    ("dictionary-order", false),
    ("ignore-case", false),
    ("general-numeric-sort", false),
    ("ignore-nonprinting", false),
    ("key", true),
    ("merge", false),
    ("month-sort", false),
    ("numeric-sort", false),
    ("human-numeric-sort", false),
    ("version-sort", false),
    ("random-sort", false),
    ("random-source", true),
    ("sort", true),
    ("output", true),
    ("reverse", false),
    ("stable", false),
    ("batch-size", true),
    ("buffer-size", true),
    ("field-separator", true),
    ("temporary-directory", true),
    ("unique", false),
    ("zero-terminated", false),
    ("parallel", true),
    ("help", false),
    ("version", false),
];

/// XARGMATCH tables (sort.c CHECK_TABLE / SORT_TABLE). The valid-arguments
/// list groups aliases of the same value: 'quiet', 'silent' together.
const CHECK_ARGS: &[(&str, char)] = &[("quiet", 'C'), ("silent", 'C'), ("diagnose-first", 'c')];
const CHECK_VALID: &str = "  - 'quiet', 'silent'\n  - 'diagnose-first'\n";
const SORT_ARGS: &[(&str, char)] = &[
    ("general-numeric", 'g'),
    ("human-numeric", 'h'),
    ("month", 'M'),
    ("numeric", 'n'),
    ("random", 'R'),
    ("version", 'V'),
];
const SORT_VALID: &str = "  - 'general-numeric'\n  - 'human-numeric'\n  - 'month'\n  - 'numeric'\n  - 'random'\n  - 'version'\n";

/// GNU single-quote form for diagnostics (C locale quote style).
fn sq(s: &str) -> String {
    format!("'{s}'")
}

/// XARGMATCH port: exact match, else unique prefix. Err(true) = ambiguous
/// prefix ("ambiguous argument"), Err(false) = no match ("invalid
/// argument") — probed p5: "--check=" prints the ambiguous form, rc 1.
fn argmatch(word: &str, table: &[(&str, char)]) -> Result<char, bool> {
    for (name, val) in table {
        if *name == word {
            return Ok(*val);
        }
    }
    let matches: Vec<char> = table
        .iter()
        .filter(|(name, _)| name.starts_with(word))
        .map(|(_, val)| *val)
        .collect();
    match matches.len() {
        1 => Ok(matches[0]),
        0 => Err(false),
        _ => Err(true),
    }
}

fn argmatch_error(
    winux_ctx: &Ctx,
    io: &mut Io,
    ctx: &str,
    word: &str,
    ambiguous: bool,
    valid: &str,
) -> u32 {
    let kind = if ambiguous {
        "ambiguous argument"
    } else {
        "invalid argument"
    };
    let _ = writeln!(io.stderr, "sort: {kind} {} for {}", sq(word), sq(ctx));
    let _ = write!(io.stderr, "Valid arguments are:\n{valid}");
    let _ = writeln!(io.stderr, "{}", winux_core::try_help(winux_ctx, "sort"));
    1 // usage (EXIT_FAILURE), unlike the rc-2 usage errors
}

fn usage_fail(ctx: &Ctx, io: &mut Io, msg: &str) -> u32 {
    let _ = writeln!(io.stderr, "sort: {msg}");
    let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "sort"));
    2
}

// ---------------------------------------------------------------------
// parse_field_count port (xstrtoumax base 10, no suffixes)
// ---------------------------------------------------------------------

fn c_isspace(b: u8) -> bool {
    matches!(b, b' ' | b'\t' | b'\n' | 0x0b | 0x0c | b'\r')
}

fn is_digit(b: u8) -> bool {
    b.is_ascii_digit()
}

/// Parse the leading integer of `s` the way xstrtoumax does for sort.c:
/// optional C whitespace, optional '+', then digits. Returns the value
/// (clamped to usize::MAX on overflow, like GNU silently substituting
/// SIZE_MAX) and the rest after the digits. Leading '-' is not a number.
fn parse_field_count(s: &str) -> Option<(usize, &str)> {
    let b = s.as_bytes();
    let mut i = 0;
    while i < b.len() && c_isspace(b[i]) {
        i += 1;
    }
    if b.get(i) == Some(&b'+') {
        i += 1;
    }
    let start = i;
    while i < b.len() && is_digit(b[i]) {
        i += 1;
    }
    if i == start {
        return None;
    }
    let text = std::str::from_utf8(&b[start..i]).unwrap_or("0");
    let val: usize = text.parse().unwrap_or(usize::MAX);
    Some((val, &s[i..]))
}

fn bad_field_spec(ctx: &Ctx, io: &mut Io, spec: &str, msg: &str) -> u32 {
    usage_fail(
        ctx,
        io,
        &format!("{}: invalid field specification {}", msg, sq(spec)),
    )
}

fn invalid_count(ctx: &Ctx, io: &mut Io, msg: &str, string: &str) -> u32 {
    usage_fail(
        ctx,
        io,
        &format!("{}: invalid count at start of {}", msg, sq(string)),
    )
}

/// Parse the full -k keyspec (sort.c 'k' case) into a key appended to
/// `keys`. Returns Some(status) on a usage failure.
fn parse_keyspec(ctx: &Ctx, io: &mut Io, spec: &str, keys: &mut Vec<KeyField>) -> Option<u32> {
    let mut key = KeyField::new();
    let (f1, rest) = match parse_field_count(spec) {
        Some(v) => v,
        None => {
            return Some(invalid_count(
                ctx,
                io,
                "invalid number at field start",
                spec,
            ))
        }
    };
    key.sword = f1;
    if key.sword == 0 {
        return Some(bad_field_spec(ctx, io, spec, "field number is zero"));
    }
    key.sword -= 1;
    let mut rest = rest;
    if let Some(r) = rest.strip_prefix('.') {
        let (c1, r2) = match parse_field_count(r) {
            Some(v) => v,
            None => return Some(invalid_count(ctx, io, "invalid number after '.'", spec)),
        };
        if c1 == 0 {
            return Some(bad_field_spec(ctx, io, spec, "character offset is zero"));
        }
        key.schar = c1 - 1;
        rest = r2;
    }
    if key.sword == 0 && key.schar == 0 {
        key.sword = usize::MAX;
    }
    // set_ordering(s, key, bl_start)
    let mut r = apply_ordering_chars(rest, &mut key, true, false);
    if !r.starts_with(',') {
        key.eword = usize::MAX;
        key.echar = 0;
    } else {
        let (f2, r2) = match parse_field_count(&r[1..]) {
            Some(v) => v,
            None => return Some(invalid_count(ctx, io, "invalid number after ','", spec)),
        };
        key.eword = f2;
        if key.eword == 0 {
            return Some(bad_field_spec(ctx, io, spec, "field number is zero"));
        }
        key.eword -= 1;
        rest = r2;
        if let Some(r3) = rest.strip_prefix('.') {
            let (c2, r4) = match parse_field_count(r3) {
                Some(v) => v,
                None => return Some(invalid_count(ctx, io, "invalid number after '.'", spec)),
            };
            key.echar = c2; // as-is: POS2 .0 is legal (no zero check here)
            rest = r4;
        }
        r = apply_ordering_chars(rest, &mut key, false, true);
    }
    if !r.is_empty() {
        return Some(bad_field_spec(
            ctx,
            io,
            spec,
            "stray character in field spec",
        ));
    }
    keys.push(key);
    None
}

/// set_ordering loop: consume ordering letters, return the unconsumed rest.
fn apply_ordering_chars(s: &str, key: &mut KeyField, blank_start: bool, blank_end: bool) -> String {
    let bytes = s.as_bytes();
    let mut oi = 0;
    while oi < bytes.len() && apply_ordering(key, bytes[oi] as char, blank_start, blank_end) {
        oi += 1;
    }
    s[oi..].to_string()
}

/// Obsolescent +POS1 operand (sort.c main case 1). Ok(None) = stays a
/// file (invalid +POS is silently a file, probed p4: "+0x" -> cannot read).
fn parse_obsolete_start(spec: &str) -> Option<KeyField> {
    let mut key = KeyField::new();
    let (f1, rest) = parse_field_count(spec)?;
    key.sword = f1;
    let mut rest = rest;
    if let Some(r) = rest.strip_prefix('.') {
        let (c1, r2) = parse_field_count(r)?;
        key.schar = c1;
        rest = r2;
    }
    if key.sword == 0 && key.schar == 0 {
        key.sword = usize::MAX;
    }
    let r = apply_ordering_chars(rest, &mut key, true, false);
    if !r.is_empty() {
        return None; // stray char: silently a file
    }
    Some(key)
}

/// Parse the -POS2 partner. Fatal on a bad number (probed p4: "+0 -1x"
/// -> stray character in field spec '-1x').
fn parse_obsolete_end(ctx: &Ctx, io: &mut Io, arg: &str, key: &mut KeyField) -> Option<u32> {
    let body = &arg[1..];
    let (f2, rest) = match parse_field_count(body) {
        Some(v) => v,
        None => return Some(invalid_count(ctx, io, "invalid number after '-'", arg)),
    };
    key.eword = f2;
    let mut rest = rest;
    if let Some(r) = rest.strip_prefix('.') {
        let (c2, r2) = match parse_field_count(r) {
            Some(v) => v,
            None => return Some(invalid_count(ctx, io, "invalid number after '.'", arg)),
        };
        key.echar = c2;
        rest = r2;
    }
    // sort.c: +A.x -B.y == -k A+1.x+1,B.y (y = 0) / -k A+1.x+1,B+1.y;
    // eword is decremented only when the end field was given and y == 0.
    if key.echar == 0 && key.eword != 0 {
        key.eword -= 1;
    }
    let r = apply_ordering_chars(rest, key, false, true);
    if !r.is_empty() {
        return Some(bad_field_spec(
            ctx,
            io,
            arg,
            "stray character in field spec",
        ));
    }
    None
}

// ---------------------------------------------------------------------
// xstrtol-shaped -S/--buffer-size validation
// ---------------------------------------------------------------------

/// specify_sort_size: xstrtoumax(s, 10, "EgGkKmMPQRtTYZ") plus the
/// special single-char 'b' / '%' suffixes.
fn parse_size_arg(s: &str) -> Result<(), &'static str> {
    let b = s.as_bytes();
    let mut i = 0;
    while i < b.len() && c_isspace(b[i]) {
        i += 1;
    }
    if b.get(i) == Some(&b'+') {
        i += 1;
    }
    let start = i;
    while i < b.len() && is_digit(b[i]) {
        i += 1;
    }
    if i == start {
        return Err("invalid"); // no digits at all
    }
    if i < b.len() {
        // remaining must be exactly one accepted suffix
        if b.len() - i != 1 {
            return Err("suffix");
        }
        match b[i] {
            b'b' | b'%' | b'E' | b'g' | b'G' | b'k' | b'K' | b'm' | b'M' | b'P' | b'Q' | b'R'
            | b't' | b'T' | b'Y' | b'Z' => {}
            _ => return Err("suffix"),
        }
    }
    Ok(())
}

// ---------------------------------------------------------------------
// the command
// ---------------------------------------------------------------------

const SORT_HELP_BODY: &str = r#"Usage: sort [OPTION]... [FILE]...
  or:  sort [OPTION]... --files0-from=F
Write sorted concatenation of all FILE(s) to standard output.

With no FILE, or when FILE is -, read standard input.

Mandatory arguments to long options are mandatory for short options too.
Ordering options:

  -b, --ignore-leading-blanks  ignore leading blanks
  -d, --dictionary-order      consider only blanks and alphanumeric characters
  -f, --ignore-case           fold lower case to upper case characters
  -g, --general-numeric-sort  compare according to general numerical value
  -i, --ignore-nonprinting    consider only printable characters
  -M, --month-sort            compare (unknown) < 'JAN' < ... < 'DEC'
  -h, --human-numeric-sort    compare human readable numbers (e.g., 2K 1G)
  -n, --numeric-sort          compare according to string numerical value
  -R, --random-sort           shuffle, but group identical keys.  See shuf(1)
      --random-source=FILE    get random bytes from FILE
  -r, --reverse               reverse the result of comparisons
      --sort=WORD             sort according to WORD:
                                general-numeric -g, human-numeric -h, month -M,
                                numeric -n, random -R, version -V
  -V, --version-sort          natural sort of (version) numbers within text

Other options:

      --batch-size=NMERGE   merge at most NMERGE inputs at once;
                            for more use temp files
  -c, --check, --check=diagnose-first  check for sorted input; do not sort
  -C, --check=quiet, --check=silent  like -c, but do not report first bad line
      --compress-program=PROG  compress temporaries with PROG;
                              decompress them with PROG -d
      --debug               annotate the part of the line used to sort,
                              and warn about questionable usage to stderr
      --files0-from=F       read input from the files specified by
                            NUL-terminated names in file F;
                            If F is - then read names from standard input
  -k, --key=KEYDEF          sort via a key; KEYDEF gives location and type
  -m, --merge               merge already sorted files; do not sort
  -o, --output=FILE         write result to FILE instead of standard output
  -s, --stable              stabilize sort by disabling last-resort comparison
  -S, --buffer-size=SIZE    use SIZE for main memory buffer
  -t, --field-separator=SEP  use SEP instead of non-blank to blank transition
  -T, --temporary-directory=DIR  use DIR for temporaries, not $TMPDIR or /tmp;
                              multiple options specify multiple directories
      --parallel=N          change the number of sorts run concurrently to N
  -u, --unique              with -c, check for strict ordering;
                              without -c, output only the first of an equal run
  -z, --zero-terminated     line delimiter is NUL, not newline
      --help        display this help and exit
      --version     output version information and exit

KEYDEF is F[.C][OPTS][,F[.C][OPTS]] for start and stop position, where F is a
field number and C a character position in the field; both are origin 1, and
the stop position defaults to the line's end.  If neither -t nor -b is in
effect, characters in a field are counted from the beginning of the preceding
whitespace.  OPTS is one or more single-letter ordering options [bdfgiMhnRrV],
which override global ordering options for that key.  If no key is given, use
the entire line as the key.  Use --debug to diagnose incorrect key usage.

SIZE may be followed by the following multiplicative suffixes:
% 1% of memory, b 1, K 1024 (default), and so on for M, G, T, P, E, Z, Y, R, Q.

*** WARNING ***
The locale specified by the environment affects sort order.
Set LC_ALL=C to get the traditional sort order that uses
native byte values.

"#;

pub fn sort_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let (mut cfg, gkey) = match parse_args(args, ctx, io) {
        Ok(Some(pair)) => pair,
        Ok(None) => return Ok(0), // --help/--version already printed
        Err(status) => return Ok(status),
    };

    // --- inheritance (sort.c main, after option parsing) ---
    let mut keys = std::mem::take(&mut cfg.keys);
    let mut gkey = gkey;
    for key in keys.iter_mut() {
        if key.default_compare() && !key.reverse {
            key.ignore = gkey.ignore;
            key.translate = gkey.translate;
            key.skipsblanks = gkey.skipsblanks;
            key.skipeblanks = gkey.skipeblanks;
            key.month = gkey.month;
            key.numeric = gkey.numeric;
            key.general_numeric = gkey.general_numeric;
            key.human_numeric = gkey.human_numeric;
            key.version = gkey.version;
            key.random = gkey.random;
            key.reverse = gkey.reverse;
        }
    }
    if keys.is_empty() && !gkey.default_compare() {
        keys.push(gkey.clone());
    }
    if let Some(status) = check_ordering_compatibility(ctx, io, &keys) {
        return Ok(status);
    }
    cfg.keys = keys;
    cfg.reverse = gkey.reverse;

    // --debug is incompatible with -c/-o (sort.c debug block).
    if cfg.debug && (cfg.checkonly.is_some() || cfg.outfile.is_some()) {
        let x = if cfg.checkonly.is_some() {
            cfg.checkonly.unwrap()
        } else {
            'o'
        };
        return Ok(usage_fail(
            ctx,
            io,
            &format!("options '-{} --debug' are incompatible", x),
        ));
    }

    // Random keys validate the random source eagerly (sort.c:
    // random_md5_state_init before any input is read; probed p4:
    // "open failed: nope-rs: ...", rc 2).
    if cfg.keys.iter().any(|k| k.random) {
        if let Some(src) = cfg.random_source.clone() {
            if let Err(e) = std::fs::File::open(fsutil::resolve(ctx, &src)) {
                let reason = read_reason(ctx, &src, &e);
                let _ = writeln!(
                    io.stderr,
                    "sort: open failed: {}: {}",
                    fsutil::quotef(&src),
                    reason
                );
                return Ok(2);
            }
        }
    }

    // checkonly branch (sort.c main): the extra-operand and -o conflicts
    // fire in this order, before anything is read.
    if let Some(ck) = cfg.checkonly {
        let mut files = cfg.files.clone();
        if files.is_empty() {
            files.push("-".to_string());
        }
        if files.len() > 1 {
            let _ = writeln!(
                io.stderr,
                "sort: extra operand {} not allowed with -{}",
                sq(&files[1]),
                ck
            );
            let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "sort"));
            return Ok(2);
        }
        if cfg.outfile.is_some() {
            let opts = format!("{ck}o");
            let _ = writeln!(io.stderr, "sort: options '-{opts}' are incompatible");
            let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "sort"));
            return Ok(2);
        }
        return run_check(&cfg, io, ctx);
    }
    run_sort(&cfg, io, ctx)
}

/// GNU getopt_long replica for sort's tables, with sort.c's in-order
/// option side effects (duplicate -o/-t/-c conflicts fire at the second
/// occurrence). Ok(None) = --help/--version printed, exit 0. Err(status)
/// = usage failure (already reported).
fn parse_args(args: &[OsString], ctx: &Ctx, io: &mut Io) -> Result<Option<(Cfg, KeyField)>, u32> {
    let mut cfg = Cfg::new();
    let mut gkey = KeyField::new();

    let mut i = 0usize;
    let mut no_more_options = false;

    while i < args.len() {
        let arg = args[i].to_string_lossy().into_owned();

        // operand branch (GNU permutes; "--" ends option processing)
        if arg == "--" && !no_more_options {
            no_more_options = true;
            i += 1;
            continue;
        }
        if no_more_options || arg == "-" || !arg.starts_with('-') {
            // sort.c case 1: obsolescent +POS1 [-POS2]
            if !no_more_options && arg.len() > 1 && arg.starts_with('+') {
                let minus_pos_usage = i + 1 < args.len() && {
                    let n = args[i + 1].to_string_lossy().into_owned();
                    n.len() > 1 && n.starts_with('-') && is_digit(n.as_bytes()[1])
                };
                if let Some(mut key) = parse_obsolete_start(&arg[1..]) {
                    if minus_pos_usage {
                        i += 1;
                        let endarg = args[i].to_string_lossy().into_owned();
                        if let Some(s) = parse_obsolete_end(ctx, io, &endarg, &mut key) {
                            return Err(s);
                        }
                    }
                    cfg.keys.push(key);
                    i += 1;
                    continue;
                }
            }
            cfg.files.push(arg);
            i += 1;
            continue;
        }

        if let Some(long) = arg.strip_prefix("--") {
            let (name, inline) = match long.split_once('=') {
                Some((n, v)) => (n.to_string(), Some(v.to_string())),
                None => (long.to_string(), None),
            };
            // exact match wins; else unique abbreviation over the table
            // plus the virtual help/version
            let exact = LONG_OPTS.iter().find(|(n, _)| *n == name);
            let mut candidates: Vec<&str> = LONG_OPTS
                .iter()
                .filter(|(n, _)| n.starts_with(&name))
                .map(|(n, _)| *n)
                .collect();
            if "help".starts_with(&name) {
                candidates.push("help");
            }
            if "version".starts_with(&name) {
                candidates.push("version");
            }

            // resolve: exact match wins; else unique abbreviation over the
            // table plus the virtual help/version
            let resolved: Option<String> = if exact.is_some() {
                exact.map(|(n, _)| n.to_string())
            } else if candidates.len() == 1 {
                Some(candidates[0].to_string())
            } else {
                None
            };
            match resolved {
                Some(canon) if canon == "help" => {
                    let _ = write_help(io);
                    return Ok(None);
                }
                Some(canon) if canon == "version" => {
                    let _ = write_version(io);
                    return Ok(None);
                }
                Some(canon) => {
                    // --check is optional-argument: an inline value routes
                    // to XARGMATCH; bare --check never consumes the next argv
                    let mut takes_value = LONG_OPTS.iter().find(|(n, _)| *n == canon).unwrap().1;
                    if canon == "check" && inline.is_some() {
                        takes_value = true;
                    }
                    if takes_value {
                        let value = match inline {
                            Some(v) => {
                                i += 1;
                                v
                            }
                            None => match args.get(i + 1) {
                                Some(v) => {
                                    i += 2;
                                    v.to_string_lossy().into_owned()
                                }
                                None => {
                                    return Err(usage_fail(
                                        ctx,
                                        io,
                                        &argopt::option_requires_argument(
                                            ctx,
                                            &format!("--{canon}"),
                                        ),
                                    ));
                                }
                            },
                        };
                        if let Some(s) =
                            apply_long_value(ctx, io, &mut cfg, &mut gkey, &canon, &value)
                        {
                            return Err(s);
                        }
                        continue;
                    }
                    if inline.is_some() {
                        return Err(usage_fail(
                            ctx,
                            io,
                            &argopt::option_no_argument(ctx, &format!("--{canon}")),
                        ));
                    }
                    if let Some(s) = apply_long_flag(ctx, io, &mut cfg, &mut gkey, &canon) {
                        return Err(s);
                    }
                    i += 1;
                    continue;
                }
                _ => {}
            }
            if candidates.is_empty() {
                return Err(usage_fail(
                    ctx,
                    io,
                    &argopt::unrecognized_option(ctx, &format!("--{name}")),
                ));
            }
            let list: Vec<String> = candidates.iter().map(|c| sq(&format!("--{c}"))).collect();
            return Err(usage_fail(
                ctx,
                io,
                &argopt::ambiguous_option(ctx, &format!("--{name}"), &list.join(" ")),
            ));
        }

        // short option cluster
        let chars: Vec<char> = arg.chars().skip(1).collect();
        let mut ci = 0usize;
        let mut consumed_next = false;
        while ci < chars.len() {
            let c = chars[ci];
            let takes_value = matches!(c, 'k' | 'o' | 't' | 'S' | 'T' | 'y');
            if takes_value {
                let rest: String = chars[ci + 1..].iter().collect();
                let attached = !rest.is_empty();
                let value = if attached {
                    rest
                } else {
                    match args.get(i + 1) {
                        Some(v) => {
                            consumed_next = true;
                            v.to_string_lossy().into_owned()
                        }
                        None => {
                            return Err(usage_fail(
                                ctx,
                                io,
                                &argopt::short_option_requires_argument(ctx, &c.to_string()),
                            ));
                        }
                    }
                };
                if let Some(s) = apply_short_value(ctx, io, &mut cfg, c, &value, attached) {
                    return Err(s);
                }
                ci = chars.len();
            } else {
                if let Some(s) = apply_short_flag(ctx, io, &mut cfg, &mut gkey, c) {
                    return Err(s);
                }
                ci += 1;
            }
        }
        i += 1 + consumed_next as usize;
    }

    Ok(Some((cfg, gkey)))
}

fn write_help(io: &mut Io) -> std::io::Result<()> {
    write!(
        io.stdout,
        "{}{}",
        SORT_HELP_BODY,
        gnuhelp::footer("sort", "sort invocation")
    )
}

fn write_version(io: &mut Io) -> std::io::Result<()> {
    write!(
        io.stdout,
        "{}",
        gnuhelp::coreutils_version("sort", "Mike Haertel and Paul Eggert")
    )
}

/// Long options with a required value (canonical name).
fn apply_long_value(
    ctx: &Ctx,
    io: &mut Io,
    cfg: &mut Cfg,
    gkey: &mut KeyField,
    canon: &str,
    value: &str,
) -> Option<u32> {
    match canon {
        "key" => parse_keyspec(ctx, io, value, &mut cfg.keys),
        "output" => set_output(ctx, io, cfg, value),
        "field-separator" => set_tab(ctx, io, cfg, value),
        "random-source" => set_random_source(ctx, io, cfg, value),
        "compress-program" => set_compress_program(ctx, io, cfg, value),
        "sort" => apply_sort_word(ctx, io, gkey, value),
        "check" => apply_check_word(ctx, io, cfg, value),
        "batch-size" => apply_batch_size(ctx, io, value),
        "buffer-size" => apply_buffer_size(ctx, io, "--buffer-size", value),
        "parallel" => apply_parallel(ctx, io, value),
        "temporary-directory" => None, // accepted and ignored (no temp files)
        _ => unreachable!(),
    }
}

/// Long flags (canonical name). --check is optional-argument: the bare
/// form is 'c' and never consumes the next argv (sort.c CHECK_OPTION).
fn apply_long_flag(
    ctx: &Ctx,
    io: &mut Io,
    cfg: &mut Cfg,
    gkey: &mut KeyField,
    canon: &str,
) -> Option<u32> {
    match canon {
        "check" => set_check_mode(ctx, io, cfg, 'c'),
        "debug" => {
            cfg.debug = true;
            None
        }
        "merge" => {
            cfg.mergeonly = true;
            None
        }
        "stable" => {
            cfg.stable = true;
            None
        }
        "unique" => {
            cfg.unique = true;
            None
        }
        "zero-terminated" => {
            cfg.eol = 0;
            None
        }
        _ => {
            let letter = match canon {
                "ignore-leading-blanks" => 'b',
                "dictionary-order" => 'd',
                "ignore-case" => 'f',
                "general-numeric-sort" => 'g',
                "ignore-nonprinting" => 'i',
                "month-sort" => 'M',
                "numeric-sort" => 'n',
                "human-numeric-sort" => 'h',
                "version-sort" => 'V',
                "random-sort" => 'R',
                "reverse" => 'r',
                _ => unreachable!(),
            };
            apply_ordering(gkey, letter, true, true);
            None
        }
    }
}

fn apply_short_flag(
    ctx: &Ctx,
    io: &mut Io,
    cfg: &mut Cfg,
    gkey: &mut KeyField,
    c: char,
) -> Option<u32> {
    match c {
        'b' | 'd' | 'f' | 'g' | 'h' | 'i' | 'M' | 'n' | 'R' | 'r' | 'V' => {
            apply_ordering(gkey, c, true, true);
            None
        }
        'm' => {
            cfg.mergeonly = true;
            None
        }
        's' => {
            cfg.stable = true;
            None
        }
        'u' => {
            cfg.unique = true;
            None
        }
        'z' => {
            cfg.eol = 0;
            None
        }
        'c' | 'C' => set_check_mode(ctx, io, cfg, c),
        _ => opt_err(ctx, io, &argopt::invalid_option(ctx, &c.to_string())),
    }
}

fn apply_short_value(
    ctx: &Ctx,
    io: &mut Io,
    cfg: &mut Cfg,
    c: char,
    value: &str,
    attached: bool,
) -> Option<u32> {
    match c {
        'k' => parse_keyspec(ctx, io, value, &mut cfg.keys),
        'o' => set_output(ctx, io, cfg, value),
        't' => set_tab(ctx, io, cfg, value),
        'S' => apply_buffer_size(ctx, io, "-S", value),
        'T' => None, // accepted and ignored
        'y' => {
            // Solaris -y: an attached value is dropped silently; a detached
            // value is dropped only when it is all digits, otherwise it
            // re-enters the stream as a file operand (sort.c case 'y').
            if !attached && !value.bytes().all(is_digit) {
                cfg.files.push(value.to_string());
            }
            None
        }
        _ => unreachable!(),
    }
}

fn opt_err(ctx: &Ctx, io: &mut Io, msg: &str) -> Option<u32> {
    Some(usage_fail(ctx, io, msg))
}

fn set_check_mode(ctx: &Ctx, io: &mut Io, cfg: &mut Cfg, c: char) -> Option<u32> {
    if let Some(prev) = cfg.checkonly {
        if prev != c {
            // GNU hardcodes the pair regardless of order (probed p4).
            return opt_err(ctx, io, "options '-cC' are incompatible");
        }
    }
    cfg.checkonly = Some(c);
    None
}

fn set_output(ctx: &Ctx, io: &mut Io, cfg: &mut Cfg, value: &str) -> Option<u32> {
    if let Some(prev) = cfg.outfile.clone() {
        if prev != value {
            return opt_err(ctx, io, "multiple output files specified");
        }
    }
    cfg.outfile = Some(value.to_string());
    None
}

fn set_tab(ctx: &Ctx, io: &mut Io, cfg: &mut Cfg, value: &str) -> Option<u32> {
    let newtab = match value.as_bytes().first() {
        None => return opt_err(ctx, io, "empty tab"),
        Some(&b'\\') if value == r"\0" => 0u8,
        Some(&first) => {
            if value.as_bytes().len() != 1 {
                return opt_err(ctx, io, &format!("multi-character tab {}", sq(value)));
            }
            first
        }
    };
    if let Some(old) = cfg.tab {
        if old != newtab {
            return opt_err(ctx, io, "incompatible tabs");
        }
    }
    cfg.tab = Some(newtab);
    None
}

fn set_random_source(ctx: &Ctx, io: &mut Io, cfg: &mut Cfg, value: &str) -> Option<u32> {
    if let Some(prev) = cfg.random_source.clone() {
        if prev != value {
            return opt_err(ctx, io, "multiple random sources specified");
        }
    }
    cfg.random_source = Some(value.to_string());
    None
}

fn set_compress_program(ctx: &Ctx, io: &mut Io, cfg: &mut Cfg, value: &str) -> Option<u32> {
    if let Some(prev) = cfg.compress_program.clone() {
        if prev != value {
            return opt_err(ctx, io, "multiple compress programs specified");
        }
    }
    cfg.compress_program = Some(value.to_string());
    None
}

fn apply_sort_word(ctx: &Ctx, io: &mut Io, gkey: &mut KeyField, word: &str) -> Option<u32> {
    match argmatch(word, SORT_ARGS) {
        Ok(letter) => {
            // --sort=WORD accumulates into gkey exactly like the letter
            // (sort.c SORT_OPTION falls through to the letter cases).
            apply_ordering(gkey, letter, true, true);
            None
        }
        Err(ambiguous) => Some(argmatch_error(
            ctx, io, "--sort", word, ambiguous, SORT_VALID,
        )),
    }
}

fn apply_check_word(ctx: &Ctx, io: &mut Io, cfg: &mut Cfg, word: &str) -> Option<u32> {
    match argmatch(word, CHECK_ARGS) {
        Ok(c) => set_check_mode(ctx, io, cfg, c),
        Err(ambiguous) => Some(argmatch_error(
            ctx,
            io,
            "--check",
            word,
            ambiguous,
            CHECK_VALID,
        )),
    }
}

/// xstrtol_fatal-shaped diagnostics for -S/--buffer-size (probed p5:
/// "-S1X" -> "invalid suffix in -S argument '1X'").
fn apply_buffer_size(ctx: &Ctx, io: &mut Io, optname: &str, value: &str) -> Option<u32> {
    match parse_size_arg(value) {
        Ok(()) => None,
        Err("invalid") => Some(
            opt_err(
                ctx,
                io,
                &format!("invalid {optname} argument {}", sq(value)),
            )
            .unwrap(),
        ),
        Err("suffix") => Some(
            opt_err(
                ctx,
                io,
                &format!("invalid suffix in {optname} argument {}", sq(value)),
            )
            .unwrap(),
        ),
        Err(_) => Some(
            opt_err(
                ctx,
                io,
                &format!("{optname} argument {} too large", sq(value)),
            )
            .unwrap(),
        ),
    }
}

/// specify_nmerge: invalid, minimum-2, or too large (rc 2 each).
fn apply_batch_size(ctx: &Ctx, io: &mut Io, value: &str) -> Option<u32> {
    let parsed = parse_field_count(value);
    match parsed {
        Some((n, rest)) if rest.is_empty() => {
            if n < 2 {
                let _ = writeln!(
                    io.stderr,
                    "sort: invalid --batch-size argument {}",
                    sq(value)
                );
                let _ = writeln!(io.stderr, "sort: minimum --batch-size argument is '2'");
                let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "sort"));
                return Some(2);
            }
            None
        }
        _ => Some(
            opt_err(
                ctx,
                io,
                &format!("invalid --batch-size argument {}", sq(value)),
            )
            .unwrap(),
        ),
    }
}

/// specify_nthreads: 0 is rejected with GNU's wording (probed p5).
fn apply_parallel(ctx: &Ctx, io: &mut Io, value: &str) -> Option<u32> {
    match parse_field_count(value) {
        Some((n, rest)) if rest.is_empty() => {
            if n == 0 {
                return Some(opt_err(ctx, io, "number in parallel must be nonzero").unwrap());
            }
            None
        }
        _ => Some(
            opt_err(
                ctx,
                io,
                &format!("invalid --parallel argument {}", sq(value)),
            )
            .unwrap(),
        ),
    }
}

/// sort.c check_ordering_compatibility: more than one ordering type per
/// key is a usage failure; the rendered letters clear b/r first
/// (key_to_opts order b d f g h i M n R r V).
fn check_ordering_compatibility(ctx: &Ctx, io: &mut Io, keys: &[KeyField]) -> Option<u32> {
    for key in keys {
        let count = key.numeric as u32
            + key.general_numeric as u32
            + key.human_numeric as u32
            + key.month as u32
            + ((key.version || key.random || key.ignore != Ignore::None) as u32);
        if count > 1 {
            let mut opts = String::new();
            if key.ignore == Ignore::Nondictionary {
                opts.push('d');
            }
            if key.translate {
                opts.push('f');
            }
            if key.general_numeric {
                opts.push('g');
            }
            if key.human_numeric {
                opts.push('h');
            }
            if key.ignore == Ignore::Nonprinting {
                opts.push('i');
            }
            if key.month {
                opts.push('M');
            }
            if key.numeric {
                opts.push('n');
            }
            if key.random {
                opts.push('R');
            }
            if key.version {
                opts.push('V');
            }
            return Some(opt_err(ctx, io, &format!("options '-{opts}' are incompatible")).unwrap());
        }
    }
    None
}

// ---------------------------------------------------------------------
// field boundaries (sort.c begfield / limfield, literal port)
// ---------------------------------------------------------------------

#[inline]
fn is_blank(b: u8) -> bool {
    b == b' ' || b == b'\t'
}

#[inline]
fn fold_upper(b: u8) -> u8 {
    if b.is_ascii_lowercase() {
        b - 32
    } else {
        b
    }
}

/// Index of the first byte of the key field within the line content.
fn begfield(content: &[u8], key: &KeyField, tab: Option<u8>) -> usize {
    let lim = content.len();
    let mut ptr = 0usize;
    let mut sword = key.sword;

    if let Some(sep) = tab {
        while ptr < lim && sword > 0 {
            sword -= 1;
            while ptr < lim && content[ptr] != sep {
                ptr += 1;
            }
            if ptr < lim {
                ptr += 1;
            }
        }
    } else {
        while ptr < lim && sword > 0 {
            sword -= 1;
            while ptr < lim && is_blank(content[ptr]) {
                ptr += 1;
            }
            while ptr < lim && !is_blank(content[ptr]) {
                ptr += 1;
            }
        }
    }
    if key.skipsblanks {
        while ptr < lim && is_blank(content[ptr]) {
            ptr += 1;
        }
    }
    ptr.saturating_add(key.schar).min(lim)
}

/// One-past-the-end index of the key field within the line content.
fn limfield(content: &[u8], key: &KeyField, tab: Option<u8>) -> usize {
    let lim = content.len();
    let mut eword = key.eword;
    let echar = key.echar;
    if echar == 0 {
        eword = eword.saturating_add(1);
    }
    let mut ptr = 0usize;
    if let Some(sep) = tab {
        while ptr < lim && eword > 0 {
            eword -= 1;
            while ptr < lim && content[ptr] != sep {
                ptr += 1;
            }
            if ptr < lim && (eword > 0 || echar > 0) {
                ptr += 1;
            }
        }
    } else {
        while ptr < lim && eword > 0 {
            eword -= 1;
            while ptr < lim && is_blank(content[ptr]) {
                ptr += 1;
            }
            while ptr < lim && !is_blank(content[ptr]) {
                ptr += 1;
            }
        }
    }
    if echar != 0 {
        if key.skipeblanks {
            while ptr < lim && is_blank(content[ptr]) {
                ptr += 1;
            }
        }
        return ptr.saturating_add(echar).min(lim);
    }
    ptr
}

/// The first key's spans for one line (sort.c fillbuf precomputation).
fn precompute_key0(buf: &[u8], ln: &mut Ln, k0: &KeyField, tab: Option<u8>) {
    let content = &buf[ln.start..ln.start + ln.len - 1];
    ln.klim = if k0.eword != usize::MAX {
        limfield(content, k0, tab)
    } else {
        content.len()
    };
    ln.kbeg = if k0.sword != usize::MAX {
        begfield(content, k0, tab)
    } else {
        let mut p = 0usize;
        if k0.skipsblanks {
            while p < ln.klim && is_blank(content[p]) {
                p += 1;
            }
        }
        p
    };
}

// ---------------------------------------------------------------------
// comparators
// ---------------------------------------------------------------------

/// A line: span in the input buffer including the eol, plus the
/// precomputed first-key span.
#[derive(Clone, Copy, Debug)]
struct Ln {
    start: usize,
    len: usize,
    kbeg: usize,
    klim: usize,
}

/// Compare driver (sort.c compare): keys, then the last-resort whole-line
/// comparison unless -s/-u; the last resort is reversed by global -r only.
struct Cmp<'a> {
    buf: &'a [u8],
    keys: &'a [KeyField],
    tab: Option<u8>,
    unique: bool,
    stable: bool,
    reverse: bool,
}

impl<'a> Cmp<'a> {
    fn content(&self, l: &Ln) -> &'a [u8] {
        &self.buf[l.start..l.start + l.len - 1]
    }

    fn compare(&self, a: &Ln, b: &Ln) -> i32 {
        if !self.keys.is_empty() {
            let diff = self.keycompare(a, b);
            if diff != 0 || self.unique || self.stable {
                return diff;
            }
        }
        let ca = self.content(a);
        let cb = self.content(b);
        let diff = match ca.cmp(cb) {
            std::cmp::Ordering::Less => -1,
            std::cmp::Ordering::Equal => 0,
            std::cmp::Ordering::Greater => 1,
        };
        if self.reverse {
            -diff
        } else {
            diff
        }
    }

    /// sort.c keycompare with the C-locale dispatch.
    fn keycompare(&self, a: &Ln, b: &Ln) -> i32 {
        let la = self.content(a);
        let lb = self.content(b);
        let mut ta = a.kbeg;
        let mut tla = a.klim;
        let mut tb = b.kbeg;
        let mut tlb = b.klim;

        for (idx, key) in self.keys.iter().enumerate() {
            if idx > 0 {
                // Find the beginning and limit of the next field.
                if key.eword != usize::MAX {
                    tla = limfield(la, key, self.tab);
                    tlb = limfield(lb, key, self.tab);
                } else {
                    tla = la.len();
                    tlb = lb.len();
                }
                if key.sword != usize::MAX {
                    ta = begfield(la, key, self.tab);
                    tb = begfield(lb, key, self.tab);
                } else {
                    ta = 0;
                    tb = 0;
                    if key.skipsblanks {
                        while ta < tla && is_blank(la[ta]) {
                            ta += 1;
                        }
                        while tb < tlb && is_blank(lb[tb]) {
                            tb += 1;
                        }
                    }
                }
            }
            // Treat field ends before field starts as empty fields.
            if tla < ta {
                tla = ta;
            }
            if tlb < tb {
                tlb = tb;
            }
            let mut diff = self.compare_one(key, la, ta..tla, lb, tb..tlb);
            if key.reverse {
                diff = -diff;
            }
            if diff != 0 {
                return diff;
            }
        }
        0
    }

    /// One key's comparison, dispatching exactly like sort.c keycompare.
    fn compare_one(
        &self,
        key: &KeyField,
        la: &'a [u8],
        sa: std::ops::Range<usize>,
        lb: &'a [u8],
        sb: std::ops::Range<usize>,
    ) -> i32 {
        let ka = &la[sa];
        let kb = &lb[sb];
        if key.numeric
            || key.general_numeric
            || key.human_numeric
            || key.month
            || key.random
            || key.version
        {
            // Filtered copies when ignore/translate are in effect
            // (sort.c copy branch; for numeric/month keys only -f can
            // pair after the compatibility check, for -R/-V also -d/-i).
            let (fa, fb): (std::borrow::Cow<'_, [u8]>, std::borrow::Cow<'_, [u8]>);
            if key.ignore != Ignore::None || key.translate {
                let mut va = Vec::with_capacity(ka.len());
                for &c in ka {
                    if !ignored(key.ignore, c) {
                        va.push(if key.translate { fold_upper(c) } else { c });
                    }
                }
                let mut vb = Vec::with_capacity(kb.len());
                for &c in kb {
                    if !ignored(key.ignore, c) {
                        vb.push(if key.translate { fold_upper(c) } else { c });
                    }
                }
                fa = std::borrow::Cow::Owned(va);
                fb = std::borrow::Cow::Owned(vb);
            } else {
                fa = std::borrow::Cow::Borrowed(ka);
                fb = std::borrow::Cow::Borrowed(kb);
            }
            let (xa, xb): (&[u8], &[u8]) = (&fa[..], &fb[..]);
            if key.numeric {
                return numcompare_blanks(xa, xb).signum();
            }
            if key.general_numeric {
                return general_numcompare(xa, xb);
            }
            if key.human_numeric {
                return human_numcompare(xa, xb);
            }
            if key.month {
                return (getmonth(xa) - getmonth(xb)).signum();
            }
            if key.random {
                return compare_random(xa, xb);
            }
            return filenvercmp(xa, xb);
        }
        if key.ignore != Ignore::None {
            // CMP_WITH_IGNORE, with optional inline translate
            let mut ia = 0usize;
            let mut ib = 0usize;
            loop {
                while ia < ka.len() && ignored(key.ignore, ka[ia]) {
                    ia += 1;
                }
                while ib < kb.len() && ignored(key.ignore, kb[ib]) {
                    ib += 1;
                }
                let ha = ia < ka.len();
                let hb = ib < kb.len();
                if !ha || !hb {
                    return (ha as i32) - (hb as i32);
                }
                let (x, y) = if key.translate {
                    (fold_upper(ka[ia]), fold_upper(kb[ib]))
                } else {
                    (ka[ia], kb[ib])
                };
                if x != y {
                    return (x as i32) - (y as i32);
                }
                ia += 1;
                ib += 1;
            }
        }
        // plain byte compare on translated (or raw) bytes, original-length
        // tiebreak (sort.c final branch: memcmp + lena/lenb)
        let n = ka.len().min(kb.len());
        let mut diff = 0i32;
        for i in 0..n {
            let (x, y) = if key.translate {
                (fold_upper(ka[i]), fold_upper(kb[i]))
            } else {
                (ka[i], kb[i])
            };
            if x != y {
                diff = (x as i32) - (y as i32);
                break;
            }
        }
        if diff == 0 {
            diff = (ka.len() as i32) - (kb.len() as i32);
        }
        diff.signum()
    }
}

fn ignored(mode: Ignore, c: u8) -> bool {
    match mode {
        Ignore::None => false,
        Ignore::Nonprinting => !(0x20..=0x7e).contains(&c),
        Ignore::Nondictionary => !(c.is_ascii_alphanumeric() || is_blank(c)),
    }
}

// ---------------------------------------------------------------------
// -n: gnulib strnumcmp-in.h numcompare, C locale (decimal '.', no
// thousands separator). sort.c's numcompare wrapper skips leading
// blanks first; numeric keys go through numcompare_blanks.
// ---------------------------------------------------------------------

fn strnumcmp(a: &[u8], b: &[u8]) -> i32 {
    let av = |i: usize| -> u8 { a.get(i).copied().unwrap_or(0) };
    let bv = |i: usize| -> u8 { b.get(i).copied().unwrap_or(0) };
    let mut ai = 0usize;
    let mut bi = 0usize;
    let mut tmpa = av(0);
    let mut tmpb = bv(0);

    if tmpa == b'-' {
        loop {
            ai += 1;
            tmpa = av(ai);
            if tmpa != b'0' {
                break;
            }
        }
        if tmpb != b'-' {
            if tmpa == b'.' {
                loop {
                    ai += 1;
                    tmpa = av(ai);
                    if tmpa != b'0' {
                        break;
                    }
                }
            }
            if is_digit(tmpa) {
                return -1;
            }
            while tmpb == b'0' {
                bi += 1;
                tmpb = bv(bi);
            }
            if tmpb == b'.' {
                loop {
                    bi += 1;
                    tmpb = bv(bi);
                    if tmpb != b'0' {
                        break;
                    }
                }
            }
            return -(is_digit(tmpb) as i32);
        }
        loop {
            bi += 1;
            tmpb = bv(bi);
            if tmpb != b'0' {
                break;
            }
        }
        // both negative
        while tmpa == tmpb && is_digit(tmpa) {
            ai += 1;
            tmpa = av(ai);
            bi += 1;
            tmpb = bv(bi);
        }
        if (tmpa == b'.' && !is_digit(tmpb)) || (tmpb == b'.' && !is_digit(tmpa)) {
            return fraccompare(b, ai, a, bi);
        }
        let tmp = (tmpb as i32) - (tmpa as i32);
        let mut log_a = 0usize;
        while is_digit(tmpa) {
            log_a += 1;
            ai += 1;
            tmpa = av(ai);
        }
        let mut log_b = 0usize;
        while is_digit(tmpb) {
            log_b += 1;
            bi += 1;
            tmpb = bv(bi);
        }
        if log_a != log_b {
            return if log_a < log_b { 1 } else { -1 };
        }
        if log_a == 0 {
            return 0;
        }
        return tmp;
    } else if tmpb == b'-' {
        loop {
            bi += 1;
            tmpb = bv(bi);
            if tmpb != b'0' {
                break;
            }
        }
        if tmpb == b'.' {
            loop {
                bi += 1;
                tmpb = bv(bi);
                if tmpb != b'0' {
                    break;
                }
            }
        }
        if is_digit(tmpb) {
            return 1;
        }
        while tmpa == b'0' {
            ai += 1;
            tmpa = av(ai);
        }
        if tmpa == b'.' {
            loop {
                ai += 1;
                tmpa = av(ai);
                if tmpa != b'0' {
                    break;
                }
            }
        }
        return is_digit(tmpa) as i32;
    }

    // both non-negative
    while tmpa == b'0' {
        ai += 1;
        tmpa = av(ai);
    }
    while tmpb == b'0' {
        bi += 1;
        tmpb = bv(bi);
    }
    while tmpa == tmpb && is_digit(tmpa) {
        ai += 1;
        tmpa = av(ai);
        bi += 1;
        tmpb = bv(bi);
    }
    if (tmpa == b'.' && !is_digit(tmpb)) || (tmpb == b'.' && !is_digit(tmpa)) {
        return fraccompare(a, ai, b, bi);
    }
    let tmp = (tmpa as i32) - (tmpb as i32);
    let mut log_a = 0usize;
    while is_digit(tmpa) {
        log_a += 1;
        ai += 1;
        tmpa = av(ai);
    }
    let mut log_b = 0usize;
    while is_digit(tmpb) {
        log_b += 1;
        bi += 1;
        tmpb = bv(bi);
    }
    if log_a != log_b {
        return if log_a < log_b { -1 } else { 1 };
    }
    if log_a == 0 {
        return 0;
    }
    tmp
}

/// fraccompare port with explicit cursors. `ai0`/`bi0` are the positions
/// of the first differing byte (or of the byte after '.', for the tail
/// paths).
fn fraccompare(a: &[u8], ai0: usize, b: &[u8], bi0: usize) -> i32 {
    let av = |i: usize| -> u8 { a.get(i).copied().unwrap_or(0) };
    let bv = |i: usize| -> u8 { b.get(i).copied().unwrap_or(0) };
    let mut ai = ai0;
    let mut bi = bi0;
    let ca = av(ai);
    let cb = bv(bi);
    if ca == b'.' && cb == b'.' {
        loop {
            ai += 1;
            bi += 1;
            let x = av(ai);
            let y = bv(bi);
            if x != y {
                if is_digit(x) && is_digit(y) {
                    return (x as i32) - (y as i32);
                }
                if is_digit(x) {
                    // a_trailing_nonzero
                    while av(ai) == b'0' {
                        ai += 1;
                    }
                    return is_digit(av(ai)) as i32;
                }
                if is_digit(y) {
                    while bv(bi) == b'0' {
                        bi += 1;
                    }
                    return -(is_digit(bv(bi)) as i32);
                }
                return 0;
            }
            if !is_digit(x) {
                return 0;
            }
        }
    } else if ca == b'.' {
        // *a++ == decimal_point: skip zeros, a digit means a > b
        let mut j = ai + 1;
        while av(j) == b'0' {
            j += 1;
        }
        return is_digit(av(j)) as i32;
    } else if cb == b'.' {
        let mut j = bi + 1;
        while bv(j) == b'0' {
            j += 1;
        }
        return -(is_digit(bv(j)) as i32);
    }
    0
}

/// sort.c numcompare wrapper: skip leading blanks then strnumcmp.
fn numcompare_blanks(a: &[u8], b: &[u8]) -> i32 {
    let mut ia = 0;
    while ia < a.len() && is_blank(a[ia]) {
        ia += 1;
    }
    let mut ib = 0;
    while ib < b.len() && is_blank(b[ib]) {
        ib += 1;
    }
    strnumcmp(&a[ia..], &b[ib..])
}

// ---------------------------------------------------------------------
// -h: find_unit_order (unit suffix order) + strnumcmp tiebreak
// ---------------------------------------------------------------------

fn unit_order(c: u8) -> i32 {
    match c {
        b'K' | b'k' => 1,
        b'M' => 2,
        b'G' => 3,
        b'T' => 4,
        b'P' => 5,
        b'E' => 6,
        b'Z' => 7,
        b'Y' => 8,
        b'R' => 9,
        b'Q' => 10,
        _ => 0,
    }
}

/// traverse_raw_number: scan digits (and one fraction), return the highest
/// digit seen and the index of the byte that stopped the scan.
fn traverse_raw_number(s: &[u8]) -> (u8, usize) {
    let mut p = 0usize;
    let mut max_digit = 0u8;
    while let Some(&c) = s.get(p) {
        if !is_digit(c) {
            break;
        }
        if c > max_digit {
            max_digit = c;
        }
        p += 1;
    }
    if s.get(p) == Some(&b'.') {
        p += 1;
        while let Some(&c) = s.get(p) {
            if !is_digit(c) {
                break;
            }
            if c > max_digit {
                max_digit = c;
            }
            p += 1;
        }
    }
    (max_digit, p)
}

fn find_unit_order(s: &[u8]) -> i32 {
    let minus_sign = s.first() == Some(&b'-');
    let start = if minus_sign { 1 } else { 0 };
    let (max_digit, stop) = traverse_raw_number(&s[start..]);
    if max_digit > b'0' {
        let ch = s.get(start + stop).copied().unwrap_or(0);
        let order = unit_order(ch);
        if minus_sign {
            -order
        } else {
            order
        }
    } else {
        0
    }
}

fn human_numcompare(a: &[u8], b: &[u8]) -> i32 {
    let mut ia = 0;
    while ia < a.len() && is_blank(a[ia]) {
        ia += 1;
    }
    let mut ib = 0;
    while ib < b.len() && is_blank(b[ib]) {
        ib += 1;
    }
    let (sa, sb) = (&a[ia..], &b[ib..]);
    let diff = find_unit_order(sa) - find_unit_order(sb);
    if diff != 0 {
        diff.signum()
    } else {
        strnumcmp(sa, sb).signum()
    }
}

// ---------------------------------------------------------------------
// -g: strtold-shaped parse + general_numcompare
// ---------------------------------------------------------------------

#[derive(Clone, Copy, Debug, PartialEq)]
enum GNum {
    Num(f64),
    NanNeg,
    NanPos,
}

fn starts_ci(s: &[u8], at: usize, word: &[u8]) -> bool {
    s.len() >= at + word.len()
        && s[at..at + word.len()]
            .iter()
            .zip(word)
            .all(|(c, w)| fold_upper(*c) == fold_upper(*w))
}

fn hex_val(c: Option<u8>) -> Option<u32> {
    let c = c?;
    match c {
        b'0'..=b'9' => Some((c - b'0') as u32),
        b'a'..=b'f' => Some((c - b'a' + 10) as u32),
        b'A'..=b'F' => Some((c - b'A' + 10) as u32),
        _ => None,
    }
}

/// Longest strtold-valid prefix. None = conversion error (nothing parsed).
fn parse_general(s: &[u8]) -> Option<GNum> {
    let mut i = 0usize;
    while i < s.len() && c_isspace(s[i]) {
        i += 1;
    }
    let mut neg = false;
    if let Some(&c) = s.get(i) {
        if c == b'-' || c == b'+' {
            neg = c == b'-';
            i += 1;
        }
    }
    if starts_ci(s, i, b"infinity") || starts_ci(s, i, b"inf") {
        return Some(GNum::Num(if neg {
            f64::NEG_INFINITY
        } else {
            f64::INFINITY
        }));
    }
    if starts_ci(s, i, b"nan") {
        let mut j = i + 3;
        if s.get(j) == Some(&b'(') {
            if let Some(rel) = s[j + 1..].iter().position(|&c| c == b')') {
                j = j + 1 + rel + 1;
            }
        }
        return Some(if neg { GNum::NanNeg } else { GNum::NanPos });
    }
    // hex float: 0x hexdigits [. hexdigits] [p exponent]
    if s.get(i) == Some(&b'0') && matches!(s.get(i + 1).map(|c| c | 0x20), Some(b'x')) {
        let mut j = i + 2;
        if hex_val(s.get(j).copied()).is_some() {
            let mut mant = 0f64;
            let mut exp2 = 0i64;
            let mut seen_dot = false;
            while let Some(&c) = s.get(j) {
                if c == b'.' && !seen_dot {
                    seen_dot = true;
                    j += 1;
                    continue;
                }
                match hex_val(Some(c)) {
                    Some(v) => {
                        mant = mant * 16.0 + v as f64;
                        if seen_dot {
                            exp2 -= 4;
                        }
                        j += 1;
                    }
                    None => break,
                }
            }
            if matches!(s.get(j).map(|c| c | 0x20), Some(b'p')) {
                let mut k = j + 1;
                let mut eneg = false;
                if matches!(s.get(k), Some(&b'-')) {
                    eneg = true;
                    k += 1;
                } else if matches!(s.get(k), Some(&b'+')) {
                    k += 1;
                }
                let estart = k;
                while is_digit(s.get(k).copied().unwrap_or(0)) {
                    k += 1;
                }
                if k > estart {
                    let etext = std::str::from_utf8(&s[estart..k]).unwrap_or("0");
                    let ev: i64 = etext.parse().unwrap_or(4000);
                    exp2 += if eneg { -ev } else { ev };
                    j = k;
                }
            }
            let val = mant * 2f64.powi(exp2.clamp(-4000, 4000) as i32);
            return Some(GNum::Num(if neg { -val } else { val }));
        }
        // "0x" without hex digits parses as 0 (endptr at 'x')
        return Some(GNum::Num(0.0));
    }
    // decimal
    let start = i;
    let mut j = i;
    let mut ndig = 0usize;
    while is_digit(s.get(j).copied().unwrap_or(0)) {
        j += 1;
        ndig += 1;
    }
    if s.get(j) == Some(&b'.') {
        j += 1;
        while is_digit(s.get(j).copied().unwrap_or(0)) {
            j += 1;
            ndig += 1;
        }
    }
    if ndig == 0 {
        return None;
    }
    if matches!(s.get(j), Some(&b'e') | Some(&b'E')) {
        let mut k = j + 1;
        if matches!(s.get(k), Some(&b'-') | Some(&b'+')) {
            k += 1;
        }
        if is_digit(s.get(k).copied().unwrap_or(0)) {
            while is_digit(s.get(k).copied().unwrap_or(0)) {
                k += 1;
            }
            j = k;
        }
    }
    let text = std::str::from_utf8(&s[start..j]).ok()?;
    let val: f64 = text.parse().ok()?;
    Some(GNum::Num(if neg { -val } else { val }))
}

/// sort.c general_numcompare: conversion errors first, then NaNs
/// (-nan before nan), then numbers.
fn general_numcompare(sa: &[u8], sb: &[u8]) -> i32 {
    let va = parse_general(sa);
    let vb = parse_general(sb);
    match (va, vb) {
        (None, None) => 0,
        (None, Some(_)) => -1,
        (Some(_), None) => 1,
        (Some(GNum::NanNeg), Some(GNum::NanNeg)) | (Some(GNum::NanPos), Some(GNum::NanPos)) => 0,
        (Some(GNum::NanNeg), _) => -1,
        (Some(GNum::NanPos), Some(GNum::NanNeg)) => 1, // nan_compare: "nan" > "-nan"
        (Some(GNum::NanPos), Some(GNum::Num(_))) => -1,
        (Some(GNum::Num(_)), Some(GNum::NanNeg | GNum::NanPos)) => 1,
        (Some(GNum::Num(x)), Some(GNum::Num(y))) => {
            if x < y {
                -1
            } else if x > y {
                1
            } else {
                0
            }
        }
    }
}

// ---------------------------------------------------------------------
// -M: month table
// ---------------------------------------------------------------------

const MONTHS: [(&str, i32); 12] = [
    ("JAN", 1),
    ("FEB", 2),
    ("MAR", 3),
    ("APR", 4),
    ("MAY", 5),
    ("JUN", 6),
    ("JUL", 7),
    ("AUG", 8),
    ("SEP", 9),
    ("OCT", 10),
    ("NOV", 11),
    ("DEC", 12),
];

/// getmonth: skip blanks, match a 3-letter uppercase name as a prefix
/// (case-folded); unknown -> 0.
fn getmonth(s: &[u8]) -> i32 {
    let mut i = 0usize;
    while i < s.len() && is_blank(s[i]) {
        i += 1;
    }
    if s.len() - i >= 3 {
        for (name, val) in MONTHS {
            let nb = name.as_bytes();
            if fold_upper(s[i]) == nb[0]
                && fold_upper(s[i + 1]) == nb[1]
                && fold_upper(s[i + 2]) == nb[2]
            {
                return val;
            }
        }
    }
    0
}

// ---------------------------------------------------------------------
// -R: deterministic FNV-1a-64 hash (documented deviation from GNU's
// per-run MD5-seeded shuffle)
// ---------------------------------------------------------------------

fn fnv1a64(bytes: &[u8]) -> u64 {
    let mut h: u64 = 0xcbf29ce484222325;
    for &b in bytes {
        h ^= b as u64;
        h = h.wrapping_mul(0x100000001b3);
    }
    h
}

fn compare_random(a: &[u8], b: &[u8]) -> i32 {
    let ha = fnv1a64(a);
    let hb = fnv1a64(b);
    if ha != hb {
        return if ha < hb { -1 } else { 1 };
    }
    // collision fallback: byte compare then length (sort.c xfrm_diff)
    let n = a.len().min(b.len());
    match a[..n].cmp(&b[..n]) {
        std::cmp::Ordering::Less => -1,
        std::cmp::Ordering::Equal => (a.len() as i32) - (b.len() as i32),
        std::cmp::Ordering::Greater => 1,
    }
}

// ---------------------------------------------------------------------
// -V: filevercmp.c filenvercmp port
// ---------------------------------------------------------------------

/// file_prefixlen: suffix = (\.[A-Za-z~][A-Za-z0-9~]*)*$
fn file_prefixlen(s: &[u8]) -> usize {
    let n = s.len();
    let mut prefixlen = 0usize;
    let mut i = 0usize;
    loop {
        if i == n {
            return prefixlen;
        }
        i += 1;
        prefixlen = i;
        while i + 1 < n && s[i] == b'.' && (s[i + 1].is_ascii_alphabetic() || s[i + 1] == b'~') {
            i += 2;
            while i < n && (s[i].is_ascii_alphanumeric() || s[i] == b'~') {
                i += 1;
            }
        }
    }
}

/// order of one byte for verrevcmp; pos == len sorts before all non-'~'.
fn ver_order(s: &[u8], pos: usize, len: usize) -> i32 {
    if pos == len {
        return -1;
    }
    let c = s[pos];
    if c.is_ascii_digit() {
        0
    } else if c.is_ascii_alphabetic() {
        c as i32
    } else if c == b'~' {
        -2
    } else {
        c as i32 + 256
    }
}

/// verrevcmp (dpkg algorithm).
fn verrevcmp(s1: &[u8], s2: &[u8]) -> i32 {
    let (mut p1, mut p2) = (0usize, 0usize);
    while p1 < s1.len() || p2 < s2.len() {
        let mut first_diff = 0i32;
        while (p1 < s1.len() && !is_digit(s1[p1])) || (p2 < s2.len() && !is_digit(s2[p2])) {
            let c1 = ver_order(s1, p1, s1.len());
            let c2 = ver_order(s2, p2, s2.len());
            if c1 != c2 {
                return c1 - c2;
            }
            p1 += 1;
            p2 += 1;
        }
        while p1 < s1.len() && s1[p1] == b'0' {
            p1 += 1;
        }
        while p2 < s2.len() && s2[p2] == b'0' {
            p2 += 1;
        }
        while p1 < s1.len() && p2 < s2.len() && is_digit(s1[p1]) && is_digit(s2[p2]) {
            if first_diff == 0 {
                first_diff = (s1[p1] as i32) - (s2[p2] as i32);
            }
            p1 += 1;
            p2 += 1;
        }
        if p1 < s1.len() && is_digit(s1[p1]) {
            return 1;
        }
        if p2 < s2.len() && is_digit(s2[p2]) {
            return -1;
        }
        if first_diff != 0 {
            return first_diff.signum();
        }
    }
    0
}

fn filenvercmp(a: &[u8], b: &[u8]) -> i32 {
    // empty-version specials (filenvercmp: aempty -> -!bempty)
    if a.is_empty() {
        return -(!b.is_empty() as i32);
    }
    if b.is_empty() {
        return 1;
    }
    // leading "." specials: "." first, then "..", then other dot names
    if a[0] == b'.' {
        if b[0] != b'.' {
            return -1;
        }
        let adot = a.len() == 1;
        let bdot = b.len() == 1;
        if adot {
            return -(!bdot as i32);
        }
        if bdot {
            return 1;
        }
        let adotdot = a[1] == b'.' && a.len() == 2;
        let bdotdot = b[1] == b'.' && b.len() == 2;
        if adotdot {
            return -(!bdotdot as i32);
        }
        if bdotdot {
            return 1;
        }
    } else if b[0] == b'.' {
        return 1;
    }
    let ap = file_prefixlen(a);
    let bp = file_prefixlen(b);
    let one_pass = ap == a.len() && bp == b.len();
    let result = verrevcmp(&a[..ap], &b[..bp]);
    if result != 0 || one_pass {
        result.signum()
    } else {
        verrevcmp(a, b).signum()
    }
}

// ---------------------------------------------------------------------
// input / output
// ---------------------------------------------------------------------

/// GNU wording for an open/read failure on an input operand (mirrors
/// tac.rs's Windows recovery of the Linux errno shapes).
fn read_reason(ctx: &Ctx, file: &str, e: &std::io::Error) -> String {
    if file.is_empty() {
        return "No such file or directory".to_string();
    }
    if let Some(stripped) = file.strip_suffix('/') {
        let exists =
            !stripped.is_empty() && std::fs::metadata(fsutil::resolve(ctx, stripped)).is_ok();
        return if exists {
            "Not a directory".to_string()
        } else {
            "No such file or directory".to_string()
        };
    }
    let native = fsutil::resolve(ctx, file);
    if std::fs::metadata(&native)
        .map(|m| m.is_dir())
        .unwrap_or(false)
    {
        return "Is a directory".to_string();
    }
    if e.kind() == std::io::ErrorKind::InvalidInput {
        // Windows-invalid-name artifact for a target that does not exist:
        // GNU reports ENOENT because open(2) never accepts the name.
        return "No such file or directory".to_string();
    }
    fsutil::reason(e).to_string()
}

/// One input file appended to the buffer; a missing final eol is supplied
/// per file (sort.c fillbuf EOF behavior), which makes every line
/// end with the eol before splitting.
fn append_one_input(
    ctx: &Ctx,
    file: &str,
    io: &mut Io,
    buf: &mut Vec<u8>,
    eol: u8,
) -> Result<(), u32> {
    let start_len = buf.len();
    if file == "-" {
        if io.stdin.read_to_end(buf).is_err() {
            let _ = writeln!(io.stderr, "sort: read failed: -: Input/output error");
            return Err(2);
        }
    } else {
        let native = fsutil::resolve(ctx, file);
        if std::fs::metadata(&native)
            .map(|m| m.is_dir())
            .unwrap_or(false)
        {
            // Linux opens the directory fine and dies at the read
            let _ = writeln!(
                io.stderr,
                "sort: read failed: {}: Is a directory",
                fsutil::quotef(file)
            );
            return Err(2);
        }
        match std::fs::File::open(&native) {
            Ok(mut f) => {
                if f.read_to_end(buf).is_err() {
                    let _ = writeln!(
                        io.stderr,
                        "sort: read failed: {}: Input/output error",
                        fsutil::quotef(file)
                    );
                    return Err(2);
                }
            }
            Err(e) => {
                let reason = read_reason(ctx, file, &e);
                let _ = writeln!(
                    io.stderr,
                    "sort: open failed: {}: {}",
                    fsutil::quotef(file),
                    reason
                );
                return Err(2);
            }
        }
    }
    if buf.len() > start_len && buf.last() != Some(&eol) {
        buf.push(eol);
    }
    Ok(())
}

/// check_inputs port: every named input must be readable (euidaccess),
/// directories pass (they die later at the read).
fn check_inputs(cfg: &Cfg, io: &mut Io, ctx: &Ctx) -> Result<(), u32> {
    for file in &cfg.files {
        if file == "-" {
            continue;
        }
        if file.is_empty() {
            let _ = writeln!(
                io.stderr,
                "sort: cannot read: {}: No such file or directory",
                fsutil::quotef(file)
            );
            return Err(2);
        }
        if let Some(stripped) = file.strip_suffix('/') {
            let is_dir = !stripped.is_empty()
                && std::fs::metadata(fsutil::resolve(ctx, stripped))
                    .map(|m| m.is_dir())
                    .unwrap_or(false);
            if is_dir {
                continue; // access("/dir/", R_OK) succeeds
            }
            let exists = std::fs::metadata(fsutil::resolve(ctx, stripped)).is_ok();
            let reason = if exists {
                "Not a directory"
            } else {
                "No such file or directory"
            };
            let _ = writeln!(
                io.stderr,
                "sort: cannot read: {}: {}",
                fsutil::quotef(file),
                reason
            );
            return Err(2);
        }
        let native = fsutil::resolve(ctx, file);
        if std::fs::metadata(&native)
            .map(|m| m.is_dir())
            .unwrap_or(false)
        {
            continue; // R_OK passes on a directory
        }
        if let Err(e) = std::fs::File::open(&native) {
            let reason = read_reason(ctx, file, &e);
            let _ = writeln!(
                io.stderr,
                "sort: cannot read: {}: {}",
                fsutil::quotef(file),
                reason
            );
            return Err(2);
        }
    }
    Ok(())
}

/// check_output port: create-or-write WITHOUT truncating, like GNU's
/// early O_WRONLY|O_CREAT probe.
fn check_output(cfg: &Cfg, io: &mut Io, ctx: &Ctx) -> Result<(), u32> {
    let Some(out) = &cfg.outfile else {
        return Ok(());
    };
    if out.is_empty() {
        let _ = writeln!(
            io.stderr,
            "sort: open failed: {}: No such file or directory",
            fsutil::quotef(out)
        );
        return Err(2);
    }
    let native = fsutil::resolve(ctx, out);
    let probe = std::fs::OpenOptions::new()
        .write(true)
        .create(true)
        .open(&native);
    if probe.is_ok() {
        return Ok(());
    }
    let reason = if std::fs::metadata(&native)
        .map(|m| m.is_dir())
        .unwrap_or(false)
        || (out.ends_with('/') && std::fs::metadata(&native).is_ok())
    {
        "Is a directory".to_string()
    } else if out.ends_with('/') {
        "No such file or directory".to_string()
    } else {
        fsutil::reason(probe.as_ref().unwrap_err()).to_string()
    };
    let _ = writeln!(
        io.stderr,
        "sort: open failed: {}: {}",
        fsutil::quotef(out),
        reason
    );
    Err(2)
}

/// Split one file's byte range into lines (each INCLUDING its eol; the
/// readers already supplied any missing final eol).
fn split_lines_range(buf: &[u8], from: usize, to: usize, eol: u8, lines: &mut Vec<Ln>) {
    let end = to.min(buf.len());
    let mut p = from;
    while p < end {
        let lstart = p;
        while p < end && buf[p] != eol {
            p += 1;
        }
        if p < end {
            p += 1; // include the eol
        } else {
            // defensive: the readers append the eol, so this is unreachable
            // for well-formed ranges; treat the tail as a line anyway.
            break;
        }
        lines.push(Ln {
            start: lstart,
            len: p - lstart,
            kbeg: 0,
            klim: 0,
        });
    }
}

fn run_sort(cfg: &Cfg, io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut files: Vec<String> = cfg.files.clone();
    if files.is_empty() {
        files.push("-".to_string());
    }
    if let Err(s) = check_inputs(cfg, io, ctx) {
        return Ok(s);
    }
    if let Err(s) = check_output(cfg, io, ctx) {
        return Ok(s);
    }

    let mut buf: Vec<u8> = Vec::new();
    let mut file_byte_ranges: Vec<(usize, usize)> = Vec::new();
    for file in &files {
        let start = buf.len();
        if let Err(s) = append_one_input(ctx, file, io, &mut buf, cfg.eol) {
            return Ok(s);
        }
        file_byte_ranges.push((start, buf.len()));
    }

    let mut lines: Vec<Ln> = Vec::new();
    let mut file_line_ranges: Vec<(usize, usize)> = Vec::with_capacity(files.len());
    for &(from, to) in &file_byte_ranges {
        let start_idx = lines.len();
        split_lines_range(&buf, from, to, cfg.eol, &mut lines);
        file_line_ranges.push((start_idx, lines.len()));
    }

    // precompute the first key's spans (sort.c fillbuf precomputation)
    if let Some(k0) = cfg.keys.first() {
        for ln in lines.iter_mut() {
            precompute_key0(&buf, ln, k0, cfg.tab);
        }
    }

    let cmp = Cmp {
        buf: &buf,
        keys: &cfg.keys,
        tab: cfg.tab,
        unique: cfg.unique,
        stable: cfg.stable,
        reverse: cfg.reverse,
    };

    let result: Result<Vec<usize>, u32> = (|| {
        if cfg.mergeonly {
            // k-way merge, stable across files (earlier file wins ties)
            let mut merged: Vec<usize> = Vec::with_capacity(lines.len());
            let mut cur: Vec<usize> = file_line_ranges.iter().map(|r| r.0).collect();
            loop {
                let mut best: Option<usize> = None;
                for (fi, &pos) in cur.iter().enumerate() {
                    let (_, end) = file_line_ranges[fi];
                    if pos >= end {
                        continue;
                    }
                    match best {
                        None => best = Some(fi),
                        Some(bi) => {
                            if cmp.compare(&lines[pos], &lines[cur[bi]]) < 0 {
                                best = Some(fi);
                            }
                        }
                    }
                }
                match best {
                    None => break,
                    Some(fi) => {
                        merged.push(cur[fi]);
                        cur[fi] += 1;
                    }
                }
            }
            if cfg.unique {
                // mergefps: keep the first line of every equal-key run
                let mut deduped: Vec<usize> = Vec::with_capacity(merged.len());
                for (k, &idx) in merged.iter().enumerate() {
                    if k > 0 && cmp.compare(&lines[merged[k - 1]], &lines[idx]) == 0 {
                        continue;
                    }
                    deduped.push(idx);
                }
                return Ok(deduped);
            }
            return Ok(merged);
        }
        // stable sort (GNU's chunk sort + k-way merge is stable too)
        let mut order: Vec<usize> = (0..lines.len()).collect();
        order.sort_by(|&x, &y| {
            let d = cmp.compare(&lines[x], &lines[y]);
            d.cmp(&0).then(x.cmp(&y))
        });
        if cfg.unique {
            // keep the first line of every equal-key run
            let mut kept: Vec<usize> = Vec::with_capacity(order.len());
            for (k, &idx) in order.iter().enumerate() {
                if k > 0 && cmp.compare(&lines[order[k - 1]], &lines[idx]) == 0 {
                    continue;
                }
                kept.push(idx);
            }
            return Ok(kept);
        }
        Ok(order)
    })();

    let out = match result {
        Ok(v) => v,
        Err(s) => return Ok(s),
    };

    // write output (buffered: one write syscall per line is the dominant
    // cost at 1M+ lines — GNU fwrite enjoys the same stdio buffering)
    const OUT_BUF: usize = 1 << 20;
    let status = if let Some(outfile) = &cfg.outfile {
        let native = fsutil::resolve(ctx, outfile);
        match std::fs::OpenOptions::new()
            .write(true)
            .create(true)
            .truncate(true)
            .open(&native)
        {
            Ok(f) => {
                let mut w = std::io::BufWriter::with_capacity(OUT_BUF, f);
                let mut failed = false;
                for idx in &out {
                    let ln = &lines[*idx];
                    if w.write_all(&buf[ln.start..ln.start + ln.len]).is_err() {
                        failed = true;
                        break;
                    }
                }
                if w.flush().is_err() {
                    failed = true;
                }
                if failed {
                    let _ = writeln!(io.stderr, "sort: write failed: {}", fsutil::quotef(outfile));
                    2
                } else {
                    0
                }
            }
            Err(e) => {
                let reason = if std::fs::metadata(&native)
                    .map(|m| m.is_dir())
                    .unwrap_or(false)
                {
                    "Is a directory".to_string()
                } else {
                    fsutil::reason(&e).to_string()
                };
                let _ = writeln!(
                    io.stderr,
                    "sort: open failed: {}: {}",
                    fsutil::quotef(outfile),
                    reason
                );
                2
            }
        }
    } else {
        let mut w = std::io::BufWriter::with_capacity(OUT_BUF, &mut io.stdout);
        let mut failed = false;
        for idx in &out {
            let ln = &lines[*idx];
            if w.write_all(&buf[ln.start..ln.start + ln.len]).is_err() {
                failed = true;
                break;
            }
        }
        if w.flush().is_err() {
            failed = true;
        }
        if failed {
            let _ = writeln!(io.stderr, "sort: write failed: standard output");
            2
        } else {
            0
        }
    };
    Ok(status)
}

/// sort.c check(): single input, sequential scan; disorder (or duplicate
/// keys with -u) exits 1; 'c' prints the first offender.
fn run_check(cfg: &Cfg, io: &mut Io, ctx: &Ctx) -> CmdResult {
    let file = cfg
        .files
        .first()
        .cloned()
        .unwrap_or_else(|| "-".to_string());
    let mut buf: Vec<u8> = Vec::new();
    if let Err(s) = append_one_input(ctx, &file, io, &mut buf, cfg.eol) {
        return Ok(s);
    }

    let mut lines: Vec<Ln> = Vec::new();
    split_lines_range(&buf, 0, buf.len(), cfg.eol, &mut lines);
    if let Some(k0) = cfg.keys.first() {
        for ln in lines.iter_mut() {
            precompute_key0(&buf, ln, k0, cfg.tab);
        }
    }
    let cmp = Cmp {
        buf: &buf,
        keys: &cfg.keys,
        tab: cfg.tab,
        unique: cfg.unique,
        stable: cfg.stable,
        reverse: cfg.reverse,
    };
    let mut ordered = true;
    for i in 1..lines.len() {
        let d = cmp.compare(&lines[i - 1], &lines[i]);
        let disorder = if cfg.unique { d >= 0 } else { d > 0 };
        if disorder {
            if cfg.checkonly == Some('c') {
                // GNU: "sort: FILE:LINE: disorder: LINE" — the line bytes,
                // with the eol always present (fillbuf supplies it).
                let _ = write!(
                    io.stderr,
                    "sort: {}:{}: disorder: ",
                    fsutil::quotef(&file),
                    i + 1
                );
                let _ = io
                    .stderr
                    .write_all(&buf[lines[i].start..lines[i].start + lines[i].len]);
            }
            ordered = false;
            break;
        }
    }
    Ok(if ordered { 0 } else { 1 })
}

#[cfg(test)]
mod tests {
    use std::ffi::OsString;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str], stdin: &str) -> (u32, String, String) {
        let mut io = MemIo::new(stdin);
        let sc = ScratchCtx::new("sort");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::sort_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    fn run_files(args: &[&str], files: &[(&str, &str)], stdin: &str) -> (u32, String, String) {
        let sc = ScratchCtx::new("sortf");
        for (name, body) in files {
            std::fs::write(sc.path(name), body).unwrap();
        }
        let mut io = MemIo::new(stdin);
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::sort_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    fn expect(args: &[&str], stdin: &str, want: &str) {
        let (r, out, err) = run(args, stdin);
        assert_eq!(r, 0, "rc for {args:?} (stderr {err:?})");
        assert_eq!(out, want, "stdout for {args:?}");
    }

    // ---------- core default sort ----------

    #[test]
    fn default_byte_order() {
        expect(&[], "a b\na  c\nab\n", "a  c\na b\nab\n");
        expect(&[], "b\na\n", "a\nb\n");
        expect(&[], "", "");
        // missing trailing newline is supplied in output (probed p5)
        expect(&[], "b\na", "a\nb\n");
    }

    #[test]
    fn reverse_stable_unique() {
        expect(&["-r"], "b\na\n", "b\na\n");
        // -u dedups whole lines (the key IS the line)
        expect(&["-u"], "b\na\nB\na\nc\n", "B\na\nb\nc\n");
        // -s without keys still sorts whole lines (there is no key run
        // to stabilize; the last resort is the only comparison)
        expect(&["-s"], "b 2\na 2\na 1\n", "a 1\na 2\nb 2\n");
        // with keys, -s keeps input order for equal keys (probed p2)
        expect(&["-s", "-k1,1"], "b 2\na 2\na 1\n", "a 2\na 1\nb 2\n");
    }

    // ---------- field syntax (probed p1) ----------

    #[test]
    fn key_field_ranges() {
        let inp = "a b\na  c\nab\n";
        expect(&["-k2"], inp, "ab\na  c\na b\n");
        expect(&["-k2,2"], inp, "ab\na  c\na b\n");
        expect(&["-k2b,2b"], inp, "ab\na b\na  c\n");
        expect(&["-k1"], inp, "a  c\na b\nab\n");
        // leading blanks kept in the field without b (probed p1)
        expect(&["-k1"], "  x y\nz w\n q  r\n", "  x y\n q  r\nz w\n");
        expect(&["-k1b"], "  x y\nz w\n q  r\n", " q  r\n  x y\nz w\n");
    }

    #[test]
    fn key_char_positions() {
        expect(&["-k1.2"], "ab\naa\n", "aa\nab\n");
        // char position past field end -> empty key -> last resort
        expect(&["-k9,9"], "a\nb\n", "a\nb\n");
    }

    #[test]
    fn tab_separator() {
        expect(&["-t:", "-k2n"], "a:2\na:10\nb:1\n", "b:1\na:2\na:10\n");
        expect(&["-t:", "-k2,2"], "a:2\na:10\nb:1\n", "b:1\na:10\na:2\n");
        // leading separator creates an empty first field with -t
        expect(&["-t:", "-k1"], "::a\nb::c\n", "::a\nb::c\n");
    }

    #[test]
    fn key_errors() {
        let (r, _, err) = run(&["-k0"], "x\n");
        assert_eq!(r, 2);
        assert!(err.contains("field number is zero"));
        assert!(run(&["-k1.0"], "x\n")
            .2
            .contains("character offset is zero"));
        assert!(run(&["-k1,0"], "x\n").2.contains("field number is zero"));
        assert!(run(&["-k1x"], "x\n")
            .2
            .contains("stray character in field spec"));
        assert!(run(&["-k,"], "x\n")
            .2
            .contains("invalid count at start of ','"));
        assert!(run(&["-k-1"], "x\n")
            .2
            .contains("invalid count at start of '-1'"));
        // huge offsets clamp like GNU (probed p1: rc 0)
        expect(&["-k1.99999999999999999999"], "b\na\n", "a\nb\n");
        // plus sign accepted (xstrtoumax), probed p5
        expect(&["-k+1"], "x9\nx10\n", "x10\nx9\n");
        // leading whitespace tolerated (xstrtoumax), probed p4
        expect(&["-k 1"], "b\na\n", "a\nb\n");
    }

    #[test]
    fn tab_errors() {
        assert!(run(&["-t", ""], "x\n").2.contains("empty tab"));
        assert_eq!(run(&["-t", ""], "x\n").0, 2);
        assert!(run(&["-txx"], "x\n").2.contains("multi-character tab 'xx'"));
        assert!(run(&["-t:", "-t,"], "x\n").2.contains("incompatible tabs"));
    }

    #[test]
    fn obsolete_plus_pos() {
        expect(&["+0"], "5\n3\n", "3\n5\n");
        expect(&["+0n"], "x10\nx9\n", "x10\nx9\n");
        // +1 -1: empty key, last resort decides (probed p3)
        expect(&["+1", "-1"], "5 6\n3 4\n7\n", "3 4\n5 6\n7\n");
        // invalid +POS becomes a file -> cannot read rc 2
        let (r, _, err) = run(&["+0x"], "x\n");
        assert_eq!(r, 2);
        assert!(err.contains("cannot read: +0x:"));
        // bad -POS2 spec is fatal (probed p4)
        let (r, _, err) = run(&["+0", "-1x"], "a\n");
        assert_eq!(r, 2);
        assert!(err.contains("invalid field specification '-1x'"));
    }

    // ---------- numeric sorts ----------

    #[test]
    fn numeric_mixed() {
        expect(
            &["-n"],
            "10\n9\n100\n-1\n+5\n 7\n  3x\n\nabc\n",
            "-1\n\n+5\nabc\n  3x\n 7\n9\n10\n100\n",
        );
        expect(&["-n"], "1.5\n1.\n.5\n-.5\n", "-.5\n.5\n1.\n1.5\n");
        // '+' is not a sign for -n; '0x10' compares as 0 (probed p2)
        expect(&["-n"], "0x10\n2\n", "0x10\n2\n");
        // hex parses under -g (16 > 2)
        expect(&["-g"], "0x10\n2\n", "2\n0x10\n");
        // scientific: -n sees 1, -g sees 100
        expect(&["-n"], "1e2\n99\n", "1e2\n99\n");
        expect(&["-g"], "1e2\n99\n", "99\n1e2\n");
    }

    #[test]
    fn numeric_key_fields() {
        expect(&["-t:", "-k2n"], "a:2\na:10\nb:1\n", "b:1\na:2\na:10\n");
        // blanks skipped before the number (probed p2 " 7" position)
        expect(&["-n"], " 7\n3\n", "3\n 7\n");
    }

    #[test]
    fn general_numeric_nan_inf() {
        expect(
            &["-g"],
            "NaN\n1\nnan\n-inf\ninf\n",
            "NaN\nnan\n-inf\n1\ninf\n",
        );
        // conversion errors sort first
        expect(&["-g"], "abc\n2\n", "abc\n2\n");
        expect(&["-g"], "abc\nxyz\n", "abc\nxyz\n");
    }

    #[test]
    fn human_numeric() {
        // suffix order dominates: 2000 < 1K < 999M < 1G; negatives first;
        // Y/R/Q top out (probed p2)
        expect(
            &["-h"],
            "1K\n2000\n999M\n1G\n1023K\n-1M\n1R\n1Q\n1Y\n",
            "-1M\n2000\n1K\n1023K\n999M\n1G\n1Y\n1R\n1Q\n",
        );
        expect(&["-h"], " 2K\n1.5M\n", " 2K\n1.5M\n");
    }

    // ---------- month sort ----------

    #[test]
    fn month_sort() {
        // unknown keys all 0, last resort byte order among them
        expect(
            &["-M"],
            "dec\nJan\nJANX\napril\nmar\nfoooo\n\nmar\n",
            "\nfoooo\nJANX\nJan\nmar\nmar\napril\ndec\n",
        );
        // prefix matching: JUNE matches JUN
        expect(&["-M"], "JUNE\nmay\njuly\n", "may\nJUNE\njuly\n");
    }

    // ---------- dictionary / fold / nonprinting ----------

    #[test]
    fn dictionary_fold_ignore() {
        expect(&["-d"], "a1\nA2\nB!\nb\n", "A2\nB!\na1\nb\n");
        // fold: keys tie (A/a fold to A), last resort is raw byte order
        expect(&["-f"], "b\nA\na\nB\n", "A\na\nB\nb\n");
        expect(&["-i"], "ab\x01\naab\n", "aab\nab\x01\n");
        expect(&["-df"], "A!\na\n", "A!\na\n");
        // -nd incompatible (probed p4)
        assert_eq!(run(&["-nd"], "x\n").0, 2);
        assert!(run(&["-nd"], "x\n")
            .2
            .contains("options '-dn' are incompatible"));
        // -k1n -k2M is fine (per-key check), probed p4
        expect(&["-k1n", "-k2M"], "a b\n", "a b\n");
    }

    // ---------- random / version ----------

    #[test]
    fn random_sort_groups_keys() {
        // deterministic: same input twice -> same output
        let (r1, o1, _) = run(&["-R"], "b\na\na\nc\n");
        let (r2, o2, _) = run(&["-R"], "b\na\na\nc\n");
        assert_eq!(r1, 0);
        assert_eq!(r2, 0);
        assert_eq!(o1, o2);
        // identical keys adjacent in the output (hash order is free)
        let mut in_a = false;
        let mut a_block_done = false;
        let mut ok = true;
        for line in o1
            .as_bytes()
            .split(|&b| b == b'\n')
            .filter(|l| !l.is_empty())
        {
            if line == b"a" {
                if a_block_done {
                    ok = false;
                }
                in_a = true;
            } else if in_a {
                a_block_done = true;
            }
        }
        assert!(ok, "identical keys not grouped: {o1:?}");
    }

    #[test]
    fn version_sort() {
        expect(&["-V"], "a5\na1\na10\na2\n", "a1\na2\na5\na10\n");
        // Debian ~ rule: 1.0~ < 1.0 (probed WSL 9.4)
        expect(&["-V"], "1.0~\n1.0\n", "1.0~\n1.0\n");
    }

    // ---------- global inheritance ----------

    #[test]
    fn global_flag_inheritance() {
        // -n global: whole line is one numeric key
        expect(&["-n"], "5\n10\n", "5\n10\n");
        // global -b applies to a plain key
        expect(&["-b", "-k1"], "  a\na\n", "  a\na\n");
        // a key with flags does NOT inherit (GNU gotcha): -k2f ignores -n
        // and -b alike
        expect(&["-n", "-k2f"], "a B\nb a\n", "b a\na B\n");
        // -n inherited into a default key (field 1 = "a" for both: key
        // tie, last resort byte order, probed p6)
        expect(&["-n", "-k1,1"], "a 10\na 9\n", "a 10\na 9\n");
        // numeric inheritance decides when the key carries the number
        expect(&["-n", "-k1"], "10 a\n9 b\n", "9 b\n10 a\n");
    }

    // ---------- merge ----------

    #[test]
    fn merge_mode() {
        let (r, out, err) = run_files(
            &["-m", "m1", "m2"],
            &[("m1", "1\n3\n5\n"), ("m2", "2\n4\n6\n")],
            "",
        );
        assert_eq!(r, 0, "{err}");
        assert_eq!(out, "1\n2\n3\n4\n5\n6\n");
        // stable across files on ties
        let (r, out, _) = run_files(
            &["-m", "m1", "m2"],
            &[("m1", "a 1\nb 2\n"), ("m2", "a 0\nb 1\n")],
            "",
        );
        assert_eq!(r, 0);
        assert_eq!(out, "a 0\na 1\nb 1\nb 2\n");
        // merge with -u
        let (r, out, _) = run_files(
            &["-m", "-u", "m1", "m2"],
            &[("m1", "1\n1\n3\n"), ("m2", "1\n2\n")],
            "",
        );
        assert_eq!(r, 0);
        assert_eq!(out, "1\n2\n3\n");
        // stdin participates via "-"
        let (r, out, _) = run_files(&["-m", "-"], &[], "0\n9\n");
        assert_eq!(r, 0);
        assert_eq!(out, "0\n9\n");
    }

    // ---------- check mode ----------

    #[test]
    fn check_modes() {
        let (r, out, err) = run_files(&["-c", "ck"], &[("ck", "a\nb\nc\n")], "");
        assert_eq!(r, 0, "{err}");
        assert_eq!(out, "");
        let (r, _, err) = run_files(&["-c", "ck"], &[("ck", "a\nc\nb\n")], "");
        assert_eq!(r, 1);
        assert_eq!(err, "sort: ck:3: disorder: b\n");
        // -C is silent
        let (r, _, err) = run_files(&["-C", "ck"], &[("ck", "a\nc\nb\n")], "");
        assert_eq!(r, 1);
        assert_eq!(err, "");
        // duplicate keys fail with -u (probed p2)
        let (r, _, err) = run_files(&["-c", "-u", "ck"], &[("ck", "a\na\n")], "");
        assert_eq!(r, 1);
        assert_eq!(err, "sort: ck:2: disorder: a\n");
        // missing trailing eol still reported with eol appended (probed p5)
        let (r, _, err) = run_files(&["-c", "ck"], &[("ck", "b\na")], "");
        assert_eq!(r, 1);
        assert_eq!(err, "sort: ck:2: disorder: a\n");
        // -s: key-equal lines are fine
        let (r, _, _) = run_files(&["-c", "-s", "-k1,1", "ck"], &[("ck", "a 2\na 1\nb\n")], "");
        assert_eq!(r, 0);
        // -r checks descending order
        let (r, _, _) = run_files(&["-c", "-r", "ck"], &[("ck", "c\nb\na\n")], "");
        assert_eq!(r, 0);
        let (r, _, _) = run_files(&["-c", "-r", "ck"], &[("ck", "c\na\nb\n")], "");
        assert_eq!(r, 1);
        // stdin named "-"
        let (r, _, err) = run(&["-c"], "b\na\n");
        assert_eq!(r, 1);
        assert_eq!(err, "sort: -:2: disorder: a\n");
    }

    #[test]
    fn check_usage_errors() {
        // second operand rejected
        let (r, _, err) = run_files(&["-c", "c1", "c2"], &[("c1", "a\n"), ("c2", "a\n")], "");
        assert_eq!(r, 2);
        assert!(err.contains("extra operand 'c2' not allowed with -c"));
        // -c -o incompatible
        let (r, _, err) = run_files(&["-c", "-o", "out", "c1"], &[("c1", "a\n")], "");
        assert_eq!(r, 2);
        assert!(err.contains("options '-co' are incompatible"));
        // -cC incompatible
        assert!(run(&["-cC"], "x\n")
            .2
            .contains("options '-cC' are incompatible"));
        assert_eq!(run(&["-cc"], "a\n").0, 0);
        // missing file: open failed (probed p5)
        let (r, _, err) = run(&["-c", "nosuchfile"], "");
        assert_eq!(r, 2);
        assert!(err.contains("open failed: nosuchfile: No such file or directory"));
    }

    #[test]
    fn check_extra_operand_wording() {
        // GNU wording covered by the -c usage test above; here the -c -m
        // combination still checks (probed p4: rc 0 for sorted input)
        let (r, _, _) = run_files(&["-c", "-m", "ck"], &[("ck", "a\nb\n")], "");
        assert_eq!(r, 0);
    }

    // ---------- input / output plumbing ----------

    #[test]
    fn multiple_inputs_concatenate() {
        let (r, out, err) = run_files(&["f1", "f2"], &[("f1", "b\n"), ("f2", "a\n")], "");
        assert_eq!(r, 0, "{err}");
        assert_eq!(out, "a\nb\n");
        // first file without trailing eol: the fixup lands between files
        let (r, out, _) = run_files(&["f1", "f2"], &[("f1", "b"), ("f2", "a\n")], "");
        assert_eq!(r, 0);
        assert_eq!(out, "a\nb\n");
        // -o in-place is safe (all reads happen before the write)
        let sc = ScratchCtx::new("sorto");
        std::fs::write(sc.path("io1"), "b\na\n").unwrap();
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["-o", "io1", "io1"].iter().map(Into::into).collect();
        let r = super::sort_cmd(&owned, &mut io.split(), &sc.ctx()).unwrap();
        assert_eq!(r, 0);
        assert_eq!(std::fs::read_to_string(sc.path("io1")).unwrap(), "a\nb\n");
    }

    #[test]
    fn cannot_read_open_failed() {
        let (r, _, err) = run(&["nosuchfile"], "");
        assert_eq!(r, 2);
        assert!(err.contains("cannot read: nosuchfile: No such file or directory"));
        // directory operand dies at the read like GNU (probed p4)
        let sc = ScratchCtx::new("sortd");
        std::fs::create_dir(sc.path("sdir")).unwrap();
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["sdir"].iter().map(Into::into).collect();
        let r = super::sort_cmd(&owned, &mut io.split(), &sc.ctx()).unwrap();
        assert_eq!(r, 2);
        assert_eq!(io.err(), "sort: read failed: sdir: Is a directory\n");
    }

    #[test]
    fn zero_terminated() {
        expect(&["-z"], "b\0a\0", "a\0b\0");
        expect(&["-z"], "b\0a", "a\0b\0");
    }

    // ---------- option surface ----------

    #[test]
    fn help_version_surface() {
        let (r, out, err) = run(&["--help"], "");
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.starts_with("Usage: sort [OPTION]... [FILE]...\n"));
        assert!(out.ends_with("info '(coreutils) sort invocation'\n"));
        let (r, out, _) = run(&["--version"], "");
        assert_eq!(r, 0);
        assert!(out.starts_with("sort (GNU coreutils) 9.4\n"));
        assert!(out.ends_with("Written by Mike Haertel and Paul Eggert.\n"));
    }

    #[test]
    fn unknown_options() {
        let (r, _, err) = run(&["--bogus"], "");
        assert_eq!(r, 2);
        assert!(err.contains("unrecognized option '--bogus'"));
        assert!(err.contains("Try 'sort --help' for more information."));
        let (r, _, err) = run(&["-Q"], "");
        assert_eq!(r, 2);
        assert!(err.contains("invalid option -- 'Q'"));
        // ambiguous abbreviation lists the possibilities (probed p4)
        let (_, _, err) = run(&["--c"], "");
        assert!(err
            .contains("option '--c' is ambiguous; possibilities: '--check' '--compress-program'"));
    }

    #[test]
    fn sort_word_and_check_word() {
        expect(&["--sort=numeric"], "10\n9\n", "9\n10\n");
        expect(&["--sort=num"], "10\n9\n", "9\n10\n"); // prefix match
        expect(&["--sort=month"], "dec\njan\n", "jan\ndec\n");
        let (r, _, err) = run(&["--sort=bogus"], "x\n");
        assert_eq!(r, 1);
        assert!(err.contains("invalid argument 'bogus' for '--sort'"));
        assert!(err.contains("Valid arguments are:"));
        // --check with word argument
        let (r, _, _) = run_files(&["--check=silent", "ck"], &[("ck", "b\na\n")], "");
        assert_eq!(r, 1);
        let (r, _, err) = run_files(&["--check=diagnose-first", "ck"], &[("ck", "b\na\n")], "");
        assert_eq!(r, 1);
        assert!(err.contains("disorder"));
        // bare --check does not eat the next operand (the file is checked)
        let (r, _, err) = run_files(&["--check", "ck"], &[("ck", "b\na\n")], "");
        assert_eq!(r, 1);
        assert!(err.contains("disorder"));
        let (r, _, err) = run(&["--check=bogus"], "");
        assert_eq!(r, 1);
        assert!(err.contains("invalid argument 'bogus' for '--check'"));
        // empty prefix is "ambiguous argument" (probed p5)
        let (r, _, err) = run(&["--check="], "");
        assert_eq!(r, 1);
        assert!(err.contains("ambiguous argument '' for '--check'"));
    }

    #[test]
    fn buffer_size_and_parallel() {
        expect(&["-S100k"], "b\na\n", "a\nb\n");
        expect(&["-S0"], "b\na\n", "a\nb\n");
        let (r, _, err) = run(&["-Sx"], "x\n");
        assert_eq!(r, 2);
        assert!(err.contains("invalid -S argument 'x'"));
        let (r, _, err) = run(&["-S1X"], "x\n");
        assert_eq!(r, 2);
        assert!(err.contains("invalid suffix in -S argument '1X'"));
        let (r, _, err) = run(&["--parallel=0"], "x\n");
        assert_eq!(r, 2);
        assert!(err.contains("number in parallel must be nonzero"));
        let (r, _, err) = run(&["--parallel=x"], "x\n");
        assert_eq!(r, 2);
        assert!(err.contains("invalid --parallel argument 'x'"));
        expect(&["--parallel=4"], "b\na\n", "a\nb\n");
        expect(&["-T", "nope-dir"], "b\na\n", "a\nb\n");
        let (r, _, err) = run(&["--batch-size=0"], "x\n");
        assert_eq!(r, 2);
        assert!(err.contains("minimum --batch-size argument is '2'"));
    }

    #[test]
    fn output_conflicts() {
        let (r, _, err) = run(&["-o", "a", "-o", "b"], "");
        assert_eq!(r, 2);
        assert!(err.contains("multiple output files specified"));
        // same name twice is fine
        let sc = ScratchCtx::new("sortoo");
        std::fs::write(sc.path("same"), "b\na\n").unwrap();
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["-o", "same", "-o", "same", "same"]
            .iter()
            .map(Into::into)
            .collect();
        let r = super::sort_cmd(&owned, &mut io.split(), &sc.ctx()).unwrap();
        assert_eq!(r, 0);
        assert_eq!(std::fs::read_to_string(sc.path("same")).unwrap(), "a\nb\n");
    }

    #[test]
    fn random_source_validation() {
        // a random key validates the source eagerly (probed p4)
        let (r, _, err) = run(&["-R", "--random-source=nope-rs"], "b\na\n");
        assert_eq!(r, 2);
        assert!(err.contains("open failed: nope-rs: No such file or directory"));
        // no random key: the source is never touched
        expect(&["--random-source=nope-rs"], "b\na\n", "a\nb\n");
    }

    #[test]
    fn y_solaris_quirk() {
        // -y100 attached / -y 100 detached: ignored
        expect(&["-y100"], "b\na\n", "a\nb\n");
        expect(&["-y", "100"], "b\na\n", "a\nb\n");
        // detached non-digits become a file operand (probed p4)
        let (r, _, err) = run(&["-y", "zz"], "b\na\n");
        assert_eq!(r, 2);
        assert!(err.contains("cannot read: zz"));
    }

    // ---------- comparator unit edges ----------

    #[test]
    fn numcompare_edges() {
        use super::strnumcmp;
        let c = |a: &str, b: &str| strnumcmp(a.as_bytes(), b.as_bytes()).signum();
        assert_eq!(c("10", "9"), 1);
        assert_eq!(c("9", "10"), -1);
        assert_eq!(c("1.5", "1.5"), 0);
        assert_eq!(c("1.5", "1.4"), 1);
        assert_eq!(c(".5", "1"), -1);
        assert_eq!(c("-2", "1"), -1);
        assert_eq!(c("-20", "-2"), -1); // longer run = smaller (negative)
        assert_eq!(c("-0", "abc"), 0); // both zero-valued
        assert_eq!(c("abc", "7"), -1); // non-number vs number
        assert_eq!(c("+5", "abc"), 0); // '+' is not a sign
        assert_eq!(c("007", "8"), -1);
        assert_eq!(c("1.", "1.5"), -1);
        assert_eq!(c("0.5", ".5"), 0);
    }

    #[test]
    fn unit_order_edges() {
        use super::find_unit_order;
        assert_eq!(find_unit_order(b"2000"), 0);
        assert_eq!(find_unit_order(b"1K"), 1);
        assert_eq!(find_unit_order(b"-1M"), -2);
        assert_eq!(find_unit_order(b"1Q"), 10);
        assert_eq!(find_unit_order(b"abc"), 0); // no digit
        assert_eq!(find_unit_order(b"0"), 0);
        assert_eq!(find_unit_order(b"0.5"), 0); // max digit '0'
    }

    #[test]
    fn month_table_edges() {
        use super::getmonth;
        assert_eq!(getmonth(b"jan"), 1);
        assert_eq!(getmonth(b"  DEC"), 12);
        assert_eq!(getmonth(b"JANX"), 1);
        assert_eq!(getmonth(b"JA"), 0); // needs the full 3 letters
        assert_eq!(getmonth(b""), 0);
        assert_eq!(getmonth(b"foo"), 0);
    }

    #[test]
    fn version_compare_edges() {
        use super::filenvercmp;
        let c = |a: &str, b: &str| filenvercmp(a.as_bytes(), b.as_bytes());
        assert_eq!(c("a1", "a2"), -1);
        assert_eq!(c("a2", "a10"), -1);
        assert_eq!(c("", "x"), -1);
        assert_eq!(c("x", ""), 1);
        assert_eq!(c(".", "a"), -1);
        assert_eq!(c(".", ".."), -1);
        assert_eq!(c("..", ".a"), -1);
        assert_eq!(c("1.0~", "1.0"), -1); // Debian ~ rule
        assert_eq!(c("1.0", "1.0~"), 1);
    }

    #[test]
    fn parse_keyspec_edges() {
        // -k1 == -k1.1 (the sword sentinel kicks in for both)
        expect(&["-k1.1"], "b\na\n", "a\nb\n");
        // -k2.1 starts at field 2's first byte (leading blank included)
        expect(&["-k2.1"], "a b\na c\n", "a b\na c\n");
    }
}
