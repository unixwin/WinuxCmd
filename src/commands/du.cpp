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
 *  - File: du.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
/// @Description: Implementation for du - estimate file space usage
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

/**
 * @brief DU command options definition
 *
 * This array defines all the options supported by the du command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 * - @a -a, @a --all: write counts for all files [IMPLEMENTED]
 * - @a -B, @a --block-size=SIZE: scale sizes by SIZE [TODO]
 * - @a -b, @a --bytes: equivalent to --apparent-size --block-size=1
 * [IMPLEMENTED]
 * - @a -c, @a --total: produce a grand total [TODO]
 * - @a -d, @a --max-depth=N: print the total for a directory only if it is N or
 * fewer levels below [IMPLEMENTED]
 * - @a -h, @a --human-readable: print sizes in powers of 1024 [IMPLEMENTED]
 * - @a -H, @a --si: print sizes in powers of 1000 [IMPLEMENTED]
 * - @a -k: like --block-size=1K [IMPLEMENTED]
 * - @a -s, @a --summarize: display only a total for each argument [IMPLEMENTED]
 */
auto constexpr DU_OPTIONS = std::array{
    OPTION("-a", "--all", "write counts for all files, not just directories"),
    OPTION("-B", "--block-size",
           "scale sizes by SIZE before printing them [TODO]", STRING_TYPE),
    OPTION("-b", "--bytes", "equivalent to '--apparent-size --block-size=1'"),
    OPTION("-c", "--total", "produce a grand total [TODO]"),
    OPTION(
        "-d", "--max-depth",
        "print the total for a directory only if it is N or fewer levels below",
        INT_TYPE),
    OPTION("-h", "--human-readable",
           "print sizes in powers of 1024 (e.g., 1023M)"),
    OPTION("-H", "--si", "print sizes in powers of 1000 (e.g., 1.1G)"),
    OPTION("-k", "", "like --block-size=1K"),
    OPTION("-s", "--summarize", "display only a total for each argument")};

// ======================================================
// Pipeline components
// ======================================================
namespace du_pipeline {
namespace cp = core::pipeline;

/**
 * @brief Format size to human-readable string
 * @param size Size in bytes
 * @param si Use 1000-based units instead of 1024-based
 * @return Formatted string
 */
auto format_size(uint64_t size, bool si) -> std::string {
  const char* units = si ? const_cast<char*>("BKMGTPE")  // 1000-based
                         : const_cast<char*>("BKMGTP");  // 1024-based

  double base = si ? 1000.0 : 1024.0;
  int unit_index = 0;
  double size_d = static_cast<double>(size);

  while (size_d >= base && unit_index < 6) {
    size_d /= base;
    unit_index++;
  }

  char buf[32];
  if (unit_index == 0) {
    snprintf(buf, sizeof(buf), "%.0f", size_d);
  } else if (size_d < 10.0) {
    snprintf(buf, sizeof(buf), "%.1f%c", size_d, units[unit_index]);
  } else {
    snprintf(buf, sizeof(buf), "%.0f%c", size_d, units[unit_index]);
  }

  return std::string(buf);
}

/**
 * @brief Get file size
 * @param path File path
 * @return File size or 0 if error
 */
auto get_file_size(const std::wstring& path) -> uint64_t {
  WIN32_FILE_ATTRIBUTE_DATA data;
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
    return 0;
  }

  return (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}

/**
 * @brief Calculate directory size recursively
 * @param path Directory path
 * @param sizes Output map of path to size
 * @param max_depth Maximum depth to calculate (-1 for unlimited)
 * @param current_depth Current depth
 * @param count_all Count all files, not just directories
 * @param summarize Only show totals for arguments
 * @return Total size
 */
auto calculate_dir_size(const std::wstring& path,
                        std::unordered_map<std::wstring, uint64_t>& sizes,
                        int max_depth, int current_depth, bool count_all,
                        bool summarize) -> uint64_t {
  WIN32_FIND_DATAW find_data;
  std::wstring search_path = path + L"\\*";
  HANDLE hFind = FindFirstFileW(search_path.c_str(), &find_data);

  if (hFind == INVALID_HANDLE_VALUE) {
    return 0;
  }

  uint64_t total_size = 0;

  do {
    std::wstring filename = find_data.cFileName;

    // Skip . and ..
    if (filename == L"." || filename == L"..") {
      continue;
    }

    std::wstring full_path = path + L"\\" + filename;

    if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      // Recursively calculate subdirectory size
      uint64_t dir_size = calculate_dir_size(
          full_path, sizes, max_depth, current_depth + 1, count_all, summarize);
      total_size += dir_size;

      // Add to sizes map if within max_depth or showing all
      if (max_depth < 0 || current_depth <= max_depth) {
        sizes[full_path] = dir_size;
      }
    } else {
      // It's a file
      uint64_t file_size = get_file_size(full_path);
      total_size += file_size;

      // Count individual files if requested
      if (count_all && (max_depth < 0 || current_depth < max_depth)) {
        sizes[full_path] = file_size;
      }
    }
  } while (FindNextFileW(hFind, &find_data) != 0);

  FindClose(hFind);

  // Add the directory itself to the sizes map
  sizes[path] = total_size;

  return total_size;
}

/**
 * @brief Print disk usage information
 * @param ctx Command context
 * @return Result with success status
 */
auto print_disk_usage(const CommandContext<DU_OPTIONS.size()>& ctx)
    -> cp::Result<bool> {
  // Use SmallVector for file paths (max 32 paths) - all stack-allocated
  SmallVector<std::string, 32> paths{};

  if (ctx.positionals.empty()) {
    paths.push_back(".");
  } else {
    for (const auto& arg : ctx.positionals) {
      std::string file_arg(arg);
      if (contains_wildcard(file_arg)) {
        auto glob_result = glob_expand(file_arg);
        if (glob_result.expanded) {
          for (const auto& file : glob_result.files) {
            paths.push_back(wstring_to_utf8(file));
          }
          continue;
        }
      }
      paths.push_back(file_arg);
    }
  }

  bool count_all = ctx.get<bool>("--all", false) || ctx.get<bool>("-a", false);
  bool bytes = ctx.get<bool>("--bytes", false) || ctx.get<bool>("-b", false);
  bool human =
      ctx.get<bool>("--human-readable", false) || ctx.get<bool>("-h", false);
  bool si = ctx.get<bool>("--si", false) || ctx.get<bool>("-H", false);
  bool kibi = ctx.get<bool>("-k", false);
  bool summarize =
      ctx.get<bool>("--summarize", false) || ctx.get<bool>("-s", false);

  int max_depth = ctx.get<int>("--max-depth", -1);
  if (ctx.get<int>("-d", -1) != -1) {
    max_depth = ctx.get<int>("-d", -1);
  }

  bool all_ok = true;

  for (size_t i = 0; i < paths.size(); ++i) {
    const auto& path = paths[i];
    std::wstring wpath = utf8_to_wstring(path);

    // Check if path exists
    DWORD attrs = GetFileAttributesW(wpath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
      safeErrorPrint("du: cannot access '");
      safeErrorPrint(path);
      safeErrorPrint("': No such file or directory\n");
      all_ok = false;
      continue;
    }

    std::unordered_map<std::wstring, uint64_t> sizes;

    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
      // Calculate directory size
      calculate_dir_size(wpath, sizes, max_depth, 0, count_all, summarize);

      // Print directory size
      uint64_t dir_size = sizes[wpath];

      safePrint(L"");
      if (bytes) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%16ju", dir_size);
        safePrint(buf);
      } else if (kibi) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%12ju", dir_size / 1024);
        safePrint(buf);
      } else if (human || si) {
        safePrint(format_size(dir_size, si));
      } else {
        char buf[32];
        snprintf(buf, sizeof(buf), "%16ju",
                 dir_size / 512);  // Default to 512-byte blocks
        safePrint(buf);
      }
      safePrint(L"  ");
      safePrintLn(wpath);

      // Print subdirectories/files if not summarize mode
      if (!summarize) {
        for (const auto& [subpath, size] : sizes) {
          if (subpath != wpath) {  // Skip the root directory itself
            safePrint(L"");
            if (bytes) {
              char buf[32];
              snprintf(buf, sizeof(buf), "%16ju", size);
              safePrint(buf);
            } else if (kibi) {
              char buf[32];
              snprintf(buf, sizeof(buf), "%12ju", size / 1024);
              safePrint(buf);
            } else if (human || si) {
              safePrint(format_size(size, si));
            } else {
              char buf[32];
              snprintf(buf, sizeof(buf), "%16ju", size / 512);
              safePrint(buf);
            }
            safePrint(L"  ");
            safePrintLn(subpath);
          }
        }
      }
    } else {
      // It's a file
      uint64_t file_size = get_file_size(wpath);

      safePrint(L"");
      if (bytes) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%16ju", file_size);
        safePrint(buf);
      } else if (kibi) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%12ju", file_size / 1024);
        safePrint(buf);
      } else if (human || si) {
        safePrint(format_size(file_size, si));
      } else {
        char buf[32];
        snprintf(buf, sizeof(buf), "%16ju", file_size / 512);
        safePrint(buf);
      }
      safePrint(L"  ");
      safePrintLn(wpath);
    }
  }

  return all_ok;
}

}  // namespace du_pipeline

REGISTER_COMMAND(
    du,
    /* name */
    "du",

    /* synopsis */
    "estimate file space usage",

    /* description */
    "The du command displays the amount of disk space used by the specified\n"
    "files and for each subdirectory (of directory arguments). If no path\n"
    "is given, the current directory is used.\n\n"
    "On Windows, it calculates the total size of all files in a directory\n"
    "tree recursively.",

    /* examples */
    "  du\n"
    "  du -h\n"
    "  du -sh /path/to/dir\n"
    "  du -d 1",

    /* see_also */
    "df(1)",

    /* author */
    "caomengxuan666",

    /* copyright */
    "Copyright © 2026 WinuxCmd",

    /* options */
    DU_OPTIONS) {
  using namespace du_pipeline;

  auto result = print_disk_usage(ctx);
  if (!result) {
    cp::report_error(result, L"du");
    return 1;
  }

  return *result ? 0 : 1;
}
