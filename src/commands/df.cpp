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
 *  - File: df.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
/// @Description: Implementation for df - display disk space usage
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
 * @brief DF command options definition
 *
 * This array defines all the options supported by the df command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 * - @a -h, @a --human-readable: print sizes in powers of 1024 [IMPLEMENTED]
 * - @a -H, @a --si: print sizes in powers of 1000 [IMPLEMENTED]
 * - @a -i, @a --inodes: list inode information instead of block usage [TODO]
 * - @a -k: like --block-size=1K [IMPLEMENTED]
 * - @a -T, @a --print-type: print file system type [TODO]
 */
auto constexpr DF_OPTIONS = std::array{
    OPTION("-h", "--human-readable",
           "print sizes in powers of 1024 (e.g., 1023M)"),
    OPTION("-H", "--si", "print sizes in powers of 1000 (e.g., 1.1G)"),
    OPTION("-i", "--inodes",
           "list inode information instead of block usage [TODO]"),
    OPTION("-k", "", "like --block-size=1K"),
    OPTION("-T", "--print-type", "print file system type [TODO]")};

// ======================================================
// Pipeline components
// ======================================================
namespace df_pipeline {
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
 * @brief Get disk free space information
 * @param path Path to check (any file/directory on the volume)
 * @return Disk info or error message
 */
auto get_disk_info(const std::string& path)
    -> std::optional<std::tuple<std::string, uint64_t, uint64_t, uint64_t>> {
  std::wstring wpath = utf8_to_wstring(path);

  ULARGE_INTEGER free_bytes, total_bytes, total_free;

  if (!GetDiskFreeSpaceExW(wpath.c_str(), &free_bytes, &total_bytes,
                           &total_free)) {
    return std::nullopt;
  }

  // Get volume information
  wchar_t volume_name[MAX_PATH];
  wchar_t file_system_name[MAX_PATH] = {0};

  if (GetVolumeInformationW(wpath.c_str(), volume_name, MAX_PATH, nullptr,
                            nullptr, nullptr, file_system_name, MAX_PATH)) {
    return std::make_tuple(wstring_to_utf8(file_system_name),
                           total_bytes.QuadPart, total_free.QuadPart,
                           free_bytes.QuadPart);
  }

  // If GetVolumeInformationW fails, try to just get disk space
  return std::make_tuple("NTFS", total_bytes.QuadPart, total_free.QuadPart,
                         free_bytes.QuadPart);
}

/**
 * @brief Print disk usage information
 * @param ctx Command context
 * @return Result with success status
 */
auto print_disk_usage(const CommandContext<DF_OPTIONS.size()>& ctx)
    -> cp::Result<bool> {
  // Use SmallVector for file paths (max 32 drives) - all stack-allocated
  SmallVector<std::string, 32> paths{};

  if (ctx.positionals.empty()) {
    paths.push_back(".");
  } else {
    for (const auto& arg : ctx.positionals) {
      paths.push_back(std::string(arg));
    }
  }

  bool human =
      ctx.get<bool>("--human-readable", false) || ctx.get<bool>("-h", false);
  bool si = ctx.get<bool>("--si", false) || ctx.get<bool>("-H", false);
  bool kibi = ctx.get<bool>("-k", false);

  bool all_ok = true;

  for (size_t i = 0; i < paths.size(); ++i) {
    const auto& path = paths[i];
    auto disk_info = get_disk_info(path);

    if (!disk_info) {
      safeErrorPrint("df: cannot access '");
      safeErrorPrint(path);
      safeErrorPrint("': No such file or directory\n");
      all_ok = false;
      continue;
    }

    auto [fs_type, total, total_free, free] = *disk_info;
    uint64_t used = total - total_free;

    // Print header (only once)
    static bool header_printed = false;
    if (!header_printed) {
      safePrint("Filesystem     ");
      if (kibi) {
        safePrint("       1K-blocks      Used  Available Capacity");
      } else if (human || si) {
        safePrint("     Size    Used  Available Capacity");
      } else {
        safePrint("           Total        Used    Available Capacity");
      }
      safePrintLn(L" Mounted on");
      header_printed = true;
    }

    // Print filesystem
    safePrint(fs_type);

    // Print sizes
    if (kibi) {
      uint64_t k_total = total / 1024;
      uint64_t k_used = used / 1024;
      uint64_t k_avail = free / 1024;

      char buf[128];
      snprintf(buf, sizeof(buf), "%12ju %7ju %10ju ", k_total, k_used, k_avail);
      safePrint(buf);
    } else if (human || si) {
      char buf[128];
      snprintf(buf, sizeof(buf), "%9s %6s %10s ",
               format_size(total, si).c_str(), format_size(used, si).c_str(),
               format_size(free, si).c_str());
      safePrint(buf);
    } else {
      char buf[128];
      snprintf(buf, sizeof(buf), "%16ju %12ju %12ju ", total, used, free);
      safePrint(buf);
    }

    // Print capacity percentage
    double percent = (total > 0) ? (100.0 * used / total) : 0.0;
    char percent_buf[32];
    snprintf(percent_buf, sizeof(percent_buf), " %.0f%%", percent);
    safePrint(percent_buf);

    // Print mount point (use path itself on Windows)
    safePrintLn(L"  " + utf8_to_wstring(path));
  }

  return all_ok;
}

}  // namespace df_pipeline

REGISTER_COMMAND(
    df,
    /* name */
    "df",

    /* synopsis */
    "report file system disk space usage",

    /* description */
    "The df command displays the amount of available disk space on file\n"
    "systems of which the invoking user has adequate read access.\n\n"
    "On Windows, it reports information about volumes that contain the\n"
    "specified paths, including total size, used space, and available space.",

    /* examples */
    "  df\n"
    "  df -h C:\\Users\n"
    "  df -k",

    /* see_also */
    "du(1)",

    /* author */
    "caomengxuan666",

    /* copyright */
    "Copyright © 2026 WinuxCmd",

    /* options */
    DF_OPTIONS) {
  using namespace df_pipeline;

  auto result = print_disk_usage(ctx);
  if (!result) {
    cp::report_error(result, L"df");
    return 1;
  }

  return *result ? 0 : 1;
}
