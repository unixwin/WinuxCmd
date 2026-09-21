// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>

/**
 * @brief Structure representing the result of command execution
 *
 * Contains exit code and captured stdout/stderr output from
 * a command or pipeline execution.
 */
struct CommandResult;

/**
 * @brief Represents a single command in a pipeline
 *
 * Stores the executable path and its arguments for pipeline execution.
 */
struct PipelineCommand {
  std::wstring exe;                ///< Executable path
  std::vector<std::wstring> args;  ///< Command arguments
};

/**
 * @brief Pipeline execution manager for Windows processes
 *
 * Manages execution of command pipelines with support for:
 * - Multiple chained commands (pipes)
 * - Custom stdin input
 * - Working directory control
 * - Environment variable setting
 * - Custom executable directory
 *
 * Handles Windows-specific process creation and inter-process
 * communication through named pipes.
 */
class Pipeline {
 public:
  /**
   * @brief Add a command to the pipeline
   *
   * Appends a new command to the pipeline sequence. Commands are
   * executed in the order they are added.
   *
   * @param exe Path to the executable
   * @param args Vector of command arguments
   */
  void add(const std::wstring &exe, const std::vector<std::wstring> &args);

  /**
   * @brief Set stdin data for the pipeline
   *
   * Provides input data that will be fed to the first command's stdin.
   *
   * @param data String containing the stdin data
   */
  void set_stdin(std::string data);

  /**
   * @brief Set working directory for all commands
   *
   * Sets the working directory where all commands in the pipeline
   * will be executed.
   *
   * @param dir Working directory path
   */
  void set_cwd(const std::wstring &dir);

  /**
   * @brief Set environment variable for all commands
   *
   * Adds or updates an environment variable that will be available
   * to all commands in the pipeline.
   *
   * @param k Environment variable name
   * @param v Environment variable value
   */
  void set_env(const std::wstring &k, const std::wstring &v);

  /**
   * @brief Set directory to search for executables
   *
   * Specifies the directory where relative executable paths
   * should be resolved from.
   *
   * @param dir Executable search directory
   */
  void set_exe_dir(const std::wstring &dir);

  /**
   * @brief Execute the pipeline and return results
   *
   * Creates all necessary pipes, spawns processes, manages
   * inter-process communication, and collects results.
   *
   * @return CommandResult Structure containing exit code and output
   * @throws std::runtime_error if pipeline setup or execution fails
   */
  CommandResult run();

 private:
  std::vector<PipelineCommand> cmds_;         ///< Commands in pipeline order
  std::optional<std::string> stdin_data_;     ///< Optional stdin input data
  std::optional<std::wstring> cwd_;           ///< Optional working directory
  std::optional<std::wstring> exe_dir_;       ///< Optional executable directory
  std::map<std::wstring, std::wstring> env_;  ///< Environment variables
};
