// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#pragma once
#include <string>
#include <vector>

/**
 * @brief Structure containing results from command execution
 *
 * Holds the exit code and captured stdout/stderr output
 * from a single command execution.
 */
struct CommandResult {
  int exit_code = -1;       ///< Process exit code (-1 if not executed)
  std::string stdout_text;  ///< Captured stdout output
  std::string stderr_text;  ///< Captured stderr output
  /// Bytes still pending on the pipeline's stdin after the child exited.
  /// Only collected when Pipeline::probe_stdin_leftover(true) is set;
  /// empty otherwise. Used by GNU-differential tests that pin the
  /// "command X must not consume stdin" contract (#1166).
  std::string stdin_leftover;
};

/**
 * @brief Execute a single command and capture its output
 *
 * Runs a command with the specified executable, arguments,
 * and optional stdin data. Captures both stdout and stderr output.
 *
 * @param exe Path to the executable
 * @param args Vector of command arguments
 * @param stdin_data Optional string to feed to command's stdin
 * @return CommandResult Structure containing execution results
 */
CommandResult run_command(const std::wstring& exe,
                          const std::vector<std::wstring>& args,
                          const std::string& stdin_data = {});
