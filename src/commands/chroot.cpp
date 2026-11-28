/*
 *  Copyright © 2026 WinuxCmd
 */
#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr CHROOT_OPTIONS = std::array{
    // [DIFFERS]
    OPTION("-g", "--groups", "specify supplementary groups", STRING_TYPE),
    // [DIFFERS]
    OPTION("-u", "--userspec", "specify user and group", STRING_TYPE),
    // [DIFFERS]
    OPTION("", "--skip-chdir", "do not change working directory to '/'"),
};

REGISTER_COMMAND(
    chroot, "chroot", "chroot [OPTION] NEWROOT [COMMAND [ARG]...]",
    "Run COMMAND with root directory set to NEWROOT.\n"
    "\n"
    "WinuxCmd accepts the GNU-compatible command line surface for chroot, but\n"
    "Windows does not provide a safe equivalent to POSIX chroot semantics.\n"
    "This command therefore acts as a compatibility placeholder and reports\n"
    "that changing the root directory is not supported on Windows.",
    "  chroot C:\\jail cmd.exe /c echo hi\n"
    "  chroot --userspec=user:group C:\\jail cmd.exe /c echo hi\n"
    "  chroot --skip-chdir C:\\jail cmd.exe /c echo hi",
    "runcon(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", CHROOT_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/chroot.rs, chroot_cmd).
  return winux::rust2::run_command("chroot", ctx.raw_args);
}
