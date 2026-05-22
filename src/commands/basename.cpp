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
 *  - File: basename.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for basename.
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

auto constexpr BASENAME_OPTIONS = std::array{
    OPTION("-a", "--multiple",
           "support multiple arguments and treat each as a NAME", BOOL_TYPE),
    OPTION("-s", "--suffix", "remove a trailing SUFFIX; implies -a",
           STRING_TYPE),
    OPTION("-z", "--zero", "end each output line with NUL, not newline",
           BOOL_TYPE)};

namespace basename_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool multiple = false;
  std::string suffix;
  bool zero = false;
  SmallVector<std::string, 64> names;
};

auto build_config(const CommandContext<BASENAME_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.multiple =
      ctx.get<bool>("--multiple", false) || ctx.get<bool>("-a", false);
  cfg.suffix = ctx.get<std::string>("--suffix", "");
  if (cfg.suffix.empty()) {
    cfg.suffix = ctx.get<std::string>("-s", "");
  }
  if (!cfg.suffix.empty()) {
    cfg.multiple = true;
  }
  cfg.zero = ctx.get<bool>("--zero", false) || ctx.get<bool>("-z", false);

  for (auto arg : ctx.positionals) {
    cfg.names.push_back(std::string(arg));
  }

  return cfg;
}

auto get_basename(std::string_view path, std::string_view suffix)
    -> std::string {
  std::string result = std::string(path);

  // Remove trailing slashes
  while (!result.empty() && (result.back() == '/' || result.back() == '\\')) {
    result.pop_back();
  }

  // Find last separator
  size_t last_sep = result.find_last_of("/\\");
  if (last_sep != std::string::npos) {
    result = result.substr(last_sep + 1);
  }

  // Remove suffix if specified
  if (!suffix.empty() && result.size() >= suffix.size()) {
    if (result.substr(result.size() - suffix.size()) == suffix) {
      result = result.substr(0, result.size() - suffix.size());
    }
  }

  return result;
}

auto run(const Config& cfg) -> int {
  if (cfg.names.empty()) {
    cp::report_custom_error(L"basename", L"missing operand");
    return 1;
  }

  if (!cfg.multiple && cfg.names.size() > 1) {
    // Non-multiple mode: first arg is NAME, second arg (if any) is SUFFIX
    std::string name = cfg.names[0];
    std::string suffix = cfg.names.size() > 1 ? cfg.names[1] : cfg.suffix;

    std::string result = get_basename(name, suffix);
    if (cfg.zero) {
      safePrint(result);
      safePrint(char{'\0'});
    } else {
      safePrintLn(result);
    }
  } else {
    // Multiple mode: process all names
    for (const auto& name : cfg.names) {
      std::string result = get_basename(name, cfg.suffix);
      if (cfg.zero) {
        safePrint(result);
        safePrint(char{'\0'});
      } else {
        safePrintLn(result);
      }
    }
  }

  return 0;
}

}  // namespace basename_pipeline

REGISTER_COMMAND(basename, "basename",
                 "basename NAME [SUFFIX]\n"
                 "basename OPTION... NAME...",
                 "Print NAME with any leading directory components removed.\n"
                 "If specified, also remove a trailing SUFFIX.",
                 "  basename /path/to/file.txt\n"
                 "  basename /path/to/file.txt .txt\n"
                 "  basename -a file1.txt file2.txt\n"
                 "  basename -s .txt file1 file2",
                 "dirname(1), realpath(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", BASENAME_OPTIONS) {
  using namespace basename_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"basename");
    return 1;
  }

  return run(*cfg_result);
}
