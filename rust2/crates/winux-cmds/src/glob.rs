//! Direct-invocation glob contract (1.x utils/wildcard.cppm glob_expand):
//! a wildcard operand expands over the named directory; '.'/'..' never
//! match; an unmatched pattern yields None so the caller keeps it literal
//! (the GNU glob default: unmatched patterns stay literal so diagnostics
//! stay useful). A literal existing path with no '[' is NOT globbed (the
//! 1.x literal-first rule). Shared by the applets that glob positional
//! operands (touch, stat).

use std::path::Path;

use crate::fs_win;
use winux_core::Ctx;

pub fn contains_wildcard(s: &str) -> bool {
    s.contains('*') || s.contains('?') || s.contains('[')
}

/// POSIX fnmatch subset ('*', '?', '[class]' with '!' negation and
/// ranges; a leading '.' is not matched by '*' or '?', the GNU glob
/// default). Case-sensitive like the 1.x POSIX re-check.
fn wildcard_match(pat: &str, name: &str) -> bool {
    let p: Vec<char> = pat.chars().collect();
    let n: Vec<char> = name.chars().collect();
    fn m(p: &[char], n: &[char]) -> bool {
        if p.is_empty() {
            return n.is_empty();
        }
        match p[0] {
            '*' => {
                if n.is_empty() || n[0] == '.' {
                    return false; // leading dot is not matched by '*'
                }
                let mut i = 0usize;
                loop {
                    if m(&p[1..], &n[i..]) {
                        return true;
                    }
                    if i >= n.len() {
                        return false;
                    }
                    i += 1;
                }
            }
            '?' => !n.is_empty() && n[0] != '.' && m(&p[1..], &n[1..]),
            '[' => {
                if n.is_empty() {
                    return false;
                }
                let c = n[0];
                let close = match p[1..].iter().position(|&x| x == ']') {
                    Some(i) => i + 1,
                    None => return false,
                };
                let body = &p[1..close];
                let (negated, body) = match body.first() {
                    Some('!') => (true, &body[1..]),
                    _ => (false, body),
                };
                let mut hit = false;
                let mut i = 0usize;
                while i < body.len() {
                    if i + 2 < body.len() && body[i + 1] == '-' {
                        if c >= body[i] && c <= body[i + 2] {
                            hit = true;
                        }
                        i += 3;
                    } else {
                        if c == body[i] {
                            hit = true;
                        }
                        i += 1;
                    }
                }
                if negated {
                    hit = !hit;
                }
                hit && m(&p[close + 1..], &n[1..])
            }
            c => !n.is_empty() && n[0] == c && m(&p[1..], &n[1..]),
        }
    }
    m(&p, &n)
}

/// Direct-invocation glob contract (1.x utils glob_expand): a wildcard
/// operand expands over the named directory; '.'/'..' never match; an
/// unmatched pattern yields None so the caller keeps it literal (the GNU
/// glob default: unmatched patterns stay literal so diagnostics stay
/// useful). A literal existing path with no '[' is NOT globbed (the 1.x
/// literal-first rule).
pub fn glob_expand_in(ctx: &Ctx, pattern: &str) -> Option<Vec<String>> {
    if !contains_wildcard(pattern) {
        return None;
    }
    let has_class = pattern.contains('[');
    if !has_class && std::path::Path::new(&fs_win::api_path(ctx, pattern)).exists() {
        return None; // literal path wins
    }
    let (dir_part, leaf) = match pattern.rfind(['/', '\\']) {
        Some(i) => (&pattern[..i], &pattern[i + 1..]),
        None => ("", pattern),
    };
    // the directory prefix resolves through the shell-namespace boundary
    // like any operand (a "/tmp/*.txt" pattern globs the shell tmp view)
    let dir_mapped = winux_core::path::normalize_operand(ctx.env, &ctx.cwd, dir_part);
    let dir_path = if dir_part.is_empty() {
        ctx.cwd.to_path_buf()
    } else if dir_mapped != dir_part {
        std::path::PathBuf::from(dir_mapped)
    } else if Path::new(dir_part).is_absolute() {
        std::path::PathBuf::from(dir_part)
    } else {
        ctx.cwd.join(dir_part)
    };
    let mut out = Vec::new();
    let entries = std::fs::read_dir(&dir_path).ok()?;
    for entry in entries.flatten() {
        let name = entry.file_name();
        let name = name.to_string_lossy();
        if name == "." || name == ".." {
            continue;
        }
        if wildcard_match(leaf, &name) {
            if dir_part.is_empty() {
                out.push(name.into_owned());
            } else {
                out.push(format!("{dir_part}/{name}"));
            }
        }
    }
    if out.is_empty() {
        None
    } else {
        Some(out)
    }
}
