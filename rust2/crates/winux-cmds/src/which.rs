//! `which` — locate a command in PATH (#1488).
//!
//! Port of `src/commands/which.cpp`. Explicit `Io`/`Ctx`, zero globals.

use std::collections::HashSet;
use std::ffi::OsString;
use std::io::{IsTerminal, Write};
use std::path::{Component, Path, PathBuf};

use crate::argopt::{self, OptSpec};
use crate::fsutil;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('a'),
        long: "all",
        takes_value: false,
    },
    OptSpec {
        short: Some('i'),
        long: "read-alias",
        takes_value: false,
    },
    OptSpec {
        short: Some('v'),
        long: "version",
        takes_value: false,
    },
    OptSpec {
        short: Some('s'),
        long: "silent",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "quiet",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "skip-dot",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "skip-tilde",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "show-dot",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "show-tilde",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "tty-only",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "skip-alias",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "read-functions",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "skip-functions",
        takes_value: false,
    },
];

struct Config {
    all: bool,
    silent: bool,
    skip_dot: bool,
    skip_tilde: bool,
    show_dot: bool,
    show_tilde: bool,
    tty_only: bool,
    read_alias: bool,
    read_functions: bool,
    cwd: PathBuf,
    home: Option<PathBuf>,
    names: Vec<String>,
}

struct SearchDir {
    dir: PathBuf,
    starts_with_dot: bool,
    starts_with_tilde: bool,
}

fn lex_normalize(p: &Path) -> PathBuf {
    let mut out = PathBuf::new();
    for comp in p.components() {
        match comp {
            Component::CurDir => {}
            Component::ParentDir => {
                out.pop();
            }
            c => out.push(c.as_os_str()),
        }
    }
    if out.as_os_str().is_empty() {
        PathBuf::from(".")
    } else {
        out
    }
}

fn make_absolute_normalized(cwd: &Path, p: &Path) -> PathBuf {
    if p.is_absolute() {
        lex_normalize(p)
    } else {
        lex_normalize(&cwd.join(p))
    }
}

fn path_to_forward(p: &Path) -> String {
    let mut s = p.to_string_lossy().replace('\\', "/");
    if let Some(stripped) = s.strip_prefix("//?/") {
        s = stripped.to_owned();
    }
    s
}

fn ensure_trailing_slash(mut s: String) -> String {
    if !s.ends_with('/') {
        s.push('/');
    }
    s
}

fn normalize_win_shell_path(text: &str) -> String {
    let b = text.as_bytes();
    if b.len() >= 3 && (b[0] == b'/' || b[0] == b'\\') && b[1].is_ascii_alphabetic() && (b[2] == b'/' || b[2] == b'\\') {
        let drive = (b[1] as char).to_ascii_uppercase();
        return format!("{drive}:{}", &text[2..]);
    }
    if let Some(rest) = text.strip_prefix("/cygdrive/") {
        let rb = rest.as_bytes();
        if rb.len() >= 2 && rb[0].is_ascii_alphabetic() && (rb[1] == b'/' || rb[1] == b'\\') {
            let drive = (rb[0] as char).to_ascii_uppercase();
            return format!("{drive}:{}", &rest[1..]);
        }
    }
    text.to_owned()
}

fn env_str(ctx: &Ctx, key: &str) -> Option<String> {
    ctx.env.get(key).map(|v| v.to_string_lossy().into_owned())
}

fn get_home_directory(ctx: &Ctx) -> Option<PathBuf> {
    if let Some(h) = env_str(ctx, "HOME").filter(|s| !s.is_empty()) {
        return Some(make_absolute_normalized(&ctx.cwd, Path::new(&normalize_win_shell_path(&h))));
    }
    if let Some(p) = env_str(ctx, "USERPROFILE").filter(|s| !s.is_empty()) {
        return Some(make_absolute_normalized(&ctx.cwd, Path::new(&normalize_win_shell_path(&p))));
    }
    if let (Some(d), Some(p)) = (env_str(ctx, "HOMEDRIVE"), env_str(ctx, "HOMEPATH")) {
        if !d.is_empty() && !p.is_empty() {
            let joined = format!("{d}{p}");
            return Some(make_absolute_normalized(&ctx.cwd, Path::new(&normalize_win_shell_path(&joined))));
        }
    }
    None
}

fn expand_leading_tilde(text: &str, home: Option<&Path>) -> String {
    let Some(home) = home else {
        return text.to_owned();
    };
    if text == "~" {
        return path_to_forward(home);
    }
    if let Some(rest) = text.strip_prefix("~/").or_else(|| text.strip_prefix("~\\")) {
        return format!("{}{}", ensure_trailing_slash(path_to_forward(home)), rest);
    }
    text.to_owned()
}

fn get_path_entries(ctx: &Ctx) -> Vec<String> {
    let mut entries = vec![".".to_owned()];
    let sep = if cfg!(windows) { ';' } else { ':' };
    if let Some(path_env) = env_str(ctx, "PATH") {
        for entry in path_env.split(sep) {
            entries.push(if entry.is_empty() {
                ".".to_owned()
            } else {
                entry.to_owned()
            });
        }
    }
    entries
}

fn append_unique_ext(exts: &mut Vec<String>, raw: &str) {
    if raw.is_empty() {
        return;
    }
    let ext = if raw.starts_with('.') {
        raw.to_ascii_lowercase()
    } else {
        format!(".{}", raw.to_ascii_lowercase())
    };
    if !exts.contains(&ext) {
        exts.push(ext);
    }
}

fn get_pathext_entries(ctx: &Ctx) -> Vec<String> {
    let mut exts = Vec::new();
    let ext_env = env_str(ctx, "PATHEXT").unwrap_or_default();
    if ext_env.is_empty() {
        for ext in [".exe", ".ps1", ".bat", ".cmd", ".com"] {
            append_unique_ext(&mut exts, ext);
        }
    } else {
        for ext in ext_env.split(';') {
            append_unique_ext(&mut exts, ext);
        }
        if exts.is_empty() {
            for ext in [".exe", ".ps1", ".bat", ".cmd", ".com"] {
                append_unique_ext(&mut exts, ext);
            }
        }
    }
    for ext in [".exe", ".com", ".bat", ".cmd", ".ps1"] {
        append_unique_ext(&mut exts, ext);
    }
    exts
}

fn make_search_dir(entry: String, cfg: &Config) -> Option<SearchDir> {
    let entry = if entry.is_empty() { ".".to_owned() } else { entry };
    let tilde = entry.starts_with('~');
    if cfg.skip_tilde && tilde {
        return None;
    }
    let expanded = expand_leading_tilde(&entry, cfg.home.as_deref());
    let dir = PathBuf::from(normalize_win_shell_path(&expanded));
    if cfg.skip_dot && !dir.is_absolute() {
        return None;
    }
    Some(SearchDir {
        dir,
        starts_with_dot: expanded.starts_with('.'),
        starts_with_tilde: tilde,
    })
}

fn command_operand_search_dirs(operand: &str, cfg: &Config, ctx: &Ctx) -> Vec<SearchDir> {
    if !operand.contains(['/', '\\']) {
        return get_path_entries(ctx)
            .into_iter()
            .filter_map(|e| make_search_dir(e, cfg))
            .collect();
    }
    let norm = normalize_win_shell_path(operand);
    let op_path = Path::new(&norm);
    let raw_parent = match op_path.parent() {
        Some(p) if !p.as_os_str().is_empty() => path_to_forward(p),
        _ => ".".to_owned(),
    };
    let posix_abs = raw_parent.starts_with('/') || raw_parent.starts_with('\\');
    let parent_text = if op_path.is_absolute()
        || posix_abs
        || operand.starts_with('~')
        || operand.starts_with('.')
    {
        raw_parent
    } else {
        format!("./{raw_parent}")
    };
    let resolved_parent = fsutil::resolve(ctx, Path::new(&parent_text));
    let parent_str = path_to_forward(&resolved_parent);
    make_search_dir(parent_str, cfg).into_iter().collect()
}

fn with_extensions(base: &Path, exts: &[String]) -> Vec<PathBuf> {
    let mut out = Vec::new();
    if base.extension().is_some() {
        out.push(base.to_path_buf());
        return out;
    }
    let base_str = base.to_string_lossy();
    for ext in exts {
        out.push(PathBuf::from(format!("{base_str}{ext}")));
    }
    out.push(base.to_path_buf());
    out
}

fn exists_command_candidate(ctx: &Ctx, p: &Path) -> bool {
    let resolved = fsutil::resolve(ctx, p);
    std::fs::metadata(resolved).map(|m| m.is_file()).unwrap_or(false)
}

fn path_suffix_under(cwd: &Path, path: &Path, base: &Path) -> Option<String> {
    let path_text = path_to_forward(&make_absolute_normalized(cwd, path));
    let base_text = ensure_trailing_slash(path_to_forward(&make_absolute_normalized(cwd, base)));
    let path_key = path_text.to_ascii_lowercase();
    let base_key = base_text.to_ascii_lowercase();
    if !path_key.starts_with(&base_key) {
        return None;
    }
    Some(path_text[base_text.len()..].to_owned())
}

fn display_hit(candidate: &Path, dir: &SearchDir, cfg: &Config) -> Option<String> {
    let normalized = make_absolute_normalized(&cfg.cwd, candidate);
    if cfg.skip_tilde {
        if let Some(home) = &cfg.home {
            if path_suffix_under(&cfg.cwd, &normalized, home).is_some() {
                return None;
            }
        }
    }
    if cfg.show_dot && dir.starts_with_dot {
        if let Some(suffix) = path_suffix_under(&cfg.cwd, &normalized, &cfg.cwd) {
            if !suffix.is_empty() {
                return Some(format!("./{suffix}"));
            }
        }
    }
    if cfg.show_tilde {
        if let Some(home) = &cfg.home {
            if let Some(suffix) = path_suffix_under(&cfg.cwd, &normalized, home) {
                if !suffix.is_empty() {
                    return Some(format!("~/{suffix}"));
                }
            }
        }
    }
    Some(path_to_forward(&normalized))
}

fn find_one(name: &str, cfg: &Config, ctx: &Ctx) -> Vec<String> {
    let mut hits = Vec::new();
    let mut seen = HashSet::new();
    let pathext = get_pathext_entries(ctx);
    let basename = if name.contains(['/', '\\']) {
        Path::new(&normalize_win_shell_path(name))
            .file_name()
            .map(|s| s.to_string_lossy().into_owned())
            .unwrap_or_else(|| name.to_owned())
    } else {
        name.to_owned()
    };

    for dir in command_operand_search_dirs(name, cfg, ctx) {
        let base = dir.dir.join(&basename);
        for candidate in with_extensions(&base, &pathext) {
            if !exists_command_candidate(ctx, &candidate) {
                continue;
            }
            let Some(disp) = display_hit(&candidate, &dir, cfg) else {
                continue;
            };
            if seen.insert(disp.clone()) {
                hits.push(disp);
            }
            if !cfg.all {
                return hits;
            }
        }
    }
    hits
}

pub fn which_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("which", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(rc) => return Ok(rc),
    };
    if let Some(which) = p
        .flags
        .iter()
        .find(|f| f.as_str() == argopt::HELP || f.as_str() == argopt::VERSION)
    {
        if which == argopt::HELP {
            writeln!(
                io.stdout,
                "Usage: which [OPTION]... COMMAND...\nLocate COMMAND in PATH."
            )?;
        } else {
            writeln!(io.stdout, "which (WinuxCmd) 0.1.0")?;
        }
        return Ok(0);
    }

    let cfg = Config {
        all: p.has("all"),
        silent: p.has("silent") || p.has("quiet"),
        skip_dot: p.has("skip-dot"),
        skip_tilde: p.has("skip-tilde"),
        show_dot: p.has("show-dot"),
        show_tilde: p.has("show-tilde"),
        tty_only: p.has("tty-only"),
        read_alias: p.has("read-alias"),
        read_functions: p.has("read-functions"),
        cwd: lex_normalize(&ctx.cwd),
        home: get_home_directory(ctx),
        names: p
            .positionals
            .iter()
            .map(|a| a.to_string_lossy().into_owned())
            .collect(),
    };

    if cfg.names.is_empty() {
        writeln!(io.stderr, "which: missing command operand")?;
        return Ok(1);
    }
    if cfg.read_alias {
        writeln!(io.stderr, "which: --read-alias is not supported on Windows")?;
        return Ok(1);
    }
    if cfg.read_functions {
        writeln!(
            io.stderr,
            "which: --read-functions is not supported on Windows"
        )?;
        return Ok(1);
    }

    let suppress_tty = cfg.tty_only && !std::io::stdout().is_terminal();
    let mut all_found = true;
    for name in &cfg.names {
        let hits = find_one(name, &cfg, ctx);
        if hits.is_empty() {
            all_found = false;
            continue;
        }
        if !cfg.silent && !suppress_tty {
            for h in hits {
                writeln!(io.stdout, "{h}")?;
            }
        }
    }
    Ok(if all_found { 0 } else { 1 })
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn finds_first_and_all_matches() {
        let sc = ScratchCtx::new("which-find");
        std::fs::write(sc.path("a.exe"), b"").unwrap();
        std::fs::write(sc.path("a.cmd"), b"").unwrap();

        let mut io = MemIo::new("");
        let rc = which_cmd(&[OsString::from("a")], &mut io.split(), &sc.ctx()).unwrap();
        assert_eq!(rc, 0);
        assert!(io.out().contains("a.exe"));
        assert!(!io.out().contains("a.cmd"));

        let mut io_all = MemIo::new("");
        let rc_all = which_cmd(
            &[OsString::from("-a"), OsString::from("a")],
            &mut io_all.split(),
            &sc.ctx(),
        )
        .unwrap();
        assert_eq!(rc_all, 0);
        assert!(io_all.out().contains("a.exe"));
        assert!(io_all.out().contains("a.cmd"));
    }

    #[test]
    fn missing_command_returns_1() {
        let sc = ScratchCtx::new("which-miss");
        let mut io = MemIo::new("");
        let rc = which_cmd(&[OsString::from("absent")], &mut io.split(), &sc.ctx()).unwrap();
        assert_eq!(rc, 1);
    }
}
