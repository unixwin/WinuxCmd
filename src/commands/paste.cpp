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
 *  - File: paste.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for paste.
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

auto constexpr PASTE_OPTIONS = std::array{
    OPTION("-d", "--delimiters", "reuse characters from LIST instead of TAB",
           OPTIONAL_STRING_TYPE),
    OPTION("-s", "--serial", "paste one file at a time instead of in parallel",
           BOOL_TYPE),
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline")};

namespace paste_pipeline {
namespace cp = core::pipeline;

struct Config {
  std::vector<std::string> delimiters = {"\t"};
  bool serial = false;
  bool zero_terminated = false;
  SmallVector<std::string, 64> files;
};

auto parse_delimiters(std::string_view input)
    -> cp::Result<std::vector<std::string>> {
  std::vector<std::string> delimiters;

  for (size_t i = 0; i < input.size(); ++i) {
    if (input[i] != '\\') {
      delimiters.push_back(std::string(1, input[i]));
      continue;
    }

    if (i + 1 >= input.size()) {
      return std::unexpected("delimiter list ends with an unescaped backslash");
    }

    char escaped = input[++i];
    switch (escaped) {
      case '0':
        delimiters.emplace_back();
        break;
      case 'b':
        delimiters.push_back("\b");
        break;
      case 'f':
        delimiters.push_back("\f");
        break;
      case 't':
        delimiters.push_back("\t");
        break;
      case 'n':
        delimiters.push_back("\n");
        break;
      case 'r':
        delimiters.push_back("\r");
        break;
      case 'v':
        delimiters.push_back("\v");
        break;
      case '\\':
        delimiters.push_back("\\");
        break;
      default:
        delimiters.push_back(std::string(1, escaped));
        break;
    }
  }

  return delimiters;
}

void add_file_arg(Config& cfg, const std::string& file_arg) {
  if (contains_wildcard(file_arg)) {
    auto glob_result = glob_expand(file_arg);
    if (glob_result.expanded) {
      for (const auto& file : glob_result.files) {
        cfg.files.push_back(wstring_to_utf8(file));
      }
      return;
    }
  }
  cfg.files.push_back(file_arg);
}

auto build_config(const CommandContext<PASTE_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  bool end_of_options = false;
  for (size_t i = 0; i < ctx.raw_args.size(); ++i) {
    std::string arg(ctx.raw_args[i]);

    if (!end_of_options && arg == "--") {
      end_of_options = true;
      continue;
    }

    if (!end_of_options && arg.starts_with("--delimiters=")) {
      auto parsed = parse_delimiters(std::string_view(arg).substr(
          std::string_view("--delimiters=").size()));
      if (!parsed) return std::unexpected(parsed.error());
      cfg.delimiters = std::move(*parsed);
      continue;
    }

    if (!end_of_options && arg == "--delimiters") {
      if (i + 1 >= ctx.raw_args.size()) {
        return std::unexpected("option '--delimiters' requires an argument");
      }
      auto parsed = parse_delimiters(ctx.raw_args[++i]);
      if (!parsed) return std::unexpected(parsed.error());
      cfg.delimiters = std::move(*parsed);
      continue;
    }

    if (!end_of_options && arg.starts_with("-d") && arg != "-") {
      std::string value;
      if (arg.size() > 2) {
        value = arg.substr(2);
      } else {
        if (i + 1 >= ctx.raw_args.size()) {
          return std::unexpected("option '-d' requires an argument");
        }
        value = std::string(ctx.raw_args[++i]);
      }
      auto parsed = parse_delimiters(value);
      if (!parsed) return std::unexpected(parsed.error());
      cfg.delimiters = std::move(*parsed);
      continue;
    }

    if (!end_of_options && arg.starts_with("--")) {
      if (arg == "--serial") {
        cfg.serial = true;
        continue;
      }
      if (arg == "--zero-terminated") {
        cfg.zero_terminated = true;
        continue;
      }
    }

    if (!end_of_options && arg.size() >= 2 && arg[0] == '-' && arg[1] != '-') {
      bool handled = true;
      for (size_t pos = 1; pos < arg.size(); ++pos) {
        if (arg[pos] == 's') {
          cfg.serial = true;
        } else if (arg[pos] == 'z') {
          cfg.zero_terminated = true;
        } else if (arg[pos] == 'd') {
          std::string value;
          if (pos + 1 < arg.size()) {
            value = arg.substr(pos + 1);
          } else {
            if (i + 1 >= ctx.raw_args.size()) {
              return std::unexpected("option '-d' requires an argument");
            }
            value = std::string(ctx.raw_args[++i]);
          }
          auto parsed = parse_delimiters(value);
          if (!parsed) return std::unexpected(parsed.error());
          cfg.delimiters = std::move(*parsed);
          break;
        } else {
          handled = false;
          break;
        }
      }
      if (handled) continue;
    }

    add_file_arg(cfg, arg);
  }

  if (cfg.files.empty()) {
    cfg.files.push_back("-");  // Read from stdin
  }

  return cfg;
}

// Read all lines from a file
auto read_lines(const std::string& filename, char delimiter = '\n')
    -> cp::Result<SmallVector<std::string, 1024>> {
  SmallVector<std::string, 1024> lines;

  if (filename == "-") {
    // Read from stdin
    std::string content;
    {
      std::ostringstream oss;
      oss << std::cin.rdbuf();
      content = oss.str();
    }
    size_t start = 0;
    for (size_t i = 0; i < content.size(); ++i) {
      if (content[i] == delimiter) {
        lines.emplace_back(content.substr(start, i - start));
        start = i + 1;
      }
    }
    if (start < content.size()) {
      lines.emplace_back(content.substr(start));
    }
  } else {
    // Read from file
    std::ifstream f(filename, std::ios::binary);
    if (!f) {
      return std::unexpected(std::string("cannot open '") + filename +
                             "' for reading");
    }

    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());

    size_t start = 0;
    for (size_t i = 0; i < content.size(); ++i) {
      if (content[i] == delimiter) {
        std::string line = content.substr(start, i - start);
        // Skip UTF-8 BOM if present at the beginning of the first line
        if (lines.empty() && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
          line = line.substr(3);
        }
        lines.push_back(line);
        start = i + 1;
      }
    }
    if (start < content.size()) {
      std::string line = content.substr(start);
      if (lines.empty() && line.size() >= 3 &&
          static_cast<unsigned char>(line[0]) == 0xEF &&
          static_cast<unsigned char>(line[1]) == 0xBB &&
          static_cast<unsigned char>(line[2]) == 0xBF) {
        line = line.substr(3);
      }
      lines.push_back(line);
    }
  }

  return lines;
}

void append_delimiter(std::string& output,
                      const std::vector<std::string>& delimiters,
                      size_t delimiter_index) {
  if (delimiters.empty()) return;
  output += delimiters[delimiter_index % delimiters.size()];
}

auto run(const Config& cfg) -> int {
  const char record_delim = cfg.zero_terminated ? '\0' : '\n';

  if (cfg.serial) {
    // Serial mode: paste files one after another
    for (const auto& file : cfg.files) {
      auto lines_result = read_lines(file, record_delim);
      if (!lines_result) {
        cp::report_error(lines_result, L"paste");
        return 1;
      }

      std::string merged_line;
      for (size_t i = 0; i < lines_result->size(); ++i) {
        if (i > 0) {
          append_delimiter(merged_line, cfg.delimiters, i - 1);
        }
        merged_line += (*lines_result)[i];
      }
      if (!lines_result->empty()) {
        safePrint(merged_line);
        safePrint(std::string_view(&record_delim, 1));
      }
    }
  } else {
    // Parallel mode: merge lines from all files
    // Use std::vector to avoid stack overflow (SmallVector was allocating too
    // much on stack)
    std::vector<SmallVector<std::string, 1024>> all_lines;
    SmallVector<std::string, 1024> stdin_lines;
    bool stdin_loaded = false;
    size_t stdin_columns = 0;

    // Read all files
    for (const auto& file : cfg.files) {
      if (file == "-") {
        ++stdin_columns;
        if (!stdin_loaded) {
          auto lines_result = read_lines(file, record_delim);
          if (!lines_result) {
            cp::report_error(lines_result, L"paste");
            return 1;
          }
          stdin_lines = std::move(*lines_result);
          stdin_loaded = true;
        }
        all_lines.emplace_back();
      } else {
        auto lines_result = read_lines(file, record_delim);
        if (!lines_result) {
          cp::report_error(lines_result, L"paste");
          return 1;
        }
        all_lines.push_back(std::move(*lines_result));
      }
    }

    // Find maximum number of lines
    size_t max_lines = 0;
    for (size_t idx = 0; idx < all_lines.size(); ++idx) {
      if (cfg.files[idx] == "-") continue;
      const auto& lines = all_lines[idx];
      if (lines.size() > max_lines) {
        max_lines = lines.size();
      }
    }
    if (stdin_columns > 0) {
      max_lines = std::max(
          max_lines, (stdin_lines.size() + stdin_columns - 1) / stdin_columns);
    }

    // Merge lines
    size_t stdin_index = 0;
    for (size_t i = 0; i < max_lines; ++i) {
      std::string merged_line;
      for (size_t j = 0; j < all_lines.size(); ++j) {
        if (cfg.files[j] == "-") {
          if (stdin_index < stdin_lines.size()) {
            merged_line += stdin_lines[stdin_index++];
          }
        } else if (i < all_lines[j].size()) {
          merged_line += all_lines[j][i];
        }

        // Add delimiter if not last file
        if (j < all_lines.size() - 1) {
          append_delimiter(merged_line, cfg.delimiters, j);
        }
      }

      safePrint(merged_line);
      safePrint(std::string_view(&record_delim, 1));
    }
  }

  return 0;
}

}  // namespace paste_pipeline

REGISTER_COMMAND(
    paste, "paste", "paste [OPTION]... [FILE]...",
    "Merge lines of files.\n"
    "\n"
    "Write lines consisting of the sequentially corresponding lines\n"
    "from each FILE, separated by TABs, to standard output.\n"
    "With no FILE, or when FILE is -, read standard input.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "Note: This implementation supports basic paste functionality.",
    "  paste file1 file2\n"
    "  paste -d '|' file1 file2    # use pipe as delimiter\n"
    "  paste -s file                # serial mode (one file at a time)\n"
    "  paste -d '\\n' file          # use newline as delimiter",
    "cut(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", PASTE_OPTIONS) {
  using namespace paste_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"paste");
    return 1;
  }

  return run(*cfg_result);
}
