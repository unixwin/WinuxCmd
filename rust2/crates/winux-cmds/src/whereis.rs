//! `whereis` — locate the binary, source, and manual-page files for a command (#1488).
//!
//! Port of `src/commands/whereis.cpp` (util-linux whereis.c hand-loop argv
//! semantics with `-B`/`-M`/`-S`/`-f`, `-b`/`-m`/`-s` per-name mask, `-u`,
//! `-l`, `-g`). Explicit `Io`/`Ctx`, zero globals.

use std::collections::HashSet;
use std::ffi::OsString;
use std::io::Write;
use std::path::Path;

use crate::argopt;
use crate::fsutil;
use winux_core::{usage_error, CmdResult, Ctx, Io};

const WANT_BIN: u8 = 1;
const WANT_MAN: u8 = 2;
const WANT_SRC: u8 = 4;
const WANT_ALL: u8 = WANT_BIN | WANT_MAN | WANT_SRC;

struct NameQuery {
    name: String,
    want: u8,
}

struct Config {
    unusual: bool,
    glob: bool,
    list_paths: bool,
    bin_replaced: bool,
    man_replaced: bool,
    src_replaced: bool,
    bin_dirs: Vec<String>,
    man_dirs: Vec<String>,
    src_dirs: Vec<String>,
    names: Vec<NameQuery>,
}

#[derive(Copy, Clone, PartialEq, Eq)]
enum Sink {
    None,
    Bin,
    Man,
    Src,
}

fn split_env_path(list: &str) -> Vec<String> {
    let sep = if cfg!(windows) { ';' } else { ':' };
    list.split(sep)
        .filter(|s| !s.is_empty())
        .map(str::to_owned)
        .collect()
}

fn dedupe_paths(dirs: Vec<String>) -> Vec<String> {
    let mut seen = HashSet::new();
    let mut out = Vec::with_capacity(dirs.len());
    for p in dirs {
        let key = p.replace('\\', "/").to_ascii_lowercase();
        if seen.insert(key) {
            out.push(p);
        }
    }
    out
}

fn canonical_dir(ctx: &Ctx, dir: &str) -> String {
    let resolved = fsutil::resolve(ctx, Path::new(dir));
    match std::fs::canonicalize(&resolved) {
        Ok(c) => {
            let mut s = c.to_string_lossy().into_owned();
            if let Some(stripped) = s.strip_prefix(r"\\?\") {
                s = stripped.to_owned();
            }
            s.replace('\\', "/")
        }
        Err(_) => dir.to_owned(),
    }
}

fn default_bin_dirs(ctx: &Ctx) -> Vec<String> {
    let mut dirs = Vec::new();
    if let Some(p) = ctx.env.get("PATH") {
        dirs.extend(split_env_path(&p));
    }
    #[cfg(windows)]
    {
        for extra in [
            "C:/Windows/System32",
            "C:/Windows",
            "C:/Windows/SysWOW64",
            "C:/Program Files/WinuxCmd",
            "C:/Program Files (x86)/WinuxCmd",
        ] {
            if Path::new(extra).exists() {
                dirs.push(extra.to_owned());
            }
        }
    }
    dirs
}

fn default_man_dirs(ctx: &Ctx) -> Vec<String> {
    ctx.env.get("MANPATH")
        .map(|p| split_env_path(&p))
        .unwrap_or_default()
}

fn glob_match(pattern: &[u8], name: &[u8]) -> bool {
    if pattern.is_empty() {
        return name.is_empty();
    }
    if pattern[0] == b'*' {
        for skip in 0..=name.len() {
            if glob_match(&pattern[1..], &name[skip..]) {
                return true;
            }
        }
        return false;
    }
    if name.is_empty() {
        return false;
    }
    if pattern[0] != b'?' && !pattern[0].eq_ignore_ascii_case(&name[0]) {
        return false;
    }
    glob_match(&pattern[1..], &name[1..])
}

fn join_dir_file(dir: &str, file: &str) -> String {
    let mut full = dir.to_owned();
    if !full.is_empty() && !full.ends_with('/') && !full.ends_with('\\') {
        full.push('/');
    }
    full.push_str(file);
    full
}

fn join_hits(ctx: &Ctx, dir: &str, name: &str, exts: &[&str]) -> Vec<String> {
    let mut hits = Vec::new();
    for ext in exts {
        let candidate = join_dir_file(dir, &format!("{name}{ext}"));
        if fsutil::resolve(ctx, Path::new(&candidate)).exists() {
            hits.push(candidate);
        }
    }
    if !exts.contains(&"") {
        let exact = join_dir_file(dir, name);
        if fsutil::resolve(ctx, Path::new(&exact)).exists() {
            hits.push(exact);
        }
    }
    hits
}

fn glob_hits_in_dir(ctx: &Ctx, dir: &str, pattern: &str) -> Vec<String> {
    let mut hits = Vec::new();
    let base = fsutil::resolve(ctx, Path::new(dir));
    let Ok(entries) = std::fs::read_dir(base) else {
        return hits;
    };
    let pat_bytes = pattern.as_bytes();
    for entry in entries.flatten() {
        let Ok(ft) = entry.file_type() else {
            continue;
        };
        if !ft.is_file() {
            continue;
        }
        let filename = entry.file_name().to_string_lossy().into_owned();
        let stem = match filename.rfind('.') {
            Some(dot) if dot > 0 => &filename[..dot],
            _ => filename.as_str(),
        };
        if glob_match(pat_bytes, filename.as_bytes()) || glob_match(pat_bytes, stem.as_bytes()) {
            hits.push(join_dir_file(dir, &filename));
        }
    }
    hits
}

fn print_help(io: &mut Io) -> CmdResult {
    writeln!(io.stdout, "Usage: whereis [OPTION]... NAME...")?;
    writeln!(
        io.stdout,
        "Locate the binary, source, and manual-page files for NAME."
    )?;
    writeln!(io.stdout)?;
    writeln!(io.stdout, "  -b        search only for binaries")?;
    writeln!(io.stdout, "  -m        search only for manual sections")?;
    writeln!(io.stdout, "  -s        search only for sources")?;
    writeln!(
        io.stdout,
        "  -u        print names with other than exactly one entry"
    )?;
    writeln!(
        io.stdout,
        "  -B DIR... define binary lookup directories (ends with -f)"
    )?;
    writeln!(
        io.stdout,
        "  -M DIR... define manual lookup directories (ends with -f)"
    )?;
    writeln!(
        io.stdout,
        "  -S DIR... define source lookup directories (ends with -f)"
    )?;
    writeln!(io.stdout, "  -f        terminate the last directory list")?;
    writeln!(io.stdout, "  -l        list the effective lookup paths")?;
    writeln!(io.stdout, "  -g        search using glob patterns")?;
    Ok(0)
}

pub fn whereis_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut cfg = Config {
        unusual: false,
        glob: false,
        list_paths: false,
        bin_replaced: false,
        man_replaced: false,
        src_replaced: false,
        bin_dirs: Vec::new(),
        man_dirs: Vec::new(),
        src_dirs: Vec::new(),
        names: Vec::new(),
    };

    let mut want = WANT_ALL;
    let mut want_resetable = false;
    let mut f_missing = false;
    let mut sink = Sink::None;

    let mut apply_mask = |bit: u8| {
        if want_resetable {
            want = WANT_ALL;
            want_resetable = false;
        }
        want = if want == WANT_ALL { bit } else { want | bit };
        f_missing = false;
    };

    let mut add_name = |token: &str, w: u8| {
        let base = token
            .rsplit(['/', '\\'])
            .next()
            .unwrap_or(token)
            .to_owned();
        cfg.names.push(NameQuery { name: base, want: w });
    };

    let mut i = 0usize;
    while i < args.len() {
        let token = args[i].to_string_lossy();
        if sink != Sink::None && !(token.len() > 1 && token.starts_with('-')) {
            match sink {
                Sink::Bin => cfg.bin_dirs.push(token.into_owned()),
                Sink::Man => cfg.man_dirs.push(token.into_owned()),
                Sink::Src => cfg.src_dirs.push(token.into_owned()),
                Sink::None => {}
            }
            i += 1;
            continue;
        }
        sink = Sink::None;

        if token == "--" {
            for rest in &args[i + 1..] {
                add_name(&rest.to_string_lossy(), want);
                want_resetable = true;
            }
            break;
        }
        if token == "--help" || token == "-h" {
            return print_help(io);
        }
        if token == "--version" || token == "-V" {
            writeln!(io.stdout, "whereis (WinuxCmd) 1.0")?;
            return Ok(0);
        }
        if token.starts_with("--") {
            let _ = usage_error(ctx, io, "whereis", &argopt::unrecognized_option(ctx, &token));
            return Ok(1);
        }
        if token.len() > 1 && token.starts_with('-') {
            for ch in token[1..].chars() {
                match ch {
                    'B' => {
                        cfg.bin_replaced = true;
                        cfg.bin_dirs.clear();
                        sink = Sink::Bin;
                        f_missing = true;
                    }
                    'M' => {
                        cfg.man_replaced = true;
                        cfg.man_dirs.clear();
                        sink = Sink::Man;
                        f_missing = true;
                    }
                    'S' => {
                        cfg.src_replaced = true;
                        cfg.src_dirs.clear();
                        sink = Sink::Src;
                        f_missing = true;
                    }
                    'f' => f_missing = false,
                    'u' => {
                        cfg.unusual = true;
                        f_missing = false;
                    }
                    'l' => cfg.list_paths = true,
                    'g' => cfg.glob = true,
                    'b' => apply_mask(WANT_BIN),
                    'm' => apply_mask(WANT_MAN),
                    's' => apply_mask(WANT_SRC),
                    'h' => return print_help(io),
                    'V' => {
                        writeln!(io.stdout, "whereis (WinuxCmd) 1.0")?;
                        return Ok(0);
                    }
                    other => {
                        let _ = usage_error(
                            ctx,
                            io,
                            "whereis",
                            &argopt::invalid_option(ctx, &other.to_string()),
                        );
                        return Ok(1);
                    }
                }
                if sink != Sink::None {
                    break;
                }
            }
            i += 1;
            continue;
        }

        add_name(&token, want);
        want_resetable = true;
        i += 1;
    }

    if f_missing {
        writeln!(io.stderr, "whereis: option -f is missing")?;
        return Ok(1);
    }
    if cfg.names.is_empty() && !cfg.list_paths {
        writeln!(io.stderr, "whereis: missing operand")?;
        writeln!(io.stderr, "Try 'whereis --help' for more information.")?;
        return Ok(1);
    }

    let mut bin_dirs = if cfg.bin_replaced {
        cfg.bin_dirs
    } else {
        default_bin_dirs(ctx)
    };
    let mut man_dirs = if cfg.man_replaced {
        cfg.man_dirs
    } else {
        default_man_dirs(ctx)
    };
    let mut src_dirs = if cfg.src_replaced {
        cfg.src_dirs
    } else {
        Vec::new()
    };

    bin_dirs = dedupe_paths(
        bin_dirs
            .into_iter()
            .map(|d| canonical_dir(ctx, &d))
            .collect(),
    );
    man_dirs = dedupe_paths(
        man_dirs
            .into_iter()
            .map(|d| canonical_dir(ctx, &d))
            .collect(),
    );
    src_dirs = dedupe_paths(
        src_dirs
            .into_iter()
            .map(|d| canonical_dir(ctx, &d))
            .collect(),
    );

    if cfg.list_paths {
        for d in &bin_dirs {
            writeln!(io.stdout, "bin: {d}")?;
        }
        for d in &man_dirs {
            writeln!(io.stdout, "man: {d}")?;
        }
        for d in &src_dirs {
            writeln!(io.stdout, "src: {d}")?;
        }
        if cfg.names.is_empty() {
            return Ok(0);
        }
    }

    let bin_exts = [".exe", ".bat", ".cmd", ".com", ".ps1"];
    let man_exts = [".1", ".2", ".3", ".4", ".5", ".6", ".7", ".8", ".9", ""];
    let src_exts = [".c", ".cpp", ".cc", ".h", ".hpp", ".rs", ".go", ".py", ""];

    for query in &cfg.names {
        let mut bins = Vec::new();
        let mut mans = Vec::new();
        let mut srcs = Vec::new();

        let collect = |dirs: &[String], exts: &[&str], out: &mut Vec<String>| {
            for d in dirs {
                if cfg.glob {
                    out.extend(glob_hits_in_dir(ctx, d, &query.name));
                } else {
                    out.extend(join_hits(ctx, d, &query.name, exts));
                }
            }
        };

        if (query.want & WANT_BIN) != 0 {
            collect(&bin_dirs, &bin_exts, &mut bins);
        }
        if (query.want & WANT_MAN) != 0 {
            collect(&man_dirs, &man_exts, &mut mans);
        }
        if (query.want & WANT_SRC) != 0 {
            collect(&src_dirs, &src_exts, &mut srcs);
        }

        bins = dedupe_paths(bins);
        mans = dedupe_paths(mans);
        srcs = dedupe_paths(srcs);

        let total = bins.len() + mans.len() + srcs.len();
        if cfg.unusual && total <= 1 {
            continue;
        }

        write!(io.stdout, "{}:", query.name)?;
        for b in bins {
            write!(io.stdout, " {b}")?;
        }
        for m in mans {
            write!(io.stdout, " {m}")?;
        }
        for s in srcs {
            write!(io.stdout, " {s}")?;
        }
        writeln!(io.stdout)?;
    }
    Ok(0)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn missing_operand_and_missing_f() {
        let sc = ScratchCtx::new("whereis-err");
        let mut io = MemIo::new("");
        let rc = whereis_cmd(&[], &mut io.split(), &sc.ctx()).unwrap();
        assert_eq!(rc, 1);
        assert!(io.err().contains("missing operand"));

        let mut io2 = MemIo::new("");
        let rc2 = whereis_cmd(
            &[OsString::from("-B"), OsString::from("."), OsString::from("foo")],
            &mut io2.split(),
            &sc.ctx(),
        )
        .unwrap();
        assert_eq!(rc2, 1);
        assert!(io2.err().contains("option -f is missing"));
    }

    #[test]
    fn finds_binary_and_glob_with_custom_dir() {
        let sc = ScratchCtx::new("whereis-find");
        std::fs::write(sc.path("mytool.exe"), b"bin").unwrap();
        std::fs::write(sc.path("mytool.1"), b"man").unwrap();
        let dir_str = sc.root().to_string_lossy().into_owned();

        let mut io = MemIo::new("");
        let rc = whereis_cmd(
            &[
                OsString::from("-b"),
                OsString::from("-B"),
                OsString::from(&dir_str),
                OsString::from("-f"),
                OsString::from("mytool"),
            ],
            &mut io.split(),
            &sc.ctx(),
        )
        .unwrap();
        assert_eq!(rc, 0);
        let out = io.out();
        assert!(out.starts_with("mytool: "));
        assert!(out.contains("mytool.exe"));
        assert!(!out.contains("mytool.1"));

        let mut io_g = MemIo::new("");
        let rc_g = whereis_cmd(
            &[
                OsString::from("-g"),
                OsString::from("-B"),
                OsString::from(&dir_str),
                OsString::from("-f"),
                OsString::from("my*"),
            ],
            &mut io_g.split(),
            &sc.ctx(),
        )
        .unwrap();
        assert_eq!(rc_g, 0);
        assert!(io_g.out().contains("mytool.exe"));
    }
}
