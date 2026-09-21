// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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

// [GNU] -z, --zero: matches GNU coreutils dirname
// [GNU] -z, --zero: separate output with NUL rather than newline
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

auto is_separator(char c) -> bool { return c == 47 || c == 92; }

auto is_ascii_alpha(char c) -> bool {
  return (65 <= c && c <= 90) || (97 <= c && c <= 122);
}

auto is_drive_prefix(std::string_view path) -> bool {
  return path.size() >= 2 && is_ascii_alpha(path[0]) && path[1] == 58;
}

auto root_length(std::string_view path) -> size_t {
  if (is_drive_prefix(path)) {
    return 2;
  }
  if (path.size() >= 2 && is_separator(path[0]) && is_separator(path[1]) &&
      (path.size() == 2 || !is_separator(path[2]))) {
    return 2;
  }
  if (!path.empty() && is_separator(path[0])) {
    return 1;
  }
  return 0;
}

auto trim_trailing_separators(std::string& path) -> void {
  while (path.size() > root_length(path) && is_separator(path.back())) {
    path.pop_back();
  }
}

auto get_dirname(std::string_view path) -> std::string {
  if (path.empty()) {
    return ".";
  }

  std::string result(path);
  trim_trailing_separators(result);
  const size_t root_len = root_length(result);
  const size_t last_sep = result.find_last_of("/\\");

  if (last_sep == std::string::npos) {
    if (root_len != 0) {
      return result.substr(0, root_len);
    }
    return ".";
  }

  if (result.size() == root_len && root_len != 0) {
    return result.substr(0, root_len);
  }

  if (last_sep <= root_len) {
    return result.substr(0, root_len == 0 ? last_sep + 1 : root_len);
  }

  result.resize(last_sep);
  trim_trailing_separators(result);
  if (result.empty()) {
    return ".";
  }
  return result;
}

auto run(const Config& cfg) -> int {
  for (const auto& name : cfg.names) {
    std::string result = get_dirname(name);
    if (cfg.zero) {
      safePrint(result);
      safePrint(char{'\0'});
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
    if (cfg_result.error() == "missing operand") {
      safeErrorPrintLn("dirname: missing operand");
      safeErrorPrintLn("Try 'dirname --help' for more information.");
      return 1;
    }

    cp::report_error(cfg_result, L"dirname");
    return 1;
  }

  return run(*cfg_result);
}
