//! `hmac256` — HMAC-SHA256 checksum utility, ported for WinuxCmd 2.0 (#1488).
//!
//! Computes RFC 2104 HMAC-SHA256 over a file or stdin using `digest::Sha256Core`
//! with explicit `Io`/`Ctx` and zero third-party dependencies.

use std::ffi::OsString;
use std::fs::File;
use std::io::{self, Read, Write};

use crate::digest::{sha256, sha256_core};
use crate::fsutil;
use crate::gnuhelp;
use winux_core::{CmdResult, Ctx, Io};

const HMAC256_HELP: &str = "\
Usage: hmac256 [KEY] [FILE]
Compute HMAC-SHA256 checksum.

With no FILE, or when FILE is -, read standard input.

  -h, --help     display this help and exit
  -V, --version  output version information and exit
";

fn hmac_sha256_stream(key: &[u8], reader: &mut dyn Read) -> io::Result<[u8; 32]> {
    const BLOCK_SIZE: usize = 64;
    let mut k_block = [0u8; BLOCK_SIZE];
    if key.len() > BLOCK_SIZE {
        let hashed = sha256(key);
        k_block[..32].copy_from_slice(&hashed);
    } else {
        k_block[..key.len()].copy_from_slice(key);
    }

    let mut ipad = [0x36u8; BLOCK_SIZE];
    let mut opad = [0x5cu8; BLOCK_SIZE];
    for i in 0..BLOCK_SIZE {
        ipad[i] ^= k_block[i];
        opad[i] ^= k_block[i];
    }

    let mut inner = sha256_core();
    inner.update(&ipad);
    let mut buf = [0u8; 64 * 1024];
    loop {
        let n = reader.read(&mut buf)?;
        if n == 0 {
            break;
        }
        inner.update(&buf[..n]);
    }
    let inner_hash = inner.finish(32);

    let mut outer = sha256_core();
    outer.update(&opad);
    outer.update(&inner_hash);
    let outer_hash = outer.finish(32);
    let mut out = [0u8; 32];
    out.copy_from_slice(&outer_hash);
    Ok(out)
}

fn to_hex(bytes: &[u8]) -> String {
    const HEX: &[u8; 16] = b"0123456789abcdef";
    let mut s = String::with_capacity(bytes.len() * 2);
    for &b in bytes {
        s.push(HEX[(b >> 4) as usize] as char);
        s.push(HEX[(b & 0x0f) as usize] as char);
    }
    s
}

pub fn hmac256_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut positionals: Vec<String> = Vec::new();
    let mut no_more_opts = false;

    for a in args {
        let s = a.to_string_lossy();
        if !no_more_opts {
            if s == "--" {
                no_more_opts = true;
                continue;
            }
            if s == "-h" || s == "--help" {
                io.stdout.write_all(HMAC256_HELP.as_bytes())?;
                return Ok(0);
            }
            if s == "-V" || s == "--version" {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("hmac256", "WinuxCmd")
                )?;
                return Ok(0);
            }
        }
        positionals.push(s.into_owned());
    }

    if positionals.is_empty() {
        writeln!(io.stderr, "hmac256: missing key")?;
        return Ok(1);
    }

    let key = positionals[0].as_bytes();
    let filename = positionals.get(1).map(String::as_str).unwrap_or("-");

    let digest = if filename == "-" {
        match hmac_sha256_stream(key, io.stdin) {
            Ok(d) => d,
            Err(e) => {
                writeln!(io.stderr, "hmac256: {}", fsutil::reason(&e))?;
                return Ok(1);
            }
        }
    } else {
        let resolved = fsutil::resolve(ctx, filename);
        let mut file = match File::open(&resolved) {
            Ok(f) => f,
            Err(e) => {
                if fsutil::failed_open_is_directory(ctx, filename) {
                    writeln!(
                        io.stderr,
                        "hmac256: cannot open '{filename}' for reading: Is a directory"
                    )?;
                } else {
                    writeln!(
                        io.stderr,
                        "hmac256: cannot open '{filename}' for reading: {}",
                        fsutil::reason(&e)
                    )?;
                }
                return Ok(1);
            }
        };
        match hmac_sha256_stream(key, &mut file) {
            Ok(d) => d,
            Err(e) => {
                writeln!(io.stderr, "hmac256: {}", fsutil::reason(&e))?;
                return Ok(1);
            }
        }
    };

    let hex = to_hex(&digest);
    if filename == "-" {
        writeln!(io.stdout, "{hex}")?;
    } else {
        writeln!(io.stdout, "{hex}  {filename}")?;
    }
    Ok(0)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn hmac256_stdin_and_file_match_rfc_vector() {
        let sc = ScratchCtx::new("hmac256-test");
        let ctx = sc.ctx();
        let mut io = MemIo::new("hello");
        let args = vec![OsString::from("secret")];
        let rc = hmac256_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(rc, 0);
        assert!(io.err().is_empty());
        assert_eq!(
            io.out(),
            "88aab3ede8d3adf94d26ab90d3bafd4a2083070c3bcce9c014ee04a443847c0b\n"
        );

        let data_path = sc.path("data.txt");
        std::fs::write(&data_path, b"hello").unwrap();
        let mut io2 = MemIo::new("");
        let args2 = vec![OsString::from("secret"), OsString::from("data.txt")];
        let rc2 = hmac256_cmd(&args2, &mut io2.split(), &ctx).unwrap();
        assert_eq!(rc2, 0);
        assert_eq!(
            io2.out(),
            "88aab3ede8d3adf94d26ab90d3bafd4a2083070c3bcce9c014ee04a443847c0b  data.txt\n"
        );
    }

    #[test]
    fn hmac256_long_key_and_missing_file_errors() {
        let sc = ScratchCtx::new("hmac256-err");
        let ctx = sc.ctx();
        let long_key = "k".repeat(100);
        let mut io = MemIo::new("payload");
        let rc = hmac256_cmd(&[OsString::from(long_key)], &mut io.split(), &ctx).unwrap();
        assert_eq!(rc, 0);
        assert_eq!(io.out().trim().len(), 64);

        let mut io_missing = MemIo::new("");
        let rc_missing = hmac256_cmd(
            &[OsString::from("secret"), OsString::from("missing.bin")],
            &mut io_missing.split(),
            &ctx,
        )
        .unwrap();
        assert_eq!(rc_missing, 1);
        assert!(io_missing.err().contains("cannot open 'missing.bin'"));
    }
}
