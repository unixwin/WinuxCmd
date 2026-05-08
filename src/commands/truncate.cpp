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
 *  - File: truncate.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for truncate.
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

auto constexpr TRUNCATE_OPTIONS = std::array{
    OPTION("-c", "--no-create", "do not create files", BOOL_TYPE),
    OPTION("-s", "--size", "set or adjust the file size by SIZE bytes",
           STRING_TYPE),
    OPTION("-r", "--reference", "base size on RFILE", STRING_TYPE)
    // -o, --io-blocks (not implemented - treat SIZE as number of IO blocks)
};

namespace truncate_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool no_create = false;
  int64_t size = 0;
  std::string reference_file;
  SmallVector<std::string, 64> files;
};

auto parse_size(const std::string& size_str) -> cp::Result<int64_t> {
  try {
    // Support: +N, -N, N, NKB, NMB, NG, etc.
    std::string s = size_str;
    bool relative = false;
    bool negative = false;

    if (!s.empty() && s[0] == '+') {
      relative = true;
      s = s.substr(1);
    } else if (!s.empty() && s[0] == '-') {
      relative = true;
      negative = true;
      s = s.substr(1);
    }

    if (s.empty()) {
      return std::unexpected("invalid size");
    }

    int64_t multiplier = 1;
    if (s.size() > 2) {
      std::string suffix = s.substr(s.size() - 2);
      std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::tolower);

      if (suffix == "kb") {
        multiplier = 1024;
        s = s.substr(0, s.size() - 2);
      } else if (suffix == "mb") {
        multiplier = 1024 * 1024;
        s = s.substr(0, s.size() - 2);
      } else if (suffix == "gb") {
        multiplier = 1024LL * 1024 * 1024;
        s = s.substr(0, s.size() - 2);
      } else if (suffix == "tb") {
        multiplier = 1024LL * 1024 * 1024 * 1024;
        s = s.substr(0, s.size() - 2);
      } else if (suffix == "pb") {
        multiplier = 1024LL * 1024 * 1024 * 1024 * 1024;
        s = s.substr(0, s.size() - 2);
      } else if (suffix == "eb") {
        multiplier = 1024LL * 1024 * 1024 * 1024 * 1024 * 1024;
        s = s.substr(0, s.size() - 2);
      } else if (s.size() > 1) {
        suffix = s.substr(s.size() - 1);
        std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::tolower);

        if (suffix == "k") {
          multiplier = 1024;
          s = s.substr(0, s.size() - 1);
        } else if (suffix == "m") {
          multiplier = 1024 * 1024;
          s = s.substr(0, s.size() - 1);
        } else if (suffix == "g") {
          multiplier = 1024LL * 1024 * 1024;
          s = s.substr(0, s.size() - 1);
        } else if (suffix == "t") {
          multiplier = 1024LL * 1024 * 1024 * 1024;
          s = s.substr(0, s.size() - 1);
        } else if (suffix == "p") {
          multiplier = 1024LL * 1024 * 1024 * 1024 * 1024;
          s = s.substr(0, s.size() - 1);
        } else if (suffix == "e") {
          multiplier = 1024LL * 1024 * 1024 * 1024 * 1024 * 1024;
          s = s.substr(0, s.size() - 1);
        }
      }
    }

    int64_t value = std::stoll(s);
    if (negative) {
      value = -value;
    }
    value *= multiplier;

    return value;
  } catch (...) {
    return std::unexpected("invalid size format");
  }
}

auto build_config(const CommandContext<TRUNCATE_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.no_create =
      ctx.get<bool>("--no-create", false) || ctx.get<bool>("-c", false);

  auto size_opt = ctx.get<std::string>("--size", "");
  if (size_opt.empty()) {
    size_opt = ctx.get<std::string>("-s", "");
  }

  if (size_opt.empty()) {
    auto ref_opt = ctx.get<std::string>("--reference", "");
    if (ref_opt.empty()) {
      ref_opt = ctx.get<std::string>("-r", "");
    }

    if (!ref_opt.empty()) {
      cfg.reference_file = ref_opt;
    } else {
      return std::unexpected("specify --size or --reference");
    }
  } else {
    auto size_result = parse_size(size_opt);
    if (!size_result) {
      return std::unexpected(size_result.error());
    }
    cfg.size = *size_result;
  }

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
    return std::unexpected("missing file operand");
  }

  return cfg;
}

auto run(const Config& cfg) -> int {
  bool all_ok = true;

  for (const auto& file : cfg.files) {
    int64_t target_size = cfg.size;

    // If using reference file, get its size
    if (!cfg.reference_file.empty()) {
      std::ifstream ref_file(cfg.reference_file,
                             std::ios::binary | std::ios::ate);
      if (!ref_file) {
        auto err = std::string("cannot open reference file '") +
                   cfg.reference_file + "'";
        cp::Result<int> result = std::unexpected(std::string_view(err));
        cp::report_error(result, L"truncate");
        all_ok = false;
        continue;
      }
      target_size = ref_file.tellg();
    }

    // Check if file exists
    std::ifstream check_file(file, std::ios::binary);
    if (!check_file) {
      // File doesn't exist
      if (cfg.no_create) {
        // Skip this file
        continue;
      }

      // Create new file with specified size
      std::ofstream new_file(file, std::ios::binary);
      if (!new_file) {
        auto err = std::string("cannot create '") + file + "'";
        cp::Result<int> result = std::unexpected(std::string_view(err));
        cp::report_error(result, L"truncate");
        all_ok = false;
        continue;
      }

      if (target_size > 0) {
        new_file.seekp(target_size - 1);
        new_file.put('\0');
      }
      new_file.close();
    } else {
      // File exists, truncate or extend it
      check_file.close();

      // Read current file content
      std::ifstream in_file(file, std::ios::binary | std::ios::ate);
      if (!in_file) {
        auto err = std::string("cannot open '") + file + "' for reading";
        cp::Result<int> result = std::unexpected(std::string_view(err));
        cp::report_error(result, L"truncate");
        all_ok = false;
        continue;
      }

      int64_t current_size = in_file.tellg();
      in_file.seekg(0);

      // Read content to preserve
      std::string content;
      if (target_size > 0) {
        content.resize(std::min(current_size, target_size));
        if (!content.empty()) {
          in_file.read(&content[0], content.size());
        }
      }
      in_file.close();

      // Write back with new size
      std::ofstream out_file(file, std::ios::binary | std::ios::trunc);
      if (!out_file) {
        auto err = std::string("cannot open '") + file + "' for writing";
        cp::Result<int> result = std::unexpected(std::string_view(err));
        cp::report_error(result, L"truncate");
        all_ok = false;
        continue;
      }

      if (!content.empty()) {
        out_file.write(content.data(), content.size());
      }

      // Extend file if needed
      if (target_size > static_cast<int64_t>(content.size())) {
        out_file.seekp(target_size - 1);
        out_file.put('\0');
      }

      out_file.close();
    }
  }

  return all_ok ? 0 : 1;
}

}  // namespace truncate_pipeline

REGISTER_COMMAND(
    truncate, "truncate", "truncate OPTION... FILE...",
    "Shrink or extend the size of each FILE to the specified size.\n"
    "\n"
    "A FILE argument that does not exist is created.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "SIZE is an integer and optional unit (example: 10M is 10*1024*1024).\n"
    "Units are K, M, G, T, P, E, Z, Y (powers of 1024) or KB, MB,... (powers "
    "of 1000).\n"
    "\n"
    "Note: This implementation supports basic truncation.\n"
    "Advanced features like IO blocks are not implemented.",
    "  truncate -s 0 file.txt          # empty file\n"
    "  truncate -s 1K file.txt         # 1KB file\n"
    "  truncate -s +100 file.txt      # extend by 100 bytes\n"
    "  truncate -s -50 file.txt       # shrink by 50 bytes\n"
    "  truncate -s 1M newfile.txt     # create 1MB file",
    "dd(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", TRUNCATE_OPTIONS) {
  using namespace truncate_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"truncate");
    return 1;
  }

  return run(*cfg_result);
}
