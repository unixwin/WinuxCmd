//! `ldd` — print PE import dependencies for Windows executables,
//! ported for WinuxCmd 2.0 (#1488).
//!
//! Pure std-only PE32 / PE32+ import directory parser with `fsutil::resolve`,
//! explicit `Io`/`Ctx`, and `SearchPathW` DLL resolution on Windows.

use std::ffi::OsString;
use std::io::Write;

use crate::argopt;
use crate::fsutil;
use crate::gnuhelp;
use winux_core::{usage_error, CmdResult, Ctx, Io};

const LDD_HELP: &str = "\
Usage: ldd [OPTION]... FILE...
Print PE import dependencies for Windows executables.

  -d, --data-relocs      process data relocations
  -r, --function-relocs  process data and function relocations
  -n, --name             print only imported DLL names
  -u, --unused           print unused direct dependencies
  -v, --verbose          print all information
  -h, --help             display this help and exit
  -V, --version          output version information and exit
";

struct Section {
    virtual_address: u32,
    virtual_size: u32,
    raw_offset: u32,
    raw_size: u32,
}

fn read_u16_le(data: &[u8], off: usize) -> Option<u16> {
    let bytes: [u8; 2] = data.get(off..off + 2)?.try_into().ok()?;
    Some(u16::from_le_bytes(bytes))
}

fn read_u32_le(data: &[u8], off: usize) -> Option<u32> {
    let bytes: [u8; 4] = data.get(off..off + 4)?.try_into().ok()?;
    Some(u32::from_le_bytes(bytes))
}

fn read_i32_le(data: &[u8], off: usize) -> Option<i32> {
    let bytes: [u8; 4] = data.get(off..off + 4)?.try_into().ok()?;
    Some(i32::from_le_bytes(bytes))
}

fn rva_to_offset(rva: u32, sections: &[Section]) -> Option<usize> {
    for s in sections {
        let span = s.virtual_size.max(s.raw_size);
        if rva >= s.virtual_address && rva < s.virtual_address.saturating_add(span) {
            return Some((s.raw_offset + (rva - s.virtual_address)) as usize);
        }
    }
    None
}

fn read_c_string(data: &[u8], off: usize) -> Option<String> {
    let slice = data.get(off..)?;
    let end = slice.iter().take(1040).position(|&b| b == 0)?;
    Some(String::from_utf8_lossy(&slice[..end]).into_owned())
}

fn imported_dlls(data: &[u8]) -> Result<Vec<String>, &'static str> {
    if read_u16_le(data, 0) != Some(0x5A4D) {
        return Err("not a PE executable");
    }
    let lfanew = read_i32_le(data, 0x3C).ok_or("invalid PE header")?;
    if lfanew < 0 {
        return Err("invalid PE header");
    }
    let nt_offset = lfanew as usize;
    if read_u32_le(data, nt_offset) != Some(0x0000_4550) {
        return Err("invalid PE header");
    }

    let file_header_off = nt_offset + 4;
    let num_sections = read_u16_le(data, file_header_off + 2).ok_or("truncated PE header")?;
    let size_of_opt = read_u16_le(data, file_header_off + 16).ok_or("truncated PE header")?;

    let opt_offset = file_header_off + 20;
    let magic = read_u16_le(data, opt_offset).ok_or("truncated optional header")?;
    let import_rva = match magic {
        0x020B => {
            if opt_offset + 240 > data.len() {
                return Err("truncated PE32+ header");
            }
            read_u32_le(data, opt_offset + 120).ok_or("truncated PE32+ header")?
        }
        0x010B => {
            if opt_offset + 224 > data.len() {
                return Err("truncated PE32 header");
            }
            read_u32_le(data, opt_offset + 104).ok_or("truncated PE32 header")?
        }
        _ => return Err("unknown PE optional header"),
    };

    let section_offset = opt_offset + size_of_opt as usize;
    let mut sections = Vec::with_capacity(num_sections as usize);
    for i in 0..(num_sections as usize) {
        let off = section_offset + i * 40;
        if off + 40 > data.len() {
            return Err("truncated section table");
        }
        sections.push(Section {
            virtual_size: read_u32_le(data, off + 8).unwrap_or(0),
            virtual_address: read_u32_le(data, off + 12).unwrap_or(0),
            raw_size: read_u32_le(data, off + 16).unwrap_or(0),
            raw_offset: read_u32_le(data, off + 20).unwrap_or(0),
        });
    }

    if import_rva == 0 {
        return Ok(Vec::new());
    }
    let import_offset = rva_to_offset(import_rva, &sections).ok_or("invalid import table")?;

    let mut imports = Vec::new();
    let mut off = import_offset;
    loop {
        if off + 20 > data.len() {
            return Err("truncated import table");
        }
        let oft = read_u32_le(data, off).unwrap_or(0);
        let name_rva = read_u32_le(data, off + 12).unwrap_or(0);
        let ft = read_u32_le(data, off + 16).unwrap_or(0);
        if oft == 0 && ft == 0 && name_rva == 0 {
            break;
        }
        if let Some(name_off) = rva_to_offset(name_rva, &sections) {
            if let Some(name) = read_c_string(data, name_off) {
                if !name.is_empty() {
                    imports.push(name);
                }
            }
        }
        off += 20;
    }

    imports.sort_by_key(|s| s.to_ascii_lowercase());
    imports.dedup_by(|a, b| a.eq_ignore_ascii_case(b));
    Ok(imports)
}

#[cfg(windows)]
fn resolve_dll(dll_name: &str) -> Option<String> {
    #[link(name = "kernel32")]
    extern "system" {
        fn SearchPathW(
            lp_path: *const u16,
            lp_file_name: *const u16,
            lp_extension: *const u16,
            n_buffer_length: u32,
            lp_buffer: *mut u16,
            lp_file_part: *mut *mut u16,
        ) -> u32;
    }
    let mut wide: Vec<u16> = dll_name.encode_utf16().collect();
    wide.push(0);
    let needed = unsafe {
        SearchPathW(
            std::ptr::null(),
            wide.as_ptr(),
            std::ptr::null(),
            0,
            std::ptr::null_mut(),
            std::ptr::null_mut(),
        )
    };
    if needed == 0 {
        return None;
    }
    let mut buf = vec![0u16; needed as usize];
    let written = unsafe {
        SearchPathW(
            std::ptr::null(),
            wide.as_ptr(),
            std::ptr::null(),
            buf.len() as u32,
            buf.as_mut_ptr(),
            std::ptr::null_mut(),
        )
    };
    if written == 0 {
        return None;
    }
    buf.truncate(written as usize);
    Some(String::from_utf16_lossy(&buf))
}

#[cfg(not(windows))]
fn resolve_dll(_dll_name: &str) -> Option<String> {
    None
}

pub fn ldd_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut names_only = false;
    let mut unused_only = false;
    let mut verbose = false;
    let mut positionals: Vec<String> = Vec::new();
    let mut no_more_opts = false;

    for a in args {
        let s = a.to_string_lossy();
        if no_more_opts {
            positionals.push(s.into_owned());
            continue;
        }
        if s == "--" {
            no_more_opts = true;
            continue;
        }
        if s == "-h" || s == "--help" {
            io.stdout.write_all(LDD_HELP.as_bytes())?;
            return Ok(0);
        }
        if s == "-V" || s == "--version" {
            write!(
                io.stdout,
                "{}",
                gnuhelp::coreutils_version("ldd", "WinuxCmd")
            )?;
            return Ok(0);
        }
        if s == "--data-relocs" || s == "-d" || s == "--function-relocs" || s == "-r" {
            continue;
        }
        if s == "-n" || s == "--name" {
            names_only = true;
            continue;
        }
        if s == "-u" || s == "--unused" {
            unused_only = true;
            continue;
        }
        if s == "-v" || s == "--verbose" {
            verbose = true;
            continue;
        }
        if s.starts_with('-') && s.len() > 1 {
            let msg = if let Some(rest) = s.strip_prefix("--") {
                argopt::unrecognized_option(ctx, &format!("--{rest}"))
            } else {
                let c = s.chars().nth(1).unwrap();
                argopt::invalid_option(ctx, &c.to_string())
            };
            let _ = usage_error(ctx, io, "ldd", &msg);
            return Ok(1);
        }
        positionals.push(s.into_owned());
    }

    if positionals.is_empty() {
        writeln!(io.stderr, "ldd: missing file operand")?;
        return Ok(1);
    }

    if verbose {
        writeln!(io.stdout, "  Version information:")?;
    }

    let mut status = 0u32;
    for path in &positionals {
        let resolved = fsutil::resolve(ctx, path);
        let data = match std::fs::read(&resolved) {
            Ok(d) => d,
            Err(_) => {
                writeln!(io.stderr, "ldd: cannot open '{path}'")?;
                status = 1;
                continue;
            }
        };
        let imports = match imported_dlls(&data) {
            Ok(list) => list,
            Err(err) => {
                writeln!(io.stderr, "ldd: {path}: {err}")?;
                status = 1;
                continue;
            }
        };
        for dll in imports {
            if unused_only {
                writeln!(io.stdout, "\t{dll}")?;
            } else if names_only {
                writeln!(io.stdout, "{dll}")?;
            } else {
                let target = resolve_dll(&dll).unwrap_or_else(|| "not found".to_owned());
                writeln!(io.stdout, "{dll} => {target}")?;
            }
        }
    }
    Ok(status)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn errors_on_missing_and_non_pe_files() {
        let sc = ScratchCtx::new("ldd-test");
        std::fs::write(sc.path("plain.txt"), b"not a PE").unwrap();
        let ctx = sc.ctx();

        let mut io = MemIo::new("");
        let rc = ldd_cmd(&[], &mut io.split(), &ctx).unwrap();
        assert_eq!(rc, 1);
        assert!(io.err().contains("missing file operand"));

        let mut io_miss = MemIo::new("");
        let rc_miss = ldd_cmd(&[OsString::from("nope.exe")], &mut io_miss.split(), &ctx).unwrap();
        assert_eq!(rc_miss, 1);
        assert!(io_miss.err().contains("cannot open 'nope.exe'"));

        let mut io_pe = MemIo::new("");
        let rc_pe = ldd_cmd(&[OsString::from("plain.txt")], &mut io_pe.split(), &ctx).unwrap();
        assert_eq!(rc_pe, 1);
        assert!(io_pe.err().contains("not a PE executable"));
    }
}
