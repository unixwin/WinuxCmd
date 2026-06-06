/*
 *  Copyright (c) 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 *
 *  - File: main.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
// src/main.cpp
// Main entry point for WinuxCmd
import std;
import core;
import utils;
import version;

namespace {
static std::string toLowerAscii(std::string s) {
  std::ranges::transform(s, s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

}  // namespace

/**
 * @brief Print help information
 * @return Exit code (1 - error)
 */
static int printHelp() noexcept {
  safePrintLn(L"WinuxCmd - Windows Compatible Linux Command Set");
  safePrintLn(L"Usage: winuxcmd <command> [options]...");
  safePrintLn(L"");
  safePrintLn(L"Available commands:");

  // Get all registered commands and display them with brief descriptions
  auto commands = CommandRegistry::getAllCommands();
  for (const auto &[cmd_name, cmd_desc] : commands) {
    // Display command name with its brief description
    std::wstring cmd_str = utf8_to_wstring(std::string(cmd_name));
    std::wstring desc_str = utf8_to_wstring(std::string(cmd_desc));
    // Pad command name for alignment
    if (cmd_str.length() < 10) {
      cmd_str.append(10 - cmd_str.length(), L' ');
    }
    safePrintLn(L"  " + cmd_str + L"   " + desc_str);
  }

  safePrintLn(L"");
  safePrintLn(L"Use 'winuxcmd <command> --help' for command-specific help.");
  return 1;
}

/**
 * @brief Main function for WinuxCmd
 * @param argc Number of command-line arguments
 * @param argv Array of command-line arguments
 * @return Exit code from the executed command (0 = success, non-zero = error)
 */
int main(int argc, char *argv[]) noexcept {
  if (argc < 1) {
    return printHelp();
  }
  // Automatically set console or pipe output.
  setupConsoleForUnicode();
  // Get the executable name (stem only)
  std::string self_name = path::get_executable_name(argv[0]);

  // Convert command-line arguments to string_views for efficiency
  std::vector<std::string_view> args;
  args.reserve(argc - 1);
  for (int i = 1; i < argc; ++i) {
    args.emplace_back(argv[i]);
  }

  if (self_name == "winuxcmd") {
    // Mode 1: winuxcmd <command> [args...] (e.g., winuxcmd ls -la)
    if (args.empty()) {
      return printHelp();
    }

    // Check for top-level help flags/alias
    if (args.size() == 1 && (args[0] == "--help" || args[0] == "-h")) {
      return printHelp();
    }

    // Check for version flags
    if (args.size() == 1 && (args[0] == "--version" || args[0] == "-v")) {
      safePrintLn(L"WinuxCmd " + utf8_to_wstring(WinuxCmd::VERSION_STRING));
      return 0;
    }

    if (!args.empty() && args[0] == "help") {
      if (args.size() == 1) return printHelp();
      if (args.size() == 2) {
        std::string topic(args[1]);
        if (CommandRegistry::hasCommand(topic)) {
          CommandRegistry::printHelp(topic);
          return 0;
        }
        std::string lowered = toLowerAscii(topic);
        if (CommandRegistry::hasCommand(lowered)) {
          CommandRegistry::printHelp(lowered);
          return 0;
        }
        safeErrorPrintLn("winuxcmd: no help topic for '" + topic + "'");
        return 1;
      }
      safeErrorPrintLn("winuxcmd: help accepts at most one command name");
      return 1;
    }

    // Extract command name and remaining arguments

    const std::string_view cmd_name = args[0];

    const std::span<std::string_view> cmd_args(args.data() + 1,

                                               args.size() - 1);

    // Check for --version in command arguments
    bool has_version = false;
    for (const auto &arg : cmd_args) {
      if (arg == "--version") {
        has_version = true;
        break;
      }
    }

    if (has_version && CommandRegistry::hasCommand(cmd_name)) {
      safePrintLn(L"WinuxCmd " + utf8_to_wstring(WinuxCmd::VERSION_STRING));
      return 0;
    }

    if (!CommandRegistry::hasCommand(cmd_name)) {
      safeErrorPrintLn("winuxcmd: command not found: " +
                       std::string(cmd_name));
      return 127;
    }

    // Direct execution
    // Dispatch the command to corresponding implementation
    return CommandRegistry::dispatch(cmd_name, cmd_args);
  } else {
    // Mode 2: <command>.exe [args...] (e.g., ls.exe -la)
    // Treat executable name as command name for direct calls
    const std::span<std::string_view> cmd_args(args.data(), args.size());

    // Dispatch the command to corresponding implementation
    return CommandRegistry::dispatch(self_name, cmd_args);
  }
}
