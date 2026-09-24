// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
    // [GNU]
    std::array{OPTION("", "", "command to run", STRING_TYPE)};

namespace {
auto nohup_usage_error_exit_code() -> int {
  const char* value = std::getenv("POSIXLY_CORRECT");
  return value != nullptr && value[0] != '\0' ? 127 : 125;
}

auto nohup_command_status_from_create_error(DWORD error) -> int {
  switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      return 127;
    default:
      return 126;
  }
}

auto nohup_windows_error_text(DWORD error) -> std::string {
  return win32_posix_error_text(error);
}

auto nohup_is_terminal(FILE* stream) -> bool {
  int fd = _fileno(stream);
  return fd >= 0 && _isatty(fd) != 0;
}

auto open_inheritable_file(const wchar_t* path, DWORD access, DWORD creation)
    -> HANDLE {
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  return CreateFileW(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                     creation, FILE_ATTRIBUTE_NORMAL, nullptr);
}

auto build_nohup_command_line(std::span<const std::string_view> args)
    -> std::wstring {
  return build_windows_command_line(args);
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
    safeErrorPrintLn("Try 'nohup --help' for more information.");
    return nohup_usage_error_exit_code();
  }

  // Prepare process startup info
  STARTUPINFOW si = {sizeof(si)};
  PROCESS_INFORMATION pi;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

  std::vector<HANDLE> owned_handles;

  if (nohup_is_terminal(stdin)) {
    HANDLE nul = open_inheritable_file(L"NUL", GENERIC_READ, OPEN_EXISTING);
    if (nul == INVALID_HANDLE_VALUE) {
      safeErrorPrintLn("nohup: failed to redirect input");
      return 125;
    }
    owned_handles.push_back(nul);
    si.hStdInput = nul;
  }

  bool stdout_redirected = false;
  if (nohup_is_terminal(stdout)) {
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

  if (nohup_is_terminal(stderr)) {
    si.hStdError = si.hStdOutput;
    if (stdout_redirected) {
      safeErrorPrintLn("nohup: redirecting stderr to stdout");
    }
  }

  auto cmd_line = build_nohup_command_line(ctx.positionals);

  DWORD creation_flags = CREATE_NEW_PROCESS_GROUP;

  if (!CreateProcessW(nullptr, cmd_line.data(), nullptr, nullptr, TRUE,
                      creation_flags, nullptr, nullptr, &si, &pi)) {
    DWORD error = GetLastError();
    for (HANDLE handle : owned_handles) {
      CloseHandle(handle);
    }
    safeErrorPrintLn("nohup: failed to run command '" +
                     std::string(ctx.positionals[0]) +
                     "': " + nohup_windows_error_text(error));
    return nohup_command_status_from_create_error(error);
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
