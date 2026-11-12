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
pub mod childproc;
pub mod chmod;
pub mod chown;
pub mod csplit;
pub mod cut;

pub mod df;
pub mod digest;
pub mod digestsum;
pub mod dirname;
pub mod expand;
pub mod factor;

pub mod fmt;
pub mod fold;
pub mod fs_win;
pub mod fsutil;
pub mod getconf;
pub mod glob;
pub mod gnuhelp;
pub mod groups;
pub mod head;

pub mod hostid;
pub mod hostname;
pub mod id;
pub mod join;
pub mod ln;
pub mod mkdir;
pub mod mktemp;
pub mod modemap;
pub mod nice;
pub mod nohup;

pub mod nproc;
pub mod numfmt;
pub mod od;

pub mod paste;
pub mod pathchk;
pub mod printenv;
pub mod ptx;
pub mod pwd;
pub mod readlink;
pub mod realpath;
pub mod rmdir;
pub mod seq;
pub mod shuf;
pub mod sleep;
pub mod sort;
pub mod stat;
pub mod sum;
pub mod sync;
pub mod tac;
pub mod tail;
pub mod tee;
pub mod test_cmd;
pub mod timeout;
pub mod touch;
pub mod tr;

pub mod truefalse;
pub mod truncate;
pub mod tsort;
pub mod uname;
pub mod unexpand;
pub mod unlink;
pub mod users;
pub mod who;
pub mod whoami;
pub mod winsys;

use std::ffi::OsString;

use winux_core::{CmdResult, Ctx, Io};

type CmdFn = fn(&[OsString], &mut Io, &Ctx) -> CmdResult;

/// Registry: command name -> library function.
pub fn registry() -> Vec<(&'static str, CmdFn)> {
    vec![
        ("sync", sync::sync_cmd),
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
        // wave2 completion (#1488 help-surface wave): these four modules
        // shipped unregistered on main — their gate tables ran 22 FAIL
        // (driver rc 127 vs cpp rc 0) because dispatch could not find them.
        ("getconf", getconf::getconf_cmd),
        ("hostname", hostname::hostname_cmd),
        ("id", id::id_cmd),
        ("uname", uname::uname_cmd),
        ("unlink", unlink::unlink_cmd),
        ("sleep", sleep::sleep_cmd),
        ("sort", sort::sort_cmd),
        ("tac", tac::tac_cmd),
        ("truncate", truncate::truncate_cmd),
        ("b2sum", digestsum::b2sum_cmd),
        ("sha224sum", digestsum::sha224sum_cmd),
        ("sha384sum", digestsum::sha384sum_cmd),
        // wave6 (#1488): completes the *sum digest family (md5sum/sha1sum
        // via the new digest.rs primitives; sha256sum/sha512sum reuse the
        // wave2 SHA-2 cores).
        ("md5sum", digestsum::md5sum_cmd),
        ("sha1sum", digestsum::sha1sum_cmd),
        ("sha256sum", digestsum::sha256sum_cmd),
        ("sha512sum", digestsum::sha512sum_cmd),
        // wave3a (#1488): alphabetical insertions
        ("fmt", fmt::fmt_cmd),
        ("fold", fold::fold_cmd),
        ("paste", paste::paste_cmd),
        ("seq", seq::seq_cmd),
        ("tee", tee::tee_cmd),
        ("stat", stat::stat_cmd),
        ("touch", touch::touch_cmd),
        ("ln", ln::ln_cmd),
        ("join", join::join_cmd),
        ("tsort", tsort::tsort_cmd),
        ("hostid", hostid::hostid_cmd),
        ("users", users::users_cmd),
        ("timeout", timeout::timeout_cmd),
        ("expand", expand::expand_cmd),
        ("unexpand", unexpand::unexpand_cmd),
        ("numfmt", numfmt::numfmt_cmd),
        // wave4a (#1488): alphabetical insertions
        ("head", head::head_cmd),
        ("tail", tail::tail_cmd),
        ("cut", cut::cut_cmd),
        ("tr", tr::tr_cmd),
        ("od", od::od_cmd),
        ("sum", sum::sum_cmd),
        ("mktemp", mktemp::mktemp_cmd),
        ("nice", nice::nice_cmd),
        // #1587: nohup lands with its spawn family — it shares the
        // childproc resolver with timeout (same non-PE child surface).
        // wave4b (#1488)
        ("factor", factor::factor_cmd),
        ("pathchk", pathchk::pathchk_cmd),
        ("csplit", csplit::csplit_cmd),
        ("shuf", shuf::shuf_cmd),
        ("ptx", ptx::ptx_cmd),
        ("nohup", nohup::nohup_cmd),
        ("groups", groups::groups_cmd),
        ("who", who::who_cmd),
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
