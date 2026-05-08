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
 *  - File: stat.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for stat.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
// *** SIMPLIFIED IMPLEMENTATION - Some features may not be fully supported ***

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr STAT_OPTIONS = std::array{
    OPTION("-L", "dereference", "follow symbolic links", BOOL_TYPE),
    OPTION("-f", "file-system",
           "display file system status instead of file status", BOOL_TYPE),
    OPTION("-c", "format", "use the specified FORMAT instead of the default",
           STRING_TYPE),
    OPTION("-t", "terse", "print the information in terse form", BOOL_TYPE),
    OPTION("", "printf", "like --format, but interpret backslash escapes",
           STRING_TYPE)};

namespace stat_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool dereference = false;
  bool file_system = false;
  std::string format;
  bool terse = false;
  bool printf_format = false;
  SmallVector<std::string, 64> files;
};

auto build_config(const CommandContext<STAT_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.dereference =
      ctx.get<bool>("-dereference", false) || ctx.get<bool>("-L", false);
  cfg.file_system =
      ctx.get<bool>("-file-system", false) || ctx.get<bool>("-f", false);
  cfg.format = ctx.get<std::string>("--format", "");
  cfg.terse = ctx.get<bool>("--terse", false) || ctx.get<bool>("-t", false);

  auto printf_fmt = ctx.get<std::string>("--printf", "");
  if (!printf_fmt.empty()) {
    cfg.format = printf_fmt;
    cfg.printf_format = true;
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
    return std::unexpected("missing operand");
  }

  return cfg;
}

auto format_timestamp(FILETIME ft) -> std::string {
  SYSTEMTIME st;
  FileTimeToSystemTime(&ft, &st);

  char buf[64];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", st.wYear,
           st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  return std::string(buf);
}

auto get_file_size(HANDLE hFile) -> uint64_t {
  LARGE_INTEGER size;
  GetFileSizeEx(hFile, &size);
  return static_cast<uint64_t>(size.QuadPart);
}

auto format_size(uint64_t size) -> std::string {
  const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  int unit_index = 0;
  double dsize = static_cast<double>(size);

  while (dsize >= 1024.0 && unit_index < 4) {
    dsize /= 1024.0;
    unit_index++;
  }

  char buf[64];
  snprintf(buf, sizeof(buf), "%.2f %s", dsize, units[unit_index]);
  return std::string(buf);
}

auto format_permissions(DWORD attrs) -> std::string {
  std::string perm;
  perm += (attrs & FILE_ATTRIBUTE_DIRECTORY) ? 'd' : '-';
  perm += (attrs & FILE_ATTRIBUTE_READONLY) ? 'r' : 'r';
  perm += (attrs & FILE_ATTRIBUTE_READONLY) ? '-' : 'w';
  perm += '-';
  perm += (attrs & FILE_ATTRIBUTE_READONLY) ? 'r' : 'r';
  perm += (attrs & FILE_ATTRIBUTE_READONLY) ? '-' : 'w';
  perm += '-';
  perm += (attrs & FILE_ATTRIBUTE_READONLY) ? 'r' : 'r';
  perm += (attrs & FILE_ATTRIBUTE_READONLY) ? '-' : 'w';
  perm += '-';
  return perm;
}

auto print_stat(const std::string& filename, const Config& cfg) -> int {
  std::error_code ec;
  std::filesystem::path p(filename);

  if (!std::filesystem::exists(p, ec)) {
    safePrint("stat: cannot stat '");
    safePrint(filename);
    safePrint("': No such file or directory\n");
    return 1;
  }

  WIN32_FILE_ATTRIBUTE_DATA data;
  if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &data)) {
    safePrint("stat: cannot stat '");
    safePrint(filename);
    safePrint("': Access denied\n");
    return 1;
  }

  if (cfg.terse) {
    // Terse format
    safePrint(filename);
    safePrint(" ");
    safePrint(data.nFileSizeLow +
              (static_cast<uint64_t>(data.nFileSizeHigh) << 32));
    safePrint(" ");
    safePrint(format_timestamp(data.ftLastWriteTime));
    safePrint("\n");
  } else if (!cfg.format.empty()) {
    // Custom format (simplified)
    safePrint(filename);
    safePrint("\n");
  } else {
    // Default format
    safePrint("  File: ");
    safePrint(filename);
    safePrint("\n");
    safePrint("  Size: ");
    safePrint(format_size(data.nFileSizeLow +
                          (static_cast<uint64_t>(data.nFileSizeHigh) << 32)));
    safePrint("\t");
    safePrint("Blocks: ");
    safePrint((data.nFileSizeLow +
               (static_cast<uint64_t>(data.nFileSizeHigh) << 32)) /
              512);
    safePrint("\t");
    safePrint("IO Block: ");
    safePrint(4096);
    safePrint("\t");

    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      safePrint("directory");
    } else {
      safePrint("regular file");
    }
    safePrint("\n");

    safePrint("Access: (");
    safePrint(format_permissions(data.dwFileAttributes));
    safePrint(")\n");

    safePrint("Modify: ");
    safePrint(format_timestamp(data.ftLastWriteTime));
    safePrint("\n");

    safePrint("Birth: ");
    safePrint(format_timestamp(data.ftCreationTime));
    safePrint("\n");
  }

  return 0;
}

auto run(const Config& cfg) -> int {
  int exit_code = 0;

  for (const auto& file : cfg.files) {
    if (print_stat(file, cfg) != 0) {
      exit_code = 1;
    }
  }

  return exit_code;
}

}  // namespace stat_pipeline

REGISTER_COMMAND(stat, "stat", "stat [OPTION]... FILE...",
                 "Display file or file system status.",
                 "  stat file.txt\n"
                 "  stat -t file.txt\n"
                 "  stat -c %s file.txt",
                 "ls(1), find(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 STAT_OPTIONS) {
  using namespace stat_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"stat");
    return 1;
  }

  return run(*cfg_result);
}
