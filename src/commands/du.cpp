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
 * - @a -A, @a --apparent-size: print apparent sizes [IMPLEMENTED]
 * - @a -B, @a --block-size=SIZE: scale sizes by SIZE [IMPLEMENTED]
 * - @a -b,
 * @a --bytes: equivalent to --apparent-size --block-size=1 [IMPLEMENTED]
 * - @a -c, @a --total: produce a grand total [IMPLEMENTED]
 * - @a -d, @a
 * --max-depth=N: print the total for a directory only if it is N or fewer
 * levels below [IMPLEMENTED]
 * - @a -h, @a --human-readable: print sizes in powers of 1024 [IMPLEMENTED]
 * - @a -H, @a --dereference-args: dereference only command line symlink args
 * [ACCEPTED]
 * - @a --si: print sizes in powers of 1000 [IMPLEMENTED]
 * - @a -k: like --block-size=1K [IMPLEMENTED]
 * - @a -s, @a --summarize: display only a total for each argument [IMPLEMENTED]
 * - @a -t, @a --threshold=SIZE: exclude entries by size threshold
 * [IMPLEMENTED]
 * - @a --exclude=PATTERN: exclude files matching shell pattern [IMPLEMENTED]
 */
auto constexpr DU_OPTIONS = std::array{
    OPTION("-a", "--all", "write counts for all files, not just directories"),
    OPTION("-A", "--apparent-size", "print apparent sizes"),
    OPTION("-B", "--block-size", "scale sizes by SIZE before printing them",
           STRING_TYPE),
    OPTION("-b", "--bytes", "equivalent to '--apparent-size --block-size=1'"),
    OPTION("-c", "--total", "produce a grand total"),
    OPTION(
        "-d", "--max-depth",
        "print the total for a directory only if it is N or fewer levels below",
        INT_TYPE),
    OPTION("-h", "--human-readable",
           "print sizes in powers of 1024 (e.g., 1023M)"),
    OPTION("-H", "--dereference-args",
           "dereference only symlinks that are command line arguments"),
    OPTION("", "--si", "print sizes in powers of 1000 (e.g., 1.1G)"),
    OPTION("-k", "", "like --block-size=1K"),
    OPTION("-s", "--summarize", "display only a total for each argument"),
    OPTION("-t", "--threshold", "exclude entries smaller/greater than SIZE",
           STRING_TYPE),
    OPTION("", "--exclude", "exclude files that match PATTERN", STRING_TYPE)};

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

auto parse_threshold_size(std::string_view text) -> std::optional<int64_t> {
  if (text.empty()) {
    return std::nullopt;
  }

  bool negative = false;
  if (text.front() == '-' || text.front() == '+') {
    negative = text.front() == '-';
    text.remove_prefix(1);
  }

  if (text == "0") {
    return 0;
  }

  auto parsed = parse_block_size(text);
  if (!parsed ||
      *parsed > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    return std::nullopt;
  }

  int64_t value = static_cast<int64_t>(*parsed);
  return negative ? -value : value;
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
  uint64_t block_size = 1024;
};

enum class ThresholdMode { None, Minimum, Maximum };

struct DuConfig {
  bool count_all = false;
  bool total = false;
  bool summarize = false;
  int max_depth = -1;
  ThresholdMode threshold_mode = ThresholdMode::None;
  uint64_t threshold_size = 0;
  SmallVector<std::string, 16> exclude_patterns;
  OutputConfig output;
};

auto print_scaled_size(uint64_t size, const OutputConfig& cfg) -> void {
  if (cfg.human || cfg.si) {
    safePrint(format_size(size, cfg.si));
    return;
  }

  char buf[32];
  snprintf(buf, sizeof(buf), "%16ju",
           static_cast<uintmax_t>(ceil_div(size, cfg.block_size)));
  safePrint(buf);
}

auto configure_output(const CommandContext<DU_OPTIONS.size()>& ctx)
    -> cp::Result<OutputConfig> {
  OutputConfig output;

  for (const auto& occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= DU_OPTIONS.size()) {
      continue;
    }

    const auto& meta = DU_OPTIONS[occurrence.index];
    if (meta.short_name == "-b" || meta.long_name == "--bytes") {
      output.human = false;
      output.si = false;
      output.block_size = 1;
      continue;
    }

    if (meta.short_name == "-k") {
      output.human = false;
      output.si = false;
      output.block_size = 1024;
      continue;
    }

    if (meta.short_name == "-h" || meta.long_name == "--human-readable") {
      output.human = true;
      output.si = false;
      continue;
    }

    if (meta.long_name == "--si") {
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
    }
  }

  return output;
}

auto configure_du(const CommandContext<DU_OPTIONS.size()>& ctx)
    -> cp::Result<DuConfig> {
  DuConfig cfg;
  cfg.count_all = ctx.get<bool>("--all", false) || ctx.get<bool>("-a", false);
  cfg.total = ctx.get<bool>("--total", false) || ctx.get<bool>("-c", false);
  cfg.summarize =
      ctx.get<bool>("--summarize", false) || ctx.get<bool>("-s", false);

  cfg.max_depth = ctx.get<int>("--max-depth", -1);
  if (ctx.get<int>("-d", -1) != -1) {
    cfg.max_depth = ctx.get<int>("-d", -1);
  }

  auto output = configure_output(ctx);
  if (!output) {
    return std::unexpected(output.error());
  }
  cfg.output = *output;

  for (const auto& occurrence : ctx.string_occurrences({"--threshold", "-t"})) {
    auto threshold = parse_threshold_size(occurrence.value);
    if (!threshold) {
      return std::unexpected("invalid threshold size");
    }

    if (*threshold < 0) {
      cfg.threshold_mode = ThresholdMode::Maximum;
      cfg.threshold_size = static_cast<uint64_t>(-*threshold);
    } else {
      cfg.threshold_mode = ThresholdMode::Minimum;
      cfg.threshold_size = static_cast<uint64_t>(*threshold);
    }
  }

  for (const auto& pattern : ctx.get_all<std::string>("--exclude")) {
    cfg.exclude_patterns.push_back(pattern);
  }

  return cfg;
}

auto normalize_pattern_path(std::string_view text) -> std::string {
  std::string normalized(text);
  if (!normalized.empty() && normalized.front() == '\x01') {
    normalized.erase(normalized.begin());
  }
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  while (!normalized.empty() && normalized.back() == '/') {
    normalized.pop_back();
  }
  return normalized;
}

auto glob_matches_name_suffix(std::string_view pattern, std::string_view name)
    -> bool {
  std::string normalized_pattern = normalize_pattern_path(pattern);
  std::string normalized_name = normalize_pattern_path(name);

  const std::wstring wpattern = utf8_to_wstring(normalized_pattern);
  size_t start = 0;
  while (start <= normalized_name.size()) {
    auto suffix = normalized_name.substr(start);
    if (wildcard_match(wpattern, utf8_to_wstring(suffix))) {
      return true;
    }
    size_t slash = normalized_name.find('/', start);
    if (slash == std::string::npos) break;
    start = slash + 1;
  }
  return false;
}

auto should_exclude(const DuConfig& cfg, const std::wstring& path,
                    const std::wstring& filename) -> bool {
  if (cfg.exclude_patterns.empty()) {
    return false;
  }

  std::string utf8_path = wstring_to_utf8(path);
  std::string utf8_filename = wstring_to_utf8(filename);
  for (const auto& pattern : cfg.exclude_patterns) {
    if (glob_matches_name_suffix(pattern, utf8_filename) ||
        glob_matches_name_suffix(pattern, utf8_path)) {
      return true;
    }
  }
  return false;
}

auto passes_threshold(const DuConfig& cfg, uint64_t size) -> bool {
  if (cfg.threshold_mode == ThresholdMode::Minimum) {
    return size >= cfg.threshold_size;
  }
  if (cfg.threshold_mode == ThresholdMode::Maximum) {
    return size <= cfg.threshold_size;
  }
  return true;
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
                        int current_depth, const DuConfig& cfg) -> uint64_t {
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

    if (should_exclude(cfg, full_path, filename)) {
      continue;
    }

    const int child_depth = current_depth + 1;

    if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      // Recursively calculate subdirectory size
      uint64_t dir_size =
          calculate_dir_size(full_path, sizes, child_depth, cfg);
      total_size += dir_size;
    } else {
      // It's a file
      uint64_t file_size = get_file_size(full_path);
      total_size += file_size;

      // Count individual files if requested
      if (cfg.count_all &&
          (cfg.max_depth < 0 || child_depth <= cfg.max_depth)) {
        sizes[full_path] = file_size;
      }
    }
  } while (FindNextFileW(hFind, &find_data) != 0);

  FindClose(hFind);

  if (cfg.max_depth < 0 || current_depth <= cfg.max_depth) {
    sizes[path] = total_size;
  }

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

  auto configured = configure_du(ctx);
  if (!configured) {
    return std::unexpected(configured.error());
  }
  DuConfig cfg = *configured;

  bool all_ok = true;
  uint64_t grand_total = 0;

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

    if (should_exclude(cfg, wpath,
                       std::filesystem::path(wpath).filename().wstring())) {
      continue;
    }

    std::unordered_map<std::wstring, uint64_t> sizes;

    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
      // Calculate directory size
      calculate_dir_size(wpath, sizes, 0, cfg);

      // Print directory size
      uint64_t dir_size = sizes[wpath];
      grand_total += dir_size;

      if (passes_threshold(cfg, dir_size)) {
        safePrint(L"");
        print_scaled_size(dir_size, cfg.output);
        safePrint(L"  ");
        safePrintLn(wpath);
      }

      // Print subdirectories/files if not summarize mode
      if (!cfg.summarize) {
        for (const auto& [subpath, size] : sizes) {
          if (subpath != wpath) {  // Skip the root directory itself
            if (!passes_threshold(cfg, size)) {
              continue;
            }
            safePrint(L"");
            print_scaled_size(size, cfg.output);
            safePrint(L"  ");
            safePrintLn(subpath);
          }
        }
      }
    } else {
      // It's a file
      uint64_t file_size = get_file_size(wpath);
      grand_total += file_size;

      if (passes_threshold(cfg, file_size)) {
        safePrint(L"");
        print_scaled_size(file_size, cfg.output);
        safePrint(L"  ");
        safePrintLn(wpath);
      }
    }
  }

  if (cfg.total && passes_threshold(cfg, grand_total)) {
    safePrint(L"");
    print_scaled_size(grand_total, cfg.output);
    safePrintLn(L"  total");
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
