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

  // [GNU] nohup.c:115-199: the three streams are probed independently
  // and each diagnostic depends on which of them are terminals.
  const bool ignoring_input = nohup_is_terminal(stdin);
  const bool redirecting_stdout = nohup_is_terminal(stdout);
  const bool redirecting_stderr = nohup_is_terminal(stderr);

  if (ignoring_input) {
    // [GNU] nohup.c:124-127: /dev/null is deliberately opened for
    // *writing*, so any read by the command evokes an error, not EOF.
    HANDLE nul = open_inheritable_file(L"NUL", GENERIC_WRITE, OPEN_EXISTING);
    if (nul == INVALID_HANDLE_VALUE) {
      safeErrorPrintLn("nohup: failed to render standard input unusable: " +
                       nohup_windows_error_text(GetLastError()));
      return nohup_usage_error_exit_code();
    }
    owned_handles.push_back(nul);
    si.hStdInput = nul;
    // [GNU] nohup.c:129-130: the lone "ignoring input" notice appears
    // only when neither output stream needed redirection.
    if (!redirecting_stdout && !redirecting_stderr) {
      safeErrorPrintLn("nohup: ignoring input");
    }
  }

  bool stdout_redirected = false;
  if (redirecting_stdout) {
    HANDLE out =
        open_inheritable_file(L"nohup.out", FILE_APPEND_DATA, OPEN_ALWAYS);
    std::string out_name = "nohup.out";
    if (out == INVALID_HANDLE_VALUE) {
      // [GNU] nohup.c:149-170: fall back to $HOME/nohup.out and report
      // each failed open with its error text before giving up.
      const DWORD first_error = GetLastError();
      const char* home = std::getenv("HOME");
      bool tried_home = false;
      if (home != nullptr && home[0] != '\0') {
        tried_home = true;
        out_name = std::string(home) + "/nohup.out";
        out = open_inheritable_file(utf8_to_wstring(out_name).c_str(),
                                    FILE_APPEND_DATA, OPEN_ALWAYS);
      }
      if (out == INVALID_HANDLE_VALUE) {
        safeErrorPrintLn("nohup: failed to open 'nohup.out': " +
                         nohup_windows_error_text(first_error));
        if (tried_home) {
          safeErrorPrintLn("nohup: failed to open '" + out_name +
                           "': " +
                           nohup_windows_error_text(GetLastError()));
        }
        return nohup_usage_error_exit_code();
      }
    }
    owned_handles.push_back(out);
    si.hStdOutput = out;
    stdout_redirected = true;
    // [GNU] nohup.c:172-176: the "ignoring input and" prefix is present
    // only when standard input really is being ignored.
    safeErrorPrintLn(ignoring_input
                         ? "nohup: ignoring input and appending output to '" +
                               out_name + "'"
                         : "nohup: appending output to '" + out_name + "'");
  }

  if (redirecting_stderr) {
    si.hStdError = si.hStdOutput;
    // [GNU] nohup.c:186-194: when stdout was redirected the appending
    // notice already covered it; the stderr notice is printed only
    // when stdout was NOT redirected.
    if (!stdout_redirected) {
      safeErrorPrintLn(ignoring_input
                           ? "nohup: ignoring input and redirecting stderr "
                             "to stdout"
                           : "nohup: redirecting stderr to stdout");
    }
  }

  // Resolve the child with GNU execvp semantics (PATH-only; no
  // application-directory, cwd, or System32 fallback) so a name colliding
  // with a Windows image resolves from the caller's PATH.
  winux::childproc::ChildLaunchRequest nohup_launch_request;
  nohup_launch_request.command = std::string(ctx.positionals[0]);
  for (size_t i = 1; i < ctx.positionals.size(); ++i) {
    nohup_launch_request.args.emplace_back(ctx.positionals[i]);
  }
  auto launch = winux::childproc::plan_child_launch(nohup_launch_request);
  if (launch.status != winux::childproc::ChildResolveStatus::Found) {
    const DWORD resolve_error =
        winux::childproc::child_status_to_win32_error(launch.status);
    for (HANDLE handle : owned_handles) {
      CloseHandle(handle);
    }
    safeErrorPrintLn("nohup: failed to run command '" +
                     std::string(ctx.positionals[0]) +
                     "': " + nohup_windows_error_text(resolve_error));
    return nohup_command_status_from_create_error(resolve_error);
  }

  DWORD creation_flags = CREATE_NEW_PROCESS_GROUP;

  if (!CreateProcessW(launch.application_name->c_str(),
                      launch.command_line.data(), nullptr, nullptr, TRUE,
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
