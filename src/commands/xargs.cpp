/*
 *  Copyright © 2026 [caomengxuan666]
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
 *  - File: xargs.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
/// @Description: Implementation for xargs.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

#pragma comment(lib, "advapi32.lib")
import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

/**
 * @brief XARGS command options definition
 *
 * This array defines all the options supported by the xargs command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 * - @a -n, --max-args: Use at most max-args arguments per command line
 * [IMPLEMENTED]
 * - @a -I: Replace occurrences of replace-str in the initial-arguments with
 * names [IMPLEMENTED]
 * - @a -0, --null: Input items are terminated by a null character [IMPLEMENTED]
 * - @a -t, --verbose: Print the command line on the standard error before
 * executing it [IMPLEMENTED]
 * - @a -r, --no-run-if-empty: If the standard input does not contain any
 * nonblanks, do not run the command [IMPLEMENTED]
 * - @a -P, --max-procs: Run up to max-procs processes at a time [NOT SUPPORT]
 */
auto constexpr XARGS_OPTIONS = std::array{
    OPTION("-n", "--max-args",
           "use at most max-args arguments per command line", INT_TYPE),
    OPTION("-I", "",
           "replace occurrences of replace-str in the initial-arguments with "
           "names",
           STRING_TYPE),
    OPTION("-0", "--null", "input items are terminated by a null character"),
    OPTION("-t", "--verbose",
           "print the command line on the standard error before executing it"),
    OPTION("-r", "--no-run-if-empty",
           "if the standard input does not contain any nonblanks, do not run "
           "the command"),
    OPTION("-P", "--max-procs", "run up to max-procs processes at a time",
           INT_TYPE)};

namespace xargs_pipeline {
namespace cp = core::pipeline;

/**
 * @brief Parse arguments from stdin
 * @param delimiter Delimiter character (default: space/newline)
 * @param replace_str Replacement string for -I option
 * @return Vector of parsed arguments
 */
auto parse_arguments(char delimiter, const std::string &replace_str)
    -> std::vector<std::string> {
  SmallVector<std::string, 256> args;
  std::string arg;

  char c;
  while (std::cin.get(c)) {
    if (c == delimiter || c == '\n' || c == '\r') {
      if (!arg.empty()) {
        args.push_back(arg);
        arg.clear();
      }
    } else if (c == ' ' || c == '\t') {
      if (!arg.empty()) {
        args.push_back(arg);
        arg.clear();
      }
    } else {
      arg += c;
    }
  }

  // Add last argument if not empty
  if (!arg.empty()) {
    args.push_back(arg);
  }

  return std::vector<std::string>(args.begin(), args.end());
}

/**
 * @brief Execute command with arguments
 * @param command Command to execute
 * @param base_args Base arguments from command line
 * @param input_args Arguments from stdin
 * @param replace_str Replacement string
 * @param max_args Maximum arguments per execution
 * @param verbose Print command before execution
 * @return Exit code
 */
auto execute_command(const std::string &command,
                     const std::vector<std::string> &base_args,
                     const std::vector<std::string> &input_args,
                     const std::string &replace_str, int max_args, bool verbose)
    -> int {
  int exit_code = 0;

  if (max_args <= 0) {
    max_args = input_args.size();
  }

  // Split input_args into batches
  for (size_t i = 0; i < input_args.size(); i += max_args) {
    SmallVector<std::string, 256> all_args;
    size_t end = std::min(i + max_args, input_args.size());

    // Add base arguments
    for (const auto &base_arg : base_args) {
      if (!replace_str.empty() &&
          base_arg.find(replace_str) != std::string::npos) {
        // Replace all occurrences with the entire batch
        std::string replaced = base_arg;
        size_t pos = 0;
        while ((pos = replaced.find(replace_str, pos)) != std::string::npos) {
          std::string replacement;
          for (size_t j = i; j < end; ++j) {
            if (j > i) replacement += " ";
            replacement += input_args[j];
          }
          replaced.replace(pos, replace_str.length(), replacement);
          pos += replacement.length();
        }
        all_args.push_back(replaced);
      } else {
        all_args.push_back(base_arg);
      }
    }

    // Add input arguments for this batch (only if not using -I)
    if (replace_str.empty()) {
      for (size_t j = i; j < end; ++j) {
        all_args.push_back(input_args[j]);
      }
    }

    // Print command if verbose
    if (verbose) {
      safeErrorPrint(command);
      for (const auto &arg : all_args) {
        safeErrorPrint(" ");
        safeErrorPrint(arg);
      }
      safeErrorPrint("\n");
    }

    // Build command line for CreateProcess
    std::wstring cmd_line = utf8_to_wstring(command);
    for (const auto &arg : all_args) {
      cmd_line += L" ";
      std::wstring warg = utf8_to_wstring(arg);
      if (warg.find(L' ') != std::wstring::npos ||
          warg.find(L'\t') != std::wstring::npos) {
        cmd_line += L"\"";
        cmd_line += warg;
        cmd_line += L"\"";
      } else {
        cmd_line += warg;
      }
    }

    // Execute command using CreateProcess
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi;

    BOOL success =
        CreateProcessW(nullptr, cmd_line.data(), nullptr, nullptr, FALSE,
                       CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &si, &pi);

    if (success) {
      WaitForSingleObject(pi.hProcess, INFINITE);
      DWORD result;
      GetExitCodeProcess(pi.hProcess, &result);
      exit_code = result;
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
    } else {
      safeErrorPrint("xargs: failed to execute '");
      safeErrorPrint(command);
      safeErrorPrint("'\n");
      exit_code = 127;
    }
  }

  return exit_code;
}

}  // namespace xargs_pipeline

REGISTER_COMMAND(
    xargs, "xargs", "build and execute command lines from standard input",
    "Build and execute command lines from standard input.\n"
    "\n"
    "Items are separated by blanks. The result command line is executed\n"
    "after each group of max-args items is read.",
    "  find . -name '*.cpp' | xargs rm -f     Delete all cpp files\n"
    "  echo file1 file2 | xargs cat         Concatenate files\n"
    "  find . -name '*.txt' | xargs -n1 grep 'pattern'  Search one file at a "
    "time",
    "find(1), grep(1), sed(1)", "caomengxuan666", "Copyright © 2026 WinuxCmd",
    XARGS_OPTIONS) {
  using namespace xargs_pipeline;

  bool use_null = ctx.get<bool>("-0", false) || ctx.get<bool>("--null", false);
  bool verbose =
      ctx.get<bool>("-t", false) || ctx.get<bool>("--verbose", false);
  bool no_run_if_empty =
      ctx.get<bool>("-r", false) || ctx.get<bool>("--no-run-if-empty", false);
  int max_args = ctx.get<int>("-n", 0);
  std::string replace_str = ctx.get<std::string>("-I", "");

  // Get command to execute (first positional arg)
  if (ctx.positionals.empty()) {
    // Default to echo if no command specified
    auto input_args_vec = parse_arguments(' ', replace_str);
    SmallVector<std::string, 256> input_args(input_args_vec.begin(),
                                             input_args_vec.end());

    if (no_run_if_empty && input_args.empty()) {
      return 0;
    }

    if (verbose) {
      safeErrorPrint("echo");
      for (const auto &arg : input_args) {
        safeErrorPrint(" ");
        safeErrorPrint(arg);
      }
      safeErrorPrint("\n");
    }

    for (const auto &arg : input_args) {
      safePrint(arg);
      safePrint(" ");
    }
    safePrint("\n");
    return 0;
  }

  std::string command = std::string(ctx.positionals[0]);
  SmallVector<std::string, 32> base_args;

  for (size_t i = 1; i < ctx.positionals.size(); ++i) {
    base_args.push_back(std::string(ctx.positionals[i]));
  }

  // Parse arguments from stdin
  char delimiter = use_null ? '\0' : ' ';
  auto input_args_vec = parse_arguments(delimiter, replace_str);
  SmallVector<std::string, 256> input_args(input_args_vec.begin(),
                                           input_args_vec.end());

  // If no input arguments, check if we should skip execution
  if (input_args.empty()) {
    // Skip if -r (no-run-if-empty) is specified
    if (no_run_if_empty) {
      return 0;
    }
    // Skip if -I is specified but there's nothing to replace
    if (!replace_str.empty()) {
      return 0;
    }
    // If no input args and no base args, skip execution
    if (base_args.empty()) {
      return 0;
    }
  }

  // Execute command with arguments - convert SmallVector to std::vector for
  // compatibility
  std::vector<std::string> base_args_vec(base_args.begin(), base_args.end());
  return execute_command(
      command, base_args_vec,
      std::vector<std::string>(input_args.begin(), input_args.end()),
      replace_str, max_args, verbose);
}
