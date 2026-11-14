/*
 *  Copyright © 2026 WinuxCmd
 */
#include "core/command_macros.h"
#include "pch/pch.h"
import std;
import core;
import utils;
import rust2_bridge;

auto constexpr TSORT_OPTIONS =
    std::array{OPTION("", "", "topological sort", STRING_TYPE),
               // [GNU] -w: accepted and ignored (POSIX.1-2024); hidden
               OPTION("-w", "", "", BOOL_TYPE)};

REGISTER_COMMAND(tsort,
                 /* cmd_name */ "tsort",
                 /* cmd_synopsis */ "tsort [FILE]",
                 /* cmd_desc */ "Perform topological sort.",
                 /* examples */ "tsort deps.txt",
                 /* see_also */ "sort",
                 /* author */ "WinuxCmd",
                 /* copyright */ "Copyright © 2026 WinuxCmd",
                 /* options */ TSORT_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/tsort.rs, tsort_cmd); the C++ side keeps
  // only this registration — the token pair reader, the FIFO zero queue,
  // the loop-break DFS and the file/stdin slurp are deleted. stdin
  // streams through the bridge's fd 0 pull callback
  // (run_command_with_stdin_streaming). The 1.x
  // acceptance of -w that GNU 9.4 rejects is not inherited: the rust2
  // side follows GNU.
  return winux::rust2::run_command_with_stdin_streaming("tsort", ctx.raw_args);
}
