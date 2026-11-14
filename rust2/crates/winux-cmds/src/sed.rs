//! `sed` — GNU sed 4.9 stream editor (WinuxCmd 2.0 rust2, #1488).
//!
//! A structural port of GNU sed 4.9 (`sed/sed.c`, `compile.c`,
//! `execute.c`, `regexp.c`, `debug.c`) on the std-only winux-regex
//! engine (`CompileOptions::sed`: glibc RE_SYNTAX_POSIX_* bits, `I`/`M`,
//! buffer anchors, leftmost-longest + backreferences). Oracle: WSL GNU
//! sed 4.9 under C.UTF-8.
//!
//! * Options are getopt_long-shaped and processed IN ORDER, exactly like
//!   sed.c: `-e`/`-f` compile when seen, so `-E` after `-e` does not
//!   affect it; `-s`/`-i` separate files; `-i[SUF]`/`--in-place[=SUF]`
//!   with `*` expansion; `-z`, `-u`, `-l N` (atoi), `--posix`, `--debug`,
//!   `--sandbox`, `--follow-symlinks`, `-b` (no-op), `COLS`,
//!   `POSIXLY_CORRECT`. Unknown/ambiguous options print the GNU getopt
//!   message plus the full usage on stderr, rc 1.
//! * Diagnostics are byte-exact with GNU: `sed: -e expression #N, char M:`
//!   / `sed: file F line L:` (rc 1), `sed: can't read X: ...` (rc 2 at the
//!   end), panics (`couldn't open file`, `can't find label`, `no input
//!   files`, `couldn't edit X: not a regular file`, the dfa
//!   `[:space:]` warning) rc 4.
//! * Windows specifics: data is bytes end to end (CRLF is data, no text
//!   mode); `-i` writes `sedXXXXXX` in the target's directory, renames
//!   the original to the backup name when a suffix is given, replaces the
//!   target and restores its file attributes (the 1.x C++ approach;
//!   read-only targets are unlocked for the replace and re-locked);
//!   `w /dev/stdout` / `w /dev/stderr` / `R /dev/stdin` / `r /dev/stdin`
//!   route to the injected handles; `e` / `s///e` run through `sh -c`
//!   when a POSIX shell resolves on PATH, else `cmd.exe /d /c`.
//! * Locale: multibyte (UTF-8) semantics unless LC_ALL / LC_CTYPE / LANG
//!   names a non-UTF-8 locale such as `C`/`POSIX` (then `y`, case
//!   conversion and `.` work on bytes, GNU's unibyte behavior). Unset
//!   locale variables mean UTF-8 (the Windows console default the
//!   product targets); the gate oracle runs C.UTF-8.
//! * Operands stay literal (no glob expansion), like GNU.

mod compile;
mod exec;

use std::ffi::OsString;

use winux_core::{CmdResult, Ctx, Io};

use compile::{Compiler, Posix};

/// A fatal diagnostic: full stderr line (without newline) and exit status.
#[derive(Clone, Debug)]
pub(crate) struct Fatal {
    pub msg: String,
    pub status: u32,
}

const USAGE: &str = "Usage: sed [OPTION]... {script-only-if-no-other-script} [input-file]...

  -n, --quiet, --silent
                 suppress automatic printing of pattern space
      --debug
                 annotate program execution
  -e script, --expression=script
                 add the script to the commands to be executed
  -f script-file, --file=script-file
                 add the contents of script-file to the commands to be executed
  --follow-symlinks
                 follow symlinks when processing in place
  -i[SUFFIX], --in-place[=SUFFIX]
                 edit files in place (makes backup if SUFFIX supplied)
  -l N, --line-length=N
                 specify the desired line-wrap length for the `l' command
  --posix
                 disable all GNU extensions.
  -E, -r, --regexp-extended
                 use extended regular expressions in the script
                 (for portability use POSIX -E).
  -s, --separate
                 consider files as separate rather than as a single,
                 continuous long stream.
      --sandbox
                 operate in sandbox mode (disable e/r/w commands).
  -u, --unbuffered
                 load minimal amounts of data from the input files and flush
                 the output buffers more often
  -z, --null-data
                 separate lines by NUL characters
      --help     display this help and exit
      --version  output version information and exit

If no -e, --expression, -f, or --file option is given, then the first
non-option argument is taken as the sed script to interpret.  All
remaining arguments are names of input files; if no input files are
specified, then the standard input is read.

GNU sed home page: <https://www.gnu.org/software/sed/>.
General help using GNU software: <https://www.gnu.org/gethelp/>.
";

const BUG_LINE: &str = "E-mail bug reports to: <bug-sed@gnu.org>.\n";

const VERSION: &str = "sed (GNU sed) 4.9
Packaged by WinuxCmd
Copyright (C) 2022 Free Software Foundation, Inc.
License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>.
This is free software: you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.

Written by Jay Fenlason, Tom Lord, Ken Pizzini,
Paolo Bonzini, Jim Meyering, and Assaf Gordon.

This sed program was built without SELinux support.

GNU sed home page: <https://www.gnu.org/software/sed/>.
General help using GNU software: <https://www.gnu.org/gethelp/>.
E-mail bug reports to: <bug-sed@gnu.org>.
";

/// getopt_long table (sed.c longopts order matters for the ambiguity
/// message): (name, has_arg 0/1/2, short code).
const LONGOPTS: &[(&str, u8, char)] = &[
    ("binary", 0, 'b'),
    ("regexp-extended", 0, 'r'),
    ("debug", 0, 'D'),
    ("expression", 1, 'e'),
    ("file", 1, 'f'),
    ("in-place", 2, 'i'),
    ("line-length", 1, 'l'),
    ("null-data", 0, 'z'),
    ("zero-terminated", 0, 'z'),
    ("quiet", 0, 'n'),
    ("posix", 0, 'p'),
    ("silent", 0, 'n'),
    ("sandbox", 0, 'S'),
    ("separate", 0, 's'),
    ("unbuffered", 0, 'u'),
    ("version", 0, 'v'),
    ("help", 0, 'h'),
    ("follow-symlinks", 0, 'F'),
];

/// SHORTOPTS "bsnrzuEe:f:l:i::V:" -> has_arg.
fn short_arg(c: char) -> Option<u8> {
    match c {
        'b' | 's' | 'n' | 'r' | 'z' | 'u' | 'E' => Some(0),
        'e' | 'f' | 'l' | 'V' => Some(1),
        'i' => Some(2),
        _ => None,
    }
}

/// C atoi.
fn atoi(s: &str) -> i64 {
    let t = s.trim_start_matches([' ', '\t', '\n', '\r', '\x0b', '\x0c']);
    let (neg, digits) = match t.as_bytes().first() {
        Some(b'-') => (true, &t[1..]),
        Some(b'+') => (false, &t[1..]),
        _ => (false, t),
    };
    let mut n: i64 = 0;
    for b in digits.bytes() {
        if !b.is_ascii_digit() {
            break;
        }
        n = n.wrapping_mul(10).wrapping_add((b - b'0') as i64);
    }
    let n = if neg { -n } else { n };
    n as i32 as i64
}

/// Multibyte (UTF-8) locale unless the effective LC_CTYPE names another.
fn utf8_locale(ctx: &Ctx) -> bool {
    for key in ["LC_ALL", "LC_CTYPE", "LANG"] {
        if let Some(v) = ctx.env.get(key) {
            let v = v.to_string_lossy().to_ascii_lowercase();
            if v.is_empty() {
                continue;
            }
            return v.contains("utf-8") || v.contains("utf8");
        }
    }
    true
}

enum Opt {
    Flag(char, Option<String>),
    Operand(String),
}

fn usage_error(io: &mut Io, msg: Option<String>) -> CmdResult {
    if let Some(m) = msg {
        let _ = writeln!(io.stderr, "{m}");
    }
    let _ = io.stderr.write_all(USAGE.as_bytes());
    Ok(1)
}

/// getopt_long over argv with GNU permutation (stops at the first operand
/// when POSIXLY_CORRECT is set). Returns the ordered option events and
/// operands, or a usage diagnostic.
fn getopt(args: &[String], posixly_correct: bool) -> Result<Vec<Opt>, String> {
    let mut out = Vec::new();
    let mut i = 0;
    let mut only_operands = false;
    while i < args.len() {
        let a = &args[i];
        i += 1;
        if only_operands || a == "-" || !a.starts_with('-') {
            out.push(Opt::Operand(a.clone()));
            if posixly_correct && !only_operands {
                only_operands = true;
            }
            continue;
        }
        if a == "--" {
            only_operands = true;
            continue;
        }
        if let Some(body) = a.strip_prefix("--") {
            let (name, val) = match body.find('=') {
                Some(p) => (&body[..p], Some(body[p + 1..].to_string())),
                None => (body, None),
            };
            let exact = LONGOPTS.iter().find(|(n, _, _)| *n == name);
            let entry = match exact {
                Some(e) => *e,
                None => {
                    let cands: Vec<&(&str, u8, char)> = LONGOPTS
                        .iter()
                        .filter(|(n, _, _)| n.starts_with(name))
                        .collect();
                    if cands.is_empty() {
                        return Err(format!("sed: unrecognized option '{a}'"));
                    }
                    let first = cands[0];
                    let ambiguous = cands.iter().any(|c| c.1 != first.1 || c.2 != first.2);
                    if ambiguous {
                        let mut m = format!(
                            "sed: option '{}' is ambiguous; possibilities:",
                            a.split('=').next().unwrap_or(a)
                        );
                        for c in &cands {
                            m.push_str(&format!(" '--{}'", c.0));
                        }
                        return Err(m);
                    }
                    *first
                }
            };
            let (lname, has_arg, code) = entry;
            match has_arg {
                0 => {
                    if val.is_some() {
                        return Err(format!("sed: option '--{lname}' doesn't allow an argument"));
                    }
                    out.push(Opt::Flag(code, None));
                }
                1 => {
                    let v = match val {
                        Some(v) => v,
                        None => {
                            if i < args.len() {
                                i += 1;
                                args[i - 1].clone()
                            } else {
                                return Err(format!(
                                    "sed: option '--{lname}' requires an argument"
                                ));
                            }
                        }
                    };
                    out.push(Opt::Flag(code, Some(v)));
                }
                _ => out.push(Opt::Flag(code, val)),
            }
            continue;
        }
        // short cluster
        let chars: Vec<char> = a[1..].chars().collect();
        let mut k = 0;
        while k < chars.len() {
            let c = chars[k];
            k += 1;
            match short_arg(c) {
                None => return Err(format!("sed: invalid option -- '{c}'")),
                Some(0) => out.push(Opt::Flag(c, None)),
                Some(1) => {
                    let rest: String = chars[k..].iter().collect();
                    let v = if !rest.is_empty() {
                        rest
                    } else if i < args.len() {
                        i += 1;
                        args[i - 1].clone()
                    } else {
                        return Err(format!("sed: option requires an argument -- '{c}'"));
                    };
                    out.push(Opt::Flag(c, Some(v)));
                    break;
                }
                _ => {
                    let rest: String = chars[k..].iter().collect();
                    out.push(Opt::Flag(
                        c,
                        if rest.is_empty() { None } else { Some(rest) },
                    ));
                    break;
                }
            }
        }
    }
    Ok(out)
}

pub fn sed_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let args: Vec<String> = args
        .iter()
        .map(|a| a.to_string_lossy().into_owned())
        .collect();
    let posixly_correct = ctx.env.get("POSIXLY_CORRECT").is_some();
    let opts = match getopt(&args, posixly_correct) {
        Ok(o) => o,
        Err(m) => return usage_error(io, Some(m)),
    };
    let utf8 = utf8_locale(ctx);
    let mut comp = Compiler::new(
        ctx,
        if posixly_correct {
            Posix::Correct
        } else {
            Posix::Extended
        },
        utf8,
    );
    let mut line_len: i64 = 70;
    if let Some(cols) = ctx.env.get("COLS") {
        let t = atoi(&cols.to_string_lossy());
        if t > 1 {
            line_len = t - 1;
        }
    }
    let mut separate = false;
    let mut in_place: Option<String> = None;
    let mut unbuffered = false;
    let mut follow_symlinks = false;
    let mut have_program = false;
    let mut operands: Vec<String> = Vec::new();

    macro_rules! fatal {
        ($e:expr) => {{
            let f: Fatal = $e;
            let _ = writeln!(io.stderr, "{}", f.msg);
            return Ok(f.status);
        }};
    }

    for o in opts {
        match o {
            Opt::Operand(s) => operands.push(s),
            Opt::Flag(c, val) => match c {
                'n' => comp.no_default_output = true,
                'e' => {
                    have_program = true;
                    if let Err(f) = comp.compile_string(val.unwrap_or_default().as_bytes()) {
                        fatal!(f);
                    }
                }
                'f' => {
                    have_program = true;
                    if let Err(f) = comp.compile_file(&val.unwrap_or_default(), &mut *io.stdin) {
                        fatal!(f);
                    }
                }
                'z' => comp.delim = 0,
                'F' => follow_symlinks = true,
                'i' => {
                    separate = true;
                    in_place = Some(match val {
                        None => "*".to_string(),
                        Some(v) if v.contains('*') => v,
                        Some(v) => format!("*{v}"),
                    });
                }
                'l' => line_len = atoi(&val.unwrap_or_default()),
                'p' => comp.posix = Posix::Basic,
                'b' => {}
                'E' | 'r' => comp.extended = true,
                's' => separate = true,
                'S' => comp.sandbox = true,
                'D' => comp.debug = true,
                'u' => unbuffered = true,
                'v' => {
                    io.stdout.write_all(VERSION.as_bytes())?;
                    return Ok(0);
                }
                'h' => {
                    io.stdout.write_all(USAGE.as_bytes())?;
                    io.stdout.write_all(BUG_LINE.as_bytes())?;
                    return Ok(0);
                }
                _ => return usage_error(io, None),
            },
        }
    }
    let mut operands = operands.into_iter();
    if !have_program {
        match operands.next() {
            Some(script) => {
                if let Err(f) = comp.compile_string(script.as_bytes()) {
                    fatal!(f);
                }
            }
            None => return usage_error(io, None),
        }
    }
    if let Err(f) = comp.check_final_program() {
        fatal!(f);
    }
    let mut files: Vec<String> = operands.collect();
    if comp.debug {
        let mut b = b"SED PROGRAM:\n".to_vec();
        let mut level = 1usize;
        for pc in 0..comp.prog.cmds.len() {
            exec::debug_print_command(&comp.prog, pc, &mut level, &mut b);
        }
        io.stdout.write_all(&b)?;
    }
    if files.is_empty() {
        if in_place.is_some() {
            let _ = writeln!(io.stderr, "sed: no input files");
            return Ok(4);
        }
        files.push("-".to_string());
    }
    let settings = exec::Settings {
        no_default_output: comp.no_default_output,
        separate,
        in_place,
        posix: comp.posix,
        delim: comp.delim,
        unbuffered,
        debug: comp.debug,
        line_len,
        follow_symlinks,
        utf8,
        no_regex: comp.no_regex_error(),
    };
    let prog = std::mem::take(&mut comp.prog);
    drop(comp);
    let mut ex = exec::Exec::new(
        ctx,
        prog,
        settings,
        files,
        &mut *io.stdin,
        &mut *io.stdout,
        &mut *io.stderr,
    );
    match ex.run() {
        Ok(st) => Ok(st),
        Err(f) => {
            drop(ex);
            let _ = writeln!(io.stderr, "{}", f.msg);
            Ok(f.status)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::sed_cmd;
    use winux_core::test_support::{os, MemIo, ScratchCtx};

    fn run(args: &[&str], input: &str) -> (u32, String, String) {
        let sc = ScratchCtx::new("sed");
        run_in(&sc, args, input)
    }

    fn run_in(sc: &ScratchCtx, args: &[&str], input: &str) -> (u32, String, String) {
        let mut io = MemIo::new(input);
        let ctx = sc.ctx();
        let rc = sed_cmd(&os(args), &mut io.split(), &ctx).unwrap();
        (rc, io.out(), io.err())
    }

    #[test]
    fn substitute_global_and_numbered() {
        assert_eq!(run(&["s/o/0/g"], "foo boo\n").1, "f00 b00\n");
        assert_eq!(run(&["s/a/X/2g"], "aaaa\n").1, "aXXX\n");
        assert_eq!(run(&["s/a*/x/g"], "baaac\n").1, "xbxcx\n");
    }

    #[test]
    fn case_conversion_follows_setup_replacement() {
        assert_eq!(
            run(&[r"s/\(hello\) \(world\)/\U\1\E \u\2/"], "hello world\n").1,
            "HELLO World\n"
        );
        assert_eq!(run(&[r"s/\(b\)\(a*\)/\u\L\2\1/"], "baaac\n").1, "aaabc\n");
        assert_eq!(run(&[r"s/\(\)\([a-z]\)/\u\1\2/"], "baaac\n").1, "Baaac\n");
    }

    #[test]
    fn missing_final_newline_is_preserved() {
        assert_eq!(run(&["p"], "a\nb").1, "a\na\nb\nb");
        assert_eq!(run(&["$a end"], "a").1, "a\nend\n");
        // x brings the hold's chomped state (line_copy/line_append rules)
        assert_eq!(run(&["x"], "a\nb").1, "\na\n");
        assert_eq!(run(&["G;G;l"], "x").1, "x\\n\\n$\nx\n\n\n");
    }

    #[test]
    fn gnu_n_at_eof_prints_unless_posixly_correct() {
        assert_eq!(run(&["N;s/\\n/-/"], "1\n2\n3\n").1, "1-2\n3\n");
        let mut sc = ScratchCtx::new("sed-pc");
        sc.env.set("POSIXLY_CORRECT", "1");
        assert_eq!(run_in(&sc, &["N;s/\\n/-/"], "1\n2\n3\n").1, "1-2\n");
    }

    #[test]
    fn parse_errors_are_gnu_exact() {
        let cases: &[(&[&str], &str)] = &[
            (
                &["s/a"],
                "sed: -e expression #1, char 3: unterminated `s' command\n",
            ),
            (&["{p"], "sed: -e expression #1, char 0: unmatched `{'\n"),
            (&["p;}"], "sed: -e expression #1, char 3: unexpected `}'\n"),
            (
                &["p x"],
                "sed: -e expression #1, char 3: extra characters after command\n",
            ),
            (&["1"], "sed: -e expression #1, char 1: missing command\n"),
            (
                &["s/a/\\3/"],
                "sed: -e expression #1, char 7: invalid reference \\3 on `s' command's RHS\n",
            ),
            (
                &["s/a/b/k"],
                "sed: -e expression #1, char 7: unknown option to `s'\n",
            ),
            (
                &["-e", "p", "-e", "s/\\(/x/"],
                "sed: -e expression #2, char 7: Unmatched ( or \\(\n",
            ),
        ];
        for (args, want) in cases {
            let (rc, out, err) = run(args, "a\n");
            assert_eq!((rc, out.as_str(), err.as_str()), (1, "", *want), "{args:?}");
        }
        let (rc, _, err) = run(&["b nowhere"], "a\n");
        assert_eq!(
            (rc, err.as_str()),
            (4, "sed: can't find label for jump to `nowhere'\n")
        );
        let (rc, _, err) = run(&["s/[:alpha:]/x/"], "a\n");
        assert_eq!(rc, 4);
        assert_eq!(
            err,
            "sed: character class syntax is [[:space:]], not [:space:]\n"
        );
    }

    #[test]
    fn script_file_errors_name_the_line() {
        let sc = ScratchCtx::new("sed-f");
        std::fs::write(sc.path("s.sed"), "p\n{\np\n").unwrap();
        let (rc, _, err) = run_in(&sc, &["-f", "s.sed"], "a\n");
        assert_eq!(
            (rc, err.as_str()),
            (1, "sed: file s.sed line 2: unmatched `{'\n")
        );
        let (rc, _, err) = run_in(&sc, &["-f", "nosuch"], "a\n");
        assert_eq!(rc, 4);
        assert_eq!(
            err,
            "sed: couldn't open file nosuch: No such file or directory\n"
        );
    }

    #[test]
    fn unreadable_input_is_rc2_after_processing() {
        let sc = ScratchCtx::new("sed-rd");
        std::fs::write(sc.path("f"), "x\n").unwrap();
        let (rc, out, err) = run_in(&sc, &["p", "nosuch", "f"], "");
        assert_eq!(rc, 2);
        assert_eq!(out, "x\nx\n");
        assert_eq!(err, "sed: can't read nosuch: No such file or directory\n");
        // q's exit status loses to the bad-input status
        assert_eq!(run_in(&sc, &["1q3", "nosuch", "f"], "").0, 2);
        assert_eq!(run_in(&sc, &["1q3", "f"], "").0, 3);
    }

    #[test]
    fn dollar_looks_ahead_across_files() {
        let sc = ScratchCtx::new("sed-last");
        std::fs::write(sc.path("f1"), "a\nb\n").unwrap();
        std::fs::write(sc.path("e"), "").unwrap();
        assert_eq!(run_in(&sc, &["-n", "$p", "f1", "e"], "").1, "b\n");
        assert_eq!(run_in(&sc, &["-s", "-n", "$=", "f1", "f1"], "").1, "2\n2\n");
    }

    #[test]
    fn in_place_with_backup_and_attributes() {
        let sc = ScratchCtx::new("sed-i");
        let f = sc.path("f");
        std::fs::write(&f, "ab\ncd\n").unwrap();
        let (rc, _, err) = run_in(&sc, &["-i.bak", "s/a/A/", "f"], "");
        assert_eq!((rc, err.as_str()), (0, ""));
        assert_eq!(std::fs::read(&f).unwrap(), b"Ab\ncd\n");
        assert_eq!(std::fs::read(sc.path("f.bak")).unwrap(), b"ab\ncd\n");
        // `*` expansion and no temp file left behind
        run_in(&sc, &["-iold_*", "s/c/C/", "f"], "");
        assert_eq!(std::fs::read(sc.path("old_f")).unwrap(), b"Ab\ncd\n");
        let leftovers: Vec<_> = std::fs::read_dir(&sc.dir)
            .unwrap()
            .filter_map(|e| e.ok())
            .filter(|e| e.file_name().to_string_lossy().starts_with("sed"))
            .collect();
        assert!(leftovers.is_empty());
        // read-only target: edited, and still read-only afterwards
        let mut perm = std::fs::metadata(&f).unwrap().permissions();
        perm.set_readonly(true);
        std::fs::set_permissions(&f, perm).unwrap();
        let (rc, _, _) = run_in(&sc, &["-i", "s/A/a/", "f"], "");
        assert_eq!(rc, 0);
        assert_eq!(std::fs::read(&f).unwrap(), b"ab\nCd\n");
        let perm = std::fs::metadata(&f).unwrap().permissions();
        assert!(perm.readonly());
        let mut perm = perm;
        #[allow(clippy::permissions_set_readonly_false)]
        perm.set_readonly(false);
        std::fs::set_permissions(&f, perm).unwrap();
    }

    #[test]
    fn in_place_requires_files_and_regular_files() {
        let sc = ScratchCtx::new("sed-i2");
        let (rc, _, err) = run_in(&sc, &["-i", "p"], "");
        assert_eq!((rc, err.as_str()), (4, "sed: no input files\n"));
        std::fs::create_dir(sc.path("d")).unwrap();
        let (rc, _, err) = run_in(&sc, &["-i", "p", "d"], "");
        assert_eq!(
            (rc, err.as_str()),
            (4, "sed: couldn't edit d: not a regular file\n")
        );
    }

    #[test]
    fn special_output_files_and_stdin_reads() {
        assert_eq!(run(&["-n", "2w /dev/stdout"], "a\nb\n").1, "b\n");
        let (_, out, err) = run(&["w /dev/stderr"], "a\n");
        assert_eq!((out.as_str(), err.as_str()), ("a\n", "a\n"));
    }

    #[test]
    fn debug_dump_matches_gnu_shape() {
        let (_, out, _) = run(&["--debug", "s/b/X/;2d"], "abc\n");
        assert_eq!(
            out,
            "SED PROGRAM:\n  s/b/X/\n  2 d\nINPUT:   'STDIN' line 1\nPATTERN: abc\n\
             COMMAND: s/b/X/\nMATCHED REGEX REGISTERS\n  regex[0] = 1-2 'b'\n\
             PATTERN: aXc\nCOMMAND: 2 d\nEND-OF-CYCLE:\naXc\n"
        );
    }

    #[test]
    fn options_are_compiled_in_order() {
        // -E after -e does not reach the earlier expression (sed.c order).
        assert_eq!(run(&["-e", "s/a+/X/", "-E"], "a+b\n").1, "Xb\n");
        assert_eq!(run(&["-E", "-e", "s/a+/X/"], "aab\n").1, "Xb\n");
        // -in is -i with suffix "n", not -i -n.
        let sc = ScratchCtx::new("sed-in");
        std::fs::write(sc.path("f"), "x\n").unwrap();
        run_in(&sc, &["-in", "p", "f"], "");
        assert_eq!(std::fs::read(sc.path("fn")).unwrap(), b"x\n");
        let (rc, _, err) = run(&["--s", "p"], "");
        assert_eq!(rc, 1);
        assert!(err.starts_with(
            "sed: option '--s' is ambiguous; possibilities: '--silent' '--sandbox' '--separate'\n"
        ));
    }

    #[test]
    fn addresses_ranges_and_steps() {
        let seq: String = (1..=10).map(|i| format!("{i}\n")).collect();
        assert_eq!(run(&["-n", "0~3p"], &seq).1, "3\n6\n9\n");
        assert_eq!(run(&["-n", "2,~4p"], &seq).1, "2\n3\n4\n");
        assert_eq!(run(&["-n", "4,~4p"], &seq).1, "4\n5\n6\n7\n8\n");
        assert_eq!(run(&["-n", "/4/,+2p"], &seq).1, "4\n5\n6\n");
        assert_eq!(run(&["-n", "0,/1/p"], &seq).1, "1\n");
        assert_eq!(run(&["-n", "1,/1/p"], &seq).1, seq.as_str());
        assert_eq!(run(&["-n", "5,3p"], &seq).1, "5\n");
    }

    #[test]
    fn address_regex_dfa_filter_agrees_with_interpreter() {
        // The address line filter (winux-regex dfa) must never change a
        // verdict: dot patterns (DFA, ValidUtf8 requirement), anchored /
        // word / class shapes, backreferences (DFA declines), `-i`, `-z`,
        // and the pattern spaces the line model cannot express (an `N`-
        // joined text contains the eol byte; `M` anchors match around it) —
        // all must still equal GNU sed 4.9.
        let t1 = "the quick\nno match\nlorem ipsum dolor sit amet\nfox and dog\nlorem x amet\n";
        assert_eq!(
            run(&["-n", "/lorem.*amet/p"], t1).1,
            "lorem ipsum dolor sit amet\nlorem x amet\n"
        );
        assert_eq!(run(&["-n", "/^the/p"], t1).1, "the quick\n");
        assert_eq!(run(&["-n", "/\\<fox\\>/p"], t1).1, "fox and dog\n");
        assert_eq!(run(&["-n", "/\\(a\\)\\1/p"], "aa\nab\n").1, "aa\n");
        assert_eq!(run(&["-n", "/ab/Ip"], "AB\nab\n").1, "AB\nab\n");
        assert_eq!(run(&["-n", "/中./p"], "x\n中a\n中\n").1, "中a\n");
        assert_eq!(run(&["-n", "/^é.*a$/p"], "é中a\nxx\n").1, "é中a\n");
        // `-z`: the record delimiter is NUL, so the DFA is built with eol=0.
        assert_eq!(run(&["-zn", "/b/p"], "a\0b\0c\0").1, "b\0");
        // `N`-joined pattern space: the text contains the eol byte and must
        // go to the interpreter.
        assert_eq!(run(&["-n", "N;/a[^x]b/p"], "a\nb\n").1, "a\nb\n");
        // `M` anchors are outside the line model (DFA declines).
        assert_eq!(run(&["-n", "N;/^b/Mp"], "a\nb\n").1, "a\nb\n");
        assert_eq!(run(&["-n", "N;/b$/Mp"], "a\nb\n").1, "a\nb\n");
        assert_eq!(run(&["-n", "N;/a.b/Mp"], "a\nb\n").1, "");
    }

    #[test]
    fn y_and_multibyte() {
        assert_eq!(run(&["y/abc/xyz/"], "aabbcc\n").1, "xxyyzz\n");
        assert_eq!(run(&["y/é/e/"], "héé\n").1, "hee\n");
        assert_eq!(run(&["s/.*/\\U&/"], "éa\n").1, "ÉA\n");
        let (rc, _, err) = run(&["y/abc/xy/"], "a\n");
        assert_eq!(rc, 1);
        assert_eq!(
            err,
            "sed: -e expression #1, char 9: strings for `y' command are different lengths\n"
        );
    }
}
