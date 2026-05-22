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

#include <io.h>

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
    std::array{OPTION("", "", "command to run", STRING_TYPE)};

namespace {
auto command_status_from_create_error(DWORD error) -> int {
  switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      return 127;
    default:
      return 126;
  }
}

auto is_terminal(FILE* stream) -> bool {
  int fd = _fileno(stream);
  return fd >= 0 && _isatty(fd) != 0;
}

auto open_inheritable_file(const wchar_t* path, DWORD access, DWORD creation)
    -> HANDLE {
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  return CreateFileW(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                     creation, FILE_ATTRIBUTE_NORMAL, nullptr);
}
}  // namespace

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
    return 125;
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
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

  std::vector<HANDLE> owned_handles;

  if (is_terminal(stdin)) {
    HANDLE nul = open_inheritable_file(L"NUL", GENERIC_READ, OPEN_EXISTING);
    if (nul == INVALID_HANDLE_VALUE) {
      safeErrorPrintLn("nohup: failed to redirect input");
      return 125;
    }
    owned_handles.push_back(nul);
    si.hStdInput = nul;
  }

  bool stdout_redirected = false;
  if (is_terminal(stdout)) {
    HANDLE out =
        open_inheritable_file(L"nohup.out", FILE_APPEND_DATA, OPEN_ALWAYS);
    if (out == INVALID_HANDLE_VALUE) {
      safeErrorPrintLn("nohup: failed to open 'nohup.out'");
      return 125;
    }
    owned_handles.push_back(out);
    si.hStdOutput = out;
    stdout_redirected = true;
    safeErrorPrintLn(
        "nohup: ignoring input and appending output to "
        "'nohup.out'");
  }

  if (is_terminal(stderr)) {
    si.hStdError = si.hStdOutput;
    if (stdout_redirected) {
      safeErrorPrintLn("nohup: redirecting stderr to stdout");
    }
  }

  std::wstring wcmd = utf8_to_wstring(cmd);

  DWORD creation_flags = CREATE_NEW_PROCESS_GROUP;

  if (!CreateProcessW(nullptr, const_cast<wchar_t*>(wcmd.c_str()), nullptr,
                      nullptr, TRUE, creation_flags, nullptr, nullptr, &si,
                      &pi)) {
    DWORD error = GetLastError();
    for (HANDLE handle : owned_handles) {
      CloseHandle(handle);
    }
    safeErrorPrintLn("nohup: failed to execute command");
    return command_status_from_create_error(error);
  }

  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD exit_code = 0;
  GetExitCodeProcess(pi.hProcess, &exit_code);

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  for (HANDLE handle : owned_handles) {
    CloseHandle(handle);
  }

  return static_cast<int>(exit_code);
}
