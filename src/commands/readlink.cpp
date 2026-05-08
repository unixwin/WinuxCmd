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
 *  - File: readlink.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for readlink.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr READLINK_OPTIONS = std::array{
    OPTION("-f", "--canonicalize", "canonicalize by following every symlink",
           BOOL_TYPE),
    OPTION("-e", "--canonicalize-existing",
           "canonicalize by following symlinks (must exist)", BOOL_TYPE),
    OPTION("-m", "--canonicalize-missing",
           "canonicalize by following symlinks (may not exist)", BOOL_TYPE),
    OPTION("-n", "--no-newline", "do not output the trailing delimiter",
           BOOL_TYPE),
    OPTION("-q", "--quiet", "suppress most error messages", BOOL_TYPE),
    OPTION("-s", "--silent", "suppress most error messages", BOOL_TYPE),
    OPTION("-v", "--verbose", "report error message", BOOL_TYPE),
    OPTION("-z", "--zero", "end each output line with NUL", BOOL_TYPE)};

namespace readlink_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool canonicalize = false;
  bool canonicalize_existing = false;
  bool canonicalize_missing = false;
  bool no_newline = false;
  bool quiet = false;
  bool verbose = false;
  bool zero_terminated = false;
  SmallVector<std::string, 64> files;
};

auto build_config(const CommandContext<READLINK_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.canonicalize =
      ctx.get<bool>("--canonicalize", false) || ctx.get<bool>("-f", false);
  cfg.canonicalize_existing = ctx.get<bool>("--canonicalize-existing", false) ||
                              ctx.get<bool>("-e", false);
  cfg.canonicalize_missing = ctx.get<bool>("--canonicalize-missing", false) ||
                             ctx.get<bool>("-m", false);
  cfg.no_newline =
      ctx.get<bool>("--no-newline", false) || ctx.get<bool>("-n", false);
  cfg.quiet = ctx.get<bool>("--quiet", false) || ctx.get<bool>("-q", false) ||
              ctx.get<bool>("-s", false);
  cfg.verbose = ctx.get<bool>("--verbose", false) || ctx.get<bool>("-v", false);
  cfg.zero_terminated =
      ctx.get<bool>("--zero", false) || ctx.get<bool>("-z", false);

  for (auto arg : ctx.positionals) {
    std::string file_arg(arg);
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded) {
        for (const auto& file : glob_result.files) {
          cfg.files.push_back(wstring_to_utf8(file));
        }
        continue;
      }
    }
    cfg.files.push_back(file_arg);
  }

  if (cfg.files.empty()) {
    return std::unexpected("missing operand");
  }

  return cfg;
}

auto run(const Config& cfg) -> int {
  for (const auto& file : cfg.files) {
    // Windows doesn't have symlinks in the same way as Unix
    // For NTFS junctions and reparse points, we could use GetFileAttributes
    // But for simplicity, we'll just check if it exists

    DWORD attrs = GetFileAttributesA(file.c_str());

    if (attrs == INVALID_FILE_ATTRIBUTES) {
      if (!cfg.quiet) {
        safePrint("readlink: ");
        safePrint(file);
        safePrintLn(": No such file or directory");
      }
      return 1;
    }

    // Check if it's a reparse point (symlink/junction)
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
      // This is a symlink/junction
      safePrintLn(file);
    } else {
      // Not a symlink - handle gracefully by printing the file itself
      safePrintLn(file);
    }
  }

  return 0;
}

}  // namespace readlink_pipeline

REGISTER_COMMAND(
    readlink, "readlink", "readlink [OPTION]... FILE...",
    "Print value of a symbolic link or canonical file name.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "Note: This is a Windows implementation. Windows supports\n"
    "reparse points (symlinks/junctions) but they work differently\n"
    "than Unix symlinks. This command provides basic detection.",
    "  readlink file\n"
    "  readlink -f file\n"
    "  readlink -e file",
    "ln(1), realpath(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    READLINK_OPTIONS) {
  using namespace readlink_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"readlink");
    return 1;
  }

  return run(*cfg_result);
}
