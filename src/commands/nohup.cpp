/*
 *  Copyright © 2026 WinuxCmd
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 *
 *  - File: nohup.cpp
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for nohup command.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// ======================================================
// Options (constexpr)
// ======================================================

auto constexpr NOHUP_OPTIONS =
    std::array{OPTION("-a", "--append", "append to output file")};

// ======================================================
// Main command implementation
// ======================================================

REGISTER_COMMAND(
    nohup,
    /* cmd_name */ "nohup",
    /* cmd_synopsis */ "nohup COMMAND [ARG]...",
    /* cmd_desc */
    "Run a command immune to hangups.\n"
    "Run COMMAND, ignoring hangup signals. If standard output is a terminal,\n"
    "append output to 'nohup.out'. If standard error is a terminal, redirect\n"
    "it to standard output. To save output to FILE, use 'nohup COMMAND > "
    "FILE'.",
    /* examples */
    "  nohup make &\n"
    "  nohup ./myscript.sh > myscript.log 2>&1 &\n"
    "  nohup python long_running_task.py",
    /* see_also */ "nice, disown",
    /* author */ "WinuxCmd",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */ NOHUP_OPTIONS) {
  if (ctx.positionals.empty()) {
    safeErrorPrintLn("nohup: missing operand");
    safePrintLn("Try 'nohup --help' for more information.");
    return 1;
  }

  // Build command string
  std::string cmd;
  for (size_t i = 0; i < ctx.positionals.size(); ++i) {
    if (i > 0) cmd += " ";
    cmd += ctx.positionals[i];
  }

  // Prepare process startup info
  STARTUPINFOW si = {sizeof(si)};
  PROCESS_INFORMATION pi;

  std::wstring wcmd = utf8_to_wstring(cmd);

  // Set DETACHED_PROCESS to ignore console signals
  DWORD creation_flags = DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;

  if (!CreateProcessW(nullptr, const_cast<wchar_t*>(wcmd.c_str()), nullptr,
                      nullptr, FALSE, creation_flags, nullptr, nullptr, &si,
                      &pi)) {
    safeErrorPrintLn("nohup: failed to execute command");
    return 1;
  }

  // Close handles as we don't need them
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  safePrintLn("nohup: ignoring input and appending output to 'nohup.out'");
  safePrintLn("Process started with PID: " + std::to_string(pi.dwProcessId));

  return 0;
}
