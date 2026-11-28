//! `getfacl` — print Windows file ACLs in a stable text format (#1488).
//!
//! Uses `GetNamedSecurityInfoW` / `GetAce` / `LookupAccountSidW` on Windows
//! (with metadata fallback on non-Windows hosts), explicit `Io`/`Ctx`, and
//! zero globals.

use std::ffi::OsString;
use std::io::Write;

use crate::argopt::{self, OptSpec};
use crate::fsutil;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('a'),
        long: "access",
        takes_value: false,
    },
    OptSpec {
        short: Some('d'),
        long: "default",
        takes_value: false,
    },
    OptSpec {
        short: Some('R'),
        long: "recursive",
        takes_value: false,
    },
    OptSpec {
        short: Some('p'),
        long: "absolute-names",
        takes_value: false,
    },
    OptSpec {
        short: Some('n'),
        long: "numeric",
        takes_value: false,
    },
    OptSpec {
        short: Some('c'),
        long: "omit-header",
        takes_value: false,
    },
    OptSpec {
        short: Some('s'),
        long: "skip-base",
        takes_value: false,
    },
    OptSpec {
        short: Some('t'),
        long: "tabular",
        takes_value: false,
    },
];

struct Config {
    _access: bool,
    _default_acl: bool,
    _recursive: bool,
    absolute_names: bool,
    numeric: bool,
    _omit_header: bool,
    _skip_base: bool,
    _tabular: bool,
}

fn display_path(path: &str, cfg: &Config) -> String {
    if cfg.absolute_names {
        path.to_owned()
    } else {
        path.trim_start_matches(['/', '\\']).to_owned()
    }
}

fn access_mask_to_rwx(mask: u32) -> String {
    const FILE_READ_DATA: u32 = 0x0001;
    const FILE_WRITE_DATA: u32 = 0x0002;
    const FILE_APPEND_DATA: u32 = 0x0004;
    const FILE_READ_EA: u32 = 0x0008;
    const FILE_WRITE_EA: u32 = 0x0010;
    const FILE_EXECUTE: u32 = 0x0020;
    const FILE_READ_ATTRIBUTES: u32 = 0x0080;
    const FILE_WRITE_ATTRIBUTES: u32 = 0x0100;
    const DELETE: u32 = 0x0001_0000;
    const WRITE_DAC: u32 = 0x0004_0000;
    const WRITE_OWNER: u32 = 0x0008_0000;
    const GENERIC_EXECUTE: u32 = 0x2000_0000;
    const GENERIC_WRITE: u32 = 0x4000_0000;
    const GENERIC_READ: u32 = 0x8000_0000;
    const STANDARD_RIGHTS_READ: u32 = 0x0002_0000;
    const SYNCHRONIZE: u32 = 0x0010_0000;
    const FILE_GENERIC_READ: u32 =
        STANDARD_RIGHTS_READ | FILE_READ_DATA | FILE_READ_ATTRIBUTES | FILE_READ_EA | SYNCHRONIZE;
    const FILE_GENERIC_WRITE: u32 = STANDARD_RIGHTS_READ
        | FILE_WRITE_DATA
        | FILE_WRITE_ATTRIBUTES
        | FILE_WRITE_EA
        | FILE_APPEND_DATA
        | SYNCHRONIZE;
    const FILE_GENERIC_EXECUTE: u32 =
        STANDARD_RIGHTS_READ | FILE_EXECUTE | FILE_READ_ATTRIBUTES | SYNCHRONIZE;

    let read = (mask
        & (FILE_GENERIC_READ | GENERIC_READ | FILE_READ_DATA | FILE_READ_ATTRIBUTES | FILE_READ_EA))
        != 0;
    let write = (mask
        & (FILE_GENERIC_WRITE
            | GENERIC_WRITE
            | FILE_WRITE_DATA
            | FILE_APPEND_DATA
            | FILE_WRITE_ATTRIBUTES
            | FILE_WRITE_EA
            | DELETE
            | WRITE_DAC
            | WRITE_OWNER))
        != 0;
    let exec = (mask & (FILE_GENERIC_EXECUTE | GENERIC_EXECUTE | FILE_EXECUTE)) != 0;
    let mut out = String::with_capacity(3);
    out.push(if read { 'r' } else { '-' });
    out.push(if write { 'w' } else { '-' });
    out.push(if exec { 'x' } else { '-' });
    out
}

#[cfg(windows)]
mod sys {
    use super::access_mask_to_rwx;
    use std::ffi::c_void;
    use std::io;
    use std::os::windows::ffi::OsStrExt;
    use std::path::Path;

    const SE_FILE_OBJECT: u32 = 1;
    const OWNER_SECURITY_INFORMATION: u32 = 0x0000_0001;
    const GROUP_SECURITY_INFORMATION: u32 = 0x0000_0002;
    const DACL_SECURITY_INFORMATION: u32 = 0x0000_0004;
    const ACCESS_ALLOWED_ACE_TYPE: u8 = 0;
    const ACCESS_DENIED_ACE_TYPE: u8 = 1;

    #[repr(C)]
    struct Acl {
        acl_revision: u8,
        sbz1: u8,
        acl_size: u16,
        ace_count: u16,
        sbz2: u16,
    }

    #[repr(C)]
    struct AceHeader {
        ace_type: u8,
        ace_flags: u8,
        ace_size: u16,
    }

    #[repr(C)]
    struct AccessAce {
        header: AceHeader,
        mask: u32,
        sid_start: u32,
    }

    #[repr(C)]
    struct SidIdentifierAuthority {
        value: [u8; 6],
    }

    extern "system" {
        fn GetNamedSecurityInfoW(
            p_object_name: *const u16,
            object_type: u32,
            security_info: u32,
            ppsid_owner: *mut *mut c_void,
            ppsid_group: *mut *mut c_void,
            pp_dacl: *mut *mut Acl,
            pp_sacl: *mut *mut Acl,
            pp_security_descriptor: *mut *mut c_void,
        ) -> u32;
        fn GetAce(p_acl: *mut Acl, dw_ace_index: u32, p_ace: *mut *mut c_void) -> i32;
        fn LocalFree(h_mem: *mut c_void) -> *mut c_void;
        fn IsValidSid(p_sid: *mut c_void) -> i32;
        fn GetSidSubAuthorityCount(p_sid: *mut c_void) -> *mut u8;
        fn GetSidSubAuthority(p_sid: *mut c_void, n_sub_authority: u32) -> *mut u32;
        fn GetSidIdentifierAuthority(p_sid: *mut c_void) -> *mut SidIdentifierAuthority;
        fn LookupAccountSidW(
            lp_system_name: *const u16,
            sid: *mut c_void,
            name: *mut u16,
            cch_name: *mut u32,
            referenced_domain_name: *mut u16,
            cch_referenced_domain_name: *mut u32,
            pe_use: *mut u32,
        ) -> i32;
    }

    fn sid_to_id(sid: *mut c_void) -> Option<String> {
        unsafe {
            if sid.is_null() || IsValidSid(sid) == 0 {
                return None;
            }
            let count = *GetSidSubAuthorityCount(sid);
            if count == 0 {
                return None;
            }
            let rid = *GetSidSubAuthority(sid, (count - 1) as u32);
            let auth = GetSidIdentifierAuthority(sid);
            let value = std::ptr::read_unaligned(&(*auth).value);
            let mut authority: u64 = 0;
            for b in value {
                authority = (authority << 8) | b as u64;
            }
            let first = *GetSidSubAuthority(sid, 0);
            if authority == 5 && first == 21 && count >= 5 {
                return Some((0x30000u64 + rid as u64).to_string());
            }
            if authority == 16 {
                return Some((0x60000u64 + rid as u64).to_string());
            }
            Some(rid.to_string())
        }
    }

    fn lookup_sid_name(sid: *mut c_void) -> String {
        unsafe {
            let mut name_size = 0u32;
            let mut domain_size = 0u32;
            let mut use_ = 0u32;
            LookupAccountSidW(
                std::ptr::null(),
                sid,
                std::ptr::null_mut(),
                &mut name_size,
                std::ptr::null_mut(),
                &mut domain_size,
                &mut use_,
            );
            if name_size == 0 {
                return String::new();
            }
            let mut name = vec![0u16; name_size as usize];
            let mut domain = vec![0u16; domain_size.max(1) as usize];
            let mut dsize = domain_size;
            let rc = LookupAccountSidW(
                std::ptr::null(),
                sid,
                name.as_mut_ptr(),
                &mut name_size,
                domain.as_mut_ptr(),
                &mut dsize,
                &mut use_,
            );
            if rc == 0 {
                return String::new();
            }
            let len = name.iter().take_while(|&&c| c != 0).count();
            String::from_utf16_lossy(&name[..len])
        }
    }

    fn account_display(sid: *mut c_void, numeric: bool) -> String {
        if sid.is_null() {
            return String::new();
        }
        let id = sid_to_id(sid).unwrap_or_default();
        if numeric {
            return if id.is_empty() {
                "0".to_owned()
            } else {
                id
            };
        }
        let name = lookup_sid_name(sid);
        if name.is_empty() {
            if id.is_empty() {
                "UNKNOWN".to_owned()
            } else {
                id
            }
        } else {
            name
        }
    }

    pub struct AclInfo {
        pub owner: String,
        pub group: String,
        pub entries: Vec<String>,
    }

    pub fn read_acl(path: &Path, numeric: bool) -> io::Result<AclInfo> {
        let mut w: Vec<u16> = path.as_os_str().encode_wide().collect();
        w.push(0);
        let mut owner_sid: *mut c_void = std::ptr::null_mut();
        let mut group_sid: *mut c_void = std::ptr::null_mut();
        let mut dacl: *mut Acl = std::ptr::null_mut();
        let mut sd: *mut c_void = std::ptr::null_mut();

        let status = unsafe {
            GetNamedSecurityInfoW(
                w.as_ptr(),
                SE_FILE_OBJECT,
                OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                &mut owner_sid,
                &mut group_sid,
                &mut dacl,
                std::ptr::null_mut(),
                &mut sd,
            )
        };
        if status != 0 {
            if !sd.is_null() {
                unsafe { LocalFree(sd) };
            }
            return Err(io::Error::from_raw_os_error(status as i32));
        }

        let owner = account_display(owner_sid, numeric);
        let group = account_display(group_sid, numeric);
        let mut entries = Vec::new();
        if dacl.is_null() {
            entries.push("user::rwx".to_owned());
            entries.push("group::rwx".to_owned());
            entries.push("other::rwx".to_owned());
        } else {
            let count = unsafe { (*dacl).ace_count } as u32;
            for i in 0..count {
                let mut raw_ace: *mut c_void = std::ptr::null_mut();
                if unsafe { GetAce(dacl, i, &mut raw_ace) } == 0 || raw_ace.is_null() {
                    if !sd.is_null() {
                        unsafe { LocalFree(sd) };
                    }
                    return Err(io::Error::new(io::ErrorKind::Other, "cannot read ACL entry"));
                }
                let header = unsafe { &*(raw_ace as *const AceHeader) };
                let prefix = match header.ace_type {
                    ACCESS_ALLOWED_ACE_TYPE => "user",
                    ACCESS_DENIED_ACE_TYPE => "deny",
                    _ => continue,
                };
                let ace = unsafe { &*(raw_ace as *const AccessAce) };
                let sid = (&ace.sid_start as *const u32) as *mut c_void;
                entries.push(format!(
                    "{prefix}:{}:{}",
                    account_display(sid, numeric),
                    access_mask_to_rwx(ace.mask)
                ));
            }
        }
        if !sd.is_null() {
            unsafe { LocalFree(sd) };
        }
        Ok(AclInfo {
            owner,
            group,
            entries,
        })
    }
}

#[cfg(not(windows))]
mod sys {
    use std::io;
    use std::path::Path;

    pub struct AclInfo {
        pub owner: String,
        pub group: String,
        pub entries: Vec<String>,
    }

    pub fn read_acl(path: &Path, numeric: bool) -> io::Result<AclInfo> {
        let _ = std::fs::metadata(path)?;
        let acct = if numeric { "0" } else { "root" };
        Ok(AclInfo {
            owner: acct.to_owned(),
            group: acct.to_owned(),
            entries: vec![
                "user::rwx".to_owned(),
                "group::r-x".to_owned(),
                "other::r-x".to_owned(),
            ],
        })
    }
}

fn print_acl_for_file(raw: &str, cfg: &Config, io: &mut Io, ctx: &Ctx) -> bool {
    let resolved = fsutil::resolve(ctx, raw);
    let info = match sys::read_acl(&resolved, cfg.numeric) {
        Ok(i) => i,
        Err(e) => {
            let _ = writeln!(io.stderr, "getfacl: {raw}: {}", fsutil::reason(&e));
            return false;
        }
    };
    let _ = writeln!(io.stdout, "# file: {}", display_path(raw, cfg));
    let _ = writeln!(io.stdout, "# owner: {}", info.owner);
    let _ = writeln!(io.stdout, "# group: {}", info.group);
    for entry in info.entries {
        let _ = writeln!(io.stdout, "{entry}");
    }
    true
}

pub fn getfacl_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("getfacl", args, OPTS, io, ctx, 1) {
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
                "Usage: getfacl [OPTION]... FILE...\nPrint Windows file ACLs in a stable text format."
            )?;
        } else {
            writeln!(io.stdout, "getfacl (WinuxCmd) 0.1.0")?;
        }
        return Ok(0);
    }

    let default_acl = p.has("default");
    let cfg = Config {
        _access: !default_acl,
        _default_acl: default_acl,
        _recursive: p.has("recursive"),
        absolute_names: p.has("absolute-names"),
        numeric: p.has("numeric"),
        _omit_header: p.has("omit-header"),
        _skip_base: p.has("skip-base"),
        _tabular: p.has("tabular"),
    };

    if p.positionals.is_empty() {
        writeln!(io.stderr, "getfacl: missing file operand")?;
        return Ok(1);
    }

    let mut ok = true;
    for file in &p.positionals {
        let s = file.to_string_lossy();
        if !print_acl_for_file(&s, &cfg, io, ctx) {
            ok = false;
        }
    }
    Ok(if ok { 0 } else { 1 })
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn missing_operand_fails() {
        let sc = ScratchCtx::new("getfacl-missing");
        let mut io = MemIo::new("");
        let rc = getfacl_cmd(&[], &mut io.split(), &sc.ctx()).unwrap();
        assert_eq!(rc, 1);
        assert!(io.err().contains("missing file operand"));
    }

    #[test]
    fn prints_acl_header_and_numeric() {
        let sc = ScratchCtx::new("getfacl-hdr");
        std::fs::write(sc.path("file.txt"), b"data").unwrap();

        let mut io = MemIo::new("");
        let rc = getfacl_cmd(&[OsString::from("file.txt")], &mut io.split(), &sc.ctx()).unwrap();
        assert_eq!(rc, 0);
        let out = io.out();
        assert!(out.contains("# file: file.txt"));
        assert!(out.contains("# owner:"));
        assert!(out.contains("# group:"));

        let mut io_num = MemIo::new("");
        let rc_num = getfacl_cmd(
            &[OsString::from("--numeric"), OsString::from("file.txt")],
            &mut io_num.split(),
            &sc.ctx(),
        )
        .unwrap();
        assert_eq!(rc_num, 0);
        assert!(io_num.out().contains("# owner:"));
    }

    #[test]
    fn display_path_strips_leading_slashes() {
        let cfg_rel = Config {
            _access: true,
            _default_acl: false,
            _recursive: false,
            absolute_names: false,
            numeric: false,
            _omit_header: false,
            _skip_base: false,
            _tabular: false,
        };
        assert_eq!(display_path("/foo/bar", &cfg_rel), "foo/bar");
        let cfg_abs = Config {
            absolute_names: true,
            ..cfg_rel
        };
        assert_eq!(display_path("/foo/bar", &cfg_abs), "/foo/bar");
    }
}
