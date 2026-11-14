// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for echo command.
/// @Version: 0.1.0
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

// [GNU] -n: do not append a newline
// [GNU] -e: enable backslash escapes
// [GNU] -E: suppress backslash escapes
// [EXT] -u, --upper: WinuxCmd extension, not in GNU coreutils
auto constexpr ECHO_OPTIONS =
    std::array{OPTION("-n", "", "do not append a newline"),
               OPTION("-e", "", "enable backslash escapes"),
               OPTION("-E", "", "suppress backslash escapes"),
               OPTION("-u", "--upper", "convert text to uppercase")};

REGISTER_COMMAND(
    echo,
    /* name */
    "echo",

    /* synopsis */
    "echo [-neE] [arg ...]",

    /* description */
    "Write arguments to the standard output.\n"
    "\n"
    "Display the ARGs, separated by a single space character and followed by "
    "a\n"
    "newline, on the standard output.\n"
    "\n"
    "Options:\n"
    "  -n        do not append a newline\n"
    "  -e        enable interpretation of backslash escapes\n"
    "  -E        explicitly suppress interpretation of backslash escapes\n"
    "\n"
    "`echo` interprets the following backslash-escaped characters:\n"
    "  \\a        alert (bell)\n"
    "  \\b        backspace\n"
    "  \\c        suppress further output\n"
    "  \\e        escape character\n"
    "  \\E        escape character\n"
    "  \\f        form feed\n"
    "  \\n        new line\n"
    "  \\r        carriage return\n"
    "  \\t        horizontal tab\n"
    "  \\v        vertical tab\n"
    "  \\\\        backslash\n"
    "  \\0nnn     the character whose ASCII code is NNN (octal).  NNN can be\n"
    "            0 to 3 octal digits\n"
    "  \\xHH      the eight-bit character whose value is HH (hexadecimal).  "
    "HH\n"
    "            can be one or two hex digits\n"
    "  \\uHHHH    the Unicode character whose value is the hexadecimal value "
    "HHHH.\n"
    "            HHHH can be one to four hex digits. (Not fully implemented on "
    "Windows)\n"
    "  \\UHHHHHHHH the Unicode character whose value is the hexadecimal value\n"
    "            HHHHHHHH. HHHHHHHH can be one to eight hex digits. (Not fully "
    "implemented on Windows)\n",

    /* examples */
    "  echo hello world\n"
    "  echo -n no newline\n"
    "  echo -e line1\\nline2\n"
    "  echo --upper hello\n"
    "  echo --repeat 3 hello",

    /* see also */
    "printf(1)",

    /* author */
    "WinuxCmd",

    /* copyright */
    "Copyright © 2026 WinuxCmd",

    /* options */
    ECHO_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/echo.rs, echo_cmd — coreutils src/echo.c's
  // hand-rolled leading-[-neE]-only flag scan, the POSIXLY_CORRECT
  // allow_options rule (argv[1] == "-n" exact match), the sole
  // --help/--version rule and the measured escape table); the C++ side
  // keeps only this registration. echo.c never calls getopt, so the Rust
  // body receives ctx.raw_args verbatim — the dispatcher's echo rewrite
  // hook (which guarded data-looking tokens behind a "--" for the generic
  // parser) is retired with this wiring, and echo's help/version
  // interception narrowed to the same sole-argument rule the library
  // implements (dispatcher.cppm/command_context.cppm, same commit).
  //
  // The -u/--upper spelling was a WinuxCmd extension carried by the 1.x
  // body; GNU coreutils echo treats it as data ("-u hello" prints
  // "-u hello"), and the rust2 port is GNU-exact, so it retired with the
  // C++ implementation (the registration above keeps spelling it for
  // help/man surface continuity; as a flag it only ever parses for
  // occurrence bookkeeping and can never reject an argv).
  //
  // The -r/--repeat spelling retired the same way but is no longer
  // registered at all (#1621): declared INT_TYPE, the dispatcher consumed
  // and int-validated its value ahead of the handler, so `echo -r x`,
  // bare `echo -r`, and `echo --repeat x` were all rejected with rc 1
  // while GNU prints them as data, rc 0. Unregistered, -r/--repeat now
  // reach the handler verbatim like every other non-[-neE] token.
  //
  // echo never reads stdin: the pass-nothing run_command contract keeps fd
  // 0 live for pipeline consumers.
  return winux::rust2::run_command("echo", ctx.raw_args);
}
