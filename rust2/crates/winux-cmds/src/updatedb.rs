//! `updatedb` — update the filename database used by `locate` (#1488).
//!
//! Port of `src/commands/updatedb.cpp`. Explicit `Io`/`Ctx`, zero globals.

use std::ffi::OsString;
use std::io::Write;
use std::path::Path;

use crate::argopt::{self, OptSpec};
use crate::fsutil;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('d'),
        long: "directory",
        takes_value: true,
    },
    OptSpec {
        short: Some('e'),
        long: "exclude",
        takes_value: true,
    },
    OptSpec {
        short: Some('f'),
        long: "",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "localpaths",
        takes_value: true,
    },
    OptSpec {
        short: Some('o'),
        long: "output",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "prunepaths",
        takes_value: true,
    },
    OptSpec {
        short: Some('s'),
        long: "",
        takes_value: false,
    },
    OptSpec {
        short: Some('v'),
        long: "verbose",
        takes_value: false,
    },
    OptSpec {
        short: Some('x'),
        long: "exclude-dir",
        takes_value: true,
    },
];

fn normalize_stored(path: &str) -> String {
    let mut out = path.replace('\\', "/");
    if let Some(stripped) = out.strip_prefix("//?/") {
        out = stripped.to_owned();
    }
    while out.len() > 1 && out.ends_with('/') {
        out.pop();
    }
    out
}

fn split_semicolon_list(text: &str) -> Vec<String> {
    text.split(';')
        .filter(|s| !s.is_empty())
        .map(str::to_owned)
        .collect()
}

fn default_database(ctx: &Ctx) -> String {
    if let Some(val) = ctx.env.get("LOCALAPPDATA") {
        let s = val.to_string_lossy();
        if !s.is_empty() {
            return format!("{s}/WinuxCmd/locate.db");
        }
    }
    "locate.db".to_owned()
}

fn absolute_path(ctx: &Ctx, raw: &str) -> String {
    let resolved = fsutil::resolve(ctx, Path::new(raw));
    let mut s = resolved.to_string_lossy().into_owned();
    if let Some(stripped) = s.strip_prefix(r"\\?\") {
        s = stripped.to_owned();
    }
    s
}

fn is_pruned_path(stored: &str, pruned_prefixes: &[String]) -> bool {
    let stored_ci = if cfg!(windows) {
        stored.to_ascii_lowercase()
    } else {
        stored.to_owned()
    };
    for prefix in pruned_prefixes {
        let norm_raw = normalize_stored(prefix);
        let norm = if cfg!(windows) {
            norm_raw.to_ascii_lowercase()
        } else {
            norm_raw
        };
        if stored_ci == norm
            || (stored_ci.len() > norm.len()
                && stored_ci.starts_with(&norm)
                && stored_ci.as_bytes()[norm.len()] == b'/')
        {
            return true;
        }
    }
    false
}

fn wildcard_match_ci(pat: &[u8], text: &[u8]) -> bool {
    if pat.is_empty() {
        return text.is_empty();
    }
    if pat[0] == b'*' {
        for skip in 0..=text.len() {
            if wildcard_match_ci(&pat[1..], &text[skip..]) {
                return true;
            }
        }
        return false;
    }
    if text.is_empty() {
        return false;
    }
    if pat[0] != b'?' && !pat[0].eq_ignore_ascii_case(&text[0]) {
        return false;
    }
    wildcard_match_ci(&pat[1..], &text[1..])
}

fn walk_dir(
    dir: &Path,
    exclude: &str,
    exclude_dir: &str,
    pruned_prefixes: &[String],
    results: &mut Vec<String>,
) {
    let Ok(entries) = std::fs::read_dir(dir) else {
        return;
    };
    for entry in entries.flatten() {
        let name = entry.file_name().to_string_lossy().into_owned();
        if name == "." || name == ".." {
            continue;
        }
        let child = entry.path();
        let stored = normalize_stored(&child.to_string_lossy());
        if is_pruned_path(&stored, pruned_prefixes) {
            continue;
        }
        let Ok(meta) = std::fs::symlink_metadata(&child) else {
            continue;
        };
        let is_dir = meta.is_dir();
        let is_symlink = meta.file_type().is_symlink();

        if !exclude_dir.is_empty()
            && is_dir
            && wildcard_match_ci(exclude_dir.as_bytes(), name.as_bytes())
        {
            continue;
        }
        if !exclude.is_empty() && stored.contains(exclude) {
            continue;
        }

        results.push(stored);
        if is_dir && !is_symlink {
            walk_dir(&child, exclude, exclude_dir, pruned_prefixes, results);
        }
    }
}

fn write_database_atomically(ctx: &Ctx, output: &str, content: &str, io: &mut Io) -> bool {
    let out_path = fsutil::resolve(ctx, Path::new(output));
    if let Some(parent) = out_path.parent() {
        if !parent.as_os_str().is_empty() {
            let _ = std::fs::create_dir_all(parent);
        }
    }
    let tmp_path = out_path.with_extension("db.tmp");
    if let Err(_) = std::fs::write(&tmp_path, content.as_bytes()) {
        let _ = writeln!(io.stderr, "updatedb: cannot create database '{output}'");
        return false;
    }
    if let Err(_) = std::fs::rename(&tmp_path, &out_path) {
        let _ = std::fs::remove_file(&tmp_path);
        let _ = writeln!(io.stderr, "updatedb: failed to replace database '{output}'");
        return false;
    }
    true
}

pub fn updatedb_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("updatedb", args, OPTS, io, ctx, 1) {
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
                "Usage: updatedb [OPTION]...\nUpdate the filename database used by locate."
            )?;
        } else {
            writeln!(io.stdout, "updatedb (WinuxCmd) 0.2.0")?;
        }
        return Ok(0);
    }

    let directory = p.value_of("directory").unwrap_or(".");
    let output = p
        .value_of("output")
        .filter(|s| !s.is_empty())
        .map(str::to_owned)
        .unwrap_or_else(|| default_database(ctx));
    let localpaths = p.value_of("localpaths").unwrap_or("");
    let prunepaths = p.value_of("prunepaths").unwrap_or("");
    let exclude = p.value_of("exclude").unwrap_or("");
    let exclude_dir = p.value_of("exclude-dir").unwrap_or("");
    let verbose = p.has("verbose");

    let roots: Vec<String> = if !localpaths.is_empty() {
        split_semicolon_list(localpaths)
            .into_iter()
            .map(|r| absolute_path(ctx, &r))
            .collect()
    } else {
        vec![absolute_path(ctx, directory)]
    };

    let pruned_prefixes: Vec<String> = split_semicolon_list(prunepaths)
        .into_iter()
        .map(|p| normalize_stored(&p))
        .collect();

    let mut paths = Vec::new();
    for root in &roots {
        if verbose {
            writeln!(io.stdout, "Walking {root}...")?;
        }
        let norm_root = normalize_stored(root);
        if is_pruned_path(&norm_root, &pruned_prefixes) {
            continue;
        }
        let root_path = Path::new(root);
        let meta = match std::fs::symlink_metadata(root_path) {
            Ok(m) => m,
            Err(e) => {
                writeln!(io.stderr, "updatedb: {root}: {}", fsutil::reason(&e))?;
                return Ok(1);
            }
        };
        paths.push(norm_root);
        if meta.is_dir() {
            walk_dir(root_path, exclude, exclude_dir, &pruned_prefixes, &mut paths);
        }
    }

    if paths.is_empty() {
        writeln!(io.stderr, "updatedb: new database would be empty")?;
        return Ok(0);
    }

    if verbose {
        write!(io.stdout, "Found {} paths. ", paths.len())?;
    }

    paths.sort();
    let mut content = String::new();
    for p in &paths {
        content.push_str(p);
        content.push('\n');
    }

    if !write_database_atomically(ctx, &output, &content, io) {
        return Ok(1);
    }

    if verbose {
        writeln!(io.stdout, "Database written to {output}.")?;
    }
    Ok(0)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn writes_sorted_db_and_prunes() {
        let sc = ScratchCtx::new("updatedb-test");
        std::fs::create_dir_all(sc.path("docs")).unwrap();
        std::fs::create_dir_all(sc.path("node_modules/pkg")).unwrap();
        std::fs::write(sc.path("docs/readme.md"), b"x").unwrap();
        std::fs::write(sc.path("node_modules/pkg/index.js"), b"x").unwrap();
        std::fs::write(sc.path("top.txt"), b"x").unwrap();

        let mut io = MemIo::new("");
        let rc = updatedb_cmd(
            &[
                OsString::from("-x"),
                OsString::from("node_modules"),
                OsString::from("-d"),
                OsString::from("."),
                OsString::from("-o"),
                OsString::from("out.db"),
            ],
            &mut io.split(),
            &sc.ctx(),
        )
        .unwrap();
        assert_eq!(rc, 0);

        let body = std::fs::read_to_string(sc.path("out.db")).unwrap();
        assert!(body.contains("docs/readme.md"));
        assert!(body.contains("top.txt"));
        assert!(!body.contains("node_modules"));
    }
}
