//! `regtool` — small Windows-native registry tool (#1488).
//!
//! Port of `src/commands/regtool.cpp`. Explicit `Io`/`Ctx`, zero globals.

use std::ffi::OsString;
use std::io::Write;

use crate::fsutil;
use winux_core::{CmdResult, Ctx, Io};

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
enum RegRoot {
    Hkcu,
    Hklm,
    Hkcr,
    Hku,
    Hkcc,
}

struct RegPath {
    root: RegRoot,
    subkey: String,
}

fn normalize_registry_path(raw: &str) -> String {
    raw.replace('\\', "/").trim_start_matches('/').to_owned()
}

fn parse_path(raw: &str) -> Result<RegPath, String> {
    let norm = normalize_registry_path(raw);
    let (root_name, subkey) = match norm.split_once('/') {
        Some((r, s)) => (r, s),
        None => (norm.as_str(), ""),
    };
    let root = if root_name.eq_ignore_ascii_case("HKCU")
        || root_name.eq_ignore_ascii_case("HKEY_CURRENT_USER")
    {
        RegRoot::Hkcu
    } else if root_name.eq_ignore_ascii_case("HKLM")
        || root_name.eq_ignore_ascii_case("HKEY_LOCAL_MACHINE")
    {
        RegRoot::Hklm
    } else if root_name.eq_ignore_ascii_case("HKCR")
        || root_name.eq_ignore_ascii_case("HKEY_CLASSES_ROOT")
    {
        RegRoot::Hkcr
    } else if root_name.eq_ignore_ascii_case("HKU")
        || root_name.eq_ignore_ascii_case("HKEY_USERS")
    {
        RegRoot::Hku
    } else if root_name.eq_ignore_ascii_case("HKCC")
        || root_name.eq_ignore_ascii_case("HKEY_CURRENT_CONFIG")
    {
        RegRoot::Hkcc
    } else {
        return Err(format!("unknown registry root '{root_name}'"));
    };
    Ok(RegPath {
        root,
        subkey: subkey.replace('/', "\\"),
    })
}

fn parse_reg_path_arg(raw: &str) -> Result<RegPath, String> {
    let p = parse_path(raw)?;
    if p.subkey.is_empty() {
        return Err("root-only paths are not supported".to_owned());
    }
    Ok(p)
}

fn value_name(name: &str) -> &str {
    if name == "@" || name == "." {
        ""
    } else {
        name
    }
}

#[cfg(windows)]
mod sys {
    use super::{RegPath, RegRoot};
    use crate::fsutil;
    use std::ffi::c_void;
    use std::io;
    use std::os::windows::ffi::OsStrExt;

    type Hkey = *mut c_void;
    const HKEY_CLASSES_ROOT: Hkey = 0x8000_0000u32 as usize as Hkey;
    const HKEY_CURRENT_USER: Hkey = 0x8000_0001u32 as usize as Hkey;
    const HKEY_LOCAL_MACHINE: Hkey = 0x8000_0002u32 as usize as Hkey;
    const HKEY_USERS: Hkey = 0x8000_0003u32 as usize as Hkey;
    const HKEY_CURRENT_CONFIG: Hkey = 0x8000_0005u32 as usize as Hkey;

    const ERROR_SUCCESS: i32 = 0;
    const KEY_QUERY_VALUE: u32 = 0x0001;
    const KEY_SET_VALUE: u32 = 0x0002;
    const KEY_ENUMERATE_SUB_KEYS: u32 = 0x0008;
    const STANDARD_RIGHTS_READ: u32 = 0x0002_0000;
    const SYNCHRONIZE: u32 = 0x0010_0000;
    const KEY_NOTIFY: u32 = 0x0010;
    const KEY_READ: u32 =
        (STANDARD_RIGHTS_READ | KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS | KEY_NOTIFY)
            & !SYNCHRONIZE;
    const REG_OPTION_NON_VOLATILE: u32 = 0;
    const REG_SZ: u32 = 1;
    const REG_EXPAND_SZ: u32 = 2;
    const REG_DWORD: u32 = 4;
    const REG_QWORD: u32 = 11;

    extern "system" {
        fn RegOpenKeyExW(
            h_key: Hkey,
            lp_sub_key: *const u16,
            ul_options: u32,
            sam_desired: u32,
            phk_result: *mut Hkey,
        ) -> i32;
        fn RegCreateKeyExW(
            h_key: Hkey,
            lp_sub_key: *const u16,
            reserved: u32,
            lp_class: *mut u16,
            dw_options: u32,
            sam_desired: u32,
            lp_security_attributes: *mut c_void,
            phk_result: *mut Hkey,
            lpdw_disposition: *mut u32,
        ) -> i32;
        fn RegCloseKey(h_key: Hkey) -> i32;
        fn RegQueryValueExW(
            h_key: Hkey,
            lp_value_name: *const u16,
            lp_reserved: *mut u32,
            lp_type: *mut u32,
            lp_data: *mut u8,
            lpcb_data: *mut u32,
        ) -> i32;
        fn RegQueryInfoKeyW(
            h_key: Hkey,
            lp_class: *mut u16,
            lpcch_class: *mut u32,
            lp_reserved: *mut u32,
            lpc_sub_keys: *mut u32,
            lpcb_max_sub_key_len: *mut u32,
            lpcb_max_class_len: *mut u32,
            lpc_values: *mut u32,
            lpcb_max_value_name_len: *mut u32,
            lpcb_max_value_len: *mut u32,
            lpcb_security_descriptor: *mut u32,
            lpft_last_write_time: *mut c_void,
        ) -> i32;
        fn RegEnumKeyExW(
            h_key: Hkey,
            dw_index: u32,
            lp_name: *mut u16,
            lpcch_name: *mut u32,
            lp_reserved: *mut u32,
            lp_class: *mut u16,
            lpcch_class: *mut u32,
            lpft_last_write_time: *mut c_void,
        ) -> i32;
        fn RegEnumValueW(
            h_key: Hkey,
            dw_index: u32,
            lp_value_name: *mut u16,
            lpcch_value_name: *mut u32,
            lp_reserved: *mut u32,
            lp_type: *mut u32,
            lp_data: *mut u8,
            lpcb_data: *mut u32,
        ) -> i32;
        fn RegSetValueExW(
            h_key: Hkey,
            lp_value_name: *const u16,
            reserved: u32,
            dw_type: u32,
            lp_data: *const u8,
            cb_data: u32,
        ) -> i32;
        fn RegDeleteValueW(h_key: Hkey, lp_value_name: *const u16) -> i32;
        fn RegDeleteTreeW(h_key: Hkey, lp_sub_key: *const u16) -> i32;
    }

    struct KeyGuard(Hkey);
    impl Drop for KeyGuard {
        fn drop(&mut self) {
            if !self.0.is_null() {
                unsafe { RegCloseKey(self.0) };
            }
        }
    }

    fn root_hkey(root: RegRoot) -> Hkey {
        match root {
            RegRoot::Hkcu => HKEY_CURRENT_USER,
            RegRoot::Hklm => HKEY_LOCAL_MACHINE,
            RegRoot::Hkcr => HKEY_CLASSES_ROOT,
            RegRoot::Hku => HKEY_USERS,
            RegRoot::Hkcc => HKEY_CURRENT_CONFIG,
        }
    }

    fn to_wide(s: &str) -> Vec<u16> {
        let mut w: Vec<u16> = std::ffi::OsStr::new(s).encode_wide().collect();
        w.push(0);
        w
    }

    fn win32_err(action: &str, status: i32) -> String {
        let e = io::Error::from_raw_os_error(status);
        format!("{action}: {}", fsutil::reason(&e))
    }

    pub fn list_key(path: &RegPath) -> Result<Vec<String>, String> {
        let sub = to_wide(&path.subkey);
        let mut hkey: Hkey = std::ptr::null_mut();
        let st = unsafe { RegOpenKeyExW(root_hkey(path.root), sub.as_ptr(), 0, KEY_READ, &mut hkey) };
        if st != ERROR_SUCCESS {
            return Err(win32_err("open key", st));
        }
        let _guard = KeyGuard(hkey);

        let mut subkeys = 0u32;
        let mut max_subkey = 0u32;
        let mut values = 0u32;
        let mut max_value = 0u32;
        let st = unsafe {
            RegQueryInfoKeyW(
                hkey,
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                &mut subkeys,
                &mut max_subkey,
                std::ptr::null_mut(),
                &mut values,
                &mut max_value,
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                std::ptr::null_mut(),
            )
        };
        if st != ERROR_SUCCESS {
            return Err(win32_err("query key", st));
        }

        let mut out = Vec::new();
        let mut buf = vec![0u16; max_subkey.max(1) as usize + 1];
        for i in 0..subkeys {
            let mut len = buf.len() as u32;
            let st = unsafe {
                RegEnumKeyExW(
                    hkey,
                    i,
                    buf.as_mut_ptr(),
                    &mut len,
                    std::ptr::null_mut(),
                    std::ptr::null_mut(),
                    std::ptr::null_mut(),
                    std::ptr::null_mut(),
                )
            };
            if st != ERROR_SUCCESS {
                return Err(win32_err("enumerate subkeys", st));
            }
            out.push(format!("{}/", String::from_utf16_lossy(&buf[..len as usize])));
        }

        let mut vbuf = vec![0u16; max_value.max(1) as usize + 1];
        for i in 0..values {
            let mut len = vbuf.len() as u32;
            let st = unsafe {
                RegEnumValueW(
                    hkey,
                    i,
                    vbuf.as_mut_ptr(),
                    &mut len,
                    std::ptr::null_mut(),
                    std::ptr::null_mut(),
                    std::ptr::null_mut(),
                    std::ptr::null_mut(),
                )
            };
            if st != ERROR_SUCCESS {
                return Err(win32_err("enumerate values", st));
            }
            if len == 0 {
                out.push("@".to_owned());
            } else {
                out.push(String::from_utf16_lossy(&vbuf[..len as usize]));
            }
        }
        Ok(out)
    }

    pub fn get_value(path: &RegPath, name: &str) -> Result<String, String> {
        let sub = to_wide(&path.subkey);
        let mut hkey: Hkey = std::ptr::null_mut();
        let st = unsafe {
            RegOpenKeyExW(root_hkey(path.root), sub.as_ptr(), 0, KEY_QUERY_VALUE, &mut hkey)
        };
        if st != ERROR_SUCCESS {
            return Err(win32_err("open key", st));
        }
        let _guard = KeyGuard(hkey);

        let wname = to_wide(name);
        let name_ptr = if name.is_empty() {
            std::ptr::null()
        } else {
            wname.as_ptr()
        };
        let mut rtype = 0u32;
        let mut bytes = 0u32;
        let st = unsafe {
            RegQueryValueExW(
                hkey,
                name_ptr,
                std::ptr::null_mut(),
                &mut rtype,
                std::ptr::null_mut(),
                &mut bytes,
            )
        };
        if st != ERROR_SUCCESS {
            return Err(win32_err("query value", st));
        }

        let mut data = vec![0u8; bytes.max(1) as usize];
        let st = unsafe {
            RegQueryValueExW(
                hkey,
                name_ptr,
                std::ptr::null_mut(),
                &mut rtype,
                data.as_mut_ptr(),
                &mut bytes,
            )
        };
        if st != ERROR_SUCCESS {
            return Err(win32_err("query value", st));
        }

        let slice = &data[..bytes as usize];
        match rtype {
            REG_SZ | REG_EXPAND_SZ => {
                let mut u16s = Vec::with_capacity(slice.len() / 2);
                for ch in slice.chunks_exact(2) {
                    u16s.push(u16::from_le_bytes([ch[0], ch[1]]));
                }
                while u16s.last() == Some(&0) {
                    u16s.pop();
                }
                Ok(String::from_utf16_lossy(&u16s))
            }
            REG_DWORD => {
                if slice.len() < 4 {
                    return Err("malformed REG_DWORD value".to_owned());
                }
                let val = u32::from_le_bytes([slice[0], slice[1], slice[2], slice[3]]);
                Ok(val.to_string())
            }
            REG_QWORD => {
                if slice.len() < 8 {
                    return Err("malformed REG_QWORD value".to_owned());
                }
                let val = u64::from_le_bytes([
                    slice[0], slice[1], slice[2], slice[3], slice[4], slice[5], slice[6], slice[7],
                ]);
                Ok(val.to_string())
            }
            _ => {
                let mut hex = String::with_capacity(slice.len() * 2);
                for b in slice {
                    use std::fmt::Write as _;
                    let _ = write!(hex, "{b:02x}");
                }
                Ok(hex)
            }
        }
    }

    pub fn set_value(path: &RegPath, name: &str, data: &str) -> Result<(), String> {
        let sub = to_wide(&path.subkey);
        let mut hkey: Hkey = std::ptr::null_mut();
        let mut disp = 0u32;
        let st = unsafe {
            RegCreateKeyExW(
                root_hkey(path.root),
                sub.as_ptr(),
                0,
                std::ptr::null_mut(),
                REG_OPTION_NON_VOLATILE,
                KEY_SET_VALUE,
                std::ptr::null_mut(),
                &mut hkey,
                &mut disp,
            )
        };
        if st != ERROR_SUCCESS {
            return Err(win32_err("create key", st));
        }
        let _guard = KeyGuard(hkey);

        let wname = to_wide(name);
        let name_ptr = if name.is_empty() {
            std::ptr::null()
        } else {
            wname.as_ptr()
        };
        let wdata = to_wide(data);
        let bytes = (wdata.len() * 2) as u32;
        let st = unsafe {
            RegSetValueExW(
                hkey,
                name_ptr,
                0,
                REG_SZ,
                wdata.as_ptr() as *const u8,
                bytes,
            )
        };
        if st != ERROR_SUCCESS {
            return Err(win32_err("set value", st));
        }
        Ok(())
    }

    pub fn remove_target(path: &RegPath, name: Option<&str>) -> Result<(), String> {
        if let Some(vname) = name {
            let sub = to_wide(&path.subkey);
            let mut hkey: Hkey = std::ptr::null_mut();
            let st = unsafe {
                RegOpenKeyExW(root_hkey(path.root), sub.as_ptr(), 0, KEY_SET_VALUE, &mut hkey)
            };
            if st != ERROR_SUCCESS {
                return Err(win32_err("open key", st));
            }
            let _guard = KeyGuard(hkey);
            let wname = to_wide(vname);
            let name_ptr = if vname.is_empty() {
                std::ptr::null()
            } else {
                wname.as_ptr()
            };
            let st = unsafe { RegDeleteValueW(hkey, name_ptr) };
            if st != ERROR_SUCCESS {
                return Err(win32_err("delete value", st));
            }
            Ok(())
        } else {
            let sub = to_wide(&path.subkey);
            let st = unsafe { RegDeleteTreeW(root_hkey(path.root), sub.as_ptr()) };
            if st != ERROR_SUCCESS {
                return Err(win32_err("delete key", st));
            }
            Ok(())
        }
    }
}

#[cfg(not(windows))]
mod sys {
    use super::RegPath;

    pub fn list_key(_path: &RegPath) -> Result<Vec<String>, String> {
        Err("Windows registry is only available on Windows".to_owned())
    }
    pub fn get_value(_path: &RegPath, _name: &str) -> Result<String, String> {
        Err("Windows registry is only available on Windows".to_owned())
    }
    pub fn set_value(_path: &RegPath, _name: &str, _data: &str) -> Result<(), String> {
        Err("Windows registry is only available on Windows".to_owned())
    }
    pub fn remove_target(_path: &RegPath, _name: Option<&str>) -> Result<(), String> {
        Err("Windows registry is only available on Windows".to_owned())
    }
}

fn fail(io: &mut Io, msg: &str) -> CmdResult {
    writeln!(io.stderr, "regtool: {msg}")?;
    Ok(1)
}

pub fn regtool_cmd(args: &[OsString], io: &mut Io, _ctx: &Ctx) -> CmdResult {
    let owned: Vec<String> = args
        .iter()
        .map(|a| a.to_string_lossy().into_owned())
        .collect();
    if owned.is_empty() {
        return fail(io, "usage: regtool ACTION KEY [VALUE] [DATA]");
    }
    let action = owned[0].as_str();
    if action == "--help" || action == "-h" {
        writeln!(
            io.stdout,
            "Usage: regtool ACTION KEY [VALUE] [DATA]\nSmall Windows-native registry tool."
        )?;
        return Ok(0);
    }
    if action == "--version" {
        writeln!(io.stdout, "regtool (WinuxCmd) 0.1.0")?;
        return Ok(0);
    }

    if action.eq_ignore_ascii_case("list") {
        if owned.len() != 2 {
            return fail(io, "usage: regtool list KEY");
        }
        let path = match parse_reg_path_arg(&owned[1]) {
            Ok(p) => p,
            Err(e) => return fail(io, &e),
        };
        match sys::list_key(&path) {
            Ok(entries) => {
                for entry in entries {
                    writeln!(io.stdout, "{entry}")?;
                }
                Ok(0)
            }
            Err(e) => fail(io, &e),
        }
    } else if action.eq_ignore_ascii_case("get") {
        if owned.len() != 2 && owned.len() != 3 {
            return fail(io, "usage: regtool get KEY [VALUE]");
        }
        let path = match parse_reg_path_arg(&owned[1]) {
            Ok(p) => p,
            Err(e) => return fail(io, &e),
        };
        let name = if owned.len() == 3 {
            value_name(&owned[2])
        } else {
            ""
        };
        match sys::get_value(&path, name) {
            Ok(val) => {
                writeln!(io.stdout, "{val}")?;
                Ok(0)
            }
            Err(e) => fail(io, &e),
        }
    } else if action.eq_ignore_ascii_case("set") {
        if owned.len() != 4 {
            return fail(io, "usage: regtool set KEY VALUE DATA");
        }
        let path = match parse_reg_path_arg(&owned[1]) {
            Ok(p) => p,
            Err(e) => return fail(io, &e),
        };
        match sys::set_value(&path, value_name(&owned[2]), &owned[3]) {
            Ok(()) => Ok(0),
            Err(e) => fail(io, &e),
        }
    } else if action.eq_ignore_ascii_case("remove")
        || action.eq_ignore_ascii_case("rm")
        || action.eq_ignore_ascii_case("delete")
    {
        if owned.len() != 2 && owned.len() != 3 {
            return fail(io, "usage: regtool remove KEY [VALUE]");
        }
        let path = match parse_reg_path_arg(&owned[1]) {
            Ok(p) => p,
            Err(e) => return fail(io, &e),
        };
        let name = if owned.len() == 3 {
            Some(value_name(&owned[2]))
        } else {
            None
        };
        match sys::remove_target(&path, name) {
            Ok(()) => Ok(0),
            Err(e) => fail(io, &e),
        }
    } else {
        fail(io, &format!("unknown action '{action}'"))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn parses_roots_and_rejects_bad_args() {
        let p = parse_reg_path_arg(r"HKLM\SOFTWARE\Microsoft\Cryptography").unwrap();
        assert_eq!(p.root, RegRoot::Hklm);
        assert_eq!(p.subkey, r"SOFTWARE\Microsoft\Cryptography");

        assert!(parse_reg_path_arg("HKCU").is_err());
        assert!(parse_reg_path_arg("NOPE/Software").is_err());

        let sc = ScratchCtx::new("regtool-err");
        let mut io = MemIo::new("");
        let rc = regtool_cmd(
            &[OsString::from("wat"), OsString::from("HKCU/Software")],
            &mut io.split(),
            &sc.ctx(),
        )
        .unwrap();
        assert_eq!(rc, 1);
        assert!(io.err().contains("unknown action"));

        let mut io2 = MemIo::new("");
        let rc2 = regtool_cmd(
            &[OsString::from("set"), OsString::from("HKCU/Software/X")],
            &mut io2.split(),
            &sc.ctx(),
        )
        .unwrap();
        assert_eq!(rc2, 1);
        assert!(io2.err().contains("usage: regtool set"));
    }
}
