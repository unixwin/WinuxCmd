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
 *  - File: cygpath.cpp
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for cygpath command.
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

auto constexpr CYGPATH_OPTIONS = std::array{
    OPTION("-u", "--unix", "print Unix form of NAME"),
    OPTION("-w", "--windows", "print Windows form of NAME"),
    OPTION("-m", "--mixed", "print Windows form, with regular slashes"),
    OPTION("-p", "--path", "NAME is a PATH list")};

// ======================================================
// Helper functions
// ======================================================

namespace {
// Convert Windows path to Unix/Cygwin path
std::string windows_to_unix(const std::string& path) {
  std::string result = path;

  // Replace backslashes with forward slashes
  for (size_t i = 0; i < result.length(); ++i) {
    if (result[i] == '\\') {
      result[i] = '/';
    }
  }

  // Convert drive letters (e.g., C: -> /c)
  if (result.length() >= 2 && result[1] == ':') {
    char drive = std::tolower(result[0]);
    result = "/" + std::string(1, drive) + result.substr(2);
  }

  return result;
}

// Convert Unix/Cygwin path to Windows path
std::string unix_to_windows(const std::string& path) {
  std::string result = path;

  // Convert /x or /X drive notation to X:
  if (result.length() >= 2 && result[0] == '/' && std::isalpha(result[1])) {
    char drive = std::toupper(result[1]);
    result = std::string(1, drive) + ":" + result.substr(2);
  }

  // Replace forward slashes with backslashes
  for (size_t i = 0; i < result.length(); ++i) {
    if (result[i] == '/') {
      result[i] = '\\';
    }
  }

  return result;
}

// Convert Unix path to Windows path with forward slashes (mixed)
std::string unix_to_mixed(const std::string& path) {
  std::string result = path;

  // Convert /x or /X drive notation to X:
  if (result.length() >= 2 && result[0] == '/' && std::isalpha(result[1])) {
    char drive = std::toupper(result[1]);
    result = std::string(1, drive) + ":" + result.substr(2);
  }

  // Keep forward slashes (mixed mode)
  return result;
}
}  // namespace

// ======================================================
// Pipeline components
// ======================================================
namespace cygpath_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool to_unix = false;
  bool to_windows = false;
  bool to_mixed = false;
  bool is_path = false;
  std::vector<std::string> paths;
};

auto build_config(const CommandContext<CYGPATH_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  cfg.to_unix = ctx.get<bool>("-u", false) || ctx.get<bool>("--unix", false);
  cfg.to_windows =
      ctx.get<bool>("-w", false) || ctx.get<bool>("--windows", false);
  cfg.to_mixed = ctx.get<bool>("-m", false) || ctx.get<bool>("--mixed", false);
  cfg.is_path = ctx.get<bool>("-p", false) || ctx.get<bool>("--path", false);

  if (ctx.positionals.empty()) {
    return std::unexpected("missing operand");
  }

  for (const auto& path : ctx.positionals) {
    cfg.paths.push_back(std::string(path));
  }

  // Default conversion: Unix to Windows
  if (!cfg.to_unix && !cfg.to_windows && !cfg.to_mixed) {
    cfg.to_windows = true;
  }

  return cfg;
}

auto run(const Config& cfg) -> int {
  for (const auto& path : cfg.paths) {
    std::string result;

    if (cfg.to_unix) {
      result = windows_to_unix(path);
    } else if (cfg.to_mixed) {
      result = unix_to_mixed(path);
    } else {
      result = unix_to_windows(path);
    }

    safePrintLn(result);
  }

  return 0;
}

}  // namespace cygpath_pipeline

// ======================================================
// Main command implementation
// ======================================================

REGISTER_COMMAND(cygpath,
                 /* name */
                 "cygpath",

                 /* synopsis */
                 "cygpath [OPTION] NAME...",

                 /* description */
                 "Convert path names between Windows and Unix formats.\n"
                 "Convert path names between Windows and Unix/Cygwin formats.\n"
                 "Default is to convert Unix paths to Windows paths.",

                 /* examples */
                 "  cygpath -u C:\\Users\\John\\Documents\n"
                 "  cygpath -w /home/user/file.txt\n"
                 "  cygpath -m /c/Users/John/Documents",

                 /* see_also */
                 "realpath(1), pwd(1)",

                 /* author */
                 "WinuxCmd",

                 /* copyright */
                 "Copyright © 2026 WinuxCmd",

                 /* options */
                 CYGPATH_OPTIONS) {
  using namespace cygpath_pipeline;
  using namespace core::pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    safeErrorPrint("cygpath: ");
    safeErrorPrintLn(cfg_result.error());
    return 1;
  }

  return run(*cfg_result);
}
