//! `dir` and `vdir` — GNU coreutils ls-wrapper semantics, ported for WinuxCmd 2.0 (#1488).
//!
//! `dir` prepends `-C -b` (columns + C-style escapes) and forwards to `ls::ls_cmd`.
//! `vdir` prepends `-l` (long listing) and forwards to `ls::ls_cmd`.

use std::ffi::OsString;

use crate::ls;
use winux_core::{CmdResult, Ctx, Io};

pub fn dir_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut forwarded = Vec::with_capacity(args.len() + 2);
    forwarded.push(OsString::from("-C"));
    forwarded.push(OsString::from("-b"));
    forwarded.extend_from_slice(args);
    ls::ls_cmd(&forwarded, io, ctx)
}

pub fn vdir_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut forwarded = Vec::with_capacity(args.len() + 1);
    forwarded.push(OsString::from("-l"));
    forwarded.extend_from_slice(args);
    ls::ls_cmd(&forwarded, io, ctx)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn dir_and_vdir_list_files_with_expected_defaults() {
        let sc = ScratchCtx::new("dir-vdir-test");
        std::fs::create_dir_all(sc.path("test data")).unwrap();
        std::fs::write(sc.path("plain.txt"), b"hello").unwrap();
        let ctx = sc.ctx();

        let mut io_dir = MemIo::new("");
        let rc_dir = dir_cmd(
            &[OsString::from("-d"), OsString::from("test data")],
            &mut io_dir.split(),
            &ctx,
        )
        .unwrap();
        assert_eq!(rc_dir, 0);
        assert!(io_dir.err().is_empty());
        assert!(io_dir.out().contains("test\\ data"));

        let mut io_vdir = MemIo::new("");
        let rc_vdir = vdir_cmd(&[], &mut io_vdir.split(), &ctx).unwrap();
        assert_eq!(rc_vdir, 0);
        assert!(io_vdir.err().is_empty());
        assert!(io_vdir.out().contains("total "));
        assert!(io_vdir.out().contains("plain.txt"));
    }
}
