//! POSIX file-mode parsing, GNU modechange semantics, and the Windows
//! application layer.
//!
//! Spec: GNU coreutils 9.4 `lib/modechange.c` (mode_compile / mode_adjust)
//! and `chmod.c`, probed against the WSL coreutils 9.4 oracle:
//! * numeric: 1-8 octal digits; leading zeros allowed; the value must fit
//!   0o7777 (`00000644` ok, `077777` -> invalid mode, verified).
//! * symbolic: comma-separated clauses `who* (op perm*)+`, at least one op
//!   per clause (`u` alone is invalid; `u+`, `+`, `=` are valid no-ops;
//!   `u+r-w` is one clause with two ops — all verified). Perms are
//!   `r w x X s t u g o`, where `u g o` in perm position copy that class's
//!   bits from the file's pre-change mode (`u=g`, `u+g` verified).
//! * `X` acts only on directories or files where *some* pre-change x bit is
//!   set (modechange.c seeds it from stat; verified `u+X`, `=X`, `a+X`).
//! * `s` belongs to u/g (setuid/setgid), `t` belongs to o (sticky);
//!   misplaced ones (`g+t`, `o+s`) are silently ignored (verified rc 0).
//! * `=` replaces the rwx bits AND the special bits owned by the affected
//!   who unless s/t is in the perm list (`u=rw` clears setuid, `u=rwxs`
//!   sets it, `a=st` sets exactly suid+sgid+sticky — verified).
//! * who omitted: rwx applies to `a` but bits set in the process umask are
//!   not affected (`=w` on 044 under umask 022 -> 0200, `+w` -> 0244,
//!   `-r` on 777 -> 0333, verified). For `=` the umask-masked bits are
//!   CLEARED, not preserved — `=r` on 777 under umask 077 -> 0400, verified
//!   (modechange.c mode_adjust: preserved = 0 when who is omitted).
//!   Special bits are not umask-gated (`+st` sets all three under umask
//!   022, verified).
//! * default who for a bare `+x` also honours the umask (verified
//!   `+x` on 0644 -> 0755 under umask 022).
//!
//! Windows persistence is the documented subset: the NTFS read-only
//! attribute tracks the owner-write bit (drvfs/WSL mapping: owner-w on ->
//! writable, owner-w off -> FILE_ATTRIBUTE_READONLY); setuid/setgid/
//! sticky bits are evaluated in the in-memory model (so `-v` wording and
//! multi-clause arithmetic match GNU) but are not persisted — NTFS has no
//! representation and Cygwin/MSYS-style sidecar SIDs are out of scope.
//! Execute bits are model-only; a file's current mode is reconstructed from
//! the read-only attribute plus the executable-extension heuristic.
//!
//! Two persistence flavors: [`apply_mode`] is the chmod flavor and leaves
//! directories untouched; [`apply_mode_dir`] is the mkdir -m flavor and
//! persists the attribute on the leaf directory too (GNU explicitly chmods
//! the created directory: coreutils src/mkdir.c main -> mode_adjust, gnulib
//! lib/mkdir-p.c make_dir_parents -> dirchownmod).

use std::path::Path;

pub const EXEC_EXTS: [&str; 4] = ["exe", "bat", "cmd", "com"];

pub const SUID: u32 = 0o4000;
pub const SGID: u32 = 0o2000;
pub const STICKY: u32 = 0o1000;
pub const RWX: u32 = 0o777;
pub const ALL_BITS: u32 = 0o7777;

/// who-class bitmask: bit2=u, bit1=g, bit0=o (`a` = all three).
pub type Who = u32;

const W_U: Who = 0b100;
const W_G: Who = 0b010;
const W_O: Who = 0b001;
const W_A: Who = 0b111;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Op {
    Add,
    Sub,
    Eq,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Perm {
    R,
    W,
    X,
    BigX,
    S,
    T,
    Copy(Who),
}

#[derive(Debug, Clone, PartialEq)]
pub struct Clause {
    /// who bitmask, 0 = omitted (umask-excluded `a`).
    pub who: Who,
    pub ops: Vec<(Op, Vec<Perm>)>,
}

#[derive(Debug, Clone, PartialEq)]
pub enum ModeSpec {
    Num(u32),
    Sym(Vec<Clause>),
}

fn who_class(c: char) -> Option<Who> {
    match c {
        'u' => Some(W_U),
        'g' => Some(W_G),
        'o' => Some(W_O),
        'a' => Some(W_A),
        _ => None,
    }
}

fn shift_of(w: Who) -> u32 {
    match w {
        W_U => 6,
        W_G => 3,
        _ => 0,
    }
}

/// Parse a mode string into a spec. `Err(())` on any grammar violation.
pub fn parse_mode_spec(s: &str) -> Result<ModeSpec, ()> {
    if s.is_empty() {
        return Err(());
    }
    if s.chars().all(|c| ('0'..='7').contains(&c)) {
        // GNU mode_compile: accumulate octal; value must fit 0o7777.
        // Overflow (huge digit runs) is also invalid, like xstrtoumax.
        let mut v: u64 = 0;
        for c in s.chars() {
            v = v
                .checked_mul(8)
                .and_then(|x| x.checked_add(c.to_digit(8).unwrap() as u64))
                .ok_or(())?;
            if v > ALL_BITS as u64 {
                // GNU keeps accumulating only while the total stays in
                // range; any digit that pushes past the mask is invalid
                // (verified: 077777 rejected, 00000644 accepted).
                return Err(());
            }
        }
        return Ok(ModeSpec::Num(v as u32));
    }
    let mut clauses = Vec::new();
    for clause in s.split(',') {
        if clause.is_empty() {
            return Err(());
        }
        clauses.push(parse_clause(clause)?);
    }
    Ok(ModeSpec::Sym(clauses))
}

fn parse_clause(clause: &str) -> Result<Clause, ()> {
    let chars: Vec<char> = clause.chars().collect();
    let mut i = 0usize;
    let mut who: Who = 0;
    while i < chars.len() {
        match who_class(chars[i]) {
            Some(w) => {
                who |= w;
                i += 1;
            }
            None => break,
        }
    }
    if i >= chars.len() {
        // bare who (`u`) — GNU requires at least one op (verified rc 1)
        return Err(());
    }
    let mut ops = Vec::new();
    while i < chars.len() {
        let op = match chars[i] {
            '+' => Op::Add,
            '-' => Op::Sub,
            '=' => Op::Eq,
            _ => return Err(()),
        };
        i += 1;
        let mut perms = Vec::new();
        while i < chars.len() {
            match chars[i] {
                'r' => perms.push(Perm::R),
                'w' => perms.push(Perm::W),
                'x' => perms.push(Perm::X),
                'X' => perms.push(Perm::BigX),
                's' => perms.push(Perm::S),
                't' => perms.push(Perm::T),
                'u' | 'g' | 'o' => perms.push(Perm::Copy(who_class(chars[i]).unwrap())),
                _ => break,
            }
            i += 1;
        }
        ops.push((op, perms));
    }
    Ok(Clause { who, ops })
}

/// Validate a mode string; returns Err(bad_clause_or_string) on failure.
pub fn validate_mode(s: &str) -> Result<(), String> {
    parse_mode_spec(s).map(|_| ()).map_err(|_| s.to_string())
}

/// Parse an octal or symbolic mode. Symbolic specs are evaluated against
/// 0o777 with the process umask applied (GNU mkdir -m applies symbolic
/// modes relative to the default creation mode modified by umask); this is
/// the pre-2.0 API kept for `mkdir`.
pub fn parse_mode(s: &str) -> Result<u32, ()> {
    match parse_mode_spec(s)? {
        ModeSpec::Num(v) => Ok(v),
        ModeSpec::Sym(clauses) => Ok(eval_clauses(0o777, true, &clauses, process_umask())),
    }
}

/// Replicate the rwx bits `bits` (one class worth, 0..7) onto every class
/// in `who`, returning the full 0o777-spread mask.
fn spread_rwx(who: Who, bits: u32) -> u32 {
    let mut out = 0u32;
    for w in [W_U, W_G, W_O] {
        if who & w != 0 {
            out |= (bits & 7) << shift_of(w);
        }
    }
    out
}

/// rwx bits of `who` classes (e.g. u+o -> 0o711).
fn rwx_of(who: Who) -> u32 {
    spread_rwx(who, 7)
}

/// Evaluate symbolic clauses against `current` (pre-change mode), GNU
/// modechange.c order: clauses left to right, ops within a clause left to
/// right; `X` and copy forms reference the PRE-CHANGE mode only.
pub fn eval_clauses(current: u32, is_dir: bool, clauses: &[Clause], umask: u32) -> u32 {
    let mut m = current & ALL_BITS;
    // X and copy forms are decided once from the pre-change state.
    let x_allowed = is_dir || current & 0o111 != 0;
    for clause in clauses {
        let omitted = clause.who == 0;
        let who = if omitted { W_A } else { clause.who };
        // who-omitted: rwx affects only bits NOT set in the umask.
        let affected_rwx = if omitted { !umask & RWX } else { RWX } & rwx_of(who);
        for &(op, ref perms) in &clause.ops {
            let mut set = 0u32;
            let mut has_s = false;
            let mut has_t = false;
            for &p in perms {
                match p {
                    Perm::R => set |= spread_rwx(who, 0o4) & affected_rwx,
                    Perm::W => set |= spread_rwx(who, 0o2) & affected_rwx,
                    Perm::X => set |= spread_rwx(who, 0o1) & affected_rwx,
                    Perm::BigX => {
                        if x_allowed {
                            set |= spread_rwx(who, 0o1) & affected_rwx;
                        }
                    }
                    // s/t are special bits: never umask-gated (verified).
                    Perm::S => {
                        has_s = true;
                        if who & W_U != 0 {
                            set |= SUID;
                        }
                        if who & W_G != 0 {
                            set |= SGID;
                        }
                    }
                    Perm::T => {
                        has_t = true;
                        if who & W_O != 0 {
                            set |= STICKY;
                        }
                    }
                    Perm::Copy(src) => {
                        let bits = (current >> shift_of(src)) & 7;
                        set |= spread_rwx(who, bits) & affected_rwx;
                    }
                }
            }
            m = match op {
                Op::Add => m | set,
                Op::Sub => m & !set,
                Op::Eq => {
                    // rwx portion: exactly `set` within the affected mask.
                    // [GNU] modechange.c mode_adjust '=' branch: preserved =
                    // (affected ? ~affected : 0) | omit_change — a
                    // who-specified '=' keeps bits outside `affected`, but a
                    // who-omitted '=' keeps NOTHING ("Otherwise, clear all
                    // the bits"): `=r` under umask 077 lands at 0400, not
                    // 0477 (WSL coreutils 9.4 oracle, chmod and mkdir -m).
                    let preserved = if omitted { 0 } else { !affected_rwx };
                    m = (m & preserved) | set;
                    // special bits owned by the affected who are replaced:
                    // kept only when s/t is present (verified u=rw clears
                    // setuid, u=rwxs sets it, a=st sets exactly all three).
                    if who & W_U != 0 {
                        m = if has_s { m | SUID } else { m & !SUID };
                    }
                    if who & W_G != 0 {
                        m = if has_s { m | SGID } else { m & !SGID };
                    }
                    if who & W_O != 0 {
                        m = if has_t { m | STICKY } else { m & !STICKY };
                    }
                    m
                }
            };
        }
    }
    m & ALL_BITS
}

/// Evaluate a parsed spec (numeric is absolute; symbolic relative).
pub fn eval_spec(current: u32, is_dir: bool, spec: &ModeSpec, umask: u32) -> u32 {
    match spec {
        ModeSpec::Num(v) => v & ALL_BITS,
        ModeSpec::Sym(clauses) => eval_clauses(current, is_dir, clauses, umask),
    }
}

/// The process umask via the CRT (the host shell's `umask` builtin lands
/// in the same process-wide CRT state). `_umask(0)` + restore: reading it
/// has no thread-safe getter across CRT versions, and the momentary
/// zero window is the same process-wide race GNU accepts.
#[cfg(windows)]
pub fn process_umask() -> u32 {
    extern "C" {
        fn _umask(newmask: i32) -> i32;
    }
    unsafe {
        let old = _umask(0);
        _umask(old);
        old as u32
    }
}

#[cfg(not(windows))]
pub fn process_umask() -> u32 {
    0o022
}

/// Reconstruct an approximate POSIX mode on Windows (read-only attribute +
/// executable-extension heuristic; directories are always traversable).
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

/// True when `path` looks like a regular file (not a dir, not a symlink to
/// a dir) — used to decide whether the read-only attribute is applicable.
fn is_regular_or_any_file(path: &Path) -> bool {
    std::fs::symlink_metadata(path)
        .map(|md| !md.is_dir())
        .unwrap_or(false)
}

/// Persist the owner-write mapping: `readonly` true iff the owner-write bit
/// is absent. Shared by both persistence flavors.
fn set_readonly(path: &Path, readonly: bool) -> std::io::Result<()> {
    let md = std::fs::symlink_metadata(path)?;
    let mut perms = md.permissions();
    if perms.readonly() != readonly {
        perms.set_readonly(readonly);
        std::fs::set_permissions(path, perms)?;
    }
    Ok(())
}

/// Windows-subset persistence: owner-write bit drives the read-only
/// attribute on files (drvfs mapping); directories are left untouched
/// (Windows ignores read-only on dirs). Special and execute bits have no
/// NTFS representation and are not persisted.
/// Returns Ok(()) even when NTFS cannot represent the change; Err on fs error.
pub fn apply_mode(path: &Path, bits: u32) -> std::io::Result<()> {
    if is_regular_or_any_file(path) {
        set_readonly(path, bits & 0o200 == 0)?;
    }
    Ok(())
}

/// mkdir -m persistence flavor: same owner-write mapping as [`apply_mode`]
/// but ALSO applied to directories. [GNU] mkdir applies the compiled mode
/// to the created leaf DIRECTORY itself — src/mkdir.c main() compiles
/// `options.mode = mode_adjust (S_IRWXUGO, true, umask_value, change, ...)`
/// and gnulib lib/mkdir-p.c:88 `make_dir_parents` ends with
/// `dirchownmod (..., mode, mode_bits)`, an explicit chmod of the leaf
/// after the raw mkdir. The Windows representation of an owner-unwritable
/// directory is FILE_ATTRIBUTE_READONLY (it gates directory deletion), so
/// `mkdir -m 555 d` must land the attribute on `d`, exactly like the
/// pre-wiring C++ pilot did (apply_directory_mode, 3a8e0f9f).
pub fn apply_mode_dir(path: &Path, bits: u32) -> std::io::Result<()> {
    set_readonly(path, bits & 0o200 == 0)
}

/// Apply a mode spec (octal or symbolic) against a path's current mode.
pub fn apply_spec(path: &Path, spec: &str) -> std::io::Result<()> {
    let parsed =
        parse_mode_spec(spec).map_err(|_| std::io::Error::from(std::io::ErrorKind::InvalidData))?;
    let cur = current_mode(path);
    let is_dir = std::fs::symlink_metadata(path)
        .map(|m| m.is_dir())
        .unwrap_or(false);
    let new = eval_spec(cur, is_dir, &parsed, process_umask());
    apply_mode(path, new)
}

/// GNU chmod/chown -v rendering: 4-digit octal plus the 9-char rwx string
/// with s/S (setuid/setgid without/with x) and t/T (sticky without/with x).
pub fn rwx_string(bits: u32) -> String {
    let mut s = String::with_capacity(9);
    for shift in [6, 3, 0] {
        let b = (bits >> shift) & 7;
        let r = if b & 4 != 0 { 'r' } else { '-' };
        let w = if b & 2 != 0 { 'w' } else { '-' };
        let x = if b & 1 != 0 { 'x' } else { '-' };
        // s/S and t/T replace the x position (GNU stat: rwSr--r--, rw-r--r-T)
        let x3 = if shift == 6 && bits & SUID != 0 {
            if b & 1 != 0 {
                's'
            } else {
                'S'
            }
        } else if shift == 3 && bits & SGID != 0 {
            if b & 1 != 0 {
                's'
            } else {
                'S'
            }
        } else if shift == 0 && bits & STICKY != 0 {
            if b & 1 != 0 {
                't'
            } else {
                'T'
            }
        } else {
            x
        };
        s.push(r);
        s.push(w);
        s.push(x3);
    }
    s
}

/// GNU -v form: `0644 (rw-r--r--)`.
pub fn mode_string(bits: u32) -> String {
    format!("{:04o} ({})", bits & ALL_BITS, rwx_string(bits))
}

#[cfg(test)]
mod tests {
    use super::*;

    const U0: u32 = 0; // umask 0 => omitted who acts on plain `a`

    #[test]
    fn octal_parsing() {
        assert_eq!(parse_mode_spec("644").unwrap(), ModeSpec::Num(0o644));
        assert_eq!(parse_mode_spec("0755").unwrap(), ModeSpec::Num(0o755));
        assert_eq!(parse_mode_spec("0").unwrap(), ModeSpec::Num(0));
        assert_eq!(parse_mode_spec("4755").unwrap(), ModeSpec::Num(0o4755));
        assert_eq!(parse_mode_spec("7755").unwrap(), ModeSpec::Num(0o7755));
        // leading zeros: value must fit the mask (GNU verified)
        assert_eq!(parse_mode_spec("00000644").unwrap(), ModeSpec::Num(0o644));
        assert!(parse_mode_spec("077777").is_err());
        assert!(parse_mode_spec("12345").is_err());
        assert!(parse_mode_spec("8oo").is_err());
        assert!(parse_mode_spec("").is_err());
        // overflow run of zeros then too-big value
        assert!(parse_mode_spec("0000000000000000000077777").is_err());
    }

    #[test]
    fn symbolic_grammar() {
        assert!(parse_mode_spec("u+x").is_ok());
        assert!(parse_mode_spec("go-w").is_ok());
        assert!(parse_mode_spec("a=r").is_ok());
        assert!(parse_mode_spec("u+rwx,go+rx").is_ok());
        // at least one op per clause (GNU verified: `u` alone invalid)
        assert!(parse_mode_spec("u").is_err());
        assert!(parse_mode_spec("badmode").is_err());
        assert!(parse_mode_spec("z+r").is_err());
        // empty perm lists are valid no-ops (GNU verified)
        assert!(parse_mode_spec("u+").is_ok());
        assert!(parse_mode_spec("+").is_ok());
        assert!(parse_mode_spec("=").is_ok());
        assert!(parse_mode_spec("u=").is_ok());
        // trailing comma invalid
        assert!(parse_mode_spec("u+r,").is_err());
        assert!(parse_mode_spec(",u+r").is_err());
        // multiple ops in one clause (GNU verified u+r-w rc 0)
        assert!(parse_mode_spec("u+r-w").is_ok());
        assert!(parse_mode_spec("g=u,g+w").is_ok());
    }

    #[test]
    fn basic_symbolic_application() {
        assert_eq!(apply_symbolic(0o644, "u+x").unwrap(), 0o744);
        assert_eq!(apply_symbolic(0o642, "go-w").unwrap(), 0o640);
        assert_eq!(apply_symbolic(0o644, "go-w").unwrap(), 0o644); // no w to clear
        assert_eq!(apply_symbolic(0o644, "a=r").unwrap(), 0o444);
        assert_eq!(apply_symbolic(0o644, "u+rwx,go+rx").unwrap(), 0o755);
        assert_eq!(apply_symbolic(0o666, "+x").unwrap(), 0o777);
    }

    // helper mirroring the pre-2.0 API over the new evaluator
    fn apply_symbolic(current: u32, symbolic: &str) -> Result<u32, ()> {
        match parse_mode_spec(symbolic)? {
            ModeSpec::Num(_) => Err(()),
            ModeSpec::Sym(c) => Ok(eval_clauses(current, false, &c, 0)),
        }
    }

    #[test]
    fn umask_free_x_semantics() {
        // X acts on dirs and pre-change-executable files only (GNU verified:
        // 666 + a+X stays rw-rw-rw-; a plain file gains nothing)
        assert_eq!(sym(0o644, false, "u+X", U0), 0o644);
        assert_eq!(sym(0o644, true, "u+X", U0), 0o744);
        assert_eq!(sym(0o666, false, "a+X", U0), 0o666); // 6xx has no x anywhere
        assert_eq!(sym(0o666, true, "a+X", U0), 0o777); // dirs always qualify
        assert_eq!(sym(0o100, false, "a-X", U0), 0o000); // u had x -> all cleared
        assert_eq!(sym(0o000, false, "a+X", U0), 0o000);
        // =X on a plain non-executable file clears everything (probe1)
        assert_eq!(sym(0o644, false, "=X", U0), 0o000);
    }

    fn sym(current: u32, is_dir: bool, spec: &str, umask: u32) -> u32 {
        let parsed = parse_mode_spec(spec).unwrap();
        eval_spec(current, is_dir, &parsed, umask)
    }

    #[test]
    fn copy_forms() {
        // u=g copies g's pre-change bits (GNU verified on 640)
        assert_eq!(sym(0o640, false, "u=g", U0), 0o440);
        // u+g ORs g's bits into u (GNU verified: 640 unchanged)
        assert_eq!(sym(0o640, false, "u+g", U0), 0o640);
        // g=o on 642 (GNU verified)
        assert_eq!(sym(0o642, false, "g=o", U0), 0o622);
        // g=u,g+w (GNU verified on 644)
        assert_eq!(sym(0o644, false, "g=u,g+w", U0), 0o664);
    }

    #[test]
    fn special_bits() {
        // u+s / +t succeed on regular files (GNU verified, S/T display)
        assert_eq!(sym(0o644, false, "u+s", U0), 0o644 | SUID);
        assert_eq!(sym(0o644, false, "+t", U0), 0o644 | STICKY);
        assert_eq!(sym(0o755, false, "+st", U0), 0o755 | SUID | SGID | STICKY);
        // g+t silently no-ops (GNU verified rc 0)
        assert_eq!(sym(0o644, false, "g+t", U0), 0o644);
        // = replaces specials owned by the affected who (GNU verified:
        // 4755 + u=rw -> -rw-r-xr-x = 0655, setuid gone)
        assert_eq!(sym(0o4755, false, "u=rw", U0), 0o655);
        assert_eq!(sym(0o4755, false, "u=rw", U0), 0o0655);
        // GNU probe R: 0000 + u=rwxs -> -rws------ = 04700
        assert_eq!(sym(0o0000, false, "u=rwxs", U0), SUID | 0o700);
        assert_eq!(sym(0o0777, false, "a=st", U0), SUID | SGID | STICKY);
        // 4-digit octal sets/clears specials absolutely
        assert_eq!(sym(0o644, false, "4755", U0), 0o4755);
        assert_eq!(sym(0o4755, false, "644", U0), 0o644);
        // u-s clears setuid only (GNU verified)
        assert_eq!(sym(0o4755, false, "u-s", U0), 0o0755);
    }

    #[test]
    fn umask_exclusion() {
        // who omitted: rwx bits set in the umask are not affected (GNU
        // verified under umask 022: =w on 044 -> 0200, +w -> 0244,
        // -r on 777 -> 0333, =rw on 644 -> 644)
        let um = 0o022;
        assert_eq!(sym(0o044, false, "=w", um), 0o200);
        assert_eq!(sym(0o044, false, "+w", um), 0o244);
        assert_eq!(sym(0o777, false, "-r", um), 0o333);
        assert_eq!(sym(0o644, false, "=rw", um), 0o644);
        assert_eq!(sym(0o644, false, "+wx", um), 0o755);
        assert_eq!(sym(0o644, false, "=X", um), 0o000);
        // specials are NOT umask-gated
        assert_eq!(sym(0o755, false, "+st", um), 0o755 | SUID | SGID | STICKY);
        // umask 0 behaves as plain a (044 + w-for-all = 0266)
        assert_eq!(sym(0o044, false, "+w", 0), 0o266);
    }

    #[test]
    fn clause_order_left_to_right() {
        assert_eq!(sym(0o000, false, "u+rwx,go+r", U0), 0o744);
        // a=r (all -> 444) then u+w -> 644
        assert_eq!(sym(0o644, false, "a=r,u+w", U0), 0o644);
        assert_eq!(sym(0o000, false, "a=r,u+w", U0), 0o644);
        // reversed order: last clause wins (a=r clears the added w)
        assert_eq!(sym(0o644, false, "u+w,a=r", U0), 0o444);
    }

    #[test]
    fn rwx_string_rendering() {
        assert_eq!(rwx_string(0o644), "rw-r--r--");
        assert_eq!(rwx_string(0o755), "rwxr-xr-x");
        assert_eq!(rwx_string(0o000), "---------");
        // setuid without x -> S (GNU stat -A verified: rwSr--r--)
        assert_eq!(rwx_string(0o644 | SUID), "rwSr--r--");
        assert_eq!(rwx_string(0o755 | SUID), "rwsr-xr-x");
        // sticky on non-exec file -> T (GNU verified: rw-r--r-T)
        assert_eq!(rwx_string(0o644 | STICKY), "rw-r--r-T");
        assert_eq!(rwx_string(0o755 | SGID), "rwxr-sr-x");
        assert_eq!(rwx_string(0o7755), "rwsr-sr-t");
        assert_eq!(mode_string(0o644), "0644 (rw-r--r--)");
        assert_eq!(mode_string(0o755), "0755 (rwxr-xr-x)");
    }

    #[test]
    fn validate_keeps_api() {
        assert!(validate_mode("644").is_ok());
        assert!(validate_mode("u+r").is_ok());
        assert_eq!(validate_mode("bad"), Err("bad".to_string()));
    }

    #[test]
    fn process_umask_is_plausible() {
        // CRT default is 0 unless the host shell set one; either way it
        // must be a valid mask.
        assert!(process_umask() <= 0o777);
    }

    #[test]
    fn eq_omitted_who_clears_umask_masked_bits() {
        // [GNU] modechange.c mode_adjust '=' branch: preserved =
        // (affected ? ~affected : 0) | omit_change — a who-omitted '='
        // keeps NOTHING outside `value`, so the umask-masked base bits are
        // cleared too. WSL coreutils 9.4 oracle under `umask 077`:
        // `mkdir -m =r d` -> 400, `mkdir -m =w d` -> 200,
        // `chmod =r f` (on 666) -> 400, `chmod =w f` -> 200.
        let um = 0o077;
        assert_eq!(sym(0o777, true, "=r", um), 0o400);
        assert_eq!(sym(0o777, true, "=w", um), 0o200);
        assert_eq!(sym(0o644, false, "=r", um), 0o400);
        assert_eq!(sym(0o644, false, "=w", um), 0o200);
        // who-specified '=' keeps the bits outside its affected mask
        // (unchanged class behavior: =r under umask on g/o stays)
        assert_eq!(sym(0o644, false, "u=r", um), 0o444);
        // +w stays umask-gated on the omitted who
        assert_eq!(sym(0o777, true, "+w", um), 0o777);
        assert_eq!(sym(0o444, false, "+w", um), 0o644);
    }
}
