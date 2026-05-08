/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the “Software”), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: pipeline.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#ifndef WINUXCMD_BIN_DIR
#define WINUXCMD_BIN_DIR L"."
#endif

#include "pipeline.h"

#include <windows.h>

#include <stdexcept>
#include <thread>
#include <vector>

#include "framework/framework_pch.h"
#include "process_win32.h"

/**
 * @brief Default binary directory when not specified at compile time
 */

// ---------------- Pipeline API Implementation ----------------

/**
 * @brief Add a command to the pipeline sequence
 *
 * Appends a new command with its executable and arguments to
 * the pipeline. Commands are executed in the order they are added.
 *
 * @param exe Path to the executable
 * @param args Vector of command arguments
 */
void Pipeline::add(const std::wstring &exe,
                   const std::vector<std::wstring> &args) {
  cmds_.push_back({exe, args});
}

/**
 * @brief Set stdin input data for the pipeline
 *
 * Moves the provided string data to be used as stdin input
 * for the first command in the pipeline.
 *
 * @param data String containing stdin data
 */
void Pipeline::set_stdin(std::string data) { stdin_data_ = std::move(data); }

/**
 * @brief Set working directory for pipeline execution
 *
 * Configures the working directory where all commands
 * in the pipeline will be executed.
 *
 * @param dir Working directory path
 */
void Pipeline::set_cwd(const std::wstring &dir) { cwd_ = dir; }

/**
 * @brief Set environment variable for pipeline commands
 *
 * Adds or updates an environment variable that will be
 * available to all commands in the pipeline.
 *
 * @param k Environment variable name
 * @param v Environment variable value
 */
void Pipeline::set_env(const std::wstring &k, const std::wstring &v) {
  env_[k] = v;
}

/**
 * @brief Set executable search directory
 *
 * Specifies the directory where relative executable paths
 * should be resolved from during pipeline execution.
 *
 * @param dir Executable directory path
 */
void Pipeline::set_exe_dir(const std::wstring &dir) { exe_dir_ = dir; }

// ---------------- Windows command line quoting ----------------

static std::wstring quote_arg(const std::wstring &arg) {
  if (arg.empty()) return L"\"\"";

  bool need_quote = arg.find_first_of(L" \t\"") != std::wstring::npos;

  if (!need_quote) return arg;

  std::wstring out = L"\"";
  size_t backslashes = 0;

  for (wchar_t c : arg) {
    if (c == L'\\') {
      backslashes++;
    } else if (c == L'"') {
      out.append(backslashes * 2 + 1, L'\\');
      out.push_back(L'"');
      backslashes = 0;
    } else {
      out.append(backslashes, L'\\');
      backslashes = 0;
      out.push_back(c);
    }
  }

  out.append(backslashes * 2, L'\\');
  out.push_back(L'"');
  return out;
}

static std::wstring build_cmd(const std::wstring &exe,
                              const std::vector<std::wstring> &args) {
  std::wstring cmd = quote_arg(exe);
  for (auto &a : args) {
    cmd += L" ";
    cmd += quote_arg(a);
  }
  return cmd;
}

// ---------------- Pipeline Execution Implementation ----------------

/**
 * @brief Execute the complete pipeline and return results
 *
 * Orchestrates the execution of all commands in the pipeline by:
 * 1. Creating necessary pipes for inter-process communication
 * 2. Setting up stdin/stdout/stderr redirection
 * 3. Spawning child processes with proper handles
 * 4. Managing data flow between pipeline stages
 * 5. Collecting output and exit codes
 *
 * @return CommandResult Results including exit code and captured output
 * @throws std::runtime_error on setup or execution failures
 */
CommandResult Pipeline::run() {
  // Validate pipeline has commands
  if (cmds_.empty()) throw std::runtime_error("Pipeline: no commands");

  // Set default executable directory if not specified
  if (!exe_dir_) {
    exe_dir_ = WINUXCMD_BIN_DIR;
  }

  // Security attributes for inheritable handles
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};

  // ----- Setup stdin pipe (if data provided) -----
  HANDLE stdin_r = nullptr;  // Read end for child processes
  HANDLE stdin_w = nullptr;  // Write end for parent process

  if (stdin_data_) {
    // Create pipe for stdin data
    if (!CreatePipe(&stdin_r, &stdin_w, &sa, 0))
      throw std::runtime_error("CreatePipe(stdin) failed");

    // Make read end inheritable by child processes
    SetHandleInformation(stdin_r, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    // Parent writes, children read - make write end non-inheritable
    SetHandleInformation(stdin_w, HANDLE_FLAG_INHERIT, 0);
  }

  const size_t n = cmds_.size();

  std::vector<HANDLE> read_pipes(n > 1 ? n - 1 : 0);
  std::vector<HANDLE> write_pipes(n > 1 ? n - 1 : 0);
  std::vector<PROCESS_INFORMATION> procs(n);

  for (auto &p : procs) ZeroMemory(&p, sizeof(p));

  // ----- middle pipes -----
  for (size_t i = 0; i + 1 < n; ++i) {
    if (!CreatePipe(&read_pipes[i], &write_pipes[i], &sa, 0))
      throw std::runtime_error("CreatePipe(mid) failed");

    // SetHandleInformation(read_pipes[i], HANDLE_FLAG_INHERIT, 0);
  }

  // ----- final stdout / stderr -----
  HANDLE final_out_r, final_out_w;
  HANDLE final_err_r, final_err_w;

  if (!CreatePipe(&final_out_r, &final_out_w, &sa, 0))
    throw std::runtime_error("CreatePipe(stdout) failed");

  if (!CreatePipe(&final_err_r, &final_err_w, &sa, 0))
    throw std::runtime_error("CreatePipe(stderr) failed");

  SetHandleInformation(final_out_r, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(final_err_r, HANDLE_FLAG_INHERIT, 0);

  // ----- spawn processes -----
  for (size_t i = 0; i < n; ++i) {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;

    // stdin
    if (i == 0) {
      si.hStdInput = stdin_r ? stdin_r : GetStdHandle(STD_INPUT_HANDLE);
    } else {
      si.hStdInput = read_pipes[i - 1];
    }

    // stdout
    if (i + 1 < n) {
      si.hStdOutput = write_pipes[i];
    } else {
      si.hStdOutput = final_out_w;
    }

    // stderr: all go to same pipe
    si.hStdError = final_err_w;
    std::wstring exe = cmds_[i].exe;

    if (exe_dir_ && exe.find(L'\\') == std::wstring::npos &&
        exe.find(L'/') == std::wstring::npos) {
      exe = *exe_dir_ + L"\\" + exe;
    }
    auto cmdline = build_cmd(exe, cmds_[i].args);
    const wchar_t *cwd = cwd_ ? cwd_->c_str() : nullptr;
    auto build_env_block = [this]() -> std::vector<wchar_t> {
      struct ci_less {
        bool operator()(const std::wstring &a, const std::wstring &b) const {
          return _wcsicmp(a.c_str(), b.c_str()) < 0;
        }
      };

      // Start from current environment to ensure required variables stay
      // present.
      std::map<std::wstring, std::wstring, ci_less> vars;
      if (LPWCH raw = GetEnvironmentStringsW()) {
        const wchar_t *p = raw;
        while (*p != L'\0') {
          std::wstring entry = p;
          p += entry.size() + 1;
          if (entry.empty() || entry[0] == L'=') continue;
          auto pos = entry.find(L'=');
          if (pos == std::wstring::npos) continue;
          vars[entry.substr(0, pos)] = entry.substr(pos + 1);
        }
        FreeEnvironmentStringsW(raw);
      }

      // Apply overrides from test pipeline.
      for (auto &[k, v] : this->env_) {
        vars[k] = v;
      }

      std::vector<wchar_t> block;
      for (auto &[k, v] : vars) {
        std::wstring entry = k + L"=" + v;
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back(L'\0');
      }
      // Double-NUL terminate the block.
      block.push_back(L'\0');
      return block;
    };
    std::vector<wchar_t> env_block;
    LPVOID env_ptr = nullptr;
    DWORD creation_flags = 0;

    if (!env_.empty()) {
      env_block = build_env_block();
      env_ptr = env_block.data();
      creation_flags |= CREATE_UNICODE_ENVIRONMENT;
    }

    if (!CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, TRUE,
                        creation_flags, env_ptr, cwd, &si, &procs[i])) {
      DWORD err = GetLastError();
      throw std::runtime_error("CreateProcessW failed: " + std::to_string(err));
    }

    if (i > 0) CloseHandle(read_pipes[i - 1]);

    if (i + 1 < n) CloseHandle(write_pipes[i]);
  }

  // parent no longer needs these
  CloseHandle(final_out_w);
  CloseHandle(final_err_w);

  // ----- write stdin -----
  if (stdin_w) {
    DWORD written = 0;
    WriteFile(stdin_w, stdin_data_->data(), (DWORD)stdin_data_->size(),
              &written, nullptr);
    CloseHandle(stdin_w);  // EOF
  }

  if (stdin_r) CloseHandle(stdin_r);

  // ----- read stdout / stderr concurrently -----
  auto read_all = [](HANDLE h) {
    std::string out;
    char buf[4096];
    DWORD r;
    while (ReadFile(h, buf, sizeof(buf), &r, nullptr) && r > 0)
      out.append(buf, buf + r);
    return out;
  };

  std::string out, err;
  std::thread t_out([&] { out = read_all(final_out_r); });
  std::thread t_err([&] { err = read_all(final_err_r); });

  // ----- Wait for all processes to complete -----
  for (auto &p : procs) WaitForSingleObject(p.hProcess, INFINITE);

  // Get exit code from the last process in pipeline
  DWORD exit_code = 0;
  GetExitCodeProcess(procs.back().hProcess, &exit_code);

  // Wait for output reading threads to complete
  t_out.join();
  t_err.join();

  // ----- Cleanup process handles -----
  for (auto &p : procs) {
    CloseHandle(p.hProcess);
    CloseHandle(p.hThread);
  }

  // Close final output pipe handles
  CloseHandle(final_out_r);
  CloseHandle(final_err_r);

  // Return results with exit code and captured output
  return {(int)exit_code, out, err};
}
