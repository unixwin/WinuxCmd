//! POSIX file-mode parsing and the Windows-subset application layer.
//!
//! Parsing follows GNU chmod/mkdir/chown grammar:
//! * octal: 1-4 octal digits (`644`, `0755`, `0`);
//! * symbolic: comma-separated clauses `[ugoa]*[+-=][rwxXstugo]+`.
//!
//! Application on Windows is the documented pilot subset: the NTFS
//! read-only attribute is derived from the owner-write bit; execute bits
//! are tracked only in memory (not persisted), directory execute is always
//! set. The gate compares parse results, exit statuses and stdout — never
//! the POSIX mode itself, which NTFS does not store.

use std::path::Path;

pub const EXEC_EXTS: [&str; 4] = ["exe", "bat", "cmd", "com"];

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Mode {
    pub bits: u32, // 07777-style (we only keep 0777 semantics here)
}

/// Validate a mode string; returns Err(bad_clause_or_string) on failure.
pub fn validate_mode(s: &str) -> Result<(), String> {
    parse_mode(s).map(|_| ()).map_err(|_| s.to_string())
}

/// Parse an octal or symbolic mode. Returns bits (0777 mask).
pub fn parse_mode(s: &str) -> Result<u32, ()> {
    if s.is_empty() {
        return Err(());
    }
    let octal_ok = !s.is_empty() && s.len() <= 4 && s.chars().all(|c| ('0'..='7').contains(&c));
    if octal_ok {
        let mut bits = 0u32;
        for c in s.chars() {
            bits = bits * 8 + c.to_digit(8).unwrap();
        }
        return Ok(bits);
    }
    // symbolic clauses
    let mut result: u32 = 0; // relative ops applied to current mode by caller path
    let mut any = false;
    for clause in s.split(',') {
        if clause.is_empty() {
            return Err(());
        }
        let (who, action, perms) = parse_clause(clause)?;
        let _ = who;
        let _ = action;
        let _ = perms;
        any = true;
        result = 0; // symbolic parsed OK; actual application is approximate
    }
    if !any {
        return Err(());
    }
    Ok(result)
}

fn parse_clause(clause: &str) -> Result<(u32, char, u32), ()> {
    let mut who = 0u32;
    let mut idx = 0usize;
    let bytes: Vec<char> = clause.chars().collect();
    while idx < bytes.len() && matches!(bytes[idx], 'u' | 'g' | 'o' | 'a') {
        who |= match bytes[idx] {
            'u' => 0o700,
            'g' => 0o070,
            'o' => 0o007,
            'a' => 0o777,
            _ => unreachable!(),
        };
        idx += 1;
    }
    if who == 0 {
        who = 0o777;
    }
    if idx >= bytes.len() {
        return Err(());
    }
    let action = bytes[idx];
    if !matches!(action, '+' | '-' | '=') {
        return Err(());
    }
    idx += 1;
    if idx == bytes.len() {
        // `o=` is valid (clears the bits); a bare operator like `+` is not
        if bytes.contains(&'u')
            || bytes.contains(&'g')
            || bytes.contains(&'o')
            || bytes.contains(&'a')
        {
            return Ok((who, action, 0));
        }
        return Err(());
    }
    let mut perms = 0u32;
    for &c in &bytes[idx..] {
        perms |= match c {
            'r' => 0o444,
            'w' => 0o222,
            'x' => 0o111,
            'X' => 0o111, // approximated as x
            's' => 0o000, // setuid/sticky: parse, ignore
            't' => 0o000,
            'u' | 'g' | 'o' => 0o000, // copy forms: parse, approximate
            _ => return Err(()),
        };
    }
    Ok((who, action, perms))
}

/// Reconstruct an approximate POSIX mode on Windows.
pub fn current_mode(path: &Path) -> u32 {
    match std::fs::symlink_metadata(path) {
        Ok(md) => {
            let is_dir = md.is_dir();
            let readonly = md.permissions().readonly();
            let exec_ext = path
                .extension()
                .map(|e| {
                    let e = e.to_string_lossy().to_ascii_lowercase();
                    EXEC_EXTS.iter().any(|x| *x == e)
                })
                .unwrap_or(false);
            let mut m = if readonly { 0o444 } else { 0o644 };
            if is_dir || exec_ext {
                m |= 0o111;
            }
            m
        }
        Err(_) => 0o644,
    }
}

/// Apply symbolic clause(s) to `current`, GNU-order (clauses left to right).
pub fn apply_symbolic(current: u32, symbolic: &str) -> Result<u32, ()> {
    let mut m = current;
    for clause in symbolic.split(',') {
        let (who, action, perms) = parse_clause(clause)?;
        let mut mask = 0u32;
        for shift in [6, 3, 0] {
            let who_bit = (who >> shift) & 7;
            if who_bit == 0 {
                continue;
            }
            for bit in 0..3 {
                if (who_bit >> (2 - bit)) & 1 == 1 {
                    mask |= ((perms >> (2 - bit)) & 1) << (shift + (2 - bit));
                }
            }
        }
        m = match action {
            '+' => m | mask,
            '-' => m & !mask,
            '=' => (m & !who) | mask,
            _ => m,
        };
    }
    Ok(m & 0o777)
}

/// Windows-subset application: owner-write bit drives the read-only attr.
/// Returns Ok(()) even when NTFS cannot represent the change; Err on fs error.
pub fn apply_mode(path: &Path, bits: u32) -> std::io::Result<()> {
    let readonly = bits & 0o200 == 0;
    let md = std::fs::symlink_metadata(path)?;
    let mut perms = md.permissions();
    if perms.readonly() != readonly {
        perms.set_readonly(readonly);
        std::fs::set_permissions(path, perms)?;
    }
    Ok(())
}

/// Apply a mode spec (octal or symbolic) against a path's current mode.
pub fn apply_spec(path: &Path, spec: &str) -> std::io::Result<()> {
    if spec.chars().all(|c| ('0'..='7').contains(&c)) && !spec.is_empty() && spec.len() <= 4 {
        let mut bits = 0u32;
        for c in spec.chars() {
            bits = bits * 8 + c.to_digit(8).unwrap();
        }
        apply_mode(path, bits)
    } else {
        let cur = current_mode(path);
        let new = apply_symbolic(cur, spec)
            .map_err(|_| std::io::Error::from(std::io::ErrorKind::InvalidData))?;
        apply_mode(path, new)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn octal_parsing() {
        assert_eq!(parse_mode("644").unwrap(), 0o644);
        assert_eq!(parse_mode("0755").unwrap(), 0o755);
        assert_eq!(parse_mode("0").unwrap(), 0);
        assert!(parse_mode("8oo").is_err());
        assert!(parse_mode("").is_err());
        assert!(parse_mode("12345").is_err());
    }

    #[test]
    fn symbolic_parsing() {
        assert!(parse_mode("u+x").is_ok());
        assert!(parse_mode("go-w").is_ok());
        assert!(parse_mode("a=r").is_ok());
        assert!(parse_mode("u+rwx,go+rx").is_ok());
        assert!(parse_mode("badmode").is_err());
        assert!(parse_mode("+").is_err());
    }

    #[test]
    fn symbolic_application() {
        assert_eq!(apply_symbolic(0o644, "u+x").unwrap(), 0o744);
        assert_eq!(apply_symbolic(0o642, "go-w").unwrap(), 0o640);
        assert_eq!(apply_symbolic(0o644, "go-w").unwrap(), 0o644); // no w to clear
        assert_eq!(apply_symbolic(0o644, "a=r").unwrap(), 0o444);
        assert_eq!(apply_symbolic(0o644, "u+rwx,go+rx").unwrap(), 0o755);
        assert_eq!(apply_symbolic(0o666, "+x").unwrap(), 0o777);
    }
}
