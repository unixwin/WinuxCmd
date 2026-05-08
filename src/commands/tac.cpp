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
 *  - File: tac.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for tac.
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

auto constexpr TAC_OPTIONS = std::array{
    OPTION("", "", "concatenate and print files in reverse", STRING_TYPE)
    // tac has no standard options
    // -b, --before (not implemented - separator before)
    // -r, --regex (not implemented - regex separator)
    // -s, --separator (not implemented - custom separator)
};

namespace tac_pipeline {
namespace cp = core::pipeline;

struct Config {
  SmallVector<std::string, 64> files;
};

auto build_config(const CommandContext<TAC_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

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
    cfg.files.push_back("-");
  }

  return cfg;
}

auto run(const Config& cfg) -> int {
  // Use std::vector to avoid stack overflow (SmallVector with 1024 capacity is
  // too large for stack)
  std::vector<std::string> all_lines;

  for (const auto& file : cfg.files) {
    if (file == "-") {
      // Read from stdin
      std::string line;
      while (std::getline(std::cin, line)) {
        all_lines.push_back(line + "\n");
      }
    } else {
      // Read from file
      std::ifstream f(file, std::ios::binary);
      if (!f) {
        auto err = std::string("cannot open '") + file + "' for reading";
        cp::Result<int> result = std::unexpected(std::string_view(err));
        cp::report_error(result, L"tac");
        return 1;
      }

      std::string line;
      while (std::getline(f, line)) {
        // Skip UTF-8 BOM if present at the beginning of the first line
        if (all_lines.empty() && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
          line = line.substr(3);
        }
        all_lines.push_back(line + "\n");
      }

      if (f.fail() && !f.eof()) {
        cp::Result<int> result = std::unexpected("error reading from file");
        cp::report_error(result, L"tac");
        return 1;
      }
    }
  }

  // Print lines in reverse order
  for (auto it = all_lines.rbegin(); it != all_lines.rend(); ++it) {
    safePrint(*it);
  }

  return 0;
}

}  // namespace tac_pipeline

REGISTER_COMMAND(
    tac, "tac", "tac [OPTION]... [FILE]...",
    "Concatenate and print files in reverse.\n"
    "\n"
    "Write each FILE to standard output, last line first.\n"
    "With no FILE, or when FILE is -, read standard input.\n"
    "\n"
    "Note: This is the reverse of 'cat'.\n"
    "Advanced features like custom separators are not implemented.",
    "  tac file.txt\n"
    "  echo -e 'line1\\nline2\\nline3' | tac",
    "cat(1), rev(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", TAC_OPTIONS) {
  using namespace tac_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"tac");
    return 1;
  }

  return run(*cfg_result);
}
