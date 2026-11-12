//! Windows system-information FFI for the wave2 platform-value commands
//! (`uname`, `id`, `hostname`, `whoami`, `nproc`, `getconf`).
//!
//! Zero crates: raw kernel32/advapi32/ntdll declarations, the same API
//! family the 1.x C++ applets use. The Rust Windows value model MUST match
//! `src/commands/{uname,id,hostname,whoami,nproc,getconf}.cpp` on main,
//! because the differential gate's equivalence side for platform-inherent
//! output is the C++ Debug exe:
//! * uname: kernel name "Windows_NT", nodename = GetComputerNameW,
//!   release = "Major.Minor", version = build number (RtlGetVersion —
//!   GetVersionEx is manifest-shimmed), machine from
//!   GetNativeSystemInfo (x86_64 / aarch64 / i386 / unknown),
//!   processor "unknown", hardware platform "PC", OS "Windows_NT".
//! * id: OpenProcessToken + GetTokenInformation(TokenUser / TokenPrimaryGroup
//!   / TokenGroups), SID string via ConvertSidToStringSidW, account name via
//!   LookupAccountSidW; the well-known "None" group counts as unresolvable
//!   (main #1024).
//! * hostname/whoami: GetComputerNameW / GetUserNameW.
//! * nproc: GetProcessAffinityMask popcount for the default count,
//!   GetNativeSystemInfo dwNumberOfProcessors for --all.
//! * getconf: GetSystemInfo page size / processor count,
//!   GlobalMemoryStatus for PHYS_PAGES / AVPHYS_PAGES.
//!
//! Non-Windows builds get lossy env-var fallbacks so the workspace still
//! compiles; the gate runs on Windows only.

#[allow(clippy::upper_case_acronyms)]
pub type DWORD = u32;
#[allow(clippy::upper_case_acronyms)]
pub type WCHAR = u16;

#[cfg(windows)]
pub mod imp {
    use super::{DWORD, WCHAR};
    use std::ffi::c_void;

    pub type Handle = *mut c_void;

    #[repr(C)]
    pub struct OsVersionInfoW {
        pub size: DWORD,
        pub major: DWORD,
        pub minor: DWORD,
        pub build: DWORD,
        pub platform_id: DWORD,
        pub csd_version: [WCHAR; 128],
    }

    #[repr(C)]
    pub struct SystemInfo {
        pub arch: u16,
        pub reserved: u16,
        pub page_size: DWORD,
        pub min_addr: *mut c_void,
        pub max_addr: *mut c_void,
        pub active_mask: usize,
        pub num_processors: DWORD,
        pub processor_type: DWORD,
        pub allocation_granularity: DWORD,
        pub processor_level: u16,
        pub processor_revision: u16,
    }

    #[repr(C)]
    pub struct MemoryStatus {
        pub length: DWORD,
        pub memory_load: DWORD,
        pub total_phys: usize,
        pub avail_phys: usize,
        pub total_page_file: usize,
        pub avail_page_file: usize,
        pub total_virtual: usize,
        pub avail_virtual: usize,
    }

    #[link(name = "kernel32")]
    extern "system" {
        fn GetComputerNameW(buffer: *mut WCHAR, size: *mut DWORD) -> i32;
        fn GetNativeSystemInfo(info: *mut SystemInfo);
        fn GetProcessAffinityMask(
            process: Handle,
            process_mask: *mut usize,
            system_mask: *mut usize,
        ) -> i32;
        fn GetCurrentProcess() -> Handle;
        fn GlobalMemoryStatus(status: *mut MemoryStatus);
        fn CloseHandle(handle: Handle) -> i32;
    }

    #[link(name = "ntdll")]
    extern "system" {
        fn RtlGetVersion(info: *mut OsVersionInfoW) -> i32;
    }

    #[link(name = "advapi32")]
    extern "system" {
        fn GetUserNameW(buffer: *mut WCHAR, size: *mut DWORD) -> i32;
        fn OpenProcessToken(process: Handle, access: DWORD, token: *mut Handle) -> i32;
        fn GetTokenInformation(
            token: Handle,
            class: u32,
            info: *mut c_void,
            length: DWORD,
            return_length: *mut DWORD,
        ) -> i32;
        fn GetSidSubAuthorityCount(sid: *mut c_void) -> *mut u8;
        fn GetSidSubAuthority(sid: *mut c_void, index: u32) -> *mut u32;
        fn GetSidIdentifierAuthority(sid: *mut c_void) -> *mut u8;
        fn IsValidSid(sid: *mut c_void) -> i32;
        fn LookupAccountSidW(
            system_name: *const WCHAR,
            sid: *mut c_void,
            name: *mut WCHAR,
            name_size: *mut DWORD,
            domain: *mut WCHAR,
            domain_size: *mut DWORD,
            use_kind: *mut DWORD,
        ) -> i32;
        fn LookupAccountNameW(
            system_name: *const WCHAR,
            account_name: *const WCHAR,
            sid: *mut c_void,
            cb_sid: *mut DWORD,
            referenced_domain: *mut WCHAR,
            cb_domain: *mut DWORD,
            use_kind: *mut DWORD,
        ) -> i32;
    }

    #[link(name = "netapi32")]
    extern "system" {
        fn NetUserGetGroups(
            servername: *const WCHAR,
            username: *const WCHAR,
            level: DWORD,
            bufptr: *mut *mut u8,
            prefmaxlen: DWORD,
            entriesread: *mut DWORD,
            totalentries: *mut DWORD,
        ) -> u32;
        fn NetApiBufferFree(buffer: *mut u8) -> u32;
    }

    /// GROUP_USERS_INFO_0 layout (level 0 of NetUserGetGroups).
    #[repr(C)]
    struct GroupUsersInfo0 {
        name: *const WCHAR,
    }

    /// NERR_UserNotFound (lmerr.h): the groups no-such-user condition.
    pub const NERR_USER_NOT_FOUND: u32 = 2221;
    /// MAX_PREFERRED_LENGTH: let NetUserGetGroups size its own buffer.
    const MAX_PREFERRED_LENGTH: DWORD = u32::MAX;

    const TOKEN_QUERY: u32 = 0x8;
    const TOKEN_USER: u32 = 1;
    const TOKEN_GROUPS: u32 = 2;
    const TOKEN_PRIMARY_GROUP: u32 = 5;

    fn wide_to_string(buf: &[WCHAR]) -> String {
        let len = buf.iter().take_while(|&&c| c != 0).count();
        String::from_utf16_lossy(&buf[..len])
    }

    pub fn computer_name() -> String {
        let mut buf = [0u16; 256];
        let mut size = buf.len() as DWORD;
        let ok = unsafe { GetComputerNameW(buf.as_mut_ptr(), &mut size) };
        if ok != 0 {
            wide_to_string(&buf)
        } else {
            "unknown".to_string()
        }
    }

    pub fn os_version() -> (u32, u32, u32) {
        let mut info = OsVersionInfoW {
            size: std::mem::size_of::<OsVersionInfoW>() as DWORD,
            major: 0,
            minor: 0,
            build: 0,
            platform_id: 0,
            csd_version: [0; 128],
        };
        let ok = unsafe { RtlGetVersion(&mut info) };
        if ok == 0 {
            (info.major, info.minor, info.build)
        } else {
            (0, 0, 0)
        }
    }

    pub fn machine_arch() -> &'static str {
        let mut info: SystemInfo = unsafe { std::mem::zeroed() };
        unsafe { GetNativeSystemInfo(&mut info) };
        match info.arch {
            9 => "x86_64",   // PROCESSOR_ARCHITECTURE_AMD64
            12 => "aarch64", // PROCESSOR_ARCHITECTURE_ARM64
            0 => "i386",     // PROCESSOR_ARCHITECTURE_INTEL
            _ => "unknown",
        }
    }

    pub fn num_processors() -> u32 {
        let mut info: SystemInfo = unsafe { std::mem::zeroed() };
        unsafe { GetNativeSystemInfo(&mut info) };
        info.num_processors
    }

    pub fn available_processors() -> Option<u32> {
        let mut process_mask = 0usize;
        let mut system_mask = 0usize;
        let ok = unsafe {
            GetProcessAffinityMask(GetCurrentProcess(), &mut process_mask, &mut system_mask)
        };
        if ok != 0 && process_mask != 0 {
            Some(process_mask.count_ones())
        } else {
            None
        }
    }

    pub fn page_size() -> u32 {
        let mut info: SystemInfo = unsafe { std::mem::zeroed() };
        unsafe { GetNativeSystemInfo(&mut info) };
        info.page_size
    }

    /// (total_pages, avail_pages) like the C++ getconf PHYS_PAGES pair.
    pub fn phys_pages() -> (u64, u64) {
        let mut status = MemoryStatus {
            length: std::mem::size_of::<MemoryStatus>() as DWORD,
            memory_load: 0,
            total_phys: 0,
            avail_phys: 0,
            total_page_file: 0,
            avail_page_file: 0,
            total_virtual: 0,
            avail_virtual: 0,
        };
        unsafe { GlobalMemoryStatus(&mut status) };
        let page = page_size().max(1) as u64;
        (
            status.total_phys as u64 / page,
            status.avail_phys as u64 / page,
        )
    }

    pub fn user_name() -> Option<String> {
        let mut buf = [0u16; 257];
        let mut size = buf.len() as DWORD;
        let ok = unsafe { GetUserNameW(buf.as_mut_ptr(), &mut size) };
        if ok != 0 {
            Some(wide_to_string(&buf))
        } else {
            None
        }
    }

    /// An account resolved from a SID in this process token: numeric id is
    /// the SID string (the C++ Win32AccountInfo.id), name via
    /// LookupAccountSidW (empty when unresolvable).
    #[derive(Clone)]
    pub struct Account {
        pub id: String,
        pub name: String,
    }

    fn sid_to_account(sid: *mut c_void) -> Account {
        // [C++ parity] win32_account_id_from_sid (src/utils/win32.cppm): the
        // account id is the last subauthority (RID), remapped so NT-domain
        // accounts (S-1-5-21-...) get 0x30000+RID and integrity SIDs
        // (S-1-16-...) get 0x60000+RID — uid=197609(cmx), not a raw SID.
        let id = unsafe {
            if IsValidSid(sid) == 0 {
                String::new()
            } else {
                let count_ptr = GetSidSubAuthorityCount(sid);
                if count_ptr.is_null() || *count_ptr == 0 {
                    String::new()
                } else {
                    let count = *count_ptr;
                    let rid_ptr = GetSidSubAuthority(sid, u32::from(count - 1));
                    if rid_ptr.is_null() {
                        String::new()
                    } else {
                        let rid = *rid_ptr;
                        let authority = GetSidIdentifierAuthority(sid);
                        let authority_value = if authority.is_null() {
                            0u64
                        } else {
                            let b = std::slice::from_raw_parts(authority, 6);
                            u64::from(b[0]) << 40
                                | u64::from(b[1]) << 32
                                | u64::from(b[2]) << 24
                                | u64::from(b[3]) << 16
                                | u64::from(b[4]) << 8
                                | u64::from(b[5])
                        };
                        let first = GetSidSubAuthority(sid, 0);
                        let first_subauth = if first.is_null() { 0 } else { *first };
                        if authority_value == 5 && first_subauth == 21 && count >= 5 {
                            (0x30000u64 + u64::from(rid)).to_string()
                        } else if authority_value == 16 {
                            (0x60000u64 + u64::from(rid)).to_string()
                        } else {
                            rid.to_string()
                        }
                    }
                }
            }
        };
        let mut name = String::new();
        let mut name_buf = [0u16; 256];
        let mut name_size = name_buf.len() as DWORD;
        let mut domain_buf = [0u16; 256];
        let mut domain_size = domain_buf.len() as DWORD;
        let mut use_kind = 0u32;
        let ok = unsafe {
            LookupAccountSidW(
                std::ptr::null(),
                sid,
                name_buf.as_mut_ptr(),
                &mut name_size,
                domain_buf.as_mut_ptr(),
                &mut domain_size,
                &mut use_kind,
            )
        };
        if ok != 0 {
            name = wide_to_string(&name_buf);
        }
        Account { id, name }
    }

    fn token_class_sids(class: u32) -> Option<(Account, Vec<Account>)> {
        unsafe {
            let mut token: Handle = std::ptr::null_mut();
            if OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &mut token) == 0 {
                return None;
            }
            let mut needed = 0u32;
            let _ = GetTokenInformation(token, class, std::ptr::null_mut(), 0, &mut needed);
            if needed == 0 {
                let _ = CloseHandle(token);
                return None;
            }
            let mut buf = vec![0u8; needed as usize];
            let ok = GetTokenInformation(
                token,
                class,
                buf.as_mut_ptr() as *mut c_void,
                needed,
                &mut needed,
            );
            let _ = CloseHandle(token);
            if ok == 0 {
                return None;
            }
            // Layout (x64): TOKEN_GROUPS = ULONG GroupCount at 0, then a
            // SID_AND_ATTRIBUTES array at offset 8 ({PSID Sid, ULONG
            // Attributes} = 16 bytes each with padding). TOKEN_USER and
            // TOKEN_PRIMARY_GROUP end with a single SID_AND_ATTRIBUTES at
            // offset 0.
            if class == TOKEN_GROUPS {
                let count = u32::from_ne_bytes(buf[0..4].try_into().ok()?) as usize;
                let mut groups = Vec::new();
                for i in 0..count {
                    let entry = 8 + i * 16;
                    if entry + 8 > buf.len() {
                        break;
                    }
                    let sid =
                        usize::from_ne_bytes(buf[entry..entry + 8].try_into().ok()?) as *mut c_void;
                    if sid.is_null() {
                        continue;
                    }
                    groups.push(sid_to_account(sid));
                }
                Some((
                    Account {
                        id: String::new(),
                        name: String::new(),
                    },
                    groups,
                ))
            } else {
                // Single SID at offset 0 (TOKEN_USER) or offset 0
                // (TOKEN_PRIMARY_GROUP {SID_AND_ATTRIBUTES}).
                let sid = usize::from_ne_bytes(buf[0..8].try_into().ok()?) as *mut c_void;
                if sid.is_null() {
                    return None;
                }
                Some((sid_to_account(sid), Vec::new()))
            }
        }
    }

    /// Process identity for `id`: (user, primary_group, supplementary groups).
    /// Mirrors id_pipeline::current_identity on main: primary falls back to
    /// user, supplementary starts with the user entry, duplicates removed.
    pub fn process_identity() -> Option<(Account, Account, Vec<Account>)> {
        let (user, _) = token_class_sids(TOKEN_USER)?;
        let mut primary = match token_class_sids(TOKEN_PRIMARY_GROUP) {
            Some((p, _)) => p,
            None => Account {
                id: String::new(),
                name: String::new(),
            },
        };
        if primary.id.is_empty() {
            primary = user.clone();
        }
        let mut groups = vec![user.clone()];
        if let Some((_, extra)) = token_class_sids(TOKEN_GROUPS) {
            for g in extra {
                if g.id.is_empty() {
                    continue;
                }
                if !groups.iter().any(|e| e.id == g.id) {
                    groups.push(g);
                }
            }
        }
        Some((user, primary, groups))
    }

    /// Global-group names of a NAMED user via NetUserGetGroups (level 0),
    /// the same data source as the C++ groups_pipeline::collect_user_groups.
    /// `Err(code)` is the raw NERR status (NERR_USER_NOT_FOUND for unknown
    /// users; other codes are network/system failures).
    pub fn group_names_of_user(user: &str) -> Result<Vec<String>, u32> {
        let mut wuser: Vec<WCHAR> = user.encode_utf16().collect();
        wuser.push(0);
        unsafe {
            let mut buf: *mut u8 = std::ptr::null_mut();
            let mut entries = 0u32;
            let mut total = 0u32;
            let status = NetUserGetGroups(
                std::ptr::null(),
                wuser.as_ptr(),
                0,
                &mut buf,
                MAX_PREFERRED_LENGTH,
                &mut entries,
                &mut total,
            );
            if status != 0 {
                return Err(status);
            }
            let mut names = Vec::new();
            if !buf.is_null() {
                let groups = buf as *const GroupUsersInfo0;
                for i in 0..entries as usize {
                    let wname = (*groups.add(i)).name;
                    let name = if wname.is_null() {
                        String::new()
                    } else {
                        let mut len = 0usize;
                        while *wname.add(len) != 0 {
                            len += 1;
                        }
                        String::from_utf16_lossy(std::slice::from_raw_parts(wname, len))
                    };
                    if !name.is_empty() {
                        names.push(name);
                    }
                }
                NetApiBufferFree(buf);
            }
            Ok(names)
        }
    }

    /// Numeric account id (the SID-remap model of `sid_to_account`) for a
    /// NAMED account via LookupAccountNameW. `groups` uses this for the
    /// Windows well-known "None" group (#1024): the C++ applet prints its
    /// numeric id like an unresolvable gid.
    pub fn account_id_from_name(name: &str) -> Option<String> {
        let mut wname: Vec<WCHAR> = name.encode_utf16().collect();
        wname.push(0);
        unsafe {
            let mut sid_len = 0u32;
            let mut domain_len = 0u32;
            let mut kind = 0u32;
            let _ = LookupAccountNameW(
                std::ptr::null(),
                wname.as_ptr(),
                std::ptr::null_mut(),
                &mut sid_len,
                std::ptr::null_mut(),
                &mut domain_len,
                &mut kind,
            );
            if sid_len == 0 {
                return None;
            }
            let mut sid = vec![0u8; sid_len as usize];
            let mut domain = [0u16; 256];
            domain_len = domain.len() as u32;
            let ok = LookupAccountNameW(
                std::ptr::null(),
                wname.as_ptr(),
                sid.as_mut_ptr() as *mut c_void,
                &mut sid_len,
                domain.as_mut_ptr(),
                &mut domain_len,
                &mut kind,
            );
            if ok == 0 {
                return None;
            }
            Some(sid_to_account(sid.as_mut_ptr() as *mut c_void).id)
        }
    }
}

#[cfg(not(windows))]
pub mod imp {
    pub fn computer_name() -> String {
        std::env::var("COMPUTERNAME").unwrap_or_else(|_| "unknown".into())
    }

    pub fn os_version() -> (u32, u32, u32) {
        (10, 0, 0)
    }

    pub fn machine_arch() -> &'static str {
        "unknown"
    }

    pub fn num_processors() -> u32 {
        std::thread::available_parallelism()
            .map(|n| n.get() as u32)
            .unwrap_or(1)
    }

    pub fn available_processors() -> Option<u32> {
        std::thread::available_parallelism().map(|n| n.get() as u32)
    }

    pub fn page_size() -> u32 {
        4096
    }

    pub fn phys_pages() -> (u64, u64) {
        (0, 0)
    }

    pub fn user_name() -> Option<String> {
        std::env::var("USERNAME").ok()
    }

    #[derive(Clone)]
    pub struct Account {
        pub id: String,
        pub name: String,
    }

    pub fn process_identity() -> Option<(Account, Account, Vec<Account>)> {
        let name = user_name()?;
        let acc = Account {
            id: "0".to_string(),
            name,
        };
        Some((acc.clone(), acc.clone(), vec![acc]))
    }

    pub fn group_names_of_user(_user: &str) -> Result<Vec<String>, u32> {
        Err(NERR_USER_NOT_FOUND)
    }

    pub fn account_id_from_name(_name: &str) -> Option<String> {
        None
    }

    pub const NERR_USER_NOT_FOUND: u32 = 2221;
}
