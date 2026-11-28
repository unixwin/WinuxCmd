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

auto constexpr CHCON_OPTIONS = std::array{
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("", "--dereference", "affect the referent of each symbolic link"),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("-h", "--no-dereference",
           "affect symbolic links instead of referenced files"),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("", "--preserve-root", "fail to operate recursively on '/'"),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("", "--no-preserve-root", "do not treat '/' specially"),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("", "--reference", "use RFILE's security context", STRING_TYPE),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("-u", "--user", "set user component of the security context",
           STRING_TYPE),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("-r", "--role", "set role component of the security context",
           STRING_TYPE),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("-t", "--type", "set type component of the security context",
           STRING_TYPE),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("-l", "--range", "set range component of the security context",
           STRING_TYPE),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("-R", "--recursive", "operate on files and directories recursively"),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("-H", "", "follow command-line symbolic links to directories"),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("-L", "", "follow every symbolic link to a directory"),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("-P", "", "do not follow symbolic links"),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("-v", "--verbose", "output a diagnostic for every file processed"),
};


REGISTER_COMMAND(
    chcon, "chcon",
    "chcon [OPTION]... CONTEXT FILE...\n"
    "  or:  chcon [OPTION]... [-u USER] [-r ROLE] [-l RANGE] [-t TYPE] "
    "FILE...\n"
    "  or:  chcon [OPTION]... --reference=RFILE FILE...",
    "Change the SELinux security context of each FILE.\n"
    "\n"
    "WinuxCmd accepts the GNU-compatible command line surface for chcon, but\n"
    "Windows does not provide SELinux file contexts. This command therefore\n"
    "acts as a compatibility command and reports that the operation is "
    "not\n"
    "supported on Windows.",
    "  chcon system_u:object_r:httpd_sys_content_t:s0 file.txt\n"
    "  chcon -R --type=httpd_sys_content_t public_html\n"
    "  chcon --reference=reference.txt target.txt",
    "chmod(1), chown(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    CHCON_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/chcon.rs, chcon_cmd).
  return winux::rust2::run_command("chcon", ctx.raw_args);
}
