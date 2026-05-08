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
 *  - File: dirname.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for dirname.
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

auto constexpr DIRNAME_OPTIONS = std::array{OPTION(
    "-z", "--zero", "separate output with NUL rather than newline", BOOL_TYPE)};

namespace dirname_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool zero = false;
  SmallVector<std::string, 64> names;
};

auto build_config(const CommandContext<DIRNAME_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.zero = ctx.get<bool>("--zero", false) || ctx.get<bool>("-z", false);

  for (auto arg : ctx.positionals) {
    cfg.names.push_back(std::string(arg));
  }

  if (cfg.names.empty()) {
    return std::unexpected("missing operand");
  }

  return cfg;
}

auto get_dirname(std::string_view path) -> std::string {
  if (path.empty()) {
    return ".";
  }

  std::string result = std::string(path);

  // Remove trailing slashes
  while (result.size() > 1 && (result.back() == '/' || result.back() == '\\')) {
    result.pop_back();
  }

  // If path is just "/" or "\\", return it
  if (result.size() == 1 && (result[0] == '/' || result[0] == '\\')) {
    return result;
  }

  // Find last separator
  size_t last_sep = result.find_last_of("/\\");
  if (last_sep == std::string::npos) {
    return ".";
  }

  // Get directory part
  result = result.substr(0, last_sep);

  // If result is empty (path was like "file"), return "."
  if (result.empty()) {
    return ".";
  }

  // Remove trailing slashes from result
  while (result.size() > 1 && (result.back() == '/' || result.back() == '\\')) {
    result.pop_back();
  }

  return result;
}

auto run(const Config& cfg) -> int {
  for (const auto& name : cfg.names) {
    std::string result = get_dirname(name);
    if (cfg.zero) {
      safePrint(result);
      safePrint("\0");
    } else {
      safePrintLn(result);
    }
  }

  return 0;
}

}  // namespace dirname_pipeline

REGISTER_COMMAND(
    dirname, "dirname", "dirname [OPTION] NAME...",
    "Output each NAME with its last non-slash component and trailing slashes\n"
    "removed; if NAME contains no /'s, output '.' (meaning the current "
    "directory).",
    "  dirname /path/to/file.txt\n"
    "  dirname dir1/file1 dir2/file2\n"
    "  dirname /foo/bar/",
    "basename(1), realpath(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    DIRNAME_OPTIONS) {
  using namespace dirname_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"dirname");
    return 1;
  }

  return run(*cfg_result);
}
