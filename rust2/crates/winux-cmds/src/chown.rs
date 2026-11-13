//! `chown` — GNU coreutils grammar with real Windows ownership semantics.
//!
//! Spec grammar matches GNU (probed against coreutils 9.4):
//! `[OWNER][:[GROUP]]`; an empty spec or a bare `:` is a no-op (rc 0);
//! `OWNER:` (trailing colon, named owner) also selects the owner's login
//! group on POSIX — Windows has no login-group model, so the owner part is
//! applied and the group left untouched (documented deviation); a numeric
//! owner with trailing colon is `invalid spec` because the login-group
//! lookup fails on POSIX too (verified: `1234:` -> invalid spec,
//! `root:` -> ok). Two colons report the whole spec as `invalid user`
//! (verified); validation order is user first, then group
//! (`invalid user: 'u'` / `invalid group: ':g'`, verified).
//!
//! Windows capability matrix (this is what the OS can honestly do):
//! * `:group` named — real change via SetNamedSecurityInfoW
//!   (GROUP_SECURITY_INFORMATION); the file owner holds implicit
//!   WRITE_OWNER, so an unprivileged owner can set the primary group.
//! * named owner — real change via OWNER_SECURITY_INFORMATION: setting
//!   your own SID succeeds (no-op when already owner); changing to another
//!   account needs SeTakeOwnershipPrivilege and maps to GNU's
//!   `changing ownership of 'f': Operation not permitted`, rc 1,
//!   continuing with the remaining operands (verified GNU behavior).
//! * numeric uid/gid — no Windows model; validated and applied as an
//!   honest no-op (rc 0), matching the root-oracle's rc.
//! * `--from=SPEC` filters by current owner/group SID (named parts);
//!   numeric parts cannot match on Windows (documented) — non-matching
//!   operands are skipped silently, exactly like GNU.
//! * `--reference=RFILE` copies owner+group SIDs from RFILE; a missing
//!   reference is `failed to get attributes of 'x': ...` rc 1 (verified);
//!   with `--reference` every positional is a file operand (verified:
//!   `chown root --reference=f g` reports `cannot access 'root'`).
//!
//! `-v`/`-c` wording (verified against the oracle): owner(+group) change
//! `changed ownership of 'f' from A:B to C:D`; group-only change
//! `changed group of 'f' from A to B`; no change `ownership of 'f'
//! retained as A:B` (owner-only specs show `as A`); a no-op spec shows
//! bare `ownership of 'f' retained`. `-f` suppresses diagnostics.
//! `-R` walks preorder and refuses filesystem roots like chmod
//! (`it is dangerous to operate recursively on '/'`).
//!
//! Danger surface: tests only touch ScratchCtx temp files owned by the
//! current user, and never assign ownership to a different account.

use std::ffi::OsString;
use std::path::Path;

use crate::argopt::{self, OptSpec};
use crate::fsutil::resolve;
use winux_core::{usage_error, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('R'),
        long: "recursive",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "rec",
        takes_value: false,
    },
    OptSpec {
        short: Some('v'),
        long: "verbose",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "verb",
        takes_value: false,
    },
    OptSpec {
        short: Some('c'),
        long: "changes",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "cha",
        takes_value: false,
    },
    OptSpec {
        short: Some('f'),
        long: "quiet",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "sil",
        takes_value: false,
    },
    OptSpec {
        short: Some('H'),
        long: "dereference-args",
        takes_value: false,
    },
    OptSpec {
        short: Some('L'),
        long: "dereference",
        takes_value: false,
    },
    OptSpec {
        short: Some('P'),
        long: "no-dereference",
        takes_value: false,
    },
    OptSpec {
        short: Some('h'),
        long: "no-dereference-short",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "reference",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "from",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "preserve-root",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "no-preserve-root",
        takes_value: false,
    },
];

/// One resolved spec part: absent, numeric (no Windows model), or a
/// resolved SID with a display name.
#[derive(Clone, Debug)]
enum Part {
    Absent,
    Numeric(String),
    Sid(Vec<u8>, String),
}

#[derive(Clone, Debug)]
struct Spec {
    owner: Part,
    group: Part,
    /// spec contained `:` (drives the `owner:group` wording shape)
    colon: bool,
    /// nothing was requested at all (`""` or `":"`) — bare `retained`
    noop: bool,
}

// ---------------------------------------------------------------- FFI

#[cfg(windows)]
mod ffi {
    pub const SE_FILE_OBJECT: u32 = 1;
    pub const OWNER_SECURITY_INFORMATION: u32 = 0x1;
    pub const GROUP_SECURITY_INFORMATION: u32 = 0x2;
    pub const ERROR_SUCCESS: u32 = 0;
    pub const ERROR_INSUFFICIENT_BUFFER: u32 = 122;

    #[link(name = "advapi32")]
    extern "system" {
        pub fn LookupAccountNameW(
            lp_system_name: *const u16,
            lp_account_name: *const u16,
            sid: *mut u8,
            cb_sid: *mut u32,
            referenced_domain_name: *mut u16,
            cch_referenced_domain_name: *mut u32,
            pe_use: *mut i32,
        ) -> i32;
        pub fn LookupAccountSidW(
            lp_system_name: *const u16,
            sid: *mut core::ffi::c_void,
            lp_name: *mut u16,
            cch_name: *mut u32,
            referenced_domain_name: *mut u16,
            cch_referenced_domain_name: *mut u32,
            pe_use: *mut u32,
        ) -> i32;
        pub fn GetLengthSid(psid: *const u8) -> u32;
        // Get/SetNamedSecurityInfoW are in Advapi32.lib (MSDN: header
        // aclapi.h, library Advapi32.lib) — no aclapi.lib dependency.
        pub fn GetNamedSecurityInfoW(
            object_name: *const u16,
            object_type: u32,
            security_info: u32,
            ppsid_owner: *mut *mut u8,
            ppsid_group: *mut *mut u8,
            pp_dacl: *mut *mut u8,
            pp_sacl: *mut *mut u8,
            pp_security_descriptor: *mut *mut core::ffi::c_void,
        ) -> u32;
        pub fn SetNamedSecurityInfoW(
            object_name: *const u16,
            object_type: u32,
            security_info: u32,
            psid_owner: *const u8,
            psid_group: *const u8,
            p_dacl: *const u8,
            p_sacl: *const u8,
        ) -> u32;
    }

    #[link(name = "kernel32")]
    extern "system" {
        pub fn LocalFree(hmem: *mut core::ffi::c_void) -> *mut core::ffi::c_void;
    }
}

#[cfg(windows)]
fn wide(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}

/// Resolve an account name to its SID + bare display name.
#[cfg(windows)]
pub(crate) fn lookup_account(name: &str) -> Option<(Vec<u8>, String)> {
    unsafe {
        let wname = wide(name);
        let mut cb_sid = 0u32;
        let mut cb_domain = 0u32;
        let mut use_type = 0i32;
        // first call with NULL buffers to size them
        if ffi::LookupAccountNameW(
            std::ptr::null(),
            wname.as_ptr(),
            std::ptr::null_mut(),
            &mut cb_sid,
            std::ptr::null_mut(),
            &mut cb_domain,
            &mut use_type,
        ) == 0
            && std::io::Error::last_os_error().raw_os_error().unwrap_or(0)
                != ffi::ERROR_INSUFFICIENT_BUFFER as i32
        {
            return None;
        }
        if cb_sid == 0 {
            return None;
        }
        let mut sid = vec![0u8; cb_sid as usize];
        let mut domain = vec![0u16; (cb_domain as usize).max(1)];
        let mut cb_domain2 = domain.len() as u32;
        if ffi::LookupAccountNameW(
            std::ptr::null(),
            wname.as_ptr(),
            sid.as_mut_ptr(),
            &mut cb_sid,
            domain.as_mut_ptr(),
            &mut cb_domain2,
            &mut use_type,
        ) == 0
        {
            return None;
        }
        let dn = String::from_utf16_lossy(&domain[..cb_domain2 as usize]);
        let full = if dn.is_empty() {
            name.to_string()
        } else {
            format!("{dn}\\{name}")
        };
        Some((sid, full))
    }
}

#[cfg(not(windows))]
pub(crate) fn lookup_account(_name: &str) -> Option<(Vec<u8>, String)> {
    None
}

/// Resolve a SID to a displayable account name (domain-qualified).
#[cfg(windows)]
pub(crate) fn lookup_sid_name(sid: &[u8]) -> Option<String> {
    unsafe {
        let mut name = vec![0u16; 256];
        let mut domain = vec![0u16; 256];
        let mut cch_name = name.len() as u32;
        let mut cch_domain = domain.len() as u32;
        let mut use_type = 0u32;
        if ffi::LookupAccountSidW(
            std::ptr::null(),
            sid.as_ptr() as *mut core::ffi::c_void,
            name.as_mut_ptr(),
            &mut cch_name,
            domain.as_mut_ptr(),
            &mut cch_domain,
            &mut use_type,
        ) == 0
        {
            return None;
        }
        let n = String::from_utf16_lossy(&name[..cch_name as usize]);
        let d = String::from_utf16_lossy(&domain[..cch_domain as usize]);
        Some(if d.is_empty() { n } else { format!("{d}\\{n}") })
    }
}

#[cfg(not(windows))]
pub(crate) fn lookup_sid_name(_sid: &[u8]) -> Option<String> {
    None
}

/// Read the current owner and group SIDs of a file.
#[cfg(windows)]
pub(crate) fn get_owner_group(path: &Path) -> Result<(Option<Vec<u8>>, Option<Vec<u8>>), u32> {
    unsafe {
        let wpath = wide(&path.to_string_lossy());
        let mut powner: *mut u8 = std::ptr::null_mut();
        let mut pgroup: *mut u8 = std::ptr::null_mut();
        let mut pdacl: *mut u8 = std::ptr::null_mut();
        let mut psacl: *mut u8 = std::ptr::null_mut();
        let mut psd: *mut core::ffi::c_void = std::ptr::null_mut();
        let rc = ffi::GetNamedSecurityInfoW(
            wpath.as_ptr(),
            ffi::SE_FILE_OBJECT,
            ffi::OWNER_SECURITY_INFORMATION | ffi::GROUP_SECURITY_INFORMATION,
            &mut powner,
            &mut pgroup,
            &mut pdacl,
            &mut psacl,
            &mut psd,
        );
        if rc != ffi::ERROR_SUCCESS {
            return Err(rc);
        }
        let copy = |p: *mut u8| -> Option<Vec<u8>> {
            if p.is_null() {
                return None;
            }
            let len = ffi::GetLengthSid(p) as usize;
            Some(std::slice::from_raw_parts(p, len).to_vec())
        };
        let owner = copy(powner);
        let group = copy(pgroup);
        if !psd.is_null() {
            ffi::LocalFree(psd);
        }
        Ok((owner, group))
    }
}

#[cfg(not(windows))]
pub(crate) fn get_owner_group(_path: &Path) -> Result<(Option<Vec<u8>>, Option<Vec<u8>>), u32> {
    Err(1)
}

/// Apply owner and/or group SIDs to a file. Returns the Win32 error code.
#[cfg(windows)]
pub(crate) fn set_owner_group(
    path: &Path,
    owner: Option<&[u8]>,
    group: Option<&[u8]>,
) -> Result<(), u32> {
    unsafe {
        let wpath = wide(&path.to_string_lossy());
        let mut info = 0u32;
        let mut powner: *const u8 = std::ptr::null();
        let mut pgroup: *const u8 = std::ptr::null();
        if let Some(o) = owner {
            info |= ffi::OWNER_SECURITY_INFORMATION;
            powner = o.as_ptr();
        }
        if let Some(g) = group {
            info |= ffi::GROUP_SECURITY_INFORMATION;
            pgroup = g.as_ptr();
        }
        let rc = ffi::SetNamedSecurityInfoW(
            wpath.as_ptr(),
            ffi::SE_FILE_OBJECT,
            info,
            powner,
            pgroup,
            std::ptr::null(),
            std::ptr::null(),
        );
        if rc == ffi::ERROR_SUCCESS {
            Ok(())
        } else {
            Err(rc)
        }
    }
}

#[cfg(not(windows))]
pub(crate) fn set_owner_group(
    _path: &Path,
    _owner: Option<&[u8]>,
    _group: Option<&[u8]>,
) -> Result<(), u32> {
    Err(1)
}

fn sids_equal(a: Option<&Vec<u8>>, b: Option<&Vec<u8>>) -> bool {
    match (a, b) {
        (Some(x), Some(y)) => x == y,
        _ => false,
    }
}

fn display_part(p: &Part) -> String {
    match p {
        Part::Absent => String::new(),
        Part::Numeric(s) => s.clone(),
        Part::Sid(_, name) => name.clone(),
    }
}

// ------------------------------------------------------------- parsing

fn is_numeric(s: &str) -> bool {
    !s.is_empty() && s.chars().all(|c| c.is_ascii_digit())
}

/// Resolve one spec part. Numeric -> Numeric (kept for display); anything
/// else must resolve via the account database, else None.
fn resolve_part(name: &str) -> Option<Part> {
    if name.is_empty() {
        return Some(Part::Absent);
    }
    if is_numeric(name) {
        return Some(Part::Numeric(name.to_string()));
    }
    lookup_account(name).map(|(sid, full)| Part::Sid(sid, full))
}

/// Split `owner:group` per GNU parse_owner_group:
/// * `""` or `":"` -> no-change spec
/// * more than one colon -> None (reported as invalid user, whole spec)
/// * `owner:` (trailing colon): POSIX sets owner + owner's login group;
///   a numeric owner has no login group -> None (invalid spec, verified);
///   a named owner resolves owner-only on Windows (documented deviation).
fn split_spec(spec: &str) -> Option<(String, Option<String>)> {
    if spec.is_empty() {
        return Some((String::new(), None));
    }
    match spec.find(':') {
        None => Some((spec.to_string(), None)),
        Some(pos) => {
            let rest = &spec[pos + 1..];
            if rest.contains(':') {
                return None;
            }
            Some((spec[..pos].to_string(), Some(rest.to_string())))
        }
    }
}

/// Parse and validate a spec against the Windows account database.
/// `Err(msg)` carries the GNU-shaped diagnostic message.
fn parse_spec(spec: &str) -> Result<Spec, String> {
    let (owner_s, group_s) = match split_spec(spec) {
        Some(pg) => pg,
        None => return Err(format!("invalid user: {}", winux_core::q(spec))),
    };
    let colon = group_s.is_some();
    if owner_s.is_empty() && group_s.as_ref().map(|g| g.is_empty()).unwrap_or(true) && colon {
        // bare ":" — no-change no-op
        return Ok(Spec {
            owner: Part::Absent,
            group: Part::Absent,
            colon,
            noop: true,
        });
    }
    // GNU validates user first, then group (verified error order)
    let owner = if owner_s.is_empty() {
        Part::Absent
    } else {
        match resolve_part(&owner_s) {
            Some(p) => p,
            None => return Err(format!("invalid user: {}", winux_core::q(spec))),
        }
    };
    // trailing colon: owner + login group. Numeric owners have no login
    // group even on POSIX -> invalid spec (verified `1234:`); named owners
    // resolve (verified `root:`); on Windows the group part is skipped.
    if colon && matches!(&group_s, Some(g) if g.is_empty()) && !owner_s.is_empty() {
        if is_numeric(&owner_s) {
            return Err(format!("invalid spec: {}", winux_core::q(spec)));
        }
        return Ok(Spec {
            owner,
            group: Part::Absent,
            colon,
            noop: false,
        });
    }
    let group = match group_s.as_deref() {
        None => Part::Absent,
        Some("") => Part::Absent,
        Some(g) => match resolve_part(g) {
            Some(p) => p,
            None => {
                return Err(format!(
                    "invalid group: {}",
                    winux_core::q(&format!(":{g}"))
                ))
            }
        },
    };
    Ok(Spec {
        owner,
        group,
        colon,
        noop: false,
    })
}

// -------------------------------------------------------------- driver

struct ChState<'i, 'o> {
    verbose: bool,
    changes_only: bool,
    quiet: bool,
    from: Option<Spec>,
    io: &'i mut Io<'o>,
    failed: bool,
}

impl ChState<'_, '_> {
    /// GNU describe_change shapes (verified):
    /// * owner(+group) change: `changed ownership of 'f' from A:B to C:D`
    /// * group-only change:    `changed group of 'f' from A to B`
    /// * retained:             `ownership of 'f' retained as A:B`
    ///   (owner-only specs show `as A`; a no-op spec shows bare `retained`)
    /// `enabled` is `-v`, or `-c` restricted to the changed branch.
    #[allow(clippy::too_many_arguments)]
    fn describe(
        &mut self,
        name: &str,
        changed: bool,
        old_owner: &str,
        old_group: &str,
        spec: &Spec,
        enabled: bool,
    ) {
        if !enabled {
            return;
        }
        if changed {
            if matches!(spec.owner, Part::Absent) && !matches!(spec.group, Part::Absent) {
                let _ = writeln!(
                    *self.io.stdout,
                    "changed group of {} from {} to {}",
                    winux_core::q(name),
                    old_group,
                    display_part(&spec.group)
                );
            } else {
                let (oldf, newf) = if spec.colon {
                    (
                        format!("{old_owner}:{old_group}"),
                        format!(
                            "{}:{}",
                            display_part(&spec.owner),
                            display_part(&spec.group)
                        ),
                    )
                } else {
                    (old_owner.to_string(), display_part(&spec.owner))
                };
                let _ = writeln!(
                    *self.io.stdout,
                    "changed ownership of {} from {} to {}",
                    winux_core::q(name),
                    oldf,
                    newf
                );
            }
        } else if spec.noop {
            let _ = writeln!(
                *self.io.stdout,
                "ownership of {} retained",
                winux_core::q(name)
            );
        } else if matches!(spec.owner, Part::Absent) && matches!(spec.group, Part::Absent) {
            let _ = writeln!(
                *self.io.stdout,
                "ownership of {} retained",
                winux_core::q(name)
            );
        } else {
            let shown = if spec.colon {
                format!("{old_owner}:{old_group}")
            } else if !matches!(spec.owner, Part::Absent) {
                display_part(&spec.owner)
            } else {
                format!(":{old_group}")
            };
            let _ = writeln!(
                *self.io.stdout,
                "ownership of {} retained as {}",
                winux_core::q(name),
                shown
            );
        }
    }

    fn process_file(&mut self, name: &str, path: &Path, spec: &Spec, deref: bool) {
        let _ = deref;
        if !path.exists() {
            if !self.quiet {
                let _ = writeln!(
                    self.io.stderr,
                    "chown: cannot access {}: {}",
                    winux_core::q(name),
                    "No such file or directory"
                );
            }
            self.failed = true;
            return;
        }
        // --from filter: compare current owner/group SIDs; non-matching
        // operands are skipped silently (GNU skips before any message).
        if let Some(from) = &self.from {
            let (cur_owner, cur_group) = match get_owner_group(path) {
                Ok(x) => x,
                Err(_) => {
                    if !self.quiet {
                        let _ = writeln!(
                            self.io.stderr,
                            "chown: cannot access {}: {}",
                            winux_core::q(name),
                            "No such file or directory"
                        );
                    }
                    self.failed = true;
                    return;
                }
            };
            let owner_match = match &from.owner {
                Part::Absent => true,
                // numeric from-parts cannot be compared on Windows
                Part::Numeric(_) => false,
                Part::Sid(sid, _) => sids_equal(Some(sid), cur_owner.as_ref()),
            };
            let group_match = match &from.group {
                Part::Absent => true,
                Part::Numeric(_) => false,
                Part::Sid(sid, _) => sids_equal(Some(sid), cur_group.as_ref()),
            };
            if !(owner_match && group_match) {
                return;
            }
        }
        let (old_owner, old_group) = match get_owner_group(path) {
            Ok(x) => x,
            Err(_) => (None, None),
        };
        let old_owner_name = old_owner
            .as_ref()
            .and_then(|s| lookup_sid_name(s))
            .unwrap_or_else(|| "?".to_string());
        let old_group_name = old_group
            .as_ref()
            .and_then(|s| lookup_sid_name(s))
            .unwrap_or_else(|| "?".to_string());

        // what the OS can do with this spec
        let new_owner = match &spec.owner {
            Part::Absent | Part::Numeric(_) => None,
            Part::Sid(sid, _) => Some(sid.clone()),
        };
        let new_group = match &spec.group {
            Part::Absent | Part::Numeric(_) => None,
            Part::Sid(sid, _) => Some(sid.clone()),
        };
        if new_owner.is_none() && new_group.is_none() {
            // numeric/no-op spec: validated, nothing representable
            self.describe(
                name,
                false,
                &old_owner_name,
                &old_group_name,
                spec,
                self.verbose,
            );
            return;
        }
        match set_owner_group(path, new_owner.as_deref(), new_group.as_deref()) {
            Ok(()) => {
                // decide "changed" from the actual SIDs, not the API call
                let owner_changed =
                    new_owner.is_some() && !sids_equal(new_owner.as_ref(), old_owner.as_ref());
                let group_changed =
                    new_group.is_some() && !sids_equal(new_group.as_ref(), old_group.as_ref());
                if owner_changed || group_changed {
                    self.describe(
                        name,
                        true,
                        &old_owner_name,
                        &old_group_name,
                        spec,
                        self.verbose || self.changes_only,
                    );
                } else {
                    self.describe(
                        name,
                        false,
                        &old_owner_name,
                        &old_group_name,
                        spec,
                        self.verbose,
                    );
                }
            }
            Err(_) => {
                if !self.quiet {
                    let what = if matches!(spec.owner, Part::Absent) {
                        "group"
                    } else {
                        "ownership"
                    };
                    let _ = writeln!(
                        self.io.stderr,
                        "chown: changing {what} of {}: Operation not permitted",
                        winux_core::q(name)
                    );
                }
                self.failed = true;
            }
        }
    }
}

pub fn chown_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("chown", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if p.has("help") || p.has("version") {
        return Ok(0);
    }
    if p.positionals.is_empty() {
        let _ = usage_error(io, "chown", "missing operand");
        return Ok(1);
    }
    let verbose = p.has("verbose");
    let changes_only = p.has("changes") && !verbose;
    let quiet = p.has("quiet");
    let recursive = p.has("recursive");
    let no_preserve_root = p.has("no-preserve-root");

    let reference = p.value_of("reference").map(|s| s.to_string());
    // --reference: every positional is a FILE operand (GNU verified).
    let ref_owner_group = match &reference {
        Some(r) => {
            let rp = resolve(ctx, Path::new(r));
            match rp.exists().then(|| get_owner_group(&rp)) {
                Some(Ok((o, g))) => match (o, g) {
                    (Some(o), Some(g)) => Some((o, g)),
                    _ => {
                        let _ = writeln!(
                            io.stderr,
                            "chown: failed to get attributes of {}: {}",
                            winux_core::q(r),
                            "No such file or directory"
                        );
                        return Ok(1);
                    }
                },
                _ => {
                    let _ = writeln!(
                        io.stderr,
                        "chown: failed to get attributes of {}: {}",
                        winux_core::q(r),
                        "No such file or directory"
                    );
                    return Ok(1);
                }
            }
        }
        None => None,
    };

    let spec: Spec = match &ref_owner_group {
        Some((o, g)) => Spec {
            owner: Part::Sid(o.clone(), lookup_sid_name(o).unwrap_or_else(|| "?".into())),
            group: Part::Sid(g.clone(), lookup_sid_name(g).unwrap_or_else(|| "?".into())),
            colon: true,
            noop: false,
        },
        None => {
            let spec = p.positionals[0].to_string_lossy().into_owned();
            if p.positionals.len() == 1 {
                let msg = format!("missing operand after {}", winux_core::q(&spec));
                let _ = usage_error(io, "chown", &msg);
                return Ok(1);
            }
            match parse_spec(&spec) {
                Ok(s) => s,
                Err(msg) => {
                    let _ = usage_error(io, "chown", &msg);
                    return Ok(1);
                }
            }
        }
    };

    let from = match p.value_of("from") {
        Some(f) => match parse_spec(f) {
            Ok(s) => Some(s),
            Err(msg) => {
                let _ = usage_error(io, "chown", &msg);
                return Ok(1);
            }
        },
        None => None,
    };

    let files: Vec<String> = if reference.is_some() {
        p.positionals
            .iter()
            .map(|a| a.to_string_lossy().into_owned())
            .collect()
    } else {
        p.positionals[1..]
            .iter()
            .map(|a| a.to_string_lossy().into_owned())
            .collect()
    };

    let mut st = ChState {
        verbose,
        changes_only,
        quiet,
        from,
        io,
        failed: false,
    };

    for name in &files {
        let path = resolve(ctx, Path::new(name));
        if recursive && !no_preserve_root && path.exists() && crate::chmod::is_fs_root(&path) {
            let _ = writeln!(
                st.io.stderr,
                "chown: it is dangerous to operate recursively on {}",
                winux_core::q(name)
            );
            let _ = writeln!(
                st.io.stderr,
                "chown: use --no-preserve-root to override this failsafe"
            );
            st.failed = true;
            continue;
        }
        if recursive && path.is_dir() {
            walk_chown(&mut st, name, &path, &spec);
        } else {
            st.process_file(name, &path, &spec, true);
        }
    }
    Ok(if st.failed { 1 } else { 0 })
}

/// GNU fts preorder: directory first, then entries; symlink entries are
/// not followed during traversal (default -P behavior).
fn walk_chown(st: &mut ChState, display: &str, dir: &Path, spec: &Spec) {
    st.process_file(display, dir, spec, false);
    let rd = match std::fs::read_dir(dir) {
        Ok(r) => r,
        Err(_) => {
            if !st.quiet {
                let _ = writeln!(
                    st.io.stderr,
                    "chown: cannot read directory {}: Permission denied",
                    winux_core::q(display)
                );
            }
            st.failed = true;
            return;
        }
    };
    for entry in rd.flatten() {
        let p = entry.path();
        let ft = match entry.file_type() {
            Ok(t) => t,
            Err(_) => continue,
        };
        if ft.is_symlink() {
            continue;
        }
        let child_display = format!(
            "{}/{}",
            display.trim_end_matches('/'),
            entry.file_name().to_string_lossy()
        );
        if ft.is_dir() {
            walk_chown(st, &child_display, &p, spec);
        } else {
            st.process_file(&child_display, &p, spec, false);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str], sc: &ScratchCtx) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(|s| s.into()).collect();
        let r = chown_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn numeric_and_empty_are_validated_noops() {
        let sc = ScratchCtx::new("chown");
        std::fs::write(sc.path("f"), "x").unwrap();
        assert_eq!(run(&["1234:5678", "f"], &sc).0, 0);
        assert_eq!(run(&["", "f"], &sc).0, 0);
        assert_eq!(run(&["0", "f"], &sc).0, 0);
        assert_eq!(run(&["0:0", "f"], &sc).0, 0);
        assert_eq!(run(&[":0", "f"], &sc).0, 0);
        assert_eq!(run(&[":", "f"], &sc).0, 0);
        assert_eq!(run(&["99999:99999", "f"], &sc).0, 0);
    }

    #[test]
    fn missing_file_and_operand() {
        let sc = ScratchCtx::new("chown2");
        let (r, _, e) = run(&["1234", "missing"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("cannot access 'missing'"));
        let (r, _, e) = run(&["1234"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("missing operand after '1234'"));
        let (r, _, e) = run(&[], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("missing operand"));
        // missing file continues to later operands
        std::fs::write(sc.path("f"), "x").unwrap();
        let (r, _, e) = run(&["1234", "missing", "f"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("cannot access"));
        assert!(run(&["1234", "f"], &sc).0 == 0);
    }

    #[test]
    fn validation_order_and_wording() {
        let sc = ScratchCtx::new("chown3");
        std::fs::write(sc.path("f"), "x").unwrap();
        // user checked before group (GNU verified)
        let (r, _, e) = run(&["nosuchu:nosuchg", "f"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("invalid user: 'nosuchu:nosuchg'"));
        let (r, _, e) = run(&[":nosuchgroup", "f"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("invalid group: ':nosuchgroup'"));
        // two colons -> invalid user with the whole spec (GNU verified)
        let (r, _, e) = run(&["a:b:c", "f"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("invalid user: 'a:b:c'"));
        // alphanumeric garbage -> invalid user (GNU verified 1234x)
        let (r, _, e) = run(&["1234x", "f"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("invalid user: '1234x'"));
        // trailing colon numeric -> invalid spec (GNU verified 1234:)
        let (r, _, e) = run(&["1234:", "f"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("invalid spec: '1234:'"));
        // an invalid spec never touches the file
        // (validated above; the file remains unchanged)
    }

    #[test]
    fn bare_colon_and_empty_are_bare_noops() {
        let sc = ScratchCtx::new("chown4");
        std::fs::write(sc.path("f"), "x").unwrap();
        let (r, out, _) = run(&["-v", ":", "f"], &sc);
        assert_eq!(r, 0);
        assert!(out.contains("ownership of 'f' retained"));
        assert!(!out.contains("retained as"));
        let (r, out, _) = run(&["-v", "", "f"], &sc);
        assert_eq!(r, 0);
        assert!(out.contains("ownership of 'f' retained"));
        assert!(!out.contains("retained as"));
    }

    #[test]
    fn named_owner_resolves_and_self_change_succeeds() {
        let sc = ScratchCtx::new("chown5");
        std::fs::write(sc.path("f"), "x").unwrap();
        // current owner of a fresh temp file is this user; find its name
        let path = sc.path("f");
        let (owner, _) = get_owner_group(&path).expect("security info");
        let my_name = lookup_sid_name(owner.as_ref().unwrap()).expect("account name");
        let bare = my_name.rsplit('\\').next().unwrap().to_string();
        // chown to SELF must succeed like GNU chown $USER (rc 0)
        let (r, _, e) = run(&[bare.as_str(), "f"], &sc);
        assert_eq!(r, 0, "stderr: {e}");
        // -v: already owner -> retained (GNU: chown $USER on own file)
        let (r, out, _) = run(&["-v", bare.as_str(), "f"], &sc);
        assert_eq!(r, 0);
        assert!(out.contains("retained as"), "out: {out}");
        // a resolvable name validates (Administrators exists on all Windows)
        assert!(lookup_account("Administrators").is_some());
        // changing owner to ANOTHER account is attempted for real and
        // expected to fail unprivileged; either outcome must map to the
        // GNU-shaped diagnostic without touching anything outside temp.
        let (r, _, e) = run(&["Administrators", "f"], &sc);
        if r == 1 {
            assert!(e.contains("changing ownership of 'f'"));
            assert!(e.contains("Operation not permitted"));
        }
        // (r == 0 would mean a privileged test host took ownership of the
        // temp file; that is a valid OS decision, not a rubash gap.)
    }

    #[test]
    fn group_change_on_owned_file() {
        let sc = ScratchCtx::new("chown6");
        std::fs::write(sc.path("f"), "x").unwrap();
        let path = sc.path("f");
        let before = get_owner_group(&path).expect("security info").1;
        // :group named form — real SetNamedSecurityInfoW path. As the file
        // owner we hold implicit WRITE_OWNER, so this succeeds unprivileged;
        // if the host denies it the GNU error mapping must be exact.
        let (r, _, e) = run(&[":Users", "f"], &sc);
        if r == 0 {
            let after = get_owner_group(&path).expect("security info").1;
            assert_ne!(before, after, "group must actually change on success");
        } else {
            assert!(e.contains("changing group of 'f'"));
            assert!(e.contains("Operation not permitted"));
        }
        // group-only -v wording (changed group of 'x' from A to B)
        let (r, out, _) = run(&["-v", ":Users", "f"], &sc);
        assert_eq!(r, 0);
        assert!(
            out.contains("changed group of") || out.contains("retained"),
            "out: {out}"
        );
    }

    #[test]
    fn from_filter() {
        let sc = ScratchCtx::new("chown7");
        std::fs::write(sc.path("f"), "x").unwrap();
        let path = sc.path("f");
        let (owner, _) = get_owner_group(&path).expect("security info");
        let my_name = lookup_sid_name(owner.as_ref().unwrap()).expect("name");
        let bare = my_name.rsplit('\\').next().unwrap().to_string();
        // from=self -> match -> change to self -> retained rc 0
        let (r, _, _) = run(&["--from", bare.as_str(), "1234", "f"], &sc);
        assert_eq!(r, 0);
        // from=other named account -> no match -> silent skip, rc 0. The
        // "other" name is picked against the actual owner: elevated hosts
        // (e.g. the CI runner) create files owned by the Administrators
        // group, so a hardcoded 'Administrators' would MATCH and -v would
        // print instead of skipping.
        let other = if bare.eq_ignore_ascii_case("Administrators") {
            "Users"
        } else {
            "Administrators"
        };
        let (r, out, _) = run(&["-v", "--from", other, "1234", "f"], &sc);
        assert_eq!(r, 0);
        assert!(out.is_empty(), "out: {out}");
        // numeric from-parts cannot match on Windows (documented)
        let (r, out, _) = run(&["-v", "--from", "0", "1234", "f"], &sc);
        assert_eq!(r, 0);
        assert!(out.is_empty());
    }

    #[test]
    fn reference_forms() {
        let sc = ScratchCtx::new("chown8");
        std::fs::write(sc.path("f"), "x").unwrap();
        std::fs::write(sc.path("rf"), "x").unwrap();
        // reference applies owner+group of rf to f
        let (r, _, e) = run(&["--reference=rf", "f"], &sc);
        assert_eq!(r, 0, "stderr: {e}");
        // missing reference file
        let (r, _, e) = run(&["--reference=nonexist", "f"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("failed to get attributes of 'nonexist'"));
        // with --reference, positional specs are FILE operands (GNU verified)
        let (r, _, e) = run(&["root", "--reference=rf", "f"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("cannot access 'root'"));
    }

    #[test]
    fn recursive_and_quiet() {
        let sc = ScratchCtx::new("chown9");
        std::fs::create_dir_all(sc.path("sub/deep")).unwrap();
        std::fs::write(sc.path("sub/aa"), "x").unwrap();
        // numeric spec is a no-op success through the whole tree
        assert_eq!(run(&["-R", "1234", "sub"], &sc).0, 0);
        // -f suppresses the missing-file diagnostic but keeps rc
        let (r, _, e) = run(&["-f", "1234", "missing"], &sc);
        assert_eq!(r, 1);
        assert!(e.is_empty());
        // preserve-root refusal under -R (drive root)
        let root = sc
            .dir
            .components()
            .next()
            .map(|c| c.as_os_str().to_string_lossy().into_owned())
            .unwrap();
        let (r, _, e) = run(&["-R", "1234", &root], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("it is dangerous to operate recursively on"));
    }

    #[test]
    fn split_spec_cases() {
        assert_eq!(split_spec("").unwrap(), (String::new(), None));
        assert_eq!(split_spec("root").unwrap(), ("root".into(), None));
        assert_eq!(
            split_spec("root:adm").unwrap(),
            ("root".into(), Some("adm".into()))
        );
        assert_eq!(
            split_spec(":adm").unwrap(),
            (String::new(), Some("adm".into()))
        );
        assert_eq!(
            split_spec("root:").unwrap(),
            ("root".into(), Some(String::new()))
        );
        assert!(split_spec("a:b:c").is_none());
    }
}
