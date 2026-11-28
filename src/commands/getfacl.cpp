// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Read Windows file ACLs in a stable text format.
/// @Version: 0.2.0
/// @License: MIT

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr GETFACL_OPTIONS = std::array{
    // [GNU] -a, --access: display the access ACL
    OPTION("-a", "--access", "display the access ACL"),
    // [GNU] -d, --default: display the default ACL
    OPTION("-d", "--default", "display the default ACL"),
    // [GNU] -R, --recursive: list the ACLs of all files and directories
    // recursively
    OPTION("-R", "--recursive",
           "list the ACLs of all files and directories recursively"),
    // [GNU] -p, --absolute-names: do not strip leading path separators
    OPTION("-p", "--absolute-names", "do not strip leading path separators"),
    // [GNU] -n, --numeric: print numeric IDs instead of account names
    OPTION("-n", "--numeric", "print numeric IDs instead of account names"),
    // [GNU] -c, --omit-header: do not display the comment header
    OPTION("-c", "--omit-header", "do not display the comment header"),
    // [GNU] -s, --skip-base: skip files that have all ACL entries equal to the
    // base ACL
    OPTION("-s", "--skip-base",
           "skip files that have all ACL entries equal to the base ACL"),
    // [GNU] -t, --tabular: use a tabular output format
    OPTION("-t", "--tabular", "use a tabular output format")};

REGISTER_COMMAND(
    getfacl, "getfacl", "getfacl [OPTION]... FILE...",
    "Print Windows file ACLs in a stable text format.\n"
    "\n"
    "Options:\n"
    "  -a, --access       display the access ACL (default)\n"
    "  -d, --default      display the default ACL\n"
    "  -R, --recursive    list ACLs recursively\n"
    "  -p, --absolute-names  do not strip leading path separators\n"
    "  -n, --numeric      print numeric IDs instead of account names\n"
    "  -c, --omit-header  do not display the comment header\n"
    "  -s, --skip-base    skip files with base ACL\n"
    "  -t, --tabular      use a tabular output format",
    "  getfacl file.txt\n"
    "  getfacl --numeric file.txt",
    "chown(1), chgrp(1), chmod(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    GETFACL_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/getfacl.rs, getfacl_cmd).
  return winux::rust2::run_command("getfacl", ctx.raw_args);
}
