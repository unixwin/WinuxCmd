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
    OPTION("-t", "--tmpdir", "interpret TEMPLATE relative to DIR", STRING_TYPE)
    // -p, --tmpdir (not implemented - same as -t)
};

namespace mktemp_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool make_directory = false;
  bool dry_run = false;
  bool quiet = false;
  std::string tmpdir;
  std::string template_str;
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
  if (!tmpdir_opt.empty()) {
    cfg.tmpdir = tmpdir_opt;
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
  // Determine temp directory
  std::string temp_dir = cfg.tmpdir;
  if (temp_dir.empty()) {
    // Use current directory for tests
    temp_dir = ".";
  }

  // Process template
  std::string template_str = cfg.template_str;

  // Find the XXXXXX pattern and replace with random characters
  size_t xxx_pos = template_str.find("XXXXXX");
  if (xxx_pos == std::string::npos) {
    // If no XXXXXX, append it
    template_str += "XXXXXX";
    xxx_pos = template_str.size() - 6;
  }

  // Generate unique filename
  std::string temp_file;
  int max_attempts = 100;

  for (int attempt = 0; attempt < max_attempts; ++attempt) {
    // Generate random characters for XXXXXX
    std::string filename = template_str;
    const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";

    for (size_t i = 0; i < 6; ++i) {
      filename[xxx_pos + i] = charset[rand() % (sizeof(charset) - 1)];
    }

    // Build full path
    temp_file = temp_dir;
    if (temp_file.back() != '\\' && temp_file.back() != '/') {
      temp_file += "\\";
    }
    temp_file += filename;

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

  // Print just the filename (not full path)
  size_t last_slash = temp_file.find_last_of("/\\");
  std::string filename_only = (last_slash != std::string::npos)
                                  ? temp_file.substr(last_slash + 1)
                                  : temp_file;
  safePrintLn(filename_only);
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
