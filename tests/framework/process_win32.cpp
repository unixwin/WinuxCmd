// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "process_win32.h"

#include <windows.h>

#include "framework/framework_pch.h"

static std::wstring build_cmd(const std::wstring &exe,
                              const std::vector<std::wstring> &args) {
  std::wstring cmd = L"\"" + exe + L"\"";
  for (auto &a : args) cmd += L" \"" + a + L"\"";
  return cmd;
}

static std::string read_all(HANDLE h) {
  std::string out;
  char buf[4096];
  DWORD read;
  while (ReadFile(h, buf, sizeof(buf), &read, nullptr) && read > 0)
    out.append(buf, buf + read);
  return out;
}

/**
 * @brief Execute a single command and capture output (Windows implementation)
 *
 * Creates pipes for stdin/stdout/stderr, spawns the process,
 * feeds stdin data, captures output, and returns results.
 *
 * @param exe Executable path
 * @param args Command arguments
 * @param stdin_data Optional stdin input data
 * @return CommandResult Execution results including exit code and output
 */
CommandResult run_command(const std::wstring &exe,
                          const std::vector<std::wstring> &args,
                          const std::string &stdin_data) {
  // Security attributes for inheritable handles
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};

  // Create pipes for stdin, stdout, and stderr
  HANDLE in_r, in_w, out_r, out_w, err_r, err_w;
  CreatePipe(&in_r, &in_w, &sa, 0);    // stdin pipe
  CreatePipe(&out_r, &out_w, &sa, 0);  // stdout pipe
  CreatePipe(&err_r, &err_w, &sa, 0);  // stderr pipe

  // Make parent-side handles non-inheritable
  SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);   // Parent writes
  SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);  // Parent reads
  SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);  // Parent reads

  // Configure process startup information
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;  // Use custom std handles
  si.hStdInput = in_r;                // Child reads from this
  si.hStdOutput = out_w;              // Child writes to this
  si.hStdError = err_w;               // Child writes errors to this

  PROCESS_INFORMATION pi{};
  auto cmd = build_cmd(exe, args);

  // Create the child process with redirected handles
  CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, 0, nullptr,
                 nullptr, &si, &pi);

  // Parent closes child-side handles
  CloseHandle(in_r);   // Child owns this now
  CloseHandle(out_w);  // Child owns this now
  CloseHandle(err_w);  // Child owns this now

  // Send stdin data to child process
  if (!stdin_data.empty()) {
    DWORD written;
    WriteFile(in_w, stdin_data.data(),
              // NOLINTNEXTLINE
              (DWORD)stdin_data.size(), &written, nullptr);
  }
  CloseHandle(in_w);  // Signal EOF by closing write end

  // Read all output from child process
  auto out = read_all(out_r);
  auto err = read_all(err_r);

  // Wait for process to complete
  WaitForSingleObject(pi.hProcess, INFINITE);

  // Get process exit code
  DWORD code;
  GetExitCodeProcess(pi.hProcess, &code);

  // Cleanup remaining handles
  CloseHandle(out_r);
  CloseHandle(err_r);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  // Return results
  return {(int)code, out, err};  // NOLINT
}
