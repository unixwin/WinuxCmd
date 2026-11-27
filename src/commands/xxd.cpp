/*
 *  Copyright © 2026 WinuxCmd
 */
#include "core/command_macros.h"
#include "pch/pch.h"
import std;
import core;
import utils;
import rust2_bridge;

auto constexpr XXD_OPTIONS = std::array{
    // [EXT] option
    OPTION("-r", "--reverse", "reverse: convert hex to binary"),
    // [EXT] option
    OPTION("-p", "--plain", "plain hexdump"),
    // [EXT] option
    OPTION("-c", "--cols", "set bytes per line", INT_TYPE),
    // [EXT] option
    OPTION("-l", "--len", "limit input to LEN bytes", INT_TYPE),
    // [EXT] option
    OPTION("-s", "--seek", "start at SEEK bytes into input", INT_TYPE),
    // [EXT] option
    OPTION("-o", "--offset", "add OFFSET to displayed file position", INT_TYPE),
    // [EXT] option
    OPTION("-u", "--upper-case", "use upper-case hex digits"),
    OPTION("-g", "--group", "set bytes per hex group", INT_TYPE),
    // [EXT] option
    OPTION("-i", "--include", "output in C include file style"),
    // [EXT] option
    OPTION("-n", "--name", "set the variable name used in C include output",
           STRING_TYPE)};

REGISTER_COMMAND(xxd,
                 /* cmd_name */ "xxd",
                 /* cmd_synopsis */ "xxd [OPTION]... [FILE]...",
                 /* cmd_desc */ "Make a hexdump or do the reverse.",
                 /* examples */ "xxd file.txt\nxxd -r hex.txt > file.txt",
                 /* see_also */ "od",
                 /* author */ "WinuxCmd",
                 /* copyright */ "Copyright © 2026 WinuxCmd",
                 /* options */ XXD_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/xxd.rs, xxd_cmd).
  const bool reads_stdin =
      ctx.positionals.empty() ||
      std::ranges::any_of(ctx.positionals,
                          [](std::string_view a) { return a == "-"; });
  if (reads_stdin) {
    return winux::rust2::run_command_with_stdin("xxd", ctx.raw_args);
  }
  return winux::rust2::run_command("xxd", ctx.raw_args);
}
