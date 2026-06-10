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
 *  - File: mktemp.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for mktemp.
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

auto constexpr MKTEMP_OPTIONS = std::array{
    OPTION("-d", "--directory", "make a directory instead of a file",
           BOOL_TYPE),
    OPTION("-u", "--dry-run", "do not actually create anything", BOOL_TYPE),
    OPTION("-q", "--quiet", "suppress diagnostics", BOOL_TYPE),
    OPTION("", "--suffix",
           "append SUFFIX to TEMPLATE; SUFFIX must not contain a path "
           "separator",
           STRING_TYPE),
    OPTION("-t", "--tmpdir", "interpret TEMPLATE relative to DIR", STRING_TYPE),
    OPTION("-p", "--tmpdir",
           "interpret TEMPLATE relative to DIR; if DIR is not specified, use "
           "$TMPDIR",
           STRING_TYPE)};

namespace mktemp_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool make_directory = false;
  bool dry_run = false;
  bool quiet = false;
  std::string tmpdir;
  std::string template_str;
  std::string suffix;
};

auto build_config(const CommandContext<MKTEMP_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.make_directory =
      ctx.get<bool>("--directory", false) || ctx.get<bool>("-d", false);
  cfg.dry_run = ctx.get<bool>("--dry-run", false) || ctx.get<bool>("-u", false);
  cfg.quiet = ctx.get<bool>("--quiet", false) || ctx.get<bool>("-q", false);

  auto tmpdir_opt = ctx.get<std::string>("--tmpdir", "");
  if (tmpdir_opt.empty()) {
    tmpdir_opt = ctx.get<std::string>("-t", "");
  }
  if (tmpdir_opt.empty()) {
    tmpdir_opt = ctx.get<std::string>("-p", "");
  }
  if (!tmpdir_opt.empty()) {
    cfg.tmpdir = tmpdir_opt;
  }

  cfg.suffix = ctx.get<std::string>("--suffix", "");
  if (cfg.suffix.find('/') != std::string::npos ||
      cfg.suffix.find('\\') != std::string::npos) {
    return std::unexpected("invalid suffix '" + cfg.suffix +
                           "', contains directory separator");
  }

  // Get template from positionals
  if (!ctx.positionals.empty()) {
    cfg.template_str = std::string(ctx.positionals[0]);
  } else {
    // Default template
    cfg.template_str = "tmp.XXXXXX";
  }

  return cfg;
}

auto run(const Config& cfg) -> int {
  auto find_template_run = [](std::string_view templ)
      -> std::optional<std::pair<size_t, size_t>> {
    auto last_sep = templ.find_last_of("/\\");
    size_t component_start =
        last_sep == std::string_view::npos ? 0 : last_sep + 1;

    size_t best_start = std::string_view::npos;
    size_t best_len = 0;
    for (size_t i = component_start; i < templ.size();) {
      if (templ[i] != 'X') {
        ++i;
        continue;
      }
      size_t j = i;
      while (j < templ.size() && templ[j] == 'X') {
        ++j;
      }
      size_t len = j - i;
      if (len >= 3) {
        best_start = i;
        best_len = len;
      }
      i = j;
    }

    if (best_start == std::string_view::npos) {
      return std::nullopt;
    }
    return std::pair{best_start, best_len};
  };

  std::filesystem::path base_dir;
  std::string template_component = cfg.template_str;

  if (!cfg.tmpdir.empty()) {
    base_dir = std::filesystem::path(cfg.tmpdir);
    template_component =
        std::filesystem::path(cfg.template_str).filename().string();
  } else {
    std::filesystem::path template_path = std::filesystem::path(cfg.template_str);
    base_dir = template_path.parent_path();
    template_component = template_path.filename().string();
  }

  if (template_component.empty()) {
    template_component = "tmp.XXXXXX";
  }

  // Process template. Replace the final X-run in the last path component and
  // append --suffix after that replacement, matching GNU/Microsoft usage such
  // as `mktemp file-XXXX.txt` and `mktemp --suffix=.txt file-XXXX`.
  std::string template_str = template_component;
  auto run_info = find_template_run(template_str);
  if (!run_info) {
    template_str += "XXXXXX";
    run_info = std::pair{template_str.size() - 6, size_t{6}};
  }
  template_str += cfg.suffix;
  auto [xxx_pos, xxx_len] = *run_info;

  // Generate unique filename
  std::string temp_file;
  int max_attempts = 100;

  for (int attempt = 0; attempt < max_attempts; ++attempt) {
    // Generate random characters for XXXXXX
    std::string filename = template_str;
    const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";

    for (size_t i = 0; i < xxx_len; ++i) {
      filename[xxx_pos + i] = charset[rand() % (sizeof(charset) - 1)];
    }

    // Build full path
    std::filesystem::path candidate_path =
        base_dir.empty() ? std::filesystem::path(filename)
                         : (base_dir / std::filesystem::path(filename));
    temp_file = candidate_path.string();

    // Check if file/directory already exists
    DWORD attrs = GetFileAttributesA(temp_file.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
      // File doesn't exist, we can use this name
      break;
    }

    // File exists, try again
    temp_file.clear();
  }

  if (temp_file.empty()) {
    if (!cfg.quiet) {
      cp::Result<int> result2 =
          std::unexpected("failed to create unique filename");
      cp::report_error(result2, L"mktemp");
    }
    return 1;
  }

  // Create file or directory (unless dry-run)
  if (!cfg.dry_run) {
    if (cfg.make_directory) {
      if (!CreateDirectoryA(temp_file.c_str(), NULL)) {
        if (!cfg.quiet) {
          cp::Result<int> result2 =
              std::unexpected("failed to create temporary directory");
          cp::report_error(result2, L"mktemp");
        }
        return 1;
      }
    } else {
      // Create file
      std::ofstream f(temp_file, std::ios::binary);
      if (!f) {
        if (!cfg.quiet) {
          cp::Result<int> result2 =
              std::unexpected("failed to create temporary file");
          cp::report_error(result2, L"mktemp");
        }
        return 1;
      }
      f.close();
    }
  }

  safePrintLn(temp_file);
  return 0;
}

}  // namespace mktemp_pipeline

REGISTER_COMMAND(
    mktemp, "mktemp", "mktemp [OPTION]... [TEMPLATE]",
    "Create a temporary file or directory, safely, and print its name.\n"
    "\n"
    "TEMPLATE must contain at least 3 consecutive 'X's in last component.\n"
    "If TEMPLATE is not specified, use tmp.XXXXXXXXXX instead.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "Note: This implementation uses Windows GetTempFileName API.\n"
    "Custom templates are partially supported.",
    "  mktemp                          # create temp file\n"
    "  mktemp -d                        # create temp directory\n"
    "  mktemp -u                        # just print name\n"
    "  mktemp -t /tmp                   # use specific directory\n"
    "  mktemp --quiet                   # suppress errors",
    "tempfile(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", MKTEMP_OPTIONS) {
  using namespace mktemp_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"mktemp");
    return 1;
  }

  return run(*cfg_result);
}
