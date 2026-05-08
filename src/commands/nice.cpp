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
 *  - File: nice.cpp
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for nice command.
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

auto constexpr NICE_OPTIONS =
    std::array{OPTION("-n", "--adjustment", "adjust increment", INT_TYPE)};

// ======================================================
// Main command implementation
// ======================================================

REGISTER_COMMAND(
    nice,
    /* cmd_name */ "nice",
    /* cmd_synopsis */ "nice [OPTION] [COMMAND [ARG]...]",
    /* cmd_desc */
    "Run a program with modified scheduling priority.\n"
    "Run COMMAND with an adjusted niceness, which affects process scheduling.\n"
    "With no COMMAND, print the current niceness. Niceness values range from\n"
    "-20 (most favorable) to 19 (least favorable). Default is 10.",
    /* examples */
    "  nice -n 5 tar -cf archive.tar /home\n"
    "  nice -n -10 make\n"
    "  nice",
    /* see_also */ "renice, nohup",
    /* author */ "WinuxCmd",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */ NICE_OPTIONS) {
  int adjustment = 10;  // Default adjustment

  // Parse adjustment option
  if (ctx.get<bool>("-n", false)) {
    try {
      adjustment = std::stoi(ctx.get<std::string>("-n", ""));
    } catch (...) {
      safeErrorPrintLn("nice: invalid adjustment value");
      return 1;
    }
  } else if (ctx.get<bool>("--adjustment", false)) {
    try {
      adjustment = std::stoi(ctx.get<std::string>("--adjustment", ""));
    } catch (...) {
      safeErrorPrintLn("nice: invalid adjustment value");
      return 1;
    }
  }

  // If no command provided, print current priority
  if (ctx.positionals.empty()) {
    DWORD priority = GetPriorityClass(GetCurrentProcess());
    safePrintLn("Current priority class: " + std::to_string(priority));
    return 0;
  }

  // Build command string
  std::string cmd;
  for (size_t i = 0; i < ctx.positionals.size(); ++i) {
    if (i > 0) cmd += " ";
    cmd += ctx.positionals[i];
  }

  // Execute command with adjusted priority
  STARTUPINFOW si = {sizeof(si)};
  PROCESS_INFORMATION pi;

  std::wstring wcmd = utf8_to_wstring(cmd);

  // Determine priority class based on adjustment
  DWORD priority_class = NORMAL_PRIORITY_CLASS;
  if (adjustment < -5) {
    priority_class = HIGH_PRIORITY_CLASS;
  } else if (adjustment > 10) {
    priority_class = BELOW_NORMAL_PRIORITY_CLASS;
  } else if (adjustment > 15) {
    priority_class = IDLE_PRIORITY_CLASS;
  }

  if (!CreateProcessW(nullptr, const_cast<wchar_t*>(wcmd.c_str()), nullptr,
                      nullptr, FALSE, priority_class, nullptr, nullptr, &si,
                      &pi)) {
    safeErrorPrintLn("nice: failed to execute command");
    return 1;
  }

  // Wait for process to complete
  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD exit_code;
  GetExitCodeProcess(pi.hProcess, &exit_code);

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  return static_cast<int>(exit_code);
}
