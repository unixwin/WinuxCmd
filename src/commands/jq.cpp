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
 *  - File: jq.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for jq.
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

auto constexpr JQ_OPTIONS = std::array{
    OPTION("-r", "--raw-output", "output raw strings, not JSON quotes",
           BOOL_TYPE),
    OPTION("-c", "--compact-output", "compact instead of pretty-printed output",
           BOOL_TYPE),
    OPTION("-C", "--color-output", "colorize JSON output", BOOL_TYPE),
    OPTION("-M", "--monochrome-output", "monochrome (don't colorize JSON)",
           BOOL_TYPE),
    OPTION("-S", "--sort-keys", "sort keys of each object alphabetically",
           BOOL_TYPE),
    OPTION("-f", "--from-file", "read filter from file", STRING_TYPE),
    OPTION("-n", "--null-input", "use `null` as the single input value",
           BOOL_TYPE)};

namespace jq_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool raw_output = false;
  bool compact_output = false;
  bool color_output = false;
  bool monochrome_output = false;
  bool sort_keys = false;
  std::string filter_file;
  bool null_input = false;
  std::string filter = ".";
  SmallVector<std::string, 16> files;
};

void append_file_operand(Config& cfg, const std::string& file_arg) {
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

auto build_config(const CommandContext<JQ_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.raw_output =
      ctx.get<bool>("--raw-output", false) || ctx.get<bool>("-r", false);
  cfg.compact_output =
      ctx.get<bool>("--compact-output", false) || ctx.get<bool>("-c", false);
  cfg.color_output =
      ctx.get<bool>("--color-output", false) || ctx.get<bool>("-C", false);
  cfg.monochrome_output =
      ctx.get<bool>("--monochrome-output", false) || ctx.get<bool>("-M", false);
  cfg.sort_keys =
      ctx.get<bool>("--sort-keys", false) || ctx.get<bool>("-S", false);
  cfg.filter_file = ctx.get<std::string>("--from-file", "");
  cfg.null_input =
      ctx.get<bool>("--null-input", false) || ctx.get<bool>("-n", false);

  size_t first_file = 0;
  if (!cfg.filter_file.empty()) {
    cfg.filter = ".";
  } else if (!ctx.positionals.empty()) {
    std::string first_arg(ctx.positionals[0]);
    std::ifstream test_file(first_arg);
    bool is_file = test_file.good();
    test_file.close();

    if (!is_file) {
      cfg.filter = first_arg;
      first_file = 1;
    }
  }

  for (size_t i = first_file; i < ctx.positionals.size(); ++i) {
    append_file_operand(cfg, std::string(ctx.positionals[i]));
  }

  return cfg;
}

auto read_json(const std::string& filename) -> cp::Result<std::string> {
  std::string content;

  if (filename.empty() || filename == "-") {
    // Read from stdin
    content.assign(std::istreambuf_iterator<char>(std::cin),
                   std::istreambuf_iterator<char>());
    if (std::cin.fail() && !std::cin.eof()) {
      return std::unexpected("error reading from standard input");
    }
  } else {
    // Read from file
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
      return std::unexpected(std::string("cannot open '") + filename +
                             "' for reading");
    }
    content.assign(std::istreambuf_iterator<char>(file),
                   std::istreambuf_iterator<char>());
    if (file.fail() && !file.eof()) {
      return std::unexpected("error reading from file");
    }
  }

  return content;
}

auto run(const Config& cfg) -> int {
  // Read JSON input
  std::string json_input;

  if (cfg.null_input) {
    json_input = "null";
  } else {
    if (cfg.files.empty()) {
      auto content_result = read_json("");
      if (!content_result) {
        cp::report_error(content_result, L"jq");
        return 1;
      }
      json_input = *content_result;
    } else {
      for (const auto& file : cfg.files) {
        auto content_result = read_json(file);
        if (!content_result) {
          cp::report_error(content_result, L"jq");
          return 1;
        }
        json_input += *content_result;
        if (&file != &cfg.files.back()) {
          json_input += '\n';
        }
      }
    }
  }

  // Parse JSON with comment support enabled
  nlohmann::json data;
  try {
    data = nlohmann::json::parse(
        json_input,
        /* callback */ nullptr,
        /* allow exceptions */ true,
        /* ignore_comments */ true  // Support // and /* */ comments
    );
  } catch (const nlohmann::json::exception& e) {
    safePrintLn(std::string("parse error: ") + e.what());
    return 1;
  }

  // Format and output JSON
  int indent = cfg.compact_output ? -1 : 2;
  auto formatted = data.dump(indent, ' ', true);  // true = ensure ASCII

  if (cfg.raw_output) {
    // Try to extract string value if it's a simple string
    try {
      if (data.is_string()) {
        formatted = data.get<std::string>();
      }
    } catch (const nlohmann::json::exception&) {
      // Keep the formatted JSON
    }
  }

  safePrintLn(formatted);
  return 0;
}

}  // namespace jq_pipeline

REGISTER_COMMAND(
    jq, "jq", "jq [OPTIONS]... FILTER [FILES...]",
    "jq is a command-line JSON processor powered by nlohmann/json.\n"
    "\n"
    "Features:\n"
    "- Parse and format JSON with nlohmann/json library\n"
    "- Support for JSON comments (// and /* */)\n"
    "- Pretty-printed or compact output\n"
    "- Raw string output mode\n"
    "- Sort keys alphabetically\n"
    "\n"
    "Note: This is a formatter with basic comment support.\n"
    "For full jq query language features, use official jq from "
    "https://jqlang.org/.",
    "  echo '{\"name\":\"John\",\"age\":30}' | jq\n"
    "  echo '{\"name\":\"John\"}' | jq '.'\n"
    "  echo '[1,2,3]' | jq -c\n"
    "  cat file.json | jq -S\n"
    "  cat config.json | jq  # Supports // and /* */ comments",
    "https://jqlang.org/manual/", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    JQ_OPTIONS) {
  using namespace jq_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"jq");
    return 1;
  }

  return run(*cfg_result);
}
