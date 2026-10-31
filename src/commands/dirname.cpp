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
    // [GNU] dirname-lgpl.c (dir_len) under the filename.h configuration
    // the drive face is pinned against (corpus dirname/03 designates
    // the MSYS oracle): FILE_SYSTEM_DRIVE_PREFIX_CAN_BE_RELATIVE is 0
    // in the __CYGWIN__ branch, so the prefix never advances past a
    // following separator and the directory part of 'c:/a' is 'c:',
    // not 'c:/'.
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

// Trailing separators above the root prefix are never part of the
// directory name. The root length is captured once by the caller: for a
// run of three or more leading separators the root is a single slash, and
// recomputing it per pop would stop early at the implementation-defined
// "//" window (dirname /// must be "/", not "//", WinuxCmd #1196).
auto trim_trailing_separators(std::string& path, size_t root_len) -> void {
  while (root_len < path.size() && is_separator(path.back())) {
    path.pop_back();
  }
}

auto get_dirname(std::string_view path) -> std::string {
  if (path.empty()) {
    return ".";
  }

  std::string result(path);
  // [GNU] Strip trailing slashes down to the root prefix. A longer run
  // folds to a single separator because the root is only its first slash
  // ("dirname ///" is "/", "dirname ////a" is "/"); the bare
  // exactly-two-slash operand folds too ("dirname //" is "/", #1337),
  // while the two-slash window is preserved only when more of the path
  // follows ("dirname //a/b" is "//a").
  const size_t raw_root_len = root_length(result);
  trim_trailing_separators(result, raw_root_len);
  const size_t root_len = root_length(result);
  const size_t last_sep = result.find_last_of("/\\");

  if (last_sep == std::string::npos) {
    if (root_len != 0) {
      return result.substr(0, root_len);
    }
    return ".";
  }

  if (result.size() == root_len && root_len != 0) {
    // [GNU] An all-forward-slash operand collapses to a single "/"
    // (#1337); pure-backslash operands keep the Windows spelling.
    if (result.find_first_not_of('/') == std::string::npos) {
      return "/";
    }
    return result.substr(0, root_len);
  }

  if (last_sep <= root_len) {
    return result.substr(0, root_len == 0 ? last_sep + 1 : root_len);
  }

  result.resize(last_sep);
  // Same rule as the first trim, rooted at the same root prefix: dropping
  // the basename must not strand more than the root's separators
  // ("dirname ///a" is "/", not "//").
  trim_trailing_separators(result, root_len);
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
