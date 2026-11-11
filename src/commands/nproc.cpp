// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for nproc command.
/// @Version: 0.2.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"
import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr NPROC_OPTIONS =
    // [GNU] --all prints every installed processor; the default
    // prints the processors available to this process (affinity).
    std::array{OPTION("", "--all", "print number of all installed processors"),
               // [GNU]
               OPTION("", "--ignore", "ignore N processors", STRING_TYPE)};

REGISTER_COMMAND(
    nproc_cmd,
    /* name */
    "nproc",

    /* synopsis */
    "nproc [OPTION]...",
    "Print the number of processing units available.\n"
    "\n"
    "This is useful for scripts that need to know how many parallel\n"
    "processes can be started.",
    "  nproc\n"
    "  nproc --all\n"
    "  nproc --ignore 1",

    /* see also */
    "sysconf(3)", "WinuxCmd", "Copyright © 2026 WinuxCmd", NPROC_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/nproc.rs, nproc_cmd); the C++ side keeps
  // only this registration — the affinity/OMP_NUM_THREADS/--ignore
  // pipeline is deleted. ctx.raw_args passes the operands verbatim
  // because GNU nproc.c parses its own getopt run (xnumtoumax for
  // --ignore, parse_omp_threads for the environment).
  return winux::rust2::run_command("nproc", ctx.raw_args);
}
