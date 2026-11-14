/*
 *  Copyright © 2026 WinuxCmd
 */
#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;
import rust2_bridge;

auto constexpr PINKY_OPTIONS = std::array{
    // [GNU]
    OPTION("-l", "", "produce long format output", BOOL_TYPE),
    // [GNU]
    OPTION("-b", "", "omit the user home directory and shell in long format",
           BOOL_TYPE),
    // [GNU]
    OPTION("-f", "", "omit the line of column headings in short format",
           BOOL_TYPE),
    // [GNU]
    OPTION("-w", "", "omit the user full name in short format", BOOL_TYPE),
    // [GNU]
    OPTION("-i", "", "omit the user full name and remote host in short format",
           BOOL_TYPE),
    // [GNU]
    OPTION("-q", "", "omit the user full name, remote host, and idle time",
           BOOL_TYPE),
    // [GNU]
    // [DIFFERS] - project files do not exist on Windows
    OPTION("-h", "", "omit the user project file in long format", BOOL_TYPE),
    // [GNU]
    // [DIFFERS] - plan files do not exist on Windows
    OPTION("-p", "", "omit the user plan file in long format", BOOL_TYPE),
    // [GNU]
    // [DIFFERS] - short format is already the default
    OPTION("-s", "", "do short format output", BOOL_TYPE)};
// [GNU] coreutils 9.4 pinky declares NO long options besides --help and
// --version (src/pinky.c longopts); a prior revision accepted an invented
// --lookup, which GNU rejects as an unrecognized option.

REGISTER_COMMAND(pinky, "pinky", "pinky [OPTION]... [USER]...",
                 "A lightweight finger-compatible user information tool.",
                 "pinky\npinky -f\npinky -l USER", "finger, who, users",
                 "WinuxCmd", "Copyright © 2026 WinuxCmd", PINKY_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/pinky.rs, pinky_cmd — WTS session table as
  // the utmp substitute, pinky.c heading/entry format strings, last-wins
  // short-option grammar, `-l` without USER usage failure). The C++ side
  // keeps only this registration; pinky never reads fd 0.
  return winux::rust2::run_command("pinky", ctx.raw_args);
}
