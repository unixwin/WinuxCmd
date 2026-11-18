//! Direct-invocation glob contract (1.x utils/wildcard.cppm glob_expand):
//! a wildcard operand expands over the named directory; '.'/'..' never
//! match; an unmatched pattern yields None so the caller keeps it literal
//! (the GNU glob default: unmatched patterns stay literal so diagnostics
//! stay useful). A literal existing path with no '[' is NOT globbed (the
//! 1.x literal-first rule). Shared by the applets that glob positional
//! operands (touch, stat, ls).
//!
//! Matching follows the bash/glibc pathname-expansion rules (FNM_PERIOD):
//! a leading '.' in a name is matched only by a literal '.' as the first
//! pattern character of that segment — '*', '?' and bracket expressions
//! (even ones containing '.') never match it. Patterns may carry
//! wildcards in intermediate segments (`d[12]\*.txt`), which recurse
//! per directory level.

use std::path::Path;

use crate::fs_win;
use winux_core::Ctx;

pub fn contains_wildcard(s: &str) -> bool {
    s.contains('*') || s.contains('?') || s.contains('[')
}

/// POSIX fnmatch subset for ONE path segment ('*', '?', '[class]' with
/// '!' negation and ranges; a ']' right after '[' or '[!' is a literal
/// class member). `leading` tracks whether the next name char is the
/// segment's first, where FNM_PERIOD applies.
fn wildcard_match(pat: &str, name: &str) -> bool {
    let p: Vec<char> = pat.chars().collect();
    let n: Vec<char> = name.chars().collect();
    fn m(p: &[char], n: &[char], leading: bool) -> bool {
        if p.is_empty() {
            return n.is_empty();
        }
        match p[0] {
            '*' => {
                if leading && !n.is_empty() && n[0] == '.' {
                    // '*' cannot consume the leading dot; matching empty
                    // still hands `leading` to the next pattern token.
                    return m(&p[1..], n, true);
                }
                let mut i = 0usize;
                loop {
                    if m(&p[1..], &n[i..], false) {
                        return true;
                    }
                    if i >= n.len() {
                        return false;
                    }
                    i += 1;
                }
            }
            '?' => {
                !n.is_empty() && !(leading && n[0] == '.') && m(&p[1..], &n[1..], false)
            }
            '[' => {
                if n.is_empty() || (leading && n[0] == '.') {
                    return false;
                }
                let c = n[0];
                let mut i = 1usize;
                let negated = if p.get(i) == Some(&'!') {
                    i += 1;
                    true
                } else {
                    false
                };
                // a ']' immediately after '[' or '[!' is a literal member
                if p.get(i) == Some(&']') {
                    i += 1;
                }
                let close = match p[i..].iter().position(|&x| x == ']') {
                    Some(j) => i + j,
                    // unterminated '[' is a literal, never a class
                    None => return false,
                };
                let body = &p[1 + negated as usize..close];
                let mut hit = false;
                let mut k = 0usize;
                while k < body.len() {
                    if k + 2 < body.len() && body[k + 1] == '-' && body[k + 2] != ']' {
                        if c >= body[k] && c <= body[k + 2] {
                            hit = true;
                        }
                        k += 3;
                    } else {
                        if c == body[k] {
                            hit = true;
                        }
                        k += 1;
                    }
                }
                if negated {
                    hit = !hit;
                }
                hit && m(&p[close + 1..], &n[1..], false)
            }
            c => !n.is_empty() && n[0] == c && m(&p[1..], &n[1..], false),
        }
    }
    m(&p, &n, true)
}

/// Split a pattern into its root prefix (drive `C:`, a leading
/// separator, or a UNC `\\server\share`) and the remaining segments.
fn split_glob_root(pattern: &str) -> (&str, &str) {
    let b = pattern.as_bytes();
    if b.len() >= 2 && b[0].is_ascii_alphabetic() && b[1] == b':' {
        if b.len() >= 3 && (b[2] == b'/' || b[2] == b'\\') {
            return (&pattern[..3], &pattern[3..]);
        }
        return (&pattern[..2], &pattern[2..]);
    }
    if pattern.starts_with("\\\\") || pattern.starts_with("//") {
        // UNC: \\server\share is the root — consume two more segments.
        let rest = &pattern[2..];
        if let Some(i) = rest.find(['/', '\\']) {
            let after_server = &rest[i + 1..];
            if let Some(j) = after_server.find(['/', '\\']) {
                return (&pattern[..2 + i + 1 + j + 1], &after_server[j + 1..]);
            }
        }
        return (pattern, "");
    }
    if b.first().is_some_and(|c| *c == b'/' || *c == b'\\') {
        return (&pattern[..1], &pattern[1..]);
    }
    ("", pattern)
}

fn join_display(disp: &str, name: &str) -> String {
    if disp.is_empty() {
        name.to_string()
    } else {
        format!("{disp}/{name}")
    }
}

/// Expand one segment level: `disp` is the display/fs spelling of the
/// directory to enumerate ("" = the context cwd), `segs` the remaining
/// pattern segments. Literal segments join directly — the filesystem
/// boundary resolves existence and case — while wildcard segments read
/// the directory and fnmatch each entry name.
fn expand_segment(ctx: &Ctx, disp: &str, segs: &[&str], out: &mut Vec<String>) {
    let seg = segs[0];
    let last = segs.len() == 1;
    if !contains_wildcard(seg) {
        let child = join_display(disp, seg);
        if last {
            if Path::new(&fs_win::api_path(ctx, &child)).exists() {
                out.push(child);
            }
        } else {
            expand_segment(ctx, &child, &segs[1..], out);
        }
        return;
    }
    let fsdir = if disp.is_empty() {
        ctx.cwd.clone()
    } else {
        std::path::PathBuf::from(fs_win::api_path(ctx, disp))
    };
    let Ok(entries) = std::fs::read_dir(&fsdir) else {
        return;
    };
    for entry in entries.flatten() {
        let name = entry.file_name().to_string_lossy().into_owned();
        if name == "." || name == ".." || !wildcard_match(seg, &name) {
            continue;
        }
        let child = join_display(disp, &name);
        if last {
            out.push(child);
        } else {
            expand_segment(ctx, &child, &segs[1..], out);
        }
    }
}

/// Direct-invocation glob contract (1.x utils glob_expand): a wildcard
/// operand expands over the named directory; '.'/'..' never match; an
/// unmatched pattern yields None so the caller keeps it literal (the GNU
/// glob default: unmatched patterns stay literal so diagnostics stay
/// useful). A literal existing path with no '[' is NOT globbed (the
/// 1.x literal-first rule).
pub fn glob_expand_in(ctx: &Ctx, pattern: &str) -> Option<Vec<String>> {
    if !contains_wildcard(pattern) {
        return None;
    }
    let has_class = pattern.contains('[');
    if !has_class && std::path::Path::new(&fs_win::api_path(ctx, pattern)).exists() {
        return None; // literal path wins
    }
    let (root, rest) = split_glob_root(pattern);
    let segs: Vec<&str> = rest
        .split(['/', '\\'])
        .filter(|s| !s.is_empty())
        .collect();
    if segs.is_empty() {
        return None;
    }
    let mut out = Vec::new();
    expand_segment(ctx, root, &segs, &mut out);
    if out.is_empty() {
        None
    } else {
        Some(out)
    }
}
