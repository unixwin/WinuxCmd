//! winux-cmds: wave1 library-form commands for WinuxCmd 2.0 (#1488).
//!
//! Every command is `fn run(args: &[OsString], io: &mut Io, ctx: &Ctx)
//! -> CmdResult` — explicit IO handles, explicit env/cwd, no process
//! globals, status code returned instead of exit(). `dispatch` is the
//! registry both the CLI driver and the cdylib ABI use.

pub mod argopt;
pub mod b64core;
pub mod basename;
pub mod basenc;
pub mod bre;
pub mod canon;
pub mod chmod;
pub mod chown;
pub mod df;
pub mod digest;
pub mod digestsum;
pub mod dirname;
pub mod fmt;
pub mod fold;
pub mod fs_win;
pub mod fsutil;
pub mod getconf;
pub mod hostname;
pub mod id;
pub mod ln;
pub mod mkdir;
pub mod modemap;
pub mod nproc;
pub mod paste;
pub mod printenv;
pub mod pwd;
pub mod readlink;
pub mod realpath;
pub mod rmdir;
pub mod seq;
pub mod sleep;
pub mod stat;
pub mod tac;
pub mod tee;
pub mod test_cmd;
pub mod touch;
pub mod truefalse;
pub mod truncate;
pub mod uname;
pub mod unlink;
pub mod whoami;
pub mod winsys;

use std::ffi::OsString;

use winux_core::{CmdResult, Ctx, Io};

type CmdFn = fn(&[OsString], &mut Io, &Ctx) -> CmdResult;

/// Registry: command name -> library function.
pub fn registry() -> Vec<(&'static str, CmdFn)> {
    vec![
        ("true", truefalse::true_cmd),
        ("false", truefalse::false_cmd),
        ("test", test_cmd::test_cmd),
        ("[", test_cmd::bracket_cmd),
        ("basename", basename::basename_cmd),
        ("dirname", dirname::dirname_cmd),
        ("printenv", printenv::printenv_cmd),
        ("pwd", pwd::pwd_cmd),
        ("mkdir", mkdir::mkdir_cmd),
        ("rmdir", rmdir::rmdir_cmd),
        ("readlink", readlink::readlink_cmd),
        ("realpath", realpath::realpath_cmd),
        ("chmod", chmod::chmod_cmd),
        ("chown", chown::chown_cmd),
        ("df", df::df_cmd),
        ("base64", basenc::base64_cmd),
        ("base32", basenc::base32_cmd),
        ("whoami", whoami::whoami_cmd),
        ("nproc", nproc::nproc_cmd),
        ("unlink", unlink::unlink_cmd),
        ("sleep", sleep::sleep_cmd),
        ("tac", tac::tac_cmd),
        ("truncate", truncate::truncate_cmd),
        ("b2sum", digestsum::b2sum_cmd),
        ("sha224sum", digestsum::sha224sum_cmd),
        ("sha384sum", digestsum::sha384sum_cmd),
        // wave3a (#1488): alphabetical insertions
        ("fmt", fmt::fmt_cmd),
        ("fold", fold::fold_cmd),
        ("paste", paste::paste_cmd),
        ("seq", seq::seq_cmd),
        ("tee", tee::tee_cmd),
        ("stat", stat::stat_cmd),
        ("touch", touch::touch_cmd),
        ("ln", ln::ln_cmd),
    ]
}

/// Look up and run a command. Returns `None` for unknown names (the caller
/// decides the "command not found" behavior — the library has no opinion).
pub fn dispatch(name: &str, args: &[OsString], io: &mut Io, ctx: &Ctx) -> Option<CmdResult> {
    registry()
        .into_iter()
        .find(|(n, _)| *n == name)
        .map(|(_, f)| f(args, io, ctx))
}
