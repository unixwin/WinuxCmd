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
 * - @a -i, @a --inodes: list inode information instead of block usage
 * [IMPLEMENTED]
 * - @a -k: like --block-size=1K [IMPLEMENTED]
 * - @a -T, @a --print-type: print file system type [IMPLEMENTED]
 * - @a -B, @a --block-size=SIZE: scale sizes by SIZE [IMPLEMENTED]
 * - @a --total: produce a grand total [IMPLEMENTED]
 * - @a -a, @a --all: include all file systems [ACCEPTED]
 * - @a --sync, @a --no-sync: sync control [ACCEPTED]
 */
auto constexpr DF_OPTIONS = std::array{
    OPTION("-a", "--all", "include all file systems"),
    OPTION("-B", "--block-size", "scale sizes by SIZE before printing them",
           STRING_TYPE),
    OPTION("-h", "--human-readable",
           "print sizes in powers of 1024 (e.g., 1023M)"),
    OPTION("-H", "--si", "print sizes in powers of 1000 (e.g., 1.1G)"),
    OPTION("-i", "--inodes", "list inode information instead of block usage"),
    OPTION("-k", "", "like --block-size=1K"),
    OPTION("-T", "--print-type", "print file system type"),
    OPTION("", "--total", "produce a grand total"),
    OPTION("", "--sync", "invoke sync before getting usage info"),
    OPTION("", "--no-sync", "do not invoke sync before getting usage info")};

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

auto pow_u64(uint64_t base, int exponent) -> std::optional<uint64_t> {
  uint64_t value = 1;
  for (int i = 0; i < exponent; ++i) {
    if (value > std::numeric_limits<uint64_t>::max() / base) {
      return std::nullopt;
    }
    value *= base;
  }
  return value;
}

auto parse_block_size(std::string_view text) -> std::optional<uint64_t> {
  if (text.empty()) {
    return std::nullopt;
  }

  struct Unit {
    std::string_view suffix;
    uint64_t multiplier;
  };

  const std::array units{Unit{"KiB", *pow_u64(1024, 1)},
                         Unit{"MiB", *pow_u64(1024, 2)},
                         Unit{"GiB", *pow_u64(1024, 3)},
                         Unit{"TiB", *pow_u64(1024, 4)},
                         Unit{"PiB", *pow_u64(1024, 5)},
                         Unit{"EiB", *pow_u64(1024, 6)},
                         Unit{"KB", *pow_u64(1000, 1)},
                         Unit{"MB", *pow_u64(1000, 2)},
                         Unit{"GB", *pow_u64(1000, 3)},
                         Unit{"TB", *pow_u64(1000, 4)},
                         Unit{"PB", *pow_u64(1000, 5)},
                         Unit{"EB", *pow_u64(1000, 6)},
                         Unit{"kiB", *pow_u64(1024, 1)},
                         Unit{"miB", *pow_u64(1024, 2)},
                         Unit{"giB", *pow_u64(1024, 3)},
                         Unit{"tiB", *pow_u64(1024, 4)},
                         Unit{"piB", *pow_u64(1024, 5)},
                         Unit{"eiB", *pow_u64(1024, 6)},
                         Unit{"kb", *pow_u64(1000, 1)},
                         Unit{"mb", *pow_u64(1000, 2)},
                         Unit{"gb", *pow_u64(1000, 3)},
                         Unit{"tb", *pow_u64(1000, 4)},
                         Unit{"pb", *pow_u64(1000, 5)},
                         Unit{"eb", *pow_u64(1000, 6)},
                         Unit{"K", *pow_u64(1024, 1)},
                         Unit{"M", *pow_u64(1024, 2)},
                         Unit{"G", *pow_u64(1024, 3)},
                         Unit{"T", *pow_u64(1024, 4)},
                         Unit{"P", *pow_u64(1024, 5)},
                         Unit{"E", *pow_u64(1024, 6)},
                         Unit{"k", *pow_u64(1024, 1)},
                         Unit{"m", *pow_u64(1024, 2)},
                         Unit{"g", *pow_u64(1024, 3)},
                         Unit{"t", *pow_u64(1024, 4)},
                         Unit{"p", *pow_u64(1024, 5)},
                         Unit{"e", *pow_u64(1024, 6)},
                         Unit{"b", 512},
                         Unit{"B", 1}};

  uint64_t multiplier = 1;
  std::string_view number = text;
  bool suffix_matched = false;
  for (const auto& unit : units) {
    if (text.size() >= unit.suffix.size() &&
        text.substr(text.size() - unit.suffix.size()) == unit.suffix) {
      multiplier = unit.multiplier;
      number = text.substr(0, text.size() - unit.suffix.size());
      suffix_matched = true;
      break;
    }
  }

  uint64_t value = 1;
  if (!number.empty()) {
    auto [ptr, ec] =
        std::from_chars(number.data(), number.data() + number.size(), value);
    if (ec != std::errc() || ptr != number.data() + number.size()) {
      return std::nullopt;
    }
  } else if (!suffix_matched) {
    return std::nullopt;
  }

  if (value == 0 || value > std::numeric_limits<uint64_t>::max() / multiplier) {
    return std::nullopt;
  }
  return value * multiplier;
}

auto ceil_div(uint64_t value, uint64_t divisor) -> uint64_t {
  if (value == 0) {
    return 0;
  }
  return 1 + ((value - 1) / divisor);
}

struct OutputConfig {
  bool human = false;
  bool si = false;
  uint64_t block_size = 1;
  std::string block_label = "Total";
};

auto block_label_for(std::string_view value) -> std::string {
  if (value.empty()) {
    return "blocks";
  }

  bool all_digits = true;
  for (char ch : value) {
    if (ch < '0' || ch > '9') {
      all_digits = false;
      break;
    }
  }

  std::string label;
  if (all_digits) {
    label.assign(value);
    label += "B";
  } else if (value.front() < '0' || value.front() > '9') {
    label = "1";
    label.append(value.data(), value.size());
  } else {
    label.assign(value);
  }
  label += "-blocks";
  return label;
}

/**
 * @brief Get disk free space information
 * @param path Path to check (any file/directory on the volume)
 * @return Disk info or error message
 */
struct DiskInfo {
  std::string filesystem;
  std::string type;
  std::string mount_point;
  uint64_t total = 0;
  uint64_t total_free = 0;
  uint64_t available = 0;
};

auto get_disk_info(const std::string& path) -> std::optional<DiskInfo> {
  std::wstring wpath = utf8_to_wstring(path);
  std::vector<wchar_t> full_path(MAX_PATH);
  DWORD full_len =
      GetFullPathNameW(wpath.c_str(), static_cast<DWORD>(full_path.size()),
                       full_path.data(), nullptr);
  if (full_len == 0) {
    return std::nullopt;
  }
  if (full_len >= full_path.size()) {
    full_path.resize(full_len + 1);
    full_len =
        GetFullPathNameW(wpath.c_str(), static_cast<DWORD>(full_path.size()),
                         full_path.data(), nullptr);
    if (full_len == 0 || full_len >= full_path.size()) {
      return std::nullopt;
    }
  }

  std::vector<wchar_t> volume_path(MAX_PATH);
  if (!GetVolumePathNameW(full_path.data(), volume_path.data(),
                          static_cast<DWORD>(volume_path.size()))) {
    return std::nullopt;
  }

  ULARGE_INTEGER free_bytes, total_bytes, total_free;

  if (!GetDiskFreeSpaceExW(volume_path.data(), &free_bytes, &total_bytes,
                           &total_free)) {
    return std::nullopt;
  }

  // Get volume information
  wchar_t volume_name[MAX_PATH];
  wchar_t file_system_name[MAX_PATH] = {0};
  std::string fs_type = "unknown";

  if (GetVolumeInformationW(volume_path.data(), volume_name, MAX_PATH, nullptr,
                            nullptr, nullptr, file_system_name, MAX_PATH)) {
    fs_type = wstring_to_utf8(file_system_name);
  }

  return DiskInfo{.filesystem = wstring_to_utf8(volume_path.data()),
                  .type = fs_type,
                  .mount_point = wstring_to_utf8(volume_path.data()),
                  .total = total_bytes.QuadPart,
                  .total_free = total_free.QuadPart,
                  .available = free_bytes.QuadPart};
}

auto print_usage_header(const OutputConfig& output, bool print_type,
                        bool inodes) -> void {
  safePrint("Filesystem     ");
  if (print_type) {
    safePrint("Type       ");
  }

  if (inodes) {
    safePrintLn("      Inodes      IUsed      IFree IUse% Mounted on");
    return;
  }

  if (output.human || output.si) {
    safePrint("     Size    Used  Available Capacity");
  } else {
    char buf[128];
    snprintf(buf, sizeof(buf), "%16s        Used    Available Capacity",
             output.block_label.c_str());
    safePrint(buf);
  }
  safePrintLn(L" Mounted on");
}

auto print_size_columns(uint64_t total, uint64_t used, uint64_t available,
                        const OutputConfig& output) -> void {
  if (output.human || output.si) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%9s %6s %10s ",
             format_size(total, output.si).c_str(),
             format_size(used, output.si).c_str(),
             format_size(available, output.si).c_str());
    safePrint(buf);
  } else {
    char buf[128];
    snprintf(buf, sizeof(buf), "%16ju %12ju %12ju ",
             static_cast<uintmax_t>(ceil_div(total, output.block_size)),
             static_cast<uintmax_t>(ceil_div(used, output.block_size)),
             static_cast<uintmax_t>(ceil_div(available, output.block_size)));
    safePrint(buf);
  }
}

auto configure_output(const CommandContext<DF_OPTIONS.size()>& ctx)
    -> cp::Result<OutputConfig> {
  OutputConfig output;

  for (const auto& occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= DF_OPTIONS.size()) {
      continue;
    }

    const auto& meta = DF_OPTIONS[occurrence.index];
    if (meta.short_name == "-k") {
      output.human = false;
      output.si = false;
      output.block_size = 1024;
      output.block_label = "1K-blocks";
      continue;
    }

    if (meta.short_name == "-h" || meta.long_name == "--human-readable") {
      output.human = true;
      output.si = false;
      continue;
    }

    if (meta.short_name == "-H" || meta.long_name == "--si") {
      output.human = false;
      output.si = true;
      continue;
    }

    if (meta.short_name == "-B" || meta.long_name == "--block-size") {
      auto value = std::get_if<std::string>(&occurrence.value);
      if (!value) {
        return std::unexpected("invalid block size");
      }
      if (*value == "human-readable") {
        output.human = true;
        output.si = false;
        continue;
      }
      if (*value == "si") {
        output.human = false;
        output.si = true;
        continue;
      }

      auto parsed = parse_block_size(*value);
      if (!parsed) {
        return std::unexpected("invalid block size");
      }
      output.human = false;
      output.si = false;
      output.block_size = *parsed;
      output.block_label = block_label_for(*value);
    }
  }

  return output;
}

auto print_total_row(uint64_t total, uint64_t used, uint64_t available,
                     const OutputConfig& output, bool print_type, bool inodes)
    -> void {
  safePrint("total");
  if (print_type) {
    safePrint(" -");
  }

  if (inodes) {
    safePrint("            -          -          -     -");
  } else {
    print_size_columns(total, used, available, output);

    double percent = (total > 0) ? (100.0 * used / total) : 0.0;
    char percent_buf[32];
    snprintf(percent_buf, sizeof(percent_buf), " %.0f%%", percent);
    safePrint(percent_buf);
  }

  safePrintLn("  total");
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

  auto output = configure_output(ctx);
  if (!output) {
    return std::unexpected(output.error());
  }

  bool print_type =
      ctx.get<bool>("--print-type", false) || ctx.get<bool>("-T", false);
  bool inodes = ctx.get<bool>("--inodes", false) || ctx.get<bool>("-i", false);
  bool total = ctx.get<bool>("--total", false);

  bool all_ok = true;
  bool header_printed = false;
  uint64_t total_size = 0;
  uint64_t total_used = 0;
  uint64_t total_available = 0;
  size_t printed_rows = 0;

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

    const auto& info = *disk_info;
    uint64_t used = info.total - info.total_free;

    // Print header (only once)
    if (!header_printed) {
      print_usage_header(*output, print_type, inodes);
      header_printed = true;
    }

    // Print filesystem
    safePrint(info.filesystem);
    if (print_type) {
      safePrint(" ");
      safePrint(info.type);
    }

    if (inodes) {
      safePrint("            -          -          -     -");
    } else {
      print_size_columns(info.total, used, info.available, *output);

      // Print capacity percentage
      double percent = (info.total > 0) ? (100.0 * used / info.total) : 0.0;
      char percent_buf[32];
      snprintf(percent_buf, sizeof(percent_buf), " %.0f%%", percent);
      safePrint(percent_buf);
    }

    // Print mount point (use path itself on Windows)
    safePrintLn(L"  " + utf8_to_wstring(info.mount_point));
    total_size += info.total;
    total_used += used;
    total_available += info.available;
    ++printed_rows;
  }

  if (total && header_printed && printed_rows > 0) {
    print_total_row(total_size, total_used, total_available, *output,
                    print_type, inodes);
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
